#pragma once
#include "DiagnosticSerial.h"
#include "TouchStudyProtocol.h"
#include "TouchStudyRaw.h"
namespace touchStudy {
TouchStudyProtocol protocol;uint32_t sampledAt=0;
int drawnTrial=-2,drawnPhase=-1,drawnSeconds=-1;bool announced=false;
const char* phaseName(){switch(protocol.phase){
 case StudyPhase::Idle:return "idle";case StudyPhase::WaitTouch:return "wait_touch";
 case StudyPhase::Contact:return "contact";case StudyPhase::Release:return "release";
 case StudyPhase::Complete:return "complete";default:return "cancelled";}}
void event(StudyPhase before){if(before!=protocol.phase)CSOS_LOGF("[TOUCH_STUDY_EVENT] stage=%s trial=%d target=%d force=%s time_ms=%lu\n",phaseName(),protocol.trial,protocol.target(),protocol.normal()?"normal":"light",(unsigned long)millis());}
void draw(uint32_t now){
 const bool end=protocol.phase==StudyPhase::Complete||protocol.phase==StudyPhase::Cancelled;
 const bool idle=protocol.phase==StudyPhase::Idle;
 const int seconds=protocol.phase==StudyPhase::Contact?std::max(0,2-int(uint32_t(now-protocol.since)/1000)):0;
 if(drawnPhase==int(protocol.phase)&&drawnSeconds==seconds&&drawnTrial==protocol.trial)return;
 const bool newTarget=drawnTrial!=protocol.trial||drawnPhase<0;
 if(newTarget||end||idle){tft.fillScreen(UiPalette::Background);}
 // Put instructions opposite the target so lower edges remain visible.
 const int infoY=!end&&!idle&&STUDY_TARGETS[protocol.target()].y>120?0:156;
 tft.setFont(&fonts::Font4);tft.setTextWrap(false);
 tft.fillRect(0,infoY,320,84,UiPalette::Background);
 tft.setTextSize(.65f);tft.setTextColor(UiPalette::Muted,UiPalette::Background);tft.setCursor(8,infoY+2);
 tft.print(end?"STUDIO TERMINATO":idle?"MISURA A RIPOSO":String(protocol.trial+1)+"/"+String(STUDY_TRIAL_COUNT)+"  "+STUDY_TARGETS[protocol.target()].name);
 String action=end?"Attendi il ripristino":idle?"NON TOCCARE IL TFT":protocol.phase==StudyPhase::WaitTouch?(protocol.normal()?"PREMI NORMALE E TIENI":"PREMI LEGGERO E TIENI"):protocol.phase==StudyPhase::Contact?"TIENI FERMO":"RILASCIA ORA";
 tft.setTextSize(.9f);tft.setTextColor(protocol.phase==StudyPhase::Release?UiPalette::Green:UiPalette::Ink,UiPalette::Background);tft.setCursor(8,infoY+24);tft.print(action);
 tft.setTextSize(.65f);tft.setTextColor(UiPalette::Muted,UiPalette::Background);tft.setCursor(8,infoY+56);
 tft.print(end?"Nessun setting cambiato":idle?"Poi compare la prima croce":protocol.phase==StudyPhase::Contact?String("Ancora ")+String(seconds)+" s, poi rilascia":protocol.phase==StudyPhase::Release?"Togli il pennino per proseguire":"Spazio salta; Opt interrompe");
 if(!idle&&!end){const auto point=STUDY_TARGETS[protocol.target()];const auto color=protocol.phase==StudyPhase::Release?UiPalette::Green:UiPalette::Orange;
  tft.drawCircle(point.x,point.y,3,color);tft.drawFastHLine(point.x-4,point.y,9,color);tft.drawFastVLine(point.x,point.y-4,9,color);
 }
 hud.fillScreen(HUD_BASE);hud.setFont(&fonts::Font4);hud.setTextWrap(false);hud.setTextColor(HUD_FG,HUD_BASE);
 hud.setTextSize(.75f);hud.setCursor(8,3);hud.print(end?"STUDIO TERMINATO":idle?"RUMORE A RIPOSO":String("PENNINO ")+String(protocol.trial+1)+"/"+String(STUDY_TRIAL_COUNT));
 hud.setTextSize(1);hud.setCursor(8,33);hud.print(end?"Attendi il PC":"Guarda il TFT");
 hud.setTextSize(.75f);hud.setCursor(8,69);hud.print(end?"Dati registrati":idle?"Non toccare lo schermo":protocol.phase==StudyPhase::Release?"Rilascia il pennino":protocol.normal()?"Pressione normale":"Pressione leggera");
 hud.setCursor(8,100);hud.print(end?"Client da ripristinare":"Opt: interrompi");hud.pushSprite(0,0);
 drawnTrial=protocol.trial;drawnPhase=int(protocol.phase);drawnSeconds=seconds;
}
void service(){
 const uint32_t now=millis();const auto key=uiPoll();const auto before=protocol.phase;
 if(key.opt)protocol.cancel();else if(key.idx>=0&&key.base==' '){CSOS_LOGF("[TOUCH_STUDY_SKIP] trial=%d\n",protocol.trial);protocol.skip(now);}
 protocol.advance(now);event(before);
 const bool end=protocol.phase==StudyPhase::Complete||protocol.phase==StudyPhase::Cancelled;
 if(end&&!announced){CSOS_LOGF("[TOUCH_STUDY_END] complete=%d\n",int(protocol.phase==StudyPhase::Complete));announced=true;}
 draw(now);if(end)return;
 if(!protocol.recording()||uint32_t(now-sampledAt)<20)return;sampledAt=now;
 const char* sampleStage=phaseName();const int sampleTrial=protocol.trial;const int sampleTarget=protocol.phase==StudyPhase::Idle?-1:protocol.target();
 tft.waitDMA();const int irqBefore=digitalRead(TOUCH_IRQ);const uint32_t driverAt=micros();
 const auto driver=csosTouch.getPoint();const uint32_t rawAt=micros();const auto raw=readStudyRaw(SPI,TOUCH_CS);
 const int irqAfter=digitalRead(TOUCH_IRQ);
 const auto beforeSample=protocol.phase;protocol.observe(now,irqBefore==LOW);event(beforeSample);
 const auto pixel=csosCalibration.map({double(raw.x),double(raw.y)});
 // Sequential observations, not simultaneous. XY below threshold is raw research
 // data, not an accepted touch; IRQ before/after is not a force measurement.
 CSOS_LOGF("TOUCHS,%s,%d,%d,%d,%lu,%lu,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%.2f\n",sampleStage,sampleTrial,sampleTarget,sampleTrial%2,(unsigned long)now,(unsigned long)driverAt,(unsigned long)rawAt,irqBefore,irqAfter,driver.y,driver.x,driver.z,raw.x,raw.y,raw.z1,raw.z2,raw.z,pixel.x,pixel.y);
}
}
void beginTouchStudy(){
 uiPrevMask=readKeyMask();touchStudy::protocol.reset(millis());
 CSOS_LOGF("[TOUCH_STUDY] v=3 automatic=1 grid=5x5 targets=%d trials=%d driver_threshold=400 raw_xy=unconditional settings=read_only network=off\n",STUDY_TARGET_COUNT,STUDY_TRIAL_COUNT);
 for(int i=0;i<STUDY_TARGET_COUNT;i++){const auto p=STUDY_TARGETS[i];CSOS_LOGF("[TOUCH_STUDY_TARGET] id=%d x=%d y=%d role=%s\n",i,p.x,p.y,p.role);}
 touchStudy::draw(millis());
}
void serviceTouchStudy(){touchStudy::service();}
