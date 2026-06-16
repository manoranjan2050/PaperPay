// ============================================================================
//  PaperPay — E-Paper UPI Counter
//  ESP32-S3-N16R8 + Waveshare 2.9" e-paper
//
//  Boot order is deliberate: WiFi (captive portal) comes up FIRST and runs
//  non-blocking, so a missing/miswired e-paper can never stop the AP from
//  appearing. The display is rendered from loop() and is allowed to fail.
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
#include "telegram.h"
#include "netctl.h"

// optional local WiFi credentials (git-ignored include/wifi_secrets.h)
#if defined(__has_include)
#  if __has_include("wifi_secrets.h")
#    include "wifi_secrets.h"
#  endif
#endif

// ---- deferred network actions (requested by web task, run on loop task) -----
static SemaphoreHandle_t netMtx;
static volatile bool reqReboot = false, reqReset = false, reqConnect = false;
static String reqSsid, reqPass;

void netRequestWifiConnect(const String& ssid, const String& pass) {
  xSemaphoreTake(netMtx, portMAX_DELAY);
  reqSsid = ssid; reqPass = pass; reqConnect = true;
  xSemaphoreGive(netMtx);
}
void netRequestReboot()    { reqReboot = true; }
void netRequestWifiReset() { reqReset  = true; }

static WiFiManager*          wm    = nullptr;
static WiFiManagerParameter* pVpa  = nullptr;
static WiFiManagerParameter* pPayee= nullptr;
static WiFiManagerParameter* pShop = nullptr;

static bool servicesUp = false;

// portal "Save" pressed -> persist the UPI fields the user typed
static void onSaveParams() {
  if (strlen(pVpa->getValue()))   CFG.vpa      = pVpa->getValue();
  if (strlen(pPayee->getValue())) CFG.payee    = pPayee->getValue();
  if (strlen(pShop->getValue()))  CFG.shopName = pShop->getValue();
  configSave();
  Serial.printf("[cfg] saved from portal: vpa=%s shop=%s\n",
                CFG.vpa.c_str(), CFG.shopName.c_str());
}

// runs once, the moment we first reach STA-connected
static void onConnected() {
  servicesUp = true;
  WiFi.setSleep(false);   // CRITICAL: keep radio awake or the web server is
                          // unreachable/slow from other devices (modem sleep
                          // drops incoming ARP/TCP between DTIM beacons).
  Serial.printf("[wifi] CONNECTED  ip=%s  rssi=%d\n",
                WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  configTime(CFG.tzOffset, 0, "pool.ntp.org", "time.google.com");
  if (MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mdns] http://%s.local\n", MDNS_HOST);
  }
  webBegin();
  displayQueueIdle(CFG.shopName, MDNS_HOST, WiFi.localIP().toString());
  tgNotify("PaperPay online at http://" + WiFi.localIP().toString());
}

// deferred network actions, executed safely on the loop task
void netService() {
  if (reqReboot) { delay(200); ESP.restart(); }
  if (reqReset)  { if (wm) wm->resetSettings(); delay(200); ESP.restart(); }
  if (reqConnect) {
    String ssid, pass;
    xSemaphoreTake(netMtx, portMAX_DELAY);
    ssid = reqSsid; pass = reqPass; reqConnect = false;
    xSemaphoreGive(netMtx);
    Serial.printf("[wifi] switching to SSID '%s'\n", ssid.c_str());
    WiFi.persistent(true);
    WiFi.disconnect();
    delay(100);
    WiFi.begin(ssid.c_str(), pass.c_str());   // creds persist; auto-reconnect handles failure
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n\n=== PaperPay booting ===");

  if (!LittleFS.begin(true)) Serial.println("[fs] LittleFS mount FAILED");
  else                       Serial.println("[fs] LittleFS ok");
  configLoad();
  storeInit();
  netMtx = xSemaphoreCreateMutex();
  tgInit();                                        // Telegram task (idle until configured)

  displayInit();                                   // pins/SPI only, no refresh
  displayBoot("WiFi setup:", "Join AP " SETUP_AP_NAME);   // queued, drawn in loop()

  // ---- WiFi: non-blocking captive portal -----------------------------------
  WiFi.persistent(true);
  WiFi.setSleep(false);   // keep radio awake (reachability) for the whole session

#ifdef WIFI_SSID
  // hard-coded network (from include/wifi_secrets.h) — connect directly on boot.
  // Keeps retrying in the background; if creds are wrong/AP weak, the portal
  // below still comes up as a fallback after a short wait.
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  Serial.print("[wifi] connecting to " WIFI_SSID " ");
  for (int i = 0; i < 16 && WiFi.status() != WL_CONNECTED; i++) { delay(500); Serial.print('.'); }
  Serial.printf(" %s\n", WiFi.status() == WL_CONNECTED ? "OK" : "(failed - opening setup portal)");
#endif

  wm     = new WiFiManager();
  wm->setConnectTimeout(8);             // don't block boot if creds are wrong
  pVpa   = new WiFiManagerParameter("vpa",   "UPI ID (VPA)", CFG.vpa.c_str(),      64);
  pPayee = new WiFiManagerParameter("payee", "Payee name",   CFG.payee.c_str(),    48);
  pShop  = new WiFiManagerParameter("shop",  "Shop name",    CFG.shopName.c_str(), 48);
  wm->addParameter(pVpa);
  wm->addParameter(pPayee);
  wm->addParameter(pShop);
  wm->setSaveParamsCallback(onSaveParams);
  wm->setConfigPortalBlocking(false);   // <-- AP runs from loop(), never blocks
  wm->setConfigPortalTimeout(0);        // keep portal up until configured
  wm->setHostname(MDNS_HOST);
  wm->setWiFiAPChannel(6);              // pin to a common clean 2.4 GHz channel

  bool connected = wm->autoConnect(SETUP_AP_NAME);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // max TX so the AP is easy to find
  Serial.printf("[wifi] autoConnect=%d\n", connected);

  if (connected) {
    onConnected();
  } else {
    // make sure the soft-AP is REALLY broadcasting (core 3.x non-blocking quirk)
    delay(150);
    Serial.printf("[wifi] mode=%d  portal=%d  apIP=%s  apSSID=%s\n",
                  WiFi.getMode(), wm->getConfigPortalActive(),
                  WiFi.softAPIP().toString().c_str(), WiFi.softAPSSID().c_str());
    if (WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
      Serial.println("[wifi] soft-AP down -> forcing startConfigPortal()");
      WiFi.mode(WIFI_AP_STA);
      wm->startConfigPortal(SETUP_AP_NAME);
      Serial.printf("[wifi] after force: apIP=%s  apSSID=%s\n",
                    WiFi.softAPIP().toString().c_str(), WiFi.softAPSSID().c_str());
    }
  }
}

void loop() {
  wm->process();        // service the captive portal (non-blocking)
  displayService();     // render any queued e-paper job (may be slow, that's ok)
  netService();         // apply any pending WiFi-switch / reboot / reset

  // detect the first STA connection that happens via the portal
  if (!servicesUp && WiFi.status() == WL_CONNECTED) onConnected();

  // heartbeat so the serial monitor always shows life + state
  static uint32_t t = 0;
  if (millis() - t > 3000) {
    t = millis();
    Serial.printf("[hb] up=%lus sta=%d staIP=%s rssi=%d heap=%u web=%d\n",
                  millis()/1000, WiFi.status() == WL_CONNECTED,
                  WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(),
                  ESP.getFreeHeap(), (int)servicesUp);
  }
  delay(20);
}
