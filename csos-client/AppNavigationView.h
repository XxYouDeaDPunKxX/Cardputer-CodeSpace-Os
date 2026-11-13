#pragma once
#include "PhysicalKeys.h"
#include <vector>
#include "AppNavigationLabels.h"
#ifdef ARDUINO
#include <lgfx/Fonts/Font32rle.h>
#else
#ifndef PROGMEM
#define PROGMEM
#define CSOS_UNDEFINE_PROGMEM
#endif
#include "../vendor/fonts/Font32rle.h"
#ifdef CSOS_UNDEFINE_PROGMEM
#undef PROGMEM
#undef CSOS_UNDEFINE_PROGMEM
#endif
#endif
// Shared presentation and geometry for the live controller and host preview.
struct AppRect { int x,y,w,h; bool contains(int px,int py)const{return px>=x&&py>=y&&px<x+w&&py<y+h;} };
struct AppViewCache { bool painted=false; String keys[12]; void invalidate(){painted=false;} };
template<class D> bool appRegion(D& d,AppViewCache& c,int slot,AppRect r,const String& key){
 if(c.painted&&c.keys[slot]==key)return false;
 c.keys[slot]=key;d.fillRect(r.x,r.y,r.w,r.h,UiPalette::Background);return true;
}
template<class D> void appText(D& d,int x,int y,const String& text,float size=.8f,uint16_t color=UiPalette::Ink,uint16_t bg=UiPalette::Background){
 d.setFont(&fonts::Font4);d.setTextWrap(false);d.setTextSize(size,size);d.setTextColor(color,bg);d.setCursor(x,y);d.print(text);
}
template<class D> void appOutline(D& d,AppRect r,uint16_t color){
 d.drawFastHLine(r.x,r.y,r.w,color);d.drawFastHLine(r.x,r.y+r.h-1,r.w,color);
 d.drawFastVLine(r.x,r.y,r.h,color);d.drawFastVLine(r.x+r.w-1,r.y,r.h,color);
}
namespace AppColors {
constexpr uint16_t Surface=UiPalette::from332(0x29), Focus=UiPalette::from332(0x4a);
constexpr uint16_t Action=UiPalette::from332(0x7e), Yellow=UiPalette::from332(0xfc);
}
template<class D> void appSurface(D& d,AppRect r,uint16_t color){
 d.fillRect(r.x+2,r.y,r.w-4,1,color);
 d.fillRect(r.x+1,r.y+1,r.w-2,1,color);
 d.fillRect(r.x,r.y+2,r.w,r.h-4,color);
 d.fillRect(r.x+1,r.y+r.h-2,r.w-2,1,color);
 d.fillRect(r.x+2,r.y+r.h-1,r.w-4,1,color);
}
template<class D> void appChoice(D& d,AppRect r,const String& label,bool selected,bool primary=false,uint16_t accent=AppColors::Action){
 const auto bg=primary?accent:selected?AppColors::Focus:AppColors::Surface;
 if(selected)appSurface(d,r,UiPalette::White);
 const int inset=selected?2:0;
 appSurface(d,{r.x+inset,r.y+inset,r.w-inset*2,r.h-inset*2},bg);
 float size=r.w<170?.72f:.80f;
 d.setFont(&fonts::Font4);d.setTextSize(size,size);
 if(d.textWidth(label)>r.w-12){size=.65f;d.setTextSize(size,size);}
 appText(d,r.x+(r.w-d.textWidth(label))/2,r.y+(r.h-int(26*size))/2,label,size,
         primary?UiPalette::KeyInk:UiPalette::Ink,bg);
}
inline AppRect appHomePrimary(){return {8,174,179,40};}
inline AppRect appHomeManage(){return {197,174,115,40};}
inline AppRect appToolRow(int row){return {8+155*(row%2),47+66*(row/2),149,55};}
enum class AppArrival:uint8_t {Ready,Offline,Stopped,Starting,AccessError,Checking,Waiting,Connecting,Cancelling,WifiChanging,WifiRestoring,Request,RateWait};
struct AppHomeView {AppArrival phase=AppArrival::Ready;bool manageSelected=false,fault=false,recoverable=false,wifiConnected=false;uint32_t waitSeconds=0;String network,machine,desktop,notice;};
template<class D> void drawAppHome(D& d,const AppHomeView& s,AppViewCache& c){
 using namespace UiPalette;
 if(!c.painted)d.fillScreen(Background);
 if(appRegion(d,c,0,{0,0,320,66},"home")){
  appText(d,12,8,"CS OS",.67f,AppColors::Action);
  appText(d,12,30,"Accesso al desktop",1.02f);
 }
 const int phase=int(s.phase);
 const bool error=s.phase==AppArrival::AccessError;
 const String network=s.wifiConnected?(s.network.length()?s.network:String("Connessa")):
                      s.phase==AppArrival::WifiChanging?String("Cambio rete in corso"):
                      s.phase==AppArrival::WifiRestoring?String("Ripristino in corso"):String("Scollegata");
 const String values[]={network,s.machine.length()?s.machine:String("Da verificare"),
                        s.desktop.length()?s.desktop:String("Da verificare")};
 const char* labels[]={"Wi-Fi","Codespace","Desktop"};
 for(int row=0;row<3;++row){
  const int y=68+row*26;
  if(!appRegion(d,c,4+row,{0,y,320,25},values[row]+":"+String(phase)))continue;
  appText(d,12,y+2,labels[row],.70f,Muted);
  const uint16_t color=row==0&&!s.wifiConnected?AppColors::Yellow:row==2&&error?AppColors::Yellow:Ink;
  d.setClipRect(116,y,193,23);
  appText(d,116,y+2,values[row],.70f,color);
  d.clearClipRect();
 }
 const String notice=s.fault?String("Evento registrato in Diagnostica"):s.notice;
 if(appRegion(d,c,1,{0,146,320,22},notice)){
  d.setClipRect(12,146,296,20);
  appText(d,12,147,notice,.63f,s.fault?AppColors::Yellow:Muted);
  d.clearClipRect();
 }
 if(appRegion(d,c,2,{0,168,320,72},String(phase)+":"+String(s.manageSelected)+":"+String(s.waitSeconds)+String(s.recoverable))){
  const char* actions[]={"Apri desktop","Scegli rete","Avvia Codespace","Avvio in corso","Verifica accessi","Verifica stato","Verifica desktop","Annulla collegamento","Chiusura in corso","Annulla cambio rete","Ripristino in corso","Verifica stato","Verifica rinviata"};
  const bool blocked=s.phase==AppArrival::Starting||s.phase==AppArrival::Cancelling||s.phase==AppArrival::WifiRestoring||
                     s.phase==AppArrival::RateWait||(s.phase==AppArrival::Request&&!s.recoverable);
  if(s.phase==AppArrival::RateWait)appText(d,12,183,String("Attendi ")+String(s.waitSeconds)+" s",.85f,AppColors::Yellow);
  else if(blocked)appText(d,12,183,s.phase==AppArrival::Request?"Verifica esito":s.phase==AppArrival::Cancelling?"Annullamento in corso":actions[phase],.76f,Muted);
  else appChoice(d,appHomePrimary(),actions[phase],!s.manageSelected,true,error?AppColors::Yellow:AppColors::Action);
  const bool cancellable=s.phase==AppArrival::Connecting||s.phase==AppArrival::WifiChanging;
  const bool manage=s.phase!=AppArrival::WifiRestoring&&s.phase!=AppArrival::Cancelling&&!cancellable;
  if(manage){
   appChoice(d,appHomeManage(),"",s.manageSelected);
   const auto manageBg=s.manageSelected?AppColors::Focus:AppColors::Surface;
   paintPhysicalKey(d,UI_OPT,202,182,30,22);
   appText(d,237,184,"Gestisci",.65f,Ink,manageBg);
   if(!blocked){
    paintPhysicalKey(d,UI_UP,8,218,19,18);paintPhysicalKey(d,UI_DOWN,29,218,19,18);
    appText(d,54,219,"Scegli",.62f,Muted);
    paintPhysicalKey(d,UI_ENTER,113,216,25,23);appText(d,143,219,"Apri",.62f,Muted);
   }
   appText(d,204,218,"R",.72f,Ink);appText(d,223,219,"Verifica",.62f,Muted);
  }else if(cancellable){
   paintPhysicalKey(d,UI_ESC,9,216,32,22);appText(d,48,219,"Annulla",.66f,Muted);
  }
 }
 c.painted=true;
}
enum class AppPanel:uint8_t {Tools,Networks,Light,AccessError};
struct AppPanelView {AppPanel page=AppPanel::Tools;int selected=0;int value=50;bool external=true,failed=false;};
template<class D> void drawAppPanel(D& d,const AppPanelView& s,AppViewCache& c){
 using namespace UiPalette;const String identity=String(int(s.page))+String(s.external)+String(s.failed);
 if(!c.painted)d.fillScreen(Background);
 if(appRegion(d,c,0,{0,0,320,33},identity)){
  paintPhysicalKey(d,UI_ESC,277,4,35,23);
  const char* names[]={"Impostazioni","Reti Wi-Fi","Luce esterna","Accesso desktop"};
  appText(d,8,4,s.page==AppPanel::Light&&!s.external?"Luce interna":names[int(s.page)],.9f);
 }
 if(appRegion(d,c,1,{0,33,320,174},identity+":"+String(s.selected))){
  c.keys[3]="";
  if(s.page==AppPanel::Tools){
   for(int i=0;i<4;i++)appChoice(d,appToolRow(i),appToolLabel(i),s.selected==i);
   if(s.selected>=0&&s.selected<4)appText(d,10,181,appToolHint(s.selected),.71f,Muted);
  }else if(s.page==AppPanel::Networks){
   appText(d,9,39,"Scegli dove collegarti",.85f);
   const char* names[]={"Casa","Laboratorio","Altra rete..."};
   for(int i=0;i<3;i++){
    const AppRect r={8,73+i*37,304,34};appChoice(d,r,names[i],s.selected==i);
    appText(d,228,r.y+8,i<2?"salvata":"",.65f,Green);
   }
   appText(d,10,187,"Cerca reti   /   Gestisci salvate",.7f,Muted);
  }else if(s.page==AppPanel::Light){
   appText(d,10,43,s.external?"Display esterno":"Display Cardputer",.85f);
   paintPhysicalKey(d,UI_LEFT,8,165,25,24);paintPhysicalKey(d,UI_RIGHT,37,165,25,24);
   appText(d,74,169,"Regola a passi del 10%",.75f);
  }else{
   appText(d,9,42,"Accesso non accettato",1,Warning);
   appText(d,9,79,"Risposta WSS: HTTP 401.",.75f);
   appText(d,9,104,"Verifica l'accesso remoto.",.75f);
   appChoice(d,{8,147,304,38},"Modifica accesso",true,true,Warning);
  }
 }
 if(s.page==AppPanel::Light&&appRegion(d,c,3,{8,73,304,77},identity+":"+String(s.value))){
  appText(d,10,75,String(s.value)+"%",1.7f,s.failed?Warning:Green);
  for(int i=0;i<10;i++)d.fillRect(10+i*30,131,26,15,i<s.value/10?Green:Rule);
 }
 if(appRegion(d,c,2,{0,207,320,33},identity)){
  if(s.page!=AppPanel::Light){paintPhysicalKey(d,UI_UP,8,214,23,20);paintPhysicalKey(d,UI_DOWN,34,214,23,20);}
  appText(d,s.page==AppPanel::Light?10:66,216,s.page==AppPanel::Light?(s.failed?"Salvataggio non riuscito":"Salvataggio automatico"):"Scegli",.65f,s.failed?Warning:Muted);
  if(s.page!=AppPanel::Light){paintPhysicalKey(d,UI_ENTER,222,211,28,24);appText(d,259,216,"Apri",.65f);}
 }
 c.painted=true;
}
enum class AppHudPage:uint8_t {Write,Control,Move,Drag,Tools,Guide,Status,Recovery};
struct AppHudView {AppHudPage page=AppHudPage::Control;int zoom=150,selected=0;uint8_t pending=0,held=0;bool precise=false,aa=false,fn=false;};
template<class D> void appRail(D& d,AppHudPage page,const char* customLabel=nullptr){
 using namespace UiPalette;
 const char* names[]={"SCRIVI","CONTROLLA","FINESTRA","DRAG","STRUMENTI","GUIDA","STATO","RECUPERO"};
 const uint16_t colors[]={from332(0x2a),from332(0x0d),from332(0x69),from332(0x69),AppColors::Focus,Rule,Rule,from332(0x89)};
 const int index=int(page);
 appSurface(d,{1,2,25,HudContentHeight-4},colors[index]);
 // Upright letters stacked top-to-bottom, never a rotated word.
 const char* label=customLabel?customLabel:names[index];const int count=strlen(label);
 const int height=count>=8?10:count>6?12:14,step=height+2;
 const int top=2+(HudContentHeight-4-count*step)/2;
 for(int i=0;i<count;++i){
  const unsigned char glyph=label[i];const int sourceWidth=widtbl_f32[glyph-32];
  const int width=std::min(14,std::max(1,sourceWidth*height/26));
  bool pixels[14*14]={};const auto* runs=chrtbl_f32[glyph-32];int source=0;
  // Pool the original glyph strokes so small upright letters keep their stems.
  while(source<sourceWidth*26){const uint8_t run=*runs++;const int end=std::min(sourceWidth*26,source+(run&127)+1);
   if(run&128)for(int at=source;at<end;++at)pixels[(at/sourceWidth*height/26)*width+(at%sourceWidth*width/sourceWidth)]=true;
   source=end;
  }
  const int x=1+(25-width)/2;
  for(int y=0;y<height;++y)for(int col=0;col<width;++col)if(pixels[y*width+col])d.fillRect(x+col,top+i*step+y,1,1,Ink);
 }

}
enum class HudPageKeys:uint8_t {List,Read,Confirm,Edit,Adjust};
template<class D> void appHudFrame(D& d,const char* rail){
 d.fillRect(0,0,240,UiPalette::HudContentHeight,UiPalette::Background);
 appRail(d,AppHudPage::Tools,rail);
}
template<class D> void appHudFooter(D& d,HudPageKeys mode,int position=0,int total=1,bool secret=false){
 using namespace UiPalette;
 d.fillRect(29,94,207,19,Background);
 if(mode==HudPageKeys::Edit){
  if(secret){paintPhysicalKey(d,UI_TAB,30,95,27,17);appText(d,59,96,"mostra",.55f);}

 }else if(mode==HudPageKeys::Confirm){
  appText(d,31,96,"Invio conferma",.58f,AppColors::Action);
 }else{
  paintPhysicalKey(d,mode==HudPageKeys::Adjust?UI_LEFT:UI_UP,31,96,17,15);
  paintPhysicalKey(d,mode==HudPageKeys::Adjust?UI_RIGHT:UI_DOWN,51,96,17,15);
  appText(d,76,96,mode==HudPageKeys::Adjust?String("10%"):String(position+1)+"/"+String(total),.55f,Muted);
 }
 if(mode==HudPageKeys::List||mode==HudPageKeys::Edit){paintPhysicalKey(d,UI_ENTER,137,94,23,18);appText(d,163,96,mode==HudPageKeys::Edit?"ok":"apri",.60f);}
 appText(d,210,96,mode==HudPageKeys::Edit?"Opt":"esc",.58f,Orange);
}
template<class D> void appHudRow(D& d,int row,const String& text,float size=.80f,uint16_t color=UiPalette::Ink,int offset=0){
 const int y=2+row*23;
 d.fillRect(37,y,197,21,UiPalette::Background);d.setClipRect(38,y,194,21);
 appText(d,38-offset,y,text,size,color);d.clearClipRect();
 d.drawFastHLine(37,y+21,197,UiPalette::Rule);
}
template<class D> void appHudDocument(D& d,const char* rail,const String& title,const std::vector<String>& lines,int position,int total,HudPageKeys keys=HudPageKeys::Read){
 appHudFrame(d,rail);appHudRow(d,0,title,.80f,AppColors::Action);
 for(int row=0;row<3&&row<int(lines.size());++row)appHudRow(d,row+1,lines[row]);
 appHudFooter(d,keys,position,total);
}
template<class D> void drawAppLight(D& d,bool small,bool external,int value,bool failed,AppViewCache& cache){
 using namespace UiPalette;
 if(!small){AppPanelView panel;panel.page=AppPanel::Light;panel.value=value;panel.external=external;panel.failed=failed;drawAppPanel(d,panel,cache);return;}
 const String key=String(external)+":"+String(value)+":"+String(failed);
 if(appRegion(d,cache,0,{0,0,240,HudContentHeight},key)){
  appHudFrame(d,"SCHERMI");appHudRow(d,0,external?"Luce esterna":"Luce interna",.80f,AppColors::Action);
  appHudRow(d,1,String(value)+"%",.80f,failed?Warning:Ink);
  for(int i=0;i<10;++i)d.fillRect(38+i*19,52,16,17,i<value/10?Green:Rule);
  appHudRow(d,3,failed?"Non salvato":"Salvataggio automatico",.65f,failed?Warning:Muted);
  appHudFooter(d,HudPageKeys::Adjust);
 }cache.painted=true;
}

