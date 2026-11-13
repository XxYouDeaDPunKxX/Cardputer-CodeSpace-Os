"""Refine the complete JPEG rectangles painted during motion, not just dirties."""
import struct
import time
from collections import OrderedDict

REFINE_BUDGET = 4096
CXL_JPEG_HEADER = 13


def choose_region_encoding(base, frame, tiles, q, sub, maxjpeg):
    """Use exact runs only if bytes, painted pixels AND JPEG count do not rise."""
    coarse=base.tiles_to_rects(tiles,frame.height)
    active={};exact=[]
    tw,th=base.TILE_W,base.TILE_H
    for row in range(frame.height//th+1):
        spans=[]
        for col in sorted(x for x,y in tiles if y==row):
            if spans and spans[-1][1]==col:spans[-1]=(spans[-1][0],col+1)
            else:spans.append((col,col+1))
        spans=set(spans)
        for span in list(active):
            if span not in spans:
                exact.append((span[0]*tw,active.pop(span)*th,span[1]*tw,row*th))
        for span in spans:active.setdefault(span,row)
    exact.sort()
    def encode(rects):
        return [part for rect in rects for part in
                base.encode_rect(frame,rect,q,sub,maxjpeg,with_settings=True)]
    old=encode(coarse)
    if exact==coarse:return coarse,old
    # The alternative cannot eliminate overdraw if both cover the same area.
    area=lambda rects:sum((r-l)*(b-t) for l,t,r,b in rects)
    if area(exact)>=area(coarse):return coarse,old
    new=encode(exact)
    cost=lambda parts:(sum(13+len(p[1]) for p in parts),
                       area([p[0] for p in parts]),len(parts))
    old_cost,new_cost=cost(old),cost(new)
    if all(a<=b for a,b in zip(new_cost,old_cost)) and new_cost!=old_cost:
        return exact,new
    return coarse,old


def with_quality_refinement(Session, tile_width=32, tile_height=16, *, base=None, clock=None):
    if base is not None:
        return _regional_quality(Session, base, tile_width, tile_height, clock or time.monotonic)
    class QualitySession(Session):
        def send(self, data):
            if data[:1] == b'T' and len(data) >= 13:
                with self.lock:
                    if self.lowq:
                        x, y, width, height = struct.unpack_from('<HHHH', data, 1)
                        # The upstream merges dirty tiles into larger rectangles.
                        # Their clean gaps are re-encoded too and must not escape
                        # the subsequent sharp pass. Conservatively include sharp
                        # rectangles sent while a refinement is already pending.
                        right = min((self.ow+tile_width-1)//tile_width,
                                    (x+width+tile_width-1)//tile_width)
                        bottom = min((self.oh+tile_height-1)//tile_height,
                                     (y+height+tile_height-1)//tile_height)
                        if width and height:
                            self.lowq.update((col, row)
                                for row in range(y//tile_height, bottom)
                                for col in range(x//tile_width, right))
            return super().send(data)

    return QualitySession


def _regional_quality(Session, base, tw, th, clock):
    class RegionalQualitySession(Session):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            self.region_counts = {phase:dict.fromkeys(('content','jpeg_refine','lossless'),0)
                                  for phase in ('planned','sent','completed')}
            self.region_pending = OrderedDict()
            self.region_batches = {'sent':0,'completed':0}
            self._region_geometry = None
            self._region_last_change = {}
            self._region_motion_at = {}
            self._region_previous_cursor = set()
            self._region_causes = set()
            self._region_refine_bytes = 0
            self.region_bytes = dict.fromkeys(('content','jpeg_refine','lossless'),0)
            self._region_encodings = {}
            self.region_geometry_choices = 0

        def _region_rectangles(self, frame, tiles, q, sub):
            if not getattr(self.server,'adaptive_rectangles',False):
                return base.tiles_to_rects(tiles,self.oh)
            rectangles,encoded=choose_region_encoding(base,frame,tiles,q,sub,self.maxjpeg)
            if rectangles!=base.tiles_to_rects(tiles,self.oh):self.region_geometry_choices+=1
            self._region_encodings[(tuple(rectangles),q,sub,self.maxjpeg)]=encoded
            return rectangles

        def encode_regions(self, frame, rectangles, q, sub, maxjpeg, *, cause='content'):
            if cause=='jpeg_refine':maxjpeg=min(maxjpeg,REFINE_BUDGET-CXL_JPEG_HEADER)
            key=(tuple(rectangles),q,sub,maxjpeg)
            if key in self._region_encodings:return self._region_encodings.pop(key)
            return [part for rect in rectangles for part in
                    base.encode_rect(frame,rect,q,sub,maxjpeg,with_settings=True)]

        def render(self, *args):
            result = super().render(*args)
            geometry = (getattr(self,'_touch_pending',None),*result[1:],self.quality)
            if geometry != self._region_geometry:
                self._region_geometry = geometry
                self.lowq.clear()
                self._region_last_change.clear()
                self._region_motion_at.clear()
                self._region_previous_cursor.clear()
            return result

        def _region_coverage(self, rectangle):
            x,y,right,bottom = rectangle
            return {(col,row) for row in range(y//th,(bottom+th-1)//th)
                    for col in range(x//tw,(right+tw-1)//tw)
                    if 0<=col<(self.ow+tw-1)//tw and 0<=row<(self.oh+th-1)//th}

        def _region_cursor(self):
            geometry=getattr(self,'_touch_pending',None)
            if not self.server.cfg['cursor'] or geometry is None:return set()
            _,_,dx,dy,dw,dh,left,top,right,bottom=geometry
            cx,cy=self.inp.cursor_pos()
            x=round(dx+(cx-left)*dw/(right-left))
            y=round(dy+(cy-top)*dh/(bottom-top))
            if not (0<=x<self.ow and 0<=y<self.oh):return set()
            right=x+max(p[0] for p in base.CURSOR_SHAPE)+1
            bottom=y+max(p[1] for p in base.CURSOR_SHAPE)+1
            return self._region_coverage((x,y,right,bottom))

        def plan_regions(self, frame, dirty, now, full):
            q,sub = base.QUALITY[self.quality]
            self._region_refine_bytes = 0
            self._region_encodings.clear()
            cursor = self._region_cursor()
            actual_dirty = dirty - cursor - self._region_previous_cursor
            recent = {tile for tile in actual_dirty
                      if tile in self._region_motion_at
                      and now-self._region_motion_at[tile] <= .5}
            for tile in dirty:self._region_last_change[tile] = now
            if not full:
                for tile in actual_dirty:self._region_motion_at[tile]=now
            self._region_previous_cursor = cursor
            if full:
                self.lowq.clear()
                batches=[([(0,0,self.ow,self.oh)],q,sub,'content')]
            else:
                motion = actual_dirty if len(actual_dirty)>self.ow//tw*(self.oh//th)*.3 else recent
                still = dirty-motion
                batches=[]
                motion_q=max(25,q-base.MOTION_QUALITY_DROP)
                motion_rectangles=self._region_rectangles(frame,motion,motion_q,2) if motion else []
                motion_area=sum((right-left)*(bottom-top) for left,top,right,bottom in motion_rectangles)
                if motion_rectangles:batches.append((motion_rectangles,motion_q,2,'content'))
                if still:batches.append((self._region_rectangles(frame,still,q,sub),q,sub,'content'))
                # Oldest eligible tiles first. At most one existing 320x16 band
                # of candidate pixels; actual emitted repair bytes are capped too.
                # During broad motion, repairs of stable islands would be
                # immediately overwritten by the next motion rectangle. Reserve
                # that bandwidth/decode work for content; local motion still
                # permits regional refinement, and broad motion recovers on stop.
                eligible = sorted((tile for tile in self.lowq-dirty
                                   if now-self._region_last_change.get(tile,now)>=base.REFINE_AFTER),
                                  key=lambda tile:(self._region_last_change.get(tile,now),tile[1],tile[0])) if motion_area<=self.ow*self.oh*.3 else []
                selected=eligible[:max(1,self.ow//tw)]
                if selected:
                    # A repair never includes clean gaps or a moving region.
                    rectangles=[]
                    for x,y in sorted(selected,key=lambda tile:(tile[1],tile[0])):
                        right,bottom=min(self.ow,(x+1)*tw),min(self.oh,(y+1)*th)
                        if rectangles and rectangles[-1][1]==y*th and rectangles[-1][2]==x*tw:
                            left,top,_,_=rectangles[-1]
                            rectangles[-1]=(left,top,right,bottom)
                        else:rectangles.append((x*tw,y*th,right,bottom))
                    batches.append((rectangles,q,sub,'jpeg_refine'))
            for cause in {batch[3] for batch in batches}:
                self.region_counts['planned'][cause]+=1
            return batches

        def permit_region_record(self, cause, size):
            return cause!='jpeg_refine' or self._region_refine_bytes+size<=REFINE_BUDGET

        def _region_complete(self):
            for fid,item in list(self.region_pending.items()):
                if item['sent'] and fid<=self.acked_id:
                    for cause in item['causes']:self.region_counts['completed'][cause]+=1
                    self.region_batches['completed']+=1
                    del self.region_pending[fid]

        def send(self, data):
            context = getattr(self,'_region_record_context',None)
            if data[:1]==b'T' and context is None:
                raise OSError('regional JPEG lacks encoding context')
            fid = struct.unpack_from('<I',data,1)[0] if data[:1]==b'F' and len(data)==12 else None
            if fid is not None:
                with self.lock:
                    self._region_complete()
                    if len(self.region_pending)>=base.MAX_INFLIGHT:
                        raise OSError('regional in-flight bound exceeded')
                    self.region_pending[fid]={'sent':False,'causes':set(self._region_causes)}
            try:
                result=super().send(data)
            except OSError:
                if fid is not None:
                    with self.lock:self.region_pending.pop(fid,None)
                raise
            with self.lock:
                if data[:1]==b'T':
                    cause,q,sub=context
                    x,y,w,h,_=struct.unpack_from('<HHHHI',data,1)
                    cover=self._region_coverage((x,y,x+w,y+h))
                    for tile in cover:self._region_last_change.setdefault(tile,clock())
                    if (q,sub)==base.QUALITY[self.quality]:self.lowq-=cover
                    else:self.lowq|=cover
                    self._region_causes.add(cause)
                    self.region_bytes[cause]+=len(data)
                    if cause=='jpeg_refine':self._region_refine_bytes+=len(data)
                elif data[:1]==b'J':
                    if 'lossless' not in self._region_causes:
                        self.region_counts['planned']['lossless']+=1
                    self._region_causes.add('lossless')
                    self.region_bytes['lossless']+=len(data)
                if fid is not None:
                    item=self.region_pending[fid];item['sent']=True
                    self.region_batches['sent']+=1
                    for cause in item['causes']:self.region_counts['sent'][cause]+=1
                    self._region_causes.clear()
                    self._region_complete()
            return result

        def trace_fps(self, event, now):
            super().trace_fps(event,now)
            if not getattr(self.server,'fps_probe',False):return
            with self.lock:
                fields=' '.join(f'{phase}_{kind}={count}'
                    for phase,counts in self.region_counts.items()
                    for kind,count in counts.items())
                fields+=' '+ ' '.join(f'bytes_{kind}={count}' for kind,count in self.region_bytes.items())
                fields+=' '+ ' '.join(f'batches_{kind}={count}' for kind,count in self.region_batches.items())
                fields+=f' geometry_choices={self.region_geometry_choices}'
                line=(f'CXL_REGIONAL event={event} mono={now:.3f} '
                      f'session={self.addr[1]}-{self.connected_at:.3f} '
                      f'pending={len(self.region_pending)} lowq={len(self.lowq)} {fields}')
            base.log(line)

        def handle(self, kind, payload):
            if kind==ord('A') and len(payload)==4:
                fid=struct.unpack('<I',payload)[0]
                with self.lock:
                    if fid>self.sent_id:return
            result=super().handle(kind,payload)
            if kind==ord('A'):
                with self.lock:self._region_complete()
            return result

    return RegionalQualitySession
