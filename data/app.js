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
};

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
function closeOverlay() { $("overlay").classList.add("hidden"); currentTxn = null; }
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
  $("gstPct").textContent = c.gst;
  renderAmount();
}
$("saveCfg").onclick = async () => {
  const body = {
    vpa: $("cVpa").value.trim(), payee: $("cPayee").value.trim(),
    shopName: $("cShop").value.trim(), currency: $("cCur").value.trim() || "Rs",
    gst: parseFloat($("cGst").value) || 0, tzOffset: parseInt($("cTz").value) || 19800
  };
  const r = await api("/api/config", { method: "POST",
    headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  if (r.ok) { toast("Saved ✓"); loadState(); loadConfig(); }
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
});

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