template<class D> void appHudList(D& d,const std::vector<String>& items,int selected,const char* rail="STRUMENTI",uint32_t elapsed=0){
 using namespace UiPalette;
 // All small menu levels share the root menu geometry.
 appRail(d,AppHudPage::Tools,rail);
 const int top=std::max(0,std::min(selected-1,int(items.size())-4));
 for(int row=0;row<4;++row){
  const int index=top+row,y=2+row*23;if(index>=int(items.size()))break;
  const bool active=index==selected;d.setTextSize(.80f);
  int excess=d.textWidth(items[index])-194,offset=0;
  if(active&&excess>0){const auto phase=elapsed%(4200+(uint32_t(excess)*1000+17)/18);if(phase>1800)offset=std::min(excess,int(uint64_t(phase-1800)*18/1000));}
  if(active)d.fillRect(29,y,3,20,AppColors::Action);
  d.setClipRect(38,y,194,21);appText(d,38-offset,y,items[index],.80f,active?AppColors::Action:Ink);d.clearClipRect();
  d.drawFastHLine(37,y+21,197,Rule);
 }
 appHudFooter(d,HudPageKeys::List,selected,items.size());
}

template<class D> void appPanKeys(D& d,int x,int y){
 const char* keys[]={"E","A","S","D"};
 const auto color=AppColors::Action;
 for(int i=0;i<4;++i){
  const int keyX=x+i*26,arrowX=keyX+13,arrowY=y+6;
  appText(d,keyX,y,keys[i],.75f);
  if(i==0)d.fillTriangle(arrowX+3,arrowY,arrowX,arrowY+5,arrowX+6,arrowY+5,color);
  if(i==1)d.fillTriangle(arrowX,arrowY+3,arrowX+5,arrowY,arrowX+5,arrowY+6,color);
  if(i==2)d.fillTriangle(arrowX,arrowY,arrowX+6,arrowY,arrowX+3,arrowY+5,color);
  if(i==3)d.fillTriangle(arrowX+5,arrowY+3,arrowX,arrowY,arrowX,arrowY+6,color);
 }
}
struct AppShortcut {const char* action;const char* keys;};
template<class D> void drawWriteShortcuts(D& d,uint8_t modifiers,bool fn){
 using namespace UiPalette;
 static const AppShortcut ctrl[]={{"Copia","Ctrl+C"},{"Incolla","Ctrl+V"},{"Taglia","Ctrl+X"},{"Tutto","Ctrl+A"},{"Annulla","Ctrl+Z"},{"Salva","Ctrl+S"}};
 static const AppShortcut alt[]={{"Finestre","Alt+Tab"},{"Menu finestra","Alt+Space"},{"Chiudi","Alt+F4"},{"Sposta","Alt+F7"}};
 static const AppShortcut altFn[]={{"Chiudi","Alt+F4"},{"Sposta","Alt+F7"},{"Dimensioni","Alt+F8"},{"Nascondi","Alt+F9"},{"Massimizza","Alt+F10"},{"Intero","Alt+F11"}};
 static const AppShortcut super[]={{"File","Super+E"},{"Esegui","Super+R"},{"Schermi","Super+P"},{"Stessa app","Super+Tab"}};
 static const AppShortcut ctrlShift[]={{"Copia term.","Ctrl+Shift+C"},{"Incolla term.","Ctrl+Shift+V"},{"Ripeti","Ctrl+Shift+Z"},{"Riapri tab","Ctrl+Shift+T"}};
 static const AppShortcut ctrlAlt[]={{"Terminale","Ctrl+Alt+T"},{"Desktop","Ctrl+Alt+D"},{"Area sx","Ctrl+Alt+Left"},{"Area dx","Ctrl+Alt+Right"}};
 static const AppShortcut altShift[]={{"Finestra prec.","Alt+Shift+Tab"},{"Abbassa","Alt+Shift+PgDn"},{"Solleva","Alt+Shift+PgUp"}};
 const AppShortcut* items=nullptr;int count=0;
 switch(modifiers){
  case 1:items=ctrl;count=6;break;
  case 2:items=fn?altFn:alt;count=fn?6:4;break;
  case 8:items=super;count=4;break;
  case 5:items=ctrlShift;count=4;break;
  case 3:items=ctrlAlt;count=4;break;
  case 6:items=fn?altShift+1:altShift;count=fn?2:3;break;
  default:break;
 }
 if((modifiers&8)&&fn){
  appText(d,31,41,"Super + Fn",.72f);
  appText(d,31,65,"Spegni Fn per lettere e Tab",.58f,AppColors::Yellow);
  paintPhysicalKey(d,UI_FN,31,87,25,20,true,true);
  appText(d,66,90,"Super conservato",.60f);
  return;
 }
 if(!items){
  String names;
  const char* labels[]={"Ctrl","Alt","Shift","Super"};
  for(int i=0;i<4;++i)if(modifiers&(1<<i)){if(names.length())names+=" + ";names+=labels[i];}
  appText(d,31,43,names,.60f);
  appText(d,31,78,"Combinazione libera",.66f,Muted);
  return;
 }
 const int columns=count>4?3:2,step=columns==3?69:106;
 // These are examples of OS/app bindings, not actions implemented by CS OS.
 appText(d,31,35,(modifiers==1||modifiers==5)?"App attiva: esempi":"XFCE: secondo configurazione",.55f,Muted);
 d.drawFastHLine(29,82,205,Rule);
 for(int i=1;i<columns;++i)d.drawFastVLine(25+i*step,51,62,Rule);
 for(int i=0;i<count;++i){
  const int x=31+i%columns*step,y=i<columns?51:83;
  d.setFont(&fonts::Font4);d.setTextSize(.58f);
  const float titleSize=d.textWidth(items[i].action)>step-6?.50f:.58f;
  appText(d,x,y,items[i].action,titleSize);
  appText(d,x,y+16,items[i].keys,.55f,modifiers&4?Blue:AppColors::Action);
 }
}
template<class D> void drawBlueKeyMap(D& d){
 using namespace UiPalette;
 const char* blue[]={"!","@","#","$","%","^","&","*","(",")","_","+"};
 const char* base[]={"1","2","3","4","5","6","7","8","9","0","-","="};
 for(int i=0;i<12;++i){const int x=31+i*17;appText(d,x,36,blue[i],.65f,Blue);appText(d,x,53,base[i],.55f);}
 const char* symbols[]={"~","{","}","|",":","\"","<",">","?","A-Z"};
 const char* keys[]={"`","[","]","\\",";","'",",",".","/","a-z"};
 for(int i=0;i<10;++i){const int x=31+i*20;appText(d,x,72,symbols[i],i==9?.50f:.65f,Blue);appText(d,x,91,keys[i],.50f);}
}

