#pragma once
// RX-owner only: full audio 'S' messages; receive() fragments MUST NOT count.
// No heap/locks/serial in hot path. micros wrap uses uint32 arithmetic.
#include <stdint.h>
#include <stddef.h>
namespace CxlDiag {
#pragma pack(push,1)
struct GapEvent8{uint32_t at_us,gap_us;};
#pragma pack(pop)
static_assert(sizeof(GapEvent8)==8,"Audio gap wire size");
struct AudioGapStats{
 uint32_t messages=0,bytes=0,intervals=0;
 uint32_t long_count=0,overwritten=0,hist_saturated=0;
};
class AudioGapRecorder{
 static constexpr uint32_t Slots=512;
 static constexpr uint32_t LongUs=80000;
 GapEvent8 long_gaps_[Slots]{};
 uint16_t histogram_[256]{};
 AudioGapStats stats_{};
 uint32_t previous_us_=0;
 bool has_previous_=false;
public:
 void reset(){
  stats_=AudioGapStats{};
  for(unsigned i=0;i<256;i++)histogram_[i]=0;
  previous_us_=0;has_previous_=false;
 }
 void record(uint32_t completed_audio_message_us,uint16_t packet_bytes){
  ++stats_.messages;stats_.bytes+=packet_bytes;
  if(has_previous_){
   const uint32_t gap_us=uint32_t(completed_audio_message_us-previous_us_);
   ++stats_.intervals;
   uint32_t bucket=gap_us/1000u;
   if(bucket>255u)bucket=255u;
   if(histogram_[bucket]!=UINT16_MAX)++histogram_[bucket];
   else ++stats_.hist_saturated;
   if(gap_us>=LongUs){
    long_gaps_[stats_.long_count%Slots]={completed_audio_message_us,gap_us};
    if(stats_.long_count>=Slots)++stats_.overwritten;
    ++stats_.long_count;
   }
  }
  previous_us_=completed_audio_message_us;
  has_previous_=true;
 }
 AudioGapStats stats()const{return stats_;}
 uint16_t histogramAt(unsigned i)const{return histogram_[i%256u];}
 GapEvent8 longAt(uint32_t i)const{return long_gaps_[i%Slots];}
};
} // namespace CxlDiag
