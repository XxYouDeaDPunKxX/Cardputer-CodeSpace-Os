#pragma once
#include <cstdint>
#include <cstring>
struct WifiProfile {char ssid[33]={};char password[65]={};};
inline bool verifiedWifiConnection(bool helperResult,bool stationConnected,const char* actual,const char* requested){
 return helperResult&&stationConnected&&actual&&requested&&std::strcmp(actual,requested)==0;
}
struct WifiProfiles {
 static constexpr uint32_t MAGIC=0x43535731;
 static constexpr int MAX=8;
 uint32_t magic=MAGIC;uint8_t count=0,preferred=0;uint8_t reserved[2]={};
 WifiProfile items[MAX];
 bool valid()const{
  if(magic!=MAGIC||count>MAX||(count&&preferred>=count))return false;
  for(int i=0;i<count;i++)if(!items[i].ssid[0]||!std::memchr(items[i].ssid,0,33)||!std::memchr(items[i].password,0,65))return false;
  return true;
 }
 int find(const char* name)const{for(int i=0;i<count;i++)if(!std::strcmp(items[i].ssid,name))return i;return -1;}
 bool remember(const char* name,const char* pass){
  if(!name||!pass||!name[0]||std::strlen(name)>32||std::strlen(pass)>64)return false;
  int at=find(name);if(at<0){if(count==MAX)return false;at=count++;}
  std::strcpy(items[at].ssid,name);std::strcpy(items[at].password,pass);preferred=at;return true;
 }
 bool forget(int at){
  if(at<0||at>=count)return false;
  for(int i=at;i+1<count;i++)items[i]=items[i+1];
  items[--count]={};if(!count||preferred==at)preferred=0;else if(preferred>at)--preferred;
  return true;
 }
};
