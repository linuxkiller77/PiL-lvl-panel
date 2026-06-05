<script setup>
import { computed, onMounted, ref } from 'vue';
import { getTags, saveTagsToRam, saveTagsToFlash } from '../api/tagApi.js';
import { designerTags, tagsLoaded, tagsDirty, tagsSourceLabel } from '../stores/panelStore.js';

const tags = designerTags;
const msg = ref('');
const busy = ref(false);
const filter = ref('');
const newTagName = ref('');
const newTagType = ref('bool');
const fileInput = ref(null);
const lastSavedAt = ref('');

const TYPE_OPTIONS = ['bool', 'int', 'float', 'string'];
const RESERVED = new Set(['true','false','if','else','for','while','return','void','bool','int','float','double','string','class','this','null']);
const SYSTEM_TAGS = new Set(['Panel_Heartbeat', 'Panel_Uptime_s']);

function isSystemTag(tagOrName) {
  const name = typeof tagOrName === 'string' ? tagOrName : tagOrName?.name;
  return !!tagOrName?.system || SYSTEM_TAGS.has(String(name || ''));
}

function clone(v) { return JSON.parse(JSON.stringify(v ?? null)); }
function normalizeName(name) {
  return String(name || '').trim().replace(/[^A-Za-z0-9_]/g, '_').replace(/^([0-9])/, '_$1');
}
function isValidName(name) {
  const n = String(name || '').trim();
  return /^[A-Za-z_][A-Za-z0-9_]*$/.test(n) && !RESERVED.has(n.toLowerCase());
}
function normalizeType(type) {
  const t = String(type || '').toLowerCase();
  return TYPE_OPTIONS.includes(t) ? t : (t === 'number' ? 'float' : 'bool');
}
function defaultValueForType(type) {
  type = normalizeType(type);
  if (type === 'bool') return false;
  if (type === 'string') return '';
  return 0;
}
function coerceValue(type, value) {
  type = normalizeType(type);
  if (type === 'bool') return value === true || value === 'true' || value === '1' || value === 1 || value === 'ON';
  if (type === 'string') return String(value ?? '');
  const n = Number(value);
  return Number.isFinite(n) ? (type === 'int' ? Math.trunc(n) : n) : 0;
}
function defaultMin(type) { return normalizeType(type) === 'bool' ? 0 : 0; }
function defaultMax(type) { return normalizeType(type) === 'bool' ? 1 : 100; }
function normalizeTag(raw) {
  const name = normalizeName(raw?.name);
  const type = normalizeType(raw?.type);
  const initialRaw = raw?.initialValue ?? raw?.initial ?? raw?.startupValue ?? raw?.value ?? defaultValueForType(type);
  const currentRaw = raw?.value ?? initialRaw;
  return {
    name,
    type,
    value: coerceValue(type, currentRaw),
    initialValue: coerceValue(type, initialRaw),
    writable: raw?.writable !== false,
    units: String(raw?.units ?? ''),
    min: Number.isFinite(Number(raw?.min)) ? Number(raw.min) : defaultMin(type),
    max: Number.isFinite(Number(raw?.max)) ? Number(raw.max) : defaultMax(type),
    description: String(raw?.description ?? ''),
    system: raw?.system === true || SYSTEM_TAGS.has(name),
  };
}
function normalizeTagList(list) {
  const seen = new Set();
  const out = [];
  for (const raw of list || []) {
    const row = normalizeTag(raw);
    if (!row.name || !isValidName(row.name)) continue;
    const key = row.name.toLowerCase();
    if (seen.has(key)) continue;
    seen.add(key);
    out.push(row);
  }
  return out.sort((a, b) => a.name.localeCompare(b.name));
}
const filteredTags = computed(() => {
  const q = String(filter.value || '').trim().toLowerCase();
  const rows = normalizeTagList(tags.value);
  if (!q) return rows;
  return rows.filter(t => [t.name, t.type, t.value, t.initialValue, t.units, t.min, t.max, t.description, t.system ? 'system' : '', t.writable ? 'writable' : 'readonly'].join(' ').toLowerCase().includes(q));
});
const tagStats = computed(() => {
  const rows = normalizeTagList(tags.value);
  return {
    total: rows.length,
    bool: rows.filter(t => t.type === 'bool').length,
    numeric: rows.filter(t => t.type === 'int' || t.type === 'float').length,
    string: rows.filter(t => t.type === 'string').length,
    writable: rows.filter(t => t.writable).length,
    system: rows.filter(t => isSystemTag(t)).length,
  };
});
const canAdd = computed(() => {
  const name = normalizeName(newTagName.value);
  if (!name || name !== String(newTagName.value || '').trim() || !isValidName(name)) return false;
  return !normalizeTagList(tags.value).some(t => t.name.toLowerCase() === name.toLowerCase());
});

