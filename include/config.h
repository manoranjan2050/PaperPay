// ============================================================================
//  config.h — pin map + persisted shop settings (NVS / Preferences)
// ============================================================================
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  E-paper wiring (ESP32-S3 <-> Waveshare 2.9")
//  These are safe GPIOs on the N16R8 (avoid 0/3/19/20/35-37/45/46).
//  Change here if you wired it differently.
// ---------------------------------------------------------------------------
#define PIN_EPD_SCK   12   // CLK  (yellow)
#define PIN_EPD_MOSI  11   // DIN  (blue)
#define PIN_EPD_CS    10   // CS   (orange)
#define PIN_EPD_DC     9   // DC   (green)
#define PIN_EPD_RST    8   // RST  (white)
#define PIN_EPD_BUSY   7   // BUSY (purple)
#define PIN_EPD_MISO  -1   // not used by e-paper

// mDNS hostname -> http://paperpay.local
#define MDNS_HOST     "paperpay"
// Wi-Fi setup access-point name (captive portal on first boot)
#define SETUP_AP_NAME "PaperPay-Setup"

// ---------------------------------------------------------------------------
//  Shop configuration, persisted in NVS. Editable from the web Settings page
//  and seeded by the captive portal on first boot.
// ---------------------------------------------------------------------------
struct ShopConfig {
  String vpa;        // UPI ID / VPA, e.g. "shop@okhdfcbank"
  String payee;      // payee name shown in the UPI app
  String shopName;   // shown on the e-paper + dashboard header
  String currency;   // symbol, default "Rs"
  float  gstPercent; // optional default GST %, 0 = off
  long   tzOffset;   // seconds east of UTC for timestamps (IST = 19800)
};

extern ShopConfig CFG;

void   configLoad();   // read from NVS into CFG (with defaults)
void   configSave();   // persist CFG to NVS
