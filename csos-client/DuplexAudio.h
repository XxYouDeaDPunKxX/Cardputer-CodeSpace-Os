#pragma once
#include <Arduino.h>
#include <atomic>
#include "CxlWssStream.h"
namespace DuplexAudio {
extern std::atomic<bool> running,healthy,micWanted,micActive,listen,negotiated,hostMic,traceWanted;
extern std::atomic<int> volume,micDb,outDb,remoteState,errorCode;
extern std::atomic<uint32_t> rxDrops,txSilence,limited,micProcessMaxUs,micFrames,traceDrops,rxOverflows,micLimited;
extern std::atomic<uint16_t> token,captureEpoch;
bool begin(CxlWssStream* stream);
void end();
void receive(const uint8_t* data,size_t n);
void accept(uint8_t version,uint16_t rate,uint16_t session);
void microphone(bool on);
}
