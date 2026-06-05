<template>
  <main class="page script-page">
    <section class="editor-panel">
      <div class="script-toolbar-row">
        <button class="btn primary" @click="compile">Compile to RAM</button>
        <span class="script-mini-status" :class="`tone-${statusTone}`">{{ statusSummary }}</span>
      </div>

      <div class="script-alert" :class="`tone-${statusTone}`">
        <div>
          <b>{{ statusTitle }}</b>
          <span>{{ statusMessage }}</span>
        </div>
        <small>Source: {{ sourceLabel }} · Flash: {{ scriptDirty ? 'dirty' : 'clean' }} · Runtime: {{ scriptRuntimeDirty ? 'not compiled' : 'compiled' }}</small>
      </div>


      <div ref="editorHost" class="script-editor-host"></div>
    </section>

    <aside class="side-panel">
      <h2>AngelScript Events</h2>

      <label>Call function manually</label>
      <div class="row call-function-row">
        <select v-model="callName" class="function-select">
          <option value="" disabled>{{ callableFunctions.length ? 'Select function…' : 'No no-argument functions found' }}</option>
          <option v-for="fn in callableFunctions" :key="fn.name" :value="fn.name">{{ fn.name }}()</option>
        </select>
        <button class="btn" :disabled="!callName" @click="call(callName)">Call</button>
      </div>
      <p class="small function-hint" v-if="scriptFunctions.length">
        {{ callableFunctions.length }} callable function{{ callableFunctions.length === 1 ? '' : 's' }} found.
        <span v-if="nonCallableFunctionCount">{{ nonCallableFunctionCount }} function{{ nonCallableFunctionCount === 1 ? '' : 's' }} with parameters hidden.</span>
      </p>

      <h2>Status</h2>
      <div class="script-status-box">
        <div><span>Editor</span><b>{{ editorReady ? 'Ready' : 'Starting' }}</b></div>
        <div><span>Engine</span><b>{{ statusText }}</b></div>
        <div><span>Compile</span><b :class="`status-${statusTone}`">{{ compileState }}</b></div>
        <div><span>Runtime</span><b :class="runtimeValid ? 'status-ok' : 'status-bad'">{{ runtimeValid ? 'valid' : 'not valid' }}</b></div>
        <div><span>Active</span><b>{{ status.active || '—' }}</b></div>
        <div><span>Internal RAM</span><b>{{ formatBytes(status.free_internal) }}</b></div>
        <div><span>PSRAM</span><b>{{ formatBytes(status.free_psram) }}</b></div>
      </div>
      <details class="raw-details"><summary>Raw status JSON</summary><pre>{{ JSON.stringify(status, null, 2) }}</pre></details>

      <h2>Console</h2>
      <div class="console">{{ consoleText }}</div>
    </aside>
  </main>
</template>

<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { minimalEditor } from 'prism-code-editor-lightweight/setups';
import { defaultCommands, editHistory } from 'prism-code-editor-lightweight/commands';
import { matchBrackets } from 'prism-code-editor-lightweight/match-brackets';
import { highlightBracketPairs } from 'prism-code-editor-lightweight/highlight-brackets';
import { indentGuides } from 'prism-code-editor-lightweight/guides';
import { autoComplete, fuzzyFilter, registerCompletions } from 'prism-code-editor-lightweight/autocomplete';
import { cursorPosition } from 'prism-code-editor-lightweight/cursor';
import autocompleteCss from 'prism-code-editor-lightweight/autocomplete.css?raw';
import autocompleteIconsCss from 'prism-code-editor-lightweight/autocomplete-icons.css?raw';
import 'prism-code-editor-lightweight/prism/languages/cpp';
import 'prism-code-editor-lightweight/languages/clike';
import { getScript, getRuntimeScript, saveScript, compileScript, callScript, getScriptStatus } from '../api/scriptApi.js';
import { scriptStatus, scriptConsoleText, scriptCallName, scriptSourceLabel, scriptSource, scriptLoaded, scriptDirty, scriptRuntimeDirty, scriptExternalRevision, scriptJumpRequest } from '../stores/panelStore.js';
import { ANGELSCRIPT_COMPLETIONS } from '../editor/angelscriptCompletions.js';

