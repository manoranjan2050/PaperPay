// ============================================================================
//  web.cpp — async dashboard server
//    /                 -> static SPA from LittleFS (/data)
//    GET  /api/state         device + shop info
//    GET  /api/config        current shop settings
//    POST /api/config        save shop settings  {vpa,payee,shopName,...}
//    POST /api/pay           {amount,note} -> {id,upi}  (+ shows QR on e-paper)
//    POST /api/paid          {id}
//    POST /api/cancel        {id}
//    GET  /api/transactions  full log (array)
//    GET  /api/qr.svg?data=  on-screen QR for the dashboard (works offline)
// ============================================================================
#include "web.h"
#include "config.h"
#include "store.h"
#include "qrpay.h"
#include "display.h"
#include "telegram.h"
#include "netctl.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>

static AsyncWebServer server(80);

// ---- helpers ----------------------------------------------------------------
static void sendJson(AsyncWebServerRequest* req, JsonDocument& doc, int code = 200) {
  auto* res = req->beginResponseStream("application/json");
  res->setCode(code);
  serializeJson(doc, *res);
  req->send(res);
}

static String qrToSvg(const String& data) {
  QRCode qr; uint8_t* buf = nullptr;
  if (qrBuild(data, &qr, &buf) != 0) return "";
  const int q = 2;                      // quiet zone (modules)
  const int n = qr.size + q * 2;
  String s;
  s.reserve(qr.size * qr.size * 12);
  s  = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 ";
  s += String(n) + " " + String(n) + "' shape-rendering='crispEdges'>";
  s += "<rect width='100%' height='100%' fill='#fff'/>";
  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y)) {
        s += "<rect x='" + String(x + q) + "' y='" + String(y + q) +
             "' width='1' height='1' fill='#000'/>";
      }
  s += "</svg>";
  free(buf);
  return s;
}

// ---- API handlers -----------------------------------------------------------
static void apiState(AsyncWebServerRequest* req) {
  JsonDocument doc;
  doc["connected"] = WiFi.isConnected();
  doc["ip"]        = WiFi.localIP().toString();
  doc["host"]      = MDNS_HOST;
  doc["rssi"]      = WiFi.RSSI();
  doc["shopName"]  = CFG.shopName;
  doc["currency"]  = CFG.currency;
  doc["gst"]       = CFG.gstPercent;
  doc["configured"] = CFG.vpa.length() > 0;
  sendJson(req, doc);
}

static void apiGetConfig(AsyncWebServerRequest* req) {
  JsonDocument doc;
  doc["vpa"]      = CFG.vpa;
  doc["payee"]    = CFG.payee;
  doc["shopName"] = CFG.shopName;
  doc["currency"] = CFG.currency;
  doc["gst"]      = CFG.gstPercent;
  doc["tzOffset"] = CFG.tzOffset;
  doc["tgToken"]   = CFG.tgToken;
  doc["tgChat"]    = CFG.tgChat;
  doc["tgEnable"]  = CFG.tgEnable;
  doc["tgAutoPaid"]= CFG.tgAutoPaid;
  sendJson(req, doc);
}

static void apiSetConfig(AsyncWebServerRequest* req, JsonVariant& body) {
  JsonObject o = body.as<JsonObject>();
  if (o["vpa"].is<const char*>())      CFG.vpa      = o["vpa"].as<String>();
  if (o["payee"].is<const char*>())    CFG.payee    = o["payee"].as<String>();
  if (o["shopName"].is<const char*>()) CFG.shopName = o["shopName"].as<String>();
  if (o["currency"].is<const char*>()) CFG.currency = o["currency"].as<String>();
  if (!o["gst"].isNull())              CFG.gstPercent = o["gst"].as<float>();
  if (!o["tzOffset"].isNull())         CFG.tzOffset   = o["tzOffset"].as<long>();
  if (o["tgToken"].is<const char*>())  CFG.tgToken  = o["tgToken"].as<String>();
  if (o["tgChat"].is<const char*>())   CFG.tgChat   = o["tgChat"].as<String>();
  if (!o["tgEnable"].isNull())         CFG.tgEnable = o["tgEnable"].as<bool>();
  if (!o["tgAutoPaid"].isNull())       CFG.tgAutoPaid = o["tgAutoPaid"].as<bool>();
  configSave();

  JsonDocument doc; doc["ok"] = true;
  sendJson(req, doc);
}

static void apiPay(AsyncWebServerRequest* req, JsonVariant& body) {
  JsonObject o = body.as<JsonObject>();
  double amount = o["amount"].as<double>();
  String note   = o["note"].as<String>();

  if (CFG.vpa.length() == 0) {
    JsonDocument e; e["error"] = "UPI ID not set. Open Settings first.";
    sendJson(req, e, 400); return;
  }
  if (!(amount > 0)) {
    JsonDocument e; e["error"] = "Amount must be greater than 0.";
    sendJson(req, e, 400); return;
  }

  uint32_t id  = storeAdd(amount, note);
  if (note.length() == 0) note = "Bill #" + String(id);
  String upi   = upiBuildUrl(CFG.vpa, CFG.payee, amount, note);

  // push QR to the e-paper (rendered on the main loop)
  displayQueuePayment(CFG.shopName, amount, upi, note);

  char b[80];
  snprintf(b, sizeof(b), "🧾 New bill #%u: %s%.2f", id, CFG.currency.c_str(), amount);
  tgNotify(String(b) + "\n" + note);

  JsonDocument doc;
  doc["id"]     = id;
  doc["amount"] = amount;
  doc["upi"]    = upi;
  sendJson(req, doc);
}

