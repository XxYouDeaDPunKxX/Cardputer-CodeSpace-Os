#include "DiagnosticSerial.h"
#include "DuplexAudio.h"
#include <M5Cardputer.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <cmath>
#include <algorithm>
#include "AudioLevels.h"
#include "AudioProfile.h"
#include "MediaTelemetry.h"

namespace DuplexAudio {
std::atomic<bool> running{false},healthy{false},micWanted{false},micActive{false},listen{true},negotiated{false},hostMic{false},traceWanted{false};
std::atomic<int> volume{5},micDb{0},outDb{10},remoteState{0},errorCode{0};
std::atomic<uint32_t> rxDrops{0},txSilence{0},limited{0},micProcessMaxUs{0},micFrames{0},traceDrops{0},rxOverflows{0},micLimited{0};
std::atomic<uint16_t> token{0},captureEpoch{0};
static constexpr unsigned N=128,RING=4096;
static uint8_t ring[RING];static std::atomic<uint32_t> head{0},tail{0};
static int16_t decodeTable[256];static uint8_t encodeTable[2048];
static TaskHandle_t task=nullptr;static std::atomic<bool> alive{false};static CxlWssStream* wire=nullptr;
static uint32_t lastArrivalUs=0;
static bool reg(uint8_t r,uint8_t v){return M5.In_I2C.writeRegister8(0x18,r,v,100000);}
static bool controlPending=false,controlOn=false;static uint16_t controlEpoch=0;
static void control(uint16_t epoch,bool on){controlEpoch=epoch;controlOn=on;uint16_t t=token;uint8_t p[]={'u',5,uint8_t(t),uint8_t(t>>8),uint8_t(epoch),uint8_t(epoch>>8),uint8_t(on)};controlPending=!wire->queueAudioControl(p,sizeof(p));}
void accept(uint8_t version,uint16_t rate,uint16_t session){if(version==AudioProfile::Version&&rate==AudioProfile::Rate&&session){token=session;negotiated=true;}}
static bool IRAM_ATTR rxOverflow(i2s_chan_handle_t,i2s_event_data_t*,void*){rxOverflows.fetch_add(1,std::memory_order_relaxed);return false;}
struct __attribute__((packed)) TracePacket {
 char magic[4];uint32_t sequence,capture,rxSample,txSample,ms;
 uint16_t count,delayMs;uint8_t micDb,outDb,volume,flags;
 int16_t raw[128],reference[128];uint32_t crc;
};
static_assert(sizeof(TracePacket)==548,"trace format");
static QueueHandle_t traceQueue=nullptr;static std::atomic<bool> traceRun{false},traceAlive{false};
static uint32_t traceCrc(const TracePacket& p){uint32_t c=~0u;auto bytes=(const uint8_t*)&p;for(size_t i=0;i<sizeof(p)-4;i++){c^=bytes[i];for(int b=0;b<8;b++)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}return ~c;}
static void traceWorker(void*){
 TracePacket p;
 while(traceRun||uxQueueMessagesWaiting(traceQueue)){
  if(xQueueReceive(traceQueue,&p,pdMS_TO_TICKS(20))==pdTRUE){p.crc=traceCrc(p);if(CsosSerial.write((uint8_t*)&p,sizeof(p))!=sizeof(p))traceDrops++;}
 }
 traceAlive=false;vTaskDelete(nullptr);
}
static bool startTrace(){
 if(traceQueue)return true;
 if(ESP.getFreeHeap()<13000)return false;
 traceQueue=xQueueCreate(1,sizeof(TracePacket));if(!traceQueue)return false;
 traceRun=true;traceAlive=true;
 if(xTaskCreatePinnedToCore(traceWorker,"audio_trace",2048,nullptr,1,nullptr,1)!=pdPASS){traceRun=traceAlive=false;vQueueDelete(traceQueue);traceQueue=nullptr;return false;}
 return true;
}
static void stopTrace(){if(!traceQueue)return;traceRun=false;while(traceAlive)delay(1);vQueueDelete(traceQueue);traceQueue=nullptr;}
void receive(const uint8_t* data,size_t n){
 if(!running||!negotiated||!listen||n>RING-1)return;
 uint32_t now=micros();if(lastArrivalUs)MediaTelemetry::peak(MediaTelemetry::arrivalGapUs,now-lastArrivalUs);lastArrivalUs=now;MediaTelemetry::audioBytes+=n;
 RxAudioProbe::probe.audio(now,wire->receivePollUs());
 uint32_t h=head.load(std::memory_order_relaxed),t=tail.load(std::memory_order_acquire);
 if(n>RING-1-(h-t)){rxDrops++;return;}
 for(size_t i=0;i<n;i++)ring[(h+i)&(RING-1)]=data[i];head.store(h+n,std::memory_order_release);
}
void microphone(bool on){
 if(!on){micWanted=false;traceWanted=false;hostMic=false;remoteState=0;wire->clearMicrophone();return;}
 if(!negotiated||!healthy){errorCode=1;return;}
 if(!wire->microphoneReady()){errorCode=7;return;}
 errorCode=0;hostMic=false;remoteState=1;micWanted=true;
}
static void worker(void*){
 i2s_chan_handle_t tx=nullptr,rx=nullptr;bool txOn=false,rxOn=false;
 int16_t out[N*2],in[N*2],raw[N];
 uint8_t packet[8+AudioProfile::MicrophoneSamples]={};unsigned pending=0;uint32_t seq=0,lastLog=0;bool primed=false;uint32_t rxSamples=0,txSamples=0,traceSeq=0,traceId=0;bool wasTrace=false;int32_t envelope=65536;
 uint16_t micToken=0;int prevVol=-1,prevOut=-1,prevMic=-1;
 uint32_t previousCycleUs=0,cycleMaxUs=0,writeMaxUs=0,bufferMax=0,bufferMin=RING;
 M5.Mic.end();M5.Speaker.stop();M5.Speaker.end();
 for(auto pin:{GPIO_NUM_41,GPIO_NUM_43,GPIO_NUM_42,GPIO_NUM_46})gpio_reset_pin(pin);
 i2s_chan_config_t chan=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1,I2S_ROLE_MASTER);chan.dma_desc_num=4;chan.dma_frame_num=N;chan.auto_clear=true;
 auto cfg=i2s_std_config_t{};cfg.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(AudioProfile::Rate);
 cfg.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
 cfg.gpio_cfg.mclk=I2S_GPIO_UNUSED;cfg.gpio_cfg.bclk=GPIO_NUM_41;cfg.gpio_cfg.ws=GPIO_NUM_43;cfg.gpio_cfg.dout=GPIO_NUM_42;cfg.gpio_cfg.din=GPIO_NUM_46;
 bool ok=i2s_new_channel(&chan,&tx,&rx)==ESP_OK;
 if(ok)ok=i2s_channel_init_std_mode(tx,&cfg)==ESP_OK&&i2s_channel_init_std_mode(rx,&cfg)==ESP_OK;
 if(ok){i2s_event_callbacks_t callbacks={};callbacks.on_recv_q_ovf=rxOverflow;ok=i2s_channel_register_event_callback(rx,&callbacks,nullptr)==ESP_OK;}
 memset(out,0,sizeof(out));size_t bytes=0;
 if(ok){i2s_channel_preload_data(tx,out,sizeof(out),&bytes);ok=i2s_channel_enable(tx)==ESP_OK;txOn=ok;}
 const uint8_t init[][2]={{9,0},{10,0},{11,0},{12,0x20},{16,0x13},{17,0x7c},{0,0x80},{1,0xb5},{2,0x18},{13,1},{18,0},{19,0x10},{50,0xbf},{55,8}};
 for(auto& r:init)if(ok)ok=reg(r[0],r[1]);
 healthy=ok;
 CSOS_LOGF("[AUDIO] bus=%d rate=%u block=128 profile=%u heap=%u\n",ok,AudioProfile::Rate,unsigned(AudioProfile::Version),unsigned(ESP.getFreeHeap()));
 while(running&&ok){
  uint32_t cycleUs=micros();if(previousCycleUs)cycleMaxUs=std::max(cycleMaxUs,cycleUs-previousCycleUs);previousCycleUs=cycleUs;
  if(controlPending)control(controlEpoch,controlOn);
  bool want=micWanted&&negotiated&&wire->connected();
  if(want&&!micActive){
   pending=0;seq=0;rxSamples=0;micToken=token;uint16_t epoch=captureEpoch+1;if(!epoch)epoch=1;captureEpoch=epoch;rxOverflows=0;
   ok=reg(1,0xbf)&&reg(14,2)&&reg(20,0x10)&&reg(23,0xbf)&&reg(28,0x6a)&&i2s_channel_enable(rx)==ESP_OK;
   rxOn=ok;micActive=ok;
   if(ok&&micWanted)control(captureEpoch,true);
   CSOS_LOGF("[AUDIO] microphone=%d heap=%u\n",int(micActive.load()),unsigned(ESP.getFreeHeap()));
  }
  if(!want&&micActive){
   i2s_channel_disable(rx);rxOn=false;reg(1,0xb5);micActive=false;pending=0;hostMic=false;wire->clearMicrophone();control(captureEpoch,false);
   traceWanted=false;stopTrace();wasTrace=false;
  }
  if(traceWanted&&micActive&&!wasTrace){if(startTrace()){traceId++;traceSeq=0;wasTrace=true;CSOS_LOGF("[AUDIO] trace=1 id=%lu\n",(unsigned long)traceId);}else{traceWanted=false;errorCode=5;}}
  if(!traceWanted&&wasTrace){stopTrace();wasTrace=false;CSOS_LOGLN("[AUDIO] trace=0");}
  int v=volume.load(),od=outDb.load(),md=micDb.load();
  int32_t outScale=audioGainQ16(od),micScale=audioGainQ16(md);
  if(v!=prevVol||od!=prevOut||md!=prevMic){CSOS_LOGF("[AUDIO] volume=%d output_db=%d mic_db=%d\n",v,od,md);prevVol=v;prevOut=od;prevMic=md;}
  uint32_t t=tail.load(),h=head.load(std::memory_order_acquire),available=h-t;
  bufferMin=std::min(bufferMin,available);bufferMax=std::max(bufferMax,available);
  if(!listen){t=h;available=0;primed=false;}
  if(available>AudioProfile::LateSamples){t=h-AudioProfile::PrimeSamples;available=AudioProfile::PrimeSamples;rxDrops++;}
  if(!primed&&available>=AudioProfile::PrimeSamples)primed=true;
  bool play=listen&&primed&&available>=N;
  if(primed&&available<N){primed=false;txSilence++;}
  int peak=1;for(unsigned i=0;play&&i<N;i++)peak=std::max(peak,std::abs(int(decodeTable[ring[(t+i)&(RING-1)]])));
  int32_t requested=int64_t(outScale)*v/10;
  int32_t target=std::min(requested,int32_t(int64_t(32000)*65536/peak));
  if(target<requested)limited++;
  envelope=target<envelope?target:std::min(target,envelope+std::max<int32_t>(1,(target-envelope)/12));
  uint32_t outClipped=0;
  for(unsigned i=0;i<N;i++){
   int sample=play?decodeTable[ring[(t+i)&(RING-1)]]:0;
   int16_t actual=audioOutput(sample,10,envelope,outClipped);out[2*i]=out[2*i+1]=actual;
  }
  tail.store(t+(play?N:0),std::memory_order_release);
  uint32_t blockTx=txSamples;txSamples+=N;
  uint32_t writeAt=micros();bytes=0;ok=i2s_channel_write(tx,out,sizeof(out),&bytes,30)==ESP_OK&&bytes==sizeof(out);writeMaxUs=std::max(writeMaxUs,uint32_t(micros()-writeAt));
  if(ok&&micActive){
   bytes=0;ok=i2s_channel_read(rx,in,sizeof(in),&bytes,30)==ESP_OK&&bytes==sizeof(in);
   if(!ok)break;
   uint32_t at=micros();
   for(unsigned i=0;i<N;i++)raw[i]=in[2*i];
   if(wasTrace&&traceWanted){
    TracePacket p={};memcpy(p.magic,"AUD6",4);p.sequence=traceSeq++;p.capture=(traceId<<16)|captureEpoch.load();p.rxSample=rxSamples;p.txSample=blockTx;p.ms=millis();p.count=N;p.delayMs=0;p.micDb=md;p.outDb=od;p.volume=v;p.flags=0;
    memcpy(p.raw,raw,sizeof(raw));for(unsigned i=0;i<N;i++)p.reference[i]=out[2*i];
    if(xQueueSend(traceQueue,&p,0)!=pdTRUE)traceDrops++;
   }
   rxSamples+=N;uint32_t micClipped=0;
   for(unsigned i=0;i<N;i++){
    int16_t value=audioMicrophone(raw[i],micScale,micClipped);packet[8+pending++]=encodeTable[(int(value)+32768)>>5];
    if(pending==AudioProfile::MicrophoneSamples){pending=0;if(micWanted&&micToken==token){packet[0]=micToken;packet[1]=micToken>>8;uint16_t epoch=captureEpoch;memcpy(packet+2,&epoch,2);memcpy(packet+4,&seq,4);seq++;if(!wire->queueMicrophone(packet,sizeof(packet)))rxDrops++;else micFrames++;}}
   }
   micLimited+=micClipped;micProcessMaxUs=std::max(micProcessMaxUs.load(),uint32_t(micros()-at));
   if(rxOverflows){errorCode=3;micWanted=false;CSOS_LOGLN("[AUDIO] microphone_error=rx_overflow");}
  }
  bool telemetryDue=CSOS_USB_CONSOLE && millis()-lastLog>3000;
  if(telemetryDue){lastLog=millis();CSOS_LOGF("[AUDIO] mic=%d host=%d packets=%lu drops=%lu gaps=%lu limited=%lu mic_process_max_us=%lu heap=%u largest=%u mic_limited=%lu trace_drops=%lu overflow=%lu stack=%u\n",int(micActive.load()),int(hostMic.load()),(unsigned long)micFrames.load(),(unsigned long)rxDrops.load(),(unsigned long)txSilence.load(),(unsigned long)limited.load(),(unsigned long)micProcessMaxUs.load(),unsigned(ESP.getFreeHeap()),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),(unsigned long)micLimited.load(),(unsigned long)traceDrops.load(),(unsigned long)rxOverflows.load(),unsigned(uxTaskGetStackHighWaterMark(nullptr)));}
  if(telemetryDue){
   RxAudioProbe::Record episode;
   if(RxAudioProbe::probe.take(episode)){
    CSOS_LOGF("[RXGAP] at_us=%lu gap_us=%lu slot_sum_us=%lu queue_sum_us=%lu read_sum_us=%lu idle_sum_us=%lu ws_poll_sum_us=%lu video_records=%lu video_bytes=%lu rx_bytes=%lu skipped=%lu\n",
     (unsigned long)episode.atUs,(unsigned long)episode.gapUs,(unsigned long)episode.slotUs,
     (unsigned long)episode.queueUs,(unsigned long)episode.readUs,(unsigned long)episode.idleUs,
     (unsigned long)episode.pollUs,(unsigned long)episode.videoRecords,(unsigned long)episode.videoBytes,
     (unsigned long)episode.rxBytes,(unsigned long)RxAudioProbe::probe.skipped.exchange(0));
   }
   CSOS_LOGF("[MEDIA] ms=%lu rate=%u received=%lu arrival_gap_us=%lu worker_gap_us=%lu i2s_write_us=%lu buffer_min=%lu buffer_max=%lu slot_wait_us=%lu queue_wait_us=%lu draw_us=%lu frames=%lu tiles=%lu wanted=%d remote=%d error=%d\n",(unsigned long)millis(),AudioProfile::Rate,(unsigned long)MediaTelemetry::audioBytes.exchange(0),(unsigned long)MediaTelemetry::arrivalGapUs.exchange(0),(unsigned long)cycleMaxUs,(unsigned long)writeMaxUs,(unsigned long)bufferMin,(unsigned long)bufferMax,(unsigned long)MediaTelemetry::slotWaitUs.exchange(0),(unsigned long)MediaTelemetry::queueWaitUs.exchange(0),(unsigned long)MediaTelemetry::drawUs.exchange(0),(unsigned long)MediaTelemetry::frames.exchange(0),(unsigned long)MediaTelemetry::tiles.exchange(0),int(micWanted.load()),remoteState.load(),errorCode.load());
   CSOS_LOGF("[VIDEO] ms=%lu draw_sum_us=%lu jpeg_decode_us=%lu dma_wait_us=%lu min_heap=%lu dma_free=%lu dma_largest=%lu ws_poll_max_us=%lu ws_chunks=%lu\n",(unsigned long)millis(),(unsigned long)MediaTelemetry::drawSumUs.exchange(0),(unsigned long)MediaTelemetry::jpegDecodeUs.exchange(0),(unsigned long)MediaTelemetry::dmaWaitUs.exchange(0),(unsigned long)ESP.getMinFreeHeap(),(unsigned long)heap_caps_get_free_size(MALLOC_CAP_DMA),(unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),(unsigned long)MediaTelemetry::wsPollMaxUs.exchange(0),(unsigned long)MediaTelemetry::wsChunks.exchange(0));
   cycleMaxUs=writeMaxUs=bufferMax=0;bufferMin=RING;
  }
  delay(1);
 }
 if(running&&!ok){errorCode=8;CSOS_LOGLN("[AUDIO] driver_error=io_or_init");}
 healthy=false;micActive=false;micWanted=false;hostMic=false;
 if(rxOn)i2s_channel_disable(rx);if(txOn)i2s_channel_disable(tx);if(rx)i2s_del_channel(rx);if(tx)i2s_del_channel(tx);
 reg(50,0);reg(14,0xff);reg(18,2);reg(13,0xfa);
 traceWanted=false;stopTrace();
 alive=false;vTaskDelete(nullptr);
}
bool begin(CxlWssStream* s){
 lastArrivalUs=0;
 wire=s;controlPending=false;rxDrops=0;txSilence=0;limited=0;micProcessMaxUs=0;micFrames=0;traceDrops=0;rxOverflows=0;micLimited=0;errorCode=0;remoteState=0;traceWanted=false;micWanted=false;micActive=false;negotiated=false;hostMic=false;token=0;head=tail=0;
 for(int q=0;q<256;q++){float y=q/127.5f-1;decodeTable[q]=int16_t(copysignf((powf(256,fabsf(y))-1)/255,y)*32000);}
 for(int i=0;i<2048;i++){float x=(i*32+16-32768)/32000.0f;int q=lroundf((copysignf(log1pf(255*fabsf(x))/logf(256),x)+1)*127.5f);encodeTable[i]=std::max(0,std::min(255,q));}
 running=true;alive=true;
 if(xTaskCreatePinnedToCore(worker,"csos_duplex",8192,nullptr,4,&task,0)!=pdPASS){running=false;healthy=false;alive=false;return false;}return true;
}
void end(){micWanted=false;running=false;if(wire)wire->clearMicrophone();while(alive)delay(1);task=nullptr;negotiated=false;token=0;}
}
