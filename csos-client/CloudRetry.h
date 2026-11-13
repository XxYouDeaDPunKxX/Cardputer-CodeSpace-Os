#pragma once
#include <cstdint>
#include <algorithm>
struct CloudRetry {
 uint32_t since=0,delayMs=0;uint8_t attempts=0;
 bool ready(uint32_t now)const{return !delayMs||uint32_t(now-since)>=delayMs;}
 void reset(){since=delayMs=attempts=0;}
 void credentialsChanged(uint32_t now){if(ready(now))reset();} // never bypass a live API deadline
 void observe(int http,uint32_t advised,uint32_t now,bool rateLimited=true){
  if(http==403&&!rateLimited){if(ready(now))reset();return;}
  if(http!=403&&http!=429){if(http>=200&&http<300)reset();return;}
  const uint32_t fallback=60000u<<std::min<unsigned>(attempts,4);
  if(attempts<4)++attempts;
  since=now;delayMs=std::max(advised,fallback);
 }
};