function setMsg(text) { msg.value = text || ''; }
function markDirty(text = 'Tag registry has unsaved changes') {
  tagsDirty.value = true;
  tagsSourceLabel.value = 'browser edits';
  lastSavedAt.value = '';
  setMsg(text);
}
function rememberSaved(label) {
  const now = new Date();
  lastSavedAt.value = `${label} at ${now.toLocaleTimeString()}`;
}
async function loadFromLcd() {
  if (busy.value) return;
  if (tagsDirty.value && !confirm('Reload tags from the LCD and discard unsaved browser tag edits?')) return;
  busy.value = true;
  try {
    const res = await getTags();
    const next = normalizeTagList(Array.isArray(res?.tags) ? res.tags : []);
    tags.value = next;
    tagsLoaded.value = true;
    tagsDirty.value = false;
    tagsSourceLabel.value = res?.source || 'lcd runtime';
    lastSavedAt.value = '';
    setMsg(`Loaded ${next.length} tag${next.length === 1 ? '' : 's'} from LCD`);
  } catch (e) {
    setMsg(`Load failed: ${e?.message || e}`);
  } finally {
    busy.value = false;
  }
}
async function saveToRam() {
  if (busy.value) return;
  busy.value = true;
  try {
    const payload = { version: 1, tags: normalizeTagList(tags.value) };
    const res = await saveTagsToRam(payload);
    tags.value = payload.tags;
    tagsLoaded.value = true;
    tagsDirty.value = false;
    tagsSourceLabel.value = 'lcd runtime RAM';
    rememberSaved('Saved to RAM');
    setMsg(`Saved ${payload.tags.length} tag${payload.tags.length === 1 ? '' : 's'} to LCD RAM${res?.generation ? ` · generation ${res.generation}` : ''}. Runtime tag table replaced exactly; omitted tags are deleted.`);
  } catch (e) {
    setMsg(`Save to RAM failed: ${e?.message || e}`);
  } finally {
    busy.value = false;
  }
}
async function saveToFlash() {
  if (busy.value) return;
  busy.value = true;
  try {
    const payload = { version: 1, tags: normalizeTagList(tags.value) };
    const res = await saveTagsToFlash(payload);
    tags.value = payload.tags;
    tagsLoaded.value = true;
    tagsDirty.value = false;
    tagsSourceLabel.value = 'lcd flash registry';
    rememberSaved('Saved to Flash');
    setMsg(`Saved ${payload.tags.length} tag${payload.tags.length === 1 ? '' : 's'} to LCD RAM and Flash for boot restore${res?.generation ? ` · generation ${res.generation}` : ''}.`);
  } catch (e) {
    setMsg(`Save to flash failed: ${e?.message || e}`);
  } finally {
    busy.value = false;
  }
}
function addTag() {
  const name = normalizeName(newTagName.value);
  if (!canAdd.value) {
    setMsg(name ? `Cannot add ${name}: invalid, reserved, or duplicate name` : 'Enter a valid tag name');
    return;
  }
  const type = normalizeType(newTagType.value);
  const row = normalizeTag({
    name,
    type,
    value: defaultValueForType(type),
    initialValue: defaultValueForType(type),
    writable: true,
    units: '',
    min: defaultMin(type),
    max: defaultMax(type),
    description: `User tag ${name}`,
  });
  tags.value = normalizeTagList([...(tags.value || []), row]);
  newTagName.value = '';
  markDirty(`Added ${name}`);
}
function updateTag(name, field, value) {
  const rows = normalizeTagList(tags.value);
  const idx = rows.findIndex(t => t.name === name);
  if (idx < 0) return;
  const row = { ...rows[idx] };
  if (field === 'type') {
    row.type = normalizeType(value);
    row.initialValue = coerceValue(row.type, row.initialValue);
    row.value = coerceValue(row.type, row.value);
    if (row.type === 'bool') { row.min = 0; row.max = 1; }
    if (row.type === 'string') { row.min = 0; row.max = 0; row.units = ''; }
  } else if (field === 'initialValue') row.initialValue = coerceValue(row.type, value);
  else if (field === 'writable') row.writable = !!value;
  else if (field === 'min' || field === 'max') row[field] = Number.isFinite(Number(value)) ? Number(value) : 0;
  else row[field] = String(value ?? '');
  rows[idx] = normalizeTag(row);
  tags.value = rows;
  markDirty(`Updated ${name}`);
}
function deleteTag(name) {
  if (SYSTEM_TAGS.has(String(name || ''))) {
    setMsg(`${name} is a system tag and cannot be removed`);
    return;
  }
  tags.value = normalizeTagList(tags.value).filter(t => t.name !== name);
  markDirty(`Removed ${name} from browser table only`);
}
function exportJson() {
  const text = JSON.stringify({ version: 1, tags: normalizeTagList(tags.value) }, null, 2);
  const blob = new Blob([text], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = 'pilab_panel_tags.json';
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
  setMsg('Exported pilab_panel_tags.json');
}
function triggerImport() { fileInput.value?.click(); }
function importJson(ev) {
  const file = ev.target.files?.[0];
  ev.target.value = '';
  if (!file) return;
  const reader = new FileReader();
  reader.onload = () => {
    try {
      const parsed = JSON.parse(String(reader.result || '{}'));
      const incoming = normalizeTagList(Array.isArray(parsed) ? parsed : parsed.tags);
      tags.value = incoming;
      tagsLoaded.value = true;
      markDirty(`Imported ${incoming.length} tag${incoming.length === 1 ? '' : 's'} from ${file.name}`);
    } catch (e) {
      setMsg(`Import failed: ${e?.message || e}`);
    }
  };
  reader.readAsText(file);
}
function displayValue(v) { return typeof v === 'object' ? JSON.stringify(v) : String(v ?? ''); }

onMounted(() => {
  if (!tagsLoaded.value || !Array.isArray(tags.value) || tags.value.length === 0) loadFromLcd();
  else setMsg(`${normalizeTagList(tags.value).length} tags in browser RAM`);
});
</script>

<template>
  <main class="page tags-page tag-registry-page">
    <section class="tag-registry-header">
      <div>
        <h2>Tag Registry</h2>
        <div class="small">Source: {{ tagsSourceLabel }} · <span :class="tagsDirty ? 'dirty-pill' : 'clean-pill'">{{ tagsDirty ? 'unsaved changes' : 'synchronized' }}</span><span v-if="lastSavedAt"> · {{ lastSavedAt }}</span></div>
        <div class="small">{{ msg }}</div>
        <div class="small">Initial Value is the boot/startup value. Value is the current runtime value reported by the LCD. Use the top Project buttons to Load to RAM or Save to Flash.</div>
      </div>
      <div class="tags-actions wrap-actions">
      </div>
    </section>

    <section class="tag-stat-grid">
      <div class="tag-stat"><span>Total</span><b>{{ tagStats.total }}</b></div>
      <div class="tag-stat"><span>Bool</span><b>{{ tagStats.bool }}</b></div>
      <div class="tag-stat"><span>Numeric</span><b>{{ tagStats.numeric }}</b></div>
      <div class="tag-stat"><span>String</span><b>{{ tagStats.string }}</b></div>
      <div class="tag-stat"><span>Writable</span><b>{{ tagStats.writable }}</b></div>
      <div class="tag-stat"><span>System</span><b>{{ tagStats.system }}</b></div>
    </section>

    <section class="tag-editor-card">
      <div class="tag-add-row">
        <div class="tag-add-name">
          <label>New tag name</label>
          <input v-model.trim="newTagName" @keyup.enter="addTag" placeholder="Motor_Start, Speed_SP, StatusText">
        </div>
        <div>
          <label>Type</label>
          <select v-model="newTagType">
            <option value="bool">bool</option>
            <option value="int">int</option>
            <option value="float">float</option>
            <option value="string">string</option>
          </select>
        </div>
        <button class="btn primary tag-add-btn" :disabled="!canAdd" @click="addTag">+ Add Tag</button>
      </div>
      <div class="tag-filter-row">
        <input v-model="filter" placeholder="Filter tags by name, type, value, units, or description...">
        <button v-if="filter" class="btn" @click="filter = ''">Clear</button>
        <span class="small">{{ filteredTags.length }} / {{ tagStats.total }} shown</span>
      </div>
    </section>

    <section class="tag-table-wrap">
      <table class="data-table tag-registry-table">
        <thead>
          <tr>
            <th>Name</th><th>Type</th><th>Initial Value</th><th>Value</th><th>Writable</th><th>Units</th><th>Min</th><th>Max</th><th>Description</th><th>Actions</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="t in filteredTags" :key="t.name">
            <td><b>{{ t.name }}</b></td>
            <td>
              <select :value="t.type" @change="updateTag(t.name, 'type', $event.target.value)">
                <option value="bool">bool</option>
                <option value="int">int</option>
                <option value="float">float</option>
                <option value="string">string</option>
              </select>
            </td>
            <td class="tag-value-cell tag-initial-cell">
              <select v-if="t.type === 'bool'" :value="String(t.initialValue)" @change="updateTag(t.name, 'initialValue', $event.target.value === 'true')">
                <option value="false">false</option><option value="true">true</option>
              </select>
              <input v-else-if="t.type === 'string'" :value="displayValue(t.initialValue)" @input="updateTag(t.name, 'initialValue', $event.target.value)">
              <input v-else type="number" :step="t.type === 'int' ? 1 : 'any'" :value="t.initialValue" @input="updateTag(t.name, 'initialValue', $event.target.value)">
            </td>
            <td class="tag-current-value"><span>{{ displayValue(t.value) }}</span></td>
            <td><input class="tag-check" type="checkbox" :checked="t.writable" @change="updateTag(t.name, 'writable', $event.target.checked)"></td>
            <td class="tag-units-cell"><input :disabled="t.type === 'string'" :value="t.units" @input="updateTag(t.name, 'units', $event.target.value)"></td>
            <td class="tag-minmax-cell"><input type="number" :disabled="t.type === 'string'" :value="t.min" @input="updateTag(t.name, 'min', $event.target.value)"></td>
            <td class="tag-minmax-cell"><input type="number" :disabled="t.type === 'string'" :value="t.max" @input="updateTag(t.name, 'max', $event.target.value)"></td>
            <td class="tag-description-cell"><input :value="t.description" @input="updateTag(t.name, 'description', $event.target.value)"></td>
            <td class="tag-row-actions">
              <span v-if="isSystemTag(t)" class="system-tag-label">System</span>
              <button v-else class="btn danger" @click="deleteTag(t.name)">Remove</button>
            </td>
          </tr>
          <tr v-if="filteredTags.length === 0"><td colspan="10" class="small">No tags match the current filter.</td></tr>
        </tbody>
      </table>
    </section>
  </main>
</template>