const DEFAULT_SCRIPT = `//PiLab Panel - Angelscript Editor

void OnPanelStart()
{
  TagWriteBool("Motor_Start", false);
  TagWriteBool("Motor_Running", false);
  TagWriteFloat("Motor_Speed_SP", 1200.0f);
  TagWriteFloat("Motor_Speed_PV", 0.0f);
  TagWriteString("Script_Status", "AngelScript ready");
  LogInfo("PiLab Panel AngelScript demo started");
}

void ToggleMotor_Click()
{
  bool running = TagReadBool("Motor_Start", false);
  running = !running;
  TagWriteBool("Motor_Start", running);
  TagWriteBool("Motor_Running", running);
  TagWriteFloat("Motor_Speed_PV", running ? TagReadFloat("Motor_Speed_SP", 1200.0f) : 0.0f);
  TagWriteString("Script_Status", running ? "Script: motor start requested" : "Script: motor stopped");
}

void StopMotor_Click()
{
  TagWriteBool("Motor_Start", false);
  TagWriteBool("Motor_Running", false);
  TagWriteFloat("Motor_Speed_PV", 0.0f);
  TagWriteString("Script_Status", "Script: stop pressed");
}

void SpeedUp_Click()
{
  float sp = TagReadFloat("Motor_Speed_SP", 1200.0f) + 250.0f;
  if (sp > 5000.0f) sp = 5000.0f;
  TagWriteFloat("Motor_Speed_SP", sp);
  if (TagReadBool("Motor_Start", false)) TagWriteFloat("Motor_Speed_PV", sp);
  TagWriteString("Script_Status", "Script: speed increased");
}

void SpeedDown_Click()
{
  float sp = TagReadFloat("Motor_Speed_SP", 1200.0f) - 250.0f;
  if (sp < 0.0f) sp = 0.0f;
  TagWriteFloat("Motor_Speed_SP", sp);
  if (TagReadBool("Motor_Start", false)) TagWriteFloat("Motor_Speed_PV", sp);
  TagWriteString("Script_Status", "Script: speed decreased");
}
`;

const editorHost = ref(null);
const editorReady = ref(false);
const status = scriptStatus;
const consoleText = scriptConsoleText;
const callName = scriptCallName;
const sourceLabel = scriptSourceLabel;
const statusText = computed(() => status.value?.status || status.value?.state || 'unknown');
const compileState = computed(() => status.value?.compile_state || status.value?.state || 'unknown');
const runtimeValid = computed(() => status.value?.runtime_valid !== false);
const statusTone = computed(() => {
  const state = String(compileState.value || '').toLowerCase();
  const text = String(statusText.value || '').toLowerCase();
  if (state === 'error' || text.includes('error') || text.includes('failed')) return 'bad';
  if (state === 'queued' || state === 'compiling') return 'warn';
  if (state === 'ok' || text === 'ok' || runtimeValid.value) return 'ok';
  return 'neutral';
});
const statusSummary = computed(() => {
  const state = compileState.value || 'unknown';
  if (state === 'error') return 'Compile failed';
  if (state === 'queued') return 'Compile queued';
  if (state === 'compiling') return 'Compiling';
  if (state === 'ok') return 'Runtime OK';
  return `Runtime ${runtimeValid.value ? 'valid' : 'not valid'}`;
});
const statusTitle = computed(() => {
  if (statusTone.value === 'bad') return 'Script compile/runtime problem';
  if (statusTone.value === 'warn') return 'Script compile in progress';
  if (statusTone.value === 'ok') return 'AngelScript runtime ready';
  return 'AngelScript status';
});
const statusMessage = computed(() => {
  const msg = statusText.value || 'unknown';
  if (statusTone.value === 'bad') return `${msg}. The firmware keeps the previous valid runtime active when possible, so failed compiles should not crash LCD button events.`;
  if (statusTone.value === 'warn') return msg;
  if (scriptRuntimeDirty.value) return 'Editor has changes that have not been compiled to RAM yet.';
  return msg;
});

