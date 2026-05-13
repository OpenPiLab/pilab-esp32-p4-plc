<template>
  <main class="app-page settings-page">
    <section class="settings-hero panel rounded-md compact-settings-hero">
      <div class="settings-hero-main">
        <div>
          <div class="settings-kicker">Device Platform</div>
          <h1 class="settings-title">Settings</h1>
        </div>
        <p class="settings-hero-text">
          Runtime settings apply immediately. Persistent saves write JSON under <span class="lcd">/config</span> and are locked while the PLC is in RUN.
        </p>
      </div>
      <div class="settings-hero-status lcd compact-status-box">
        <span :class="plcRunning ? 'text-sky-300' : 'text-amber-300'">{{ plcMode }}</span>
        <span :class="online ? 'text-emerald-300' : 'text-red-300'">{{ online ? 'ONLINE' : 'OFFLINE' }}</span>
        <span :class="flashWritesAllowed ? 'text-emerald-300' : 'text-amber-300'">{{ flashWritesAllowed ? 'WRITES UNLOCKED' : 'WRITES LOCKED' }}</span>
      </div>
    </section>

    <section class="settings-layout mt-4">
      <aside class="settings-sidebar panel rounded-md p-3">
        <button
          v-for="topic in topics"
          :key="topic.key"
          type="button"
          class="settings-topic"
          :class="activeTopic === topic.key ? 'active' : ''"
          @click="selectTopic(topic.key)"
        >
          <span class="settings-topic-icon">{{ topic.icon }}</span>
          <span class="min-w-0">
            <span class="settings-topic-title">{{ topic.title }}</span>
            <span class="settings-topic-subtitle">{{ topic.subtitle }}</span>
          </span>
        </button>
      </aside>

      <section class="settings-panel panel rounded-md overflow-hidden">
        <header class="settings-panel-head compact-panel-head">
          <div>
            <div class="settings-kicker">{{ currentTopic.group }}</div>
            <h2 class="mt-1 text-xl font-black text-slate-100">{{ currentTopic.title }}</h2>
          </div>
          <div class="flex items-center gap-2 flex-wrap justify-end">
            <div class="settings-state-chip" :class="dirty ? 'planned' : 'ready'">{{ dirty ? 'Runtime dirty' : 'Current' }}</div>
            <div class="settings-state-chip" :class="settingsLoadError ? 'planned' : 'ready'">{{ settingsLoadError ? 'API offline' : 'API backed' }}</div>
          </div>
        </header>

        <div class="settings-panel-body">
          <div v-if="settingsLoadError" class="settings-note warn mb-4">
            <span class="led led-warn"></span>
            {{ settingsLoadError }} — showing local defaults until firmware endpoints are available.
          </div>

          <section v-if="currentTopic.liveFields?.length" class="settings-section">
            <h3 class="settings-section-title">Live status</h3>
            <div class="settings-form-grid">
              <div v-for="field in currentTopic.liveFields" :key="field.label" class="settings-field hard-panel rounded-md p-3">
                <div class="settings-field-label">{{ field.label }}</div>
                <div class="settings-field-value lcd" :class="field.tone ? `tone-${field.tone}` : ''">{{ field.value() }}</div>
              </div>
            </div>
          </section>

          <section class="settings-section">
            <h3 class="settings-section-title">Runtime settings</h3>
            <div class="settings-control-grid">
              <label v-for="field in currentTopic.fields" :key="field.key" class="settings-control">
                <span class="settings-field-label">{{ field.label }}</span>

                <select
                  v-if="field.type === 'select'"
                  class="settings-input"
                  :value="form[field.key]"
                  @change="updateField(field.key, normalizeValue(field, $event.target.value))"
                >
                  <option v-for="option in field.options" :key="String(option.value)" :value="option.value">{{ option.label }}</option>
                </select>

                <input
                  v-else-if="field.type === 'number'"
                  class="settings-input"
                  type="number"
                  :min="field.min"
                  :max="field.max"
                  :step="field.step || 1"
                  :value="form[field.key]"
                  @input="updateField(field.key, normalizeValue(field, $event.target.value))"
                />

                <input
                  v-else-if="field.type === 'checkbox'"
                  class="settings-checkbox"
                  type="checkbox"
                  :checked="!!form[field.key]"
                  @change="updateField(field.key, $event.target.checked)"
                />

                <input
                  v-else
                  class="settings-input"
                  type="text"
                  :value="form[field.key] ?? ''"
                  @input="updateField(field.key, $event.target.value)"
                />

                <span v-if="field.help" class="settings-help">{{ field.help }}</span>
              </label>
            </div>
          </section>

          <section class="settings-section">
            <h3 class="settings-section-title">Actions</h3>
            <div class="settings-action-row">
              <button class="settings-action" :disabled="loading || !dirty" @click="applyRuntime">Apply Runtime</button>
              <button class="settings-action" :disabled="loading" @click="reloadSettings">Reload</button>
              <button class="settings-action" :disabled="loading || !dirty || !flashWritesAllowed" @click="savePersistent">Save JSON to /config</button>
            </div>
            <div class="settings-note mt-4" :class="flashWritesAllowed ? 'ok' : 'warn'">
              <span class="led" :class="flashWritesAllowed ? 'led-on' : 'led-warn'"></span>
              <span v-if="flashWritesAllowed">PLC STOP mode active — persistent settings can be saved to LittleFS.</span>
              <span v-else>PLC RUN mode active — persistent settings are locked. Runtime Apply is still allowed.</span>
            </div>
            <div v-if="message" class="settings-note ok mt-3"><span class="led led-on"></span>{{ message }}</div>
          </section>
        </div>
      </section>
    </section>
  </main>
