<!-- ========================== HEADER ========================== -->
<p align="center">
  <img src="https://capsule-render.vercel.app/api?type=waving&color=0:0ea975,100:16b886&height=210&section=header&text=PaperPay&fontSize=82&fontColor=ffffff&fontAlignY=36&desc=E-Paper%20UPI%20Counter%20for%20Shops&descSize=22&descAlignY=60&animation=fadeIn" width="100%"/>
</p>

<p align="center">
  <img src="https://readme-typing-svg.demolab.com?font=Segoe+UI&weight=600&size=22&pause=900&color=16B886&center=true&vCenter=true&width=620&lines=Tap+an+amount+%E2%86%92+show+a+UPI+QR;Customer+scans+with+any+UPI+app;Forward+the+SMS+%E2%86%92+auto-marked+paid;No+cloud.+No+app+store.+Just+WiFi." alt="typing" />
</p>

<p align="center">
  <img src="https://img.shields.io/badge/ESP32--S3-N16R8-E7352C?logo=espressif&logoColor=white" />
  <img src="https://img.shields.io/badge/PlatformIO-Arduino-FF7F00?logo=platformio&logoColor=white" />
  <img src="https://img.shields.io/badge/Display-Waveshare%202.9%22%20e--Paper-555555" />
  <img src="https://img.shields.io/badge/Pay-UPI-097969?logo=googlepay&logoColor=white" />
  <img src="https://img.shields.io/badge/Bot-Telegram-26A5E4?logo=telegram&logoColor=white" />
  <img src="https://img.shields.io/badge/License-MIT-16b886" />
</p>

<p align="center"><b>A tiny self-hosted payment counter.</b> An ESP32-S3 hosts a mobile dashboard over WiFi, builds a <br/><code>upi://pay</code> QR for the billed amount, shows it on a 2.9" e-paper facing the customer, and logs every sale.</p>

<!-- ========================== HERO ========================== -->
<table align="center"><tr>
  <td align="center" width="62%"><img src="assets/device.svg" width="460"/><br/><sub>Customer-facing e-paper</sub></td>
  <td align="center" width="38%"><img src="assets/dashboard.svg" width="210"/><br/><sub>Phone / PC dashboard</sub></td>
</tr></table>

---

## ✨ Features

| | |
|---|---|
| 📱 **Mobile dashboard** | Calculator + keypad, works in any browser — no install. |
| 🧾 **UPI QR on demand** | Builds `upi://pay?...` and renders the QR on the e-paper **and** on screen. |
| 🖥️ **Sunlight-readable e-paper** | Customer-facing amount + QR, ultra-low power. |
| 🤖 **Telegram bot** | Bill & paid alerts, `/today` `/pending` commands. |
| ✅ **Auto-confirm payments** | Forward your bank/UPI "credited ₹X" SMS → the matching bill is **auto-marked paid**, e-paper flips to *Paid*, dashboard QR closes itself. |
| 📜 **Payment manager** | Today's sales, pending vs paid, full log on flash. |
| ⚙️ **Captive-portal setup** | First boot opens a `PaperPay-Setup` hotspot for WiFi + UPI details. |
| 📶 **WiFi from the dashboard** | Scan & switch networks, reboot, factory-reset — no re-flash. |

---

## 🧠 How it works

```mermaid
flowchart LR
    P["📱 Phone / PC<br/>dashboard"] -- "WiFi · HTTP" --> E
    subgraph E["ESP32-S3 · PaperPay"]
        W["Web server<br/>+ calculator"] --> Q["UPI QR<br/>generator"]
        Q --> D["2.9&quot; e-paper"]
        L["Payment log<br/>(LittleFS)"]
        T["Telegram task"]
    end
    D -- "shows QR" --> C["🧑 Customer<br/>UPI app"]
    C -- "pays" --> BANK["🏦 Bank / UPI"]
    BANK -- "credited SMS<br/>(forwarded)" --> T
    T -- "match amount → mark paid" --> L
    style E fill:#0f1115,stroke:#16b886,color:#e8edf2
    style Q fill:#10271f,stroke:#16b886,color:#16b886
    style D fill:#f6f6f0,stroke:#888,color:#111
```

<details>
<summary><b>💳 A sale, step by step</b></summary>