let editor = null;
let suppressEditorUpdate = false;
let cleanup = [];
let pollTimer = null;

function getInitialSource() {
  return scriptSource.value || DEFAULT_SCRIPT;
}

function getSource() {
  return (editor?.value ?? editor?.textarea?.value ?? scriptSource.value ?? DEFAULT_SCRIPT) || DEFAULT_SCRIPT;
}

function setEditorSource(text, markEdited = false) {
  const source = String(text ?? '');
  scriptSource.value = source;
  if (markEdited) { scriptDirty.value = true; scriptRuntimeDirty.value = true; }
  if (!editor) return;
  suppressEditorUpdate = true;
  let updated = false;
  try {
    if (typeof editor.setValue === 'function') {
      editor.setValue(source);
      updated = true;
    }
  } catch {}
  try {
    if (!updated && typeof editor.update === 'function') {
      editor.update(source);
      updated = true;
    }
  } catch {}
  try {
    if ('value' in editor) editor.value = source;
  } catch {}
  if (editor.textarea && editor.textarea.value !== source) {
    editor.textarea.value = source;
    editor.textarea.dispatchEvent(new Event('input', { bubbles: true }));
  }
  suppressEditorUpdate = false;
}

const scriptFunctions = computed(() => extractScriptFunctions(scriptSource.value || ''));
const callableFunctions = computed(() => scriptFunctions.value.filter(fn => fn.callable));
const nonCallableFunctionCount = computed(() => scriptFunctions.value.length - callableFunctions.value.length);

function stripScriptForFunctionScan(source) {
  return String(source || '')
    .replace(/\/\*[\s\S]*?\*\//g, '')
    .replace(/(^|[^:])\/\/.*$/gm, '$1')
    .replace(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g, '""');
}

function extractScriptFunctions(source) {
  const text = stripScriptForFunctionScan(source);
  const re = /(?:^|[;{}\n\r])\s*(?:shared\s+)?(?:const\s+)?([A-Za-z_][A-Za-z0-9_]*(?:\s*<[^;{}()]*>)?(?:\s*[&@])?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^()]*)\)\s*(?:const\s*)?\{/g;
  const out = [];
  const seen = new Set();
  const skip = new Set(['if', 'for', 'while', 'switch', 'catch']);
  let m;
  while ((m = re.exec(text)) !== null) {
    const name = m[2];
    if (!name || skip.has(name)) continue;
    const params = String(m[3] || '').trim();
    const key = `${name}(${params})`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push({
      name,
      params,
      callable: params.length === 0 || params === 'void',
    });
  }
  return out.sort((a, b) => a.name.localeCompare(b.name));
}


function escapeRegExp(text) {
  return String(text || '').replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}
function findScriptFunctionPosition(source, functionName) {
  const name = String(functionName || '').trim().replace(/\(\s*\)\s*$/, '');
  if (!name) return -1;
  const re = new RegExp(`(?:^|[;{}\\n\\r])\\s*(?:shared\\s+)?(?:const\\s+)?[A-Za-z_][A-Za-z0-9_]*(?:\\s*<[^;{}()]*>)?(?:\\s*[&@])?\\s+${escapeRegExp(name)}\\s*\\(`, 'm');
  const m = re.exec(source || '');
  if (!m) return -1;
  return m.index + m[0].lastIndexOf(name);
}
function makeHandlerStub(req) {
  const fn = String(req?.functionName || '').trim().replace(/\(\s*\)\s*$/, '');
  const eventLabel = req?.eventName || 'event';
  const widgetLabel = req?.widgetLabel || req?.widgetId || 'widget';
  return `\n\nvoid ${fn}()\n{\n  // ${eventLabel} handler for ${widgetLabel}\n}\n`;
}
function getEditorScroller() {
  return editor?.scrollContainer
    || editorHost.value?.shadowRoot?.querySelector?.('.prism-code-editor')
    || editorHost.value?.querySelector?.('.prism-code-editor')
    || editorHost.value?.querySelector?.('.pce-wrapper')
    || editor?.textarea
    || null;
}

