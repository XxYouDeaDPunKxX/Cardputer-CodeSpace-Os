#pragma once
#include "calibration.hpp"
#include <Preferences.h>
#include <cstddef>
#include <cstdint>
struct TouchCalibrationRecord {
    uint32_t magic=0x43545031;
    uint16_t version=1,width=320,height=240;
    uint8_t rotation=0,reserved[5]={};
    double coefficients[6]={};
    uint32_t checksum=0;
};
inline uint32_t touchCalibrationChecksum(const TouchCalibrationRecord& record){
    uint32_t value=2166136261u;
    const auto* bytes=reinterpret_cast<const uint8_t*>(&record);
    for(size_t i=0;i<offsetof(TouchCalibrationRecord,checksum);i++)value=(value^bytes[i])*16777619u;
    return value;
}
inline bool validTouchCalibrationRecord(const TouchCalibrationRecord& r,uint8_t rotation){
    if(r.magic!=0x43545031||r.version!=1||r.width!=320||r.height!=240||r.rotation!=rotation||r.checksum!=touchCalibrationChecksum(r))return false;
    for(double value:r.coefficients)if(!std::isfinite(value))return false;
    return std::fabs(r.coefficients[0]*r.coefficients[4]-r.coefficients[1]*r.coefficients[3])>1e-7;
}
inline bool loadTouchCalibration(Calibration& calibration,uint8_t rotation){
    Preferences store;TouchCalibrationRecord record;
    if(!store.begin("csostouch",true))return false;
    const bool read=store.getBytesLength("pen")==sizeof(record)&&store.getBytes("pen",&record,sizeof(record))==sizeof(record);
    store.end();if(!read||!validTouchCalibrationRecord(record,rotation))return false;
    const double* v=record.coefficients;
    calibration.a=v[0];calibration.b=v[1];calibration.c=v[2];
    calibration.d=v[3];calibration.e=v[4];calibration.f=v[5];calibration.valid=true;
    return true;
}
inline bool saveTouchCalibration(const Calibration& calibration,uint8_t rotation){
    if(!calibration.valid)return false;
    TouchCalibrationRecord record;record.rotation=rotation;
    const double values[]={calibration.a,calibration.b,calibration.c,calibration.d,calibration.e,calibration.f};
    for(int i=0;i<6;i++)record.coefficients[i]=values[i];
    record.checksum=touchCalibrationChecksum(record);
    if(!validTouchCalibrationRecord(record,rotation))return false;
    Preferences store;if(!store.begin("csostouch",false))return false;
    const bool written=store.putBytes("pen",&record,sizeof(record))==sizeof(record);store.end();
    Calibration verify;return written&&loadTouchCalibration(verify,rotation)&&verify.a==calibration.a&&verify.f==calibration.f;
}
