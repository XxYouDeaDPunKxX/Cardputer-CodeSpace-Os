#pragma once
// TX owner only. Audit ACK IDs passed to websocket sendBIN, never treat as
// server-received. Exactly one writer, no heap allocation or serial logging.
#include <stdint.h>
#include <stddef.h>
namespace CxlDiag {
#pragma pack(push,1)
struct AckTx12 {
 uint32_t frame_id,started_us;
 uint16_t duration_64,status;
};
#pragma pack(pop)
static_assert(sizeof(AckTx12)==12,"ACK TX marker contract");
struct AckTxStats {
 uint32_t total=0,sampled=0,failures=0;
 uint32_t sampled_overwritten=0,failures_overwritten=0,duration_saturated=0;
};
class AckTxRecorder {
 AckTx12 sampled_[256]{};
 AckTx12 failed_[16]{};
 AckTxStats stats_{};
public:
 void reset(){stats_=AckTxStats{};}
 void record(uint32_t id,uint32_t started,uint32_t finished,bool success) {
  const uint32_t elapsed=uint32_t(finished-started);
  const bool saturated=elapsed>65535u*64u;
  if(saturated)++stats_.duration_saturated;
  AckTx12 row{id,started,
    saturated?uint16_t(65535u):static_cast<uint16_t>((elapsed+32u)/64u),
    static_cast<uint16_t>((success?1u:2u)|(saturated?4u:0u))};
  ++stats_.total;
  if(success) {
   if((id&3u)==0u) {
    sampled_[stats_.sampled%256u]=row;
    if(stats_.sampled>=256u)++stats_.sampled_overwritten;
    ++stats_.sampled;
   }
  } else {
   failed_[stats_.failures%16u]=row;
   if(stats_.failures>=16u)++stats_.failures_overwritten;
   ++stats_.failures;
  }
 }
 AckTxStats stats()const{return stats_;}
 const AckTx12& sampledAt(uint32_t i)const{return sampled_[i%256u];}
 const AckTx12& failedAt(uint32_t i)const{return failed_[i%16u];}
};
} // namespace CxlDiag
