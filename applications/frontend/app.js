// Dashboard do ESP32 Wi-Fi IDS: histórico via REST e tempo real via WebSocket.
// Todo texto vindo dos dados entra no DOM por textContent (nunca innerHTML).
"use strict";

const SERIES = {
  traffic: [
    { key: "beacon", label: "Beacon", color: "--series-1" },
    { key: "probe_req", label: "Probe request", color: "--series-2" },
  ],
  mgmt: [
    { key: "deauth", label: "Deauth", color: "--series-3" },
    { key: "disassoc", label: "Disassoc", color: "--series-4" },
  ],
};

const state = {
  devices: new Map(),
  device: null,
  minutes: 60,
  source: "",
  stats: [],
  alerts: [],
  backendMqtt: null,
  wsOpen: false,
};

const $ = (id) => document.getElementById(id);
const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

// ---------- formatação ----------

const fmtTime = (iso) => new Date(iso).toLocaleTimeString("pt-BR");
const fmtDateTime = (iso) => new Date(iso).toLocaleString("pt-BR");
function fmtUptime(ms) {
  if (ms == null) return "—";
  const s = Math.floor(ms / 1000);
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), r = s % 60;
  return h ? `${h}h ${String(m).padStart(2, "0")}m` : `${m}m ${String(r).padStart(2, "0")}s`;
}
function fmtAge(sec) {
  if (sec == null) return "sem contato";
  if (sec < 60) return `há ${Math.round(sec)} s`;
  if (sec < 3600) return `há ${Math.round(sec / 60)} min`;
  return `há ${Math.round(sec / 3600)} h`;
}
const maskBssid = (b) => {
  const p = String(b).split(":");
  return p.length === 6 ? `${p[0]}:${p[1]}:${p[2]}:••:••:${p[5]}` : "—";
};

// ---------- gráficos ----------

// Linha vertical que acompanha o ponteiro (o crosshair encontra o X).
const crosshair = {
  id: "crosshair",
  afterDatasetsDraw(chart) {
    const active = chart.tooltip?.getActiveElements?.() || [];
    if (!active.length) return;
    const x = active[0].element.x;
    const { top, bottom } = chart.chartArea;
    const ctx = chart.ctx;
    ctx.save();
    ctx.strokeStyle = css("--axis");
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(x, top);
    ctx.lineTo(x, bottom);
    ctx.stroke();
    ctx.restore();
  },
};

// Rótulo direto no fim de cada linha (nome + último valor), em cor de texto.
const endLabels = {
  id: "endLabels",
  afterDatasetsDraw(chart) {
    const ctx = chart.ctx;
    ctx.save();
    ctx.font = "12px system-ui, -apple-system, sans-serif";
    ctx.textBaseline = "middle";
    const { top, bottom, right } = chart.chartArea;
    const items = [];
    chart.data.datasets.forEach((ds, i) => {
      const meta = chart.getDatasetMeta(i);
      const n = ds.data.length;
      if (!n || ds.data[n - 1] == null) return;
      items.push({ y: meta.data[n - 1].y, text: `${ds.label} ${ds.data[n - 1]}` });
    });
    // Separa rótulos sobrepostos (14 px) e mantém todos dentro da área do gráfico, empurrando para cima.
    items.sort((a, b) => a.y - b.y);
    for (let i = 1; i < items.length; i++) items[i].y = Math.max(items[i].y, items[i - 1].y + 14);
    for (let i = items.length - 1; i >= 0; i--) {
      const limit = i === items.length - 1 ? bottom - 6 : items[i + 1].y - 14;
      items[i].y = Math.max(top + 6, Math.min(items[i].y, limit));
    }
    ctx.fillStyle = css("--text-secondary");
    for (const it of items) ctx.fillText(it.text, right + 8, it.y);
    ctx.restore();
  },
};

