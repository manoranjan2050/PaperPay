// ============================================================================
//  config.h — pin map + persisted shop settings (NVS / Preferences)
// ============================================================================
#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
//  M5Paper has its own integrated e-paper (IT8951) + touch (GT911) + RTC,
//  driven by the M5EPD library — no discrete wiring pins needed here.
// ---------------------------------------------------------------------------

// mDNS hostname -> http://m5paperpay.local
#define MDNS_HOST     "m5paperpay"
// Wi-Fi setup access-point name (captive portal on first boot)
#define SETUP_AP_NAME "M5PaperPay-Setup"

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

  // --- Telegram integration ---
  String tgToken;    // bot token from @BotFather
  String tgChat;     // chat id to send to / accept commands from
  bool   tgEnable;   // master on/off
  bool   tgAutoPaid; // auto-mark a pending bill paid when a matching amount
                     // appears in a Telegram message (forwarded bank/UPI SMS)
};

extern ShopConfig CFG;

void   configLoad();   // read from NVS into CFG (with defaults)
void   configSave();   // persist CFG to NVS
