#pragma once
#include <cstdint>
struct DeviceStatusData {
 int battery=-1,cpu=-1,rssi=-127;
 bool wifi=false;
 uint32_t freeHeap=0;
 uint32_t heapMinimum=0,heapLargest=0;
 uint32_t uptimeSeconds=0,receivedKiBs=0;
 int wifiChannel=0,resetReason=-1;
 uint32_t wifiAddress=0;
 bool streamSample=false,chipTemperatureValid=false;
 int chipTemperature=0;
};
