<template>
  <section class="script-page app-page">
    <div class="script-toolbar panel rounded-md">
      <div class="script-toolbar-left">
        <div>
          <div class="script-title">Script Console</div>
          <div class="script-subtitle">AngelScript editor / upload target /api/upload_script</div>
        </div>
        <div class="script-status-chip" :class="statusToneClass">
          <span class="hs-dot"></span>
          <span>{{ displayState }}</span>
        </div>
        <div class="script-meta hide-md">GEN {{ generationText }}</div>
        <div class="script-meta hide-sm">COMPILE {{ compileText }}</div>
      </div>

      <div class="script-toolbar-right">
        <label class="script-name-label hide-sm">
          <span>Name</span>
          <input v-model="scriptName" class="script-name-input" spellcheck="false" />
        </label>
        <button class="script-action muted" @click="pollStatusOnce(true)">Status</button>
        <button class="script-action primary" :disabled="uploading" @click="uploadScript">
          {{ uploading ? 'Uploading…' : 'Upload' }}
        </button>
        <button class="script-action muted" @click="clearDiagnostics">Clear</button>
      </div>
    </div>

    <div class="script-workspace">
      <section class="script-editor-card panel rounded-md">
        <div ref="editorHost" class="script-editor-host"></div>
      </section>

      <aside class="script-side">
        <section class="script-panel panel rounded-md script-library-panel" :class="{ collapsed: !panelOpen('library') }">
          <div class="script-panel-head script-panel-toggle" @click="togglePanel('library')">
            <div>Saved Scripts</div>
            <div class="script-panel-head-actions" @click.stop>
              <div v-show="panelOpen('library')" class="script-library-actions">
                <button class="script-clear-log" :disabled="scriptsLoading" @click="refreshScriptLibrary">Refresh</button>
                <button class="script-clear-log" :disabled="!selectedScriptPath || scriptDownloadBusy" @click="downloadSelectedScript">
                  {{ scriptDownloadBusy ? 'Loading' : 'Download' }}
                </button>
              </div>
              <span class="script-collapse-mark" @click.stop="togglePanel('library')">{{ panelOpen('library') ? '−' : '+' }}</span>
            </div>
          </div>
          <div v-show="panelOpen('library')" class="script-library-body">
            <button
              v-for="file in scriptFiles"
              :key="file.path"
              class="script-file-item"
              :class="{ selected: selectedScriptPath === file.path }"
              @click="selectedScriptPath = file.path"
              @dblclick="downloadScript(file)"
            >
              <div class="script-file-name">{{ file.name }}</div>
              <div class="script-file-meta">{{ formatBytes(file.size || 0) }}</div>
            </button>
            <div v-if="scriptsLoading" class="script-empty">Loading scripts…</div>
            <div v-else-if="scriptFiles.length === 0" class="script-empty">No scripts saved in /scripts yet.</div>
          </div>
        </section>

        <section class="script-panel panel rounded-md" :class="{ collapsed: !panelOpen('diagnostics') }">
          <div class="script-panel-head script-panel-toggle" @click="togglePanel('diagnostics')">
            <div>Diagnostics</div>
            <div class="script-panel-head-actions">
              <div class="script-count" :class="diagnostics.errors.length ? 'bad-text' : 'ok-text'">
                {{ diagnostics.errors.length }} ERR / {{ diagnostics.warnings.length }} WARN
              </div>
              <span class="script-collapse-mark">{{ panelOpen('diagnostics') ? '−' : '+' }}</span>
            </div>
          </div>
          <div v-show="panelOpen('diagnostics')" class="script-diagnostics">
            <button
              v-for="item in diagnosticItems"
              :key="item.kind + ':' + item.line + ':' + item.col + ':' + item.msg"
              class="script-diag-item"
              :class="item.kind === 'ERR' ? 'err' : 'warn'"
              @click="jumpToLine(item.line)"
            >
              <div class="script-diag-loc">{{ item.kind }} L{{ item.line }}:{{ item.col }}</div>
              <div class="script-diag-msg">{{ item.msg }}</div>
            </button>
            <div v-if="diagnosticItems.length === 0" class="script-empty">No diagnostics.</div>
          </div>
        </section>

        <section class="script-panel panel rounded-md" :class="{ collapsed: !panelOpen('console') }">
          <div class="script-panel-head script-panel-toggle" @click="togglePanel('console')">
            <div>Console</div>
            <div class="script-panel-head-actions" @click.stop>
              <button v-show="panelOpen('console')" class="script-clear-log" @click="events = []">Clear</button>
              <span class="script-collapse-mark" @click.stop="togglePanel('console')">{{ panelOpen('console') ? '−' : '+' }}</span>
            </div>
          </div>
          <div v-show="panelOpen('console')" class="script-log lcd">
            <div v-for="e in events" :key="e.id" class="script-log-line" :class="e.tone">
              <span>[{{ e.time }}]</span> {{ e.msg }}
            </div>
            <div v-if="events.length === 0" class="script-empty">No console messages.</div>
          </div>
        </section>

        <section class="script-panel panel rounded-md hide-lg" :class="{ collapsed: !panelOpen('shortcuts') }">
          <div class="script-panel-head script-panel-toggle" @click="togglePanel('shortcuts')">
            <div>Shortcuts</div>
            <span class="script-collapse-mark">{{ panelOpen('shortcuts') ? '−' : '+' }}</span>
          </div>
          <div v-show="panelOpen('shortcuts')" class="script-help">
            <div><b>Ctrl+S</b> upload script</div>
            <div><b>Ctrl+B</b> upload script</div>
            <div><b>Ctrl+Enter</b> refresh status</div>
            <div><b>Ctrl+F</b> editor search</div>
          </div>
        </section>
      </aside>
    </div>
  </section>