function getEditorScrollCandidates() {
  const shadow = editorHost.value?.shadowRoot || null;
  return [
    editor?.scrollContainer,
    shadow?.querySelector?.('.prism-code-editor'),
    editorHost.value?.querySelector?.('.prism-code-editor'),
    editorHost.value,
  ].filter((el, index, arr) => el && arr.indexOf(el) === index);
}

function getBestEditorScroller() {
  const candidates = getEditorScrollCandidates();
  return candidates.find(el =>
    (el.scrollHeight || 0) > (el.clientHeight || 0) + 2 ||
    (el.scrollWidth || 0) > (el.clientWidth || 0) + 2
  ) || getEditorScroller();
}

function wheelBelongsToEditor(e) {
  const host = editorHost.value;
  if (!host) return false;
  const path = typeof e.composedPath === 'function' ? e.composedPath() : [];
  if (path.includes(host) || path.includes(editor?.scrollContainer) || path.includes(editor?.textarea) || path.includes(editor?.wrapper)) return true;
  if (host.contains?.(e.target)) return true;
  try { if (host.matches?.(':hover')) return true; } catch {}
  try { if (editor?.scrollContainer?.matches?.(':hover')) return true; } catch {}
  return false;
}

function normalizeWheelDelta(e, scroller) {
  const scale = e.deltaMode === WheelEvent.DOM_DELTA_LINE
    ? editorLineHeight()
    : e.deltaMode === WheelEvent.DOM_DELTA_PAGE
      ? Math.max(1, scroller?.clientHeight || 1)
      : 1;
  return {
    dx: Number(e.deltaX || 0) * scale,
    dy: Number(e.deltaY || 0) * scale,
  };
}

function editorLineHeight() {
  const scroller = getEditorScroller();
  const lineEl = editor?.activeLine || scroller?.querySelector?.('.pce-line');
  const target = lineEl || editor?.textarea || scroller || editorHost.value;
  const cssLineHeight = target ? getComputedStyle(target).lineHeight : '';
  const parsed = parseFloat(cssLineHeight);
  if (Number.isFinite(parsed) && parsed > 4) return parsed;
  const fontSize = target ? parseFloat(getComputedStyle(target).fontSize) : 13;
  return (Number.isFinite(fontSize) && fontSize > 0 ? fontSize : 13) * 1.4;
}

function scrollEditorToPosition(pos, preferTopThird = true) {
  const scroller = getEditorScroller();
  if (!scroller) return;
  const source = String(getSource());
  const safePos = Math.max(0, Math.min(Number(pos) || 0, source.length));
  const line = source.slice(0, safePos).split(/\r\n|\r|\n/).length;
  const lineHeight = editorLineHeight();
  const visibleLines = scroller.clientHeight ? Math.max(4, Math.floor(scroller.clientHeight / lineHeight)) : 18;
  const offsetLines = preferTopThird ? Math.floor(visibleLines / 3) : Math.floor(visibleLines / 2);
  const targetTop = Math.max(0, (line - offsetLines) * lineHeight);
  scroller.scrollTop = targetTop;
  if (editor?.textarea && editor.textarea !== scroller) editor.textarea.scrollTop = targetTop;
}

function focusEditorAt(pos) {
  const source = String(getSource());
  const safePos = Math.max(0, Math.min(Number(pos) || 0, source.length));
  const ta = editor?.textarea || editorHost.value?.querySelector?.('textarea');

  const applyFocusAndSelection = () => {
    try { editor?.focus?.(); } catch {}
    try { ta?.focus?.({ preventScroll: true }); } catch { try { ta?.focus?.(); } catch {} }
    try {
      if (typeof editor?.setSelection === 'function') editor.setSelection(safePos, safePos);
      else ta?.setSelectionRange?.(safePos, safePos);
    } catch { try { ta?.setSelectionRange?.(safePos, safePos); } catch {} }
  };

  const reveal = () => {
    applyFocusAndSelection();
    // The Prism editor exposes a cursor extension. Use it first because it
    // understands the overlay/textarea relationship, then force a deterministic
    // top-third scroll fallback for large scripts.
    try { editor?.extensions?.cursor?.scrollIntoView?.(); } catch {}
    scrollEditorToPosition(safePos, true);
    try { editor?.activeLine?.scrollIntoView?.({ block: 'nearest', inline: 'nearest' }); } catch {}
    try { editorHost.value?.scrollIntoView?.({ block: 'nearest', inline: 'nearest' }); } catch {}
  };

  // Run the reveal after Vue has made the Script tab visible and after the
  // Prism editor has processed the selection change. Multiple frames make this
  // reliable when jumping from Designer to Script with v-show transitions.
  nextTick(() => {
    reveal();
    requestAnimationFrame(() => {
      reveal();
      setTimeout(reveal, 40);
    });
  });
}

