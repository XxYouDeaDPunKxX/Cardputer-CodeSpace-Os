#pragma once
#include <cstring>
enum class TransportFailure { None, Connect, Clock, Http401, Http403,
  EndpointUnavailable, Redirect, UpgradeTimeout, Disconnected, Protocol,
  TxQueue, TxWrite, RxBlock, Owner };
inline bool transportNeedsManualRetry(TransportFailure f) {
  return f==TransportFailure::Http401 || f==TransportFailure::Http403;
}
inline TransportFailure classifyTransportClose(const char* reason) {
  if(!reason) return TransportFailure::Connect;
  if(std::strstr(reason,"HTTP 401")) return TransportFailure::Http401;
  if(std::strstr(reason,"HTTP 403")) return TransportFailure::Http403;
  if(std::strstr(reason,"HTTP 404")) return TransportFailure::EndpointUnavailable;
  if(std::strstr(reason,"HTTP 30")) return TransportFailure::Redirect;
  if(std::strstr(reason,"Header response timeout")) return TransportFailure::UpgradeTimeout;
  if(std::strstr(reason,"Connection lost")) return TransportFailure::Disconnected;
  return TransportFailure::Connect;
}
inline const char* transportFailureText(TransportFailure f) {
  switch(f) {
    case TransportFailure::None: return "Connessione WSS";
    case TransportFailure::Clock: return "Sincronizzazione ora fallita";
    case TransportFailure::Http401: return "HTTP 401: accesso negato";
    case TransportFailure::Http403: return "HTTP 403: accesso vietato";
    case TransportFailure::EndpointUnavailable: return "HTTP 404: porta non trovata";
    case TransportFailure::Redirect: return "HTTP 3xx: reindirizzamento";
    case TransportFailure::UpgradeTimeout: return "Risposta WSS: tempo scaduto";
    case TransportFailure::Disconnected: return "Collegamento interrotto";
    case TransportFailure::Protocol: return "Messaggio WSS non valido";
    case TransportFailure::TxQueue: return "Coda invio satura";
    case TransportFailure::TxWrite: return "Invio WSS fallito";
    case TransportFailure::RxBlock: return "Blocco WSS non accettato";
    case TransportFailure::Owner: return "Accesso WSS fuori task";
    default: return "Rete/TLS: WSS non raggiunto";
  }
}
