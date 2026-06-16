// ============================================================================
//  netctl.h — actions the web task requests but that must run on the loop task
// ============================================================================
#pragma once
#include <Arduino.h>

void netRequestWifiConnect(const String& ssid, const String& pass);
void netRequestReboot();
void netRequestWifiReset();   // forget WiFi creds -> reopen setup portal
void netService();            // call once per loop()
