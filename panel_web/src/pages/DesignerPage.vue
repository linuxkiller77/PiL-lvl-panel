<script setup>
import { computed, onMounted, onBeforeUnmount, ref } from 'vue';
import { getLayout, getRuntimeLayout, saveLayout, applyLayout, reloadLayout } from '../api/hmiApi.js';
import { getTags, writeTag } from '../api/tagApi.js';
import { getScriptStatus } from '../api/scriptApi.js';
import { designerLayout, designerTags, designerSelected, designerActivePageId, designerLive, designerMessage, designerRaw, designerZoom, designerHistory, designerLoaded, designerDirty, designerRuntimeDirty, designerSourceLabel, appActiveTab, scriptJumpRequest, tagsLoaded, tagsDirty, tagsSourceLabel } from '../stores/panelStore.js';
import WidgetPreview from '../components/widgets/WidgetPreview.vue';

const props = defineProps({ active: { type: Boolean, default: true } });

function defaultLayout() {
  return {
    version: 4,
    screen: { width: 800, height: 480, background: '#101828' },
    startupPage: 'main',
    scriptTick: { enabled: false, rateMs: 250, function: 'OnPanelTick' },
    activePage: 'main',
    pages: [
      {
        id: 'main',
        name: 'Main',
        widgets: [
          { id:'title', type:'label', x:24, y:16, w:430, h:48, events:{}, props:{ label:'PiLab Panel + AngelScript', pin:'', unit:'', min:0, max:100, decimals:1, color:'#ffffff' } },
          { id:'heartbeat', type:'led', x:710, y:20, w:68, h:72, events:{}, props:{ label:'HB', pin:'Panel_Heartbeat', unit:'', min:0, max:100, decimals:1, color:'#22c55e' } },
          { id:'script_status', type:'label', x:28, y:72, w:590, h:44, events:{}, props:{ label:'Script status', pin:'Script_Status', unit:'', min:0, max:100, decimals:1, color:'#facc15' } },
          { id:'toggle_btn', type:'button', x:40, y:138, w:180, h:82, events:{ onClick:'ToggleMotor_Click' }, props:{ label:'TOGGLE MOTOR', pin:'Motor_Start', unit:'', min:0, max:100, decimals:1, color:'#0ea5e9' } },
          { id:'stop_btn', type:'button', x:245, y:138, w:140, h:82, events:{ onClick:'StopMotor_Click' }, props:{ label:'STOP', pin:'Motor_Start', unit:'', min:0, max:100, decimals:1, color:'#ef4444' } },
          { id:'running_led', type:'led', x:420, y:134, w:120, h:92, events:{}, props:{ label:'Running', pin:'Motor_Running', unit:'', min:0, max:100, decimals:1, color:'#22c55e' } },
          { id:'speed_pv', type:'readout', x:575, y:134, w:190, h:92, events:{}, props:{ label:'Speed PV', pin:'Motor_Speed_PV', unit:' rpm', min:0, max:5000, decimals:0, color:'#38bdf8' } },
          { id:'speed_down', type:'button', x:52, y:252, w:110, h:76, events:{ onClick:'SpeedDown_Click' }, props:{ label:'SP -', pin:'Motor_Speed_SP', unit:'', min:0, max:5000, decimals:0, color:'#64748b' } },
          { id:'speed_sp', type:'readout', x:185, y:252, w:170, h:76, events:{}, props:{ label:'Speed SP', pin:'Motor_Speed_SP', unit:' rpm', min:0, max:5000, decimals:0, color:'#f97316' } },
          { id:'speed_up', type:'button', x:380, y:252, w:110, h:76, events:{ onClick:'SpeedUp_Click' }, props:{ label:'SP +', pin:'Motor_Speed_SP', unit:'', min:0, max:5000, decimals:0, color:'#16a34a' } },
          { id:'goto_setup', type:'button', x:520, y:252, w:150, h:76, events:{ onClick:'SetupPage_Click' }, props:{ label:'SETUP', pin:'', unit:'', min:0, max:100, decimals:0, color:'#3b82f6' } },
          { id:'note', type:'label', x:52, y:370, w:640, h:48, events:{}, props:{ label:'ScreenShow("Setup") can switch pages from AngelScript.', pin:'', unit:'', min:0, max:100, decimals:1, color:'#94a3b8' } }
        ]
      },
      {
        id: 'setup',
        name: 'Setup',
        widgets: [
          { id:'setup_title', type:'label', x:24, y:22, w:430, h:48, events:{}, props:{ label:'Setup Screen', pin:'', unit:'', min:0, max:100, decimals:1, color:'#ffffff' } },
          { id:'setup_note', type:'label', x:52, y:112, w:620, h:54, events:{}, props:{ label:'This is a second page. Use ScreenShow("Main") to return.', pin:'', unit:'', min:0, max:100, decimals:1, color:'#facc15' } },
          { id:'back_main', type:'button', x:52, y:250, w:170, h:78, events:{ onClick:'MainPage_Click' }, props:{ label:'BACK', pin:'', unit:'', min:0, max:100, decimals:0, color:'#0ea5e9' } }
        ]
      }
    ]
  };
}

function slugify(s) {
  return String(s || 'screen').trim().toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '') || 'screen';
}