</template>

<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from 'vue';
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
import { getScriptStatus, loadSavedScript, listSavedScripts, uploadScriptText } from '../api/scriptApi';
import { usePlcStore } from '../stores/plcStore';
import { ensureTagStoreLoaded, getTagStoreNames, getTagStorePointMap } from '../stores/tagStore';
import { ANGELSCRIPT_COMPLETIONS, makeTagCompletions, mergeCompletions } from '../editor/angelscriptCompletions';

const SCRIPT_SOURCE_KEY = 'pilab_script_source_v2';
const SCRIPT_NAME_KEY = 'pilab_script_name';

const DEFAULT_SCRIPT = `// PiLab default demo program
// Upload this script, set PLC mode to RUN, then open the HMI page.
//
// HMI_I0..HMI_I3 are writable HMI demo switches from the Tag Registry.
// Q0..Q3 follow those switches.
// Q4..Q7 run a four-step chase pattern.
//
// Note: this firmware exposes physical outputs Q0..Q7 only, so the demo
// chase uses Q4..Q7 instead of Q5..Q8.

uint scanCounter = 0;

void scan()
{
  scanCounter++;

  // HMI switch demo: browser toggles -> user tags -> physical outputs.
  Q0 = HMI_I0;
  Q1 = HMI_I1;
  Q2 = HMI_I2;
  Q3 = HMI_I3;

  // Four-output light chase. Adjust CHASE_DIV for speed.
  const uint CHASE_DIV = 100;
  uint step = (scanCounter / CHASE_DIV) % 4;

  Q4 = (step == 0);
  Q5 = (step == 1);
  Q6 = (step == 2);
  Q7 = (step == 3);
}
`;

const editorHost = ref(null);
const editorReady = ref(false);
const uploading = ref(false);
const scriptsLoading = ref(false);
const scriptDownloadBusy = ref(false);
const scriptFiles = ref([]);
const selectedScriptPath = ref('');
const scriptName = ref(localStorage.getItem(SCRIPT_NAME_KEY) || 'main.as');
const status = ref({});
const events = ref([]);
const diagnostics = ref({ errors: [], warnings: [] });
const collapsedPanels = ref({ library: false, diagnostics: false, console: false, shortcuts: false });
const savedSource = localStorage.getItem(SCRIPT_SOURCE_KEY) || DEFAULT_SCRIPT;
const store = usePlcStore();

const STATIC_COMPLETIONS = ANGELSCRIPT_COMPLETIONS;

