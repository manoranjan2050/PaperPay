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
#include <math.h>

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
static String   amountStr = "0";   // calculator display / current number
static uint32_t curId = 0;
static double   curAmount = 0;
static String   curUpi;

// calculator state
static double calcAcc = 0;
static char   calcOp = 0;
static bool   calcFresh = true;    // next digit starts a fresh number

static String fmtNum(double v) {
  char b[24];
  if (v == (long long)v && fabs(v) < 1e12) snprintf(b, sizeof(b), "%lld", (long long)v);
  else                                     snprintf(b, sizeof(b), "%.2f", v);
  return String(b);
}
static double applyOp(double a, char op, double b) {
  switch (op) { case '+': return a + b; case '-': return a - b;
                case 'x': return a * b; case '/': return b != 0 ? a / b : 0; }
  return b;
}

struct Btn { int x, y, w, h; const char* label; };

// calculator keypad (left side): digits + . and the four operators + =
#define NKEYS 18
static Btn keys[NKEYS] = {
  {20,138,110,64,"C"},{138,138,110,64,"DEL"},{256,138,110,64,"/"},{374,138,110,64,"x"},
  {20,208,110,64,"7"},{138,208,110,64,"8"},{256,208,110,64,"9"},{374,208,110,64,"-"},
  {20,278,110,64,"4"},{138,278,110,64,"5"},{256,278,110,64,"6"},{374,278,110,64,"+"},
  {20,348,110,64,"1"},{138,348,110,64,"2"},{256,348,110,64,"3"},{374,348,110,134,"="},
  {20,418,228,64,"0"},{256,418,110,64,"."},
};
static Btn bCharge = {500,138,440,170,"CHARGE  ->  UPI QR"};
static Btn bCash   = {500,322,440,90,"CASH PAID"};

static bool isOpLabel(const char* l) {
  char c = l[0]; return (c == '+' || c == '-' || c == 'x' || c == '/' || c == '=');
}
static Btn bPaid   = {40,392,210,78,"MARK PAID"};
static Btn bCancel = {270,392,170,78,"CANCEL"};

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

static void header() {
  cv->fillRect(0, 0, SCR_W, 56, C_WHITE);
  txt(CFG.shopName.length() ? CFG.shopName : String("PaperPay"), 16, 16, 1.0, lgfx::TL_DATUM);

  // right side: date + time + battery
  char rb[48], tb[24];
  time_t now; time(&now); struct tm ti; localtime_r(&now, &ti);
  if (ti.tm_year > 120) strftime(tb, sizeof(tb), "%d %b  %I:%M %p", &ti);  // 12-hour
  else                  strcpy(tb, "--:--");
  int batt = M5.Power.getBatteryLevel();
  if (batt >= 0) snprintf(rb, sizeof(rb), "%s    %d%%", tb, batt);
  else           snprintf(rb, sizeof(rb), "%s", tb);

  cv->setTextFont(4); cv->setTextSize(0.9); cv->setTextDatum(lgfx::TR_DATUM);
  if (batt >= 0 && batt < 15) {                  // low battery -> inverted chip
    int w = cv->textWidth(rb);
    cv->fillRoundRect(SCR_W - 16 - w - 12, 8, w + 18, 38, 6, C_BLACK);
    cv->setTextColor(C_WHITE);
  } else {
    cv->setTextColor(C_BLACK);
  }
  cv->drawString(rb, SCR_W - 18, 16);
  cv->drawLine(0, 56, SCR_W, 56, C_BLACK);
}

// ---- screens ----------------------------------------------------------------
static void renderBill(epd_mode_t m = epd_mode_t::epd_fast) {
  cv->fillSprite(C_WHITE);
  header();

  // calculator display bar
  cv->drawRoundRect(20, 60, 920, 68, 10, C_BLACK);
  String pend = calcOp ? (fmtNum(calcAcc) + " " + String((char)calcOp)) : String("");
  txt(pend, 36, 80, 0.9, lgfx::TL_DATUM);
  txt(CFG.currency + " " + amountStr, 924, 94, 1.5, lgfx::MR_DATUM);

  for (int i = 0; i < NKEYS; i++) drawBtn(keys[i], isOpLabel(keys[i].label), 1.2);
  drawBtn(bCharge, true, 1.0);
  drawBtn(bCash, false, 1.0);
  txt("Calculator: + - x / =  then CHARGE", 720, 432, 0.7, lgfx::MC_DATUM);
  push(m);
}

