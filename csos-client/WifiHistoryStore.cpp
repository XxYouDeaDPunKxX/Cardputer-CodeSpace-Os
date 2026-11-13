#include "WifiHistoryStore.h"
#include <Preferences.h>
#include "FaultDiagnostics.h"
#include "NvsDiagnostic.h"
WifiProfiles knownWifi;
namespace {
bool persist(const WifiProfiles& draft){
 Preferences store;if(!store.begin("csoswifi",false)){
  recordFault(FaultArea::Nvs,FaultCode::NvsOpen,0,diagnosticTime());return false;
 }
 const bool ok=store.putBytes("profiles",&draft,sizeof(draft))==sizeof(draft);store.end();
 if(ok)knownWifi=draft;
 else recordFault(FaultArea::Nvs,FaultCode::NvsWrite,0,diagnosticTime());
 return ok;
}
}
bool loadKnownWifi(String& ssid,String& password){
 WifiProfiles loaded;size_t size=sizeof(loaded);
 if(!readNvsRecord("csoswifi","profiles",&loaded,size,diagnosticTime()))return false;
 if(size!=sizeof(loaded)||!loaded.valid()){
  recordFault(FaultArea::Nvs,FaultCode::NvsRead,0,diagnosticTime());return false;
 }
 knownWifi=loaded;
 if(loaded.count){ssid=loaded.items[loaded.preferred].ssid;password=loaded.items[loaded.preferred].password;}
 else {ssid="";password="";}
 return true;
}
bool rememberKnownWifi(const String& ssid,const String& password){
 WifiProfiles draft=knownWifi;if(!draft.remember(ssid.c_str(),password.c_str()))return false;
 if(!memcmp(&draft,&knownWifi,sizeof(draft)))return true;
 return persist(draft);
}
bool forgetKnownWifi(int index){WifiProfiles draft=knownWifi;return draft.forget(index)&&persist(draft);}
