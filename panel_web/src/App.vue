<script setup>
import { ref, onBeforeUnmount } from 'vue';
import DesignerPage from './pages/DesignerPage.vue';
import ScriptPage from './pages/ScriptPage.vue';
import TagsPage from './pages/TagsPage.vue';
import MqttPage from './pages/MqttPage.vue';
import DevicePage from './pages/DevicePage.vue';
import { getTags, saveTagsToRam, saveTagsToFlash } from './api/tagApi.js';
import { getScript, getRuntimeScript, saveScript, compileScript, getScriptStatus } from './api/scriptApi.js';
import { getLayout, getRuntimeLayout, saveLayout, applyLayout } from './api/hmiApi.js';
import { getMqttConfig, saveMqttConfig, startMqtt } from './api/mqttApi.js';
import { getWifiStatus, scanWifiNetworks, connectWifi, saveWifiHostname, forgetWifi } from './api/wifiApi.js';
import { getDeviceStatus } from './api/deviceApi.js';
import {
  designerLayout,
  designerTags,
  designerSelected,
  designerActivePageId,
  designerRaw,
  designerDirty,
  designerRuntimeDirty,
  designerSourceLabel,
  designerLoaded,
  designerHistory,
  scriptSource,
  scriptDirty,
  scriptRuntimeDirty,
  scriptExternalRevision,
  scriptSourceLabel,
  scriptLoaded,
  tagsLoaded,
  tagsDirty,
  tagsSourceLabel,
  projectMessage,
  appActiveTab,
  mqttConfigExternalRevision,
  mqttConfig,
  mqttLoaded,
  mqttDirty,
  mqttSourceLabel,
} from './stores/panelStore.js';
import { buildManifest, createZip, downloadBlob, makeProjectFileName, readZipFile } from './utils/projectZip.js';

const tabs = [
  ['designer', 'Designer'],
  ['script', 'Script'],
  ['tags', 'Tags'],
  ['mqtt', 'MQTT'],
  ['device', 'Device']
];
const active = appActiveTab;
const fileInput = ref(null);
const projectBusy = ref(false);
let projectMessageTimer = null;

const loadDialogOpen = ref(false);
const saveDialogOpen = ref(false);
const projectOpBusy = ref(false);
const projectOpMessage = ref('');
const loadOptions = ref({ screen: true, script: true, tags: true, mqtt: true });
const saveOptions = ref({ screen: true, script: true, tags: true, mqtt: true });

function projectTagPayload() {
  return { version: 1, tags: Array.isArray(designerTags.value) ? designerTags.value : [] };
}

async function ensureMqttConfigLoaded() {
  if (mqttConfig.value) return mqttConfig.value;
  try {
    mqttConfig.value = await getMqttConfig();
    mqttLoaded.value = true;
    mqttDirty.value = false;
    mqttSourceLabel.value = 'device config';
  } catch {
    mqttConfig.value = null;
  }
  return mqttConfig.value;
}


function mqttConfigWantsAutoStart(cfg) {
  if (!cfg || !cfg.enabled) return false;
  const active = Number(cfg.activeServer || 0);
  const srv = Array.isArray(cfg.servers) ? cfg.servers[active] : null;
  return !!(srv && srv.enabled && srv.autoConnect && String(srv.host || '').trim());
}

async function applyMqttConfigAndMaybeStart(cfg, { startIfAuto = true } = {}) {
  if (!cfg) return { saved: false, started: false, startError: '' };
  await saveMqttConfig(cfg);
  let started = false;
  let startError = '';
  if (startIfAuto && mqttConfigWantsAutoStart(cfg)) {
    try {
      await startMqtt();
      started = true;
    } catch (e) {
      // Keep the config apply successful even if WiFi/broker is not ready.
      // The MQTT status panel exposes the start failure reason and the user
      // can press Start after fixing the network/broker settings.
      startError = e?.message || String(e);
      console.warn('MQTT auto-start after project apply failed', e);
    }
  }
  return { saved: true, started, startError };
}

function openLoadToRamDialog() {
  projectOpMessage.value = '';
  loadDialogOpen.value = true;
}

