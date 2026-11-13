"""Deterministic replay through production run/render and composed wrappers."""
import importlib.util,io,pathlib,random,struct,tempfile,threading,types,unittest
from unittest.mock import patch
from PIL import Image,ImageDraw

ROOT=pathlib.Path(__file__).resolve().parents[2]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def replay(regional=True, changing=True, geometry=False, quality_mode=1, early_ack=False, fail_at=None, ack_every=1, textured=False, scene='motion', quality_path=None, adaptive=False, maxjpeg=4096):
    base=load('regional_base',ROOT/'server/cxl_remote_server.py')
    touch=load('regional_touch',ROOT/'csos-client/server/csos_touch.py')
    quality=load('regional_quality',quality_path or ROOT/'csos-client/server/csos_quality.py')
    video=load('regional_video',ROOT/'csos-client/server/csos_video.py')
    presence=load('regional_presence',ROOT/'csos-client/server/csos_presence.py')
    audio=load('regional_audio',ROOT/'csos-client/server/csos_audio.py')
    class Clock:
        now=100.
        def monotonic(self):return self.now
        def time(self):return self.now
        def sleep(self,delay):self.now+=delay
    clock=Clock();wire=[];snapshots=[];frames=[];index=[0];session=[None];records=[];errors=[];stream_errors=[]
    class Input:
        def cursor_pos(self):return (50+index[0]*3,200) if scene=='cursor' else (300,220)
        def set_cursor(self,*args):pass
        def button(self,*args):pass
        def key(self,*args):pass
    class Capture:
        monitors=[dict(left=0,top=0,width=640,height=480)]*2
        def __enter__(self):return self
        def __exit__(self,*args):pass
        def grab(self,rect):
            step=index[0];index[0]+=1
            image=Image.new('RGB',(320,240),(185,185,185));draw=ImageDraw.Draw(image)
            for y in range(4):
                for x in range(5):
                    phase=(step if step<16 or 20<=step<26 else (16 if step<20 else 26)) if changing else 0
                    draw.rectangle((x*32,y*16,x*32+31,y*16+15),fill=(40+phase*7,70+x*6,100+y*9))
            if textured:
                image.paste(Image.frombytes('RGB',(160,64),random.Random(phase).randbytes(160*64*3)),(0,0))
            if scene=='columns':
                image=Image.frombytes('RGB',(320,240),random.Random(71).randbytes(320*240*3))
                for x in (0,288):
                    image.paste(Image.frombytes('RGB',(32,240),random.Random(phase+x).randbytes(32*240*3)),(x,0))
            # This region moves briefly, then must recover while the video moves.
            level=100 if not changing or step<3 else (110+step*15 if step<6 else 230)
            draw.rectangle((224,128,287,143),fill=(level,level,level))
            if scene=='scroll':image=Image.new('RGB',(320,240),(40+min(step,26)*5,80,120)) if step<26 else Image.new('RGB',(320,240),(170,80,120))
            if scene=='scroll':ImageDraw.Draw(image).rectangle((160,96,287,175),fill=(230,230,230))
            if scene=='refine_noise':image=Image.frombytes('RGB',(320,240),random.Random(min(step,6)).randbytes(320*240*3))
            if step>=({'scroll':52,'columns':100,'refine_noise':100}.get(scene,36)):session[0].alive=False
            frames.append(image)
            image=image.resize((640,480),Image.Resampling.NEAREST)
            crop=image.crop((rect['left'],rect['top'],rect['left']+rect['width'],rect['top']+rect['height']))
            return types.SimpleNamespace(size=crop.size,bgra=crop.tobytes('raw','BGRX'))
    class Wake:
        def wait(self,delay):
            clock.now+=max(.1,delay)
            s=session[0]
            if s:
                old=s.acked_id;s.handle(ord('A'),struct.pack('<I',s.sent_id+100))
                assert s.acked_id==old
                assert 0<=s.acked_id<=s.sent_id
                if index[0]%ack_every==0:
                    s.handle(ord('A'),struct.pack('<I',s.sent_id))
                    s.handle(ord('A'),struct.pack('<I',s.sent_id)) # repeated cumulative ACK
                snapshots.append((index[0],set(s.lowq),len(getattr(s,'region_pending',{}))))
                if geometry and index[0]==10:s.handle(ord('Z'),b'\x01')
                if geometry and index[0]==12:s.handle(ord('V'),struct.pack('<Ihh',s.sent_id,16,0))
                if geometry and index[0]==15:s.handle(ord('Q'),b'\x02')
        def clear(self):pass
        def set(self):pass
    class Thread:
        def __init__(self,*args,**kwargs):pass
        def start(self):pass
    class Socket:
        def sendall(self,data):
            if fail_at is not None and index[0]>=fail_at and data[:1]==b'T':raise OSError('replay send failure')
            wire.append((index[0],bytes(data)))
            if data[:1]==b'T':records.append((index[0],bytes(data),getattr(session[0],'_region_record_context',None)))
            if early_ack and data[:1]==b'F':session[0].handle(ord('A'),data[1:5])
        def close(self):pass
        def shutdown(self,*args):pass
    server=types.SimpleNamespace(inp=Input(),monitor=0,view_only=False,matrix=None,
                                adaptive_rectangles=adaptive,
                                cfg=dict(stretch=True,fps=10,audio=False,cursor=scene=='cursor'),remember=lambda **kw:None)
    with tempfile.TemporaryDirectory() as folder:
        cls=touch.absolute_session(base)
        cls=presence.with_presence(cls,pathlib.Path(folder)/'lease.json',clock=clock.time)
        if regional:
            cls=quality.with_quality_refinement(cls,32,16,base=base,clock=clock.monotonic)
        else:cls=quality.with_quality_refinement(cls,32,16)
        cls=video.with_lossless_video(cls,base.tiles_to_rects,clock=clock.monotonic)
        cls=audio.with_audio(cls)
        class Trial(cls):
            def handshake(self,*args):
                self.maxjpeg=maxjpeg;self.video_enabled=True;self.quality=quality_mode
        threaded=types.SimpleNamespace(Thread=Thread,Condition=threading.Condition,Lock=threading.Lock,Event=threading.Event)
        def log(*args):
            if args and args[0]=='stream failed:':stream_errors.append(str(args))
            if args and args[0]=='capture failed:':
                errors.append(str(args));session[0].alive=False
        with patch.object(base,'time',clock),patch.object(base,'log',log),patch.object(base,'threading',threaded),patch.dict('sys.modules',{'mss':types.SimpleNamespace(MSS=Capture)}):
            s=Trial(Socket(),('replay',0),server);session[0]=s;s.wake=Wake();s.run()
    if errors:raise AssertionError(errors)
    return types.SimpleNamespace(session=s,wire=wire,snapshots=snapshots,frames=frames,records=records,stream_errors=stream_errors)

