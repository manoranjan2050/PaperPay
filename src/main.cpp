// ============================================================================
//  PaperPay — E-Paper UPI Counter
//  ESP32-S3-N16R8 + Waveshare 2.9" e-paper
//
//  Flow:
//    1. First boot -> captive portal "PaperPay-Setup" to join WiFi + enter
//       UPI ID / payee / shop name.
//    2. Hosts a mobile-friendly dashboard at http://paperpay.local
//    3. Bill an amount -> a UPI QR appears on the e-paper + the dashboard.
//    4. Customer scans with any UPI app and pays. Mark Paid to log it.
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include <time.h>

#include "config.h"
#include "display.h"
#include "store.h"
#include "web.h"

static void startMdns() {
  if (MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mdns] http://%s.local\n", MDNS_HOST);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== PaperPay booting ===");

  if (!LittleFS.begin(true)) Serial.println("[fs] LittleFS mount failed");
  configLoad();
  storeInit();

  displayInit();
  displayBoot("Starting WiFi setup...", "Join AP: " SETUP_AP_NAME);
  displayService();

  // ---- WiFi + onboarding ----------------------------------------------------
  WiFiManager wm;
  WiFiManagerParameter pVpa  ("vpa",   "UPI ID (VPA)",  CFG.vpa.c_str(),      64);
  WiFiManagerParameter pPayee("payee", "Payee name",    CFG.payee.c_str(),    48);
  WiFiManagerParameter pShop ("shop",  "Shop name",     CFG.shopName.c_str(), 48);
  wm.addParameter(&pVpa);
  wm.addParameter(&pPayee);
  wm.addParameter(&pShop);
  wm.setConfigPortalTimeout(0); // stay in portal until configured

  bool ok = wm.autoConnect(SETUP_AP_NAME);
  if (ok) {
    // persist anything entered in the portal
    if (strlen(pVpa.getValue()))   CFG.vpa      = pVpa.getValue();
    if (strlen(pPayee.getValue())) CFG.payee    = pPayee.getValue();
    if (strlen(pShop.getValue()))  CFG.shopName = pShop.getValue();
    configSave();
    Serial.printf("[wifi] connected: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[wifi] not connected");
  }

  configTime(CFG.tzOffset, 0, "pool.ntp.org", "time.google.com");
  startMdns();
  webBegin();

  displayQueueIdle(CFG.shopName, MDNS_HOST, WiFi.localIP().toString());
}

void loop() {
  displayService();          // render any queued e-paper job (SPI on loop task)

  // auto-reconnect if WiFi drops
  static uint32_t lastChk = 0;
  if (millis() - lastChk > 10000) {
    lastChk = millis();
    if (!WiFi.isConnected()) WiFi.reconnect();
  }
  delay(50);
}
