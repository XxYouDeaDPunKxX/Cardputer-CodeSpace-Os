"""Run the pinned encoder loop; inspect the quality actually left on the panel."""
import importlib.util,io,pathlib,struct,threading,types,unittest
from unittest.mock import patch
from PIL import Image,ImageDraw

ROOT=pathlib.Path(__file__).resolve().parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module
base=load('cxl_quality_fixture',ROOT.parent/'server/cxl_remote_server.py')
fix=load('csos_quality',ROOT/'server/csos_quality.py')

class Clock:
    now=0.
    def monotonic(self):return self.now
    def time(self):return 1000+self.now
    def sleep(self,n):self.now+=n

class QualityTest(unittest.TestCase):
    def run_frames(self,wrapped,quality=1):
        clock=Clock()
        class Wake:
            def wait(self,n):clock.now+=max(.2,n)
            def clear(self):pass
            def set(self):pass
        class Thread:
            def __init__(self,*a,**kw):pass
            def start(self):pass
        class Capture:
            monitors=[dict(left=0,top=0,width=640,height=480)]
            def __enter__(self):return self
            def __exit__(self,*a):pass
        class Socket:
            def close(self):pass
        server=types.SimpleNamespace(inp=None,monitor=0,view_only=False,matrix=None,
                                    cfg=dict(stretch=True,fps=5,audio=False,cursor=True))
        first=Image.new('RGB',(320,240),(185,185,185));motion=first.copy()
        painter=ImageDraw.Draw(motion)
        for row in range(15):
            for col in ([0,4,5,9] if row==7 else [3,4,5,6]):
                painter.rectangle((col*32,row*16,col*32+31,row*16+15),fill=(90+row*5,100,120))
        settled=motion.copy();painter=ImageDraw.Draw(settled)
        for col in [0,4,5,9]:painter.rectangle((col*32,112,col*32+31,127),fill=(220,220,220))
        high=Image.open(io.BytesIO(base.jpeg(first,*base.QUALITY[quality]))).quantization
        class ObservedSession(base.Session):
            def __init__(self):
                super().__init__(Socket(),('fixture',0),server)
                self.index=0;self.wake=Wake();self.low=Image.new('L',(320,240),0);self.frames=0;self.quality=quality
            def handshake(self,*a):pass
            def render(self,*a):
                frame=[first,motion,settled][min(self.index,2)];self.index+=1
                if self.index>=7:self.alive=False
                return frame,2.,0,2,True
            def send(self,data):
                if data[:1]==b'T':
                    x,y,w,h,n=struct.unpack('<HHHHI',data[1:13])
                    image=Image.open(io.BytesIO(data[13:]))
                    ImageDraw.Draw(self.low).rectangle((x,y,x+w-1,y+h-1),fill=0 if image.quantization==high else 255)
                elif data[:1]==b'F':self.acked_id=self.sent_id;self.frames+=1
        cls=fix.with_quality_refinement(ObservedSession,base.TILE_W,base.TILE_H) if wrapped else ObservedSession
        with patch.object(base,'time',clock),patch.object(base,'log',lambda *a:None),patch.object(base,'threading',types.SimpleNamespace(Thread=Thread,Condition=threading.Condition,Lock=threading.Lock,Event=threading.Event)),patch.dict('sys.modules',{'mss':types.SimpleNamespace(MSS=Capture)}):
            session=cls();session.run()
        return session,sum(value>0 for value in session.low.tobytes())

    def test_full_encoded_area_is_refined_after_motion(self):
        for quality in range(3):
            with self.subTest(quality=quality):
                upstream,residual=self.run_frames(False,quality)
                self.assertFalse(upstream.lowq)
                self.assertGreater(residual,0)  # Reproduces the pinned upstream defect.
                fixed,residual=self.run_frames(True,quality)
                self.assertFalse(fixed.lowq)
                self.assertEqual(residual,0)
                self.assertEqual(fixed.frames,upstream.frames)

    def test_nonvideo_and_sharp_only_sends_do_not_create_refinement_work(self):
        class Sink:
            def __init__(self):self.lowq=set();self.ow=320;self.oh=240;self.sent=[];self.lock=threading.RLock()
            def send(self,data):self.sent.append(data)
        session=fix.with_quality_refinement(Sink,32,16)()
        tile=b'T'+struct.pack('<HHHHI',0,0,320,240,1)+b'x'
        session.send(tile);session.send(b'H');self.assertFalse(session.lowq)
        session.lowq={(3,0)};session.send(tile)
        self.assertEqual(len(session.lowq),150)
        self.assertEqual(session.sent,[tile,b'H',tile])

if __name__=='__main__':unittest.main()
