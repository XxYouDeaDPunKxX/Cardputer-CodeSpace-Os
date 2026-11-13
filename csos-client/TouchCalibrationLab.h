#pragma once
#include "DiagnosticSerial.h"
#include "TouchCalibrationData.h"
#ifndef CSOS_PEN_ONLY
#define CSOS_PEN_ONLY 0
#endif
namespace touchLab {
int phase=0,target=0;
bool started=false,review=false,fitFailed=false,saved=false,dirty=true,seenContact=false,tftDirty=true;
uint32_t tick=0,lastDraw=0;
TouchCapture capture;
Point rawPoints[9]={};
Calibration baseline,candidate;
TouchErrors errors[4],oldErrors;
String note;
const char* phaseName(){return phase==0?"PENNINO":phase==1?"VERIFICA":phase==2?"POLLICE":phase==3?"INDICE":"TRACCIA";}
int targetCount(){return phase==1?8:9;}
Point targetPoint(){return phase==1?TOUCH_CHECK_TARGETS[target]:TOUCH_CAL_TARGETS[target];}
void resetCapture(){capture.reset();seenContact=false;}
void draw(){
    dirty=false;lastDraw=millis();
    if(tftDirty){
    tftDirty=false;
    tft.setFont(&fonts::Font4);tft.setTextSize(.75f);tft.setTextColor(0xAD55,C_BG);tft.fillScreen(C_BG);
    if(phase<4&&!review&&!fitFailed){
        Point p=targetPoint();int x=int(p.x),y=int(p.y);
        tft.drawCircle(x,y,4,0x87F0);tft.drawFastHLine(x-7,y,15,0x87F0);tft.drawFastVLine(x,y-7,15,0x87F0);
        const int infoY=p.y<80?180:8;
        tft.setCursor(8,infoY);tft.print(String(phaseName())+" "+String(target+1)+"/"+String(targetCount()));
#if CSOS_PEN_ONLY
        tft.setCursor(8,infoY+28);tft.print(capture.accepted?"Rilascia il pennino":"Tieni fermo sulla croce");
#endif
    }else{
        tft.setCursor(10,40);
        if(fitFailed)tft.print("Calibrazione non valida");
        else if(review){
            tft.print("PENNINO: verifica terminata");tft.setCursor(10,80);
            tft.printf("RMS %.1f px | MAX %.1f",errors[1].rms(),errors[1].maximum);
            tft.setCursor(10,112);tft.printf("Prima: RMS %.1f px",oldErrors.rms());
            tft.setCursor(10,160);tft.print("Invio salva | Opt non salva");
            tft.setCursor(10,193);tft.print("R: ripeti pennino");
        }else{
            tft.print("TRACCIA: pennino libero");tft.setCursor(10,85);tft.print("R: ripeti | Spazio: pulisci");
            tft.setCursor(10,130);tft.print(saved?"Mappa pennino salvata":"Mappa NON salvata");
            #if !CSOS_PEN_ONLY
            tft.setCursor(10,166);tft.printf("Pollice: RMS %.1f px",errors[2].rms());
            tft.setCursor(10,196);tft.printf("Indice: RMS %.1f px",errors[3].rms());
#else
            tft.setCursor(10,166);tft.print("Calibrazione terminata");
#endif
        }
    }
    }
    hud.fillScreen(HUD_BASE);hud.setFont(&fonts::Font4);hud.setTextWrap(false);hud.setTextColor(HUD_FG,HUD_BASE);
    hud.setTextSize(.75f);hud.setCursor(8,3);hud.print(String(phaseName())+(phase<4?" "+String(target+1)+"/"+String(targetCount()):""));
    hud.setTextSize(1);hud.setCursor(8,32);
    hud.print(fitFailed?"R: ricomincia":review?"Verifica errore":phase==4?"Terminata":!started?"Invio: inizia":capture.accepted?"Ora rilascia":"Centra la croce");
    hud.setTextSize(.75f);hud.setCursor(8,69);
    hud.print(review?"Invio salva pennino":phase==4?(saved?"Pennino salvato":"Pennino non salvato"):phase>=2?"Solo dati di ricerca":capture.accepted?"Campione acquisito":"Tieni fermo il pennino");
    hud.setCursor(8,99);hud.print(note.length()?note:phase==2||phase==3?"Spazio: salta punto":"R: ripeti percorso");hud.pushSprite(0,0);
}
void nextPhase(){
 phase++;
#if CSOS_PEN_ONLY
 if(phase==2)phase=4;
#endif
 target=0;started=false;
#if CSOS_PEN_ONLY
 started=phase<2;
#endif
 review=false;note="";resetCapture();dirty=tftDirty=true;
}
void finishPoint(bool skipped=false){
    const Point targetPixel=targetPoint();
    if(skipped)errors[phase].missed++;
    else if(phase==0)rawPoints[target]=capture.raw;
    else{
        Point p=candidate.map(capture.raw);
        const double error=std::hypot(p.x-targetPixel.x,p.y-targetPixel.y);errors[phase].add(error);
        note="Errore "+String(float(error),1)+" px";
        if(phase==1){Point b=baseline.map(capture.raw);oldErrors.add(std::hypot(b.x-targetPixel.x,b.y-targetPixel.y));}
    }
    CSOS_LOGF("[LAB_POINT] phase=%d target=%d skipped=%d raw=%.1f,%.1f target_px=%.0f,%.0f z_min=%d z_max=%d below400=%d samples=%d spread=%d\n",phase,target+1,int(skipped),capture.raw.x,capture.raw.y,targetPixel.x,targetPixel.y,capture.samples?capture.minZ:0,capture.maxZ,capture.below,capture.samples,capture.spread);
    target++;resetCapture();
    if(target==targetCount()){
        if(phase==0)CSOS_LOGLN("[LAB_CAL] points=9 fit_pending=1");
        else CSOS_LOGF("[LAB_PHASE] phase=%d count=%d missed=%d rms=%.2f max=%.2f\n",phase,errors[phase].count,errors[phase].missed,errors[phase].rms(),errors[phase].maximum);
        target=0;
        if(phase==0){
            if(!candidate.fitMany(rawPoints,TOUCH_CAL_TARGETS,9)){fitFailed=true;started=false;}
            else{
                CSOS_LOGF("[LAB_FIT] a=%.12f b=%.12f c=%.12f d=%.12f e=%.12f f=%.12f\n",candidate.a,candidate.b,candidate.c,candidate.d,candidate.e,candidate.f);
                nextPhase();
            }
        }else if(phase==1){review=true;started=false;}
        else nextPhase();
    }
    dirty=tftDirty=true;
}
void begin(){
#if CSOS_PEN_ONLY
    started=true;
    CSOS_LOGLN("[LAB] pen_only=1 automatic=1 fit=9 check=8 pressure=300 spi_hz=1000000 capture=median96");
#endif
    baseline=csosCalibration;candidate=baseline;uiPrevMask=readKeyMask();
    #if !CSOS_PEN_ONLY
    CSOS_LOGLN("[LAB] v=1 stages=pen9,validation8,thumb9,index9,trace save=pen_only network=off");
#endif
    draw();
}
void reset(){phase=target=0;started=review=fitFailed=saved=false;
#if CSOS_PEN_ONLY
started=true;
#endif
candidate=baseline;for(auto &e:errors)e={};oldErrors={};note="";resetCapture();dirty=tftDirty=true;}
void service(){
    serviceHaptic();UiKey key=uiPoll();
    if(key.idx>=0||key.opt){
        if(key.base=='r'||key.base=='R'){reset();}
        else if(review){
            if(key.base==KEY_ENTER){
                saved=saveTouchCalibration(candidate,rotation);
                if(saved){csosCalibration=candidate;CSOS_LOGLN("[LAB_SAVE] pen_only=1 verified=1");nextPhase();}
                else{note="Salvataggio fallito";dirty=true;}
            }else if(key.opt||key.base=='`'){CSOS_LOGLN("[LAB_SAVE] saved=0");nextPhase();}
        }else if(!fitFailed&&!started&&phase<4&&key.base==KEY_ENTER){started=true;resetCapture();dirty=true;}
        else if(started&&(phase==2||phase==3)&&key.base==' '){finishPoint(true);}
        else if(phase==4&&key.base==' '){dirty=tftDirty=true;}
    }
    if(dirty)draw();
    if(millis()-tick<20)return;tick=millis();
    tft.waitDMA();
#if CSOS_PEN_ONLY
    const bool irqBefore=digitalRead(TOUCH_IRQ)==LOW;
    if(irqBefore)csosTouch.isrWake=true;
#endif
    const auto point=csosTouch.getPoint();
#if CSOS_PEN_ONLY
    const bool contact=irqBefore&&digitalRead(TOUCH_IRQ)==LOW;
#endif
    // Extra Z-only observation. It cannot change the operational driver threshold.
    #if CSOS_PEN_ONLY
    SPI.beginTransaction(SPISettings(1000000,MSBFIRST,SPI_MODE0));
#else
    SPI.beginTransaction(SPISettings(2000000,MSBFIRST,SPI_MODE0));
#endif
    digitalWrite(TOUCH_CS,LOW);
    SPI.transfer(0xB1);const int z1=SPI.transfer16(0xC1)>>3;
    const int z2=SPI.transfer16(0xD0)>>3;SPI.transfer16(0);digitalWrite(TOUCH_CS,HIGH);SPI.endTransaction();
    const int rawZ=std::max(0,z1+4095-z2);
    if(phase==4){
        if(point.z>=(CSOS_PEN_ONLY?300:400)){Point p=candidate.map({double(point.y),double(point.x)});if(p.x>=0&&p.x<320&&p.y>=0&&p.y<240)tft.fillCircle(int(p.x),int(p.y),1,0x87F0);}
        return;
    }
    if(!started||review||fitFailed)return;
    if(rawZ>75||seenContact){
        CSOS_LOGF("LABS,%d,%d,%lu,%d,%d,%d,%d,%d,%d\n",phase,target+1,(unsigned long)tick,point.y,point.x,point.z,z1,z2,rawZ);
        // Raw Z below the driver's threshold is research data, not a new touch.
        if(point.z>=(CSOS_PEN_ONLY?300:400))seenContact=true;
    }
    #if CSOS_PEN_ONLY
    const int releasedBefore=capture.low;
    if(capture.sample(point.y,point.x,contact?point.z:0,rawZ,&baseline,300,true)){haptic(35);dirty=tftDirty=true;}
    capture.low=contact?0:releasedBefore+1;
#else
    if(capture.sample(point.y,point.x,point.z,rawZ,&baseline)){haptic(35);dirty=true;}
#endif
    if(capture.released()&&seenContact){
        if(capture.accepted)finishPoint();
        else{const String retry="Tocco instabile";if(note!=retry){note=retry;dirty=true;}CSOS_LOGF("[LAB_RETRY] phase=%d target=%d samples=%d below400=%d\n",phase,target+1,capture.samples,capture.below);resetCapture();}
    }
}
}
void beginTouchCalibrationLab(){touchLab::begin();}
void serviceTouchCalibrationLab(){touchLab::service();}
