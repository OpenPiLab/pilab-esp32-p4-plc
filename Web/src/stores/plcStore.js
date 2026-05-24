import { computed, ref } from 'vue';
import { getCommandCenter, getPlcData, getTagData, setPlcMode, writePlcTag } from '../api/plcApi';
import { ensureTagStoreLoaded } from './tagStore';

// Lightweight singleton store. No Pinia dependency, no duplicate pollers.
const overview = ref({});
const plc = ref({});
const script = ref({});
const plcData = ref({ points: [] });
const pointsByName = ref({});
const tagNames = ref([]);
const online = ref(false);
const plcDataOnline = ref(false);
const lastError = ref('');
const commandPollMs = ref(1000);
const dataPollMs = ref(100);
const commandLastUpdated = ref(0);
const dataLastUpdated = ref(0);

let commandTimer = null;
let dataTimer = null;
let commandInflight = null;
let dataInflight = null;
let dataUsers = 0;
let started = false;
let fullPlcDataLoaded = false;
let layoutVersion = 0;
let layoutPointNames = [];

const stateName = (s) => typeof s === 'string' ? s : (['IDLE','QUEUED','COMPILING','OK','FAILED','QUEUE_FULL'][s] || s || 'UNKNOWN');

const scriptState = computed(() => stateName(script.value.state ?? overview.value.script_state));
const plcRunning = computed(() => overview.value.plc_running !== undefined ? !!overview.value.plc_running : overview.value.plc_mode === 'RUN');
const flashWritesAllowed = computed(() => overview.value.flash_writes_allowed !== undefined ? !!overview.value.flash_writes_allowed : !plcRunning.value);
const plcMode = computed(() => overview.value.plc_mode || (plcRunning.value ? 'RUN' : 'STOP'));
const activeScriptName = computed(() => overview.value.active_script_name || script.value.active_script_name || plcData.value.active_script_name || '');
const activeScriptPath = computed(() => overview.value.active_script_path || script.value.active_script_path || plcData.value.active_script_path || (activeScriptName.value ? `/scripts/${activeScriptName.value}` : ''));
const pointCount = computed(() => Number(plcData.value.point_count ?? tagNames.value.length ?? overview.value.cache_points ?? 0));

function currentCommandInterval() {
  return document.hidden ? Math.max(3000, commandPollMs.value) : commandPollMs.value;
}
function currentDataInterval() {
  return document.hidden ? Math.max(2000, dataPollMs.value) : dataPollMs.value;
}
function resetCommandTimer() {
  if (commandTimer) clearInterval(commandTimer);
  commandTimer = setInterval(refreshCommandCenter, currentCommandInterval());
}
function resetDataTimer() {
  if (dataTimer) clearInterval(dataTimer);
  dataTimer = null;
  if (dataUsers > 0) dataTimer = setInterval(refreshPlcData, currentDataInterval());
}

async function refreshCommandCenter() {
  if (commandInflight) return commandInflight;
  commandInflight = (async () => {
    try {
      const cc = await getCommandCenter();
      overview.value = cc.overview || {};
      script.value = cc.script || {};
      plc.value = cc.plc || {};
      online.value = true;
      lastError.value = '';
      commandLastUpdated.value = Date.now();
      // command_center carries masks/ticks. Keep a light plcData fallback for pages
      // that only need I/Q masks and tick_count.
      plcData.value = {
        ...plcData.value,
        tick_count: plc.value.tick_count,
        di_mask: plc.value.di_mask,
        do_mask: plc.value.do_mask,
      };
      return cc;
    } catch (e) {
      online.value = false;
      lastError.value = e?.message || String(e);
      throw e;
    } finally {
      commandInflight = null;
    }
  })();
  return commandInflight;
}

function ingestFullPlcData(data) {
  const map = {};
  const names = [];
  const order = [];
  for (const p of data.points || []) {
    if (!p?.name) continue;
    map[p.name] = p;
    names.push(p.name);
    order.push(p.name);
  }
  plcData.value = data;
  pointsByName.value = map;
  tagNames.value = names.sort();
  layoutPointNames = order;
  layoutVersion = Number(data.cache_version ?? data.version ?? data.layout_version ?? layoutVersion ?? 0);
  fullPlcDataLoaded = true;
  return data;
}

function normalizeCompactTagData(data = {}) {
  const points = [];
  const indexedValues = Array.isArray(data.values) ? data.values : (Array.isArray(data.a) ? data.a : null);
  const indexed = !!(data.indexed ?? data.i);
  const pointCount = Number(data.point_count ?? data.pc ?? (indexedValues ? indexedValues.length : 0));

  if (indexed && indexedValues && layoutPointNames.length) {
    if (indexedValues.length !== layoutPointNames.length || pointCount !== layoutPointNames.length) {
      return {
        ...data,
        indexed,
        layout_version: Number(data.layout_version ?? data.layout ?? data.l ?? 0),
        value_version: Number(data.value_version ?? data.v ?? 0),
        point_count: pointCount,
        points: [],
        layout_mismatch: true,
      };
    }
    for (let i = 0; i < indexedValues.length; i++) {
      points.push({
        name: layoutPointNames[i],
        value: indexedValues[i],
      });
    }
  } else {
    const rawPoints = data.points || data.p || data.t || [];
    for (const p of rawPoints) {
      const name = p?.name ?? p?.n;
      if (!name) continue;
      points.push({
        name,
        value: p?.value ?? p?.v,
      });
    }
  }

  return {
    ...data,
    indexed,
    layout_version: Number(data.layout_version ?? data.layout ?? data.l ?? 0),
    value_version: Number(data.value_version ?? data.v ?? 0),
    snapshot_us: data.snapshot_us ?? data.ts,
    tick_count: data.tick_count ?? data.tc,
    script_scan_count: data.script_scan_count ?? data.ssc,
    output_write_count: data.output_write_count ?? data.owc,
    raw_di_mask: data.raw_di_mask ?? data.rim,
    di_mask: data.di_mask ?? data.dim,
    do_mask: data.do_mask ?? data.dom,
    point_count: data.point_count ?? data.pc ?? points.length,
    build_us: data.build_us ?? data.bu,
    points,
  };
}

