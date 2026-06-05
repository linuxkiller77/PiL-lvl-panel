<script setup>
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { getTags } from '../api/tagApi.js';
import { getMqttConfig, saveMqttConfig, getMqttStatus, startMqtt, stopMqtt, testMqttPublish } from '../api/mqttApi.js';
import { designerTags, mqttConfigExternalRevision, mqttConfig, mqttLoaded, mqttDirty, mqttSourceLabel } from '../stores/panelStore.js';

const RATE_OPTIONS = [100, 250, 500, 1000, 5000, 10000, 30000, 60000];
const qosOptions = [0, 1];
const busy = ref(false);
const msg = ref('');
const config = mqttConfig;
config.value = config.value || defaultConfig();
const status = ref({});
const tagFilter = ref('');
const activeTagPicker = ref({ kind: '', index: -1, original: '' });
const tagEditSession = ref({ kind: '', index: -1, original: '' });
const testTopic = ref('test');
const testPayload = ref('hello from PiLab Panel');
let statusTimer = null;

function defaultServer(i = 0) {
  return {
    name: i === 0 ? 'Local Broker' : `Broker ${i + 1}`,
    enabled: i === 0,
    autoConnect: i === 0,
    host: i === 0 ? '192.168.1.50' : '',
    port: 1883,
    useTls: false,
    username: '',
    password: '',
    clientId: 'pilab-panel',
    baseTopic: 'pilab/panel/pilab-panel',
  };
}
function defaultConfig() {
  return {
    version: 1,
    enabled: false,
    activeServer: 0,
    servers: [defaultServer(0), defaultServer(1), defaultServer(2)],
    publish: [
      { enabled: true, server: 0, tag: 'Panel_Heartbeat', topic: 'tag/Panel_Heartbeat/state', rateMs: 1000, qos: 0, retain: false },
      { enabled: true, server: 0, tag: 'Panel_Uptime_s', topic: 'tag/Panel_Uptime_s/state', rateMs: 1000, qos: 0, retain: false },
    ],
    subscribe: [
      { enabled: true, server: 0, tag: 'Run_Command', topic: 'tag/Run_Command/set', qos: 0 },
    ],
  };
}
function normalizeServer(s, i) { return { ...defaultServer(i), ...(s || {}), port: Number(s?.port || 1883) || 1883 }; }
function normalizeConfig(raw) {
  const base = defaultConfig();
  const next = { ...base, ...(raw || {}) };
  next.servers = Array.from({ length: 3 }, (_, i) => normalizeServer(raw?.servers?.[i], i));
  next.activeServer = Math.max(0, Math.min(2, Number(next.activeServer || 0)));
  next.publish = Array.isArray(raw?.publish) ? raw.publish.map(normalizePublish).filter(r => r.tag || r.topic) : base.publish;
  next.subscribe = Array.isArray(raw?.subscribe) ? raw.subscribe.map(normalizeSubscribe).filter(r => r.tag || r.topic) : base.subscribe;
  return next;
}
function normalizePublish(r = {}) {
  return {
    enabled: r.enabled !== false,
    server: Math.max(0, Math.min(2, Number(r.server || 0))),
    tag: String(r.tag || ''),
    topic: String(r.topic || ''),
    rateMs: RATE_OPTIONS.includes(Number(r.rateMs)) ? Number(r.rateMs) : 1000,
    qos: Number(r.qos || 0) === 1 ? 1 : 0,
    retain: !!r.retain,
  };
}
function normalizeSubscribe(r = {}) {
  return {
    enabled: r.enabled !== false,
    server: Math.max(0, Math.min(2, Number(r.server || 0))),
    tag: String(r.tag || ''),
    topic: String(r.topic || ''),
    qos: Number(r.qos || 0) === 1 ? 1 : 0,
  };
}
const tagNames = computed(() => (designerTags.value || []).map(t => t.name).filter(Boolean).sort((a, b) => a.localeCompare(b)));
const filteredTagNames = computed(() => {
  const q = tagFilter.value.trim().toLowerCase();
  if (!q) return tagNames.value.slice(0, 60);
  return tagNames.value.filter(n => n.toLowerCase().includes(q)).slice(0, 60);
});
const activeServer = computed(() => config.value.servers?.[config.value.activeServer] || defaultServer(0));

