#pragma once
#include <cstdint>
#include <cstdlib>
struct TouchClick {
  bool valid=false;
  uint16_t x=0,y=0;
  uint32_t frame=0;
  uint8_t button=0;
};
class DirectTouch {
  bool pressed_=false,canceled_=false,fired_=false,blocked_=false;
  int x_=0,y_=0;
  uint32_t started_=0,frame_=0;
  TouchClick click(uint8_t button) const { return {true,(uint16_t)x_,(uint16_t)y_,frame_,button}; }
public:
  void cancel() { pressed_=canceled_=fired_=false; blocked_=true; }
  TouchClick sample(bool down,int x,int y,uint32_t now,uint32_t frame) {
    if(blocked_) { if(!down) blocked_=false; return {}; }
    if(!down) {
      TouchClick event;
      if(pressed_ && !canceled_ && !fired_) event=click(now-started_>=600?1:0);
      pressed_=canceled_=fired_=false;
      return event;
    }
    if(x<0 || x>=320 || y<0 || y>=240) { cancel(); return {}; }
    if(!pressed_) {
      pressed_=true; canceled_=fired_=false;
      x_=x;y_=y;started_=now;frame_=frame;
      return {};
    }
    if(std::abs(x-x_)>8 || std::abs(y-y_)>8) canceled_=true;
    if(!canceled_ && !fired_ && now-started_>=600) { fired_=true; return click(1); }
    return {};
  }
};