</template>

<script setup>
import { computed, onMounted, onUnmounted, reactive, ref } from 'vue';
import { usePlcStore } from '../stores/plcStore';
import { applyRuntimeSettings, getSettingsPage, saveSettingsPage } from '../api/settingsApi';

const store = usePlcStore();
const overview = store.overview;
const plc = store.plc;
const script = store.script;
const plcData = store.plcData;
const online = store.online;
const plcMode = store.plcMode;
const plcRunning = store.plcRunning;
const flashWritesAllowed = store.flashWritesAllowed;
const scriptState = store.scriptState;
const activeScriptName = store.activeScriptName;
const activeScriptPath = store.activeScriptPath;
const pointsByName = store.pointsByName;

const option = (value, label = null) => ({ value, label: label ?? String(value) });

function formatMs(v) { const n = Number(v); return Number.isFinite(n) ? `${n} ms` : '—'; }
function formatUs(v) { const n = Number(v); return Number.isFinite(n) ? `${n} µs` : '—'; }
function pointValue(name, fallback = '—') {
  const p = pointsByName.value?.[name];
  return p && p.value !== undefined ? p.value : fallback;
}

function formatBytes(v) {
  const n = Number(v);
  if (!Number.isFinite(n)) return '—';
  if (n >= 1024 * 1024) return `${(n / (1024 * 1024)).toFixed(2)} MB`;
  if (n >= 1024) return `${(n / 1024).toFixed(1)} KB`;
  return `${n} B`;
}

