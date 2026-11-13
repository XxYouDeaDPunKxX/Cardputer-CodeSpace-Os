#pragma once
#include <cstdint>
#ifndef CSOS_DIAGNOSTIC_TELEMETRY
#define CSOS_DIAGNOSTIC_TELEMETRY 0
#endif
static_assert(sizeof(uint32_t)==4,"Diagnostic timestamps require 32 bits");

enum class DiagnosticArea : uint8_t { Session, Wifi, Nvs, Cloud, Input, Boot };
enum class DiagnosticSeverity : uint8_t { Notice, Warning, Error };

struct DiagnosticRecord {
  uint32_t atMs = 0;
  uint16_t code = 0;
  DiagnosticArea area = DiagnosticArea::Session;
  DiagnosticSeverity severity = DiagnosticSeverity::Notice;

  void retain(DiagnosticRecord event) {
    if (event.code && event.severity != DiagnosticSeverity::Notice &&
        (!code || event.severity >= severity)) *this = event;
  }
};
static_assert(sizeof(DiagnosticRecord)<=8,"Diagnostic record exceeds static budget");

enum class SessionExit : uint16_t {
  None, User, Peer, Replaced, ServerClosed, WssLost, RxIdle, RxQueue,
  RxSlot, TxQueue, TxWrite, WssReceive, WssProtocol, TransportOwner,
  TileSize, CxlProtocol, Jpeg, Lossless, RxTask
};

inline const char* sessionExitText(SessionExit cause) {
  switch (cause) {
    case SessionExit::User: return "Disconnessione richiesta";
    case SessionExit::Peer: return "Disconnected by the PC";
    case SessionExit::Replaced: return "Another client took over";
    case SessionExit::ServerClosed: return "Server closed session";
    case SessionExit::WssLost: return "Connection lost";
    case SessionExit::RxIdle: return "Server stopped responding";
    case SessionExit::RxQueue: return "Coda video satura";
    case SessionExit::RxSlot: return "Attesa slot video scaduta";
    case SessionExit::TxQueue: return "Coda invio satura";
    case SessionExit::TxWrite: return "Invio WSS fallito";
    case SessionExit::WssReceive: return "Blocco WSS non accettato";
    case SessionExit::WssProtocol: return "Messaggio WSS non valido";
    case SessionExit::TransportOwner: return "Accesso WSS fuori task";
    case SessionExit::TileSize: return "Tile too big";
    case SessionExit::CxlProtocol: return "Protocol error";
    case SessionExit::Jpeg: return "JPEG decode failed";
    case SessionExit::Lossless: return "Lossless video decode failed";
    case SessionExit::RxTask: return "RX task allocation failed";
    case SessionExit::None: return "";
  }
  return "";
}

inline DiagnosticSeverity sessionExitSeverity(SessionExit cause) {
  if (cause == SessionExit::None || cause == SessionExit::User ||
      cause == SessionExit::Peer || cause == SessionExit::Replaced) return DiagnosticSeverity::Notice;
  if (cause == SessionExit::ServerClosed || cause == SessionExit::WssLost) return DiagnosticSeverity::Warning;
  return DiagnosticSeverity::Error;
}