function installAutocompleteStyles() {
  const root = editorHost.value?.shadowRoot;
  if (!root || root.querySelector('#pilab-autocomplete-style')) return;
  const style = document.createElement('style');
  style.id = 'pilab-autocomplete-style';
  style.textContent = `${autocompleteCss}\n${autocompleteIconsCss}\n\n.pce-ac-tooltip{font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace;border:1px solid #334155;background:#020617;color:#dbeafe;box-shadow:0 18px 40px rgba(0,0,0,.45)}\n.pce-ac-row[aria-selected=true]{background:#0ea5e9;color:#020617}\n.pce-ac-details{color:#94a3b8}`;
  root.appendChild(style);
}

function buildCompletions() {
  const tagNames = new Set([...(getTagStoreNames({ includeSystem: true }) || []), ...(store.tagNames.value || [])]);
  const pointMap = { ...getTagStorePointMap({ includeSystem: true }), ...(store.pointsByName.value || {}) };
  const dynamicTags = makeTagCompletions([...tagNames].sort(), pointMap);
  // Runtime PLC tags override cached registry metadata when both exist.
  return mergeCompletions(dynamicTags, STATIC_COMPLETIONS);
}

function registerPiLabCompletions() {
  registerCompletions(['cpp', 'c', 'clike'], {
    sources: [
      (context) => {
        const match = /[A-Za-z_][A-Za-z0-9_]*$/.exec(context.lineBefore || '');
        if (!match && !context.explicit) return null;
        return {
          from: context.pos - (match ? match[0].length : 0),
          options: buildCompletions(),
        };
      },
    ],
  });
}

let editor = null;
let statusTimer = null;
let eventId = 0;
let editorCleanup = [];

const displayState = computed(() => {
  const s = status.value || {};
  if (s.compile_busy) return 'COMPILING';
  if (s.pending) return 'PENDING';
  return String(s.state || store.scriptState.value || 'IDLE').toUpperCase();
});
const generationText = computed(() => status.value?.generation ?? store.script.value?.generation ?? store.overview.value?.script_generation ?? '—');
const compileText = computed(() => {
  const us = Number(status.value?.last_compile_us ?? store.script.value?.last_compile_us ?? 0);
  if (!us) return '—';
  return us >= 1000 ? `${(us / 1000).toFixed(1)}ms` : `${Math.round(us)}µs`;
});
const statusToneClass = computed(() => {
  const state = displayState.value;
  if (state === 'OK' || state === 'RUNNING' || state === 'RUN') return 'ok';
  if (state === 'FAILED' || state === 'ERROR' || state === 'QUEUE_FULL') return 'bad';
  if (state === 'COMPILING' || state === 'PENDING') return 'warn';
  return '';
});
const diagnosticItems = computed(() => [
  ...(diagnostics.value.errors || []).map(x => ({ ...x, kind: 'ERR' })),
  ...(diagnostics.value.warnings || []).map(x => ({ ...x, kind: 'WARN' })),
]);

