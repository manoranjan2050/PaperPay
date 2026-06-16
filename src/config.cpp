// ============================================================================
//  config.cpp — load/save ShopConfig from NVS (Preferences)
// ============================================================================
#include "config.h"
#include <Preferences.h>

ShopConfig CFG;
static Preferences prefs;

static const char* NS = "paperpay";

void configLoad() {
  prefs.begin(NS, true);  // read-only
  CFG.vpa        = prefs.getString("vpa", "");
  CFG.payee      = prefs.getString("payee", "My Shop");
  CFG.shopName   = prefs.getString("shop", "My Shop");
  CFG.currency   = prefs.getString("cur", "Rs");
  CFG.gstPercent = prefs.getFloat("gst", 0.0f);
  CFG.tzOffset   = prefs.getLong("tz", 19800); // IST default
  CFG.tgToken    = prefs.getString("tgtok", "");
  CFG.tgChat     = prefs.getString("tgchat", "");
  CFG.tgEnable   = prefs.getBool("tgen", false);
  CFG.tgAutoPaid = prefs.getBool("tgauto", true);
  prefs.end();
}

void configSave() {
  prefs.begin(NS, false); // read-write
  prefs.putString("vpa", CFG.vpa);
  prefs.putString("payee", CFG.payee);
  prefs.putString("shop", CFG.shopName);
  prefs.putString("cur", CFG.currency);
  prefs.putFloat("gst", CFG.gstPercent);
  prefs.putLong("tz", CFG.tzOffset);
  prefs.putString("tgtok", CFG.tgToken);
  prefs.putString("tgchat", CFG.tgChat);
  prefs.putBool("tgen", CFG.tgEnable);
  prefs.putBool("tgauto", CFG.tgAutoPaid);
  prefs.end();
}
