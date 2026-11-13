#pragma once
#include <cstdint>
enum class StudyPhase:uint8_t {Idle,WaitTouch,Contact,Release,Complete,Cancelled};
struct StudyTarget {int x,y;const char* name;bool fit;const char* role;};
// Full usable panel, 4 px inset. Centre is repeated to expose temporal drift.
constexpr StudyTarget STUDY_TARGETS[]={
 {160,120,"Centro iniziale",true,"fit"},
 {4,4,"Riga 1 Colonna 1",true,"fit"},
 {82,4,"Riga 1 Colonna 2",false,"validation"},
 {160,4,"Riga 1 Colonna 3",true,"fit"},
 {237,4,"Riga 1 Colonna 4",false,"validation"},
 {315,4,"Riga 1 Colonna 5",true,"fit"},
 {315,62,"Riga 2 Colonna 5",false,"validation"},
 {237,62,"Riga 2 Colonna 4",false,"validation"},
 {160,62,"Riga 2 Colonna 3",false,"validation"},
 {82,62,"Riga 2 Colonna 2",false,"validation"},
 {4,62,"Riga 2 Colonna 1",false,"validation"},
 {4,120,"Riga 3 Colonna 1",true,"fit"},
 {82,120,"Riga 3 Colonna 2",false,"validation"},
 {237,120,"Riga 3 Colonna 4",false,"validation"},
 {315,120,"Riga 3 Colonna 5",true,"fit"},
 {315,177,"Riga 4 Colonna 5",false,"validation"},
 {237,177,"Riga 4 Colonna 4",false,"validation"},
 {160,177,"Riga 4 Colonna 3",false,"validation"},
 {82,177,"Riga 4 Colonna 2",false,"validation"},
 {4,177,"Riga 4 Colonna 1",false,"validation"},
 {4,235,"Riga 5 Colonna 1",true,"fit"},
 {82,235,"Riga 5 Colonna 2",false,"validation"},
 {160,235,"Riga 5 Colonna 3",true,"fit"},
 {237,235,"Riga 5 Colonna 4",false,"validation"},
 {315,235,"Riga 5 Colonna 5",true,"fit"},
 {160,120,"Centro finale",false,"repeat"}
};
constexpr int STUDY_TARGET_COUNT=sizeof(STUDY_TARGETS)/sizeof(STUDY_TARGETS[0]);
constexpr int STUDY_TRIAL_COUNT=2*STUDY_TARGET_COUNT;
struct TouchStudyProtocol {
 StudyPhase phase=StudyPhase::Idle;uint32_t since=0;int trial=0,downSamples=0,upSamples=0;bool skipped[STUDY_TRIAL_COUNT]={};
 int target()const{return trial<STUDY_TRIAL_COUNT?trial/2:STUDY_TARGET_COUNT-1;}bool normal()const{return trial%2;}
 void reset(uint32_t now=0){*this=TouchStudyProtocol{};since=now;}
 void cancel(){phase=StudyPhase::Cancelled;}
 void skip(uint32_t now){if(phase==StudyPhase::WaitTouch||phase==StudyPhase::Contact){skipped[trial]=true;phase=StudyPhase::Release;since=now;upSamples=0;}}
 void advance(uint32_t now){
  const auto elapsed=uint32_t(now-since);
  if(phase==StudyPhase::Idle&&elapsed>=3000){phase=trial==STUDY_TRIAL_COUNT?StudyPhase::Complete:StudyPhase::WaitTouch;since=now;}
  else if(phase==StudyPhase::Contact&&elapsed>=2000){phase=StudyPhase::Release;since=now;upSamples=0;}
 }
 // Called only at the 20ms sample cadence. IRQ is observed outside SPI activity.
 void observe(uint32_t now,bool contact){
  if(phase==StudyPhase::WaitTouch){downSamples=contact?downSamples+1:0;if(downSamples>=2){phase=StudyPhase::Contact;since=now;downSamples=0;}}
  else if(phase==StudyPhase::Release){upSamples=contact?0:upSamples+1;if(upSamples>=5){trial++;phase=trial==STUDY_TRIAL_COUNT?StudyPhase::Idle:StudyPhase::WaitTouch;since=now;upSamples=0;}}
 }
 bool recording()const{return phase==StudyPhase::Idle||phase==StudyPhase::WaitTouch||phase==StudyPhase::Contact||phase==StudyPhase::Release;}
};
