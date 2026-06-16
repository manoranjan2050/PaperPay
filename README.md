# PaperPay — E‑Paper UPI Counter

A tiny shop payment counter on an **ESP32‑S3‑N16R8** with a **Waveshare 2.9″ e‑paper**
display. Bill an amount from a phone/PC, and a **UPI QR code** appears both on the
e‑paper (facing the customer) and on the dashboard. The customer scans with any UPI app
(GPay / PhonePe / Paytm / BHIM) and pays. You log it with one tap.

No cloud, no app store — the ESP32 hosts everything over WiFi at **http://paperpay.local**.

```
 ┌─────────────┐      WiFi       ┌──────────────────────────┐
 │  Your phone │  ───────────▶   │  ESP32‑S3  (web server)  │
 │  / PC       │  dashboard      │  • calculator + billing  │
 └─────────────┘                 │  • UPI QR generator      │
                                 │  • payment log (LittleFS)│
                                 └────────────┬─────────────┘
                                              │ SPI
                                     ┌────────▼────────┐
                                     │  2.9" e‑paper   │  ← customer scans QR
                                     └─────────────────┘
```

## Features
- 📱 **Mobile‑friendly dashboard** (works on any browser, no install) with a calculator + keypad.
- 🧾 **UPI QR generation** — builds a `upi://pay?...` deep link and renders the QR on the e‑paper and on screen.
- 🖥️ **Customer‑facing e‑paper** shows the amount + QR; ultra‑low power, readable in sunlight.
- 📜 **Payment manager** — today's sales, pending vs paid, full log (kept on flash).
- ⚙️ **Captive‑portal onboarding** — first boot creates a `PaperPay-Setup` WiFi AP to enter WiFi + UPI ID.
- 🏷️ Optional **GST**, custom currency symbol, shop name.

## Hardware & wiring
Waveshare 2.9″ B/W module → ESP32‑S3 (defaults in [`include/config.h`](include/config.h)):

| e‑paper | wire   | ESP32‑S3 GPIO |
|---------|--------|---------------|
| BUSY    | purple | 7  |
| RST     | white  | 8  |
| DC      | green  | 9  |
| CS      | orange | 10 |
| CLK/SCK | yellow | 12 |
| DIN/MOSI| blue   | 11 |
| VCC     | red    | 3V3 |
| GND     | black  | GND |

> **Panel variant:** code defaults to `GxEPD2_290_T94_V2` (SSD1680, the common current
> module). If the screen stays blank or mirrored, edit `epd` in
> [`src/display.cpp`](src/display.cpp) to `GxEPD2_290_T94` or `GxEPD2_290`.

## Build & flash (PlatformIO)
```bash
# from this folder
pio run                       # compile
pio run -t uploadfs           # upload the web dashboard (data/) to LittleFS
pio run -t upload             # flash the firmware
pio device monitor            # watch logs
```
(VS Code + the PlatformIO extension works too: *Build*, *Upload Filesystem Image*, *Upload*.)

## First‑time setup
1. Power the board. The e‑paper shows **"Join AP: PaperPay‑Setup"**.
2. On your phone, join the WiFi **`PaperPay-Setup`**; the captive portal opens.
3. Pick your home/shop WiFi, enter the password, and fill in **UPI ID**, **payee name**, **shop name**.
4. It reboots, joins your WiFi, and the e‑paper shows the shop name + **http://paperpay.local**.
5. Open that URL on any device on the same network → start billing.

## How a sale works
1. Type the amount on the keypad (toggle GST if you set one) → **Generate UPI QR**.
2. The QR shows on the dashboard *and* on the e‑paper facing the customer.
3. Customer scans & pays in their UPI app.
4. Tap **Mark Paid** (or **Cancel**). It's saved to the **Payments** log.

## ⚠️ About automatic payment confirmation
A plain UPI QR (this project) is a **collect‑request link** — the customer's bank app
handles the payment, so the ESP32 has **no way to automatically know** that money arrived.
Confirmation here is **manual** (Mark Paid), which is exactly how most small QR‑sticker
shops already work.

For *automatic* confirmation you need a payment‑gateway merchant account that exposes
webhooks/UPI‑intent status (e.g. **Razorpay**, **Cashfree**, **PhonePe for Business**).
The clean way to add it: have a small cloud function receive the gateway webhook and
POST `/api/paid` to the device (or have the device poll the gateway's order‑status API).
The `apiPay` handler in [`src/web.cpp`](src/web.cpp) is where you'd create the gateway
order instead of a local txn.

## REST API (for integrations)
| Method | Path | Body / Query | Purpose |
|--------|------|--------------|---------|
| GET  | `/api/state` | – | connection + shop info |
| GET/POST | `/api/config` | `{vpa,payee,shopName,currency,gst,tzOffset}` | shop settings |
| POST | `/api/pay` | `{amount,note}` | create bill, show QR → `{id,upi}` |
| POST | `/api/paid` | `{id}` | mark a bill paid |
| POST | `/api/cancel` | `{id}` | cancel a bill |
| GET  | `/api/transactions` | – | full log (array) |
| GET  | `/api/qr.svg` | `?data=<text>` | QR as SVG (used by the dashboard) |

## Project layout
```
platformio.ini        build config, board + libraries
include/ + src/
  config.*            pin map + shop settings (NVS)
  qrpay.*             UPI URL + QR generation
  display.*           e‑paper rendering (queued, runs on loop task)
  store.*             transaction log on LittleFS
  web.*               async web server + REST API
  main.cpp            WiFi onboarding, NTP, mDNS, wiring
data/                 dashboard SPA (index.html, style.css, app.js) → LittleFS
```

## License
MIT — do whatever helps your shop. No warranty.