function installEditorWheelForwarding() {
  // v6.29: keep mouse-wheel scrolling simple and deterministic.
  // prism-code-editor-lightweight uses a scrollable .prism-code-editor element
  // with a transparent textarea over the code.  Depending on browser/shadow-DOM
  // retargeting, wheel events can land on the textarea or host instead of the
  // scroll container.  Capture wheel globally, confirm the pointer is over the
  // editor, normalize mouse-wheel line/page deltas, and scroll the real editor
  // container directly.
  const wheel = (e) => {
    if (!wheelBelongsToEditor(e)) return;

    const scroller = getBestEditorScroller();
    if (!scroller) return;

    const maxY = Math.max(0, scroller.scrollHeight - scroller.clientHeight);
    const maxX = Math.max(0, scroller.scrollWidth - scroller.clientWidth);
    if (!maxY && !maxX) return;

    const beforeY = scroller.scrollTop;
    const beforeX = scroller.scrollLeft;
    const delta = normalizeWheelDelta(e, scroller);
    const useHorizontal = e.shiftKey && Math.abs(delta.dy) >= Math.abs(delta.dx);
    const dx = useHorizontal ? delta.dy : delta.dx;
    const dy = useHorizontal ? 0 : delta.dy;

    if (maxY) scroller.scrollTop = Math.max(0, Math.min(maxY, beforeY + dy));
    if (maxX) scroller.scrollLeft = Math.max(0, Math.min(maxX, beforeX + dx));

    const moved = scroller.scrollTop !== beforeY || scroller.scrollLeft !== beforeX;
    if (!moved) return;

    // Keep any mirrored editor surfaces synchronized after wheel scrolling.
    if (editor?.textarea && editor.textarea !== scroller) {
      editor.textarea.scrollTop = scroller.scrollTop;
      editor.textarea.scrollLeft = scroller.scrollLeft;
    }
    if (editor?.scrollContainer && editor.scrollContainer !== scroller) {
      editor.scrollContainer.scrollTop = scroller.scrollTop;
      editor.scrollContainer.scrollLeft = scroller.scrollLeft;
    }

    e.preventDefault();
    e.stopPropagation();
    if (typeof e.stopImmediatePropagation === 'function') e.stopImmediatePropagation();
  };

  const opts = { passive: false, capture: true };
  const targets = [window, document, editorHost.value, editor?.scrollContainer, editor?.textarea, editor?.wrapper]
    .filter((el, index, arr) => el && arr.indexOf(el) === index);
  for (const target of targets) target.addEventListener('wheel', wheel, opts);
  cleanup.push(() => {
    for (const target of targets) target.removeEventListener('wheel', wheel, opts);
  });
}