function nowText() {
  return new Date().toLocaleTimeString();
}
function addLog(msg, tone = '') {
  events.value.unshift({ id: ++eventId, time: nowText(), msg, tone });
  events.value = events.value.slice(0, 80);
}
function panelOpen(name) {
  return !collapsedPanels.value[name];
}
function togglePanel(name) {
  collapsedPanels.value[name] = !collapsedPanels.value[name];
}
function getSource() {
  return editor?.value ?? editor?.textarea?.value ?? savedSource;
}
function persistSource() {
  if (!editor) return;
  localStorage.setItem(SCRIPT_SOURCE_KEY, getSource());
}
function basename(path) {
  const text = String(path || '');
  const idx = text.lastIndexOf('/');
  return idx >= 0 ? text.slice(idx + 1) : text;
}
function formatBytes(bytes) {
  const n = Number(bytes || 0);
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  return `${(n / (1024 * 1024)).toFixed(1)} MB`;
}
function setEditorSource(source) {
  const text = String(source ?? '');
  let updated = false;
  try {
    if (editor && typeof editor.setValue === 'function') {
      editor.setValue(text);
      updated = true;
    }
  } catch {}
  try {
    if (!updated && editor && typeof editor.update === 'function') {
      editor.update(text);
      updated = true;
    }
  } catch {}
  try {
    if (editor && 'value' in editor) editor.value = text;
  } catch {}
  if (editor?.textarea) {
    editor.textarea.value = text;
    editor.textarea.dispatchEvent(new Event('input', { bubbles: true }));
    editor.textarea.focus();
  }
  localStorage.setItem(SCRIPT_SOURCE_KEY, text);
  nextTick(renderDiagnosticLines);
}
function handleExternalScriptEditorUpdate(event) {
  const detail = event?.detail || {};
  const source = String(detail.source ?? localStorage.getItem(SCRIPT_SOURCE_KEY) ?? '');
  const name = String(detail.name || localStorage.getItem(SCRIPT_NAME_KEY) || 'main.piAS');
  if (name) {
    scriptName.value = name;
    localStorage.setItem(SCRIPT_NAME_KEY, name);
  }
  setEditorSource(source);
  clearDiagnostics();
  addLog(`Loaded generated ladder script as ${name}`, 'ok');
}
async function refreshScriptLibrary() {
  scriptsLoading.value = true;
  try {
    const data = await listSavedScripts();
    const files = Array.isArray(data?.entries) ? data.entries : [];
    scriptFiles.value = files
      .filter(f => !f.dir)
      .map(f => ({ ...f, name: f.name || basename(f.path), path: f.path || `/scripts/${f.name || ''}` }))
      .sort((a, b) => String(a.name).localeCompare(String(b.name)));

    if (!scriptFiles.value.some(f => f.path === selectedScriptPath.value)) {
      selectedScriptPath.value = scriptFiles.value[0]?.path || '';
    }
  } catch (e) {
    scriptFiles.value = [];
    selectedScriptPath.value = '';
    addLog(`script list failed: ${e?.message || e}`, 'err');
  } finally {
    scriptsLoading.value = false;
  }
}
async function downloadScript(file) {
  const target = file?.path || selectedScriptPath.value;
  if (!target) return;
  scriptDownloadBusy.value = true;
  try {
    const source = await loadSavedScript(target);
    const name = file?.name || basename(target) || 'main.as';
    scriptName.value = name;
    localStorage.setItem(SCRIPT_NAME_KEY, name);
    setEditorSource(source);
    clearDiagnostics();
    addLog(`Loaded ${name} from ${target}`, 'ok');
  } catch (e) {
    addLog(`script download failed: ${e?.message || e}`, 'err');
  } finally {
    scriptDownloadBusy.value = false;
  }
}
function downloadSelectedScript() {
  const file = scriptFiles.value.find(f => f.path === selectedScriptPath.value);
  return downloadScript(file || { path: selectedScriptPath.value, name: basename(selectedScriptPath.value) });
}
function statusIsDiagnosticFailure(state) {
  return ['FAILED', 'ERROR', 'QUEUE_FULL'].includes(String(state || '').toUpperCase());
}

function parseAngelScriptDiagnostics(text) {
  const errors = [];
  const warnings = [];
  // The ESP32 compile result can return multiple AngelScript diagnostics on
  // one physical line, for example:
  //   uploaded_script (11,1): ERR: Expected identifier uploaded_script (11,1): ERR: Instead found reserved keyword 'class' Build failed
  // So this parser scans the whole text, not line-by-line. It accepts bare
  // diagnostics plus optional generated/uploaded filenames or paths before
  // the (line,col) location.
  const source = String(text || '').replace(/\r\n/g, '\n');
  const diagRe = /(?:^|\s)(?:(?:[A-Za-z0-9_./\\:-]+)\s+)?\((\d+)\s*,\s*(\d+)\)\s*:\s*(ERR|WARN|INFO)\s*:\s*/gi;
  const matches = [];
  let m;
  while ((m = diagRe.exec(source)) !== null) {
    matches.push({
      index: m.index,
      bodyStart: diagRe.lastIndex,
      line: Math.max(1, parseInt(m[1], 10) || 1),
      col: Math.max(1, parseInt(m[2], 10) || 1),
      level: String(m[3] || '').toUpperCase(),
    });
  }

  for (let i = 0; i < matches.length; i += 1) {
    const cur = matches[i];
    const next = matches[i + 1];
    let msg = source.slice(cur.bodyStart, next ? next.index : source.length)
      .replace(/\s*Build failed\s*$/i, '')
      .trim();
    if (!msg) msg = `${cur.level} at ${cur.line}:${cur.col}`;
    const item = { line: cur.line, col: cur.col, msg };
    if (cur.level === 'ERR') errors.push(item);
    else if (cur.level === 'WARN') warnings.push(item);
  }
  return { errors, warnings };
}

