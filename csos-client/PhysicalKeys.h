#pragma once
#include "UiPalette.h"
#include <cstdlib>
// IDs are the verified 4x14 matrix, not character values or arbitrary UI icons.
constexpr int UI_ESC=0,UI_BACK=13,UI_TAB=14,UI_FN=28,UI_AA=29,UI_UP=39,
 UI_ENTER=41,UI_CTRL=42,UI_OPT=43,UI_ALT=44,UI_LEFT=52,UI_DOWN=53,UI_RIGHT=54,UI_SPACE=55;
const char* const uiKeyLabels[56]={"esc","1","2","3","4","5","6","7","8","9","0","-","=","",
 "tab","Q","W","E","R","T","Y","U","I","O","P","[","]","\\",
 "fn","Aa","A","S","D","F","G","H","J","K","L",";","'","",
 "ctrl","opt","alt","Z","X","C","V","B","N","M",",",".","/",""};
template<class D> void uiLine(D& d,int x0,int y0,int x1,int y1,uint16_t color){
 const int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=-std::abs(y1-y0),sy=y0<y1?1:-1;int error=dx+dy;
 while(true){d.fillRect(x0,y0,1,1,color);if(x0==x1&&y0==y1)break;const int e=2*error;if(e>=dy){error+=dy;x0+=sx;}if(e<=dx){error+=dx;y0+=sy;}}
}
template<class D> void paintPhysicalKey(D& d,int id,int x,int y,int w=30,int h=23,bool active=false,bool indicator=false){
 using namespace UiPalette;
 const bool colored=id==UI_FN||id==UI_ESC||id==UI_AA||id==UI_OPT;
 const auto accent=id==UI_AA?Blue:id==UI_OPT?Green:Orange;
 const bool filled=colored&&(!indicator||active);
 const uint16_t face=id==UI_AA?AaFace:id==UI_FN||id==UI_ESC?FnFace:accent;
 const uint16_t bg=filled?face:Background;
 const uint16_t fg=filled?(id==UI_OPT?KeyInk:White):colored?accent:Ink;
 // Reproduce the printed legend, not a generic outlined PC keycap.
 d.fillRect(x,y,w,h,bg);
 if(id==UI_UP)d.fillTriangle(x+w/2,y+4,x+5,y+h-4,x+w-5,y+h-4,Orange);
 else if(id==UI_DOWN)d.fillTriangle(x+5,y+4,x+w-5,y+4,x+w/2,y+h-4,Orange);
 else if(id==UI_LEFT)d.fillTriangle(x+4,y+h/2,x+w-4,y+4,x+w-4,y+h-4,Orange);
 else if(id==UI_RIGHT)d.fillTriangle(x+w-4,y+h/2,x+4,y+4,x+4,y+h-4,Orange);
 else if(id==UI_ENTER){uiLine(d,x+w-7,y+5,x+w-7,y+12,fg);uiLine(d,x+w-7,y+12,x+6,y+12,fg);uiLine(d,x+6,y+12,x+11,y+7,fg);uiLine(d,x+6,y+12,x+11,y+17,fg);}
 else if(id==UI_BACK){uiLine(d,x+9,y+5,x+w-4,y+5,fg);uiLine(d,x+w-4,y+5,x+w-4,y+h-5,fg);uiLine(d,x+w-4,y+h-5,x+9,y+h-5,fg);uiLine(d,x+9,y+h-5,x+3,y+h/2,fg);uiLine(d,x+3,y+h/2,x+9,y+5,fg);}
 else if(id==UI_SPACE){uiLine(d,x+5,y+8,x+5,y+15,fg);uiLine(d,x+5,y+15,x+w-5,y+15,fg);uiLine(d,x+w-5,y+15,x+w-5,y+8,fg);}
 else{d.setFont(&fonts::Font4);d.setTextSize(id==UI_ESC||h<21?0.65f:0.75f);d.setTextColor(fg,bg);d.setCursor(x+2,y+2);d.print(uiKeyLabels[id]);}
 if(id==11||id==12||id==25||id==26){
  d.setFont(&fonts::Font4);d.setTextSize(.55f);d.setTextColor(Blue,bg);d.setCursor(x+w-11,y+1);
  d.print(id==11?"_":id==12?"+":id==25?"{":"}");
 }
 if(active&&!indicator)d.fillRect(x,y+h-2,w,2,Blue);
}
