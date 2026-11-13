#pragma once
#include <cstdint>
struct ControlGesture {
 enum Action {None,Cycle,Open,Close};
 bool held=false,fired=false,localAtPress=false;uint32_t since=0;
 void reset(){*this={};}
 Action update(bool down,uint32_t now,bool local){
  if(down&&!held){held=true;fired=false;localAtPress=local;since=now;}
  if(down&&held&&!fired&&uint32_t(now-since)>=1000){fired=true;return Open;}
  if(!down&&held){held=false;if(!fired)return localAtPress?Close:Cycle;}
  return None;
 }
};