```mermaid
sequenceDiagram
    actor S as Shopkeeper
    participant W as Dashboard
    participant E as e-Paper
    actor C as Customer
    participant T as Telegram
    S->>W: Type amount → Generate QR
    W->>E: Show ₹ + UPI QR
    W-->>S: Show QR on phone too
    C->>E: Scan QR, pay in UPI app
    Note over C,T: Bank SMS forwarded to the bot
    T->>W: Amount matches open bill → mark PAID
    W->>E: "Paid ✓"
    W-->>S: Bill auto-closes
```
</details>

---

## 🔌 Wiring — Waveshare 2.9" → ESP32-S3

<details open>
<summary><b>Pin map</b> (defaults in <code>include/config.h</code>)</summary>

| e-Paper | Cable | ESP32-S3 | | e-Paper | Cable | ESP32-S3 |
|---|---|---|---|---|---|---|
| **VCC** | 🔴 | `3V3` | | **DC**  | 🟢 | `GPIO 9` |
| **GND** | ⚫ | `GND`  | | **RST** | ⚪ | `GPIO 8` |
| **DIN** | 🔵 | `GPIO 11` | | **BUSY**| 🟣 | `GPIO 7` |
| **CLK** | 🟡 | `GPIO 12` | | | | |
| **CS**  | 🟠 | `GPIO 10` | | | | |

> ⚠️ **VCC = 3.3 V only.** Power the board from a real wall charger, not a weak USB port.
</details>

---

## 🚀 Quick start

```bash
pio run -t uploadfs     # upload the dashboard (data/) to LittleFS
pio run -t upload       # flash the firmware
pio device monitor      # watch the logs
```

1. Power on → e-paper shows **Join AP `PaperPay-Setup`**.
2. Join that WiFi on your phone → enter your WiFi + **UPI ID / payee / shop name**.
3. It reboots, joins your WiFi, shows its IP → open `http://<that-ip>` and start billing.

> 💡 **N16R8 must build with `board_build.arduino.memory_type = qio_opi`** (octal PSRAM) — otherwise it boot-loops in `psramInit()`. Already set in [`platformio.ini`](platformio.ini).

<details>
<summary><b>🤖 Telegram setup</b></summary>

1. `@BotFather` → `/newbot` → copy the **token**.
2. Message your bot once; get your **Chat ID** (e.g. via `@userinfobot`).
3. Dashboard → **Settings → Telegram** → paste token + chat ID → **Enable** → **Save** → **Send test**.
4. Forward a bank/UPI payment SMS into the chat to auto-confirm a matching bill.

> Some ISPs DNS-block Telegram — the firmware connects to Telegram's IP directly to get around it.
</details>

<details>
<summary><b>🔗 REST API</b></summary>

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/api/state` | device + shop info |
| `GET/POST` | `/api/config` | shop + Telegram settings |
| `POST` | `/api/pay` | `{amount,note}` → create bill, show QR |
| `POST` | `/api/paid` · `/api/cancel` | `{id}` update a bill |
| `GET` | `/api/transactions` · `/api/txn?id=` | log / single bill |
| `GET` | `/api/qr.svg?data=` | on-screen QR (offline) |
| `GET` | `/api/wifi[/scan]` · `POST /api/wifi/connect` | WiFi management |
</details>

---

## 🛠️ Built with

<p align="center">
  <img src="https://img.shields.io/badge/C++-00599C?logo=cplusplus&logoColor=white" />
  <img src="https://img.shields.io/badge/GxEPD2-e--paper-555" />
  <img src="https://img.shields.io/badge/ESPAsyncWebServer-async-orange" />
  <img src="https://img.shields.io/badge/ArduinoJson-7-5b8" />
  <img src="https://img.shields.io/badge/WiFiManager-portal-blue" />
  <img src="https://img.shields.io/badge/LittleFS-storage-777" />
</p>

```
include/ + src/   config · qrpay · display · store · web · telegram · netctl · main
data/             dashboard SPA  (index.html · style.css · app.js)
assets/           README artwork
```

<p align="center"><sub>MIT — use it, fork it, run your shop on it. Made with ☕ + 🤖.</sub></p>

<img src="https://capsule-render.vercel.app/api?type=waving&color=0:16b886,100:0ea975&height=120&section=footer" width="100%"/>
