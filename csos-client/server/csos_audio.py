"""Negotiated 12 kHz duplex on the authenticated CXL connection.
Only audio workers touch PulseAudio pipes; the input reader never blocks on PCM.
"""
import array,math,os,queue,secrets,select,struct,subprocess,threading,time
RATE=12000
VERSION=5
BLOCK=RATE//50
MIC_BLOCK=RATE//100
PCM_BLOCK_BYTES=BLOCK*2
_DECODE=[round(math.copysign((256**abs(q/127.5-1)-1)/255,q/127.5-1)*32000) for q in range(256)]
_ENCODE=bytes(max(0,min(255,round((math.copysign(math.log1p(255*abs((i-32768)/32000))/math.log(256),(i-32768))+1)*127.5))) for i in range(65536))
def decode(data):
    a=array.array('h',(_DECODE[x] for x in data))
    if os.sys.byteorder!='little':a.byteswap()
    return a.tobytes()
def encode(data):
    a=array.array('h');a.frombytes(data)
    if os.sys.byteorder!='little':a.byteswap()
    return bytes(_ENCODE[x+32768] for x in a)
_active_lock=threading.Lock()
_active=None

def with_audio(Session):
    class AudioSession(Session):
        def __init__(self,*args,**kwargs):
            super().__init__(*args,**kwargs)
            self.audio_rate=RATE;self.audio_profile=VERSION
            self.audio_ready=False;self.audio_mic=False;self.audio_token=0;self.audio_epoch=0;self.audio_seq=None
            self.audio_pcm_ready=False;self.audio_generation=0
            self.audio_state_lock=threading.Lock()
            self.audio_queue=queue.Queue(8);self.audio_stop=threading.Event()
            self.audio_drops=0;self.audio_gaps=0;self.audio_down=0;self.audio_up=0;self.audio_down_drops=0
            self.audio_threads=[];self.audio_procs=set();self.audio_proc_lock=threading.Lock()
        def _audio_process(self,cmd):
            p=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,bufsize=0)
            with self.audio_proc_lock:self.audio_procs.add(p)
            return p
        def _audio_remove(self,p):
            try:
                if p.poll() is None:p.terminate()
                try:p.wait(timeout=.5)
                except subprocess.TimeoutExpired:p.kill();p.wait(timeout=.5)
            except (OSError,subprocess.TimeoutExpired):pass
            for stream in (p.stdin,p.stdout):
                if stream:
                    try:stream.close()
                    except OSError:pass
            with self.audio_proc_lock:self.audio_procs.discard(p)
        def _audio_shutdown(self):
            with self.audio_state_lock:
                self.audio_mic=False;self.audio_pcm_ready=False;self.audio_generation+=1
            self.audio_stop.set()
            with self.audio_proc_lock:
                for p in list(self.audio_procs):
                    try:p.terminate()
                    except OSError:pass
        def _mic_status(self,token,epoch,status):
            if self.alive and not self.audio_stop.is_set():
                self.send(b'i'+struct.pack('<HHB',token,epoch,status))
        def _mic_failed(self,generation,token,epoch,cause):
            with self.audio_state_lock:
                if generation!=self.audio_generation:return
                self.audio_mic=False;self.audio_pcm_ready=False
            print('CSOS_AUDIO_ERROR microphone',cause,'epoch',epoch,flush=True)
            self._mic_status(token,epoch,3)
        def send(self,data):
            if data[:1]==b'F' and len(data)==12:data=data[:11]+bytes([data[11]|0x40])
            return super().send(data)
        def handle(self,t,p):
            global _active
            if t==ord('a'):
                if not getattr(self,'_presence_authenticated',False):return
                if p!=bytes([VERSION]):
                    # Existing clients understand this fixed-length reply and
                    # reject token zero. Do not acquire audio or start workers.
                    self.send(b'a'+struct.pack('<BHH',VERSION,RATE,0))
                    print('CSOS_AUDIO_PROFILE_REJECT requested='+p.hex()+' supported='+str(VERSION),flush=True)
                    return
                if not self.audio_ready:
                    with _active_lock:
                        old=_active;_active=self
                    if old is not None and old is not self:old.close()
                    self.audio_token=secrets.randbelow(65535)+1;self.audio_ready=True
                    for target in (self._audio_output,self._audio_input,self._audio_stats):
                        thread=threading.Thread(target=target,daemon=True);self.audio_threads.append(thread);thread.start()
                self.send(b'a'+struct.pack('<BHH',VERSION,RATE,self.audio_token));return
            if t==ord('u'):
                if not self.audio_ready or len(p)!=5:return
                token,epoch,on=struct.unpack('<HHB',p)
                if token!=self.audio_token or not epoch or on not in (0,1):return
                with self.audio_state_lock:
                    if epoch==self.audio_epoch and bool(on)==self.audio_mic:return
                    self.audio_mic=bool(on);self.audio_epoch=epoch;self.audio_seq=None
                    self.audio_pcm_ready=False;self.audio_generation+=1
                    while True:
                        try:self.audio_queue.get_nowait()
                        except queue.Empty:break
                # This means request received, never Linux PCM ready.
                if on:self._mic_status(token,epoch,1)
                return
            if t==ord('m'):
                if not self.audio_ready or len(p)!=8+MIC_BLOCK:return
                token,epoch,seq=struct.unpack_from('<HHI',p)
                with self.audio_state_lock:
                    if not self.audio_mic or token!=self.audio_token or epoch!=self.audio_epoch:return
                    if self.audio_seq is not None and seq<=self.audio_seq:return
                    if self.audio_seq is not None and seq!=self.audio_seq+1:self.audio_gaps+=1
                    self.audio_seq=seq
                    try:self.audio_queue.put_nowait((time.monotonic(),self.audio_generation,token,epoch,bytes(p[8:])))
                    except queue.Full:self.audio_drops+=1
                return
            if t==ord('U'):
                if len(p)==1:self.sound_wanted=self.audio_ready and p==b'\x01'
                return
            return super().handle(t,p)
        def _audio_send(self,data):
            if not hasattr(self,'send_lock'):
                self.send(data);return True
            requested=time.monotonic();acquired=None;sent=None;locked=False
            try:
                locked=self.send_lock.acquire(timeout=.02)
                if not locked:return False
                acquired=time.monotonic()
                if not self.alive or self.audio_stop.is_set():return False
                self.sock.sendall(data);self._stat_bytes+=len(data);sent=time.monotonic();return True
            finally:
                if locked:self.send_lock.release()
                observer=getattr(self,'observe_audio_send',None)
                if observer:
                    try:observer(data,requested,acquired,sent,time.monotonic())
                    except Exception:pass  # Instrumentation must never break streaming.
        def _audio_output(self):
            while self.alive and not self.audio_stop.is_set():
                p=None
                try:
                    p=self._audio_process(['parec','--raw','--format=s16le','--rate='+str(RATE),'--channels=1','--latency-msec=40','--process-time-msec=10','--device=csos_output.monitor'])
                    os.set_blocking(p.stdout.fileno(),False);buf=bytearray()
                    while self.alive and not self.audio_stop.is_set() and p.poll() is None:
                        if not select.select([p.stdout],[],[],.1)[0]:continue
                        chunk=os.read(p.stdout.fileno(),8192)
                        if not chunk:break
                        buf.extend(chunk)
                        if len(buf)>3*PCM_BLOCK_BYTES:
                            skip=(len(buf)-2*PCM_BLOCK_BYTES)//PCM_BLOCK_BYTES*PCM_BLOCK_BYTES;del buf[:skip];self.audio_down_drops+=skip//PCM_BLOCK_BYTES
                        while len(buf)>=PCM_BLOCK_BYTES:
                            data=bytes(buf[:PCM_BLOCK_BYTES]);del buf[:PCM_BLOCK_BYTES]
                            if self.sound_wanted:
                                self.audio_active=self._audio_send(b'S'+struct.pack('<H',BLOCK)+encode(data))
                                if self.audio_active:self.audio_down+=BLOCK
                                else:self.audio_down_drops+=1
                            else:self.audio_active=False
                except (OSError,ValueError) as error:
                    print('CSOS_AUDIO_ERROR output',type(error).__name__,flush=True)
                finally:
                    self.audio_active=False
                    if p:self._audio_remove(p)
                self.audio_stop.wait(.5)
        def _pulse_stream_ready(self,p,name):
            try:
                check=subprocess.run(['pactl','--format=json','list','sink-inputs'],capture_output=True,text=True,timeout=.5)
                if check.returncode:return False
                return any(row.get('properties',{}).get('media.name')==name and
                           row.get('properties',{}).get('application.process.id')==str(p.pid)
                           for row in __import__('json').loads(check.stdout))
            except (OSError,ValueError,subprocess.TimeoutExpired):return False
        def _pipe_writable(self,stream):
            return bool(select.select([],[stream],[],.01)[1])
        def _audio_input(self):
            p=None;generation=-1;acknowledged=False;pulse_ready=False;next_probe=0;started=0
            try:
                while self.alive and not self.audio_stop.is_set():
                    with self.audio_state_lock:
                        current=self.audio_generation;on=self.audio_mic;token=self.audio_token;epoch=self.audio_epoch
                    if generation!=current or not on:
                        if p:self._audio_remove(p);p=None
                        if generation!=current:
                            generation=current;acknowledged=False;pulse_ready=False;started=time.monotonic();next_probe=0
                            if not on and epoch:self._mic_status(token,epoch,0)
                    if not on:
                        self.audio_stop.wait(.02);continue
                    try:
                        name=f'CSOS_MIC_{token}_{epoch}'
                        if p is None:
                            p=self._audio_process(['pacat','--playback','--raw','--format=s16le','--rate='+str(RATE),'--channels=1','--latency-msec=30','--process-time-msec=10','--stream-name='+name,'--device=csos_mic_feed'])
                            os.set_blocking(p.stdin.fileno(),False)
                        if p.poll() is not None:raise OSError('pacat exited')
                        if not pulse_ready and time.monotonic()>=next_probe:
                            pulse_ready=self._pulse_stream_ready(p,name);next_probe=time.monotonic()+.1
                            if not pulse_ready and time.monotonic()-started>3:raise OSError('PulseAudio stream not ready')
                        try:at,packet_gen,packet_token,packet_epoch,pcm=self.audio_queue.get(timeout=.02)
                        except queue.Empty:continue
                        if time.monotonic()-at>.12:self.audio_drops+=1;continue
                        data=decode(pcm)
                        if not self._pipe_writable(p.stdin):self.audio_drops+=1;continue
                        with self.audio_state_lock:
                            valid=(self.audio_mic and self.audio_generation==packet_gen==generation and packet_token==token and packet_epoch==epoch)
                            if not valid:continue
                            n=os.write(p.stdin.fileno(),data)
                            if n!=len(data):self.audio_drops+=1;continue
                            self.audio_up+=MIC_BLOCK
                            if pulse_ready:self.audio_pcm_ready=True
                        if pulse_ready and not acknowledged:
                            # Pacat exists in PulseAudio AND accepted PCM of
                            # the current capture generation before this ACK.
                            self._mic_status(token,epoch,2);acknowledged=True
                    except (OSError,ValueError) as error:
                        if p:self._audio_remove(p);p=None
                        self._mic_failed(generation,token,epoch,str(error))
            finally:
                if p:self._audio_remove(p)
        def _audio_stats(self):
            while not self.audio_stop.wait(5):
                print('CSOS_AUDIO',self.audio_token,'epoch',self.audio_epoch,'up',self.audio_up,'down',self.audio_down,'drops',self.audio_drops,'gaps',self.audio_gaps,'down_drops',self.audio_down_drops,'mic',int(self.audio_mic),'pcm_ready',int(self.audio_pcm_ready),'video_fps',round(getattr(self,'fps',0),2),flush=True)
        def close(self,reason=None):
            global _active
            self._audio_shutdown()
            result=super().close(reason)
            with _active_lock:
                if _active is self:_active=None
            for t in self.audio_threads:
                if t is not threading.current_thread():t.join(timeout=.3)
            return result
    return AudioSession