function normalizeWidget(w, i = 0) {
  const props = w?.props && typeof w.props === 'object' ? w.props : {};
  return {
    id: String(w?.id || `widget_${i}`),
    type: String(w?.type || 'label'),
    x: Number(w?.x ?? 40),
    y: Number(w?.y ?? 40),
    w: Number(w?.w ?? 160),
    h: Number(w?.h ?? 70),
    events: w?.events && typeof w.events === 'object' ? { ...w.events } : {},
    props: {
      label: props.label ?? props.text ?? String(w?.label || w?.text || w?.type || 'Widget'),
      pin: props.pin ?? props.tag ?? w?.tag ?? '',
      unit: props.unit ?? props.units ?? '',
      min: Number(props.min ?? 0),
      max: Number(props.max ?? 100),
      decimals: Number(props.decimals ?? 1),
      color: props.color || '#38bdf8',
      value: props.value,
      pressValue: props.pressValue ?? true,
      releaseValue: props.releaseValue ?? false,
    },
  };
}
function normalizePage(p, i = 0) {
  const name = String(p?.name || p?.title || `Screen ${i + 1}`);
  const id = String(p?.id || slugify(name) || `screen_${i + 1}`);
  const widgets = Array.isArray(p?.widgets) ? p.widgets : Array.isArray(p?.objects) ? p.objects : [];
  return { id, name, widgets: widgets.map(normalizeWidget) };
}
function normalizeLayout(input) {
  const base = defaultLayout();
  const src = input && typeof input === 'object' ? input : base;
  let pages = [];
  if (Array.isArray(src.pages) && src.pages.length) {
    pages = src.pages.map(normalizePage);
  } else {
    const widgets = Array.isArray(src.widgets) ? src.widgets : Array.isArray(src.objects) ? src.objects : [];
    pages = [{ id: 'main', name: 'Main', widgets: widgets.map(normalizeWidget) }];
  }
  const screen = {
    width: Number(src.screen?.width ?? 800),
    height: Number(src.screen?.height ?? 480),
    background: src.screen?.background || '#101828',
  };
  const activePage = String(src.activePage || src.editorPage || src.currentPage || pages[0]?.id || 'main');
  const startupPage = String(src.startupPage || src.bootPage || pages[0]?.id || 'main');
  const safeActive = pages.some(p => p.id === activePage) ? activePage : pages[0].id;
  const safeStartup = pages.some(p => p.id === startupPage || p.name === startupPage) ? startupPage : pages[0].id;
  const scriptTickSrc = src.scriptTick && typeof src.scriptTick === 'object' ? src.scriptTick : {};
  const allowedRates = [100, 250, 500, 1000];
  let scriptTickRate = Number(scriptTickSrc.rateMs ?? src.scriptTickMs ?? 250);
  if (!allowedRates.includes(scriptTickRate)) scriptTickRate = 250;
  const scriptTick = {
    enabled: Boolean(scriptTickSrc.enabled ?? src.scriptTickEnabled ?? false),
    rateMs: scriptTickRate,
    function: 'OnPanelTick',
  };
  return {
    version: Number(src.version ?? 5),
    screen,
    startupPage: safeStartup,
    scriptTick,
    activePage: safeActive,
    pages,
    // Keep a legacy widgets mirror for older code/tools. Firmware reads pages first.
    widgets: pages.find(p => p.id === safeActive)?.widgets || pages[0]?.widgets || [],
  };
}

const widgetCatalog = [
  { type:'label', title:'Label', icon:'Aa', category:'Display', w:260, h:48, color:'#e2e8f0' },
  { type:'readout', title:'Readout', icon:'123', category:'Display', w:200, h:84, color:'#38bdf8' },
  { type:'led', title:'LED', icon:'●', category:'Display', w:94, h:82, color:'#22c55e' },
  { type:'gauge', title:'Gauge', icon:'▰', category:'Display', w:260, h:82, color:'#38bdf8' },
  { type:'tank', title:'Tank', icon:'▥', category:'Display', w:110, h:180, color:'#38bdf8' },
  { type:'button', title:'Button', icon:'BTN', category:'Input', w:170, h:78, color:'#0ea5e9' },
  { type:'toggle', title:'Toggle', icon:'↔', category:'Input', w:185, h:80, color:'#8b5cf6' },
  { type:'setpoint', title:'Setpoint', icon:'SP', category:'Input', w:180, h:78, color:'#f97316' },
];
const categories = computed(() => [...new Set(widgetCatalog.map(w => w.category))]);

const layout = designerLayout;
if (!layout.value) layout.value = normalizeLayout(defaultLayout());
const tags = designerTags;
const selected = designerSelected;
const activePageId = designerActivePageId;
const live = designerLive;
const message = designerMessage;
const raw = designerRaw;
const reloadBusy = ref(false);
const applyBusy = ref(false);
const saveBusy = ref(false);
const zoom = designerZoom;
const showHelp = ref(false);
const showHistory = ref(false);
const history = designerHistory;
const dirty = designerDirty;
const runtimeDirty = designerRuntimeDirty;
const sourceLabel = designerSourceLabel;
const guides = ref([]);
let drag = null;
let tagTimer = null;
let scriptStatusTimer = null;
const scriptRuntimeStatus = ref(null);
const scriptStatusMessage = ref('');
const designerTagPickerOpen = ref(false);
const designerTagPickerOriginal = ref('');
const designerTagEditActive = ref(false);

const pages = computed(() => {
  if (!layout.value?.pages?.length) {
    layout.value = normalizeLayout(layout.value);
    activePageId.value = layout.value.activePage || layout.value.pages[0]?.id || 'main';
  }
  return layout.value?.pages || [];
});
const activePageIndex = computed(() => Math.max(0, pages.value.findIndex(p => p.id === activePageId.value)));
const activePage = computed(() => pages.value[activePageIndex.value] || pages.value[0] || { id:'main', name:'Main', widgets:[] });
const currentWidgets = computed(() => activePage.value.widgets || []);
const selectedWidget = computed(() => currentWidgets.value?.[selected.value] || null);
const tagNames = computed(() => (tags.value || []).map(t => t.name).filter(Boolean).sort((a, b) => a.localeCompare(b)));
const widgetCount = computed(() => currentWidgets.value?.length || 0);
const totalWidgetCount = computed(() => pages.value.reduce((sum, p) => sum + (p.widgets?.length || 0), 0));
const zoomLabel = computed(() => `${Math.round(zoom.value * 100)}%`);

