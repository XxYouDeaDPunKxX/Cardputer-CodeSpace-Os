#pragma once
#include "DiagnosticSerial.h"
#include <nvs.h>
#include <cstdint>
#include <cstring>
#include "FaultDiagnostics.h"

inline void traceDeviceNvs(const char* stage,esp_err_t error){
 if(error!=ESP_OK&&!(error==ESP_ERR_NVS_NOT_FOUND&&!strncmp(stage,"read",4))){
  FaultCode code=FaultCode::NvsRead;
  if(strstr(stage,"open"))code=FaultCode::NvsOpen;
  else if(!strcmp(stage,"write_blob"))code=FaultCode::NvsWrite;
  else if(!strcmp(stage,"commit"))code=FaultCode::NvsCommit;
  recordFault(FaultArea::Nvs,code,error,diagnosticTime());
 }
#ifdef ARDUINO
 CSOS_LOGF("[CSOS] nvs_stage=%s error=%s\n",stage,esp_err_to_name(error));
#else
 (void)stage;(void)error;
#endif
}

// One small, versioned NVS record. No credentials, transient modes or calibration.
struct DeviceSettings {
 uint8_t version=1, brightness=10, externalBrightness=10, quality=1, fpsCap=4;
 bool valid()const{return version==1&&brightness>=1&&brightness<=10&&externalBrightness>=1&&externalBrightness<=10&&quality<=2&&fpsCap<=4;}
};
inline esp_err_t loadDeviceSettings(DeviceSettings& out){
 nvs_handle_t handle;
 auto err=nvs_open("csosdevice",NVS_READONLY,&handle);traceDeviceNvs("read_open",err);
 if(err!=ESP_OK)return err;
 DeviceSettings draft;size_t size=sizeof(draft);
 err=nvs_get_blob(handle,"settings",&draft,&size);traceDeviceNvs("read_blob",err);nvs_close(handle);
 if(err!=ESP_OK)return err;
 if(size!=sizeof(draft)||!draft.valid()){
  recordFault(FaultArea::Nvs,FaultCode::NvsRead,ESP_FAIL,diagnosticTime());return ESP_FAIL;
 }
 out=draft;return ESP_OK;
}
inline esp_err_t saveDeviceSettings(const DeviceSettings& value){
 if(!value.valid()){
  recordFault(FaultArea::Nvs,FaultCode::NvsVerify,ESP_FAIL,diagnosticTime());return ESP_FAIL;
 }
 DeviceSettings previous;
 auto err=loadDeviceSettings(previous);
 if(err==ESP_OK&&!memcmp(&previous,&value,sizeof(value)))return ESP_OK;
 // Unknown/corrupt records and storage errors must not be silently overwritten.
 if(err!=ESP_OK&&err!=ESP_ERR_NVS_NOT_FOUND)return err;
 nvs_handle_t handle;err=nvs_open("csosdevice",NVS_READWRITE,&handle);traceDeviceNvs("write_open",err);
 if(err!=ESP_OK)return err;
 err=nvs_set_blob(handle,"settings",&value,sizeof(value));traceDeviceNvs("write_blob",err);
 if(err==ESP_OK){err=nvs_commit(handle);traceDeviceNvs("commit",err);}
 nvs_close(handle);
 if(err!=ESP_OK)return err;
 DeviceSettings verified;err=loadDeviceSettings(verified);
 if(err!=ESP_OK)return err;
 if(memcmp(&verified,&value,sizeof(value))){
  recordFault(FaultArea::Nvs,FaultCode::NvsVerify,ESP_FAIL,diagnosticTime());return ESP_FAIL;
 }
 return ESP_OK;
}
