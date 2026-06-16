// ============================================================================
//  store.h — transaction log persisted to LittleFS (/txns.json)
// ============================================================================
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum TxnStatus { TX_PENDING = 0, TX_PAID = 1, TX_CANCELLED = 2 };

struct Txn {
  uint32_t id;
  time_t   ts;       // epoch seconds (0 if NTP not synced)
  double   amount;
  String   note;
  uint8_t  status;
};

void     storeInit();                       // mount FS, load nextId
uint32_t storeAdd(double amount, const String& note);  // -> new pending id
bool     storeSetStatus(uint32_t id, uint8_t status);
bool     storeGet(uint32_t id, Txn& out);

// Serialize the whole (capped) log into a JSON array document.
void     storeToJson(JsonArray arr);
