#pragma once
#include <cstdint>
namespace AudioProfile {
constexpr uint8_t Version = 5;
constexpr unsigned Rate = 12000;
constexpr unsigned SamplesPerMs = Rate / 1000;
constexpr unsigned DownlinkSamples = Rate / 50;  // 20 ms
constexpr unsigned MicrophoneSamples = Rate / 100;  // 10 ms
constexpr unsigned PrimeSamples = Rate * 80 / 1000;
constexpr unsigned LateSamples = Rate * 160 / 1000;
static_assert(Rate % 1000 == 0, "integer samples per millisecond");
}
