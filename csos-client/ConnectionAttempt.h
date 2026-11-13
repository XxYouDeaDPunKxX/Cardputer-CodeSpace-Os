#pragma once
#include <atomic>
#include <cstdint>
enum class ConnectionPhase:uint8_t { Time,Dns,Wss,Cxl,Cancelling };
class ConnectionAttempt {
 std::atomic<bool> cancelled_{false};
 std::atomic<ConnectionPhase> phase_{ConnectionPhase::Wss};
public:
 void begin(){cancelled_=false;phase_=ConnectionPhase::Wss;}
 void cancel(){cancelled_=true;}
 bool cancelled()const{return cancelled_.load();}
 bool update(ConnectionPhase phase){phase_=phase;return !cancelled();}
 ConnectionPhase phase()const{return cancelled()?ConnectionPhase::Cancelling:phase_.load();}
};
inline const char* connectionPhaseText(ConnectionPhase phase){
 switch(phase){
 case ConnectionPhase::Time:return "Sincronizzazione ora";
 case ConnectionPhase::Dns:return "Ricerca indirizzo WSS";
 case ConnectionPhase::Wss:return "Apertura collegamento WSS";
 case ConnectionPhase::Cxl:return "Autenticazione desktop CXL";
 default:return "Annullamento in corso";
 }
}