function ingestCompactTagData(data) {
  const normalized = normalizeCompactTagData(data);
  const existing = pointsByName.value || {};
  const map = { ...existing };
  const namesSet = new Set(tagNames.value || []);

  for (const p of normalized.points || []) {
    const prev = map[p.name] || { name: p.name };
    map[p.name] = { ...prev, value: p.value };
    namesSet.add(p.name);
  }

  pointsByName.value = map;
  tagNames.value = Array.from(namesSet).sort();
  plcData.value = {
    ...plcData.value,
    ...normalized,
    // Preserve full metadata shape while replacing the live point values.
    points: tagNames.value.map((name) => map[name]).filter(Boolean),
  };
  return plcData.value;
}

async function refreshFullPlcData() {
  const data = await getPlcData();
  const previousLayoutVersion = layoutVersion;
  const previousPointCount = layoutPointNames.length;
  const result = ingestFullPlcData(data);
  const layoutChanged = previousLayoutVersion !== layoutVersion || previousPointCount !== layoutPointNames.length;
  if (layoutChanged) {
    // /api/tags includes dynamically created script metadata tags. When one
    // browser tab uploads a script, other tabs discover the layout version
    // change through /api/tag_data and refresh this registry as well.
    ensureTagStoreLoaded({ force: true, preserveEdits: true }).catch(() => {});
  }
  return result;
}

async function refreshPlcData() {
  if (dataInflight) return dataInflight;
  dataInflight = (async () => {
    try {
      let data;
      if (!fullPlcDataLoaded) {
        data = await refreshFullPlcData();
      } else {
        const compact = await getTagData();
        const compactLayout = Number(compact.layout_version ?? compact.layout ?? compact.l ?? 0);
        const compactPointCount = Number(compact.point_count ?? compact.pc ?? 0);
        const indexedValues = Array.isArray(compact.values) ? compact.values : (Array.isArray(compact.a) ? compact.a : null);
        const indexed = !!(compact.indexed ?? compact.i);
        const needsLayoutRefresh =
          (compactLayout && layoutVersion && compactLayout !== layoutVersion) ||
          (indexed && compactPointCount && compactPointCount !== layoutPointNames.length) ||
          (indexed && indexedValues && indexedValues.length !== layoutPointNames.length);

        if (needsLayoutRefresh) {
          data = await refreshFullPlcData();
          return data;
        }

        data = ingestCompactTagData(compact);
        if (data.layout_mismatch) {
          data = await refreshFullPlcData();
        }
      }
      plcDataOnline.value = true;
      dataLastUpdated.value = Date.now();
      return data;
    } catch (e) {
      // Older firmware builds do not have /api/tag_data. Fall back to full plc_data.
      try {
        const data = await refreshFullPlcData();
        plcDataOnline.value = true;
        dataLastUpdated.value = Date.now();
        return data;
      } catch (_) {
        plcDataOnline.value = false;
        throw e;
      }
    } finally {
      dataInflight = null;
    }
  })();
  return dataInflight;
}

function start() {
  if (started) return;
  started = true;
  refreshCommandCenter().catch(() => {});
  resetCommandTimer();
  document.addEventListener('visibilitychange', () => {
    resetCommandTimer();
    resetDataTimer();
    if (!document.hidden) {
      refreshCommandCenter().catch(() => {});
      if (dataUsers > 0) refreshPlcData().catch(() => {});
    }
  });
}

function usePlcData() {
  dataUsers++;
  if (dataUsers === 1) {
    refreshPlcData().catch(() => {});
    resetDataTimer();
  }
  return () => {
    dataUsers = Math.max(0, dataUsers - 1);
    if (dataUsers === 0) resetDataTimer();
  };
}

async function setPlcRun(run) {
  await setPlcMode(run);
  await refreshCommandCenter().catch(() => {});
}

async function plcWrite(tag, value) {
  await writePlcTag(tag, value);
  refreshPlcData().catch(() => {});
}

export function usePlcStore() {
  return {
    overview, plc, script, plcData, pointsByName, tagNames,
    online, plcDataOnline, lastError, commandLastUpdated, dataLastUpdated,
    commandPollMs, dataPollMs,
    scriptState, plcRunning, flashWritesAllowed, plcMode, activeScriptName, activeScriptPath, pointCount,
    start, usePlcData, refreshCommandCenter, refreshPlcData, refreshFullPlcData, setPlcRun, plcWrite,
  };
}
