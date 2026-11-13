#pragma once
#include <cstdint>
enum class GithubState:uint8_t {Unknown,Stopped,Starting,Available,Stopping,Unavailable};
struct PreDesktopModel {
 bool wifi=false,cxlReady=false,cxlChecked=false,cxlAuthRejected=false,operationPending=false,lifecyclePending=false;
 GithubState requestedTarget=GithubState::Unknown;
 uint32_t requestedAt=0;
 GithubState githubState=GithubState::Unknown;
 uint32_t generation=0,githubAt=0,cxlAt=0;
 void invalidate(){++generation;cxlReady=cxlChecked=cxlAuthRejected=false;githubState=GithubState::Unknown;}
 void network(bool up){if(wifi!=up){wifi=up;invalidate();}}
 void requested(GithubState target,uint32_t now){lifecyclePending=true;requestedTarget=target;requestedAt=now;cxlReady=cxlChecked=cxlAuthRejected=false;}
 void github(GithubState state,uint32_t now,uint32_t gen){if(gen==generation&&wifi){
  githubState=state;githubAt=now;if(state==GithubState::Stopped)cxlReady=cxlChecked=cxlAuthRejected=false;
  if(lifecyclePending&&((requestedTarget==GithubState::Available&&(state==GithubState::Starting||state==GithubState::Available))||
    (requestedTarget==GithubState::Stopped&&state==GithubState::Stopped)))lifecyclePending=false;
 }}
 void cxl(bool ready,uint32_t now,uint32_t gen,bool rejected=false){
  if(gen==generation&&wifi){cxlReady=ready;cxlChecked=true;cxlAuthRejected=!ready&&rejected;cxlAt=now;}
 }
 static bool fresh(uint32_t now,uint32_t at){return uint32_t(now-at)<=30000;}
 bool desktopReady(uint32_t now)const{return wifi&&cxlReady&&fresh(now,cxlAt)&&!lifecyclePending;}
 bool canEnter(uint32_t now)const{return desktopReady(now)&&!operationPending;}
 bool canStart(uint32_t now)const{return wifi&&githubState==GithubState::Stopped&&fresh(now,githubAt)&&!operationPending&&!lifecyclePending;}
 bool canStop(uint32_t now)const{return wifi&&githubState==GithubState::Available&&fresh(now,githubAt)&&!operationPending&&!lifecyclePending;}
};
