// ============================================================================
//  display_m5.cpp — M5Paper (4.7" IT8951 e-ink, 960x540 landscape) via
//  M5Unified + M5GFX. Same display.h interface the web server uses, PLUS an
//  on-device touch billing UI (keypad -> QR -> paid).
//
//  Pattern proven on the user's working M5Paper project: draw into a 1-bpp
//  LGFX_Sprite (landscape 960x540, touch coords map 1:1 after rotation), then
//  setEpdMode + pushSprite. Touch via M5.Touch.getDetail() (M5.update() runs
//  in the main loop).
// ============================================================================
#include "display.h"
#include "config.h"
#include "store.h"
#include "qrpay.h"
#include "telegram.h"
#include <M5Unified.h>

#define SCR_W 960
#define SCR_H 540
#define C_BLACK 0x000000u
#define C_WHITE 0xFFFFFFu

static LGFX_Sprite* cv = nullptr;     // off-screen canvas, created after M5.begin()

enum class Mode { Boot, Bill, Payment, Paid, Idle };
static Mode mode = Mode::Boot;

// ---- web-queued state (async task) -----------------------------------------
static SemaphoreHandle_t mtx;
static volatile bool havePending = false;
static Mode    pMode;
static String  pShop, pHost, pIp, pUpi, pL1, pL2;
static double  pAmount = 0;
static uint32_t pId = 0;

// ---- active bill / on-device input -----------------------------------------
static String   amountStr = "0";
static uint32_t curId = 0;
static double   curAmount = 0;
static String   curUpi;

struct Btn { int x, y, w, h; const char* label; };
static Btn keys[12] = {
  {30,180,140,74,"7"},{182,180,140,74,"8"},{334,180,140,74,"9"},
  {30,264,140,74,"4"},{182,264,140,74,"5"},{334,264,140,74,"6"},
  {30,348,140,74,"1"},{182,348,140,74,"2"},{334,348,140,74,"3"},
  {30,432,140,74,"C"},{182,432,140,74,"0"},{334,432,140,74,"<"},
};
static Btn bCharge = {500,180,440,150,"CHARGE  (show UPI QR)"};
static Btn bCash   = {500,346,440,90,"CASH PAID"};
static Btn bPaid   = {40,360,300,120,"MARK PAID"};
static Btn bCancel = {360,360,180,120,"CANCEL"};