function updateDiagnosticsFromText(text) {
  const parsed = parseAngelScriptDiagnostics(text);
  diagnostics.value = parsed;
  nextTick(renderDiagnosticLines);
  return parsed;
}
function installEditorDiagnosticStyles() {
  const root = editor?.scrollContainer?.getRootNode?.();
  if (!root || !root.querySelector || root.querySelector('#pilab-script-diagnostics-style')) return;
  const style = document.createElement('style');
  style.id = 'pilab-script-diagnostics-style';
  style.textContent = `
    .pilab-diag-line-err{background:rgba(239,68,68,.13)!important;box-shadow:inset 3px 0 0 rgba(239,68,68,.85)}
    .pilab-diag-line-warn{background:rgba(245,158,11,.10)!important;box-shadow:inset 3px 0 0 rgba(245,158,11,.75)}
  `;
  root.appendChild(style);
}
function renderDiagnosticLines() {
  if (!editor?.lines) return;
  installEditorDiagnosticStyles();
  for (const line of Array.from(editor.lines || [])) {
    line.classList?.remove('pilab-diag-line-err', 'pilab-diag-line-warn');
    line.removeAttribute?.('title');
  }
  for (const err of diagnostics.value.errors || []) {
    const line = editor.lines?.[err.line];
    if (line) {
      line.classList.add('pilab-diag-line-err');
      line.title = err.msg || 'Compile error';
    }
  }
  for (const warn of diagnostics.value.warnings || []) {
    const line = editor.lines?.[warn.line];
    if (line && !line.classList.contains('pilab-diag-line-err')) {
      line.classList.add('pilab-diag-line-warn');
      line.title = warn.msg || 'Compile warning';
    }
  }
}
function clearDiagnostics() {
  diagnostics.value = { errors: [], warnings: [] };
  nextTick(renderDiagnosticLines);
  addLog('Diagnostics cleared', 'dim');
}
function jumpToLine(line) {
  if (!editor?.textarea) return;
  const source = getSource();
  const lines = source.split('\n');
  let pos = 0;
  for (let i = 1; i < Math.max(1, line) && i <= lines.length; i++) pos += lines[i - 1].length + 1;
  editor.setSelection?.(pos, pos);
  editor.textarea.focus();
  const el = editor.lines?.[line];
  el?.scrollIntoView?.({ block: 'center' });
}
async function pollStatusOnce(logResult = false) {
  try {
    const s = await getScriptStatus();
    status.value = s || {};
    const state = displayState.value;
    const resultText = String(s?.last_result || '');
    if (resultText && statusIsDiagnosticFailure(state)) {
      updateDiagnosticsFromText(resultText);
    }
    if (logResult) {
      addLog(`state=${state} gen=${generationText.value} compile=${compileText.value}`, 'dim');
      if (resultText) addLog(resultText, statusIsDiagnosticFailure(state) ? 'err' : 'ok');
    }
    return s;
  } catch (e) {
    addLog(`status failed: ${e?.message || e}`, 'err');
    return null;
  }
}
async function waitForCompileResult(timeoutMs = 6500) {
  const start = performance.now();
  let last = '';
  while (performance.now() - start < timeoutMs) {
    await new Promise(resolve => setTimeout(resolve, 250));
    const s = await pollStatusOnce(false);
    const state = displayState.value;
    if (state !== last) {
      addLog(`status ${state}`, state === 'FAILED' ? 'err' : 'dim');
      last = state;
    }
    if (s && !s.compile_busy && !s.pending && ['OK', 'FAILED', 'QUEUE_FULL'].includes(state)) {
      if (state === 'OK') {
        clearDiagnostics();
        addLog(`Compile OK. generation=${generationText.value} compile=${compileText.value}`, 'ok');
      } else {
        updateDiagnosticsFromText(s.last_result || '');
        addLog(s.last_result || 'Compile failed', 'err');
      }
      store.refreshCommandCenter().catch(() => {});
      return;
    }
  }
  addLog('Compile result still pending; background status polling will continue.', 'warn');
}
async function uploadScript() {
  uploading.value = true;
  clearDiagnostics();
  persistSource();
  localStorage.setItem(SCRIPT_NAME_KEY, scriptName.value || 'main.as');
  try {
    addLog(`Uploading ${scriptName.value || 'main.as'}...`, 'dim');
    const result = await uploadScriptText(getSource(), scriptName.value || 'main.as');
    if (!result.ok) {
      updateDiagnosticsFromText(result.text);
      addLog(`Upload rejected HTTP ${result.status}: ${result.text || 'No response text'}`, 'err');
      await pollStatusOnce(false);
      return;
    }
    const msg = result.json?.message || result.text || 'Upload accepted; compile pending';
    const savedPath = result.json?.saved_path;
    addLog(savedPath ? `${msg} Will save ${savedPath} after successful compile.` : msg, 'ok');
    await waitForCompileResult();
    await refreshScriptLibrary();
  } catch (e) {
    addLog(`upload failed: ${e?.message || e}`, 'err');
  } finally {
    uploading.value = false;
  }
}
function handleEditorKeydown(e) {
  if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
    e.preventDefault();
    uploadScript();
  } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'b') {
    e.preventDefault();
    uploadScript();
  } else if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
    e.preventDefault();
    pollStatusOnce(true);
  }
}