function openSaveToFlashDialog() {
  projectOpMessage.value = '';
  saveDialogOpen.value = true;
}


function sleepMs(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function scriptStatusRuntime(status) {
  return status?.script_runtime || status || {};
}

async function waitForPanelRuntimeReady(label = 'panel runtime', timeoutMs = 15000) {
  const start = Date.now();
  let lastState = '';
  while (Date.now() - start < timeoutMs) {
    try {
      const status = await getDeviceStatus();
      const state = String(status?.hmi_runtime_state || '').toUpperCase();
      lastState = state || lastState;
      // Older/mock firmware does not expose hmi_runtime_state. In that case,
      // do not block forever; just give the ESP32 a short settle window.
      if (!state) {
        await sleepMs(350);
        return;
      }
      if (state === 'READY') return;
    } catch {
      // If the panel is briefly busy/restarting its HTTP connection, keep polling.
    }
    await sleepMs(250);
  }
  throw new Error(`${label} did not return to READY${lastState ? ` (last state: ${lastState})` : ''}`);
}

async function waitForScriptCompileComplete(previousGeneration = 0, timeoutMs = 20000) {
  const start = Date.now();
  let sawNewGeneration = false;
  let lastState = '';
  while (Date.now() - start < timeoutMs) {
    try {
      const status = await getScriptStatus();
      const rt = scriptStatusRuntime(status);
      const generation = Number(rt.compile_generation ?? status?.compile_generation ?? 0);
      const compileState = String(rt.compile_state ?? status?.compile_state ?? '').toUpperCase();
      const active = !!rt.compile_task_active;
      lastState = compileState || lastState;
      if (generation > previousGeneration) sawNewGeneration = true;
      const busy = active || compileState.includes('COMPIL') || compileState.includes('QUEUED');
      if (sawNewGeneration && !busy) {
        if (compileState.includes('FAIL') || compileState.includes('ERROR')) {
          throw new Error(`script compile failed: ${compileState}`);
        }
        await waitForPanelRuntimeReady('script compile runtime settle', 15000);
        return status;
      }
    } catch (e) {
      if (String(e?.message || e).includes('script compile failed')) throw e;
    }
    await sleepMs(250);
  }
  throw new Error(`script compile did not finish${lastState ? ` (last state: ${lastState})` : ''}`);
}

async function compileScriptAndWait(scriptText) {
  let previousGeneration = 0;
  try {
    const before = await getScriptStatus();
    const rt = scriptStatusRuntime(before);
    previousGeneration = Number(rt.compile_generation ?? before?.compile_generation ?? 0);
  } catch {
    previousGeneration = 0;
  }
  await compileScript(scriptText || '');
  await waitForScriptCompileComplete(previousGeneration);
}

async function loadSelectedToRam() {
  if (projectOpBusy.value) return;
  projectOpBusy.value = true;
  projectOpMessage.value = 'Loading selected project sections to LCD RAM...';
  const done = [];
  try {
    const opt = loadOptions.value;
    if (opt.tags) {
      projectOpMessage.value = 'Loading Tags to LCD RAM...';
      await saveTagsToRam(projectTagPayload());
      tagsDirty.value = false;
      tagsSourceLabel.value = 'lcd runtime RAM';
      done.push('Tags');
    }
    if (opt.script) {
      projectOpMessage.value = 'Compiling Script to LCD RAM...';
      await compileScriptAndWait(scriptSource.value || '');
      scriptRuntimeDirty.value = false;
      done.push('Script');
    }
    if (opt.screen) {
      projectOpMessage.value = 'Loading Screen layout to LCD RAM...';
      await applyLayout(designerLayout.value || defaultLayout());
      await waitForPanelRuntimeReady('screen layout load', 15000);
      designerRuntimeDirty.value = false;
      designerSourceLabel.value = 'lcd runtime RAM';
      done.push('Screen');
    }
    if (opt.mqtt) {
      projectOpMessage.value = 'Applying MQTT config...';
      const cfg = await ensureMqttConfigLoaded();
      if (cfg) {
        const mqttResult = await applyMqttConfigAndMaybeStart(cfg, { startIfAuto: true });
        mqttDirty.value = false;
        mqttSourceLabel.value = mqttResult.started ? 'device config - MQTT started' : 'device config';
        mqttConfigExternalRevision.value++;
        done.push(mqttResult.started ? 'MQTT + Start' : (mqttResult.startError ? 'MQTT applied, start failed' : 'MQTT'));
      }
    }
    projectOpMessage.value = `Loaded to RAM: ${done.join(', ') || 'nothing selected'}.`;
    setProjectMessage(projectOpMessage.value);
    loadDialogOpen.value = false;
  } catch (e) {
    projectOpMessage.value = `Load to RAM failed: ${e?.message || e}`;
  } finally {
    projectOpBusy.value = false;
  }
}

async function saveSelectedToFlash() {
  if (projectOpBusy.value) return;
  projectOpBusy.value = true;
  projectOpMessage.value = 'Saving selected project sections to flash...';
  const done = [];
  try {
    const opt = saveOptions.value;
    if (opt.tags) {
      await saveTagsToFlash(projectTagPayload());
      tagsDirty.value = false;
      tagsSourceLabel.value = 'device flash';
      done.push('Tags');
    }
    if (opt.script) {
      await saveScript(scriptSource.value || '');
      scriptDirty.value = false;
      scriptSourceLabel.value = 'device flash';
      done.push('Script');
    }
    if (opt.screen) {
      await saveLayout(designerLayout.value || defaultLayout());
      designerDirty.value = false;
      designerSourceLabel.value = 'device flash';
      done.push('Screen');
    }
    if (opt.mqtt) {
      projectOpMessage.value = 'Applying MQTT config...';
      const cfg = await ensureMqttConfigLoaded();
      if (cfg) {
        await saveMqttConfig(cfg);
        mqttDirty.value = false;
        mqttSourceLabel.value = 'device config';
        mqttConfigExternalRevision.value++;
        done.push('MQTT');
      }
    }
    projectOpMessage.value = `Saved to flash: ${done.join(', ') || 'nothing selected'}.`;
    setProjectMessage(projectOpMessage.value);
    saveDialogOpen.value = false;
  } catch (e) {
    projectOpMessage.value = `Save to flash failed: ${e?.message || e}`;
  } finally {
    projectOpBusy.value = false;
  }
}

const wifiOpen = ref(false);
const wifiBusy = ref(false);
const wifiMessage = ref('');
const wifiStatus = ref({});
const wifiNetworks = ref([]);
const wifiSelectedSsid = ref('');
const wifiPassword = ref('');
const wifiShowPassword = ref(false);
const wifiHostname = ref('pilab-panel');

function wifiQuality(rssi) {
  const n = Number(rssi || -100);
  if (n >= -50) return 'excellent';
  if (n >= -60) return 'good';
  if (n >= -70) return 'fair';
  return 'weak';
}

async function refreshWifiStatus() {
  try {
    wifiStatus.value = await getWifiStatus();
    wifiHostname.value = wifiStatus.value?.hostname || 'pilab-panel';
  } catch (e) {
    wifiMessage.value = `WiFi status failed: ${e?.message || e}`;
  }
}

async function refreshWifiScan() {
  wifiBusy.value = true;
  wifiMessage.value = 'Scanning WiFi networks...';
  try {
    const res = await scanWifiNetworks();
    wifiNetworks.value = Array.isArray(res?.networks) ? res.networks : [];
    wifiMessage.value = wifiNetworks.value.length ? `Found ${wifiNetworks.value.length} network(s).` : 'No networks found.';
  } catch (e) {
    wifiMessage.value = `WiFi scan failed: ${e?.message || e}`;
  } finally {
    wifiBusy.value = false;
  }
}

async function openWifiDialog() {
  wifiOpen.value = true;
  wifiMessage.value = '';
  await refreshWifiStatus();
  wifiSelectedSsid.value = wifiStatus.value?.sta_ssid || '';
  wifiHostname.value = wifiStatus.value?.hostname || 'pilab-panel';
  wifiPassword.value = '';
  await refreshWifiScan();
}

function selectWifiNetwork(net) {
  wifiSelectedSsid.value = net?.ssid || '';
  wifiPassword.value = '';
  wifiMessage.value = wifiSelectedSsid.value ? `Selected ${wifiSelectedSsid.value}. Enter password and connect.` : '';
}

async function saveWifiConnection() {
  if (!wifiSelectedSsid.value.trim()) {
    wifiMessage.value = 'Select or type an SSID first.';
    return;
  }
  wifiBusy.value = true;
  wifiMessage.value = `Saving ${wifiSelectedSsid.value} and connecting...`;
  try {
    await connectWifi(wifiSelectedSsid.value.trim(), wifiPassword.value || '', wifiHostname.value || 'pilab-panel');
    await refreshWifiStatus();
    wifiMessage.value = `WiFi setting saved. Use ${wifiStatus.value?.mdns_url || ('http://' + (wifiHostname.value || 'pilab-panel') + '.local')} or the STA IP after it connects. The PiLab-Panel AP remains available as fallback.`;
  } catch (e) {
    wifiMessage.value = `WiFi save/connect failed: ${e?.message || e}`;
  } finally {
    wifiBusy.value = false;
  }
}

async function saveDeviceNameOnly() {
  wifiBusy.value = true;
  wifiMessage.value = `Saving device name ${wifiHostname.value || 'pilab-panel'}...`;
  try {
    await saveWifiHostname(wifiHostname.value || 'pilab-panel');
    await refreshWifiStatus();
    wifiMessage.value = `Device name saved. Try ${wifiStatus.value?.mdns_url || ('http://' + (wifiHostname.value || 'pilab-panel') + '.local')}.`;
  } catch (e) {
    wifiMessage.value = `Device name save failed: ${e?.message || e}`;
  } finally {
    wifiBusy.value = false;
  }
}

async function forgetWifiConnection() {
  wifiBusy.value = true;
  wifiMessage.value = 'Deleting saved WiFi setting...';
  try {
    await forgetWifi();
    wifiPassword.value = '';
    wifiSelectedSsid.value = '';
    await refreshWifiStatus();
    wifiMessage.value = 'Saved WiFi setting deleted. The panel will boot in access-point/recovery mode.';
  } catch (e) {
    wifiMessage.value = `Delete failed: ${e?.message || e}`;
  } finally {
    wifiBusy.value = false;
  }
}

function setProjectMessage(text, timeoutMs = 3500) {
  projectMessage.value = text || '';
  if (projectMessageTimer) {
    clearTimeout(projectMessageTimer);
    projectMessageTimer = null;
  }
  if (text && timeoutMs > 0) {
    projectMessageTimer = setTimeout(() => {
      projectMessage.value = '';
      projectMessageTimer = null;
    }, timeoutMs);
  }
}

onBeforeUnmount(() => {
  if (projectMessageTimer) clearTimeout(projectMessageTimer);
});

function defaultLayout() {
  return {
    version: 3,
    screen: { width: 800, height: 480, background: '#101828' },
    widgets: [],
  };
}

function normalizeProjectTags(input) {
  if (Array.isArray(input)) return input;
  if (Array.isArray(input?.tags)) return input.tags;
  return [];
}

async function ensureProjectDataLoaded() {
  if (!designerLayout.value) {
    try { designerLayout.value = await getRuntimeLayout(); }
    catch { try { designerLayout.value = await getLayout(); } catch { designerLayout.value = defaultLayout(); } }
      }
  if (!scriptLoaded.value && !scriptSource.value) {
    try {
      let res;
      try { res = await getRuntimeScript(); }
      catch { res = await getScript(); }
      scriptSource.value = res?.script || '';
      scriptLoaded.value = true;
      scriptSourceLabel.value = res?.source || 'lcd runtime';
    } catch { /* export whatever is in RAM */ }
  }
  if (!designerTags.value.length && !tagsDirty.value) {
    try {
      const res = await getTags();
      designerTags.value = Array.isArray(res?.tags) ? res.tags : [];
      tagsLoaded.value = true;
      tagsSourceLabel.value = 'device runtime';
    } catch { /* tags are optional for export */ }
  }
}

async function exportProject() {
  if (projectBusy.value) return;
  projectBusy.value = true;
  try {
    await ensureProjectDataLoaded();
    const layout = designerLayout.value || defaultLayout();
    const script = scriptSource.value || '';
    const tags = designerTags.value || [];
    let mqtt = mqttConfig.value || null;
    if (!mqtt) { try { mqtt = await getMqttConfig(); } catch { mqtt = null; } }
    const manifest = buildManifest({ layout, script, tags, mqtt });
    const files = [
      { name: 'manifest.json', data: JSON.stringify(manifest, null, 2) },
      { name: 'screen.json', data: JSON.stringify(layout, null, 2) },
      { name: 'script.as', data: script },
      { name: 'tags.json', data: JSON.stringify({ version: 1, tags }, null, 2) },
    ];
    if (mqtt) files.push({ name: 'mqtt_config.json', data: JSON.stringify(mqtt, null, 2) });
    const zip = createZip(files);
    const name = makeProjectFileName();
    downloadBlob(zip, name);
    setProjectMessage(`Exported ${name}`);
  } catch (e) {
    setProjectMessage(`Export failed: ${e?.message || e}`, 6000);
  } finally {
    projectBusy.value = false;
  }
}

function openImportDialog() {
  fileInput.value?.click();
}

async function importProject(ev) {
  const file = ev.target.files?.[0];
  ev.target.value = '';
  if (!file) return;
  if (projectBusy.value) return;
  projectBusy.value = true;
  try {
    const entries = await readZipFile(file);
    const manifest = entries['manifest.json'] ? JSON.parse(entries['manifest.json']) : null;
    const layout = entries['screen.json'] ? JSON.parse(entries['screen.json']) : null;
    const script = entries['script.as'] ?? '';
    const tagDoc = entries['tags.json'] ? JSON.parse(entries['tags.json']) : { tags: [] };
    const tags = normalizeProjectTags(tagDoc);
    const mqtt = entries['mqtt_config.json'] ? JSON.parse(entries['mqtt_config.json']) : null;

    if (manifest && manifest.format && manifest.format !== 'PiLabPanelProject') {
      throw new Error(`Not a PiLab Panel project: ${manifest.format}`);
    }
    if (!layout || typeof layout !== 'object') throw new Error('Project is missing screen.json');

    designerLayout.value = layout;
    designerActivePageId.value = layout.activePage || layout.startupPage || (Array.isArray(layout.pages) && layout.pages[0]?.id) || 'main';
    designerSelected.value = -1;
    designerRaw.value = JSON.stringify(layout, null, 2);
    designerLoaded.value = true;
    designerDirty.value = true;
    designerRuntimeDirty.value = true;
    designerSourceLabel.value = `imported ${file.name}`;
    designerHistory.value.unshift({ label: `Import project ${file.name}`, kind: 'open', at: 'now', icon: '⇣' });
    designerHistory.value = designerHistory.value.slice(0, 24);

    scriptSource.value = script;
    scriptLoaded.value = true;
    scriptDirty.value = true;
    scriptRuntimeDirty.value = true;
    scriptSourceLabel.value = `imported ${file.name}`;
    scriptExternalRevision.value++;

    designerTags.value = tags;
    tagsLoaded.value = true;
    tagsDirty.value = true;
    tagsSourceLabel.value = `imported ${file.name}`;

    let mqttMessage = '';
    if (mqtt) {
      mqttConfig.value = mqtt;
      mqttLoaded.value = true;
      mqttDirty.value = true;
      mqttSourceLabel.value = `imported ${file.name}`;
      mqttConfigExternalRevision.value++;
      mqttMessage = ' MQTT config was loaded into the web app; use Load to RAM or Save to Flash to apply it.';
    }

    setProjectMessage(`Imported ${file.name}. Apply/compile/save when ready.${mqttMessage}`);
    active.value = 'designer';
  } catch (e) {
    setProjectMessage(`Import failed: ${e?.message || e}`, 6000);
  } finally {
    projectBusy.value = false;
  }
}
</script>

<template>
  <div class="app-shell">
    <header class="topbar">
      <div class="brand">PiLab Panel</div>
      <div class="subtitle">Browser HMI Designer + LVGL Runtime + AngelScript</div>
      <div class="project-actions">
        <button class="top-btn" :disabled="projectBusy" @click="openImportDialog">Import Project</button>
        <button class="top-btn" :disabled="projectBusy" @click="exportProject">Export Project</button>
        <button class="top-btn primary" :disabled="projectBusy || projectOpBusy" @click="openLoadToRamDialog">Load to RAM</button>
        <button class="top-btn" :disabled="projectBusy || projectOpBusy" @click="openSaveToFlashDialog">Save to Flash</button>
        <input ref="fileInput" class="hidden-file" type="file" accept=".zip,application/zip" @change="importProject">
      </div>
      <nav class="tabs">
        <button v-for="[id, label] in tabs" :key="id" :class="['tab', { active: active === id }]" @click="active = id">{{ label }}</button>
      </nav>
      <button class="wifi-cog" title="WiFi settings" @click="openWifiDialog">⚙</button>
    </header>
    <Transition name="project-toast">
      <div v-if="projectMessage" class="project-message">{{ projectMessage }}</div>
    </Transition>
    <DesignerPage v-show="active === 'designer'" :active="active === 'designer'" />
    <ScriptPage v-show="active === 'script'" />
    <TagsPage v-show="active === 'tags'" />
    <MqttPage v-show="active === 'mqtt'" />
    <DevicePage v-show="active === 'device'" />


    <Teleport to="body">
      <div v-if="loadDialogOpen" class="modal-backdrop project-op-backdrop" @click.self="loadDialogOpen = false">
        <section class="project-op-modal">
          <header class="wifi-modal-head">
            <div>
              <h2>Load to RAM</h2>
              <p>Apply selected web-editor sections to the running LCD/runtime without saving the whole project to flash.</p>
            </div>
            <button class="modal-close" @click="loadDialogOpen = false">×</button>
          </header>
          <div class="project-op-list">
            <label><input type="checkbox" v-model="loadOptions.screen"> <b>Screen layout</b><small>Apply Designer screens to the LCD LVGL runtime.</small></label>
            <label><input type="checkbox" v-model="loadOptions.script"> <b>Script</b><small>Compile the Script editor contents into runtime RAM.</small></label>
            <label><input type="checkbox" v-model="loadOptions.tags"> <b>Tags</b><small>Upsert current Tag Registry rows into runtime RAM.</small></label>
            <label><input type="checkbox" v-model="loadOptions.mqtt"> <b>MQTT config</b><small>Apply the current MQTT config. MQTT config is file-backed on the device.</small></label>
          </div>
          <p v-if="projectOpMessage" class="wifi-message">{{ projectOpMessage }}</p>
          <div class="wifi-actions modal-actions-right">
            <button class="btn" :disabled="projectOpBusy" @click="loadDialogOpen = false">Cancel</button>
            <button class="btn primary" :disabled="projectOpBusy" @click="loadSelectedToRam">{{ projectOpBusy ? 'Loading...' : 'Load Selected' }}</button>
          </div>
        </section>
      </div>
    </Teleport>

    <Teleport to="body">
      <div v-if="saveDialogOpen" class="modal-backdrop project-op-backdrop" @click.self="saveDialogOpen = false">
        <section class="project-op-modal">
          <header class="wifi-modal-head">
            <div>
              <h2>Save to Flash</h2>
              <p>Persist selected web-editor sections so they are restored after the Panel reboots.</p>
            </div>
            <button class="modal-close" @click="saveDialogOpen = false">×</button>
          </header>
          <div class="project-op-list">
            <label><input type="checkbox" v-model="saveOptions.screen"> <b>Screen layout</b><small>Save Designer screens to the panel flash layout file.</small></label>
            <label><input type="checkbox" v-model="saveOptions.script"> <b>Script</b><small>Save the Script editor contents to flash.</small></label>
            <label><input type="checkbox" v-model="saveOptions.tags"> <b>Tags</b><small>Save Tag Registry rows and current values to flash.</small></label>
            <label><input type="checkbox" v-model="saveOptions.mqtt"> <b>MQTT config</b><small>Save broker, publish, and subscribe mappings.</small></label>
          </div>
          <p v-if="projectOpMessage" class="wifi-message">{{ projectOpMessage }}</p>
          <div class="wifi-actions modal-actions-right">
            <button class="btn" :disabled="projectOpBusy" @click="saveDialogOpen = false">Cancel</button>
            <button class="btn primary" :disabled="projectOpBusy" @click="saveSelectedToFlash">{{ projectOpBusy ? 'Saving...' : 'Save Selected' }}</button>
          </div>
        </section>
      </div>
    </Teleport>

    <Teleport to="body">
      <div v-if="wifiOpen" class="modal-backdrop wifi-backdrop" @click.self="wifiOpen = false">
        <section class="wifi-modal">
          <header class="wifi-modal-head">
            <div>
              <h2>WiFi Settings</h2>
              <p>Connect the panel to your local WiFi. Settings are saved in NVS and reused after reboot.</p>
            </div>
            <button class="modal-close" @click="wifiOpen = false">×</button>
          </header>

          <div class="wifi-status-grid">
            <div><span>AP fallback</span><b>{{ wifiStatus.ap_ssid || 'PiLab-Panel' }}</b><small>{{ wifiStatus.ap_ip || '192.168.4.1' }}</small></div>
            <div><span>Saved STA</span><b>{{ wifiStatus.sta_saved ? wifiStatus.sta_ssid : 'None' }}</b><small>{{ wifiStatus.sta_saved ? 'Saved in NVS' : 'AP-only on reboot' }}</small></div>
            <div><span>STA state</span><b>{{ wifiStatus.sta_connected ? 'Connected' : (wifiStatus.sta_connecting ? 'Connecting' : 'Not connected') }}</b><small>{{ wifiStatus.sta_ip || wifiStatus.sta_auth || '—' }}</small></div>
            <div><span>Network URL</span><b>{{ wifiStatus.mdns_url || 'http://pilab-panel.local' }}</b><small>{{ wifiStatus.sta_connected ? (wifiStatus.sta_ip ? ('http://' + wifiStatus.sta_ip) : 'STA connected') : 'Works when mDNS is available on your network' }}</small></div>
          </div>

          <div class="wifi-form-grid wifi-device-grid">
            <label>
              Device Name / mDNS Hostname
              <input v-model="wifiHostname" placeholder="pilab-panel" autocomplete="off">
              <small class="field-help">Use letters, numbers, and dashes. Example: http://shop-hmi.local</small>
            </label>
            <div class="wifi-name-actions">
              <button class="btn" :disabled="wifiBusy" @click="saveDeviceNameOnly">Save Device Name</button>
            </div>
          </div>

          <div class="wifi-form-grid">
            <label>
              SSID
              <input v-model="wifiSelectedSsid" placeholder="Select a network or type SSID" autocomplete="off">
            </label>
            <label>
              Password
              <input v-model="wifiPassword" :type="wifiShowPassword ? 'text' : 'password'" placeholder="Network password" autocomplete="new-password">
            </label>
          </div>
          <label class="wifi-show-pass"><input type="checkbox" v-model="wifiShowPassword"> Show password</label>

          <div class="wifi-actions">
            <button class="btn primary" :disabled="wifiBusy" @click="saveWifiConnection">Save Name + Connect</button>
            <button class="btn" :disabled="wifiBusy" @click="refreshWifiScan">Scan</button>
            <button class="btn danger" :disabled="wifiBusy || !wifiStatus.sta_saved" @click="forgetWifiConnection">Delete Saved WiFi / AP Mode</button>
          </div>

          <p v-if="wifiMessage" class="wifi-message">{{ wifiMessage }}</p>

          <div class="wifi-network-list">
            <button v-for="net in wifiNetworks" :key="net.ssid" :class="['wifi-network', { selected: wifiSelectedSsid === net.ssid }]" @click="selectWifiNetwork(net)">
              <span><b>{{ net.ssid }}</b><small>{{ net.auth }} · ch {{ net.channel }}</small></span>
              <em>{{ wifiQuality(net.rssi) }} {{ net.rssi }} dBm</em>
            </button>
          </div>
        </section>
      </div>
    </Teleport>
  </div>
</template>
