#pragma once
#include "PhysicalKeys.h"
#include "SessionDiagnostic.h"
#include "FaultDiagnostics.h"
#include "AppNavigationView.h"
#include "GuideView.h"
extern DiagnosticRecord lastSessionFault;
extern bool sessionFaultNotice;
// The host adapter compiles this exact renderer with the production Font4.
constexpr uint16_t HUD_BASE=UiPalette::Background,HUD_FG=UiPalette::Ink;
uint16_t HUD_BG=HUD_BASE;
bool hudAnimating=false;
struct HudTextMotion {String text;uint32_t since=0;};
HudTextMotion hudMotion[14];
void hudText(int slot,int x,int y,int width,const String& text,float size=.75f,uint16_t color=HUD_FG,uint16_t bg=HUD_BASE){
 hud.setFont(&fonts::Font4);hud.setTextSize(size,size);hud.setTextColor(color,bg);
 auto& motion=hudMotion[slot];if(motion.text!=text){motion.text=text;motion.since=millis();}
 int offset=0,overflow=hud.textWidth(text)-width;
 if(overflow>0){hudAnimating=true;const uint32_t travel=(uint32_t(overflow)*1000+17)/18;
  const uint32_t phase=(millis()-motion.since)%(4200+travel);
  if(phase>1800)offset=std::min(overflow,int(uint64_t(phase-1800)*18/1000));}
 hud.setClipRect(x,y,width,int(26*size));hud.setCursor(x-offset,y);hud.print(text);hud.clearClipRect();
}
std::vector<String> hudWrap(const String& text,int width,float size=.85f){
 hud.setFont(&fonts::Font4);hud.setTextSize(size,size);
 std::vector<String> lines;String line,word;
 auto append=[&](){
  if(!word.length())return;
  if(line.length()&&hud.textWidth(line+word)>width){lines.push_back(line);line="";}
  for(unsigned i=0;i<word.length();i++){
   String next=line;next+=word[i];
   if(line.length()&&hud.textWidth(next)>width){lines.push_back(line);line="";}
   line+=word[i];
  }word="";
 };
 for(unsigned i=0;i<text.length();i++){if(text[i]==' '){append();if(line.length())line+=' ';}else word+=text[i];}
 append();if(line.length())lines.push_back(line);if(lines.empty())lines.push_back("");return lines;
}
void hudStatus(bool editor=false){
 using namespace UiPalette;
 const auto bg=from332(0x25);
 hud.fillRect(0,HudContentHeight,240,135-HudContentHeight,bg);
 auto paintLatch=[&](int x,const char* label,bool active,uint16_t face,uint16_t color){
  if(active)appSurface(hud,{x-2,114,24,19},face);
  appText(hud,x,118,label,.66f,active?Ink:color,active?face:bg);
 };
 paintLatch(5,"fn",csosControls.fn,FnFace,Orange);
 paintLatch(28,"Aa",editor?csosControls.aa:csosControls.mode==InputMode::Keys?(csosControls.aa||(csosHeldMods&4)):csosControls.precise,AaFace,Blue);
 const int bars=!deviceStatus.wifi?0:deviceStatus.rssi>-60?3:deviceStatus.rssi>-75?2:1;
 for(int i=0;i<3;++i){
  const int height=6+4*i;
  hud.fillRect(53+i*6,130-height,3,height,i<bars?Green:White);
 }
 if(!deviceStatus.wifi)uiLine(hud,52,116,69,129,Warning);
 hud.fillTriangle(78,116,71,125,76,125,AppColors::Yellow);
 hud.fillTriangle(74,129,82,120,77,120,AppColors::Yellow);
 const auto chip=from332(0xb7);
 hud.fillRect(129,117,11,13,chip);hud.fillRect(131,119,7,9,bg);
 for(int i=0;i<3;++i){
  hud.fillRect(127,119+i*4,2,1,chip);hud.fillRect(140,119+i*4,2,1,chip);
 }
 auto percent=[&](int x,int value){
  const String digits=value<0?String("--"):String(value);
  appText(hud,x,115,digits,value==100?.67f:.78f,Ink,bg);
  appText(hud,x+hud.textWidth(digits)+1,120,"%",.46f,Ink,bg);
 };
 percent(85,deviceStatus.battery);
 percent(145,deviceStatus.cpu);
 const bool ready=inSession&&csosFrameSeen;
 const String value=DuplexAudio::micActive?String("MIC ON"):ready?String(int(fps+.5f))+" FPS":String("- FPS");
 const float size=DuplexAudio::micActive?.59f:value.length()>6?.59f:.72f;
 hud.setFont(&fonts::Font4);hud.setTextSize(size,size);
 appText(hud,239-hud.textWidth(value),117,value,size,ready?Ink:from332(0xf2),bg);
}
void hudPanel(const String& title){
 hudText(0,5,3,155,title,.75f);
 paintPhysicalKey(hud,UI_ENTER,166,1,29,24);paintPhysicalKey(hud,UI_ESC,201,1,35,24);
 hud.drawFastHLine(4,28,232,UiPalette::Rule);
}
void hudNavigation(int index,int count){
 paintPhysicalKey(hud,UI_UP,8,89,27,17);paintPhysicalKey(hud,UI_DOWN,40,89,27,17);
 hudText(1,82,89,47,String(index+1)+"/"+String(count),.65f);
}
void hudDocument(const String& title,const std::vector<String>& lines){
 const int count=std::max(1,int((lines.size()+2)/3));csosControls.detailCount=count;
 const int page=csosControls.detailPage%count;std::vector<String> visible;
 for(int r=0;r<3;++r){const int n=page*3+r;if(n<int(lines.size()))visible.push_back(lines[n]);}
 appHudDocument(hud,"STATO",title,visible,page,count);
}
String localPanelMessage(){
 String message;
   if(csosControls.unavailable)message=inSession?"Operazione non disponibile durante la sessione.":"Serve il desktop collegato. Nessun comando inviato.";
   else if(csosControls.page==6)message="CPU locale "+(deviceStatus.cpu<0?String("--"):String(deviceStatus.cpu))+"%. Heap "+String(int(deviceStatus.freeHeap/1024))+" KB. Wi-Fi "+String(deviceStatus.rssi)+" dBm. Desktop "+String(fps,1)+" FPS. Linux remoto: dati non disponibili.";
   else if(csosControls.page==7)message="CS OS / Cardputer ADV. TFT 320x240. HUD 240x135. Pennino: calibrazione a nove punti incorporata.";
   else if(inSession)message=csosFrameSeen?"Wi-Fi e CXL collegati. Desktop pronto.":"CXL collegato. Attendo il primo frame.";
   else if(csosSaveFailed)message="Salvataggio fallito. Controlla le impostazioni.";
   else if(csosCxlRejected)message="Codice CXL rifiutato. Correggi accesso remoto.";
   else if(lastTransportFailure==TransportFailure::Http401||lastTransportFailure==TransportFailure::Http403)message=String(lastTransportFailure==TransportFailure::Http401?"HTTP 401. ":"HTTP 403. ")+"Origine non verificata. Controlla accesso remoto. Riprova non avvia Codespace.";
   else message=(WiFi.status()==WL_CONNECTED?String("Wi-Fi connesso. "):String("Wi-Fi assente. "))+(connError.length()?connError:statusLine2)+". Stato Codespace sul TFT.";

 return message;
}
// Sections never share a page: live state, retained event, technical details.
struct DiagnosticSheet {String title;std::vector<String> lines;};
std::vector<DiagnosticSheet> diagnosticSheets(const std::vector<String>& current,int width,int rows,float size,bool logs=false){
 std::vector<DiagnosticSheet> pages;
 auto append=[&](const String& title,const std::vector<String>& facts){
  std::vector<String> lines;
  for(const auto& fact:facts){auto wrapped=hudWrap(fact,width,size);lines.insert(lines.end(),wrapped.begin(),wrapped.end());}
  for(size_t start=0;start<lines.size();start+=rows){
   DiagnosticSheet sheet;sheet.title=title;
   for(size_t n=start;n<std::min(lines.size(),start+rows);++n)sheet.lines.push_back(lines[n]);
   pages.push_back(sheet);
  }
 };
 if(!logs){
 append("COLLEGAMENTO",current);
 const auto pct=[](int value){return value<0?String("--"):String(value)+"%";};
 append("CARDPUTER",{"Batteria "+pct(deviceStatus.battery),"CPU locale "+pct(deviceStatus.cpu),
                     "Heap libero "+String(deviceStatus.freeHeap/1024)+" KiB",
                     deviceStatus.chipTemperatureValid?"Chip "+String(deviceStatus.chipTemperature)+" C":String("Temperatura chip --")});
 if(deviceStatus.heapLargest)
  append("MEMORIA",{"Minimo "+String(deviceStatus.heapMinimum/1024)+" KiB",
                    "Blocco maggiore "+String(deviceStatus.heapLargest/1024)+" KiB"});
 if(deviceStatus.wifi){
  std::vector<String> network={"Segnale "+String(deviceStatus.rssi)+" dBm"};
  if(deviceStatus.wifiChannel>0)network.push_back("Canale "+String(deviceStatus.wifiChannel));
  if(deviceStatus.wifiAddress){
   const uint32_t ip=deviceStatus.wifiAddress;
   network.push_back("IP "+String(ip&255)+"."+String((ip>>8)&255)+"."+String((ip>>16)&255)+"."+String(ip>>24));
  }
  append("RETE WI-FI",network);
 }
 if(inSession&&deviceStatus.streamSample)
  append("VIDEO",{String(fps,1)+" FPS",String(deviceStatus.receivedKiBs)+" KiB/s ricevuti"});
 if(deviceStatus.resetReason>=0)
  append("AVVIO",{"Acceso da "+String(deviceStatus.uptimeSeconds/60)+" min",
                  "Codice reset "+String(deviceStatus.resetReason)});
 return pages;
 }
 struct RetainedEvent{uint32_t at;String area,cause,code;};
 std::vector<RetainedEvent> events;
 if(lastSessionFault.code)events.push_back({lastSessionFault.atMs,"CXL",sessionExitText(SessionExit(lastSessionFault.code)),String(lastSessionFault.code)});
 for(size_t i=0;i<size_t(FaultArea::Count);++i){
  const auto& fault=areaFaults[i];if(!fault.event.code)continue;
  const auto code=FaultCode(fault.event.code);String cause=faultText(code);
  const int transport=code==FaultCode::CloudRequest&&fault.detail<0?-fault.detail:code==FaultCode::CloudAccess?fault.detail:0;
  if(transport>0&&transport<=int(TransportFailure::Owner))cause=transportFailureText(TransportFailure(transport));
  else if(code==FaultCode::CloudRequest&&fault.detail>=100&&fault.detail<=599)cause="Richiesta HTTP "+String(fault.detail);
  events.push_back({fault.event.atMs,faultAreaText(FaultArea(i)),cause,String(fault.detail)});
 }
 std::sort(events.begin(),events.end(),[](const RetainedEvent& a,const RetainedEvent& b){return a.at>b.at;});
 if(events.empty())append("ULTIMI EVENTI",{"Nessun guasto registrato"});
 for(const auto& event:events){
  const uint32_t sec=event.at/1000;
  const String time="+"+String(sec/60)+":"+(sec%60<10?"0":"")+String(sec%60);
  append("ULTIMI EVENTI",{time+"  "+event.area,event.cause+" ["+event.code+"]"});
 }
 return pages;
}
inline AppViewCache& operationHudCache(){static AppViewCache cache;return cache;}
inline bool& operationHudActive(){static bool active=false;return active;}
inline void invalidateOperationalHud(){operationHudCache().invalidate();operationHudActive()=false;}
void drawHud(){
 using namespace UiPalette;
 auto& operationCache=operationHudCache();auto& operational=operationHudActive();
 static AppViewCache lightCache;
 if(inSession&&csosControls.audio){
  invalidateOperationalHud();
  std::vector<String> items={String("Ascolto: ")+(DuplexAudio::listen?"ON":"OFF"),
   String("Microfono: ")+(DuplexAudio::micActive?(DuplexAudio::hostMic?"ON Linux":"ON attesa"):DuplexAudio::micWanted?"avvio":"OFF"),
   "Volume "+String(DuplexAudio::volume.load())+"/10",
   "Boost MIC +"+String(DuplexAudio::micDb.load())+" dB",
   "Boost OUT +"+String(DuplexAudio::outDb.load())+" dB"};
#if CSOS_USB_CONSOLE
  items.push_back(String("Salva prova PC: ")+(DuplexAudio::traceWanted?"ON":"OFF"));
#endif
  const int audioError=DuplexAudio::errorCode;
  if(audioError){const char* messages[]={"","Collega desktop","Errore audio","MIC: dati persi","Riattiva microfono","Traccia: poca RAM","MIC: errore Linux","MIC: coda assente","Audio: errore driver"};
   if(audioError<9)items[1]=messages[audioError];}
  if(!DuplexAudio::healthy)items[0]="Audio non pronto";
  else if(!DuplexAudio::negotiated)items[0]="Attendo audio Linux";
  auto& motion=hudMotion[2];const String identity="audio:"+String(csosControls.audioIndex);
  if(motion.text!=identity){motion.text=identity;motion.since=millis();}
  hud.setTextSize(.80f);hudAnimating=hud.textWidth(items[csosControls.audioIndex])>194;
  appHudList(hud,items,csosControls.audioIndex,"AUDIO",millis()-motion.since);
  if(csosControls.audioIndex>=2&&csosControls.audioIndex<=4){
   hud.fillRect(29,95,205,17,UiPalette::Background);
   appText(hud,38,96,", meno  / piu",.66f);
  }
  hudStatus();hud.pushSprite(0,0);return;
 }
 if(inSession&&csosControls.adjustingLight){
  if(operational)invalidateOperationalHud();
  drawAppLight(hud,true,csosControls.lightExternal,(csosControls.lightExternal?externalBrightness:brightness)*10,csosSaveFailed,lightCache);
  hudStatus();hud.pushSprite(0,0);return;
 }lightCache.invalidate();
 if(inSession&&(csosControls.menu||(!csosControls.local()&&!externalDark))){
  if(!operational){hud.fillScreen(Background);operationCache.invalidate();}
  AppHudView view;view.page=csosControls.menu?AppHudPage::Tools:csosControls.mode==InputMode::Keys?AppHudPage::Write:csosControls.movingWindow?AppHudPage::Move:csosDrag?AppHudPage::Drag:AppHudPage::Control;
  view.selected=csosControls.page;
  view.zoom=viewRatioX100?(10000+viewRatioX100/2)/viewRatioX100:0;
  view.pending=csosPendingMods;view.held=csosHeldMods|csosDragMods;view.precise=csosControls.precise;view.aa=csosControls.aa;view.fn=csosControls.fn;
  drawAppHudBody(hud,view,operationCache);hudAnimating=false;hudStatus();hud.pushSprite(0,0);operational=true;return;
 }
 operational=false;operationCache.invalidate();
 hud.fillScreen(Background);hud.setTextWrap(false);hudAnimating=false;
 if(inSession&&csosControls.local()){
  if(!csosControls.unavailable&&(csosControls.menu||csosControls.system||csosControls.superMenu)){
   const int choice=csosControls.menu?csosControls.page:csosControls.superMenu?csosControls.superIndex:csosControls.systemIndex;
   const int count=csosControls.menu?4:csosControls.superMenu?2:csosControls.itemCount();
   std::vector<String> items;
   for(int i=0;i<count;++i){
    String label=csosControls.menu?String(hudMainLabel(i)):csosControls.superMenu?String(i?"Prossimo tasto":"Super singolo"):String(hudCommandLabel(csosControls.item(i)));
    if(csosControls.system){const auto command=csosControls.item(i);
     if(command==LocalCommand::Brightness)label+=" "+String(brightness*10)+"%";
     if(command==LocalCommand::ExternalBrightness)label+=" "+String(externalBrightness*10)+"%";
     if(command==LocalCommand::ExternalLight)label=externalDark?"TFT: accendi":"TFT: spegni";
     if(command==LocalCommand::Fps)label+=" "+String(fpsCapNames[fpsCapIdx]);
     if(command==LocalCommand::Quality)label+=" "+String(qualityNames[quality]);
     if(command==LocalCommand::Cursor)label+=cursorShown?" si":" no";}
    items.push_back(label);
   }
   static const char* rails[]={"SCHERMI","ACCESSO","AIUTO","CODESPACE"};
   auto& motion=hudMotion[2];const String identity=String(csosControls.group)+":"+String(choice);
   if(motion.text!=identity){motion.text=identity;motion.since=millis();}
   hud.setTextSize(.80f);hudAnimating=hud.textWidth(items[choice])>194;
   appHudList(hud,items,choice,csosControls.superMenu?"SUPER":rails[csosControls.group],millis()-motion.since);
  }else if(csosControls.guide){
   const int topic=csosControls.guidePage;
   const auto pages=guidePages(hud,topic,true);
   csosControls.guideCount=pages.size();
   const int part=csosControls.guideDetail%csosControls.guideCount;
   drawGuidePage(hud,topic,part,pages[part],pages.size(),true);
  }else if(csosControls.confirm){
   const auto lines=hudWrap(csosControls.confirmed==LocalCommand::Stop?String("Arresta Codespace? I processi si chiudono."):String("Disconnetti? Codespace resta acceso."),194,.80f);
   appHudDocument(hud,"CONFERMA","Conferma",lines,0,1,HudPageKeys::Confirm);
  }else{
   if(!csosControls.unavailable&&(csosControls.page==5||csosControls.page==6||csosControls.page==8)){
    const auto pages=diagnosticSheets({WiFi.status()==WL_CONNECTED?String("Wi-Fi: connesso"):String("Wi-Fi: assente"),csosFrameSeen?String("CXL: desktop pronto"):String("CXL: attendo immagine")},194,3,.80f,csosControls.page==8);
    csosControls.detailCount=pages.size();const int page=csosControls.detailPage%pages.size();
    appHudDocument(hud,csosControls.page==8?"LOG":"STATO",pages[page].title,pages[page].lines,page,pages.size());
    if(csosControls.page==8){areaFaultNotice=false;sessionFaultNotice=false;}
   }else{
    const String message=localPanelMessage();
    hudDocument(csosControls.unavailable?String("INDISPONIBILE"):csosControls.page==6?String("CARDPUTER"):csosControls.page==7?String("INFO"):String(inSession?"DIAGNOSTICA":"OFFLINE"),hudWrap(message,194,.80f));
   }
  }
 }else if(!inSession){
  hudPanel(csosPreDesktopActive?String("CS OS / AVVIO"):String("OFFLINE"));
  hudText(2,8,37,224,"Controlli sul TFT",1);
  paintPhysicalKey(hud,UI_OPT,8,75,36,24);hudText(3,53,77,180,"opzioni sul TFT",.75f);
 }else{
  hudText(0,6,7,228,"Schermo esterno spento",.85f,Warning);
  paintPhysicalKey(hud,UI_OPT,6,48,36,24);hudText(2,53,51,180,"tieni: impostazioni",.75f);
  hudText(3,6,82,228,"Regola schermi > TFT",.75f);
 }

 hudStatus();hud.pushSprite(0,0);
}