template<class D> void drawAppHudBody(D& d,const AppHudView& s,AppViewCache& c){
 using namespace UiPalette;
 const String identity=String(int(s.page));
 if(appRegion(d,c,1,{0,0,240,HudContentHeight},identity+":"+String(s.selected)+":"+String(s.pending)+":"+String(s.held)+":"+String(s.precise)+String(s.aa)+String(s.fn))){
  c.keys[2]="";
  if(s.page!=AppHudPage::Tools)appRail(d,s.page);
  if(s.page==AppHudPage::Tools){
   appHudList(d,{"Schermi","Connessioni","Assistenza","Codespace"},s.selected);
  }else if(s.page==AppHudPage::Write){
   // Stable modifier strip, matching the printed Ctrl/Alt/Aa legends.
   for(int i=0;i<3;++i){
    const int bit=i==0?1:i==1?2:8,x=31+i*52;
    const bool held=(s.held&bit)!=0,ready=(s.pending&bit)!=0;
    const auto color=held?Ink:ready?AppColors::Action:Muted;
    appText(d,x,1,i==0?"ctrl":i==1?"alt":"Super",.67f,color);
    appText(d,x,19,held?"tenuto":ready?"pronto":"libero",.60f,color);
   }
   paintPhysicalKey(d,UI_AA,194,0,29,18,s.aa||(s.held&4),true);
   appText(d,194,19,"Shift",.55f,(s.aa||(s.held&4))?Blue:Muted);
   if(s.held&4)d.fillRect(229,5,3,9,Ink);
   else if(s.aa)d.fillRect(229,5,3,3,Blue);
   d.drawFastHLine(29,33,205,Rule);
   const uint8_t modifiers=s.pending|s.held|(s.aa?4:0);
   if(modifiers&11){
    drawWriteShortcuts(d,modifiers,s.fn);
   }else if(s.fn){
    // Center each physical key below its Linux function label.
    const char* functionKeys[]={"1","2","3","4","5","6","7","8","9","0","-","="};
    int x=30;
    for(int i=0;i<12;++i){
     const String label="F"+String(i+1);
     d.setFont(&fonts::Font4);d.setTextSize(.50f);
     const int width=d.textWidth(label);
     appText(d,x,35,label,.50f);
     d.setTextSize(.60f);
     const int keyWidth=d.textWidth(functionKeys[i]);
     appText(d,x+(width-keyWidth)/2,49,functionKeys[i],.60f,Orange);
     d.drawFastVLine(x+width,35,28,Rule);
     x+=width+1;
    }
    appText(d,31,66,"Pagina su",.55f);appText(d,100,66,"[",.60f,Orange);
    appText(d,139,66,"Pagina giu",.55f);appText(d,222,66,"]",.60f,Orange);
    const char* keys[]={"H","E","I","P","M"};
    const char* functions[]={"Home","End","Insert","Stampa","Menu"};
    for(int i=0;i<5;++i){
     const int column=i==4?202:31+i*40;
     appText(d,column,82,functions[i],.55f);
     appText(d,column,96,keys[i],.50f,Orange);
    }
   }else if(modifiers&4){
    drawBlueKeyMap(d);
   }else{
    // These functions belong to the typing asset; Opt stays outside this panel.
    d.drawFastHLine(29,74,205,Rule);d.drawFastVLine(132,36,36,Rule);
    appText(d,31,38,"Funzioni",.65f);
    paintPhysicalKey(d,UI_FN,31,54,23,19,false,true);appText(d,61,57,"F1-F12",.58f);
    appText(d,141,38,"Super",.65f);
    appText(d,141,57,"fn + tab",.58f,Orange);
    appText(d,31,78,"Pennino",.62f);
    appText(d,31,96,"tap: sinistro",.58f);
    appText(d,139,96,"0.6s: destro",.58f);
   }
  }else if(s.page==AppHudPage::Move||s.page==AppHudPage::Drag){
   const bool move=s.page==AppHudPage::Move;
   appText(d,31,8,move?"Sposta finestra":"Trascinamento",.88f);
   {
    paintPhysicalKey(d,UI_LEFT,31,42,20,19);paintPhysicalKey(d,UI_UP,52,42,20,19);
    paintPhysicalKey(d,UI_DOWN,73,42,20,19);paintPhysicalKey(d,UI_RIGHT,94,42,20,19);
   }
   paintPhysicalKey(d,move?UI_ENTER:UI_SPACE,30,83,26,23);
   appText(d,59,85,move?"fissa":"rilascia",.71f);
   paintPhysicalKey(d,UI_ESC,132,83,32,23);appText(d,170,85,"annulla",.68f);
  }else{
   // Fixed command groups; bright rules survive the 8-bit HUD sprite.
   d.drawFastVLine(134,2,82,White);
   d.drawFastHLine(29,42,205,White);d.drawFastHLine(29,86,205,White);
   appText(d,31,1,"Vista",.70f,AppColors::Action);
   appPanKeys(d,31,20);
   const int bits[]={1,2,8};const char* labels[]={"Ctrl","Alt","Super"};
   if(!((s.pending|s.held)&11))appText(d,140,1,"Puntatore",.70f,AppColors::Yellow);
   else for(int i=0;i<3;++i)if((s.pending|s.held)&bits[i])
    appText(d,140+i*30,2,labels[i],.60f,s.held&bits[i]?Ink:AppColors::Action);
   paintPhysicalKey(d,UI_UP,139,20,20,19);paintPhysicalKey(d,UI_LEFT,160,20,20,19);
   paintPhysicalKey(d,UI_DOWN,181,20,20,19);paintPhysicalKey(d,UI_RIGHT,202,20,20,19);
   appText(d,31,45,"Zoom",.70f,AppColors::Action);
   if(s.precise)appSurface(d,{78,44,54,18},AaFace);
   appText(d,80,45,"Aa fine",.60f,s.precise?Ink:Blue,s.precise?AaFace:Background);
   paintPhysicalKey(d,11,31,64,21,19);paintPhysicalKey(d,12,54,64,21,19);
   appText(d,140,45,"Rotella",.70f,Blue);
   paintPhysicalKey(d,25,139,64,23,19);paintPhysicalKey(d,26,164,64,23,19);
   paintPhysicalKey(d,UI_TAB,190,64,37,19);
   paintPhysicalKey(d,UI_ENTER,29,90,24,21);appText(d,55,92,"clic",.68f);
   paintPhysicalKey(d,UI_SPACE,88,90,22,21);appText(d,113,92,"drag",.68f);
   appText(d,154,92,"F finestra",.67f);

  }
 }
 if(s.page==AppHudPage::Control&&appRegion(d,c,2,{79,62,54,22},identity+":"+String(s.zoom)))
  appText(d,80,64,s.zoom?String(s.zoom)+"%":String("--"),.70f);
 c.painted=true;
}
