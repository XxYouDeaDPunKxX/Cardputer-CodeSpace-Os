#pragma once
#include <cstdint>
struct CpuWindow {
 uint32_t clock=0,idle0=0,idle1=0;bool initialized=false,valid=false;uint8_t percent=0;
 bool sample(uint32_t now,uint32_t first,uint32_t second){
  const uint32_t elapsed=now-clock;
  const uint64_t idle=uint64_t(uint32_t(first-idle0))+uint32_t(second-idle1);
  clock=now;idle0=first;idle1=second;
  if(!initialized){initialized=true;return valid=false;}
  const uint64_t capacity=uint64_t(elapsed)*2;
  if(!elapsed||idle>capacity+200)return valid=false;
  const uint64_t used=idle>=capacity?0:capacity-idle;
  percent=uint8_t((used*100+capacity/2)/capacity);return valid=true;
 }
};
