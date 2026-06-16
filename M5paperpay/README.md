# M5PaperPay — PaperPay for M5Stack M5Paper

The same PaperPay UPI counter, ported to the **M5Stack M5Paper** (4.7" 960×540
e‑ink, capacitive touch, RTC, battery). It reuses the ESP32‑S3 build's logic
(web dashboard, UPI QR, payment store, Telegram bot) unchanged — only the
**display + on‑device touch billing** layer is M5Paper‑specific.

> The original ESP32‑S3 + Waveshare 2.9" project in the parent folder is
> **untouched**. This is a separate, self‑contained PlatformIO project.

## Why M5Paper
- **Bill directly on the device** — touch keypad, no phone needed.
- **Big crisp QR** on the 4.7" screen facing the customer.
- **RTC** → correct timestamps offline; **battery** → portable; **microSD** available.
- Phone/PC dashboard still works over WiFi exactly as before.

## What's reused vs new
| Reused (copied, identical) | M5Paper‑specific |
|---|---|
| `config` · `qrpay` · `store` · `web` · `telegram` · `netctl` · dashboard `data/` | `display_m5.cpp` (M5GFX render + touch keypad), `main.cpp` |

## Build & flash
```bash
pio run -t uploadfs     # dashboard (data/) -> LittleFS
pio run -t upload       # firmware
pio device monitor
```
- Board/libs are proven on a working M5Paper project: **M5Unified + M5GFX**,
  board `m5stack-grey` (M5Unified auto‑detects the real M5Paper at runtime).
- Optional auto‑connect: copy `include/wifi_secrets.h.example` → `wifi_secrets.h`
  and fill in your WiFi (git‑ignored).

## On‑device use
- **Bill screen:** tap the numeric keypad → **CHARGE** (shows the UPI QR) or
  **CASH PAID** (logs a cash sale).
- **Payment screen:** big QR + **MARK PAID** / **CANCEL**.
- Telegram auto‑confirm + the phone dashboard work the same as the S3 build.

## Notes
- **M5PaperS3** (ESP32‑S3 model) also uses M5Unified — the same code should
  build by switching the board id; tell me if you have the S3.
- If the touch feels mirrored on your unit, flip `t.x`/`t.y` in
  `display_m5.cpp` → `displayService()` (M5Unified usually maps it correctly
  after the landscape rotation).