function makeChart(canvasId, series, suggestedMax) {
  return new Chart($(canvasId), {
    type: "line",
    data: { labels: [], datasets: series.map((s) => ({ label: s.label, data: [], _color: s.color })) },
    options: {
      animation: false,
      maintainAspectRatio: false,
      interaction: { mode: "index", intersect: false },
      layout: { padding: { right: 110 } },
      elements: { point: { radius: 0, hoverRadius: 4, hitRadius: 12 }, line: { borderWidth: 2, tension: 0 } },
      plugins: {
        legend: { display: false },
        tooltip: { callbacks: { label: (c) => ` ${c.formattedValue}  ${c.dataset.label}` } },
      },
      scales: {
        x: { ticks: { maxTicksLimit: 6, maxRotation: 0 }, grid: { display: false } },
        y: { beginAtZero: true, suggestedMax, ticks: { precision: 0, maxTicksLimit: 5 } },
      },
    },
    plugins: [crosshair, endLabels],
  });
}

const charts = {};

function applyTheme() {
  for (const [name, chart] of Object.entries(charts)) {
    chart.data.datasets.forEach((ds) => {
      ds.borderColor = css(ds._color);
      ds.backgroundColor = css(ds._color);
    });
    const o = chart.options;
    o.scales.x.ticks.color = o.scales.y.ticks.color = css("--muted");
    o.scales.y.grid.color = css("--grid");
    o.scales.x.border = o.scales.y.border = { color: css("--axis") };
    chart.update("none");
    renderLegend(name);
  }
}

function renderLegend(name) {
  const box = $(`legend-${name}`);
  box.replaceChildren();
  for (const s of SERIES[name]) {
    const item = document.createElement("span");
    const key = document.createElement("i");
    key.style.background = css(s.color);
    item.append(key, document.createTextNode(s.label));
    box.append(item);
  }
}

// Insere um ponto vazio onde faltaram janelas (broker fora, dispositivo desligado): a linha mostra o buraco
// em vez de ligar os dois lados como se nada tivesse acontecido.
function withGaps(rows) {
  const out = [];
  rows.forEach((r, i) => {
    const prev = rows[i - 1];
    if (prev) {
      const dt = new Date(r.received_at) - new Date(prev.received_at);
      if (dt > (r.window_s || 10) * 1000 * 1.8 || r.boot_id !== prev.boot_id) out.push(null);
    }
    out.push(r);
  });
  return out;
}

function renderCharts() {
  const rows = withGaps(state.stats);
  const labels = rows.map((r) => (r ? fmtTime(r.received_at) : ""));
  for (const [name, chart] of Object.entries(charts)) {
    chart.data.labels = labels;
    SERIES[name].forEach((s, i) => (chart.data.datasets[i].data = rows.map((r) => (r ? r.counts[s.key] : null))));
    chart.update("none");
  }
  // Tabela (mesmos dados dos gráficos, mais recentes primeiro).
  const body = $("windows-body");
  body.replaceChildren();
  for (const r of [...state.stats].reverse().slice(0, 500)) {
    const tr = document.createElement("tr");
    const cells = [fmtTime(r.received_at), r.counts.beacon, r.counts.probe_req, r.counts.deauth, r.counts.disassoc,
      r.rssi ?? "—", r.channel];
    cells.forEach((v, i) => {
      const td = document.createElement("td");
      td.textContent = v;
      if (i > 0) td.className = "num";
      tr.append(td);
    });
    body.append(tr);
  }
}

// ---------- alertas ----------

function alertRow(a, fresh) {
  const tr = document.createElement("tr");
  if (fresh) tr.className = "fresh";
  const td = (text, cls) => {
    const c = document.createElement("td");
    c.textContent = text;
    if (cls) c.className = cls;
    tr.append(c);
    return c;
  };
  td(fmtDateTime(a.received_at));
  td(a.rule);
  const sev = td("");
  const span = document.createElement("span");
  span.className = `sev sev-${a.severity}`;
  span.textContent = a.severity === "high" ? "alta" : "média";
  sev.append(span);
  td(maskBssid(a.bssid)).title = "BSSID mascarado";
  td(a.count, "num");
  td(`${a.window_s} s`, "num");
  const src = td("");
  const badge = document.createElement("span");
  badge.className = `badge ${a.source === "synthetic" ? "badge-synthetic" : ""}`;
  badge.textContent = a.source;
  src.append(badge);
  const ev = td("");
  const code = document.createElement("code");
  code.textContent = a.event_id;
  ev.append(code);
  return tr;
}