static bool inRect(const Btn& b, int x, int y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

// ---- drawing helpers --------------------------------------------------------
static void txt(const String& s, int x, int y, float size, uint8_t datum, uint32_t color = C_BLACK) {
  cv->setTextColor(color);
  cv->setTextFont(4);
  cv->setTextSize(size);
  cv->setTextDatum(datum);
  cv->drawString(s, x, y);
}

static void drawBtn(const Btn& b, bool filled, float size) {
  cv->fillRoundRect(b.x, b.y, b.w, b.h, 12, filled ? C_BLACK : C_WHITE);
  cv->drawRoundRect(b.x, b.y, b.w, b.h, 12, C_BLACK);
  txt(b.label, b.x + b.w / 2, b.y + b.h / 2, size, lgfx::MC_DATUM, filled ? C_WHITE : C_BLACK);
}

static void push(epd_mode_t m) { M5.Display.setEpdMode(m); cv->pushSprite(0, 0); }

static void header(const String& right) {
  cv->fillRect(0, 0, SCR_W, 56, C_WHITE);
  txt(CFG.shopName.length() ? CFG.shopName : String("PaperPay"), 16, 16, 1.0, lgfx::TL_DATUM);
  txt(right, SCR_W - 16, 20, 0.8, lgfx::TR_DATUM);
  cv->drawLine(0, 56, SCR_W, 56, C_BLACK);
}

// ---- screens ----------------------------------------------------------------
static void renderBill(epd_mode_t m = epd_mode_t::epd_fast) {
  cv->fillSprite(C_WHITE);
  header("UPI Counter");

  cv->drawRoundRect(20, 66, 920, 96, 12, C_BLACK);
  txt("Amount to charge", 36, 78, 0.8, lgfx::TL_DATUM);
  txt(CFG.currency + " " + amountStr, 920, 112, 2.2, lgfx::MR_DATUM);

  for (int i = 0; i < 12; i++) drawBtn(keys[i], false, 1.4);
  drawBtn(bCharge, true, 1.0);
  drawBtn(bCash, false, 1.0);
  txt("Bill here, or from the phone dashboard", 720, 452, 0.7, lgfx::MC_DATUM);
  push(m);
}

static void renderPayment() {
  cv->fillSprite(C_WHITE);
  header("Scan to Pay");

  char amt[24]; snprintf(amt, sizeof(amt), "%s %.2f", CFG.currency.c_str(), curAmount);
  txt(amt, 40, 110, 2.4, lgfx::TL_DATUM);
  txt("Scan with any UPI app", 40, 250, 1.0, lgfx::TL_DATUM);
  drawBtn(bPaid, true, 1.0);
  drawBtn(bCancel, false, 1.0);

  QRCode qr; uint8_t* buf = nullptr;
  if (qrBuild(curUpi, &qr, &buf) == 0) {
    int avail = 420, scale = avail / qr.size, dim = scale * qr.size;
    int x0 = 540 + (400 - dim) / 2, y0 = 60;
    cv->fillRect(x0 - 12, y0 - 12, dim + 24, dim + 24, C_WHITE);
    for (uint8_t y = 0; y < qr.size; y++)
      for (uint8_t x = 0; x < qr.size; x++)
        if (qrcode_getModule(&qr, x, y))
          cv->fillRect(x0 + x * scale, y0 + y * scale, scale, scale, C_BLACK);
    free(buf);
  }
  push(epd_mode_t::epd_quality);
}

static void renderPaid() {
  cv->fillSprite(C_WHITE);
  // tick
  for (int i = 0; i < 5; i++) {
    cv->drawLine(390 + i, 270, 450 + i, 330, C_BLACK);
    cv->drawLine(450 + i, 330, 560 + i, 210, C_BLACK);
  }
  char amt[28]; snprintf(amt, sizeof(amt), "PAID  %s %.2f", CFG.currency.c_str(), curAmount);
  txt(amt, SCR_W / 2, 400, 1.8, lgfx::MC_DATUM);
  txt("Tap to bill again", SCR_W / 2, 470, 0.9, lgfx::MC_DATUM);
  push(epd_mode_t::epd_quality);
}

static void renderIdle() {
  cv->fillSprite(C_WHITE);
  header(pIp);
  txt("Ready", SCR_W / 2, 200, 2.0, lgfx::MC_DATUM);
  txt("http://" + pHost + ".local", SCR_W / 2, 300, 1.0, lgfx::MC_DATUM);
  txt("Tap the screen to bill on the device", SCR_W / 2, 370, 0.9, lgfx::MC_DATUM);
  push(epd_mode_t::epd_quality);
}

static void renderBoot() {
  cv->fillSprite(C_WHITE);
  txt("PaperPay", SCR_W / 2, 220, 2.4, lgfx::MC_DATUM);
  txt(pL1, SCR_W / 2, 320, 1.0, lgfx::MC_DATUM);
  txt(pL2, SCR_W / 2, 370, 1.0, lgfx::MC_DATUM);
  push(epd_mode_t::epd_fast);
}

static void render() {
  switch (mode) {
    case Mode::Boot:    renderBoot();    break;
    case Mode::Idle:    renderIdle();    break;
    case Mode::Bill:    renderBill();    break;
    case Mode::Payment: renderPayment(); break;
    case Mode::Paid:    renderPaid();    break;
  }
}

// ---- on-device actions ------------------------------------------------------
static void onKey(const char* k) {
  String key(k);
  if (key == "C") amountStr = "0";
  else if (key == "<") { amountStr.remove(amountStr.length() - 1); if (!amountStr.length()) amountStr = "0"; }
  else {
    int dot = amountStr.indexOf('.');
    if (dot >= 0 && amountStr.length() - dot > 2) return;
    if (amountStr == "0") amountStr = "";
    amountStr += key;
  }
  renderBill();
}

static void doCharge(bool cash) {
  double a = amountStr.toDouble();
  if (a <= 0) return;
  if (cash) {
    uint32_t id = storeAdd(a, "CASH");
    storeSetStatus(id, TX_PAID);
    curAmount = a;
    char b[80]; snprintf(b, sizeof(b), "Cash bill #%u PAID (%s%.2f)", id, CFG.currency.c_str(), a);
    tgNotify(String(b));
    mode = Mode::Paid; render(); amountStr = "0";
  } else {
    if (CFG.vpa.length() == 0) return;
    uint32_t id = storeAdd(a, "");
    curId = id; curAmount = a;
    curUpi = upiBuildUrl(CFG.vpa, CFG.payee, a, "Bill #" + String(id));
    char b[80]; snprintf(b, sizeof(b), "New bill #%u: %s%.2f", id, CFG.currency.c_str(), a);
    tgNotify(String(b));
    mode = Mode::Payment; render(); amountStr = "0";
  }
}

static void onTap(int x, int y) {
  switch (mode) {
    case Mode::Bill:
      for (int i = 0; i < 12; i++) if (inRect(keys[i], x, y)) { onKey(keys[i].label); return; }
      if (inRect(bCharge, x, y)) { doCharge(false); return; }
      if (inRect(bCash, x, y))   { doCharge(true);  return; }
      break;
    case Mode::Payment:
      if (inRect(bPaid, x, y)) {
        storeSetStatus(curId, TX_PAID);
        char b[80]; snprintf(b, sizeof(b), "Bill #%u PAID (%s%.2f)", curId, CFG.currency.c_str(), curAmount);
        tgNotify(String(b));
        mode = Mode::Paid; render(); return;
      }
      if (inRect(bCancel, x, y)) { storeSetStatus(curId, TX_CANCELLED); mode = Mode::Bill; render(); return; }
      break;
    case Mode::Paid:
    case Mode::Idle:
      mode = Mode::Bill; render(); return;
    default: break;
  }
}

// ---- public interface (display.h) ------------------------------------------
void displayInit() {
  mtx = xSemaphoreCreateMutex();
  if (M5.Display.width() < M5.Display.height())
    M5.Display.setRotation(M5.Display.getRotation() ^ 1);   // -> landscape 960x540
  cv = new LGFX_Sprite(&M5.Display);
  cv->setColorDepth(1);
  cv->createSprite(SCR_W, SCR_H);
  cv->setTextColor(C_BLACK);
}

static void queue(Mode m) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  pMode = m; havePending = true;
  xSemaphoreGive(mtx);
}

void displayBoot(const String& l1, const String& l2) { pL1 = l1; pL2 = l2; queue(Mode::Boot); }
void displayQueueIdle(const String& shop, const String& host, const String& ip) {
  pHost = host; pIp = ip; queue(Mode::Idle);
}
void displayQueuePayment(uint32_t id, const String& shop, double amount,
                         const String& upi, const String& note) {
  pId = id; pShop = shop; pAmount = amount; pUpi = upi; queue(Mode::Payment);
}
void displayQueuePaid(double amount) { pAmount = amount; queue(Mode::Paid); }

void displayService() {
  if (havePending) {
    Mode m;
    xSemaphoreTake(mtx, portMAX_DELAY);
    m = pMode; havePending = false;
    xSemaphoreGive(mtx);
    mode = m;
    if (m == Mode::Payment) { curId = pId; curAmount = pAmount; curUpi = pUpi; }
    if (m == Mode::Paid)    { curAmount = pAmount; }
    render();
  }
  if (mode != Mode::Boot && M5.Touch.getCount()) {
    auto t = M5.Touch.getDetail(0);
    if (t.wasClicked()) onTap(t.x, t.y);
  }
}