function beginDesignerTagEdit() {
  if (!designerTagEditActive.value) {
    designerTagPickerOriginal.value = String(selectedWidget.value?.props?.pin || '');
    designerTagEditActive.value = true;
  }
}
function finishDesignerTagEdit() {
  designerTagEditActive.value = false;
  designerTagPickerOriginal.value = '';
}
function openDesignerTagPicker() {
  beginDesignerTagEdit();
  designerTagPickerOpen.value = true;
}
function closeDesignerTagPicker() {
  designerTagPickerOpen.value = false;
}
function cancelDesignerTagPicker() {
  if (selectedWidget.value) {
    selectedWidget.value.props.pin = designerTagPickerOriginal.value || '';
    saveAndSync();
  }
  closeDesignerTagPicker();
  finishDesignerTagEdit();
}
function handleDesignerTagKeydown(event) {
  if (event.key === 'Escape') {
    event.preventDefault();
    event.stopPropagation();
    cancelDesignerTagPicker();
    return;
  }
  if (event.key === 'Tab') return;
  beginDesignerTagEdit();
  designerTagPickerOpen.value = true;
}
function designerTagOptions(text = '') {
  const q = String(text || '').trim().toLowerCase();
  const names = tagNames.value || [];
  if (!q) return names.slice(0, 80);
  return names.filter(n => n.toLowerCase().includes(q)).slice(0, 80);
}
function selectDesignerTag(name) {
  if (!selectedWidget.value) return;
  selectedWidget.value.props.pin = name;
  closeDesignerTagPicker();
  finishDesignerTagEdit();
  saveAndSync();
}
function updateDesignerTagText() {
  beginDesignerTagEdit();
  designerTagPickerOpen.value = true;
  saveAndSync();
}

function syncLegacyWidgets() {
  if (!layout.value) return;
  layout.value.activePage = activePageId.value;
  if (!layout.value.startupPage) layout.value.startupPage = layout.value.pages?.[0]?.id || activePageId.value;
  layout.value.widgets = currentWidgets.value;
}
function ensurePages() {
  layout.value = normalizeLayout(layout.value);
  if (!pages.value.some(p => p.id === activePageId.value)) activePageId.value = layout.value.activePage || pages.value[0].id;
  syncLegacyWidgets();
}
function ensureWidgets() { ensurePages(); if (!Array.isArray(activePage.value.widgets)) activePage.value.widgets = []; }
function pushHistory(label, kind = 'edit') {
  history.value.unshift({ label, kind, at:'now', icon: kind === 'add' ? '+' : kind === 'move' ? '✥' : kind === 'delete' ? '−' : kind === 'resize' ? '↘' : kind === 'save' ? '✓' : '•' });
  history.value = history.value.slice(0, 24);
}
function catalogFor(type) { return widgetCatalog.find(w => w.type === type) || widgetCatalog[0]; }
function markDirty() { dirty.value = true; runtimeDirty.value = true; }
function widgetDefault(type){
  const spec = catalogFor(type);
  return normalizeWidget({
    id: `${type}_${Date.now().toString(36)}`,
    type,
    x:80,
    y:70,
    w:spec.w,
    h:spec.h,
    events:{},
    props:{ label:spec.title, pin:'', unit:type==='readout'?' rpm':'', min:0, max:100, decimals:type==='readout'?0:1, color:spec.color }
  });
}
function add(type){ ensureWidgets(); currentWidgets.value.push(widgetDefault(type)); selected.value=currentWidgets.value.length-1; pushHistory(`Add widget ${type}`, 'add'); markDirty(); syncRaw(); }
function remove(i = selected.value){ ensureWidgets(); if(i < 0 || i >= currentWidgets.value.length) return; const name = currentWidgets.value[i].id; currentWidgets.value.splice(i,1); selected.value=Math.min(i, currentWidgets.value.length-1); if(!currentWidgets.value.length) selected.value=-1; pushHistory(`Delete ${name}`, 'delete'); markDirty(); syncRaw(); }
function duplicate(){ if(!selectedWidget.value) return; const copy = normalizeWidget(JSON.parse(JSON.stringify(selectedWidget.value))); copy.id = `${copy.id}_copy`; copy.x += 20; copy.y += 20; currentWidgets.value.push(copy); selected.value = currentWidgets.value.length - 1; pushHistory(`Duplicate ${copy.type}`, 'add'); markDirty(); syncRaw(); }
function tagVal(name){ const t=tags.value.find(x=>x.name===name); return t ? t.value : undefined; }
function fmt(w){ let v=tagVal(w.props.pin); if(v===undefined)v=w.props.value??w.props.label??0; if(typeof v==='boolean')return v?'ON':'OFF'; const n=Number(v); return Number.isFinite(n)?n.toFixed(Number(w.props.decimals??1))+(w.props.unit||''):String(v); }
function pct(w){ let v=Number(tagVal(w.props.pin)??w.props.value??0), mn=Number(w.props.min??0), mx=Number(w.props.max??100); return mx===mn?0:Math.max(0,Math.min(100,(v-mn)*100/(mx-mn))); }
function styleFor(w){ return { left:w.x+'px', top:w.y+'px', width:w.w+'px', height:w.h+'px', '--c': w.props.color||'#38bdf8', '--widget-color': w.props.color||'#38bdf8' }; }

