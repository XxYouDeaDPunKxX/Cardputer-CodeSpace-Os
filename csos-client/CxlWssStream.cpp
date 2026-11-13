#include "DiagnosticSerial.h"
#include "CxlWssStream.h"
#include "CsosTlsClient.h"
#include "CloudConfig_private.h"
#include "CaBundle.h"
#include "MediaTelemetry.h"
#include <time.h>
#include <algorithm>
#include <WiFi.h>

CxlWssStream::CxlWssStream(bool useMfl) {
  if(useMfl)ws_.setSecureClientFactory([]()->WiFiClientSecure*{return new CsosTlsClient();});
  ws_.setBinaryReceiveBuffer(rx_.storage(),rx_.capacity());
  ws_.onEvent([this](WStype_t type,uint8_t* data,size_t size){event(type,data,size);});
  ws_.setReconnectInterval(60000); // CXL owns reconnect: every attempt needs a new handshake.
}

void CxlWssStream::fail(TransportFailure cause) {
  auto current = failure_.load();
  while ((current == TransportFailure::None || current == TransportFailure::Connect) &&
         !failure_.compare_exchange_weak(current,cause)) {}
  failed_=true;
}

void CxlWssStream::event(WStype_t type,uint8_t* data,size_t size) {
  switch(type) {
    case WStype_CONNECTED: connected_=true; failure_=TransportFailure::None; break;
    case WStype_DISCONNECTED: {
      const bool wasConnected=connected_.exchange(false);
      char reason[128]={};
      if(data && size) memcpy(reason,data,std::min(size,sizeof(reason)-1));
      fail(wasConnected?TransportFailure::Disconnected:classifyTransportClose(reason));
      break;
    }
    case WStype_BIN:
      if(fragmented_) fail(TransportFailure::Protocol);
      else if(!rx_.put(data,size)) fail(TransportFailure::RxBlock);
      else if(size) MediaTelemetry::wsChunks++;
      break;
    case WStype_FRAGMENT_BIN_START:
      if(fragmented_) fail(TransportFailure::Protocol);
      else if(!rx_.put(data,size)) fail(TransportFailure::RxBlock);
      else if(size) MediaTelemetry::wsChunks++;
      fragmented_=true;
      break;
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
      if(!fragmented_) fail(TransportFailure::Protocol);
      else if(!rx_.put(data,size)) fail(TransportFailure::RxBlock);
      else if(size) MediaTelemetry::wsChunks++;
      if(type==WStype_FRAGMENT_FIN) fragmented_=false;
      break;
    case WStype_TEXT:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_ERROR: fail(TransportFailure::Protocol); break;
    default: break;
  }
}