class RegionalReplay(unittest.TestCase):
    def test_8k_interactive_jpegs_refine_progressively_inside_4k_budget(self):
        for mode in (1,2):
            with self.subTest(mode=mode):
                run=replay(scene='refine_noise',quality_mode=mode,maxjpeg=8192,adaptive=True)
                self.assertTrue(any(len(p)>4096 and c[0]=='content' for _,p,c in run.records))
                self.assertTrue(any(low for _,low,_ in run.snapshots),'motion must create lowq work')
                self.assertFalse(run.session.lowq,'all pending regions must eventually recover')
                repairs={};low=set()
                for step,p,context in run.records:
                    x,y,w,h,n=struct.unpack_from('<HHHHI',p,1)
                    self.assertEqual(n,len(p)-13)
                    self.assertTrue(p.endswith(b'\xff\xd9'),'truncated JPEG')
                    decoded=Image.open(io.BytesIO(p[13:]));decoded.load()
                    self.assertEqual(decoded.size,(w,h))
                    cover={(col,row) for col in range(x//32,(x+w+31)//32) for row in range(y//16,(y+h+15)//16)}
                    if context[1:]==((45,2),(70,0),(88,0))[mode]:low-=cover
                    else:low|=cover
                    if context[0]=='jpeg_refine':
                        self.assertEqual(context[1:],((70,0),(88,0))[mode-1])
                        repairs[step]=repairs.get(step,0)+len(p)
                self.assertTrue(repairs)
                self.assertTrue(all(n<=4096 for n in repairs.values()))
                self.assertEqual(low,run.session.lowq,'only sent records may clear quality debt')
                self.assertEqual(run.session.region_counts['sent'],run.session.region_counts['completed'])

    def test_unencodable_minimum_rectangle_is_not_truncated(self):
        base=load('unencodable_base',ROOT/'server/cxl_remote_server.py')
        image=Image.new('RGB',(32,16),(10,20,30))
        for settings in (False,True):
            with self.assertRaisesRegex(OSError,'JPEG'):
                base.encode_rect(image,(0,0,32,16),10,2,16,with_settings=settings)

    def test_unencodable_stream_stops_instead_of_retrying_or_sending_partial_jpeg(self):
        run=replay(scene='refine_noise',maxjpeg=16)
        self.assertFalse(run.session.alive)
        self.assertFalse(run.records)
        self.assertEqual(len(run.frames),1,'failed encoding must not loop forever')
        self.assertEqual(len(run.stream_errors),1)
        self.assertIn('JPEG',run.stream_errors[0])

    def test_legacy_composition_rejects_future_ack_before_valid_ack(self):
        run=replay(regional=False,ack_every=2)
        self.assertGreater(run.session.sent_id,2)
        self.assertEqual(run.session.acked_id,run.session.sent_id)
    def test_adaptive_composed_path_reduces_overdraw_without_losing_ack_or_recovery(self):
        old=replay(scene='columns');new=replay(scene='columns',adaptive=True)
        def cost(run):
            records=[p for step,p,c in run.records if 2<=step<16 and c[0]=='content']
            return sum(map(len,records)),sum((lambda r:r[0]*r[1])(struct.unpack_from('<HH',p,5)) for p in records),len(records)
        self.assertTrue(all(a<b for a,b in zip(cost(new),cost(old))))
        self.assertGreater(new.session.region_geometry_choices,0)
        self.assertFalse(new.session.lowq)
        self.assertEqual(new.session.region_counts['sent'],new.session.region_counts['completed'])
        view=replay(adaptive=True,geometry=True,early_ack=True)
        self.assertFalse(view.session.region_pending)
        self.assertEqual(view.session.zoom_idx,1)
    def test_persistent_local_motion_and_static_recovery(self):
        baseline=replay(False);candidate=replay(True)
        def observed(run):
            result=[]
            for step,data in run.wire:
                if data[:1]!=b'T':continue
                x,y,w,h,n=struct.unpack_from('<HHHHI',data,1)
                self.assertEqual(n,len(data)-13);self.assertLessEqual(n,4096)
                image=Image.open(io.BytesIO(data[13:]));image.load()
                result.append((step,(x,y,w,h),image.quantization))
            return result
        old=observed(baseline);new=observed(candidate)
        high=old[0][2]
        self.assertTrue(all(q==high for step,rect,q in old if step<16))
        self.assertTrue(any(q!=high for step,rect,q in new if 5<=step<16))
        # The one-time static change overlaps an expanded motion rectangle
        # only if its emitted footprint says so. It must not wait for video stop.
        refined=[(step,data) for step,data,context in candidate.records if context[0]=='jpeg_refine' and 7<=step<16]
        self.assertTrue(any((lambda r:r[0]<=224<r[0]+r[2] and r[1]<=128<r[1]+r[3])
                            (struct.unpack_from('<HHHH',data,1)) for step,data in refined))
        self.assertFalse(candidate.session.lowq)
        self.assertGreater(candidate.session.region_counts['completed']['content'],0)
        self.assertTrue(all(fid<=candidate.session.acked_id for fid in candidate.session._touch_frames))
        self.assertTrue(all(pending==0 for _,_,pending in candidate.snapshots))
        self.assertEqual(candidate.session.region_counts['sent'],candidate.session.region_counts['completed'])
        self.assertGreater(candidate.session.region_counts['sent']['jpeg_refine'],0)
        for step in range(1,38):
            repairs=[data for n,data,context in candidate.records if n==step and context[0]=='jpeg_refine']
            self.assertLessEqual(sum(map(len,repairs)),4096)
            self.assertLessEqual(sum(struct.unpack_from('<HH',p,5)[0]*struct.unpack_from('<HH',p,5)[1] for p in repairs),320*16)

    def test_geometry_change_and_quality_are_invalidated(self):
        run=replay(True,geometry=True)
        self.assertFalse(run.session.lowq)
        self.assertLessEqual(len(run.session.region_pending),2)
        self.assertEqual(run.session.zoom_idx,1)
        self.assertIsNotNone(run.session._manual_view)

    def test_real_jpeg_coverage_matches_quality_owner_in_all_modes(self):
        for mode in range(3):
            run=replay(quality_mode=mode)
            low=set()
            for step,data,context in run.records:
                x,y,w,h,n=struct.unpack_from('<HHHHI',data,1)
                image=Image.open(io.BytesIO(data[13:]));image.load()
                self.assertEqual(image.size,(w,h))
                q,sub=context[1:]
                reference=Image.open(io.BytesIO(load('coverage_base',ROOT/'server/cxl_remote_server.py').jpeg(image,q,sub)))
                self.assertEqual(image.quantization,reference.quantization)
                cover={(col,row) for col in range(x//32,(x+w+31)//32) for row in range(y//16,(y+h+15)//16)}
                if (q,sub)==((45,2),(70,0),(88,0))[mode]:low-=cover
                else:low|=cover
            self.assertEqual(low,run.session.lowq)
            self.assertFalse(low)

    def test_idle_screen_has_no_motion_work(self):
        run=replay(changing=False)
        self.assertTrue(all(context[1:]==(70,0) for _,_,context in run.records))
        self.assertEqual(run.session.region_counts['sent']['content'],1)

    def test_ack_during_send_and_connection_failure(self):
        run=replay(early_ack=True)
        self.assertEqual(run.session.region_counts['sent'],run.session.region_counts['completed'])
        self.assertFalse(run.session.region_pending)
        failed=replay(fail_at=5)
        self.assertFalse(failed.session.alive)
        self.assertFalse(failed.session.region_pending)
        self.assertTrue(all(step<5 for step,_,_ in failed.records))

    def test_delayed_ack_does_not_duplicate_completions(self):
        run=replay(ack_every=2)
        self.assertTrue(all(pending<=2 for _,_,pending in run.snapshots))
        self.assertLessEqual(run.session.region_batches['sent']-run.session.region_batches['completed'],2)

    def test_encoder_reports_actual_fallback_settings(self):
        base=load('fallback_base',ROOT/'server/cxl_remote_server.py')
        image=Image.frombytes('RGB',(32,16),random.Random(42).randbytes(32*16*3))
        encoded=base.encode_rect(image,(0,0,32,16),88,0,850,with_settings=True)
        self.assertEqual(len(encoded),1)
        rect,data,q,sub=encoded[0]
        self.assertLess(q,88)
        decoded=Image.open(io.BytesIO(data));decoded.load()
        reference=Image.open(io.BytesIO(base.jpeg(image,q,sub)))
        self.assertEqual(decoded.quantization,reference.quantization)
        self.assertEqual(len(data),len(base.jpeg(image,q,sub)))

    def test_cursor_does_not_start_motion_and_scroll_recovers(self):
        cursor=replay(changing=False,scene='cursor')
        self.assertTrue(all(context[1:]==(70,0) for _,_,context in cursor.records))
        scroll=replay(scene='scroll')
        self.assertTrue(any(context[1:]!=(70,0) for _,_,context in scroll.records))
        self.assertFalse(scroll.session.lowq)

    def test_fullscreen_motion_does_not_repaint_stable_island_twice(self):
        run=replay(scene='scroll')
        self.assertFalse(any(context[0]=='jpeg_refine' and step<26 for step,_,context in run.records))
        self.assertTrue(any(context[0]=='jpeg_refine' and step>26 for step,_,context in run.records))
        self.assertFalse(run.session.lowq)

if __name__=='__main__':unittest.main()
