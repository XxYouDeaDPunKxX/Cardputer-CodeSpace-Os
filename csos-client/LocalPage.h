#pragma once
#include <vector>
#include <algorithm>
#include "AppNavigationView.h"
struct LocalPage {
 String title,label,value,help,summary,detail;uint16_t accent=UiPalette::Green;std::vector<String> lines,values;
 int selected=-1,position=0,total=1;
 bool editor=false,secret=false,confirmation=false;int primaryRows=0;
};
struct LocalPageCache {
 bool painted=false,editor=false,confirmation=false;int width=0;uint16_t accent=0;
 String title,label,value,index,help,paintedHelp;String row[4],values[4];bool selected[4]={},primary[4]={};int offset[4]={};
 uint32_t helpSince=0;int helpOffset=0;
 String moving;uint32_t since=0,titleSince=0;int labelOffset=0,titleOffset=0;
 void invalidate(){painted=false;}
};
template<class D> std::vector<String> localWrap(D& d,const String& text,int width,float size){
 d.setFont(&fonts::Font4);d.setTextSize(size,size);
 std::vector<String> result;String line,word;
 auto add=[&](){
  if(!word.length())return;
  String candidate=line.length()?line+" "+word:word;
  if(line.length()&&d.textWidth(candidate)>width){result.push_back(line);line="";}
  if(line.length())line+=' ';
  for(unsigned i=0;i<word.length();i++){String next=line;next+=word[i];if(line.length()&&d.textWidth(next)>width){result.push_back(line);line="";}line+=word[i];}
  word="";
 };
 for(unsigned i=0;i<text.length();i++){if(text[i]==' ')add();else word+=text[i];}add();
 if(line.length())result.push_back(line);
 if(result.empty())result.push_back("");
 return result;
}
inline int localTextOffset(int excess,uint32_t elapsed){
 if(excess<=0)return 0;
 const auto phase=elapsed%(4200+(uint32_t(excess)*1000+17)/18);
 return phase<=1800?0:std::min(excess,int(uint64_t(phase-1800)*18/1000));
}
template<class D> bool drawSmallLocalPage(D& d,const LocalPage& page,LocalPageCache& cache,uint32_t now){
 using namespace UiPalette;
 const bool full=!cache.painted||cache.title!=page.title||cache.editor!=page.editor||cache.confirmation!=page.confirmation;
 if(full){cache.title=page.title;cache.editor=page.editor;cache.confirmation=page.confirmation;cache.titleSince=cache.since=now;cache.titleOffset=cache.labelOffset=-1;}
 if(page.selected>=0&&!page.editor){
  const String selected=page.selected<int(page.lines.size())?page.lines[page.selected]:String("");
  String identity=page.title;for(const auto& line:page.lines)identity+="|"+line;
  if(cache.moving!=selected){cache.moving=selected;cache.since=now;}
  const uint32_t elapsed=now-cache.since;d.setFont(&fonts::Font4);d.setTextSize(.80f);
  const bool scrolling=d.textWidth(selected)>194&&elapsed>1800;
  if(full||cache.index!=String(page.selected)||cache.label!=identity||scrolling){
   appHudFrame(d,"STRUMENTI");appHudList(d,page.lines,page.selected,page.title=="RETI WI-FI"?"WI-FI":page.title=="DIMENTICA RETE"?"DIMENTICA":page.title.length()<=9?page.title.c_str():"STRUMENTI",elapsed);
   if(page.title=="RETI WI-FI")appText(d,119,96,"R",.55f,UiPalette::Orange);
   cache.index=String(page.selected);cache.label=identity;cache.painted=true;return true;
  }return false;
 }
 bool changed=full;
 if(full)appHudFrame(d,page.editor?"ACCESSO":page.confirmation?"CONFERMA":"STATO");
 d.setFont(&fonts::Font4);d.setTextSize(.80f);
 const int titleOffset=localTextOffset(d.textWidth(page.title)-194,now-cache.titleSince);
 if(full||cache.titleOffset!=titleOffset){appHudRow(d,0,page.title,.80f,AppColors::Action,titleOffset);cache.titleOffset=titleOffset;changed=true;}
 if(page.editor){
  if(cache.label!=page.label){cache.label=page.label;cache.since=now;cache.labelOffset=-1;}
  const int offset=localTextOffset(d.textWidth(page.label)-194,now-cache.since);
  if(full||offset!=cache.labelOffset){appHudRow(d,1,page.label,.80f,Ink,offset);cache.labelOffset=offset;changed=true;}
  if(full||cache.value!=page.value){
   cache.value=page.value;String shown=page.value;
   while(shown.length()&&d.textWidth(shown+"_")>194)shown.remove(0,1);
   appHudRow(d,2,shown+"_",.80f);changed=true;
  }
  if(full){appHudRow(d,3,page.secret?"Tab: mostra / nascondi":"Invio: continua",.65f,Muted);appHudFooter(d,HudPageKeys::Edit,0,1,page.secret);}
 }else{
  for(int row=0;row<3;++row){
   const String line=row<int(page.lines.size())?page.lines[row]:String("");
   if(full||cache.row[row]!=line){appHudRow(d,row+1,line);cache.row[row]=line;changed=true;}
  }
  const String index=String(page.position)+":"+String(page.total);
  if(full||cache.index!=index){appHudFooter(d,page.confirmation?HudPageKeys::Confirm:HudPageKeys::Read,page.position,page.total);cache.index=index;changed=true;}
 }
 cache.painted=true;return changed;
}

