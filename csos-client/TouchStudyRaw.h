#pragma once
#include <algorithm>
#include <cstdlib>
// Diagnostic only: same SPI conversion order and best-pair average as the
// installed XPT2046 library, but XY conversions are never gated by Z>=400.
struct StudyRaw {int x,y,z1,z2,z;};
inline int studyPair(int a,int b,int c){
 const int ab=std::abs(a-b),ac=std::abs(a-c),bc=std::abs(b-c);
 return ab<=ac&&ab<=bc?(a+b)/2:ac<=ab&&ac<=bc?(a+c)/2:(b+c)/2;
}
template<class Bus> StudyRaw readStudyRaw(Bus& bus,int chipSelect){
 bus.beginTransaction(SPISettings(2000000,MSBFIRST,SPI_MODE0));digitalWrite(chipSelect,LOW);
 bus.transfer(0xB1);const int z1=bus.transfer16(0xC1)>>3,z2=bus.transfer16(0x91)>>3;
 bus.transfer16(0x91); // discard first X conversion, as the installed driver does
 int data[6];data[0]=bus.transfer16(0xD1)>>3;data[1]=bus.transfer16(0x91)>>3;
 data[2]=bus.transfer16(0xD1)>>3;data[3]=bus.transfer16(0x91)>>3;
 data[4]=bus.transfer16(0xD0)>>3;data[5]=bus.transfer16(0)>>3;
 digitalWrite(chipSelect,HIGH);bus.endTransaction();
 // Match the measured affine basis used by serviceCsosTouch (point.y,point.x).
 return {studyPair(data[1],data[3],data[5]),studyPair(data[0],data[2],data[4]),z1,z2,std::max(0,z1+4095-z2)};
}
