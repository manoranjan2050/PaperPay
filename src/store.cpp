// ============================================================================
//  store.cpp — append-only-ish transaction log in LittleFS, capped at 500.
//  Low shop volume -> we just rewrite the file on each change. Simple + safe.
// ============================================================================
#include "store.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>

static const char* TX_FILE = "/txns.json";
static const size_t TX_CAP = 500;

static Preferences prefs;
static uint32_t nextId = 1;
static SemaphoreHandle_t fmtx;

static void load(JsonDocument& doc) {
  File f = LittleFS.open(TX_FILE, "r");
  if (!f) { doc.to<JsonArray>(); return; }
  if (deserializeJson(doc, f) != DeserializationError::Ok) doc.to<JsonArray>();
  f.close();
  if (!doc.is<JsonArray>()) doc.to<JsonArray>();
}

static void save(JsonDocument& doc) {
  JsonArray arr = doc.as<JsonArray>();
  while (arr.size() > TX_CAP) arr.remove(0); // drop oldest
  File f = LittleFS.open(TX_FILE, "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

void storeInit() {
  fmtx = xSemaphoreCreateMutex();
  prefs.begin("paperpay", false);
  nextId = prefs.getUInt("nextId", 1);
  prefs.end();
  if (!LittleFS.exists(TX_FILE)) {
    JsonDocument doc; doc.to<JsonArray>(); save(doc);
  }
}

uint32_t storeAdd(double amount, const String& note) {
  xSemaphoreTake(fmtx, portMAX_DELAY);
  JsonDocument doc; load(doc);
  JsonObject o = doc.as<JsonArray>().add<JsonObject>();
  uint32_t id = nextId++;
  o["id"]     = id;
  o["ts"]     = (uint32_t) time(nullptr);
  o["amount"] = amount;
  o["note"]   = note;
  o["status"] = (uint8_t) TX_PENDING;
  save(doc);

  prefs.begin("paperpay", false);
  prefs.putUInt("nextId", nextId);
  prefs.end();
  xSemaphoreGive(fmtx);
  return id;
}

bool storeSetStatus(uint32_t id, uint8_t status) {
  xSemaphoreTake(fmtx, portMAX_DELAY);
  JsonDocument doc; load(doc);
  bool found = false;
  for (JsonObject o : doc.as<JsonArray>()) {
    if (o["id"] == id) { o["status"] = status; found = true; break; }
  }
  if (found) save(doc);
  xSemaphoreGive(fmtx);
  return found;
}

bool storeGet(uint32_t id, Txn& out) {
  xSemaphoreTake(fmtx, portMAX_DELAY);
  JsonDocument doc; load(doc);
  bool found = false;
  for (JsonObject o : doc.as<JsonArray>()) {
    if (o["id"] == id) {
      out.id     = o["id"];
      out.ts     = (time_t) o["ts"].as<uint32_t>();
      out.amount = o["amount"];
      out.note   = o["note"].as<String>();
      out.status = o["status"];
      found = true; break;
    }
  }
  xSemaphoreGive(fmtx);
  return found;
}

uint32_t storeMarkOldestPendingPaid(double amount) {
  xSemaphoreTake(fmtx, portMAX_DELAY);
  JsonDocument doc; load(doc);
  uint32_t matched = 0;
  for (JsonObject o : doc.as<JsonArray>()) {       // array is oldest-first
    if (o["status"] == (uint8_t)TX_PENDING &&
        fabs(o["amount"].as<double>() - amount) < 0.01) {
      o["status"] = (uint8_t) TX_PAID;
      matched = o["id"];
      break;
    }
  }
  if (matched) save(doc);
  xSemaphoreGive(fmtx);
  return matched;
}

void storeSummary(double& todayTotal, uint32_t& todayCount, uint32_t& pending) {
  todayTotal = 0; todayCount = 0; pending = 0;
  time_t now = time(nullptr);
  struct tm tnow; localtime_r(&now, &tnow);

  xSemaphoreTake(fmtx, portMAX_DELAY);
  JsonDocument doc; load(doc);
  for (JsonObject o : doc.as<JsonArray>()) {
    uint8_t st = o["status"];
    if (st == TX_PENDING) pending++;
    if (st == TX_PAID) {
      time_t ts = (time_t) o["ts"].as<uint32_t>();
      struct tm tt; localtime_r(&ts, &tt);
      if (ts && tt.tm_year == tnow.tm_year && tt.tm_yday == tnow.tm_yday) {
        todayTotal += o["amount"].as<double>();
        todayCount++;
      }
    }
  }
  xSemaphoreGive(fmtx);
}

void storeToJson(JsonArray arr) {
  xSemaphoreTake(fmtx, portMAX_DELAY);
  JsonDocument doc; load(doc);
  for (JsonObject o : doc.as<JsonArray>()) arr.add(o);
  xSemaphoreGive(fmtx);
}
