#pragma once
#include <algorithm>
#include <cstdint>

struct FilteredTouch {bool down=false;int x=0,y=0;bool pending=false;};
class TouchFilter {
 int xs_[3]{},ys_[3]{};
 uint8_t count_=0,next_=0,missing_=0;
 FilteredTouch point_;
 static int median(int a,int b,int c){return a+b+c-std::min(a,std::min(b,c))-std::max(a,std::max(b,c));}
 FilteredTouch missing(){
  if(!point_.down){count_=next_=0;return point_;}
  if(++missing_>=3)reset();
  return point_;
 }
public:
 static constexpr int Pressure=300;
 void reset(){count_=next_=missing_=0;point_={};}
 FilteredTouch sample(bool irq,int pressure,int x,int y){
  if(!irq||pressure<Pressure)return missing();
  xs_[next_]=x;ys_[next_]=y;next_=(next_+1)%3;
  if(count_<3)++count_;
  if(count_<3)return {false,0,0,true};
  x=median(xs_[0],xs_[1],xs_[2]);y=median(ys_[0],ys_[1],ys_[2]);
  // Filter before rejecting border outliers; never clamp noise onto an edge.
  if(x<0||x>=320||y<0||y>=240)return missing();
  missing_=0;point_={true,x,y};return point_;
 }
};