template<class D> bool drawLocalPage(D& d,const LocalPage& page,LocalPageCache& cache,uint32_t now,int width,int height){
 using namespace UiPalette;
 if(width==240)return drawSmallLocalPage(d,page,cache,now);
 const bool small=width==240;const float size=small?.85f:.9f;
 const int visible=small?2:4,first=small?33:43,step=small?27:38,rowHeight=small?24:32;
 const int nav=small?89:214,footer=small?UiPalette::HudContentHeight:height;
 const bool full=!cache.painted||cache.title!=page.title||cache.editor!=page.editor||cache.confirmation!=page.confirmation||cache.width!=width||cache.accent!=page.accent;
 bool changed=full;
 if(full){
  d.fillRect(0,0,width,footer,Background);cache.title=page.title;cache.editor=page.editor;cache.confirmation=page.confirmation;cache.width=width;cache.accent=page.accent;
  cache.titleSince=now;cache.titleOffset=0;
  paintPhysicalKey(d,UI_ENTER,width-84,2,30,24);paintPhysicalKey(d,page.editor?UI_OPT:UI_ESC,width-45,2,37,24);
  d.drawFastHLine(8,small?28:33,width-16,Rule);
  if(page.editor){
   if(page.secret)paintPhysicalKey(d,UI_TAB,8,nav,35,17);
   d.setTextSize(.65f);d.setTextColor(Muted,Background);d.setCursor(page.secret?48:8,nav);
   d.print(page.secret?"Mostra / nascondi":"Invio continua");
  }else if(!small&&page.confirmation){
   paintPhysicalKey(d,UI_ENTER,8,nav,30,17);d.setTextSize(.65f);d.setTextColor(Ink,Background);d.setCursor(44,nav);d.print("conferma");
   paintPhysicalKey(d,UI_ESC,176,nav,35,17);d.setCursor(218,nav);d.print("annulla");
  }else{paintPhysicalKey(d,UI_UP,8,nav,27,17);paintPhysicalKey(d,UI_DOWN,40,nav,27,17);}
 }
 d.setFont(&fonts::Font4);d.setTextSize(small?.75f:.9f);
 const int titleOffset=localTextOffset(d.textWidth(page.title)-(width-100),now-cache.titleSince);
 if(full||titleOffset!=cache.titleOffset){changed=true;cache.titleOffset=titleOffset;
  d.fillRect(8,3,width-100,26,Background);d.setTextColor(page.accent,Background);
  d.setClipRect(8,3,width-100,26);d.setCursor(8-titleOffset,3);d.print(page.title);d.clearClipRect();}
 d.setFont(&fonts::Font4);d.setTextSize(size,size);
 if(page.editor){
  // Label is complete and scrolls in its own region. Values show the editable tail.
  const int labelY=small?34:48,fieldY=small?62:112,fieldHeight=small?24:34;
  const bool labelChanged=cache.label!=page.label;
  if(labelChanged){cache.label=page.label;cache.since=now;}
  const int offset=localTextOffset(d.textWidth(page.label)-(width-16),now-cache.since);
  if(full||labelChanged||offset!=cache.labelOffset){changed=true;d.fillRect(8,labelY,width-16,25,Background);d.setTextColor(small?Muted:Ink,Background);d.setClipRect(8,labelY,width-16,25);d.setCursor(8-offset,labelY);d.print(page.label);d.clearClipRect();cache.labelOffset=offset;}
  if(full||cache.value!=page.value){
   changed=true;
   cache.value=page.value;appSurface(d,{8,fieldY,width-16,fieldHeight},AppColors::Surface);d.fillRect(12,fieldY+fieldHeight-3,width-24,1,AppColors::Action);
   String shown=page.value;
   while(shown.length()&&d.textWidth(shown+"_")>width-32)shown.remove(0,1);
   d.setTextColor(Ink,AppColors::Surface);d.setCursor(14,fieldY+3);d.print(shown+"_");
  }
 }else{
  const int top=page.selected>=0?std::max(0,std::min(page.selected-1,int(page.lines.size())-visible)):0;
  String selectedValue=page.selected>=0&&page.selected<int(page.lines.size())?page.lines[page.selected]:String("");
  if(cache.moving!=selectedValue){cache.moving=selectedValue;cache.since=now;}
  for(int row=0;row<visible;row++){
   const int index=top+row;const bool selected=index==page.selected;
   const String value=index<int(page.lines.size())?page.lines[index]:String("");
   const String field=!small&&index<int(page.values.size())?page.values[index]:String("");
   const int textWidth=field.length()?width-124:width-28;
   const int offset=selected?localTextOffset(d.textWidth(value)-textWidth,now-cache.since):0;
   const bool primary=!small&&index<page.primaryRows;
   if(!full&&cache.row[row]==value&&cache.values[row]==field&&cache.primary[row]==primary&&cache.selected[row]==selected&&cache.offset[row]==offset)continue;
   changed=true;cache.row[row]=value;cache.values[row]=field;cache.primary[row]=primary;cache.selected[row]=selected;cache.offset[row]=offset;
   const auto bg=selected?AppColors::Focus:Background;
   const int y=first+row*step;
   d.fillRect(8,y,width-16,rowHeight,Background);
   if(selected){
    appSurface(d,{8,y,width-16,rowHeight},White);
    appSurface(d,{9,y+1,width-18,rowHeight-2},bg);
   }
   d.setTextColor(selected?Ink:(!small&&page.selected<0&&page.primaryRows>0&&!primary?Muted:Ink),bg);
   d.setClipRect(14,first+row*step+2,textWidth,25);
   d.setCursor(14-offset,first+row*step+2);d.print(value);d.clearClipRect();
   if(field.length()){d.setTextColor(selected?Ink:page.accent,bg);d.setClipRect(width-104,first+row*step+2,90,25);d.setCursor(width-14-d.textWidth(field),first+row*step+2);d.print(field);d.clearClipRect();}
  }
  const int position=page.selected>=0?page.selected:page.position,total=page.selected>=0?int(page.lines.size()):page.total;
  const String index=String(position+1)+"/"+String(std::max(1,total));
  if((small||!page.confirmation)&&(full||cache.index!=index)){changed=true;cache.index=index;d.fillRect(78,nav,width-86,19,Background);d.setTextSize(.65f);d.setTextColor(Muted,Background);d.setCursor(82,nav);d.print(index);}
 }
 if(!small){
  if(cache.help!=page.help){cache.help=page.help;cache.helpSince=now;}
  d.setTextSize(.65f);const int shift=localTextOffset(d.textWidth(page.help)-(width-16),now-cache.helpSince);
  if(full||cache.helpOffset!=shift||cache.paintedHelp!=page.help){
   changed=true;cache.helpOffset=shift;cache.paintedHelp=page.help;
   d.fillRect(8,194,width-16,18,Background);d.setTextColor(Muted,Background);d.setClipRect(8,194,width-16,18);d.setCursor(8-shift,194);d.print(page.help);d.clearClipRect();
  }
 }
 cache.painted=true;return changed;
}
