#pragma once
#include <cstdint>
#include <cstdio>
#ifndef CSOS_FPS_PROBE
#define CSOS_FPS_PROBE 0
#endif
// UI-task counters: one frame only after all tiles are drawn. No I/O here.
struct FpsProbe {
 uint32_t selectedAt=0,lastAt=0,total=0,lastTotal=0,sequence=0;
 uint8_t cap=255;
 void select(uint32_t now,uint8_t value){
  selectedAt=lastAt=now;lastTotal=total;cap=value;++sequence;
 }
 void frame(){++total;}
 bool due(uint32_t now)const{return uint32_t(now-lastAt)>=1000;}
 int format(char* line,size_t size,const char* event,uint32_t now)const{
  return snprintf(line,size,"[CSOS] fps_probe event=%s ms=%lu seq=%lu cap=%u selected_ms=%lu dt_ms=%lu frames=%lu total=%lu\n",
   event,(unsigned long)now,(unsigned long)sequence,unsigned(cap),(unsigned long)selectedAt,
   (unsigned long)(now-lastAt),(unsigned long)(total-lastTotal),(unsigned long)total);
 }
 void delivered(uint32_t now){lastAt=now;lastTotal=total;}
};