function isTagPickerOpen(kind, index) {
  return activeTagPicker.value.kind === kind && activeTagPicker.value.index === index;
}
function currentTagValue(kind, index) {
  const rows = kind === 'publish' ? config.value.publish : config.value.subscribe;
  return String(rows?.[index]?.tag || '');
}
function setTagValue(kind, index, value) {
  if (kind === 'publish' && config.value.publish?.[index]) config.value.publish[index].tag = value;
  if (kind === 'subscribe' && config.value.subscribe?.[index]) config.value.subscribe[index].tag = value;
}
function isSamePicker(a, kind, index) {
  return a.kind === kind && a.index === index;
}
function beginTagEdit(kind, index) {
  if (!isSamePicker(tagEditSession.value, kind, index)) {
    tagEditSession.value = { kind, index, original: currentTagValue(kind, index) };
  }
}
function finishTagEdit() {
  tagEditSession.value = { kind: '', index: -1, original: '' };
}
function openTagPicker(kind, index) {
  if (!isSamePicker(activeTagPicker.value, kind, index)) {
    finishTagEdit();
  }
  beginTagEdit(kind, index);
  activeTagPicker.value = { kind, index, original: tagEditSession.value.original };
}
function closeTagPicker() {
  activeTagPicker.value = { kind: '', index: -1, original: '' };
}
function cancelTagPicker() {
  const src = tagEditSession.value.kind ? tagEditSession.value : activeTagPicker.value;
  const { kind, index, original } = src;
  if (kind && index >= 0) setTagValue(kind, index, original || '');
  closeTagPicker();
  finishTagEdit();
}
function handleTagFieldKeydown(kind, index, event) {
  if (event.key === 'Escape') {
    event.preventDefault();
    event.stopPropagation();
    cancelTagPicker();
    return;
  }
  if (event.key === 'Tab') return;
  beginTagEdit(kind, index);
  if (!isTagPickerOpen(kind, index)) {
    activeTagPicker.value = { kind, index, original: tagEditSession.value.original };
  }
}
function tagOptionsFor(text = '') {
  const q = String(text || '').trim().toLowerCase();
  const names = tagNames.value || [];
  if (!q) return names.slice(0, 80);
  return names.filter(n => n.toLowerCase().includes(q)).slice(0, 80);
}
function selectPublishTag(i, tag) {
  useTagForPublish(i, tag);
  closeTagPicker();
  finishTagEdit();
}
function selectSubscribeTag(i, tag) {
  useTagForSubscribe(i, tag);
  closeTagPicker();
  finishTagEdit();
}

