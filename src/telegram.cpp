// ============================================================================
//  telegram.cpp — runs in its own FreeRTOS task.
//
//  * Sends payment-record notifications (queued via tgNotify()).
//  * Long-polls getUpdates for messages from the configured chat.
//  * Auto-confirm: when a message contains a number equal to a PENDING bill's
//    amount (e.g. a forwarded bank/UPI "credited Rs.149.00" SMS), that bill is
//    marked paid, the e-paper shows "Paid", and a confirmation is sent back.
//  * Commands: /today, /pending, /help.
//
//  TLS uses setInsecure() (no cert pinning) — fine for this LAN gadget.
// ============================================================================
#include "telegram.h"
#include "config.h"
#include "store.h"
#include "display.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>

static QueueHandle_t sendQ = nullptr;
static int64_t lastUpdateId = 0;

static String i64(int64_t v) {
  char b[24]; snprintf(b, sizeof(b), "%lld", (long long)v); return String(b);
}

static String urlEncode(const String& s) {
  static const char* hex = "0123456789ABCDEF";
  String o; o.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c)) o += c;
    else { o += '%'; o += hex[(c >> 4) & 0xF]; o += hex[c & 0xF]; }
  }
  return o;
}

static bool tgApiGet(const String& path, String& out) {
  if (CFG.tgToken.isEmpty()) return false;
  WiFiClientSecure cli; cli.setInsecure();
  HTTPClient http;
  String url = "https://api.telegram.org/bot" + CFG.tgToken + path;
  if (!http.begin(cli, url)) return false;
  http.setTimeout(30000);
  int code = http.GET();
  if (code == 200) out = http.getString();
  http.end();
  return code == 200;
}

// real send (blocking) — only called from the task
static void tgSendNow(const String& text) {
  if (!CFG.tgEnable || CFG.tgToken.isEmpty() || CFG.tgChat.isEmpty()) return;
  String resp;
  tgApiGet("/sendMessage?chat_id=" + CFG.tgChat + "&text=" + urlEncode(text), resp);
}

// public: queue a message from any task/context
void tgNotify(const String& text) {
  if (!sendQ || !CFG.tgEnable) return;
  String* s = new String(text);
  if (xQueueSend(sendQ, &s, 0) != pdTRUE) delete s;
}

// pull every number out of a message ("Rs.1,499.00 credited" -> 1499.00)
static std::vector<double> extractNumbers(const String& in) {
  std::vector<double> nums;
  String s = in; s.replace(",", "");
  String tok;
  for (size_t i = 0; i <= s.length(); i++) {
    char c = (i < s.length()) ? s[i] : ' ';
    if (isdigit((unsigned char)c) || c == '.') tok += c;
    else {
      if (tok.length() && tok != ".") nums.push_back(tok.toDouble());
      tok = "";
    }
  }
  return nums;
}

static String summaryText() {
  double total; uint32_t cnt, pend;
  storeSummary(total, cnt, pend);
  char b[96];
  snprintf(b, sizeof(b), "%s\nToday: %s%.2f (%u paid)\nPending: %u",
           CFG.shopName.c_str(), CFG.currency.c_str(), total, cnt, pend);
  return String(b);
}

static void handleMessage(const String& text) {
  if (text.startsWith("/today") || text.startsWith("/pending") || text.startsWith("/start")) {
    tgSendNow(summaryText());
    return;
  }
  if (text.startsWith("/help")) {
    tgSendNow("PaperPay bot\n/today - sales summary\n/pending - open bills\n"
              "Forward your bank/UPI payment SMS here to auto-mark a matching bill paid.");
    return;
  }
  if (!CFG.tgAutoPaid) return;

  // auto-confirm: match any number in the message to a pending bill amount
  for (double n : extractNumbers(text)) {
    if (n <= 0) continue;
    uint32_t id = storeMarkOldestPendingPaid(n);
    if (id) {
      displayQueuePaid(n);
      char b[80];
      snprintf(b, sizeof(b), "✅ Bill #%u marked PAID (%s%.2f)",
               id, CFG.currency.c_str(), n);
      tgSendNow(String(b));
      return;
    }
  }
}

static void tgTask(void*) {
  for (;;) {
    // 1) drain outgoing queue
    String* s;
    while (sendQ && xQueueReceive(sendQ, &s, 0) == pdTRUE) { tgSendNow(*s); delete s; }

    // 2) poll for incoming messages
    if (CFG.tgEnable && !CFG.tgToken.isEmpty() && WiFi.isConnected()) {
      String resp;
      if (tgApiGet("/getUpdates?timeout=20&offset=" + i64(lastUpdateId + 1), resp)) {
        JsonDocument doc;
        if (deserializeJson(doc, resp) == DeserializationError::Ok) {
          for (JsonObject u : doc["result"].as<JsonArray>()) {
            lastUpdateId = u["update_id"].as<int64_t>();
            JsonObject m = u["message"];
            if (m.isNull()) m = u["channel_post"];
            if (m.isNull()) continue;
            String chat = i64(m["chat"]["id"].as<int64_t>());
            if (CFG.tgChat.length() && chat != CFG.tgChat) continue; // lock to chat
            String txt = m["text"].as<String>();
            if (txt.length()) handleMessage(txt);
          }
        }
      } else {
        vTaskDelay(3000 / portTICK_PERIOD_MS);
      }
    } else {
      vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    vTaskDelay(300 / portTICK_PERIOD_MS);
  }
}

void tgInit() {
  sendQ = xQueueCreate(8, sizeof(String*));
  // TLS needs a generous stack
  xTaskCreatePinnedToCore(tgTask, "tg", 12288, nullptr, 1, nullptr, 1);
}
