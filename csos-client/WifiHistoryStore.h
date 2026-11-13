#pragma once
#include <Arduino.h>
#include "WifiProfiles.h"
extern WifiProfiles knownWifi;
bool loadKnownWifi(String& ssid,String& password);
bool rememberKnownWifi(const String& ssid,const String& password);
bool forgetKnownWifi(int index);
