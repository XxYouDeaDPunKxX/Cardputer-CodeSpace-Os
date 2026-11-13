#pragma once
#include "TouchFilter.h"
#include <cstdlib>
inline uint32_t& localTouchGeneration(){static uint32_t generation=0;return generation;}
// A contact belongs to the context where it started, through its release.
struct TouchTap {
 bool held=false,moved=false;int firstX=0,firstY=0;uint64_t owner=0;uint32_t generation=0;
 bool sample(const FilteredTouch& p,uint64_t context,int& x,int& y){
  if(held&&(owner!=context||generation!=localTouchGeneration()))moved=true;
  if(p.pending)return false;
  if(!p.down){if(!held)return false;held=false;if(moved)return false;x=firstX;y=firstY;return true;}
  if(!held){held=true;moved=false;firstX=p.x;firstY=p.y;owner=context;generation=localTouchGeneration();}
  else if(std::abs(p.x-firstX)>10||std::abs(p.y-firstY)>10)moved=true;
  return false;
 }
};
