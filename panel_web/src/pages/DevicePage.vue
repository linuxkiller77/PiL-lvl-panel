<script setup>
import { computed, onMounted, onUnmounted, ref } from 'vue';
import { getDeviceStatus } from '../api/deviceApi.js';
import { getWifiStatus } from '../api/wifiApi.js';
import { getMqttStatus } from '../api/mqttApi.js';
import { getScriptStatus } from '../api/scriptApi.js';

const device = ref({});
const wifi = ref({});
const mqtt = ref({});
const script = ref({});
const error = ref('');
const lastRefresh = ref(null);
let timer = null;

function fmtBytes(v) {
  const n = Number(v || 0);
  if (!n) return '—';
  if (n < 1024) return `${n} B`;
  if (n < 1048576) return `${(n / 1024).toFixed(1)} KB`;
  return `${(n / 1048576).toFixed(2)} MB`;
}

function fmtUptime(ms) {
  const total = Math.max(0, Math.floor(Number(ms || 0) / 1000));
  if (!total) return '—';
  const d = Math.floor(total / 86400);
  const h = Math.floor((total % 86400) / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  if (d) return `${d}d ${h}h ${m}m`;
  if (h) return `${h}h ${m}m ${s}s`;
  if (m) return `${m}m ${s}s`;
  return `${s}s`;
}

function boolText(v) { return v ? 'Yes' : 'No'; }
function clean(v, fallback = '—') { return v === undefined || v === null || v === '' ? fallback : String(v); }
function pct(used, total) {
  const u = Number(used || 0);
  const t = Number(total || 0);
  if (!t) return 0;
  return Math.max(0, Math.min(100, Math.round((u / t) * 100)));
}

const internalFree = computed(() => Number(device.value.free_internal || 0));
const psramFree = computed(() => Number(device.value.free_psram || 0));
const internalTotalApprox = computed(() => Math.max(213 * 1024, internalFree.value));
const psramTotalApprox = computed(() => 8 * 1024 * 1024);
const internalUsedPct = computed(() => pct(internalTotalApprox.value - internalFree.value, internalTotalApprox.value));
const psramUsedPct = computed(() => pct(psramTotalApprox.value - psramFree.value, psramTotalApprox.value));

const runtimeState = computed(() => clean(device.value.hmi_runtime_state || device.value.runtime_state || 'UNKNOWN'));
const runtimeClass = computed(() => {
  const s = runtimeState.value.toUpperCase();
  if (s === 'READY') return 'ok';
  if (s.includes('ERROR') || s.includes('FAILED')) return 'bad';
  return 'warn';
});
const scriptState = computed(() => clean(device.value.script_compile_state || script.value.compile_state || script.value.state || 'unknown'));
const scriptValid = computed(() => device.value.script_runtime_valid !== false && script.value.runtime_valid !== false);
const mqttClass = computed(() => mqtt.value.connected ? 'ok' : mqtt.value.started ? 'warn' : 'neutral');
const wifiClass = computed(() => wifi.value.sta_connected ? 'ok' : wifi.value.sta_connecting ? 'warn' : 'neutral');

const headlineStats = computed(() => [
  { label: 'Runtime', value: runtimeState.value, tone: runtimeClass.value },
  { label: 'WiFi STA', value: wifi.value.sta_connected ? 'Connected' : wifi.value.sta_connecting ? 'Connecting' : 'Not connected', tone: wifiClass.value },
  { label: 'MQTT', value: mqtt.value.connected ? 'Connected' : mqtt.value.started ? 'Starting' : 'Stopped', tone: mqttClass.value },
  { label: 'Script', value: scriptValid.value ? clean(scriptState.value, 'OK') : 'Invalid', tone: scriptValid.value ? 'ok' : 'bad' },
]);

const deviceRows = computed(() => [
  ['Firmware', clean(device.value.firmware_version)],
  ['Build', clean(device.value.build_note)],
  ['Uptime', fmtUptime(device.value.uptime_ms)],
  ['Active widgets', clean(device.value.widgets)],
  ['Tags', clean(device.value.tags)],
  ['Runtime script', clean(device.value.script)],
]);

const networkRows = computed(() => [
  ['Hostname', clean(wifi.value.hostname || 'pilab-panel')],
  ['mDNS URL', clean(wifi.value.mdns_url || 'http://pilab-panel.local')],
  ['AP', wifi.value.started === false ? 'Stopped' : `${clean(wifi.value.ap_ssid || 'PiLab-Panel')} @ ${clean(wifi.value.ap_ip || '192.168.50.1')}`],
  ['STA SSID', clean(wifi.value.sta_ssid || wifi.value.ssid)],
  ['STA IP', clean(wifi.value.sta_ip)],
]);

const mqttRows = computed(() => [
  ['Broker', mqtt.value.broker ? `${mqtt.value.broker}:${mqtt.value.port || 1883}` : '—'],
  ['Base topic', clean(mqtt.value.baseTopic)],
  ['Mappings', `${Number(mqtt.value.publishCount || 0)} publish / ${Number(mqtt.value.subscribeCount || 0)} subscribe`],
  ['Traffic', `${Number(mqtt.value.txCount || 0)} TX / ${Number(mqtt.value.rxCount || 0)} RX / ${Number(mqtt.value.errorCount || 0)} errors`],
  ['Last RX', mqtt.value.lastRxTopic ? `${mqtt.value.lastRxTopic} = ${mqtt.value.lastRxPayload || ''}` : '—'],
  ['Last error', clean(mqtt.value.lastError)],
]);

const scriptRows = computed(() => [
  ['Compile state', clean(device.value.script_compile_state || script.value.compile_state || script.value.state)],
  ['Runtime valid', boolText(scriptValid.value)],
  ['OnPanelTick pending', boolText(script.value.panel_tick_pending || device.value.panel_tick_pending)],
  ['OnPanelTick missing', boolText(script.value.panel_tick_missing || device.value.panel_tick_missing)],
  ['Tick counters', `${Number(script.value.panel_tick_executed_count || device.value.panel_tick_executed_count || 0)} run / ${Number(script.value.panel_tick_skipped_count || device.value.panel_tick_skipped_count || 0)} skipped / ${Number(script.value.panel_tick_missing_count || device.value.panel_tick_missing_count || 0)} missing`],
]);

async function refresh() {
  const results = await Promise.allSettled([
    getDeviceStatus(),
    getWifiStatus(),
    getMqttStatus(),
    getScriptStatus(),
  ]);
  const [dev, w, m, s] = results;
  const failures = [];
  if (dev.status === 'fulfilled') device.value = dev.value || {}; else failures.push(`device: ${dev.reason?.message || dev.reason}`);
  if (w.status === 'fulfilled') wifi.value = w.value || {}; else failures.push(`wifi: ${w.reason?.message || w.reason}`);
  if (m.status === 'fulfilled') mqtt.value = m.value || {}; else failures.push(`mqtt: ${m.reason?.message || m.reason}`);
  if (s.status === 'fulfilled') script.value = s.value || {}; else failures.push(`script: ${s.reason?.message || s.reason}`);
  error.value = failures.join(' | ');
  lastRefresh.value = new Date();
}

onMounted(() => {
  refresh();
  timer = setInterval(refresh, 1500);
});

onUnmounted(() => {
  if (timer) clearInterval(timer);
});
</script>

<template>
  <main class="page device-page pro-device-page">
    <section class="status-strip">
      <article v-for="item in headlineStats" :key="item.label" class="status-tile" :class="item.tone">
        <span>{{ item.label }}</span>
        <b>{{ item.value }}</b>
      </article>
    </section>

    <section class="metrics-grid compact">
      <article class="metric-card memory-card">
        <header><span>Memory</span><b>Internal + PSRAM</b></header>
        <div class="memory-row">
          <div class="mem-label"><strong>Internal</strong><span>{{ fmtBytes(internalFree) }} free</span></div>
          <div class="bar"><i :style="{ width: internalUsedPct + '%' }"></i></div>
        </div>
        <div class="memory-row">
          <div class="mem-label"><strong>PSRAM</strong><span>{{ fmtBytes(psramFree) }} free</span></div>
          <div class="bar"><i :style="{ width: psramUsedPct + '%' }"></i></div>
        </div>
      </article>

      <article class="metric-card">
        <header><span>Runtime</span><b>{{ runtimeState }}</b></header>
        <table><tbody><tr v-for="[k,v] in deviceRows" :key="k"><th>{{ k }}</th><td>{{ v }}</td></tr></tbody></table>
      </article>

      <article class="metric-card">
        <header><span>Network</span><b>{{ wifi.sta_connected ? 'STA online' : 'AP available' }}</b></header>
        <table><tbody><tr v-for="[k,v] in networkRows" :key="k"><th>{{ k }}</th><td>{{ v }}</td></tr></tbody></table>
      </article>

      <article class="metric-card">
        <header><span>MQTT</span><b>{{ mqtt.connected ? 'Connected' : mqtt.started ? 'Started' : 'Stopped' }}</b></header>
        <table><tbody><tr v-for="[k,v] in mqttRows" :key="k"><th>{{ k }}</th><td>{{ v }}</td></tr></tbody></table>
      </article>

      <article class="metric-card">
        <header><span>AngelScript</span><b>{{ scriptValid ? 'Runtime valid' : 'Runtime invalid' }}</b></header>
        <table><tbody><tr v-for="[k,v] in scriptRows" :key="k"><th>{{ k }}</th><td>{{ v }}</td></tr></tbody></table>
      </article>
    </section>

    <section class="device-footer-row">
      <p v-if="error" class="status-bad">{{ error }}</p>
      <p v-else class="small">Updated {{ lastRefresh ? lastRefresh.toLocaleTimeString() : '—' }}</p>
      <button class="btn secondary compact-refresh" @click="refresh">Refresh</button>
      <details class="raw-details slim">
        <summary>Raw JSON</summary>
        <pre>{{ JSON.stringify({ device, wifi, mqtt, script }, null, 2) }}</pre>
      </details>
    </section>
  </main>
</template>

<style scoped>
.pro-device-page{
  padding:10px 14px 14px;
  overflow:auto;
  background:radial-gradient(circle at 18% 0%,rgba(56,189,248,.08),transparent 30%),#020617;
}
.status-strip{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px;margin-bottom:12px;}
.status-tile{
  min-width:0;
  border:1px solid rgba(148,163,184,.16);
  border-radius:14px;
  background:#0b1220;
  padding:10px 12px;
}
.status-tile span{display:block;font-size:10px;text-transform:uppercase;letter-spacing:.12em;color:#94a3b8;font-weight:750;}
.status-tile b{display:block;margin-top:4px;font-size:16px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:#e2e8f0;}
.status-tile.ok{border-color:rgba(34,197,94,.35);background:linear-gradient(180deg,rgba(20,83,45,.34),rgba(11,18,32,.96));}
.status-tile.warn{border-color:rgba(251,191,36,.35);background:linear-gradient(180deg,rgba(113,63,18,.30),rgba(11,18,32,.96));}
.status-tile.bad{border-color:rgba(239,68,68,.38);background:linear-gradient(180deg,rgba(127,29,29,.30),rgba(11,18,32,.96));}
.status-tile.neutral{border-color:rgba(148,163,184,.18);}
.metrics-grid.compact{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;align-items:start;}
.metric-card{
  min-width:0;
  border:1px solid rgba(148,163,184,.16);
  border-radius:14px;
  background:rgba(11,18,32,.92);
  padding:12px;
  box-shadow:inset 0 1px 0 rgba(255,255,255,.035);
}
.metric-card header{display:flex;align-items:baseline;justify-content:space-between;gap:10px;margin-bottom:8px;border-bottom:1px solid rgba(148,163,184,.10);padding-bottom:8px;}
.metric-card header span{font-size:11px;text-transform:uppercase;letter-spacing:.13em;color:#38bdf8;font-weight:850;}
.metric-card header b{font-size:12px;color:#cbd5e1;text-align:right;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}
.metric-card table{width:100%;border-collapse:collapse;font-size:12px;}
.metric-card th,.metric-card td{border-bottom:1px solid rgba(148,163,184,.08);padding:5px 0;vertical-align:top;}
.metric-card tr:last-child th,.metric-card tr:last-child td{border-bottom:0;}
.metric-card th{width:130px;color:#94a3b8;text-align:left;font-weight:650;padding-right:10px;}
.metric-card td{color:#e2e8f0;word-break:break-word;}
.memory-card{grid-column:span 2;}
.memory-row{display:grid;grid-template-columns:170px 1fr;gap:12px;align-items:center;margin:8px 0;}
.mem-label{display:flex;justify-content:space-between;gap:10px;align-items:baseline;}
.mem-label strong{font-size:12px;color:#e2e8f0;}
.mem-label span{font-size:12px;color:#94a3b8;}
.bar{height:10px;border-radius:999px;border:1px solid rgba(148,163,184,.14);background:#020617;overflow:hidden;}
.bar i{display:block;height:100%;border-radius:inherit;background:linear-gradient(90deg,#0ea5e9,#22c55e);box-shadow:0 0 14px rgba(56,189,248,.25);}
.device-footer-row{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;margin-top:12px;}
.raw-details.slim{min-width:min(520px,60vw);}
.raw-details.slim summary{cursor:pointer;color:#38bdf8;font-size:12px;}
.raw-details.slim pre{margin-top:8px;max-height:320px;overflow:auto;white-space:pre-wrap;background:#020617;border:1px solid rgba(148,163,184,.16);border-radius:10px;padding:10px;color:#94a3b8;font-size:11px;}
.compact-refresh{padding:6px 10px;font-size:12px;}
@media(max-width:980px){.status-strip{grid-template-columns:repeat(2,minmax(0,1fr));}.metrics-grid.compact{grid-template-columns:1fr}.memory-card{grid-column:span 1}.memory-row{grid-template-columns:1fr}.device-footer-row{display:block}.raw-details.slim{min-width:0}}
</style>
