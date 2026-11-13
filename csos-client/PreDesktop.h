#pragma once
#include "DiagnosticSerial.h"
#include "PreDesktopModel.h"
#include "PreDesktopView.h"
#include "CloudControl.h"
#include "SessionDiagnostic.h"
#include "FaultDiagnostics.h"
#include "NvsDiagnostic.h"
#include "CloudRetry.h"
#include "ManagementConfig_private.h"
#include <strings.h>
namespace {
PreDesktopModel pre;
CloudRetry githubRetry;
String githubDetail,cxlDetail,preNote;
bool controlWorker=false,jobActive=false,firstNetwork=true,wifiAutoCancelled=false;
bool githubBlocked=false,cxlBlocked=false,pendingLifecycleCheck=false;
bool activeAutomatic=false,githubFaultActive=false,cxlFaultActive=false;
CloudJobKind activeKind=CloudJobKind::Status;
uint32_t githubCheckAt=0,cxlCheckAt=0,wifiCheckAt=0,preDrawAt=0;
int preSelection=1;bool preStopConfirmation=false;
bool entryPending=false,preDesktopEditing=false;
uint32_t entryGeneration=0,stopGeneration=0;
String observedWifi,stopNetwork;
struct ManagementSettings {uint32_t magic=0x43534D31;char token[256]={},name[81]={},seed[65]={};}management;
bool managementMatchesHost(const char* name){
 char host[128];
 snprintf(host,sizeof(host),"%s-8089.app.github.dev",name);
 return *name&&strcasecmp(host,CSOS_WSS_HOST)==0;
}
bool githubConfigured(){return management.token[0]&&managementMatchesHost(management.name);}
bool confirmedStopStillValid(){
 return stopGeneration==pre.generation&&WiFi.status()==WL_CONNECTED&&WiFi.SSID()==stopNetwork&&githubConfigured();
}
bool saveManagement(const ManagementSettings& draft){
 Preferences store;if(!store.begin("csoscloud",false)){
  recordFault(FaultArea::Nvs,FaultCode::NvsOpen,0,millis());return false;
 }
 const bool ok=store.putBytes("settings",&draft,sizeof(draft))==sizeof(draft);store.end();
 if(ok)management=draft;
 else recordFault(FaultArea::Nvs,FaultCode::NvsWrite,0,millis());
 return ok;
}
void loadManagement(){
 ManagementSettings loaded;size_t size=sizeof(loaded);
 if(readNvsRecord("csoscloud","settings",&loaded,size,millis())){
  const bool found=size==sizeof(loaded);
  if(found&&loaded.magic==management.magic&&memchr(loaded.token,0,256)&&memchr(loaded.name,0,81)&&memchr(loaded.seed,0,65))management=loaded;
  else if(size)recordFault(FaultArea::Nvs,FaultCode::NvsRead,0,millis());
 }
 if(strcmp(management.seed,CSOS_GH_BOOTSTRAP_ID)){
  ManagementSettings draft=management;
  strlcpy(draft.token,CSOS_GH_BOOTSTRAP_TOKEN,sizeof(draft.token));
  strlcpy(draft.seed,CSOS_GH_BOOTSTRAP_ID,sizeof(draft.seed));
  if(!draft.name[0])strlcpy(draft.name,CSOS_CODESPACE_NAME,sizeof(draft.name));
  if(!saveManagement(draft)){management=draft;preNote="Token in RAM; salvataggio non riuscito";}
 }
}
bool cloudRequest(CloudJobKind kind,bool automatic=false){
 if(jobActive||!controlWorker||WiFi.status()!=WL_CONNECTED)return false;
 if(kind!=CloudJobKind::Probe&&kind!=CloudJobKind::Connect&&!githubRetry.ready(millis()))return false;
 if((kind==CloudJobKind::Status||kind==CloudJobKind::Start||kind==CloudJobKind::Stop)&&!githubConfigured()){
  recordFault(FaultArea::Cloud,
   management.name[0]&&!managementMatchesHost(management.name)?FaultCode::CloudIdentity:FaultCode::CloudAccess,
   0,millis());return false;
 }
 CloudJob job;job.kind=kind;job.generation=pre.generation;
 if(kind==CloudJobKind::Stop){
  if(!confirmedStopStillValid()||!pre.canStop(millis()))return false;
  strlcpy(job.stopNetwork,stopNetwork.c_str(),sizeof(job.stopNetwork));job.stopStatusAt=pre.githubAt;
 }
 strlcpy(job.token,management.token,sizeof(job.token));strlcpy(job.name,management.name,sizeof(job.name));
 strlcpy(job.user,bridgeUser.c_str(),sizeof(job.user));strlcpy(job.password,bridgePassword.c_str(),sizeof(job.password));strlcpy(job.pin,serverPin.c_str(),sizeof(job.pin));
 if(!submitCloudJob(job)){
  recordFault(FaultArea::Cloud,FaultCode::CloudSubmit,int(kind),millis());return false;
 }
 jobActive=true;pre.operationPending=true;
 activeAutomatic=automatic;
 if(kind==CloudJobKind::Start||kind==CloudJobKind::Stop)pre.requested(kind==CloudJobKind::Start?GithubState::Available:GithubState::Stopped,millis());
 activeKind=kind;
 preNote=kind==CloudJobKind::Probe?"Verifica WSS e autenticazione CXL":
  (kind==CloudJobKind::Status?"Verifica stato GitHub":(kind==CloudJobKind::Start?"Richiedo avvio Codespace":"Richiedo arresto Codespace"));
 if(kind==CloudJobKind::Status)githubCheckAt=millis();
 if(kind==CloudJobKind::Probe)cxlCheckAt=millis();
 return true;
}
uint8_t wifiRecoveryStage=0;int wifiRecoveryIndex=0;uint32_t wifiRecoveryAt=0;
String wifiCandidate,wifiCandidatePass;
enum class WifiChangePhase:uint8_t {Idle,Disconnecting,Testing,DisconnectingToRestore,Restoring};
WifiChangePhase wifiChange=WifiChangePhase::Idle;
bool wifiSessionOnly=false,wifiChangeCancelled=false;
void beginWifiCandidate(const String& name,const String& password,uint32_t now){
 wifiCandidate=name;wifiCandidatePass=password;wifiRecoveryAt=now;wifiRecoveryStage=1;
 WiFi.mode(WIFI_STA);WiFi.persistent(false);WiFi.setSleep(false);WiFi.setAutoReconnect(false);
 WiFi.begin(wifiCandidate.c_str(),wifiCandidatePass.c_str());preNote="Collegamento Wi-Fi in corso";
}
void serviceWifiRecovery(uint32_t now){
 if(wifiAutoCancelled||jobActive)return;
 if(wifiChange!=WifiChangePhase::Idle||pendingWifiSsid.length()){
  switch(wifiChange){
   case WifiChangePhase::Idle:
    pre.invalidate();githubCheckAt=cxlCheckAt=0;githubBlocked=cxlBlocked=false;
    WiFi.setAutoReconnect(false);WiFi.disconnect();wifiRecoveryStage=0;wifiRecoveryAt=now;
    wifiChange=WifiChangePhase::Disconnecting;preNote="Cambio rete in corso";return;
   case WifiChangePhase::Disconnecting:
   case WifiChangePhase::DisconnectingToRestore:
    if(WiFi.status()==WL_CONNECTED){
     if(uint32_t(now-wifiRecoveryAt)<1500)return;
     recordFault(FaultArea::Wifi,FaultCode::WifiAssociation,1,now);
     WiFi.setAutoReconnect(true);preNote="Cambio rete non riuscito";
     break;
    }
    if(wifiChange==WifiChangePhase::DisconnectingToRestore||wifiChangeCancelled){
     if(!wifiSsid.length()){preNote="Rete assente";break;}
     beginWifiCandidate(wifiSsid,wifiPass,now);wifiChange=WifiChangePhase::Restoring;
     preNote="Ripristino rete precedente";return;
    }
    beginWifiCandidate(pendingWifiSsid,pendingWifiPass,now);wifiChange=WifiChangePhase::Testing;return;
   case WifiChangePhase::Testing:
    if(!wifiChangeCancelled&&WiFi.status()==WL_CONNECTED&&WiFi.SSID()==wifiCandidate){
     wifiSsid=wifiCandidate;wifiPass=wifiCandidatePass;
     wifiSessionOnly=!rememberKnownWifi(wifiSsid,wifiPass);
     WiFi.setAutoReconnect(true);
     preNote=wifiSessionOnly?"Connesso, rete non salvata":"Rete verificata e salvata";
     break;
    }
    if(!wifiChangeCancelled&&uint32_t(now-wifiRecoveryAt)<20000)return;
    if(!wifiChangeCancelled)recordFault(FaultArea::Wifi,FaultCode::WifiAssociation,0,now);
    WiFi.disconnect();
    wifiRecoveryAt=now;wifiChange=WifiChangePhase::DisconnectingToRestore;return;
   case WifiChangePhase::Restoring:
    if(WiFi.status()==WL_CONNECTED&&WiFi.SSID()==wifiCandidate){
     WiFi.setAutoReconnect(true);preNote="Rete precedente ripristinata";break;
    }
    if(uint32_t(now-wifiRecoveryAt)<20000)return;
    recordFault(FaultArea::Wifi,FaultCode::WifiRestore,0,now,DiagnosticSeverity::Error,!wifiChangeCancelled);
    WiFi.disconnect();preNote="Ripristino fallito; rete assente";break;
  }
  pendingWifiSsid="";pendingWifiPass="";wifiChange=WifiChangePhase::Idle;wifiChangeCancelled=false;
  wifiRecoveryStage=0;wifiCheckAt=now;firstNetwork=false;
  return;
 }
 if(WiFi.status()==WL_CONNECTED){
  if(wifiRecoveryStage&&WiFi.SSID()==wifiCandidate){
   if(wifiCandidate!=wifiSsid)wifiSessionOnly=false;
   wifiSsid=wifiCandidate;wifiPass=wifiCandidatePass;
   if(!wifiSessionOnly&&!rememberKnownWifi(wifiSsid,wifiPass))preNote="Connesso; profilo non salvato";
   WiFi.setAutoReconnect(true);
  }
  if(wifiRecoveryStage)WiFi.scanDelete();wifiRecoveryStage=0;return;
 }
 if(wifiRecoveryStage==0){
  if(!firstNetwork&&uint32_t(now-wifiCheckAt)<30000)return;
  firstNetwork=false;wifiCheckAt=now;wifiRecoveryIndex=0;
  if(wifiSsid.length()){beginWifiCandidate(wifiSsid,wifiPass,now);return;}
  wifiRecoveryStage=2;WiFi.scanNetworks(true);wifiRecoveryAt=now;return;
 }
 if(wifiRecoveryStage==1){
  if(uint32_t(now-wifiRecoveryAt)<20000)return;
  recordFault(FaultArea::Wifi,FaultCode::WifiAssociation,0,now,DiagnosticSeverity::Warning);
  if(wifiRecoveryIndex==0){wifiRecoveryStage=2;WiFi.scanNetworks(true);wifiRecoveryAt=now;return;}
  wifiRecoveryStage=3;
 }
 if(wifiRecoveryStage==2){
  const int count=WiFi.scanComplete();
  if(count==WIFI_SCAN_RUNNING&&uint32_t(now-wifiRecoveryAt)<12000)return;
  if(count<0){
   recordFault(FaultArea::Wifi,FaultCode::WifiScan,count,now,DiagnosticSeverity::Warning);
   WiFi.scanDelete();wifiRecoveryStage=0;preNote="Wi-Fi assente: scegli una rete";return;
  }
  wifiRecoveryStage=3;
 }
 if(wifiRecoveryStage==3){
  const int count=WiFi.scanComplete();
  while(wifiRecoveryIndex<knownWifi.count){
   const auto& profile=knownWifi.items[wifiRecoveryIndex++];
   if(wifiSsid==profile.ssid)continue;
   for(int i=0;i<count;i++)if(WiFi.SSID(i)==profile.ssid){beginWifiCandidate(profile.ssid,profile.password,now);return;}
  }
  WiFi.scanDelete();wifiRecoveryStage=0;preNote="Wi-Fi assente: scegli una rete";
 }
}
void configureManagement(){
 if(pre.lifecyclePending){uiMessage("GITHUB","Operazione in corso","Attendi la verifica dello stato");waitAnyKey(2000);return;}
 String name=management.name,token=management.token;
 if(!textInput("CODESPACE","Nome del Codespace",name,false,80)||name.isEmpty())return;
 if(!managementMatchesHost(name.c_str())){
  uiMessage("GITHUB","Nome ed endpoint WSS diversi","Correggi il nome del Codespace");waitAnyKey(2000);return;
 }
 if(!textInput("TOKEN GITHUB","Token locale (Tab mostra/nasconde)",token,true,255)||token.isEmpty())return;
 if(!confirmAccess("SALVARE GITHUB?","Nome e token del Codespace","Il token resta nascosto"))return;
 ManagementSettings draft=management;strlcpy(draft.name,name.c_str(),sizeof(draft.name));strlcpy(draft.token,token.c_str(),sizeof(draft.token));
  if(saveManagement(draft)){githubRetry.credentialsChanged(millis());pre.invalidate();githubFaultActive=cxlFaultActive=false;githubBlocked=false;githubCheckAt=cxlCheckAt=0;preNote="Accesso GitHub salvato";}
 else preNote="Accesso GitHub non salvato";
 uiMessage("GITHUB",preNote,"La sessione attuale non cambia");waitAnyKey(1800);
}
void preDesktopSystem(){
 csosControls.openOptions();invalidatePreDesktopView();csosOptGesture.reset();uiPrevMask=readKeyMask();hudDirty=true;
}
String githubCaption(){
 if(!pre.wifi)return "In attesa della rete";
 if(management.name[0]&&!managementMatchesHost(management.name))return "Nome diverso da WSS";
 if(!githubConfigured())return "Accesso da configurare";
 if(!githubRetry.ready(millis()))return "Verifica temporaneamente sospesa";
 if(pre.lifecyclePending)return pre.requestedTarget==GithubState::Available?"Avvio da verificare":"Arresto da verificare";
 if(githubBlocked)return "Accesso da verificare";
 if(pre.githubState==GithubState::Unknown||!pre.fresh(millis(),pre.githubAt))
  return jobActive&&activeKind==CloudJobKind::Status?"Verifica in corso":"Stato sconosciuto";
 switch(pre.githubState){
 case GithubState::Stopped:return "Fermo";case GithubState::Available:return "In esecuzione";
 case GithubState::Starting:return "Avvio in corso";case GithubState::Stopping:return "Arresto in corso";
 case GithubState::Unavailable:return "Non disponibile";
 default:return "Stato ignoto";
 }
}
String desktopCaption(uint32_t now){
 if(!pre.wifi)return "In attesa della rete";
 if(entryPending)return "Collegamento in corso";
 if(pre.lifecyclePending)return "In attesa del Codespace";
 if(pre.desktopReady(now))return "Pronto";
 if(jobActive&&activeKind==CloudJobKind::Probe)return "Verifica in corso";
 if(pre.cxlChecked&&pre.fresh(now,pre.cxlAt)){
  if(pre.cxlAuthRejected)return "Accesso rifiutato";
  if(cxlBlocked)return "Accesso da verificare";
  return "Verifica fallita";
 }
 if(pre.githubState==GithubState::Stopped&&pre.fresh(now,pre.githubAt))return "In attesa dell'avvio";
 return "Da verificare";
}
PreDesktopToolsScreen preDesktopToolsScreen(){
 PreDesktopToolsScreen screen;
 if(csosControls.adjustingLight){screen.lightAdjust=true;screen.lightExternal=csosControls.lightExternal;
  screen.lightValue=(screen.lightExternal?externalBrightness:brightness)*10;screen.saveFailed=csosSaveFailed;return screen;}
 if(csosControls.menu){
  for(int i=0;i<4;i++)screen.lines.push_back(hudMainLabel(i));
  screen.title="IMPOSTAZIONI";screen.selected=screen.position=csosControls.page;screen.total=4;
  screen.help=appToolHint(csosControls.page);
 }else if(csosControls.system&&!csosControls.unavailable){
  screen.title=hudMainLabel(csosControls.group);screen.total=csosControls.itemCount();
  screen.selected=screen.position=csosControls.systemIndex;screen.help=hudCommandHint(csosControls.selectedCommand());
  for(int i=0;i<screen.total;i++){
   const auto command=csosControls.item(i);String label=hudCommandLabel(command),value;
   if(command==LocalCommand::Brightness)value=String(brightness*10)+"%";
   if(command==LocalCommand::ExternalBrightness)value=String(externalBrightness*10)+"%";
   if(command==LocalCommand::ExternalLight)label+=" (in sessione)";
   if(command==LocalCommand::Fps)value=fpsCapNames[fpsCapIdx];
   if(command==LocalCommand::Quality)value=qualityNames[quality];
   if(command==LocalCommand::Cursor)value=cursorShown?"si":"no";
   screen.lines.push_back(label);screen.values.push_back(value);
  }
 }else if(csosControls.confirm){
  screen.title="CONFERMA";screen.stopConfirmation=csosControls.confirmed==LocalCommand::Stop;
 }else if(csosControls.guide){
  screen.guideTopic=csosControls.guidePage;
  const auto pages=guidePages(tft,screen.guideTopic,false);csosControls.guideCount=pages.size();
  screen.position=csosControls.guideDetail%csosControls.guideCount;screen.total=pages.size();
  screen.title=pages[screen.position].command;screen.lines=pages[screen.position].lines;
 }else if(csosControls.superMenu){
  screen.help="Questi comandi richiedono il desktop";screen.title="LINUX / SUPER";screen.lines=hudWrap("Serve una sessione Linux. Nessun comando inviato.",268,.9f);
 }else{
  screen.title=csosControls.unavailable?"INDISPONIBILE":csosControls.page==6?"CARDPUTER":csosControls.page==7?"INFO":"DIAGNOSTICA";
  screen.help=csosControls.page==5?"Riprova verifica; non avvia la macchina":"Esc: indietro";
  if(csosControls.page==7){
   csosControls.detailCount=1;csosControls.detailPage=0;
   screen.lines={"Dispositivo","TFT esterno","HUD interno","Pennino"};screen.values={"ADV","320x240","240x135","9 punti"};
   screen.help="Calibrazione incorporata nel firmware";return screen;
  }
  String message=localPanelMessage();
  std::vector<String> lines;
  if(!csosControls.unavailable&&(csosControls.page==5||csosControls.page==6||csosControls.page==8)){
   const auto pages=diagnosticSheets({pre.wifi?String("Wi-Fi: connesso"):String("Wi-Fi: assente"),String("GitHub: ")+githubDetail,String("CXL: ")+cxlDetail},268,4,.9f,csosControls.page==8);
   csosControls.detailCount=pages.size();screen.position=csosControls.detailPage%pages.size();screen.total=pages.size();
   screen.title=pages[screen.position].title;screen.lines=pages[screen.position].lines;
   screen.help=csosControls.page==8?"Eventi registrati, dal piu recente":"Dati del Cardputer e della sessione";
   if(csosControls.page==8){sessionFaultNotice=false;areaFaultNotice=false;}
   return screen;
  }else lines=hudWrap(message,268,.9f);csosControls.detailCount=std::max(1,int((lines.size()+3)/4));
  screen.position=csosControls.detailPage%csosControls.detailCount;screen.total=csosControls.detailCount;
  for(int i=screen.position*4;i<std::min(int(lines.size()),(screen.position+1)*4);i++)screen.lines.push_back(lines[i]);
 }
 return screen;
}

}
void confirmCodespaceStop(){
 stopGeneration=pre.generation;stopNetwork=WiFi.SSID();
 pendingLifecycleCheck=false;pendingSetup=PendingSetup::Stop;
}
void pauseAutoWifi(){wifiAutoCancelled=true;wifiRecoveryStage=0;WiFi.scanDelete();}
void resumeAutoWifi(){wifiAutoCancelled=false;}
void beginPreDesktop(){
 csosPreDesktopActive=true;
 loadManagement();controlWorker=beginCloudControl();
 CSOS_LOGF("[CSOS] pre_desktop=v1 manual_entry=1 profiles=%u worker=%d\n",knownWifi.count,int(controlWorker));
 if(!controlWorker){
  recordFault(FaultArea::Boot,FaultCode::BootWorker,0,millis());
  preNote="Controlli cloud: memoria insufficiente";
 }
}
void scheduleCloudChecks(uint32_t now){
 if(!pre.wifi||jobActive||preDesktopEditing||wifiAutoCancelled||wifiChange!=WifiChangePhase::Idle||
    pendingWifiSsid.length()||pendingSetup!=PendingSetup::None||entryPending)return;
 const bool githubDue=githubConfigured()&&!githubBlocked&&
  (!githubCheckAt||uint32_t(now-githubCheckAt)>15000);
 const bool cxlDue=!pre.lifecyclePending&&!cxlBlocked&&
  (!cxlCheckAt||uint32_t(now-cxlCheckAt)>15000)&&(!githubConfigured()||pre.githubState!=GithubState::Stopped);
 // If CXL has expired, probe first unless GitHub must confirm a lifecycle transition.
 if(cxlDue&&!pre.desktopReady(now)&&(!githubDue||githubCheckAt))cloudRequest(CloudJobKind::Probe,true);
 else if(githubDue)cloudRequest(CloudJobKind::Status,true);
 else if(cxlDue)cloudRequest(CloudJobKind::Probe,true);
}
bool canRequestEntry(uint32_t now){
 return pre.desktopReady(now)&&pendingSetup==PendingSetup::None&&!pendingWifiSsid.length()&&
  (!jobActive||activeKind==CloudJobKind::Status||activeKind==CloudJobKind::Probe);
}
bool canOfferStop(){
 return pre.wifi&&githubConfigured()&&!githubBlocked&&!pre.lifecyclePending&&
  pendingSetup==PendingSetup::None&&!pendingWifiSsid.length()&&pre.githubState==GithubState::Available;
}
void servicePreDesktopBackground(bool schedule){
 const uint32_t now=millis();
 const bool wifiUp=WiFi.status()==WL_CONNECTED;
 const String network=wifiUp?WiFi.SSID():String("");
 if(pre.wifi!=wifiUp||(wifiUp&&observedWifi!=network))githubFaultActive=cxlFaultActive=false;
 if(wifiUp&&pre.wifi&&observedWifi!=network)pre.invalidate();
 observedWifi=network;
 pre.network(wifiUp);
 CloudResult result;
 if(pollCloudResult(result)){
  CSOS_LOGF("[CSOS] cloud_result kind=%u http=%d ok=%d state=%u generation=%lu\n",unsigned(result.kind),result.http,int(result.ok),unsigned(result.state),(unsigned long)result.generation);
  jobActive=false;pre.operationPending=false;
  if(result.generation==pre.generation){
   if(result.kind!=CloudJobKind::Probe)githubRetry.observe(result.http,result.retryAfterMs,now,result.rateLimited);
   bool& unresolved=result.kind==CloudJobKind::Probe?cxlFaultActive:githubFaultActive;
   if(!result.ok){
    const int detail=result.http?result.http:-int(result.transport);
    const auto& stored=areaFaults[size_t(FaultArea::Cloud)];
    const bool repeated=activeAutomatic&&unresolved&&stored.event.code==uint16_t(FaultCode::CloudRequest)&&stored.detail==detail;
    recordFault(FaultArea::Cloud,FaultCode::CloudRequest,detail,now,
     result.authRejected||result.http==401||result.http==403?DiagnosticSeverity::Error:DiagnosticSeverity::Warning,
     false,!repeated);
   }
   unresolved=!result.ok;
   if(result.ok){
    if(result.kind==CloudJobKind::Probe)preNote="Desktop pronto";
    else if(result.kind==CloudJobKind::Start||result.kind==CloudJobKind::Stop)preNote="Richiesta accettata: verifico lo stato";
    else if(result.state==GithubState::Stopped)preNote="Codespace fermo: puoi avviarlo";
    else if(result.state==GithubState::Available)preNote="Codespace disponibile: verifico CXL";
    else preNote="Stato GitHub aggiornato";
   }else if(result.rateLimited)preNote="Limite GitHub: verifica rinviata automaticamente";
   else if(result.http==403)preNote="HTTP 403: causa da verificare in Diagnostica";
   else if(result.http==401)preNote=result.kind==CloudJobKind::Probe?"WSS: accesso non accettato":"GitHub: token non accettato";
   else preNote=result.kind==CloudJobKind::Probe?"CXL non pronto: puoi ricontrollare":"GitHub non verificato: puoi ricontrollare";
   if(result.kind==CloudJobKind::Probe){pre.cxl(result.ok,now,result.generation,result.authRejected);cxlDetail=result.detail;cxlBlocked=result.authRejected||transportNeedsManualRetry(result.transport);}
   else if(result.kind==CloudJobKind::Status){pre.github(result.ok?result.state:GithubState::Unknown,now,result.generation);githubDetail=result.detail;githubBlocked=result.http==401||result.authRejected;}
   else {
    pre.github(GithubState::Unknown,now,result.generation);githubDetail=result.detail;githubCheckAt=0;
    if((result.http>=400&&result.http<500)||(!result.ok&&result.http==0))pre.lifecyclePending=false;
    if(!result.ok&&result.http==0)preNote=result.detail;
   }
  }
  preDrawAt=0;
 }
 // End the local lifecycle wait after the existing 30 s recovery threshold.
 // Do this when the worker is idle, also if R was pressed during its request.
 // The next read must verify GitHub again; expiry never submits Start or Stop.
 if(pre.lifecyclePending&&uint32_t(now-pre.requestedAt)>=30000&&!jobActive){
  pre.lifecyclePending=false;pre.invalidate();githubCheckAt=cxlCheckAt=0;
  preNote="Esito non confermato: verifico lo stato";preDrawAt=0;
 }
 serviceWifiRecovery(now);
 if(!jobActive)serviceFaultTraces();
 if(schedule)scheduleCloudChecks(now);
}
bool servicePreDesktop(){
 const bool menuBefore=csosControls.menu,systemBefore=csosControls.system;
 servicePreDesktopBackground(false);const uint32_t now=millis();
 const int primary=!pre.wifi?0:canRequestEntry(now)?1:pre.canStart(now)?2:4;
 if(primary!=preDesktopPrimaryAction()){preDesktopPrimaryAction()=primary;++localTouchGeneration();}
 if(preSelection!=3)preSelection=primary;
 // Editor requests are consumed outside the streaming session. Lifecycle
 // requests wait for a fresh GitHub state; a retry is never an implicit start.
 if(pendingSetup!=PendingSetup::None&&!jobActive&&wifiChange==WifiChangePhase::Idle&&!pendingWifiSsid.length()){
  const auto requested=pendingSetup;
  if(requested==PendingSetup::Access||requested==PendingSetup::Wifi||requested==PendingSetup::Management){
   const String userBefore=bridgeUser,passwordBefore=bridgePassword,pinBefore=serverPin;
   pendingSetup=PendingSetup::None;preDesktopEditing=true;
   if(requested==PendingSetup::Access)configureCloudAccess();
   else if(requested==PendingSetup::Wifi){wifiSetup();wifiAutoCancelled=false;}
   else configureManagement();
   preDesktopEditing=false;
   if(userBefore!=bridgeUser||passwordBefore!=bridgePassword||pinBefore!=serverPin){
    pre.invalidate();cxlFaultActive=false;cxlBlocked=false;cxlCheckAt=0;
   }
   uiPrevMask=readKeyMask();csosOptGesture.reset();preDrawAt=0;hudDirty=true;
  }else if(requested==PendingSetup::Stop&&!confirmedStopStillValid()){
   pendingSetup=PendingSetup::None;pendingLifecycleCheck=false;preNote="Contesto cambiato: conferma di nuovo Arresta";
  }else if(!pre.wifi||githubBlocked||!githubConfigured()){
   pendingSetup=PendingSetup::None;preNote=!pre.wifi?"Prima connetti Wi-Fi":"Verifica accesso GitHub";
  }else if(pre.githubState==GithubState::Unknown||!pre.fresh(now,pre.githubAt)){
   if(!pendingLifecycleCheck){pendingLifecycleCheck=cloudRequest(CloudJobKind::Status);}
   else {pendingSetup=PendingSetup::None;pendingLifecycleCheck=false;preNote="Stato GitHub ignoto: operazione non inviata";}
  }else{
   pendingSetup=PendingSetup::None;pendingLifecycleCheck=false;
   if(requested==PendingSetup::Start&&pre.canStart(now))cloudRequest(CloudJobKind::Start);
   else if(requested==PendingSetup::Stop&&confirmedStopStillValid()&&pre.canStop(now))cloudRequest(CloudJobKind::Stop);
   else preNote="Operazione non consentita dallo stato GitHub verificato";
  }
 }
 if(csosOfflineRetry){csosOfflineRetry=false;githubFaultActive=cxlFaultActive=false;githubCheckAt=cxlCheckAt=0;githubBlocked=cxlBlocked=false;wifiAutoCancelled=false;}
 UiKey key=uiPoll();int action=-1,x=0,y=0;
 if(wifiChange!=WifiChangePhase::Idle){
  const bool restoring=wifiChangeCancelled||wifiChange==WifiChangePhase::Restoring||wifiChange==WifiChangePhase::DisconnectingToRestore;
  if(!restoring&&(key.opt||key.base=='`'||(localPanelTap(x,y)&&appHomePrimary().contains(x,y))))wifiChangeCancelled=true;
  PreDesktopScreen screen;screen.wifi=pendingWifiSsid;screen.github="In attesa della rete";screen.cxl="In attesa della rete";
  screen.note=restoring?"Ripristino rete precedente":"Cambio rete; Esc per annullare";
  screen.busy=true;screen.connecting=!restoring;screen.locked=restoring;screen.selected=restoring?-1:1;
  screen.wifiChanging=!restoring;screen.wifiRestoring=restoring;
  drawPreDesktop(tft,screen,now);serviceHud();serviceHaptic();delay(3);return false;
 }
 const bool handlingEntry=entryPending;
 if(handlingEntry){
  const bool cancelled=key.opt||key.base=='`'||(localPanelTap(x,y)&&appHomePrimary().contains(x,y));
  if(cancelled||entryGeneration!=pre.generation||!pre.desktopReady(now)||pendingWifiSsid.length()){
   entryPending=false;preNote=cancelled?"Ingresso annullato":"Desktop da verificare: premi Entra quando pronto";preDrawAt=0;
  }else if(!jobActive){
   entryPending=false;csosPreDesktopActive=false;uiPrevMask=readKeyMask();return true;
  }
  key={}; // Ignore additional input until the queued entry is resolved.
 }
 const auto gesture=handlingEntry?ControlGesture::None:csosOptGesture.update(bool(uiPrevMask&kbit(IDX_OPT)),now,csosControls.local());
 if(gesture==ControlGesture::Cycle){csosControls.openOptions();invalidatePreDesktopView();hudDirty=true;}
 if(gesture==ControlGesture::Open){csosControls.openOptions();invalidatePreDesktopView();hudDirty=true;}
 if(gesture==ControlGesture::Close){csosControls.close();invalidatePreDesktopView();hudDirty=true;}
 const bool local=csosControls.local();
 if(local&&key.idx>=0&&!key.opt&&!(uiPrevMask&kbit(IDX_OPT))){
  if(preStopConfirmation&&key.base=='`'){csosControls.close();preStopConfirmation=false;}
  else executeLocalCommand(csosControls.press(false,false,key.base==KEY_ENTER?13:key.base==KEY_TAB?9:key.base));
  hudDirty=true;
 }else if(!local&&key.idx>=0&&!key.opt&&gesture==ControlGesture::None){
  preSelection=preDesktopNavigate(preSelection,key.base==KEY_TAB?9:key.base);
  if(key.base==KEY_ENTER)action=preSelection;
  if(key.base=='r'||key.base=='R'){
   githubFaultActive=cxlFaultActive=false;
   githubCheckAt=cxlCheckAt=0;githubBlocked=cxlBlocked=false;wifiAutoCancelled=false;wifiCheckAt=0;firstNetwork=true;
  }
 }
 const bool panelTapped=!handlingEntry&&localPanelTap(x,y);
 if(panelTapped){
  if(!local)action=preDesktopHit(x,y);
  else if(csosControls.guide){
   const auto guideKey=guideTftKey(x,y);
   if(guideKey){executeLocalCommand(csosControls.press(false,false,guideKey));hudDirty=true;}
  }
  else if(csosControls.confirm){
   if(appConfirmCancel().contains(x,y)){
    if(preStopConfirmation){csosControls.close();preStopConfirmation=false;}
    else csosControls.press(false,false,'`');
   }else if(appConfirmCommit().contains(x,y))executeLocalCommand(csosControls.press(false,false,13));
  }
  else if(csosControls.adjustingLight&&y>=165&&y<189&&x>=8&&x<62)executeLocalCommand(csosControls.press(false,false,x<35?',':'/'));
  else if(x>=275&&y<32){if(preStopConfirmation){csosControls.close();preStopConfirmation=false;}else csosControls.press(false,false,'`');}
  else if(!csosControls.menu&&!csosControls.adjustingLight&&x>=236&&x<266&&y<32)executeLocalCommand(csosControls.press(false,false,13));
  else if(csosControls.menu&&AppRect{222,211,90,24}.contains(x,y))executeLocalCommand(csosControls.press(false,false,13));
  else if(y>=214&&x>=8&&x<67)executeLocalCommand(csosControls.press(false,false,x<36?';':'.'));
  else if((csosControls.menu||csosControls.system)&&!csosControls.confirm&&y>=43&&y<195){
   const auto tools=preDesktopToolsScreen();const int top=tools.selected>=0?std::max(0,std::min(tools.selected-1,int(tools.lines.size())-4)):0;
   const int index=top+(y-43)/38;
   if(index<int(tools.lines.size())){
    if(csosControls.menu)csosControls.page=index;
    else if(csosControls.system)csosControls.systemIndex=index;
    executeLocalCommand(csosControls.press(false,false,13));hudDirty=true;
   }
  }
 }
 if(!csosControls.confirm)preStopConfirmation=false;
 if(menuBefore!=csosControls.menu||systemBefore!=csosControls.system)
  CSOS_LOGF("[CSOS] panel_transition menu=%d->%d system=%d->%d key=%u opt=%d gesture=%u touch=%d x=%d y=%d uptime_ms=%lu\n",int(menuBefore),int(csosControls.menu),int(systemBefore),int(csosControls.system),unsigned(key.base),int(key.opt),unsigned(gesture),int(panelTapped),x,y,(unsigned long)millis());
 if(local!=csosControls.local())invalidatePreDesktopView();
 if(action==0&&pendingSetup==PendingSetup::None){pendingSetup=PendingSetup::Wifi;preDrawAt=0;}
 if(action==4&&(!pre.lifecyclePending||uint32_t(now-pre.requestedAt)>=30000)){
  if(githubRetry.ready(now)){githubBlocked=false;githubCheckAt=0;}
  cxlBlocked=false;cxlCheckAt=0;preDrawAt=0;
 }
 if(action==3)preDesktopSystem();
 if(action==2){
  if(canOfferStop()){
   csosControls.close();csosControls.confirmed=LocalCommand::Stop;csosControls.confirm=true;preStopConfirmation=true;
   invalidatePreDesktopView();hudDirty=true;
  }else if(pre.canStart(now))cloudRequest(CloudJobKind::Start);
  else preNote="Verifica lo stato GitHub prima di avviare o arrestare";
 }
 if(action==1&&!entryPending){
  if(canRequestEntry(now)){
   if(jobActive){entryPending=true;entryGeneration=pre.generation;preDrawAt=0;}
   else {
   PreDesktopScreen screen;screen.wifi=WiFi.SSID();screen.github=githubCaption();screen.cxl="Collegamento in corso";
   screen.note="Attendo la sessione WSS/CXL";screen.busy=true;screen.selected=1;screen.stop=pre.canStop(now);
   drawPreDesktop(tft,screen,now);csosPreDesktopActive=false;uiPrevMask=readKeyMask();return true;
   }
  }else{
   preNote="Desktop da verificare";
  }
 }
 if(!preDrawAt||uint32_t(now-preDrawAt)>=200){
  if(csosControls.local())drawPreDesktopTools(tft,preDesktopToolsScreen(),now);
  else {
  PreDesktopScreen screen;screen.wifi=pre.wifi?WiFi.SSID():wifiRecoveryStage==1?wifiCandidate:wifiRecoveryStage==2?String("Ricerca reti note"):String("Non connesso");screen.github=githubCaption();
  screen.cxl=desktopCaption(now);screen.note=preNote;
  if(entryPending){screen.connecting=true;screen.note="Attendo la verifica; Esc per annullare";}
  else if(sessionFaultNotice||areaFaultNotice)screen.note="Ultimo guasto in Diagnostica";
  else if(wifiSessionOnly&&pre.wifi&&!jobActive)screen.note="Connesso, rete non salvata";
  screen.enter=canRequestEntry(now);screen.start=pre.canStart(now);screen.stop=canOfferStop();screen.busy=jobActive;screen.selected=preSelection;
  screen.wifiConnected=pre.wifi;screen.starting=pre.githubState==GithubState::Starting;
  screen.accessError=githubBlocked||cxlBlocked;screen.fault=sessionFaultNotice||areaFaultNotice;
  screen.requestPending=pre.lifecyclePending;screen.recoverable=pre.lifecyclePending&&uint32_t(now-pre.requestedAt)>=30000;
  if(!githubRetry.ready(now))screen.retrySeconds=(githubRetry.delayMs-uint32_t(now-githubRetry.since)+999)/1000;
  drawPreDesktop(tft,screen,now);}
  preDrawAt=now;
 }
 scheduleCloudChecks(now);
 serviceHud();serviceHaptic();delay(3);return false;
}
void preDesktopSessionEnded(){
 invalidatePreDesktopView();
 csosPreDesktopActive=true;
 const bool keepStop=pendingSetup==PendingSetup::Stop&&confirmedStopStillValid();
 releaseAll();csosControls.close();pre.invalidate();githubCheckAt=cxlCheckAt=0;
 if(keepStop)stopGeneration=pre.generation; // Normal stream teardown only expires cached status.
 preNote=connError.length()?connError:String("Desktop scollegato; Codespace non arrestato");
 cxlDetail=preNote;preDrawAt=0;uiPrevMask=readKeyMask();
}