function selectPage(id) { activePageId.value = id; selected.value = -1; syncLegacyWidgets(); syncRaw(); }
function addPage() {
  ensurePages();
  let n = pages.value.length + 1;
  let id = `screen_${n}`;
  while (pages.value.some(p => p.id === id)) { n++; id = `screen_${n}`; }
  layout.value.pages.push({ id, name: `Screen ${n}`, widgets: [] });
  activePageId.value = id;
  selected.value = -1;
  pushHistory(`Add screen ${n}`, 'add');
  markDirty();
  syncRaw();
}
function deletePage(id) {
  ensurePages();
  if (pages.value.length <= 1) { message.value = 'At least one screen is required'; return; }
  const idx = pages.value.findIndex(p => p.id === id);
  if (idx < 0) return;
  const removed = pages.value[idx].name;
  layout.value.pages.splice(idx, 1);
  activePageId.value = layout.value.pages[Math.max(0, idx - 1)]?.id || layout.value.pages[0].id;
  if (!layout.value.pages.some(p => p.id === layout.value.startupPage)) layout.value.startupPage = layout.value.pages[0].id;
  selected.value = -1;
  pushHistory(`Delete screen ${removed}`, 'delete');
  markDirty();
  syncRaw();
}
function renameActivePage() {
  const p = activePage.value;
  if (!p) return;
  const next = prompt('Screen name', p.name);
  if (!next || next === p.name) return;
  p.name = next.trim() || p.name;
  pushHistory(`Rename screen ${p.name}`, 'edit');
  markDirty();
  syncRaw();
}
function setStartupPage(id = activePage.value?.id) {
  ensurePages();
  if (!id || !pages.value.some(p => p.id === id)) return;
  layout.value.startupPage = id;
  pushHistory(`Set startup screen ${pages.value.find(p => p.id === id)?.name || id}`, 'edit');
  markDirty();
  syncRaw();
}
function startupPageName() {
  const id = layout.value?.startupPage || pages.value[0]?.id || '';
  return pages.value.find(p => p.id === id || p.name === id)?.name || id || 'Main';
}

function commitPageIdChange(raw) {
  ensurePages();
  const p = activePage.value;
  if (!p) return;

  const oldId = p.id;
  let nextId = slugify(raw || oldId);
  if (!nextId) nextId = oldId || `screen_${activePageIndex.value + 1}`;

  // Keep IDs unique because the firmware, startupPage, and ScreenShow() all
  // depend on a stable page identifier.
  let candidate = nextId;
  let suffix = 2;
  while (pages.value.some(page => page !== p && page.id === candidate)) {
    candidate = `${nextId}_${suffix++}`;
  }
  nextId = candidate;

  if (nextId === oldId) {
    p.id = nextId;
    return;
  }

  p.id = nextId;
  if (activePageId.value === oldId) activePageId.value = nextId;
  if (layout.value.activePage === oldId) layout.value.activePage = nextId;
  if (layout.value.startupPage === oldId) layout.value.startupPage = nextId;

  pushHistory(`Change screen ID ${oldId} → ${nextId}`, 'edit');
  markDirty();
  syncRaw();
}

