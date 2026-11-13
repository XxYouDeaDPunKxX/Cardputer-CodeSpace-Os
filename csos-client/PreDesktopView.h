#pragma once
#include "UiPalette.h"
#include <vector>
#include "PhysicalKeys.h"
#include "LocalPage.h"
#include "GuideView.h"
#include "AppNavigationView.h"
#include "TouchTap.h"
// All geometry and hit targets are shared with host previews.
struct PreDesktopScreen {
 String wifi="Non connesso",github="Da verificare",cxl="Da verificare",note="";
 bool enter=false,start=false,stop=false,busy=false,connecting=false,locked=false;
 bool wifiConnected=true,starting=false,cancelling=false,accessError=false,fault=false,wifiChanging=false,wifiRestoring=false;int selected=1;
 bool requestPending=false,recoverable=false;uint32_t retrySeconds=0;
};
struct PreDesktopPaintState {
 bool painted=false,connecting=false,locked=false;String prior[16];uint32_t since[16]={};int offset[16]={};uint16_t color[16]={};
 bool enabled[4]={},focused[4]={};
};
inline PreDesktopPaintState& preDesktopPaintState(){static PreDesktopPaintState state;return state;}
inline AppViewCache& appHomeCache(){static AppViewCache cache;return cache;}
inline int& preDesktopPrimaryAction(){static int action=1;return action;}
inline void invalidatePreDesktopView(){preDesktopPaintState().painted=false;appHomeCache().invalidate();++localTouchGeneration();}
// Action IDs stay stable. The renderer owns the hit rectangles.
inline int preDesktopHit(int x,int y){
 if(appHomePrimary().contains(x,y))return preDesktopPrimaryAction();
 if(appHomeManage().contains(x,y))return 3;
 return -1;
}
inline int preDesktopNavigate(int selected,uint8_t key){
 if(key==9||key==';'||key=='.'||key==','||key=='/')return selected==3?preDesktopPrimaryAction():3;
 return selected;
}

template<class Display> void drawPreDesktop(Display& panel,const PreDesktopScreen& s,uint32_t now){
 (void)now;
 AppHomeView view;view.manageSelected=s.selected==3;view.fault=s.fault;view.recoverable=s.recoverable;view.waitSeconds=s.retrySeconds;
 view.phase=s.wifiRestoring?AppArrival::WifiRestoring:s.wifiChanging?AppArrival::WifiChanging:s.cancelling?AppArrival::Cancelling:s.connecting?AppArrival::Connecting:
  !s.wifiConnected?AppArrival::Offline:s.enter?AppArrival::Ready:s.retrySeconds?AppArrival::RateWait:s.requestPending?AppArrival::Request:s.start?AppArrival::Stopped:
  s.starting?AppArrival::Starting:s.accessError?AppArrival::AccessError:s.stop?AppArrival::Waiting:AppArrival::Checking;
 view.wifiConnected=s.wifiConnected&&!s.wifiChanging&&!s.wifiRestoring;
 if(view.wifiConnected)view.network=s.wifi;
 view.machine=s.github;view.desktop=s.cxl;
 if(s.note=="Connesso, rete non salvata"||s.note=="Ingresso annullato")view.notice=s.note;
 drawAppHome(panel,view,appHomeCache());preDesktopPaintState().painted=true;
}
inline AppRect appConfirmCancel(){return {8,174,144,42};}
inline AppRect appConfirmCommit(){return {166,174,146,42};}
struct PreDesktopToolsScreen {
 int guideTopic=-1;
 String title="OPZIONI",help;std::vector<String> lines,values;int selected=-1,position=0,total=1;
 bool lightAdjust=false,lightExternal=false,saveFailed=false,stopConfirmation=false;int lightValue=50;
};
// Temporary panels use TFT while offline, never an overlay on streaming Linux.
template<class Display> void drawPreDesktopTools(Display& panel,const PreDesktopToolsScreen& screen,uint32_t now){
 static LocalPageCache cache;
 static AppViewCache lightCache;
 static AppViewCache toolsCache;
 if(screen.guideTopic>=0){
  cache.invalidate();lightCache.invalidate();
  if(!preDesktopPaintState().painted)toolsCache.invalidate();
  const String key="guide:"+String(screen.guideTopic)+":"+String(screen.position);
  if(appRegion(panel,toolsCache,0,{0,0,320,240},key))
   drawGuidePage(panel,screen.guideTopic,screen.position,{screen.title.c_str(),screen.lines},screen.total,false);
  toolsCache.painted=true;preDesktopPaintState().painted=true;return;
 }
 if(screen.title=="CONFERMA"){
  cache.invalidate();lightCache.invalidate();
  if(!preDesktopPaintState().painted)toolsCache.invalidate();
  const String key=screen.stopConfirmation?"stop":"disconnect";
  if(appRegion(panel,toolsCache,0,{0,0,320,240},key)){
   appText(panel,12,10,"CS OS",.72f,AppColors::Action);
   appText(panel,12,52,screen.stopConfirmation?"Arresta Codespace?":"Disconnetti desktop?",.98f);
   appText(panel,12,96,screen.stopConfirmation?"I processi in corso si chiudono.":"Il Codespace resta acceso.",.79f);
   if(screen.stopConfirmation)appText(panel,12,126,"I file salvati restano.",.75f,UiPalette::Muted);
   appChoice(panel,appConfirmCancel(),"Annulla",false);
   appChoice(panel,appConfirmCommit(),screen.stopConfirmation?"Arresta":"Disconnetti",true,true,AppColors::Yellow);
   appText(panel,12,221,"esc annulla",.60f,UiPalette::Muted);
   appText(panel,173,221,"Invio conferma",.60f,UiPalette::Muted);
  }
  toolsCache.painted=true;preDesktopPaintState().painted=true;return;
 }
 if(toolsCache.painted){cache.invalidate();}
 toolsCache.invalidate();
 if(screen.lightAdjust){cache.invalidate();if(!preDesktopPaintState().painted)lightCache.invalidate();drawAppLight(panel,false,screen.lightExternal,screen.lightValue,screen.saveFailed,lightCache);preDesktopPaintState().painted=true;return;}
 if(lightCache.painted)cache.invalidate();
 lightCache.invalidate();
 if(!preDesktopPaintState().painted)cache.invalidate();
 LocalPage page;page.title=screen.title;page.lines=screen.lines;page.values=screen.values;page.help=screen.help;page.selected=screen.selected;
 if(screen.title=="INDISPONIBILE")page.accent=UiPalette::Warning;
 page.position=screen.position;page.total=screen.total;
 drawLocalPage(panel,page,cache,now,320,240);preDesktopPaintState().painted=true;
}