function renderAlerts() {
  const body = $("alerts-body");
  body.replaceChildren();
  if (!state.alerts.length) {
    const tr = document.createElement("tr");
    tr.className = "empty";
    const c = document.createElement("td");
    c.colSpan = 8;
    c.textContent = "Nenhum alerta no período.";
    tr.append(c);
    body.append(tr);
  } else {
    for (const a of state.alerts) body.append(alertRow(a, false));
  }
  renderAlertTile();
}

function renderAlertTile() {
  const live = state.alerts.filter((a) => a.source === "live").length;
  $("t-alerts").textContent = state.alerts.length;
  $("t-alerts-note").textContent = `${live} live · ${state.alerts.length - live} sintéticos`;
}

// ---------- estado do dispositivo ----------

function renderTiles() {
  const d = state.devices.get(state.device);
  const last = state.stats[state.stats.length - 1];
  const st = d?.last_status;
  $("synthetic-banner").hidden = !d?.synthetic;

  // Idade do último contato recalculada no navegador a partir de last_seen_at.
  let age = null;
  if (d?.last_seen_at) age = (Date.now() - new Date(d.last_seen_at).getTime()) / 1000;
  const offlineAfter = state.offlineAfter || 75;
  const online = d && d.state === "online" && age != null && age <= offlineAfter;
  const tile = document.querySelector(".tile-state");
  tile.dataset.state = d ? (online ? "online" : "offline") : "";
  $("t-state").lastElementChild.textContent = d ? (online ? "Online" : "Offline") : "—";
  let why = fmtAge(age);
  if (d && !online) why += d.state === "offline" ? " · Last Will recebido" : " · sem heartbeat";
  $("t-seen").textContent = d ? `último contato ${why}` : "sem dados";

  const channel = last?.channel ?? st?.channel;
  const rssi = last?.rssi ?? st?.rssi;
  $("t-channel").textContent = channel ?? "—";
  $("t-rssi").textContent = rssi != null ? `RSSI ${rssi} dBm` : "RSSI —";
  $("t-uptime").textContent = fmtUptime(last?.uptime_ms ?? st?.uptime_ms);
  $("t-boot").textContent = `boot ${d?.boot_id ?? "—"}`;
  $("t-drops").textContent = last ? last.capture_queue_drops ?? 0 : "—";
  $("t-mqttfail").textContent = last ? last.mqtt_enqueue_failures ?? 0 : "—";
  $("t-reconnects").textContent = `reconexões Wi-Fi ${last?.wifi_reconnects ?? "—"}`;
}

function renderConn() {
  const el = $("conn");
  if (!state.wsOpen) {
    el.dataset.state = "bad";
    $("conn-text").textContent = "sem conexão com o back-end";
  } else if (state.backendMqtt === false) {
    el.dataset.state = "warn";
    $("conn-text").textContent = "back-end sem broker MQTT";
  } else {
    el.dataset.state = "ok";
    $("conn-text").textContent = "tempo real ativo";
  }
}

function renderDeviceSelect() {
  const sel = $("device");
  const current = state.device;
  sel.replaceChildren();
  for (const d of state.devices.values()) {
    const opt = document.createElement("option");
    opt.value = d.device_id;
    opt.textContent = d.synthetic ? `${d.device_id} (teste)` : d.device_id;
    sel.append(opt);
  }
  if (!state.devices.size) {
    const opt = document.createElement("option");
    opt.textContent = "aguardando dispositivo…";
    sel.append(opt);
  }
  sel.value = current ?? "";
}

// ---------- carga de dados ----------