const topics = [
  {
    key:'system', icon:'▣', title:'System', subtitle:'Identity and runtime', group:'Runtime',
    defaults:{ deviceName:'PiLab PLC', location:'Bench / Lab', bootBehavior:'stopped', startupScript:'', ntpEnabled:true, timezone:'America/Toronto' },
    fields:[
      { key:'deviceName', label:'Device name', help:'Shown in the browser header and diagnostic snapshots.' },
      { key:'location', label:'Location label', help:'Human-readable installation label.' },
      { key:'bootBehavior', label:'Boot behavior', type:'select', options:[option('stopped','Start controller stopped'), option('resume','Resume last run state'), option('startupScript','Run selected startup script')] },
      { key:'startupScript', label:'Startup script', help:'Path or script name used by future startup policy.' },
      { key:'ntpEnabled', label:'NTP enabled', type:'checkbox' },
      { key:'timezone', label:'Time zone' },
    ],
    liveFields:[
      { label:'Controller mode', value:() => plcMode.value, tone:'sky' },
      { label:'API state', value:() => online.value ? 'Online' : 'Offline', tone:'green' },
      { label:'Active script', value:() => activeScriptName.value || '—' },
      { label:'Script state', value:() => scriptState.value, tone:'sky' },
      { label:'Script generation', value:() => script.value.generation ?? overview.value.script_generation ?? 0 },
      { label:'Tick count', value:() => plc.value.tick_count ?? plcData.value.tick_count ?? overview.value.tick_count ?? 0 },
    ],
  },
  {
    key:'network', icon:'⌁', title:'Network', subtitle:'IP, hostname, time', group:'Connectivity',
    defaults:{ hostname:'pilab-p4', mdnsName:'pilab-p4.local', ipMode:'static', staticIpv4:'192.168.5.210', netmask:'255.255.255.0', gateway:'192.168.5.1', dns:'192.168.5.1', ntpServer:'pool.ntp.org' },
    fields:[
      { key:'hostname', label:'Hostname' },
      { key:'mdnsName', label:'mDNS name' },
      { key:'ipMode', label:'IP mode', type:'select', options:[option('dhcp','DHCP'), option('static','Static IPv4')] },
      { key:'staticIpv4', label:'Static IPv4' },
      { key:'netmask', label:'Netmask' },
      { key:'gateway', label:'Gateway' },
      { key:'dns', label:'DNS server' },
      { key:'ntpServer', label:'NTP server' },
    ],
  },
  {
    key:'logging', icon:'≡', title:'Logging', subtitle:'Events and verbosity', group:'Observability',
    defaults:{ espConsoleLogLevel:'warn', pilabScriptLogLevel:'info', retainedEvents:250, serialConsoleLogs:true, includeCompileLog:true, includeTimingSamples:true, includeTagSnapshot:true, exportFormat:'jsonBundle' },
    fields:[
      { key:'espConsoleLogLevel', label:'ESP-IDF console log level', type:'select', options:[option('off','OFF / NONE'), option('error','ERROR'), option('warn','WARNING'), option('info','INFO'), option('debug','DEBUG'), option('verbose','VERBOSE')], help:'Controls ESP_LOGx console output. OFF is only applied after boot and web server startup so boot errors remain visible.' },
      { key:'pilabScriptLogLevel', label:'PiLab script log level', type:'select', options:[option('none','NONE'), option('error','ERROR'), option('warn','WARNING'), option('info','INFO'), option('debug','DEBUG'), option('verbose','VERBOSE')], help:'Independent PiLab/AngelScript runtime filter. Does not change ESP-IDF console verbosity.' },
      { key:'retainedEvents', label:'Retained events', type:'number', min:0, max:5000 },
      { key:'serialConsoleLogs', label:'Serial console enabled', type:'checkbox', help:'When disabled, ESP-IDF console logging is forced to OFF after web startup.' },
      { key:'includeCompileLog', label:'Include script compile log', type:'checkbox' },
      { key:'includeTimingSamples', label:'Include timing samples', type:'checkbox' },
      { key:'includeTagSnapshot', label:'Include tag snapshot', type:'checkbox' },
      { key:'exportFormat', label:'Export format', type:'select', options:[option('jsonBundle','JSON bundle'), option('textLog','Text log'), option('csvTiming','CSV timing data')] },
    ],
  },
  {
    key:'script-engine', icon:'AS', title:'Script Engine', subtitle:'AngelScript runtime', group:'Runtime',
    defaults:{ compileBeforeRun:true, scriptWatchdog:true, watchdogBudgetUs:5000, startupCompileAction:'stopOnError', registeredAddons:'Arrays, Math, Strings', tagBinding:'auto', scriptUpdateMode:'pauseDuringCompile' },
    fields:[
      { key:'compileBeforeRun', label:'Compile before run', type:'checkbox' },
      { key:'scriptWatchdog', label:'Script watchdog', type:'checkbox' },
      { key:'watchdogBudgetUs', label:'Watchdog budget µs', type:'number', min:500, max:100000, step:100 },
      { key:'startupCompileAction', label:'Startup compile action', type:'select', options:[option('stopOnError','Keep stopped on error'), option('previousGood','Run previous good script')] },
      { key:'registeredAddons', label:'Registered addons' },
      { key:'tagBinding', label:'Global tag binding', type:'select', options:[option('auto','Auto bind known tags'), option('explicit','Explicit bind only')] },
      { key:'scriptUpdateMode', label:'Script update mode', type:'select', options:[option('stopBeforeCompile','Stop PLC before compile'), option('pauseDuringCompile','Pause script scan during compile'), option('onlineHotSwap','Online compile / hot swap')], help:'Online hot swap is an advanced mode for development and edge workloads. It may add jitter during compile.' },
    ],
    liveFields:[
      { label:'Active script name', value:() => activeScriptName.value || '—' },
      { label:'Active script path', value:() => activeScriptPath.value || '—' },
      { label:'Compile state', value:() => scriptState.value, tone:'sky' },
      { label:'Script scans', value:() => script.value.script_scans_completed ?? overview.value.script_scan_count ?? '—' },
      { label:'Last compile', value:() => formatMs(script.value.compile_ms ?? overview.value.compile_ms) },
      { label:'Flash writes', value:() => flashWritesAllowed.value ? 'Allowed' : 'Locked', tone:'amber' },
    ],
  },
  {
    key:'performance', icon:'µs', title:'Performance', subtitle:'Timing and memory', group:'Observability',
    defaults:{ scriptScanBudgetUs:5000, warnAtPercent:75, dashboardPollMs:1000, tagCachePollMs:100, plcDataCacheMs:100, telemetryEnabled:true, overrunPolicy:'faultAfterLimit', scriptTimeMode:'actualClamped', maxDeltaUs:50000, faultAfterConsecutiveOverruns:20, faultAfterCoalescedScans:100 },
    fields:[
      { key:'scriptScanBudgetUs', label:'Script scan budget µs', type:'number', min:500, max:100000, step:100 },
      { key:'warnAtPercent', label:'Warn above % of budget', type:'number', min:1, max:100 },
      { key:'dashboardPollMs', label:'Dashboard poll ms', type:'number', min:100, max:10000, step:100 },
      { key:'tagCachePollMs', label:'Tag cache poll ms', type:'number', min:20, max:5000, step:10 },
      { key:'plcDataCacheMs', label:'PLC data cache ms', type:'number', min:20, max:5000, step:10 },
      { key:'telemetryEnabled', label:'Telemetry enabled', type:'checkbox' },
      { key:'overrunPolicy', label:'Script overrun policy', type:'select', options:[option('coalesceContinue','Coalesce and continue'), option('warnContinue','Warn and continue'), option('faultAfterLimit','Fault after limit'), option('stopImmediately','Stop immediately'), option('edgeTask','Edge task / coalesce indefinitely')], help:'Controls what happens when the script scan exceeds its budget or missed scan ticks are coalesced.' },
      { key:'scriptTimeMode', label:'Script time mode', type:'select', options:[option('nominal','Nominal 5 ms'), option('actual','Actual elapsed time'), option('actualClamped','Actual elapsed time, clamped')], help:'Controls PLC_DeltaTimeUs / PLC_DeltaTimeMs passed to scripts.' },
      { key:'maxDeltaUs', label:'Max script delta µs', type:'number', min:500, max:1000000, step:500, help:'Clamp for elapsed-time mode so long pauses do not create one huge timer jump.' },
      { key:'faultAfterConsecutiveOverruns', label:'Fault after consecutive overruns', type:'number', min:1, max:100000 },
      { key:'faultAfterCoalescedScans', label:'Fault after coalesced scans/window', type:'number', min:0, max:100000, help:'0 disables this threshold.' },
    ],
    liveFields:[
      { label:'PLC work average', value:() => formatUs(overview.value.plc_work_avg_us), tone:'sky' },
      { label:'Cache build', value:() => formatUs(overview.value.cache_build_us), tone:'green' },
      { label:'Script average', value:() => formatUs(script.value.script_avg_us ?? overview.value.script_avg_us), tone:'sky' },
      { label:'Script 1s max', value:() => formatUs(script.value.script_max_us ?? overview.value.script_max_us), tone:'amber' },
      { label:'Coalesced scans', value:() => pointValue('PLC_ScanCoalescedCount'), tone:'amber' },
      { label:'Scan load', value:() => { const v = pointValue('PLC_ScanLoadPercent', null); return v === null ? '—' : `${Number(v).toFixed(1)} %`; }, tone:'amber' },
      { label:'Actual script period', value:() => formatUs(pointValue('PLC_ScanActualPeriodUs', NaN)), tone:'sky' },
      { label:'Scan fault', value:() => pointValue('PLC_ScanFaultActive'), tone:'amber' },
      { label:'Internal heap free', value:() => formatBytes(overview.value.heap_internal_free) },
      { label:'PSRAM free', value:() => formatBytes(overview.value.heap_psram_free) },
    ],
  },
  {
    key:'storage', icon:'▤', title:'Storage', subtitle:'LittleFS and backups', group:'Device',
    defaults:{ writesWhileRunning:'blocked', backupPath:'/backups', userFilesRoot:'/user', maxUploadKb:512, configSaveMode:'atomic', keepBackups:5 },
    fields:[
      { key:'writesWhileRunning', label:'Writes while running', type:'select', options:[option('blocked','Blocked'), option('smallConfig','Allow small config only')] },
      { key:'backupPath', label:'Backup path' },
      { key:'userFilesRoot', label:'User files root' },
      { key:'maxUploadKb', label:'Max upload KB', type:'number', min:1, max:8192 },
      { key:'configSaveMode', label:'Config save mode', type:'select', options:[option('atomic','Atomic temp + rename'), option('direct','Direct write')] },
      { key:'keepBackups', label:'Backups to retain', type:'number', min:0, max:100 },
    ],
  },
  {
    key:'security', icon:'⛨', title:'Security', subtitle:'Access and auth', group:'Access',
    defaults:{ authentication:'disabled', writeConfirmation:true, sessionTimeoutMin:30, apiTokenConfigured:false, corsPolicy:'sameOrigin', readonlyGuestMode:false },
    fields:[
      { key:'authentication', label:'Authentication', type:'select', options:[option('disabled','Disabled for lab build'), option('localPassword','Local password'), option('token','Token required')] },
      { key:'writeConfirmation', label:'Write confirmation', type:'checkbox' },
      { key:'sessionTimeoutMin', label:'Session timeout minutes', type:'number', min:1, max:1440 },
      { key:'apiTokenConfigured', label:'API token configured', type:'checkbox' },
      { key:'corsPolicy', label:'CORS policy', type:'select', options:[option('sameOrigin','Same-origin'), option('lan','LAN allowed'), option('open','Open / development')] },
      { key:'readonlyGuestMode', label:'Readonly guest mode', type:'checkbox' },
    ],
  },
  {
    key:'updates', icon:'↻', title:'Updates', subtitle:'Firmware and web app', group:'Maintenance',
    defaults:{ firmwareVersion:'Development build', webAppVersion:'Development build', updateMethod:'manual', rollbackSlot:'notConfigured', preUpdateAction:'stopPlc', backupBeforeUpdate:true },
    fields:[
      { key:'firmwareVersion', label:'Firmware version' },
      { key:'webAppVersion', label:'Web app version' },
      { key:'updateMethod', label:'Update method', type:'select', options:[option('manual','Manual flash / LittleFS upload'), option('ota','OTA package')] },
      { key:'rollbackSlot', label:'Rollback slot' },
      { key:'preUpdateAction', label:'Pre-update action', type:'select', options:[option('stopPlc','Stop PLC before update'), option('warn','Warn only')] },
      { key:'backupBeforeUpdate', label:'Backup before update', type:'checkbox' },
    ],
  },
  {
    key:'diagnostics', icon:'◎', title:'Diagnostics', subtitle:'Health and snapshots', group:'Maintenance',
    defaults:{ includeRuntimeStatus:true, includeTaskStats:true, includeHeapInfo:true, includeFileManifest:true, includeScriptSource:'optional', snapshotFormat:'jsonText' },
    fields:[
      { key:'includeRuntimeStatus', label:'Include runtime status', type:'checkbox' },
      { key:'includeTaskStats', label:'Include task stats', type:'checkbox' },
      { key:'includeHeapInfo', label:'Include heap info', type:'checkbox' },
      { key:'includeFileManifest', label:'Include file manifest', type:'checkbox' },
      { key:'includeScriptSource', label:'Include script source', type:'select', options:[option('no','No'), option('optional','Optional'), option('yes','Yes')] },
      { key:'snapshotFormat', label:'Snapshot format', type:'select', options:[option('jsonText','JSON + text summary'), option('json','JSON only'), option('zip','Future ZIP bundle')] },
    ],
  },
];