onMounted(() => {
  // Script autocomplete needs the live PLC tag list. Subscribe to the shared
  // PLC data stream while this page is mounted so user-created tags appear
  // beside I/Q/AI/AO completions without adding a second independent poller.
  ensureTagStoreLoaded().catch(() => {});
  const releasePlcData = store.usePlcData();
  editorCleanup.push(releasePlcData);
  store.refreshPlcData().catch(() => {});

  window.addEventListener('pilab:set-script-editor', handleExternalScriptEditorUpdate);
  editorCleanup.push(() => window.removeEventListener('pilab:set-script-editor', handleExternalScriptEditorUpdate));

  registerPiLabCompletions();
  editor = minimalEditor(
    editorHost.value,
    {
      language: 'cpp',
      theme: 'github-dark',
      value: savedSource,
      tabSize: 2,
      insertSpaces: true,
      lineNumbers: true,
      wordWrap: false,
      onUpdate(value) {
        localStorage.setItem(SCRIPT_SOURCE_KEY, value);
        renderDiagnosticLines();
      },
    },
    () => {
      editorReady.value = true;
      installAutocompleteStyles();
      installEditorDiagnosticStyles();
      renderDiagnosticLines();
      addLog('Prism editor ready', 'ok');
    },
  );

  // Use minimalEditor and install the exact extensions we need in a stable order.
  // basicEditor loads a second cursor extension asynchronously; that can leave the
  // autocomplete tooltip using a stale cursor marker after normal typing.
  const history = editHistory();
  editor.addExtensions(
    defaultCommands(),
    history,
    indentGuides(),
    matchBrackets(),
    highlightBracketPairs(),
    cursorPosition(),
    autoComplete({
      filter: fuzzyFilter,
      closeOnBlur: true,
      preferAbove: false,
    }),
  );
  editor.textarea?.addEventListener('keydown', handleEditorKeydown);
  editorCleanup.push(() => editor?.textarea?.removeEventListener('keydown', handleEditorKeydown));
  statusTimer = setInterval(() => pollStatusOnce(false), 2500);
  pollStatusOnce(true);
  refreshScriptLibrary();
});

onBeforeUnmount(() => {
  persistSource();
  if (statusTimer) clearInterval(statusTimer);
  for (const fn of editorCleanup) fn();
  editorCleanup = [];
  editor?.remove?.();
  editor = null;
});
</script>