function snap(value, step = 8) { return Math.round(value / step) * step; }
function updateGuides(w) {
  const sw = layout.value.screen?.width || 800;
  const sh = layout.value.screen?.height || 480;
  const out = [];
  const cx = w.x + w.w / 2;
  const cy = w.y + w.h / 2;
  if (Math.abs(cx - sw / 2) < 5) out.push({ kind:'v', x: sw / 2, label:'screen center' });
  if (Math.abs(cy - sh / 2) < 5) out.push({ kind:'h', y: sh / 2, label:'screen center' });
  for (const other of currentWidgets.value || []) {
    if (other === w) continue;
    const ocx = other.x + other.w / 2;
    const ocy = other.y + other.h / 2;
    if (Math.abs(w.x - other.x) < 4) out.push({ kind:'v', x: other.x, label:'left' });
    if (Math.abs(w.x + w.w - (other.x + other.w)) < 4) out.push({ kind:'v', x: other.x + other.w, label:'right' });
    if (Math.abs(cx - ocx) < 5) out.push({ kind:'v', x: ocx, label:'center' });
    if (Math.abs(w.y - other.y) < 4) out.push({ kind:'h', y: other.y, label:'top' });
    if (Math.abs(w.y + w.h - (other.y + other.h)) < 4) out.push({ kind:'h', y: other.y + other.h, label:'bottom' });
    if (Math.abs(cy - ocy) < 5) out.push({ kind:'h', y: ocy, label:'center' });
  }
  guides.value = out.slice(0, 4);
}
function start(ev,i,mode){ if(live.value)return; selected.value=i; drag={i,mode,sx:ev.clientX,sy:ev.clientY,ox:currentWidgets.value[i].x,oy:currentWidgets.value[i].y,ow:currentWidgets.value[i].w,oh:currentWidgets.value[i].h, moved:false}; ev.currentTarget.setPointerCapture?.(ev.pointerId); }
function move(ev){
  if(!drag)return;
  const w=currentWidgets.value[drag.i];
  const dx=(ev.clientX-drag.sx)/zoom.value;
  const dy=(ev.clientY-drag.sy)/zoom.value;
  drag.moved = drag.moved || Math.abs(dx) > 1 || Math.abs(dy) > 1;
  if(drag.mode==='move'){
    w.x=Math.max(0,Math.min((layout.value.screen?.width || 800)-w.w,snap(drag.ox+dx)));
    w.y=Math.max(0,Math.min((layout.value.screen?.height || 480)-w.h,snap(drag.oy+dy)));
    updateGuides(w);
  } else {
    w.w=Math.max(32,Math.min((layout.value.screen?.width || 800)-w.x,snap(drag.ow+dx)));
    w.h=Math.max(24,Math.min((layout.value.screen?.height || 480)-w.y,snap(drag.oh+dy)));
    updateGuides(w);
  }
}
function stop(){ if(drag){ if(drag.moved) { pushHistory(`${drag.mode === 'move' ? 'Move' : 'Resize'} ${currentWidgets.value[drag.i]?.type || 'widget'}`, drag.mode); markDirty(); } syncRaw(); } drag=null; guides.value=[]; }
async function clickWidget(w,i){ if(live.value){ if((w.type==='button'||w.type==='toggle') && w.props.pin && !w.events?.onClick){ const current=!!tagVal(w.props.pin); await writeTag(w.props.pin, !current); await refreshTags(); } return; } selected.value=i; }
async function applyToLcd(){
  if (applyBusy.value) return;
  applyBusy.value = true;
  try {
    await applyLayout(normalizeLayout(layout.value));
    runtimeDirty.value = false;
    message.value = 'Applied all screens to LCD runtime RAM. Load from LCD will now return this runtime copy.';
    pushHistory('Apply screens to LCD RAM', 'save');
    syncRaw();
  } finally { setTimeout(()=>{ applyBusy.value = false; }, 600); }
}
async function save(){
  if (saveBusy.value) return;
  saveBusy.value = true;
  try {
    await saveLayout(normalizeLayout(layout.value));
    dirty.value = false;
    sourceLabel.value = 'device flash';
    message.value='Saved layout/screens to flash. Use Apply to LCD to update the running screen if needed.';
    pushHistory('Save layout to flash', 'save');
    syncRaw();
  } finally { setTimeout(()=>{ saveBusy.value = false; }, 500); }
}
async function reload(){
  if (reloadBusy.value) return;
  reloadBusy.value = true;
  try { await reloadLayout(); runtimeDirty.value = false; message.value='LCD runtime reloaded from its current runtime copy'; pushHistory('Reload LCD runtime', 'save'); }
  finally { setTimeout(()=>{ reloadBusy.value = false; }, 1000); }
}
async function load(force = false){
  if (designerLoaded.value && !force) { ensurePages(); syncRaw(); return; }
  try {
    layout.value = normalizeLayout(await getRuntimeLayout());
    activePageId.value = layout.value.activePage || layout.value.startupPage || layout.value.pages[0].id;
    message.value='Loaded current LCD runtime layout';
    sourceLabel.value='lcd runtime';
    dirty.value=false;
    runtimeDirty.value=false;
    selected.value=-1;
    pushHistory('Load current LCD layout', 'open');
  }
  catch(e) {
    try {
      layout.value = normalizeLayout(await getLayout());
      activePageId.value = layout.value.activePage || layout.value.pages[0].id;
      message.value='Loaded saved flash layout because LCD runtime load failed';
      sourceLabel.value='device flash fallback';
      dirty.value=false;
      runtimeDirty.value=false;
      selected.value=-1;
    } catch (e2) {
      layout.value = normalizeLayout(defaultLayout());
      activePageId.value = layout.value.activePage;
      message.value=`Using embedded fallback because panel layout load failed: ${e2?.message || e2}`;
      sourceLabel.value='embedded fallback';
      dirty.value=true;
      runtimeDirty.value=true;
    }
  }
  designerLoaded.value = true;
  syncRaw();
}
async function resetDemo(){ layout.value = normalizeLayout(defaultLayout()); activePageId.value = layout.value.activePage; selected.value=-1; markDirty(); syncRaw(); pushHistory('Load default multi-screen demo', 'open'); message.value='Default multi-screen event demo loaded in browser RAM only. Apply to LCD or Save to Flash when ready.'; sourceLabel.value='browser RAM demo'; }
async function refreshTags(){
  if (tagsDirty.value) return; // Do not destroy unsaved tag edits with a background /api/tags refresh.
  try {
    const res = await getTags();
    tags.value = Array.isArray(res?.tags) ? res.tags : [];
    tagsLoaded.value = true;
    tagsSourceLabel.value = res?.source || 'lcd runtime';
  } catch(e) { /* keep last tags while editing */ }
}
function applyRaw(){ layout.value = normalizeLayout(JSON.parse(raw.value)); activePageId.value = layout.value.activePage || layout.value.pages[0].id; selected.value=-1; pushHistory('Apply raw JSON', 'edit'); markDirty(); syncRaw(); }
function syncRaw(){ ensurePages(); raw.value = JSON.stringify(normalizeLayout(layout.value),null,2); }
function saveAndSync(){ markDirty(); syncRaw(); }
function sanitizeScriptFunctionName(name) {
  return String(name || '')
    .trim()
    .replace(/\(\s*\)\s*$/, '')
    .replace(/[^A-Za-z0-9_]/g, '_')
    .replace(/^[^A-Za-z_]+/, '')
    .replace(/^$/, 'Widget_Handler');
}
function defaultHandlerName(eventName) {
  const base = sanitizeScriptFunctionName(selectedWidget.value?.id || selectedWidget.value?.props?.label || selectedWidget.value?.type || 'Widget');
  return `${base}_${eventName === 'onChange' ? 'Changed' : 'Click'}`;
}
function jumpToEventHandler(eventName) {
  if (!selectedWidget.value) return;
  if (!selectedWidget.value.events || typeof selectedWidget.value.events !== 'object') selectedWidget.value.events = {};
  let handler = sanitizeScriptFunctionName(selectedWidget.value.events[eventName]);
  if (!handler) handler = defaultHandlerName(eventName);
  selectedWidget.value.events[eventName] = handler;
  saveAndSync();
  scriptJumpRequest.value = {
    id: Date.now(),
    functionName: handler,
    eventName,
    widgetId: selectedWidget.value.id,
    widgetLabel: selectedWidget.value.props?.label || selectedWidget.value.id,
    createIfMissing: true,
  };
  appActiveTab.value = 'script';
}