function jumpToScriptFunction(req) {
  const name = String(req?.functionName || '').trim().replace(/\(\s*\)\s*$/, '');
  if (!name) return;
  let source = getSource();
  let pos = findScriptFunctionPosition(source, name);
  if (pos < 0 && req?.createIfMissing) {
    const needsNl = source.trim().length ? '' : '';
    source = source.replace(/\s*$/, '') + needsNl + makeHandlerStub({ ...req, functionName: name });
    setEditorSource(source, true);
    pos = findScriptFunctionPosition(source, name);
    log(`Created handler stub ${name}()`, 'ok');
  } else if (pos >= 0) {
    log(`Opened handler ${name}()`, 'ok');
  } else {
    log(`Handler ${name}() was not found`, 'warn');
  }
  if (pos >= 0) focusEditorAt(pos);
}
function formatBytes(bytes) {
  const n = Number(bytes || 0);
  if (!n) return '—';
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  return `${(n / (1024 * 1024)).toFixed(2)} MB`;
}
function log(line, tone = '') {
  const prefix = tone ? `[${tone}] ` : '';
  consoleText.value = `${new Date().toLocaleTimeString()}  ${prefix}${line}\n` + consoleText.value;
}
function installAutocompleteStyles() {
  const root = editorHost.value?.shadowRoot;
  if (!root || root.querySelector('#pilab-panel-ac-style')) return;
  const style = document.createElement('style');
  style.id = 'pilab-panel-ac-style';
  style.textContent = `${autocompleteCss}\n${autocompleteIconsCss}\n\n.pce-ac-tooltip{font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace;border:1px solid #334155;background:#020617;color:#dbeafe;box-shadow:0 18px 40px rgba(0,0,0,.45)}\n.pce-ac-row[aria-selected=true]{background:#0ea5e9;color:#020617}\n.pce-ac-details{color:#94a3b8}`;
  root.appendChild(style);
}
function registerPanelCompletions() {
  registerCompletions(['cpp', 'c', 'clike'], {
    sources: [
      (context) => {
        const match = /[A-Za-z_][A-Za-z0-9_]*$/.exec(context.lineBefore || '');
        if (!match && !context.explicit) return null;
        return {
          from: context.pos - (match ? match[0].length : 0),
          options: ANGELSCRIPT_COMPLETIONS,
        };
      },
    ],
  });
}
async function refresh() {
  try {
    status.value = await getScriptStatus();
    return status.value;
  }
  catch (e) {
    log(`status failed: ${e?.message || e}`, 'err');
    return status.value;
  }
}
function sleep(ms) { return new Promise(resolve => setTimeout(resolve, ms)); }
async function waitForCompileResult(timeoutMs = 8000) {
  const started = Date.now();
  let last = status.value || {};
  while (Date.now() - started < timeoutMs) {
    await sleep(350);
    last = await refresh();
    const state = String(last?.compile_state || '').toLowerCase();
    if (state === 'ok' || state === 'error') return last;
  }
  return last;
}
async function load(force = false) {
  try {
    if (scriptLoaded.value && !force) {
      setEditorSource(scriptSource.value || DEFAULT_SCRIPT, false);
      log('Restored script editor state from Vue RAM');
      return;
    }
    let res;
    try {
      res = await getRuntimeScript();
    } catch (runtimeErr) {
      log(`runtime script load failed, trying flash copy: ${runtimeErr?.message || runtimeErr}`, 'warn');
      res = await getScript();
    }
    const incoming = typeof res.script === 'string' && res.script.trim().length ? res.script : DEFAULT_SCRIPT;
    setEditorSource(incoming, false);
    sourceLabel.value = res.source || res.active || (res.ok ? 'lcd runtime' : 'embedded demo');
    scriptDirty.value = false;
    scriptRuntimeDirty.value = false;
    status.value = { status: res.status || 'loaded', active: res.active, source: res.source, compile_state: res.compile_state, runtime_valid: res.runtime_valid };
    scriptLoaded.value = true;
    log(`Loaded script from ${sourceLabel.value}`);
  } catch (e) {
    setEditorSource(DEFAULT_SCRIPT, false);
    scriptLoaded.value = true;
    scriptDirty.value = true;
    scriptRuntimeDirty.value = true;
    sourceLabel.value = 'embedded fallback';
    log(`load failed, using bundled default demo: ${e?.message || e}`, 'warn');
  }
}
async function save() {
  try {
    await saveScript(getSource());
    scriptDirty.value = false;
    sourceLabel.value = 'device flash';
    await refresh();
    log('Saved script to panel flash', 'ok');
    return true;
  }
  catch (e) {
    log(`save failed: ${e?.message || e}`, 'err');
    return false;
  }
}
async function compile() {
  try {
    const reply = await compileScript(getSource());
    status.value = { ...(status.value || {}), ...(reply || {}), status: reply?.status || 'compile queued' };
    scriptRuntimeDirty.value = true;
    log('Compile to RAM queued on panel');
    const finalStatus = await waitForCompileResult();
    const state = String(finalStatus?.compile_state || '').toLowerCase();
    if (state === 'ok') {
      scriptRuntimeDirty.value = false;
      sourceLabel.value = scriptDirty.value ? 'browser RAM compiled, flash dirty' : 'device flash / runtime compiled';
      log('Compile to RAM succeeded', 'ok');
      return true;
    }
    if (state === 'error') {
      scriptRuntimeDirty.value = true;
      log(`compile failed: ${finalStatus?.status || 'AngelScript build failed'}`, 'err');
      return false;
    }
    log(`compile status uncertain after timeout: ${state || 'unknown'}`, 'warn');
    return false;
  }
  catch (e) {
    scriptRuntimeDirty.value = true;
    log(`compile failed to queue: ${e?.message || e}`, 'err');
    await refresh();
    return false;
  }
}
async function saveAndCompile() {
  log('Compile + Save: compiling to RAM first so invalid scripts are not written to flash');
  const ok = await compile();
  if (!ok) {
    log('Compile + Save aborted; script was not saved to flash because compile failed', 'err');
    return;
  }
  await save();
}
async function call(fn) {
  const name = String(fn || '').trim();
  if (!name) {
    log('select a function to call first', 'warn');
    return;
  }
  try { await callScript(name); log(`Queued ${name}()`); setTimeout(refresh, 500); }
  catch (e) { log(`call failed: ${e?.message || e}`, 'err'); }
}
function handleEditorKeydown(e) {
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
    e.preventDefault();
    save();
  } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'b') {
    e.preventDefault();
    compile();
  }
}


