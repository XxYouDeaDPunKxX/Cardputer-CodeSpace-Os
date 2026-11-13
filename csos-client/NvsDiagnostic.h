#pragma once
#include <nvs.h>
#include "FaultDiagnostics.h"

inline bool readNvsRecord(const char* name,const char* key,void* data,size_t& size,uint32_t now){
 nvs_handle_t handle;
 auto error=nvs_open(name,NVS_READONLY,&handle);
 if(error!=ESP_OK){
  if(error!=ESP_ERR_NVS_NOT_FOUND)recordFault(FaultArea::Nvs,FaultCode::NvsOpen,error,now);
  return false;
 }
 error=nvs_get_blob(handle,key,data,&size);
 nvs_close(handle);
 if(error!=ESP_OK&&error!=ESP_ERR_NVS_NOT_FOUND)
  recordFault(FaultArea::Nvs,FaultCode::NvsRead,error,now);
 return error==ESP_OK;
}