function ensureScriptTickConfig() {
  if (!layout.value.scriptTick || typeof layout.value.scriptTick !== 'object') {
    layout.value.scriptTick = { enabled: false, rateMs: 250, function: 'OnPanelTick' };
  }
  layout.value.scriptTick.function = 'OnPanelTick';
  const allowedRates = [100, 250, 500, 1000];
  if (!allowedRates.includes(Number(layout.value.scriptTick.rateMs))) layout.value.scriptTick.rateMs = 250;
  return layout.value.scriptTick;
}
function saveScriptTickConfig() {
  ensureScriptTickConfig();
  saveAndSync();
}
function jumpToPanelTickHandler() {
  ensureScriptTickConfig();
  saveAndSync();
  scriptJumpRequest.value = {
    id: Date.now(),
    functionName: 'OnPanelTick',
    eventName: 'periodic panel tick',
    widgetId: 'panel',
    widgetLabel: 'Panel runtime',
    createIfMissing: true,
  };
  appActiveTab.value = 'script';
}

async function refreshScriptRuntimeStatus() {
  try {
    const status = await getScriptStatus();
    scriptRuntimeStatus.value = status?.script_runtime || null;
    scriptStatusMessage.value = '';
  } catch (e) {
    scriptStatusMessage.value = e?.message || 'Script status unavailable';
  }
}
const panelTickMissing = computed(() => Boolean(layout.value?.scriptTick?.enabled && scriptRuntimeStatus.value?.panel_tick_missing));
const panelTickStats = computed(() => scriptRuntimeStatus.value || {});

