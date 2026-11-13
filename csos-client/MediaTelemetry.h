#pragma once
#include <atomic>
#include <cstdint>
#include "RxAudioProbe.h"
// Aggregated peaks only: no per-packet serial output or PCM capture.
namespace MediaTelemetry {
inline std::atomic<uint32_t> slotWaitUs{0},queueWaitUs{0},drawUs{0};
inline std::atomic<uint32_t> frames{0},tiles{0},audioBytes{0},arrivalGapUs{0};
inline std::atomic<uint32_t> drawSumUs{0},jpegDecodeUs{0},dmaWaitUs{0};
inline std::atomic<uint32_t> wsPollMaxUs{0},wsChunks{0};
inline void peak(std::atomic<uint32_t>& value,uint32_t sample){
 uint32_t before=value.load(std::memory_order_relaxed);
 while(before<sample&&!value.compare_exchange_weak(before,sample,std::memory_order_relaxed)){}
}
}