async function getJSON(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url}: ${r.status}`);
  return r.json();
}

async function loadDevices() {
  const list = await getJSON("/api/devices");
  state.devices = new Map(list.map((d) => [d.device_id, d]));
  if (!state.device || !state.devices.has(state.device)) {
    const real = list.find((d) => !d.synthetic);
    state.device = (real || list[0])?.device_id ?? null;
  }
  renderDeviceSelect();
}

async function loadHistory() {
  document.querySelector(".charts").style.opacity = 0.5;  // mantém o quadro enquanto recarrega
  const q = new URLSearchParams({ minutes: state.minutes });
  if (state.device) q.set("device_id", state.device);
  const qa = new URLSearchParams(q);
  if (state.source) qa.set("source", state.source);
  try {
    const [stats, alerts, health] = await Promise.all([
      getJSON(`/api/stats?${q}`), getJSON(`/api/alerts?${qa}`), getJSON("/api/health"),
    ]);
    state.stats = stats;
    state.alerts = alerts;
    state.offlineAfter = health.offline_after_s;
    state.backendMqtt = health.mqtt_connected;
    renderCharts();
    renderAlerts();
    renderTiles();
    renderConn();
    $("footer").textContent = `Banco: ${health.db.stats} janelas, ${health.db.alerts} alertas · ` +
      `ingestão: ${health.ingest.accepted} aceitas, ${health.ingest.duplicates} duplicadas, ` +
      `${health.ingest.rejected} rejeitadas · broker ${health.mqtt_broker}`;
  } finally {
    document.querySelector(".charts").style.opacity = 1;
  }
}

async function reloadAll() {
  await loadDevices();
  await loadHistory();
}

// ---------- tempo real ----------

function onEvent(ev) {
  if (ev.kind === "backend") {
    state.backendMqtt = ev.data.mqtt_connected;
    renderConn();
    return;
  }
  const p = ev.data;
  const dev = state.devices.get(p.device_id);
  if (!dev) {  // dispositivo novo: recarrega a lista
    reloadAll();
    return;
  }
  if (ev.kind === "status") {
    dev.state = p.state;
    dev.boot_id = p.boot_id;
    dev.last_status = p;
    if (!ev.retained) dev.last_seen_at = ev.received_at;
  } else {
    dev.state = "online";
    dev.boot_id = p.boot_id;
    dev.last_seen_at = ev.received_at;
  }
  if (p.device_id !== state.device) return;

  if (ev.kind === "stats") {
    state.stats.push({ ...p, received_at: ev.received_at });
    const cutoff = Date.now() - state.minutes * 60_000;
    while (state.stats.length && new Date(state.stats[0].received_at).getTime() < cutoff) state.stats.shift();
    renderCharts();
  } else if (ev.kind === "alert" && (!state.source || state.source === p.source)) {
    const a = { ...p, received_at: ev.received_at };
    state.alerts.unshift(a);
    const body = $("alerts-body");
    if (body.querySelector("tr.empty")) body.replaceChildren();
    body.prepend(alertRow(a, true));
    renderAlertTile();
  }
  renderTiles();
}

function connectWs(delay = 1000) {
  const ws = new WebSocket(`${location.protocol === "https:" ? "wss" : "ws"}://${location.host}/ws`);
  ws.onopen = () => {
    state.wsOpen = true;
    renderConn();
    reloadAll();  // recupera o que chegou enquanto estava desconectado
    delay = 1000;
  };
  ws.onmessage = (m) => onEvent(JSON.parse(m.data));
  ws.onclose = () => {
    state.wsOpen = false;
    renderConn();
    setTimeout(() => connectWs(Math.min(delay * 2, 15000)), delay);
  };
}

// ---------- inicialização ----------

function init() {
  charts.traffic = makeChart("chart-traffic", SERIES.traffic);
  charts.mgmt = makeChart("chart-mgmt", SERIES.mgmt, 5);  // com tudo em 0 a escala não colapsa em 0..1
  applyTheme();
  matchMedia("(prefers-color-scheme: dark)").addEventListener("change", applyTheme);

  $("device").addEventListener("change", (e) => {
    state.device = e.target.value;
    loadHistory();
  });
  $("source").addEventListener("change", (e) => {
    state.source = e.target.value;
    loadHistory();
  });
  document.querySelectorAll(".range button").forEach((b) =>
    b.addEventListener("click", () => {
      document.querySelectorAll(".range button").forEach((x) => x.setAttribute("aria-pressed", "false"));
      b.setAttribute("aria-pressed", "true");
      state.minutes = Number(b.dataset.minutes);
      loadHistory();
    }),
  );

  setInterval(renderTiles, 2000);  // "último contato há X s" e detecção de offline sem esperar evento
  connectWs();
}

document.addEventListener("DOMContentLoaded", init);