bool CxlWssStream::connect() {
  claimOwner();
  stop();
  failure_=TransportFailure::Connect;
  if(!tx_) tx_=xQueueCreate(16,sizeof(Packet));
  if(!tx_) return false;
  xQueueReset(tx_);
#if CSOS_CXL_DIAG_TRACE
  if(ackTxTrace_)ackTxTrace_->reset();
#endif
  if(!audioTx_)audioTx_=xQueueCreate(4,sizeof(AudioPacket));
  if(audioTx_)xQueueReset(audioTx_);
  failed_=false; fragmented_=false;
  auto progress=[this](ConnectionPhase phase){
    if(!progress_||progress_(phase))return true;
    stop();failed_=true;return false;
  };
  if(!progress(ConnectionPhase::Time))return false;
  // Preserve the network's resolvers for both probes and desktop sessions.
  // An HTTP refusal is not evidence that its DNS should be replaced.
  CSOS_LOGF("[CSOS] dns_policy=network primary=%s secondary=%s\n",
    WiFi.dnsIP(0).toString().c_str(),WiFi.dnsIP(1).toString().c_str());
  CSOS_LOGF("[CSOS] transport_start epoch=%lld heap=%u\n",(long long)time(nullptr),(unsigned)ESP.getFreeHeap());
  // A cold device must acquire valid time before certificate date validation.
  if(time(nullptr)<1767225600) {
    configTime(0,0,"pool.ntp.org","time.cloudflare.com");
    const uint32_t started=millis();
    while(time(nullptr)<1767225600 && millis()-started<20000){
      if(!progress(ConnectionPhase::Time))return false;
      delay(20);
    }
    if(time(nullptr)<1767225600) { CSOS_LOGF("[CSOS] time_sync_failed epoch=%lld\n",(long long)time(nullptr)); failure_=TransportFailure::Clock; failed_=true; return false; }
  }
  CSOS_LOGF("[CSOS] tls_start epoch=%lld bundle_bytes=%u\n",(long long)time(nullptr),(unsigned)sizeof(CSOS_CA_BUNDLE));
  IPAddress endpointAddress;
  if(!progress(ConnectionPhase::Dns))return false;
  const int resolved=WiFi.hostByName(CSOS_WSS_HOST,endpointAddress);
  CSOS_LOGF("[CSOS] endpoint_dns ok=%d address=%s\n",resolved,endpointAddress.toString().c_str());
  if(!progress(ConnectionPhase::Wss))return false;
  ws_.beginSslWithBundle(CSOS_WSS_HOST,443,"/",CSOS_CA_BUNDLE,sizeof(CSOS_CA_BUNDLE),"binary");
  ws_.setReconnectInterval(0);
  ws_.setAuthorization(user_.c_str(),password_.c_str());
  const uint32_t started=millis();
  while(!connected() && !failed_ && millis()-started<15000) {
    if(!progress(ConnectionPhase::Wss))return false;
    service();delay(1);
  }
  if(!progress(ConnectionPhase::Wss))return false;
  if(!connected()) { CSOS_LOGF("[CSOS] wss_failed after_ms=%lu failed_flag=%d\n",(unsigned long)(millis()-started),(int)failed_.load()); stop(); return false; }
  CSOS_LOGF("[CSOS] wss_connected after_ms=%lu\n",(unsigned long)(millis()-started));
  ws_.setReconnectInterval(60000);
  return true;
}

void CxlWssStream::service(bool receive) {
  if(owner_!=xTaskGetCurrentTaskHandle()) { fail(TransportFailure::Owner); return; }
  if(failed_) { ws_.disconnect(); connected_=false; return; }
  // Drain incoming records before uplink work. Otherwise microphone writes can
  // repeatedly postpone WS polling while keyboard/ACK producers fill tx_.
  if(receive && !rx_.available()) {
    const uint32_t pollStartedUs=micros();
    ws_.loop();
    const uint32_t pollUs=micros()-pollStartedUs;
    receivePollUs_+=pollUs;
    MediaTelemetry::peak(MediaTelemetry::wsPollMaxUs,pollUs);
  }
  if(!connected())return;
  size_t used=0;unsigned controls=0;
#if CSOS_CXL_DIAG_TRACE
  uint32_t diagAckIds[16];
  unsigned diagAckCount=0;
#endif
  Packet packet;
  // TX is serviced even while RX is applying backpressure on a full tile slot.
  for(int i=0;i<16 && connected() && xQueueReceive(tx_,&packet,0)==pdTRUE;i++) {
#if CSOS_CXL_DIAG_TRACE
    if(ackTxTrace_ && packet.size==6 && packet.data[0]=='A' && packet.data[1]==4 && diagAckCount<16) {
      const uint8_t* p=packet.data+2;
      diagAckIds[diagAckCount++]=uint32_t(p[0])|uint32_t(p[1])<<8|
        uint32_t(p[2])<<16|uint32_t(p[3])<<24;
    }
#endif
    memcpy(txBatch_+used,packet.data,packet.size);used+=packet.size;controls++;
  }
  AudioPacket mic;
  const uint32_t now=millis();
  if(uint32_t(now-lastAudioTxAt_)>=20){
    lastAudioTxAt_=now;
    for(int i=0;i<2 && audioTx_ && xQueueReceive(audioTx_,&mic,0)==pdTRUE;i++){
      if(uint32_t(now-mic.at)>120)continue;
      memcpy(txBatch_+used,mic.data,mic.size);used+=mic.size;
    }
  }
  // 16*64 input bytes + 2*170 audio bytes <= 1400, independent of backlog.
  if(used){
#if CSOS_CXL_DIAG_TRACE
    // Library submission, not proof of TCP wire send or server receipt.
    const uint32_t diagStartedUs=micros();
    const bool diagSubmitted=ws_.sendBIN(txBatch_,used);
    const uint32_t diagFinishedUs=micros();
    if(ackTxTrace_)
      for(unsigned i=0;i<diagAckCount;i++)
        ackTxTrace_->record(diagAckIds[i],diagStartedUs,diagFinishedUs,diagSubmitted);
    if(!diagSubmitted){fail(TransportFailure::TxWrite);return;}
#else
    if(!ws_.sendBIN(txBatch_,used)){fail(TransportFailure::TxWrite);return;}
#endif
    sentCount_+=controls;
  }
}

