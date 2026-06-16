// ============================================================================
//  M5PaperPay — main
//  M5Stack M5Paper (4.7" e-ink + touch + RTC). Reuses PaperPay's web/qrpay/
//  store/telegram logic; display + touch are M5Paper-specific (display_m5.cpp).
// ============================================================================
#include <M5Unified.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include <time.h>

#include "config.h"
#include "display.h"
#include "store.h"
#include "web.h"
#include "telegram.h"
#include "netctl.h"

#if defined(__has_include)
#  if __has_include("wifi_secrets.h")
#    include "wifi_secrets.h"
#  endif
#endif

static WiFiManager*          wm    = nullptr;
static WiFiManagerParameter* pVpa  = nullptr;
static WiFiManagerParameter* pPayee= nullptr;
static WiFiManagerParameter* pShop = nullptr;
static bool servicesUp = false;

// ---- deferred network actions (web task requests, run on loop task) --------
static SemaphoreHandle_t netMtx;
static volatile bool reqReboot = false, reqReset = false, reqConnect = false;
static String reqSsid, reqPass;
void netRequestWifiConnect(const String& s, const String& p) {
  xSemaphoreTake(netMtx, portMAX_DELAY); reqSsid = s; reqPass = p; reqConnect = true; xSemaphoreGive(netMtx);
}
void netRequestReboot()    { reqReboot = true; }
void netRequestWifiReset() { reqReset  = true; }

static void onSaveParams() {
  if (strlen(pVpa->getValue()))   CFG.vpa      = pVpa->getValue();
  if (strlen(pPayee->getValue())) CFG.payee    = pPayee->getValue();
  if (strlen(pShop->getValue()))  CFG.shopName = pShop->getValue();
  configSave();
}

static void onConnected() {
  servicesUp = true;
  WiFi.setSleep(false);
  Serial.printf("[wifi] CONNECTED ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  configTime(CFG.tzOffset, 0, "pool.ntp.org", "time.google.com");
  if (MDNS.begin(MDNS_HOST)) MDNS.addService("http", "tcp", 80);
  webBegin();
  displayQueueIdle(CFG.shopName, MDNS_HOST, WiFi.localIP().toString());
  tgNotify("M5PaperPay online at http://" + WiFi.localIP().toString());
}

void netService() {
  if (reqReboot) { delay(200); ESP.restart(); }
  if (reqReset)  { if (wm) wm->resetSettings(); delay(200); ESP.restart(); }
  if (reqConnect) {
    String ssid, pass;
    xSemaphoreTake(netMtx, portMAX_DELAY); ssid = reqSsid; pass = reqPass; reqConnect = false; xSemaphoreGive(netMtx);
    WiFi.persistent(true); WiFi.disconnect(); delay(100);
    WiFi.begin(ssid.c_str(), pass.c_str());
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);                   // power, e-ink, touch, RTC, serial
  Serial.begin(115200);
  Serial.println("\n=== M5PaperPay booting ===");

  if (!LittleFS.begin(true)) Serial.println("[fs] LittleFS mount FAILED");
  configLoad();
  storeInit();
  netMtx = xSemaphoreCreateMutex();
  tgInit();

  displayInit();
  displayBoot("Connecting WiFi...", "AP: " SETUP_AP_NAME);
  displayService();

  WiFi.persistent(true);
  WiFi.setSleep(false);
#ifdef WIFI_SSID
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 16 && WiFi.status() != WL_CONNECTED; i++) delay(500);
#endif

  wm = new WiFiManager();
  wm->setConnectTimeout(8);
  pVpa   = new WiFiManagerParameter("vpa",   "UPI ID (VPA)", CFG.vpa.c_str(),      64);
  pPayee = new WiFiManagerParameter("payee", "Payee name",   CFG.payee.c_str(),    48);
  pShop  = new WiFiManagerParameter("shop",  "Shop name",    CFG.shopName.c_str(), 48);
  wm->addParameter(pVpa); wm->addParameter(pPayee); wm->addParameter(pShop);
  wm->setSaveParamsCallback(onSaveParams);
  wm->setConfigPortalBlocking(false);
  wm->setConfigPortalTimeout(0);
  wm->setHostname(MDNS_HOST);

  bool connected = wm->autoConnect(SETUP_AP_NAME);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  if (connected) onConnected();
}

void loop() {
  M5.update();
  wm->process();
  displayService();
  netService();
  if (!servicesUp && WiFi.status() == WL_CONNECTED) onConnected();
  delay(20);
}
