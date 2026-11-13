#pragma once
// Minimal opt-in, one-writer per-frame CXL trace. No serial, allocations, locks.
// All times come from ESP32 micros(); never subtract absolute server timestamps.
#include <stdint.h>
#include <stddef.h>
namespace CxlDiag {
#pragma pack(push,1)
struct FrameTrace16 {
  uint32_t frame_id,first_tile_rx_us;
  uint16_t last_tile_rx_64,f_rx_64,draw_done_64,ack_queued_64;
};
#pragma pack(pop)
static_assert(sizeof(FrameTrace16)==16,"CXL frame trace size must remain 16B");

struct Stats {
  uint32_t total=0,sampled=0,anomalies=0;
  uint32_t sampled_overwritten=0,anomalies_overwritten=0;
  uint32_t invalid=0,saturated=0;
};

// Single writer: the UI task which owns drawing and ACK enqueue.
// The rxTask annotates F's existing RxItem; no global cross-core write to ring.
class Recorder {
  FrameTrace16 samples_[256]{};
  FrameTrace16 unusual_[64]{};
  Stats stats_{};
  static uint16_t quantize(uint32_t first,uint32_t event,bool& saturated) {
    const uint32_t elapsed=uint32_t(event-first);
    if (elapsed > 65535u*64u) { saturated=true;return 65535u; }
    return static_cast<uint16_t>((elapsed+32u)/64u);
  }
public:
  void reset(){stats_=Stats{};}
  void recordFrame(uint32_t id,uint32_t first,uint32_t lastRx,uint32_t fRx,
      uint32_t draw,uint32_t ack,uint16_t receivedTiles,uint16_t drawnTiles,bool ackQueued) {
    ++stats_.total;
    bool saturated=false;
    const bool invalid = receivedTiles==0 || receivedTiles!=drawnTiles || !ackQueued;
    FrameTrace16 row{id,first,
      quantize(first,lastRx,saturated),quantize(first,fRx,saturated),
      quantize(first,draw,saturated),quantize(first,ack,saturated)};
    if(invalid)++stats_.invalid;
    if(saturated)++stats_.saturated;
    if ((id & 3u)==0u) {
      samples_[stats_.sampled % 256u]=row;
      if(stats_.sampled>=256u)++stats_.sampled_overwritten;
      ++stats_.sampled;
    }
    if(invalid||saturated) {
      unusual_[stats_.anomalies % 64u]=row;
      if(stats_.anomalies>=64u)++stats_.anomalies_overwritten;
      ++stats_.anomalies;
    }
  }
  Stats stats() const {return stats_;}
  const FrameTrace16& sampledAt(uint32_t index) const {return samples_[index%256u];}
  const FrameTrace16& anomalyAt(uint32_t index) const {return unusual_[index%64u];}
};
} // namespace CxlDiag
