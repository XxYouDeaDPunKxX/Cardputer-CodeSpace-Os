// SPDX-License-Identifier: MIT
// Cardputer ADV matrix mapping follows M5Stack's MIT TCA8418KeyboardReader
// (Copyright 2025 M5Stack Technology CO LTD). Controller access reuses its driver.
#pragma once
#include "DiagnosticSerial.h"
#include <algorithm>
#include <cstdint>
#ifndef CSOS_KEYBOARD_HOST_TEST
#include <utility/Keyboard/KeyboardReader/KeyboardReader.h>
#include <utility/Adafruit_TCA8418/Adafruit_TCA8418.h>
#include <Arduino.h>
#endif
namespace CsosKeyboard {
class ReliableReader : public KeyboardReader {
 Adafruit_TCA8418 device_;
 unsigned failures_=0;
 void resetHeld(const char* reason){
  _key_list.clear();++recoveries;
#ifndef CSOS_KEYBOARD_HOST_TEST
  CSOS_LOGF("[INPUT] keyboard_recovery=%s count=%u\n",reason,recoveries);
#else
  (void)reason;
#endif
 }
 bool read(uint8_t reg,uint8_t& value){
  if(device_.readRegister(reg,&value,1))return true;
  ++ioErrors;if(++failures_==3)resetHeld("i2c");return false;
 }
public:
 unsigned recoveries=0,ioErrors=0;
 void begin() override {
  _key_list.clear();_key_list.reserve(8);
  device_.begin();device_.matrix(7,8);device_.flush();
  uint8_t cfg=0;
  if(read(1,cfg))device_.writeRegister8(1,uint8_t((cfg&~0x12)|0x29));
  // TI overflow erratum requires both overflow-mode and interrupt bits.
  // Read availability directly: no edge/ISR-flag race can suppress a release.
 }
 void update() override {
  uint8_t status=0,count=0;
  if(!read(2,status)||!read(3,count))return;
  if((status&8)||count>10){
   resetHeld("fifo_overflow");
   for(unsigned i=0;i<10;++i){
    if(!read(3,count)||!count)break;
    uint8_t discard;if(!read(4,discard))break;
   }
   device_.writeRegister8(2,9);return;
  }
  if(!count){failures_=0;if(status&1)device_.writeRegister8(2,1);return;}
  uint8_t event=0;if(!read(4,event))return;failures_=0;
  device_.writeRegister8(2,1);
  unsigned key=event&127;
  if(!key)return;  // Empty event is not a release of an underflowed key index.
  --key;unsigned row=key/10,col=key%10;
  if(row>=7||col>=8)return;  // Ignore unused matrix columns and GPIO events.
  Point2D_t point;point.x=int(row*2+(col>3));point.y=int((col+4)%4);
  auto found=std::find(_key_list.begin(),_key_list.end(),point);
  if(event&128){if(found==_key_list.end())_key_list.push_back(point);}
  else if(found!=_key_list.end())_key_list.erase(found);
  // Exactly one FIFO event per update: callers observe every press/release.
 }
};
}