static void renderPayment() {
  cv->fillSprite(C_WHITE);
  header();

  char amt[24]; snprintf(amt, sizeof(amt), "%s %.2f", CFG.currency.c_str(), curAmount);
  txt(amt, 40, 100, 2.2, lgfx::TL_DATUM);
  txt("Scan with any", 40, 220, 1.0, lgfx::TL_DATUM);
  txt("UPI app to pay", 40, 270, 1.0, lgfx::TL_DATUM);
  drawBtn(bPaid, true, 0.9);
  drawBtn(bCancel, false, 0.9);

  // QR confined to the right half so it never overlaps the buttons
  QRCode qr; uint8_t* buf = nullptr;
  if (qrBuild(curUpi, &qr, &buf) == 0) {
    int boxX = 500, boxY = 70, boxW = 440, boxH = 430;
    int budget = (boxW < boxH ? boxW : boxH);
    int scale = budget / qr.size; if (scale < 1) scale = 1;
    int dim = scale * qr.size;
    int x0 = boxX + (boxW - dim) / 2, y0 = boxY + (boxH - dim) / 2;
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
  header();
  txt("Ready", SCR_W / 2, 190, 2.0, lgfx::MC_DATUM);
  txt("http://" + pIp, SCR_W / 2, 300, 1.0, lgfx::MC_DATUM);
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
static void calcEquals() {
  if (calcOp) {
    amountStr = fmtNum(applyOp(calcAcc, calcOp, amountStr.toDouble()));
    calcOp = 0; calcFresh = true;
  }
}

static void calcInput(const String& k) {
  if (k == "C") { amountStr = "0"; calcAcc = 0; calcOp = 0; calcFresh = true; }
  else if (k == "DEL") {
    if (!calcFresh) { amountStr.remove(amountStr.length() - 1);
      if (!amountStr.length() || amountStr == "-") amountStr = "0"; }
  }
  else if (k == "+" || k == "-" || k == "x" || k == "/") {
    if (calcOp && !calcFresh) {        // chain: 30 + 40 then x -> evaluate first
      amountStr = fmtNum(applyOp(calcAcc, calcOp, amountStr.toDouble()));
      calcAcc = amountStr.toDouble();
    } else {
      calcAcc = amountStr.toDouble();
    }
    calcOp = k[0]; calcFresh = true;
  }
  else if (k == "=") { calcEquals(); }
  else {                               // digit or "."
    if (calcFresh) { amountStr = (k == "." ? "0." : k); calcFresh = false; }
    else if (k == ".") { if (amountStr.indexOf('.') < 0) amountStr += "."; }
    else {
      int dot = amountStr.indexOf('.');
      if (dot >= 0 && amountStr.length() - dot > 2) return;   // max 2 decimals
      if (amountStr == "0") amountStr = k; else amountStr += k;
    }
  }
  renderBill();
}

static void doCharge(bool cash) {
  calcEquals();                        // finalize any pending calculation
  double a = amountStr.toDouble();
  if (a <= 0) return;
  if (cash) {
    uint32_t id = storeAdd(a, "CASH");
    storeSetStatus(id, TX_PAID);
    curAmount = a;
    char b[80]; snprintf(b, sizeof(b), "Cash bill #%u PAID (%s%.2f)", id, CFG.currency.c_str(), a);
    tgNotify(String(b));
    mode = Mode::Paid; render(); amountStr = "0"; calcAcc = 0; calcOp = 0; calcFresh = true;
  } else {
    if (CFG.vpa.length() == 0) return;
    uint32_t id = storeAdd(a, "");
    curId = id; curAmount = a;
    curUpi = upiBuildUrl(CFG.vpa, CFG.payee, a, "Bill #" + String(id));
    char b[80]; snprintf(b, sizeof(b), "New bill #%u: %s%.2f", id, CFG.currency.c_str(), a);
    tgNotify(String(b));
    mode = Mode::Payment; render(); amountStr = "0"; calcAcc = 0; calcOp = 0; calcFresh = true;
  }
}

static void onTap(int x, int y) {
  switch (mode) {
    case Mode::Bill:
      for (int i = 0; i < NKEYS; i++) if (inRect(keys[i], x, y)) { calcInput(keys[i].label); return; }
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

  // auto-return to the keypad ~4s after a payment is marked paid
  static Mode lastMode = Mode::Boot;
  static uint32_t paidAt = 0;
  if (mode == Mode::Paid && lastMode != Mode::Paid) paidAt = millis();
  lastMode = mode;
  if (mode == Mode::Paid && millis() - paidAt > 4000) { mode = Mode::Bill; render(); }

  // keep the idle clock current (cheap, only while idle)
  static uint32_t lastClock = 0;
  if (mode == Mode::Idle && millis() - lastClock > 60000) { lastClock = millis(); render(); }
}
