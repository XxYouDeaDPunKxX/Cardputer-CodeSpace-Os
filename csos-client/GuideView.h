#pragma once
#include "HudView.h"
#include "LocalPage.h"

inline AppRect guideTftControl(int index){
 static const AppRect controls[]={{8,218,22,17},{34,218,22,17},{135,218,22,17},{161,218,22,17},{273,218,36,17}};
 return controls[index];
}
inline uint8_t guideTftKey(int x,int y){
 const uint8_t keys[]={';','.',',','/','`'};
 for(int i=0;i<5;++i)if(guideTftControl(i).contains(x,y))return keys[i];
 return 0;
}
struct GuidePage {const char* command;std::vector<String> lines;};
template<class D> std::vector<GuidePage> guidePages(D& d,int topic,bool small){
 std::vector<GuidePage> pages;
 const auto* content=hudGuideTopic(topic);if(!content)return pages;
 const int rows=small?2:4,width=small?194:292;
 for(const auto& item:content->items){
  if(!item.command)break;
  const auto lines=localWrap(d,item.text,width,small?.80f:.9f);
  for(size_t first=0;first<lines.size();first+=rows){
   GuidePage page;page.command=item.command;
   for(size_t i=first;i<lines.size()&&i<first+rows;++i)page.lines.push_back(lines[i]);
   pages.push_back(page);
  }
 }
 return pages;
}
// One operation per page; its heading stays visible on every continuation.
template<class D> void drawGuidePage(D& d,int topic,int part,const GuidePage& page,int total,bool small){
 using namespace UiPalette;
 if(small){
  appHudFrame(d,"GUIDA");appHudRow(d,0,hudGuideTopic(topic)->title,.65f,Muted);
  appHudRow(d,1,page.command,.80f,AppColors::Action);
  for(size_t i=0;i<page.lines.size();++i)appHudRow(d,i+2,page.lines[i]);
  appHudFooter(d,HudPageKeys::Read,part,total);
  d.fillRect(74,94,130,19,Background);
  appText(d,76,96,String(topic+1)+"/"+String(GUIDE_PAGES),.55f,Muted);
  paintPhysicalKey(d,UI_LEFT,120,96,17,15);paintPhysicalKey(d,UI_RIGHT,140,96,17,15);
  appText(d,161,96,String(part+1)+"/"+String(total),.55f,Muted);return;
 }
 const int width=small?240:320,footer=small?HudContentHeight:240;
 d.fillRect(0,0,width,footer,Background);
 appText(d,8,small?2:8,hudGuideTopic(topic)->title,small?.75f:.95f,Ink);
 d.drawFastHLine(8,small?24:40,width-16,Rule);
 appText(d,8,small?26:52,page.command,small?.80f:.90f,AppColors::Action);
 for(size_t i=0;i<page.lines.size();++i)
  appText(d,small?8:14,(small?48:87)+int(i)*(small?22:27),page.lines[i],small?.80f:.9f,Ink);
 const int nav=small?95:218;
 auto key=[&](int id,int index,int x,int w){const auto r=small?AppRect{x,nav,w,12}:guideTftControl(index);paintPhysicalKey(d,id,r.x,r.y,r.w,r.h);};
 key(UI_UP,0,8,14);
 key(UI_DOWN,1,26,14);
 appText(d,small?46:64,nav,String(topic+1)+"/"+String(GUIDE_PAGES),small?.58f:.65f,Muted);
 key(UI_LEFT,2,100,14);
 key(UI_RIGHT,3,118,14);
 appText(d,small?138:191,nav,String(part+1)+"/"+String(total),small?.58f:.65f,Muted);
 if(small)appText(d,208,nav,"esc",.58f,Orange);
 else key(UI_ESC,4,273,36);
}
