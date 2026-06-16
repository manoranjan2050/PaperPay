// ============================================================================
//  display.cpp — Waveshare 2.9" (296x128, SSD1680) via GxEPD2
//
//  Web requests run in the AsyncTCP task; SPI to the panel must run on the
//  main loop task. So web handlers only QUEUE a job (guarded by a mutex) and
//  displayService() — called from loop() — does the actual drawing.
// ============================================================================
#include "display.h"
#include "config.h"
#include "qrpay.h"
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

// --- Panel driver class ------------------------------------------------------
// Most current Waveshare 2.9" B/W modules are the SSD1680 "V2" (GxEPD2_290_T94_V2).
// If your screen is blank or mirrored, try GxEPD2_290_T94 or GxEPD2_290 (IL3820).
GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> epd(
    GxEPD2_290_T94_V2(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

// --- queued job --------------------------------------------------------------
enum class Job { None, Boot, Idle, Payment, Paid };
static SemaphoreHandle_t mtx;
static volatile Job pending = Job::None;
static String  jShop, jHost, jIp, jUpi, jNote, jL1, jL2;
static double  jAmount = 0;

static void setJob(Job j) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  pending = j;
  xSemaphoreGive(mtx);
}

void displayInit() {
  mtx = xSemaphoreCreateMutex();
  pinMode(PIN_EPD_BUSY, INPUT);
  Serial.printf("[epd] init  BUSY pin level=%d (HIGH usually = busy)\n",
                digitalRead(PIN_EPD_BUSY));
  SPI.begin(PIN_EPD_SCK, PIN_EPD_MISO, PIN_EPD_MOSI, PIN_EPD_CS);
  epd.init(115200, true, 2, false);
  epd.setRotation(1);            // landscape: 296 wide x 128 tall
  epd.setTextColor(GxEPD_BLACK);
  Serial.printf("[epd] init done  w=%d h=%d\n", epd.width(), epd.height());
}

// ---- queue helpers (called from web task) ----------------------------------
void displayBoot(const String& l1, const String& l2) {
  jL1 = l1; jL2 = l2; setJob(Job::Boot);
}
void displayQueueIdle(const String& shop, const String& host, const String& ip) {
  jShop = shop; jHost = host; jIp = ip; setJob(Job::Idle);
}
void displayQueuePayment(const String& shop, double amount,
                         const String& upi, const String& note) {
  jShop = shop; jAmount = amount; jUpi = upi; jNote = note; setJob(Job::Payment);
}
void displayQueuePaid(double amount) { jAmount = amount; setJob(Job::Paid); }

// ---- drawing primitives -----------------------------------------------------
static void drawQR(int ox, int oy, int boxPx) {
  QRCode qr; uint8_t* buf = nullptr;
  if (qrBuild(jUpi, &qr, &buf) != 0) return;

  int scale = boxPx / qr.size;
  if (scale < 1) scale = 1;
  int dim = scale * qr.size;
  int x0 = ox + (boxPx - dim) / 2;
  int y0 = oy + (boxPx - dim) / 2;

  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y))
        epd.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, GxEPD_BLACK);

  free(buf);
}

static void textAt(int x, int y, const String& s) {
  epd.setCursor(x, y); epd.print(s);
}

// ---- full renders -----------------------------------------------------------
static void renderBoot() {
  epd.setFullWindow();
  epd.firstPage();
  do {
    epd.fillScreen(GxEPD_WHITE);
    epd.setFont(&FreeSansBold12pt7b);
    textAt(10, 40, "PaperPay");
    epd.setFont(&FreeSans9pt7b);
    textAt(10, 75, jL1);
    textAt(10, 100, jL2);
  } while (epd.nextPage());
}

static void renderIdle() {
  epd.setFullWindow();
  epd.firstPage();
  do {
    epd.fillScreen(GxEPD_WHITE);
    epd.setFont(&FreeSansBold18pt7b);
    textAt(10, 45, jShop.length() ? jShop : String("My Shop"));
    epd.setFont(&FreeSans9pt7b);
    textAt(10, 80, "Ready - bill on the dashboard");
    textAt(10, 105, "http://" + jHost + ".local");
    textAt(10, 124, jIp);
  } while (epd.nextPage());
}

static void renderPayment() {
  const int W = epd.width();          // 296
  const int H = epd.height();         // 128
  const int qrBox = H;                // square QR area on the right (128)
  const int qrX   = W - qrBox;        // 168

  epd.setFullWindow();
  epd.firstPage();
  do {
    epd.fillScreen(GxEPD_WHITE);
    epd.drawFastVLine(qrX - 4, 0, H, GxEPD_BLACK);

    // left column: shop, amount, scan instruction
    epd.setFont(&FreeSans9pt7b);
    textAt(8, 20, jShop.length() ? jShop : String("Scan to Pay"));

    char amt[24];
    snprintf(amt, sizeof(amt), "%s%.2f", CFG.currency.c_str(), jAmount);
    epd.setFont(&FreeSansBold18pt7b);
    textAt(8, 70, amt);

    epd.setFont(&FreeSans9pt7b);
    textAt(8, 100, "Scan with any");
    textAt(8, 120, "UPI app to pay");

    drawQR(qrX, 0, qrBox);
  } while (epd.nextPage());
}

static void renderPaid() {
  epd.setFullWindow();
  epd.firstPage();
  do {
    epd.fillScreen(GxEPD_WHITE);
    // big tick
    epd.drawLine(110, 70, 130, 95, GxEPD_BLACK);
    epd.drawLine(111, 70, 131, 95, GxEPD_BLACK);
    epd.drawLine(130, 95, 180, 40, GxEPD_BLACK);
    epd.drawLine(131, 95, 181, 40, GxEPD_BLACK);
    epd.setFont(&FreeSansBold18pt7b);
    char amt[24];
    snprintf(amt, sizeof(amt), "Paid %s%.2f", CFG.currency.c_str(), jAmount);
    int16_t x1, y1; uint16_t w, h;
    epd.getTextBounds(amt, 0, 0, &x1, &y1, &w, &h);
    textAt((epd.width() - w) / 2, 122, amt);
  } while (epd.nextPage());
}

// ---- called from loop() -----------------------------------------------------
void displayService() {
  Job job;
  xSemaphoreTake(mtx, portMAX_DELAY);
  job = pending;
  pending = Job::None;
  xSemaphoreGive(mtx);

  if (job == Job::None) return;
  Serial.printf("[epd] rendering job=%d  BUSY=%d\n", (int)job, digitalRead(PIN_EPD_BUSY));
  uint32_t t0 = millis();
  switch (job) {
    case Job::Boot:    renderBoot();    break;
    case Job::Idle:    renderIdle();    break;
    case Job::Payment: renderPayment(); break;
    case Job::Paid:    renderPaid();    break;
    default: return;
  }
  epd.hibernate(); // low power between updates
  Serial.printf("[epd] render done in %lums\n", millis() - t0);
}
