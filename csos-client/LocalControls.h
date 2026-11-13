#pragma once
#include <cstdint>
#include "DiagnosticSerial.h"
constexpr int LOCAL_GUIDE_PAGES=13;
// Mouse/View are compatibility names for the same unified control asset.
enum class InputMode:uint8_t {Control=0,Mouse=Control,View=Control,Keys=1};
enum class LocalCommand : uint8_t {
 None,Remote,ZoomIn,ZoomOut,Fit,ViewToggle,Mouse,Refresh,Quality,Cursor,Monitor,
 Brightness,Fps,SuperTap,SuperNext,Disconnect,Configure,Retry,WheelUp,WheelDown,OpenGuide,OpenDiagnostics,
 PanUp,PanDown,PanLeft,PanRight,CancelInput,Wifi,Management,Start,Stop,Telemetry,Info,ExternalBrightness,ExternalLight,WindowMove,LightIncrease,LightDecrease,OpenLogs,OpenAudio,AudioToggle,AudioUp,AudioDown
};
struct LocalControls {
 InputMode mode=InputMode::Keys;
 bool audio=false;uint8_t audioIndex=0;
 bool movingWindow=false,precise=false,adjustingLight=false,lightExternal=false;
 bool fn=false,aa=false,menu=false,guide=false,details=false,system=false,superMenu=false,confirm=false,unavailable=false;
 uint8_t page=0,group=0,guidePage=0,guideDetail=0,detailPage=0,systemIndex=0,superIndex=0;
 uint8_t detailCount=1,guideCount=2;
 LocalCommand confirmed=LocalCommand::Disconnect;
 bool local()const{return audio||menu||guide||details||system||superMenu||confirm||unavailable||adjustingLight;}
 bool vista()const{return mode==InputMode::View&&!local();}
 bool mouse()const{return mode==InputMode::Mouse&&!local();}
 bool pointerAllowed()const{return !local();}
 void close(){audio=false;menu=guide=details=system=superMenu=confirm=unavailable=adjustingLight=false;}
 void openOptions(){close();menu=true;page=0;}
 void cycle(){mode=mode==InputMode::Keys?InputMode::Control:InputMode::Keys;}
 void toggleAa(){if(mode==InputMode::Keys||local())aa=!aa;else precise=!precise;}
 int itemCount()const{return group==0?10:group==1?4:group==2?3:6;}
 LocalCommand item(int index)const{
  static constexpr LocalCommand screens[]={LocalCommand::Brightness,LocalCommand::ExternalBrightness,LocalCommand::ExternalLight,LocalCommand::Fps,LocalCommand::Quality,LocalCommand::Cursor,LocalCommand::Monitor,LocalCommand::Refresh,LocalCommand::Telemetry,LocalCommand::Info};
  static constexpr LocalCommand access[]={LocalCommand::Wifi,LocalCommand::Configure,LocalCommand::Management,LocalCommand::Retry};
  static constexpr LocalCommand help[]={LocalCommand::OpenGuide,LocalCommand::OpenDiagnostics,LocalCommand::OpenLogs};
  static constexpr LocalCommand machine[]={LocalCommand::Start,LocalCommand::Stop,LocalCommand::Disconnect,LocalCommand::SuperTap,LocalCommand::SuperNext,LocalCommand::OpenAudio};
  if(index<0||index>=itemCount())return LocalCommand::None;
  return group==0?screens[index]:group==1?access[index]:group==2?help[index]:machine[index];
 }
 LocalCommand selectedCommand()const{return item(systemIndex);}
 LocalCommand press(bool fnKey,bool optKey,uint8_t base){
  if(fnKey){fn=!fn;
   return LocalCommand::None;}
  if(optKey){if(local())close();else openOptions();
   return LocalCommand::None;}
  if(unavailable){unavailable=false;
   return LocalCommand::None;}
  const bool next=base=='.'||base==9,prev=base==';';
  if(!local()){
   if(base=='`'&&mode!=InputMode::Keys)return LocalCommand::CancelInput;
   if(mode!=InputMode::View)return LocalCommand::Remote;
   if(base=='f')return LocalCommand::WindowMove;
   if(base=='=')return LocalCommand::ZoomIn;
   if(base=='-')return LocalCommand::ZoomOut;
   if(base=='[')return LocalCommand::WheelUp;
   if(base==']')return LocalCommand::WheelDown;
   if(base=='e')return LocalCommand::PanUp;
   if(base=='s')return LocalCommand::PanDown;
   if(base=='a')return LocalCommand::PanLeft;
   if(base=='d')return LocalCommand::PanRight;
   if(base==9)return LocalCommand::ViewToggle;
   return LocalCommand::Remote;
  }
  if(audio){
   if(base=='`'){audio=false;system=true;group=3;return LocalCommand::None;}
   constexpr uint8_t audioItems=CSOS_USB_CONSOLE ? 6 : 5;
   if(next)audioIndex=(audioIndex+1)%audioItems;
   if(prev)audioIndex=(audioIndex+audioItems-1)%audioItems;
   if(base==','||base=='/')return base=='/'?LocalCommand::AudioUp:LocalCommand::AudioDown;
   if(base==13)return audioIndex==0||audioIndex==1||audioIndex==5?LocalCommand::AudioToggle:LocalCommand::AudioUp;
   return LocalCommand::None;
  }
  if(base=='`'){
   if(adjustingLight){adjustingLight=false;system=true;}
   else if(confirm){confirm=false;system=true;}
   else if(details&&(page==6||page==7)){
    // Device documents have their own page IDs, not root-menu indices.
    details=false;system=true;group=0;page=0;
   }else if(system||guide||details||superMenu){
    const uint8_t parent=system?group:(guide||details)?2:3;
    close();menu=true;page=parent;
   }
   else close();
   return LocalCommand::None;
  }
  if(confirm){if(base==13){const auto action=confirmed;close();return action;}return LocalCommand::None;}
  if(adjustingLight){
   if(base=='/'||base=='.'||base=='=')return LocalCommand::LightIncrease;
   if(base==','||base==';'||base=='-')return LocalCommand::LightDecrease;
   return LocalCommand::None;
  }
  if(guide){
   if(next||prev){guidePage=(guidePage+(prev?LOCAL_GUIDE_PAGES-1:1))%LOCAL_GUIDE_PAGES;guideDetail=0;}
   if(base==','||base=='/'||base==13)guideDetail=(guideDetail+(base==','?guideCount-1:1))%guideCount;
   return LocalCommand::None;
  }
  if(details){if(next||prev||base==','||base=='/')detailPage=(detailPage+(prev?detailCount-1:1))%detailCount;
   if(base==13&&page==5)return LocalCommand::Retry;
   return LocalCommand::None;}
  if(superMenu){if(next||prev)superIndex=1-superIndex;
   if(base==13){const bool pending=superIndex;close();return pending?LocalCommand::SuperNext:LocalCommand::SuperTap;}return LocalCommand::None;}
  if(system){
   if(next)systemIndex=(systemIndex+1)%itemCount();
   if(prev)systemIndex=(systemIndex+itemCount()-1)%itemCount();
   if(base!=13)return LocalCommand::None;
   const auto command=item(systemIndex);
   if(command==LocalCommand::OpenAudio){system=false;audio=true;audioIndex=0;return LocalCommand::None;}
   if(command==LocalCommand::OpenGuide){system=false;guide=true;guidePage=guideDetail=0;return LocalCommand::None;}
   if(command==LocalCommand::OpenDiagnostics||command==LocalCommand::OpenLogs){
    system=false;details=true;page=command==LocalCommand::OpenLogs?8:5;detailPage=0;
    return LocalCommand::None;
   }
   if(command==LocalCommand::Disconnect||command==LocalCommand::Stop){confirmed=command;system=false;confirm=true;
   return LocalCommand::None;}
   if(command==LocalCommand::Telemetry||command==LocalCommand::Info){system=false;details=true;page=command==LocalCommand::Telemetry?6:7;detailPage=0;
   return LocalCommand::None;}
   return command;
  }
  if(next)page=(page+1)%4;
   if(prev)page=(page+3)%4;
  if(base==13){menu=false;
   system=true;group=page;systemIndex=0;}
  return LocalCommand::None;
 }
};
