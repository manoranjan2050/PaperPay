// ============================================================================
//  PaperPay dashboard logic
// ============================================================================
const $ = (id) => document.getElementById(id);
const api = (p, opt) => fetch(p, opt).then(r => r.json());

let CFG = { currency: "Rs", gst: 0 };
let amountStr = "0";
let currentTxn = null;

// ---- amount entry -----------------------------------------------------------
function amountVal() { return parseFloat(amountStr || "0") || 0; }
function gstVal() {
  if (!$("gstChk").checked) return 0;
  return +(amountVal() * (CFG.gst / 100)).toFixed(2);
}
function totalVal() { return +(amountVal() + gstVal()).toFixed(2); }

function renderAmount() {
  $("amount").textContent = amountVal().toFixed(2);
  const g = gstVal();
  $("gstAmt").textContent = g ? "+ " + CFG.currency + g.toFixed(2) : "";
}

document.querySelectorAll(".key").forEach(k => k.onclick = () => {
  const v = k.dataset.k;
  if (v === "." && amountStr.includes(".")) return;
  if (amountStr === "0" && v !== ".") amountStr = "";
  // cap to 2 decimals
  if (amountStr.includes(".") && amountStr.split(".")[1]?.length >= 2 && v !== ".") return;
  amountStr += v;
  renderAmount();
});
$("clearBtn").onclick = () => { amountStr = "0"; renderAmount(); };
$("backBtn").onclick  = () => { amountStr = amountStr.slice(0, -1) || "0"; renderAmount(); };
$("gstChk").onchange  = renderAmount;

// ---- charge -> QR -----------------------------------------------------------
$("chargeBtn").onclick = async () => {
  const amount = totalVal();
  if (amount <= 0) return toast("Enter an amount first");
  const note = $("noteInp").value.trim();
  const res = await api("/api/pay", {
    method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ amount, note })
  });
  if (res.error) return toast(res.error);
  currentTxn = res.id;
  $("ovAmt").textContent = CFG.currency + amount.toFixed(2);
  $("qrbox").innerHTML = `<img src="/api/qr.svg?data=${encodeURIComponent(res.upi)}" alt="QR"/>`;
  $("ovOpen").href = res.upi;
  $("overlay").classList.remove("hidden");
  startTxnWatch();   // auto-close + mark paid if Telegram confirms the payment
};

// Poll the open bill's status; if it becomes paid (e.g. via Telegram), reflect it.
let txnWatch;
function startTxnWatch() {
  clearInterval(txnWatch);
  txnWatch = setInterval(async () => {
    if (currentTxn == null) return clearInterval(txnWatch);
    try {
      const t = await api("/api/txn?id=" + currentTxn);
      if (t.status === 1) {            // paid
        clearInterval(txnWatch);
        closeOverlay(); toast("Payment received ✓");
        amountStr = "0"; renderAmount(); $("noteInp").value = "";
        loadTxns();
      } else if (t.status === 2) {     // cancelled elsewhere
        clearInterval(txnWatch); closeOverlay(); loadTxns();
      }
    } catch (e) {}
  }, 2500);
}

$("ovPaid").onclick = async () => {
  await api("/api/paid", { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ id: currentTxn }) });
  closeOverlay(); toast("Payment marked paid ✓");
  amountStr = "0"; renderAmount(); $("noteInp").value = "";
  loadTxns();
};
$("ovCancel").onclick = async () => {
  await api("/api/cancel", { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ id: currentTxn }) });
  closeOverlay(); loadTxns();
};
function closeOverlay() { $("overlay").classList.add("hidden"); currentTxn = null; clearInterval(txnWatch); }
$("overlay").addEventListener("click", e => { if (e.target.id === "overlay") closeOverlay(); });

// ---- transactions / payments ------------------------------------------------
function isToday(ts) {
  if (!ts) return false;
  const d = new Date(ts * 1000), n = new Date();
  return d.getFullYear() === n.getFullYear() && d.getMonth() === n.getMonth() && d.getDate() === n.getDate();
}
function fmtTime(ts) {
  if (!ts) return "—";
  return new Date(ts * 1000).toLocaleString();
}
const STAT = { 0: ["pending", "Pending"], 1: ["paid", "Paid"], 2: ["cancel", "Cancelled"] };

async function loadTxns() {
  const list = await api("/api/transactions");
  list.sort((a, b) => b.id - a.id);

  let total = 0, count = 0, pend = 0;
  for (const t of list) {
    if (t.status === 1 && isToday(t.ts)) { total += t.amount; count++; }
    if (t.status === 0) pend++;
  }
  $("todayTotal").textContent = CFG.currency + total.toFixed(0);
  $("todayCount").textContent = count;
  $("pendCount").textContent  = pend;

  $("txList").innerHTML = list.map(t => {
    const [cls, label] = STAT[t.status] || STAT[0];
    const actions = t.status === 0
      ? `<button class="btn primary mini" onclick="mark(${t.id},1)">Paid</button>
         <button class="btn danger mini" onclick="mark(${t.id},2)">✕</button>`
      : "";
    return `<div class="tx">
      <div class="meta">
        <div class="amt">${CFG.currency}${t.amount.toFixed(2)}</div>
        <div class="nt">${t.note || "Bill #" + t.id} · ${fmtTime(t.ts)}</div>
      </div>
      <span class="pill ${cls}">${label}</span>
      ${actions}
    </div>`;
  }).join("") || `<p class="muted">No payments yet.</p>`;
}
window.mark = async (id, status) => {
  await api(status === 1 ? "/api/paid" : "/api/cancel", {
    method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ id }) });
  loadTxns();
};