const activeTopic = ref('system');
const currentTopic = computed(() => topics.find((t) => t.key === activeTopic.value) || topics[0]);
const form = reactive({});
const dirty = ref(false);
const loading = ref(false);
const message = ref('');
const settingsLoadError = ref('');

function applyDefaults() {
  Object.keys(form).forEach((key) => delete form[key]);
  Object.assign(form, currentTopic.value.defaults);
  dirty.value = false;
}

function normalizeValue(field, value) {
  if (field.type === 'number') {
    const n = Number(value);
    return Number.isFinite(n) ? n : 0;
  }
  if (field.type === 'checkbox') return !!value;
  return value;
}

function updateField(key, value) {
  form[key] = value;
  dirty.value = true;
  message.value = '';
}

async function reloadSettings() {
  loading.value = true;
  message.value = '';
  settingsLoadError.value = '';
  try {
    const data = await getSettingsPage(activeTopic.value);
    Object.keys(form).forEach((key) => delete form[key]);
    Object.assign(form, currentTopic.value.defaults, data.settings || data);
    dirty.value = !!data.dirty;
  } catch (e) {
    settingsLoadError.value = e?.message || String(e);
    applyDefaults();
  } finally {
    loading.value = false;
  }
}

async function applyRuntime() {
  loading.value = true;
  message.value = '';
  try {
    const response = await applyRuntimeSettings(activeTopic.value, { ...form });
    dirty.value = !!response.dirty;
    message.value = 'Runtime settings applied. Not saved to flash.';
  } catch (e) {
    settingsLoadError.value = e?.message || String(e);
  } finally {
    loading.value = false;
  }
}

async function savePersistent() {
  loading.value = true;
  message.value = '';
  try {
    await applyRuntimeSettings(activeTopic.value, { ...form });
    const response = await saveSettingsPage(activeTopic.value);
    dirty.value = !!response.dirty;
    message.value = `Saved ${activeTopic.value}.json to /config.`;
  } catch (e) {
    settingsLoadError.value = e?.message || String(e);
  } finally {
    loading.value = false;
  }
}

function selectTopic(key) {
  activeTopic.value = key;
  reloadSettings();
}

let stopPlcData = null;
onMounted(() => {
  stopPlcData = store.usePlcData();
  reloadSettings();
});
onUnmounted(() => {
  if (stopPlcData) stopPlcData();
});
</script>