function selectById(id){ const idx = currentWidgets.value.findIndex(w => w.id === id); if (idx >= 0) selected.value = idx; }
function nudge(dx, dy) { if (!selectedWidget.value || live.value) return; selectedWidget.value.x = Math.max(0, Math.min((layout.value.screen?.width || 800) - selectedWidget.value.w, selectedWidget.value.x + dx)); selectedWidget.value.y = Math.max(0, Math.min((layout.value.screen?.height || 480) - selectedWidget.value.h, selectedWidget.value.y + dy)); markDirty(); syncRaw(); }
function isEditableKeyTarget(ev) {
  const path = typeof ev.composedPath === 'function' ? ev.composedPath() : [];
  const nodes = path.length ? path : [ev.target];
  return nodes.some(node => {
    if (!node || !node.tagName) return false;
    const tag = node.tagName.toLowerCase();
    return tag === 'input' || tag === 'textarea' || tag === 'select' || node.isContentEditable || node.getAttribute?.('role') === 'textbox';
  });
}
function keydown(ev) {
  if (!props.active) return;
  if (isEditableKeyTarget(ev)) return;
  if (ev.key === '?') { showHelp.value = true; ev.preventDefault(); return; }
  if (ev.key === 'Delete' || ev.key === 'Backspace') {
    if (selectedWidget.value) { remove(); ev.preventDefault(); }
    return;
  }
  if ((ev.ctrlKey || ev.metaKey) && ev.key.toLowerCase() === 'd') {
    if (selectedWidget.value) { duplicate(); ev.preventDefault(); }
    return;
  }
  if (['ArrowLeft','ArrowRight','ArrowUp','ArrowDown'].includes(ev.key)) {
    if (selectedWidget.value && !live.value) {
      const step = ev.shiftKey ? 8 : 1;
      nudge(ev.key === 'ArrowLeft' ? -step : ev.key === 'ArrowRight' ? step : 0, ev.key === 'ArrowUp' ? -step : ev.key === 'ArrowDown' ? step : 0);
      ev.preventDefault();
    }
  }
}
onMounted(async()=>{ await load(); await refreshTags(); await refreshScriptRuntimeStatus(); tagTimer = setInterval(refreshTags,1500); scriptStatusTimer = setInterval(refreshScriptRuntimeStatus,3000); window.addEventListener('keydown', keydown); });
onBeforeUnmount(()=>{ closeDesignerTagPicker(); finishDesignerTagEdit(); if(tagTimer) clearInterval(tagTimer); if(scriptStatusTimer) clearInterval(scriptStatusTimer); window.removeEventListener('keydown', keydown); });
</script>
<template>
  <main class="designer-shell page" @pointermove="move" @pointerup="stop">
    <aside class="designer-left">
      <div class="designer-panel panel-title">
        <div>
          <h2>Widgets</h2>
          <p>Edits stay in browser RAM until applied or saved.</p>
        </div>
        <button class="icon-btn" title="Keyboard shortcuts" @click="showHelp=true">⌘</button>
      </div>

      <div v-for="cat in categories" :key="cat" class="designer-section">
        <h3>{{ cat }}</h3>
        <div class="widget-palette">
          <button v-for="w in widgetCatalog.filter(x=>x.category===cat)" :key="w.type" class="palette-card" @click="add(w.type)">
            <span class="palette-icon">{{ w.icon }}</span>
            <span><b>{{ w.title }}</b><small>{{ w.type }}</small></span>
          </button>
        </div>
      </div>

      <div class="designer-section compact-actions">
        <button :class="['mode-toggle',{live}]" @click="live=!live"><span>{{ live ? 'Live' : 'Build' }}</span><small>Mode</small></button>
        <button class="btn tool" @click="resetDemo">Load Default Multi-Screen Demo</button>
        <div class="status-pill">{{ message }}</div>
        <div class="status-pill">Source: {{ sourceLabel }} · Flash: {{ dirty ? 'dirty' : 'clean' }} · LCD: {{ runtimeDirty ? 'not applied' : 'current' }}</div>
      </div>

      <div class="designer-section">
        <div class="section-row"><h3>Elements</h3><span class="badge">{{ widgetCount }} / {{ totalWidgetCount }}</span></div>
        <div class="element-tree">
          <button v-for="(w,i) in currentWidgets" :key="w.id" :class="['element-row',{active:selected===i}]" @click="selectById(w.id)">
            <span class="element-dot">{{ catalogFor(w.type).icon }}</span>
            <span><b>{{w.id}}</b><small>{{w.type}}</small></span>
          </button>
        </div>
      </div>
    </aside>

    <section class="designer-center">
      <div class="screen-tabs-bar">
        <button v-for="p in pages" :key="p.id" :class="['screen-tab',{active:p.id===activePage.id}]" @click="selectPage(p.id)" @dblclick="renameActivePage">
          <span class="screen-icon">▣</span><span>{{ p.name }}</span><span v-if="layout.startupPage === p.id" class="startup-mark" title="Startup screen">★</span><span class="screen-close" title="Delete screen" @click.stop="deletePage(p.id)">×</span>
        </button>
        <button class="screen-add" title="Add screen" @click="addPage">+</button>
      </div>
      <div class="canvas-toolbar">
        <div class="toolbar-group">
          <button class="mini-btn" @click="zoom=Math.max(.5, zoom-.25)">−</button>
          <button :class="['mini-btn',{active:zoom===.5}]" @click="zoom=.5">0.5×</button>
          <button :class="['mini-btn',{active:zoom===1}]" @click="zoom=1">1×</button>
          <button :class="['mini-btn',{active:zoom===2}]" @click="zoom=2">2×</button>
          <button class="mini-btn" @click="zoom=Math.min(3, zoom+.25)">+</button>
        </div>
        <div class="toolbar-status"><span>{{ zoomLabel }}</span><span>{{ layout.screen?.width || 800 }} × {{ layout.screen?.height || 480 }}</span><span>{{ activePage.name }}</span></div>
        <div class="toolbar-group">
          <button class="mini-btn" @click="showHistory=!showHistory">History</button>
          <button class="mini-btn" @click="showHelp=true">Shortcuts</button>
        </div>
      </div>

      <div class="canvas-stage">
        <div class="canvas-outer" :style="{transform:`scale(${zoom})`}">
          <div class="canvas" :style="{width:(layout.screen?.width||800)+'px',height:(layout.screen?.height||480)+'px',backgroundColor: layout.screen?.background||'#101828'}" @click="selected=-1">
            <div v-if="!currentWidgets.length" class="empty-canvas-hint">
              <div class="empty-canvas-icon">▦</div>
              <b>{{ activePage.name }} is empty</b>
              <span>Add widgets from the left palette.</span>
            </div>
            <div v-for="g in guides" :key="`${g.kind}-${g.x || g.y}-${g.label}`" :class="['smart-guide', g.kind]" :style="g.kind==='v'?{left:g.x+'px'}:{top:g.y+'px'}"><span>{{ g.label }}</span></div>

            <div v-for="(w,i) in currentWidgets" :key="w.id" :class="['widget','preview-'+w.type,{selected:selected===i,live} ]" :style="styleFor(w)" @click.stop="clickWidget(w,i)" @pointerdown="start($event,i,'move')">
              <WidgetPreview :widget="w" :value="tagVal(w.props.pin)" :formatted="fmt(w)" :percent="pct(w)" />
              <div v-if="!live" class="resize" @pointerdown.stop="start($event,i,'resize')"></div>
            </div>
          </div>
        </div>
      </div>

      <div v-if="showHistory" class="history-popover">
        <div class="popover-head"><b>History</b><span>{{ history.length }} / 24</span></div>
        <button v-for="(h,hi) in history" :key="hi + h.label" class="history-row">
          <span>{{ h.icon }}</span><b>{{ h.label }}</b><small>{{ h.at }}</small>
        </button>
      </div>
    </section>

    <aside class="designer-right">
      <div class="designer-panel inspector-head">
        <div><h2>Inspector</h2><p>{{ selectedWidget ? selectedWidget.type : 'Screen' }}</p></div>
        <span class="badge">{{ selectedWidget ? selectedWidget.id : activePage.name }}</span>
      </div>

      <div v-if="!selectedWidget" class="inspector-form">
        <label>Screen Name</label><input v-model="activePage.name" @input="saveAndSync">
        <label>Screen ID for AngelScript ScreenShow()</label><input :value="activePage.id" @change="commitPageIdChange($event.target.value)">
        <label>Startup Screen</label>
        <select v-model="layout.startupPage" @change="saveAndSync">
          <option v-for="p in pages" :key="p.id" :value="p.id">{{ p.name }} ({{ p.id }})</option>
        </select>
        <button class="btn tool" @click="setStartupPage()">Set Current Screen as Startup</button>
        <div class="status-pill">Boot/apply starts on: <b>{{ startupPageName() }}</b><br>Switch from script with:<br><code>ScreenShow("{{ activePage.id }}");</code><template v-if="activePage.name && activePage.name !== activePage.id"><br><code>ScreenShow("{{ activePage.name }}");</code></template></div>
        <div class="inspector-subsection">
          <label class="inline-check panel-tick-check">
            <input type="checkbox" v-model="layout.scriptTick.enabled" @change="saveScriptTickConfig">
            Enable OnPanelTick()
          </label>
          <label>OnPanelTick Rate</label>
          <select v-model.number="layout.scriptTick.rateMs" :disabled="!layout.scriptTick.enabled" @change="saveScriptTickConfig">
            <option :value="100">100 ms</option>
            <option :value="250">250 ms</option>
            <option :value="500">500 ms</option>
            <option :value="1000">1 s</option>
          </select>
          <label>Periodic AngelScript Function</label>
          <div class="event-edit-row">
            <input value="OnPanelTick" readonly>
            <button class="mini-btn event-jump-btn" title="Open or create OnPanelTick() in the Script editor" @click="jumpToPanelTickHandler">Edit</button>
          </div>
          <div class="status-pill">When enabled, app_logic queues <code>OnPanelTick()</code> at the selected rate. Ticks are skipped if the previous tick is still pending/running.</div>
          <div v-if="panelTickMissing" class="status-pill warn">OnPanelTick is enabled, but <code>void OnPanelTick()</code> was not found in the compiled script. Ticks are being skipped. Click <b>Edit</b> to create it, then compile the script.</div>
          <div v-else-if="layout.scriptTick.enabled" class="status-pill">Tick stats: executed {{ panelTickStats.panel_tick_executed_count || 0 }}, skipped {{ panelTickStats.panel_tick_skipped_count || 0 }}</div>
        </div>
      </div>
      <div v-else class="inspector-form">
        <div class="row"><button class="btn danger" @click="remove()">Delete</button><button class="btn" @click="duplicate">Duplicate</button></div>
        <label>ID</label><input v-model="selectedWidget.id" @change="saveAndSync">
        <div class="row"><div><label>X</label><input type="number" v-model.number="selectedWidget.x" @change="saveAndSync"></div><div><label>Y</label><input type="number" v-model.number="selectedWidget.y" @change="saveAndSync"></div></div>
        <div class="row"><div><label>W</label><input type="number" v-model.number="selectedWidget.w" @change="saveAndSync"></div><div><label>H</label><input type="number" v-model.number="selectedWidget.h" @change="saveAndSync"></div></div>
        <label>Label</label><input v-model="selectedWidget.props.label" @input="saveAndSync">
        <label>Tag Binding</label>
        <div class="mqtt-tag-select designer-tag-select">
          <input v-model="selectedWidget.props.pin" placeholder="Tag_Name or type manually" @focus="openDesignerTagPicker" @click="openDesignerTagPicker" @input="updateDesignerTagText" @keydown="handleDesignerTagKeydown($event)">
          <button type="button" class="mqtt-tag-select-btn" title="Select tag" @click="openDesignerTagPicker">⌄</button>
          <div v-if="designerTagPickerOpen" class="mqtt-tag-popup designer-tag-popup">
            <div class="mqtt-tag-popup-head"><span>Select tag</span><button type="button" @click="cancelDesignerTagPicker">×</button></div>
            <button type="button" class="mqtt-tag-option typed" @click="selectDesignerTag('')">— no tag —</button>
            <button v-if="selectedWidget.props.pin" type="button" class="mqtt-tag-option typed" @click="selectDesignerTag(selectedWidget.props.pin)">Use typed value: {{ selectedWidget.props.pin }}</button>
            <button v-for="name in designerTagOptions(selectedWidget.props.pin)" :key="name" type="button" class="mqtt-tag-option" @click="selectDesignerTag(name)">{{ name }}</button>
            <div v-if="!designerTagOptions(selectedWidget.props.pin).length" class="mqtt-tag-empty">No matching tags</div>
          </div>
        </div>
        <label>onClick AngelScript Function</label>
        <div class="event-edit-row">
          <input v-model="selectedWidget.events.onClick" placeholder="ToggleMotor_Click" @input="saveAndSync">
          <button class="mini-btn event-jump-btn" title="Open or create this onClick handler in the Script editor" @click="jumpToEventHandler('onClick')">Edit</button>
        </div>
        <label>onChange AngelScript Function</label>
        <div class="event-edit-row">
          <input v-model="selectedWidget.events.onChange" placeholder="SpeedSetpoint_Changed" @input="saveAndSync">
          <button class="mini-btn event-jump-btn" title="Open or create this onChange handler in the Script editor" @click="jumpToEventHandler('onChange')">Edit</button>
        </div>
        <div class="row"><div><label>Color</label><input type="color" v-model="selectedWidget.props.color" @input="saveAndSync"></div><div><label>Unit</label><input v-model="selectedWidget.props.unit" @input="saveAndSync"></div></div>
        <div class="row"><div><label>Min</label><input type="number" v-model.number="selectedWidget.props.min" @change="saveAndSync"></div><div><label>Max</label><input type="number" v-model.number="selectedWidget.props.max" @change="saveAndSync"></div></div>
        <details class="json-details"><summary>Raw Layout JSON</summary><textarea v-model="raw" @focus="syncRaw"></textarea><button class="btn tool" @click="applyRaw">Apply Raw JSON</button></details>
      </div>
    </aside>

    <div v-if="showHelp" class="modal-backdrop" @click.self="showHelp=false">
      <div class="shortcut-modal">
        <div class="modal-title"><span class="modal-icon">⌨</span><b>Keyboard Shortcuts</b><button @click="showHelp=false">×</button></div>
        <div class="shortcut-section"><h3>Navigation</h3><p><span>Move selected widget 1px</span><kbd>← → ↑ ↓</kbd></p><p><span>Move selected widget 8px</span><kbd>Shift</kbd><kbd>← → ↑ ↓</kbd></p><p><span>Deselect / Close modal</span><kbd>Esc</kbd></p></div>
        <div class="shortcut-section"><h3>Editing</h3><p><span>Delete selected widget</span><kbd>Delete</kbd><kbd>Backspace</kbd></p><p><span>Duplicate selected widget</span><kbd>Ctrl</kbd><kbd>D</kbd></p><p><span>Open shortcuts</span><kbd>?</kbd></p></div>
        <button class="btn tool" @click="showHelp=false">Close</button>
      </div>
    </div>
  </main>
</template>