watch(callableFunctions, (list) => {
  if (!list.length) {
    callName.value = '';
    return;
  }
  if (!list.some(fn => fn.name === callName.value)) {
    callName.value = list[0].name;
  }
}, { immediate: true });


watch(scriptExternalRevision, () => {
  if (!editor) return;
  setEditorSource(scriptSource.value || '', false);
  log('Imported script loaded into editor from project package', 'ok');
});

watch(scriptJumpRequest, (req) => {
  if (!req) return;
  const run = () => jumpToScriptFunction(req);
  if (!editor) nextTick(run);
  else run();
});

onMounted(() => {
  registerPanelCompletions();
  editor = minimalEditor(
    editorHost.value,
    {
      language: 'cpp',
      theme: 'github-dark',
      value: getInitialSource(),
      tabSize: 2,
      insertSpaces: true,
      lineNumbers: true,
      wordWrap: false,
      onUpdate(value) {
        scriptSource.value = value;
        if (!suppressEditorUpdate) {
          scriptDirty.value = true;
          scriptRuntimeDirty.value = true;
        }
      },
    },
    () => {
      editorReady.value = true;
      installAutocompleteStyles();
      log('PiLab Prism editor ready', 'ok');
    },
  );

  const history = editHistory();
  editor.addExtensions(
    defaultCommands(),
    history,
    indentGuides(),
    matchBrackets(),
    highlightBracketPairs(),
    cursorPosition(),
    autoComplete({ filter: fuzzyFilter, closeOnBlur: true, preferAbove: false }),
  );
  editor.textarea?.addEventListener('keydown', handleEditorKeydown);
  cleanup.push(() => editor?.textarea?.removeEventListener('keydown', handleEditorKeydown));
  installEditorWheelForwarding();
  pollTimer = setInterval(() => refresh(), 2500);
  load(false).then(() => {
    if (scriptJumpRequest.value) jumpToScriptFunction(scriptJumpRequest.value);
    return refresh();
  });
});

onBeforeUnmount(() => {
  if (pollTimer) clearInterval(pollTimer);
  for (const fn of cleanup) fn();
  cleanup = [];
  try { editor?.remove?.(); } catch {}
  editor = null;
});
</script>