function setMsg(text) { msg.value = text || ''; }
function topicFor(tag, suffix = 'state') { return tag ? `tag/${tag}/${suffix}` : ''; }
function addPublish(tag = '') {
  config.value.publish.push(normalizePublish({ tag, topic: topicFor(tag, 'state'), rateMs: 1000 }));
}
function addSubscribe(tag = '') {
  config.value.subscribe.push(normalizeSubscribe({ tag, topic: topicFor(tag, 'set') }));
}
function removePublish(i) { config.value.publish.splice(i, 1); }
function removeSubscribe(i) { config.value.subscribe.splice(i, 1); }
function useTagForPublish(i, tag) {
  config.value.publish[i].tag = tag;
  if (!config.value.publish[i].topic) config.value.publish[i].topic = topicFor(tag, 'state');
}
function useTagForSubscribe(i, tag) {
  config.value.subscribe[i].tag = tag;
  if (!config.value.subscribe[i].topic) config.value.subscribe[i].topic = topicFor(tag, 'set');
}
async function loadTagsIfNeeded() {
  if (Array.isArray(designerTags.value) && designerTags.value.length) return;
  try {
    const res = await getTags();
    designerTags.value = Array.isArray(res?.tags) ? res.tags : [];
  } catch { /* optional */ }
}
async function refreshConfig() {
  busy.value = true;
  try {
    const res = await getMqttConfig();
    config.value = normalizeConfig(res);
    mqttLoaded.value = true;
    mqttDirty.value = false;
    mqttSourceLabel.value = 'device config';
    setMsg('Loaded MQTT configuration from LCD.');
  } catch (e) {
    setMsg(`Load failed: ${e?.message || e}`);
  } finally { busy.value = false; }
}
async function refreshStatus() {
  try { status.value = await getMqttStatus(); }
  catch (e) { status.value = { lastError: e?.message || String(e) }; }
}
async function saveConfig() {
  busy.value = true;
  try {
    config.value = normalizeConfig(config.value);
    await saveMqttConfig(config.value);
    let started = false;
    let startError = '';
    if (wantsAutoStart(config.value)) {
      try {
        await startMqtt();
        started = true;
      } catch (e) {
        startError = e?.message || String(e);
      }
    }
    mqttDirty.value = false;
    mqttSourceLabel.value = started ? 'device config - MQTT started' : 'device config';
    setMsg(started ? 'Saved MQTT config and started MQTT.' : (startError ? `Saved MQTT config, but start failed: ${startError}` : (config.value.enabled ? 'Saved MQTT config. Press Start to connect.' : 'Saved MQTT config. MQTT is disabled.')));
    await refreshStatus();
  } catch (e) {
    setMsg(`Save failed: ${e?.message || e}`);
  } finally { busy.value = false; }
}
async function startNow() {
  busy.value = true;
  try { await startMqtt(); setMsg('MQTT start requested.'); await refreshStatus(); }
  catch (e) { setMsg(`Start failed: ${e?.message || e}`); }
  finally { busy.value = false; }
}
async function stopNow() {
  busy.value = true;
  try { await stopMqtt(); setMsg('MQTT stopped.'); await refreshStatus(); }
  catch (e) { setMsg(`Stop failed: ${e?.message || e}`); }
  finally { busy.value = false; }
}
async function sendTest() {
  busy.value = true;
  try { await testMqttPublish(testTopic.value, testPayload.value, 0, false); setMsg(`Test publish sent to ${testTopic.value}.`); await refreshStatus(); }
  catch (e) { setMsg(`Test publish failed: ${e?.message || e}`); }
  finally { busy.value = false; }
}

watch(config, () => { if (mqttLoaded.value) mqttDirty.value = true; }, { deep: true });

watch(mqttConfigExternalRevision, async () => {
  await refreshConfig();
  await refreshStatus();
});

onMounted(async () => {
  await Promise.all([loadTagsIfNeeded(), refreshConfig(), refreshStatus()]);
  statusTimer = setInterval(refreshStatus, 2000);
});

onBeforeUnmount(() => {
  closeTagPicker();
  finishTagEdit();
  if (statusTimer) clearInterval(statusTimer);
});

</script>