static void apiSetTxn(AsyncWebServerRequest* req, JsonVariant& body, uint8_t status) {
  uint32_t id = body["id"].as<uint32_t>();
  bool ok = storeSetStatus(id, status);
  if (ok && status == TX_PAID) {
    Txn t;
    if (storeGet(id, t)) {
      displayQueuePaid(t.amount);
      char b[80];
      snprintf(b, sizeof(b), "✅ Bill #%u PAID (%s%.2f)", id, CFG.currency.c_str(), t.amount);
      tgNotify(String(b));
    }
  }
  if (ok && status == TX_CANCELLED) {
    displayQueueIdle(CFG.shopName, MDNS_HOST, WiFi.localIP().toString());
  }
  JsonDocument doc; doc["ok"] = ok;
  sendJson(req, doc, ok ? 200 : 404);
}

// single transaction status — used by the dashboard to live-update the QR overlay
static void apiTxn(AsyncWebServerRequest* req) {
  if (!req->hasParam("id")) { req->send(400, "text/plain", "missing id"); return; }
  uint32_t id = req->getParam("id")->value().toInt();
  Txn t; JsonDocument doc;
  if (storeGet(id, t)) {
    doc["id"] = t.id; doc["status"] = t.status; doc["amount"] = t.amount;
    sendJson(req, doc);
  } else {
    doc["error"] = "not found"; sendJson(req, doc, 404);
  }
}

// ---- WiFi / device management ----------------------------------------------
static void apiWifiInfo(AsyncWebServerRequest* req) {
  JsonDocument doc;
  doc["ssid"]      = WiFi.SSID();
  doc["ip"]        = WiFi.localIP().toString();
  doc["rssi"]      = WiFi.RSSI();
  doc["connected"] = WiFi.isConnected();
  sendJson(req, doc);
}

static void apiWifiScan(AsyncWebServerRequest* req) {
  // synchronous scan (keep STA so we don't drop the current connection)
  int n = WiFi.scanNetworks(false /*async*/, true /*show hidden*/);
  JsonDocument doc;
  doc["scanning"] = false;
  JsonArray arr = doc["nets"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i).isEmpty()) continue;          // skip hidden/blank
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["lock"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
  }
  WiFi.scanDelete();
  sendJson(req, doc);
}

static void apiWifiConnect(AsyncWebServerRequest* req, JsonVariant& body) {
  String ssid = body["ssid"].as<String>();
  String pass = body["password"].as<String>();
  if (ssid.isEmpty()) {
    JsonDocument e; e["error"] = "SSID required"; sendJson(req, e, 400); return;
  }
  netRequestWifiConnect(ssid, pass);
  JsonDocument doc; doc["ok"] = true; sendJson(req, doc);
}

static void apiTxns(AsyncWebServerRequest* req) {
  JsonDocument doc;
  storeToJson(doc.to<JsonArray>());
  sendJson(req, doc);
}

static void apiQrSvg(AsyncWebServerRequest* req) {
  if (!req->hasParam("data")) { req->send(400, "text/plain", "missing data"); return; }
  String svg = qrToSvg(req->getParam("data")->value());
  if (svg.length() == 0) { req->send(500, "text/plain", "qr failed"); return; }
  auto* res = req->beginResponse(200, "image/svg+xml", svg);
  res->addHeader("Cache-Control", "no-store");
  req->send(res);
}

// ---- wiring -----------------------------------------------------------------
void webBegin() {
  server.on("/api/state",        HTTP_GET, apiState);
  server.on("/api/config",       HTTP_GET, apiGetConfig);
  server.on("/api/transactions", HTTP_GET, apiTxns);
  server.on("/api/txn",          HTTP_GET, apiTxn);
  server.on("/api/qr.svg",       HTTP_GET, apiQrSvg);
  server.on("/api/wifi",         HTTP_GET, apiWifiInfo);
  server.on("/api/wifi/scan",    HTTP_GET, apiWifiScan);

  // device actions (no body)
  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* r){
    r->send(200, "application/json", "{\"ok\":true}"); netRequestReboot(); });
  server.on("/api/wifi/reset", HTTP_POST, [](AsyncWebServerRequest* r){
    r->send(200, "application/json", "{\"ok\":true}"); netRequestWifiReset(); });
  server.on("/api/telegram/test", HTTP_POST, [](AsyncWebServerRequest* r){
    tgNotify("PaperPay test ✅"); r->send(200, "application/json", "{\"ok\":true}"); });

  server.addHandler(new AsyncCallbackJsonWebHandler("/api/config",
      [](AsyncWebServerRequest* r, JsonVariant& j){ apiSetConfig(r, j); }));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/pay",
      [](AsyncWebServerRequest* r, JsonVariant& j){ apiPay(r, j); }));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/paid",
      [](AsyncWebServerRequest* r, JsonVariant& j){ apiSetTxn(r, j, TX_PAID); }));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/cancel",
      [](AsyncWebServerRequest* r, JsonVariant& j){ apiSetTxn(r, j, TX_CANCELLED); }));
  server.addHandler(new AsyncCallbackJsonWebHandler("/api/wifi/connect",
      [](AsyncWebServerRequest* r, JsonVariant& j){ apiWifiConnect(r, j); }));

  // static SPA (LittleFS /data) — index.html is the default file
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest* r){
    r->send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("[web] HTTP server listening on :80");
}
