#pragma once
#include <Arduino.h>
#include <WebSocketsClient.h>
#include <atomic>
#include "RxBlock.h"
#include "TransportStatus.h"
#include "ConnectionAttempt.h"
#ifndef CSOS_CXL_DIAG_TRACE
#define CSOS_CXL_DIAG_TRACE 0
#endif
#if CSOS_CXL_DIAG_TRACE
#include "CxlDiagnosticTx.h"
#endif

// Handshake runs in the caller. The session then explicitly transfers ownership
// to rxTask; UI/rendering only enqueue copied writes. Stop follows RX exit.
class CxlWssStream {
  struct Packet { uint8_t size; uint8_t data[64]; };
  WebSocketsClient ws_;
  RxBlock rx_;
  struct AudioPacket {uint8_t size;uint32_t at;uint8_t data[170];};
  QueueHandle_t tx_=nullptr,audioTx_=nullptr;
  // Ordered CXL records share a bounded WS/TLS write; no extra RX owner.
  uint8_t txBatch_[1400];
  uint32_t lastAudioTxAt_=0;
  uint32_t receivePollUs_=0; // Owned by the RX task, read by its audio delivery callback.
  TaskHandle_t owner_=nullptr;
  std::atomic<bool> connected_{false}, failed_{false};
  std::atomic<TransportFailure> failure_{TransportFailure::None};
  bool fragmented_=false;
  String user_,password_;
  std::atomic<uint32_t> queuedCount_{0},sentCount_{0};
#if CSOS_CXL_DIAG_TRACE
  CxlDiag::AckTxRecorder* ackTxTrace_=nullptr; // optional, desktop stream only
#endif
  bool (*progress_)(ConnectionPhase)=nullptr;
  void event(WStype_t type,uint8_t* data,size_t size);
  void fail(TransportFailure cause);
public:
  explicit CxlWssStream(bool useMfl=false);
#if CSOS_CXL_DIAG_TRACE
  void setDiagnosticTx(CxlDiag::AckTxRecorder* recorder){ackTxTrace_=recorder;}
  bool queueDiagnosticEcho(const uint8_t* nonce4); // nonfatal; preserve reserved ACK slots
#endif
  bool connect();
  void setConnectProgress(bool (*progress)(ConnectionPhase)){progress_=progress;}
  void setCredentials(const String& user,const String& password) { user_=user; password_=password; }
  void setNoDelay(bool) {}
  bool connected() const { return connected_.load() && !failed_.load(); }
  TransportFailure failure() const { return failure_.load(); }
  void claimOwner() { owner_=xTaskGetCurrentTaskHandle(); }
  void service(bool receive=true);
  int available();
  uint32_t receivePollUs() const { return receivePollUs_; }
  int read(uint8_t* destination,size_t size);
  size_t write(const uint8_t* data,size_t size);
  bool queueMotion(int16_t dx,int16_t dy);
  bool microphoneReady()const{return audioTx_&&connected();}
  bool queueAudioControl(const uint8_t* data,size_t size);
  bool queueMicrophone(const uint8_t* data,size_t size);
  void clearMicrophone();
  void stop();
  uint32_t queuedCount() const { return queuedCount_; }
  uint32_t sentCount() const { return sentCount_; }
};