<template>
  <main class="page mqtt-page">
    <section class="mqtt-header tag-registry-header">
      <div>
        <h2>MQTT Tag Bridge</h2>
        <div class="small">Config file: /spiffs/mqtt_config.json · Publish scheduler tick: 100ms · Source: {{ mqttSourceLabel }} · {{ mqttDirty ? 'unsaved edits' : 'synchronized' }}</div>
        <div class="small">{{ msg }}</div>
      </div>
      <div class="tag-actions">
        <button class="btn" :disabled="busy" @click="startNow">Start</button>
        <button class="btn danger" :disabled="busy" @click="stopNow">Stop</button>
      </div>
    </section>

    <section class="mqtt-status-grid">
      <div><span>MQTT</span><b>{{ status.connected ? 'Connected' : (status.started ? 'Connecting' : 'Stopped') }}</b><small>{{ status.lastError || '—' }}</small></div>
      <div><span>Broker</span><b>{{ status.broker || activeServer.host || 'not set' }}</b><small>{{ status.port || activeServer.port }} · {{ status.baseTopic || activeServer.baseTopic }}</small></div>
      <div><span>Traffic</span><b>TX {{ status.txCount || 0 }} · RX {{ status.rxCount || 0 }}</b><small>Errors {{ status.errorCount || 0 }}</small></div>
      <div><span>Last RX</span><b>{{ status.lastRxTopic || '—' }}</b><small>{{ status.lastRxPayload || '—' }}</small></div>
    </section>

    <section class="mqtt-card">
      <div class="mqtt-card-head">
        <h3>Global</h3>
        <label class="inline-check"><input type="checkbox" v-model="config.enabled"> Enable MQTT at boot / save</label>
      </div>
      <div class="mqtt-form-grid compact">
        <label>Active Broker
          <select v-model.number="config.activeServer">
            <option v-for="(srv, i) in config.servers" :key="i" :value="i">{{ i }} · {{ srv.name || `Broker ${i + 1}` }}</option>
          </select>
        </label>
        <label>Test Topic
          <input v-model="testTopic" placeholder="test or pilab/panel/test">
        </label>
        <label>Test Payload
          <input v-model="testPayload" placeholder="hello">
        </label>
        <div class="mqtt-form-actions"><button class="btn" :disabled="busy" @click="sendTest">Test Publish</button></div>
      </div>
    </section>

    <section class="mqtt-card">
      <div class="mqtt-card-head"><h3>Brokers</h3><p>Config supports three saved brokers. This build connects to one active broker at a time.</p></div>
      <div class="mqtt-brokers">
        <div v-for="(srv, i) in config.servers" :key="i" class="mqtt-broker-card">
          <div class="broker-title"><b>#{{ i }} {{ srv.name || `Broker ${i + 1}` }}</b><label><input type="checkbox" v-model="srv.enabled"> Enabled</label></div>
          <div class="mqtt-form-grid">
            <label>Name<input v-model="srv.name"></label>
            <label>Host/IP<input v-model="srv.host" placeholder="192.168.1.50"></label>
            <label>Port<input v-model.number="srv.port" type="number" min="1" max="65535"></label>
            <label>Client ID<input v-model="srv.clientId" placeholder="pilab-panel"></label>
            <label>Base Topic<input v-model="srv.baseTopic" placeholder="pilab/panel/pilab-panel"></label>
            <label>Username<input v-model="srv.username" autocomplete="off"></label>
            <label>Password<input v-model="srv.password" type="password" autocomplete="new-password"></label>
            <label class="inline-check"><input type="checkbox" v-model="srv.autoConnect"> Auto-connect</label>
            <label class="inline-check"><input type="checkbox" v-model="srv.useTls"> TLS / mqtts</label>
          </div>
        </div>
      </div>
    </section>

    <section class="mqtt-split">
      <div class="mqtt-card">
        <div class="mqtt-card-head"><h3>Publish Tags</h3><button class="btn" @click="addPublish()">Add Publish</button></div>
        <div class="mqtt-row header"><span>On</span><span>Tag</span><span>Topic</span><span>Rate</span><span>QoS</span><span>Retain</span><span></span></div>
        <div v-for="(row, i) in config.publish" :key="i" class="mqtt-row">
          <input type="checkbox" v-model="row.enabled">
          <div class="mqtt-tag-select">
            <input v-model="row.tag" placeholder="Tag_Name" @focus="openTagPicker('publish', i)" @click="openTagPicker('publish', i)" @change="useTagForPublish(i, row.tag)" @keydown="handleTagFieldKeydown('publish', i, $event)">
            <button type="button" class="mqtt-tag-select-btn" title="Select tag" @click="openTagPicker('publish', i)">⌄</button>
            <div v-if="isTagPickerOpen('publish', i)" class="mqtt-tag-popup">
              <div class="mqtt-tag-popup-head"><span>Select tag</span><button type="button" @click="cancelTagPicker">×</button></div>
              <button v-if="row.tag" type="button" class="mqtt-tag-option typed" @click="selectPublishTag(i, row.tag)">Use typed value: {{ row.tag }}</button>
              <button v-for="name in tagOptionsFor(row.tag)" :key="name" type="button" class="mqtt-tag-option" @click="selectPublishTag(i, name)">{{ name }}</button>
              <div v-if="!tagOptionsFor(row.tag).length" class="mqtt-tag-empty">No matching tags</div>
            </div>
          </div>
          <input v-model="row.topic" placeholder="tag/Tag_Name/state">
          <select v-model.number="row.rateMs"><option v-for="r in RATE_OPTIONS" :key="r" :value="r">{{ r >= 1000 ? `${r/1000}s` : `${r}ms` }}</option></select>
          <select v-model.number="row.qos"><option v-for="q in qosOptions" :key="q" :value="q">{{ q }}</option></select>
          <input type="checkbox" v-model="row.retain">
          <button class="icon-btn" title="Remove" @click="removePublish(i)">×</button>
        </div>
      </div>

      <div class="mqtt-card">
        <div class="mqtt-card-head"><h3>Subscribe to Tag Writes</h3><button class="btn" @click="addSubscribe()">Add Subscribe</button></div>
        <div class="mqtt-row sub header"><span>On</span><span>Tag</span><span>Topic</span><span>QoS</span><span></span></div>
        <div v-for="(row, i) in config.subscribe" :key="i" class="mqtt-row sub">
          <input type="checkbox" v-model="row.enabled">
          <div class="mqtt-tag-select">
            <input v-model="row.tag" placeholder="Tag_Name" @focus="openTagPicker('subscribe', i)" @click="openTagPicker('subscribe', i)" @change="useTagForSubscribe(i, row.tag)" @keydown="handleTagFieldKeydown('subscribe', i, $event)">
            <button type="button" class="mqtt-tag-select-btn" title="Select tag" @click="openTagPicker('subscribe', i)">⌄</button>
            <div v-if="isTagPickerOpen('subscribe', i)" class="mqtt-tag-popup">
              <div class="mqtt-tag-popup-head"><span>Select tag</span><button type="button" @click="cancelTagPicker">×</button></div>
              <button v-if="row.tag" type="button" class="mqtt-tag-option typed" @click="selectSubscribeTag(i, row.tag)">Use typed value: {{ row.tag }}</button>
              <button v-for="name in tagOptionsFor(row.tag)" :key="name" type="button" class="mqtt-tag-option" @click="selectSubscribeTag(i, name)">{{ name }}</button>
              <div v-if="!tagOptionsFor(row.tag).length" class="mqtt-tag-empty">No matching tags</div>
            </div>
          </div>
          <input v-model="row.topic" placeholder="tag/Tag_Name/set">
          <select v-model.number="row.qos"><option v-for="q in qosOptions" :key="q" :value="q">{{ q }}</option></select>
          <button class="icon-btn" title="Remove" @click="removeSubscribe(i)">×</button>
        </div>
      </div>
    </section>

    <section class="mqtt-card tag-picker-card">
      <div class="mqtt-card-head"><h3>Tag Picker</h3><input v-model="tagFilter" placeholder="Filter tags"></div>
      <div class="tag-chip-list">
        <button v-for="name in filteredTagNames" :key="name" class="tag-chip" @click="addPublish(name)">+ Pub {{ name }}</button>
        <button v-for="name in filteredTagNames" :key="name + '-sub'" class="tag-chip alt" @click="addSubscribe(name)">+ Sub {{ name }}</button>
      </div>
    </section>

  </main>
</template>
