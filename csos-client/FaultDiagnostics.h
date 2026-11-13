#pragma once
#include "SessionDiagnostic.h"
#include <cstddef>

enum class FaultArea : uint8_t { Wifi, Nvs, Cloud, Input, Boot, Count };
enum class FaultCode : uint16_t {
  None, WifiLost, WifiAssociation, WifiRestore, WifiScan,
  NvsOpen, NvsRead, NvsWrite, NvsCommit, NvsVerify,
  CloudIdentity, CloudSubmit, CloudRequest, CloudAccess,
  InputSize, InputQueue, BootHud, BootVideo, BootQueue, BootWorker, BootAudio
};
struct AreaFault {
  DiagnosticRecord event;
  int32_t detail=0;
};
inline AreaFault areaFaults[size_t(FaultArea::Count)];
inline bool areaFaultNotice=false;
// One pending trace per area, coalesced while no safe output point is available.
inline AreaFault pendingFaultTraces[size_t(FaultArea::Count)];
inline uint8_t pendingFaultTraceMask=0;
static_assert(sizeof(areaFaults)<=60,"Diagnostic area records exceed static budget");

// Called by the UI task, including its synchronous settings operations.
inline void recordFault(FaultArea area,FaultCode code,int32_t detail,uint32_t now,
                        DiagnosticSeverity severity=DiagnosticSeverity::Error,bool preserveCause=false,bool notify=true) {
  auto& stored=areaFaults[size_t(area)];
  pendingFaultTraces[size_t(area)]={{now,uint16_t(code),DiagnosticArea(size_t(area)+1),severity},detail};
  pendingFaultTraceMask|=uint8_t(1u<<size_t(area));
  if((area==FaultArea::Boot||preserveCause)&&stored.event.code)return;
  if(stored.event.code&&stored.event.severity>severity)return;
  stored.event={now,uint16_t(code),DiagnosticArea(size_t(area)+1),severity};
  stored.detail=detail;
  areaFaultNotice|=notify;
}
inline uint32_t diagnosticTime(){
#ifdef ARDUINO
  return millis();
#else
  return 0;
#endif
}
inline const char* faultAreaText(FaultArea area){
  switch(area){
    case FaultArea::Wifi:return "Wi-Fi";
    case FaultArea::Nvs:return "Memoria impostazioni";
    case FaultArea::Cloud:return "Cloud";
    case FaultArea::Input:return "Input";
    case FaultArea::Boot:return "Avvio";
    default:return "";
  }
}
inline const char* faultText(FaultCode code){
  switch(code){
    case FaultCode::WifiLost:return "Collegamento perso";
    case FaultCode::WifiAssociation:return "Associazione non riuscita";
    case FaultCode::WifiRestore:return "Ripristino rete fallito";
    case FaultCode::WifiScan:return "Scansione reti fallita";
    case FaultCode::NvsOpen:return "Apertura archivio fallita";
    case FaultCode::NvsRead:return "Lettura non valida";
    case FaultCode::NvsWrite:return "Scrittura fallita";
    case FaultCode::NvsCommit:return "Conferma scrittura fallita";
    case FaultCode::NvsVerify:return "Verifica salvataggio fallita";
    case FaultCode::CloudIdentity:return "Nome diverso da WSS";
    case FaultCode::CloudSubmit:return "Operazione non accodata";
    case FaultCode::CloudRequest:return "Operazione remota fallita";
    case FaultCode::CloudAccess:return "Accesso desktop fallito";
    case FaultCode::InputSize:return "Comando troppo grande";
    case FaultCode::InputQueue:return "Comando non accodato";
    case FaultCode::BootHud:return "Memoria HUD insufficiente";
    case FaultCode::BootVideo:return "Memoria video insufficiente";
    case FaultCode::BootQueue:return "Coda video non creata";
    case FaultCode::BootWorker:return "Task cloud non creato";
    case FaultCode::BootAudio:return "Memoria audio insufficiente";
    default:return "";
  }
}