// ---- settings ---------------------------------------------------------------
async function loadConfig() {
  const c = await api("/api/config");
  CFG = c;
  $("cVpa").value = c.vpa; $("cPayee").value = c.payee; $("cShop").value = c.shopName;
  $("cCur").value = c.currency; $("cGst").value = c.gst; $("cTz").value = c.tzOffset;
  $("cTgTok").value = c.tgToken || ""; $("cTgChat").value = c.tgChat || "";
  $("cTgEn").checked = !!c.tgEnable; $("cTgAuto").checked = !!c.tgAutoPaid;
  $("gstPct").textContent = c.gst;
  renderAmount();
}
$("saveCfg").onclick = async () => {
  const body = {
    vpa: $("cVpa").value.trim(), payee: $("cPayee").value.trim(),
    shopName: $("cShop").value.trim(), currency: $("cCur").value.trim() || "Rs",
    gst: parseFloat($("cGst").value) || 0, tzOffset: parseInt($("cTz").value) || 19800,
    tgToken: $("cTgTok").value.trim(), tgChat: $("cTgChat").value.trim(),
    tgEnable: $("cTgEn").checked, tgAutoPaid: $("cTgAuto").checked
  };
  const r = await api("/api/config", { method: "POST",
    headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  if (r.ok) { toast("Saved ✓"); loadState(); loadConfig(); }
};
$("tgTest").onclick = async () => {
  await fetch("/api/telegram/test", { method: "POST" });
  toast("Test sent — check Telegram");
};

// ---- WiFi management --------------------------------------------------------
async function loadWifiInfo() {
  try {
    const w = await api("/api/wifi");
    $("wifiNow").innerHTML = w.connected
      ? `Connected to <b>${w.ssid}</b> · ${w.ip} · ${w.rssi}dBm`
      : "Not connected";
  } catch (e) {}
}
$("wifiScan").onclick = async () => {
  $("wifiList").innerHTML = `<p class="muted small">Scanning…</p>`;
  for (let i = 0; i < 8; i++) {
    const r = await api("/api/wifi/scan");
    if (!r.scanning) {
      const nets = (r.nets || []).sort((a, b) => b.rssi - a.rssi);
      $("wifiList").innerHTML = nets.map(n =>
        `<div class="wifi" onclick="pickWifi('${n.ssid.replace(/'/g, "\\'")}')">
           <span>${n.lock ? "🔒" : "📶"}</span><span>${n.ssid}</span>
           <span class="grow"></span><span class="muted small">${n.rssi}dBm</span>
         </div>`).join("") || `<p class="muted small">No networks found.</p>`;
      return;
    }
    await new Promise(r => setTimeout(r, 1000));
  }
  $("wifiList").innerHTML = `<p class="muted small">Scan timed out, try again.</p>`;
};
window.pickWifi = (ssid) => { $("wifiSsid").value = ssid; $("wifiPass").focus(); };
$("wifiConnect").onclick = async () => {
  const ssid = $("wifiSsid").value.trim();
  if (!ssid) return toast("Pick or type a network");
  await api("/api/wifi/connect", { method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ ssid, password: $("wifiPass").value }) });
  toast("Switching WiFi… the device may drop offline briefly");
};

// ---- device actions ---------------------------------------------------------
$("btnReboot").onclick = async () => {
  if (!confirm("Reboot the device?")) return;
  await fetch("/api/reboot", { method: "POST" }); toast("Rebooting…");
};
$("btnResetWifi").onclick = async () => {
  if (!confirm("Forget WiFi and reopen the setup hotspot?")) return;
  await fetch("/api/wifi/reset", { method: "POST" }); toast("Resetting WiFi…");
};

// ---- state / header ---------------------------------------------------------
async function loadState() {
  try {
    const s = await api("/api/state");
    $("shopName").textContent = s.shopName || "PaperPay";
    $("dot").className = "dot " + (s.connected ? "ok" : "bad");
    $("status").innerHTML = `<span class="dot ${s.connected ? "ok" : "bad"}"></span>` +
      (s.configured ? `${s.ip} · ${s.rssi}dBm` : "⚠ Set your UPI ID in Settings");
  } catch (e) {
    $("dot").className = "dot bad";
  }
}

// ---- tabs -------------------------------------------------------------------
document.querySelectorAll(".tabbtn").forEach(b => b.onclick = () => {
  document.querySelectorAll(".tabbtn").forEach(x => x.classList.remove("active"));
  document.querySelectorAll(".tab").forEach(x => x.classList.remove("active"));
  b.classList.add("active");
  $("tab-" + b.dataset.tab).classList.add("active");
  if (b.dataset.tab === "pay") loadTxns();
  if (b.dataset.tab === "set") loadWifiInfo();
});

// keep the Payments tab live so Telegram auto-confirmations show up
setInterval(() => {
  if ($("tab-pay").classList.contains("active")) loadTxns();
}, 5000);

// ---- toast ------------------------------------------------------------------
let toastT;
function toast(msg) {
  let el = $("_toast");
  if (!el) { el = document.createElement("div"); el.id = "_toast"; el.className = "toast"; document.body.appendChild(el); }
  el.textContent = msg; el.classList.add("show");
  clearTimeout(toastT); toastT = setTimeout(() => el.classList.remove("show"), 2200);
}

// ---- boot -------------------------------------------------------------------
loadConfig().then(loadState).then(loadTxns);
setInterval(loadState, 15000);
