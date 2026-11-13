#pragma once
#include <atomic>
#include <cstdint>

// RX is the sole producer; the existing audio worker is the sole consumer.
// One frozen episode, no queue allocation and no serial writes in RX.
namespace RxAudioProbe {
struct Record {
 uint32_t atUs,gapUs,slotUs,queueUs,readUs,idleUs,pollUs,videoRecords,videoBytes,rxBytes;
};
class Probe {
 Record record_{};
 std::atomic<bool> ready_{false};
 uint32_t previousAt_=0,previousPoll_=0;
 bool primed_=false;
public:
 uint32_t slotUs=0,queueUs=0,readUs=0,idleUs=0,videoRecords=0,videoBytes=0,rxBytes=0;
 std::atomic<uint32_t> skipped{0};
 void reset(){ready_=false;primed_=false;skipped=0;clear();}
 void clear(){slotUs=queueUs=readUs=idleUs=videoRecords=videoBytes=rxBytes=0;}
 void audio(uint32_t at,uint32_t poll){
  if(primed_ && uint32_t(at-previousAt_)>=80000){
   if(!ready_.load(std::memory_order_acquire)){
    record_={at,uint32_t(at-previousAt_),slotUs,queueUs,readUs,idleUs,
             uint32_t(poll-previousPoll_),videoRecords,videoBytes,rxBytes};
    ready_.store(true,std::memory_order_release);
   }else skipped.fetch_add(1,std::memory_order_relaxed);
  }
  previousAt_=at;previousPoll_=poll;primed_=true;clear();
 }
 bool take(Record& out){
  if(!ready_.load(std::memory_order_acquire))return false;
  out=record_;ready_.store(false,std::memory_order_release);return true;
 }
};
static_assert(sizeof(Probe)<=128,"RX episode instrumentation must remain bounded");
inline Probe probe;
template<class Clock>struct Timer {
 uint32_t& sum;Clock clock;uint32_t start;
 Timer(uint32_t& target,Clock source):sum(target),clock(source),start(source()){}
 ~Timer(){sum+=uint32_t(clock()-start);}
};
}