int CxlWssStream::available() { service(); return connected()?(int)rx_.available():0; }
int CxlWssStream::read(uint8_t* destination,size_t size) {
  if(!available()) return connected()?0:-1;
  return (int)rx_.read(destination,size);
}
size_t CxlWssStream::write(const uint8_t* data,size_t size) {
  if(!connected() || !tx_ || size>sizeof(Packet::data)) return 0;
  Packet packet={}; packet.size=(uint8_t)size;
  if(size) memcpy(packet.data,data,size);
  if(xQueueSend(tx_,&packet,0)!=pdTRUE) { fail(TransportFailure::TxQueue); return 0; }
  queuedCount_++;
  return size;
}
#if CSOS_CXL_DIAG_TRACE
bool CxlWssStream::queueDiagnosticEcho(const uint8_t* nonce4) {
  if(!nonce4 || !connected() || !tx_ || uxQueueSpacesAvailable(tx_)<=4)return false;
  Packet p={};p.size=6;p.data[0]='e';p.data[1]=4;
  memcpy(p.data+2,nonce4,4);
  if(xQueueSend(tx_,&p,0)!=pdTRUE)return false;
  queuedCount_++;
  return true;
}
#endif

bool CxlWssStream::queueMotion(int16_t dx,int16_t dy){
  // Continuous pointer movement must not consume the slots needed by key/button
  // releases, frame ACKs and stop/mute. Caller retains unsent movement.
  if(!connected()||!tx_||uxQueueSpacesAvailable(tx_)<=4)return false;
  Packet packet={};packet.size=6;packet.data[0]='M';packet.data[1]=4;
  packet.data[2]=uint8_t(dx);packet.data[3]=uint8_t(uint16_t(dx)>>8);
  packet.data[4]=uint8_t(dy);packet.data[5]=uint8_t(uint16_t(dy)>>8);
  if(xQueueSend(tx_,&packet,0)!=pdTRUE)return false;
  queuedCount_++;return true;
}
bool CxlWssStream::queueAudioControl(const uint8_t* data,size_t size){
  if(!connected()||!tx_||size>sizeof(Packet::data))return false;
  Packet p={};p.size=size;if(size)memcpy(p.data,data,size);
  // Audio control backpressure is retried by its owner, never a fatal desktop
  // input-queue fault. Stop/mute goes before ordinary queued key events.
  return xQueueSendToFront(tx_,&p,0)==pdTRUE;
}
bool CxlWssStream::queueMicrophone(const uint8_t* data,size_t size){
  if(!connected()||!audioTx_||size>168)return false;
  AudioPacket p={};p.size=size+2;p.at=millis();p.data[0]='m';p.data[1]=size;memcpy(p.data+2,data,size);
  return xQueueSend(audioTx_,&p,0)==pdTRUE;
}
void CxlWssStream::clearMicrophone(){if(audioTx_)xQueueReset(audioTx_);}
void CxlWssStream::stop() {
  if(owner_ && owner_!=xTaskGetCurrentTaskHandle()) { fail(TransportFailure::Owner); return; }
  ws_.disconnect(); connected_=false; fragmented_=false; rx_.reset();
  if(tx_) xQueueReset(tx_);
  clearMicrophone();
}
