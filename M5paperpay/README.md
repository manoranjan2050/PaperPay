<!-- ============================ HEADER ============================ -->
<p align="center">
  <img src="https://capsule-render.vercel.app/api?type=waving&color=0:0ea975,100:16b886&height=200&section=header&text=M5PaperPay&fontSize=66&fontColor=ffffff&fontAlignY=36&desc=Touchscreen%20UPI%20Counter%20on%20M5Stack%20M5Paper&descSize=20&descAlignY=60&animation=fadeIn" width="100%"/>
</p>

<p align="center">
  <img src="https://readme-typing-svg.demolab.com?font=Segoe+UI&weight=600&size=22&pause=900&color=16B886&center=true&vCenter=true&width=640&lines=Tap+the+amount+on+the+screen;Big+UPI+QR+for+the+customer;Mark+paid+%E2%80%94+or+let+Telegram+auto-confirm;No+phone+needed+at+the+counter." alt="typing"/>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/M5Stack-M5Paper-red?logo=m5stack&logoColor=white"/>
  <img src="https://img.shields.io/badge/4.7%22-e--ink%20960x540-555"/>
  <img src="https://img.shields.io/badge/Touch-capacitive-097969"/>
  <img src="https://img.shields.io/badge/Lib-M5Unified%20%2B%20M5GFX-orange"/>
  <img src="https://img.shields.io/badge/Pay-UPI-097969?logo=googlepay&logoColor=white"/>
  <img src="https://img.shields.io/badge/Bot-Telegram-26A5E4?logo=telegram&logoColor=white"/>
</p>

<p align="center"><b>PaperPay, ported to the M5Paper</b> — a self-contained payment terminal. Bill on the<br/>device's touchscreen, show a big UPI QR to the customer, and log every sale.<br/>Reuses all of <a href="../">PaperPay</a>'s logic; only the display + touch layer is new.</p>

<!-- ============================ SCREENS ============================ -->
<table align="center"><tr>
  <td align="center"><img src="assets/m5-calculator.svg" width="430"/><br/><sub>① Bill with the on-device calculator</sub></td>
  <td align="center"><img src="assets/m5-payment.svg" width="430"/><br/><sub>② Customer scans the QR</sub></td>
</tr></table>

---

## 🤔 How it works

```mermaid
flowchart LR
    S["🧑‍💼 Shopkeeper<br/>taps amount"] --> M
    subgraph M["📟 M5Paper"]
        UI["Touch keypad"] --> QR["UPI QR<br/>on screen"]
        RTC["RTC clock"]:::g
        BAT["Battery %"]:::g
        WEB["WiFi dashboard<br/>(phone too)"]:::g
    end
    QR --> C["🧑 Customer<br/>UPI app"]
    C --> BANK["🏦 Bank / UPI"]
    BANK -. "forward SMS" .-> TG["🤖 Telegram bot"]
    TG -. "amount matches → PAID" .-> M
    classDef g fill:#0e271f,stroke:#16b886,color:#9fe;
    style M fill:#0f1115,stroke:#16b886,color:#e8edf2
    style QR fill:#10271f,stroke:#16b886,color:#16b886
```

A bill becomes **Paid** in any of three ways: tap **MARK PAID**, tap **CASH PAID**, or
forward the bank/UPI "credited ₹X" SMS into Telegram (it matches the amount and
marks the bill paid automatically, flipping the screen to ✓ Paid).

---

## ✨ On the device
- 🧮 **On-device calculator** (`+ − × ÷ =`) — e.g. `30 × 2 + 40` → **CHARGE** (UPI QR) or **CASH PAID**
- 🧾 **Big QR** facing the customer; **MARK PAID / CANCEL**
- 🕒 **Date + time** (onboard RTC) and 🔋 **battery %** in the header
- 📱 Phone/PC dashboard still served over WiFi (calculator, payments log, settings)
- 🤖 Telegram alerts + **auto‑confirm** from a forwarded payment SMS

## 🔧 Hardware
| | |
|---|---|
| Board | **M5Stack M5Paper** (ESP32‑D0WDQ6, 4.7" IT8951 e‑ink 960×540, GT911 touch, RTC, battery) |
| Library | **M5Unified + M5GFX** (also covers M5PaperS3 — just change the board id) |
| Power | USB‑C / built‑in battery |

> No wiring — everything is integrated. Just flash and go.

## 🚀 Build & flash
```bash
pio run -t uploadfs     # dashboard (data/) → LittleFS
pio run -t upload       # firmware
pio device monitor
```
Optional: copy `include/wifi_secrets.h.example` → `wifi_secrets.h` and add your
WiFi to auto‑connect on boot (git‑ignored). Otherwise join the
**`M5PaperPay-Setup`** hotspot on first boot to configure WiFi + UPI ID.

## 🧩 What's reused vs new
| Reused verbatim from PaperPay | M5Paper‑specific |
|---|---|
| `config` · `qrpay` · `store` · `web` · `telegram` · `netctl` · dashboard `data/` | `display_m5.cpp` (M5GFX render + touch UI) · `main.cpp` |

<p align="center"><sub>Part of <a href="../">PaperPay</a> · MIT</sub></p>

<img src="https://capsule-render.vercel.app/api?type=waving&color=0:16b886,100:0ea975&height=110&section=footer" width="100%"/>
