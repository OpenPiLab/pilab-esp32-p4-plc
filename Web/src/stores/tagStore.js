import { reactive } from 'vue';
import { loadTags } from '../api/tagApi';

// App-wide in-memory tag registry state.
// This intentionally does not use localStorage: unsaved edits survive route
// changes while the Vue app is alive, but a browser refresh starts clean.
const state = reactive({
  imported: {},
  edits: {},
  system: {},
  rawPayload: null,
  status: 'Not loaded',
  loadedOnce: false,
  loading: false,
  lastLoadedAt: 0,
  lastSavedAt: 0,
  lastError: '',
  dirty: false,
  dirtyReason: '',
  dirtyUpdatedAt: 0,
});

let inflightLoad = null;

const JS_RESERVED = new Set([
  'if','else','return','let','const','var','for','while','do','switch','case','break','continue','function',
  'new','class','this','typeof','void','delete','in','instanceof','true','false','null','undefined','NaN','Infinity',
  'Number','Boolean','Math','bool','int','uint','float','double','string','auto','scan'
]);
const PLC_WEB_RESERVED_RE = /^(scan|Scan|true|false|bool|int|float|void|uint|if|else|for|while|return|const|I\d+|Q\d+|AI\d+|AO\d+)$/;

export function normalizeTagName(tag) {
  const name = String(tag || '').replace(/[^A-Za-z0-9_]/g, '_').replace(/^([0-9])/, '_$1');
  return /^[A-Za-z_][A-Za-z0-9_]*$/.test(name) ? name : '';
}

export function isSystemTagName(name) {
  const n = String(name || '').trim();
  return /^(I\d+|Q\d+|AI\d+|AO\d+)$/.test(n) || /^PLC_/.test(n) || /^__obj_/.test(n);
}

function isReservedOrSystem(name) {
  const n = normalizeTagName(name);
  return !n || JS_RESERVED.has(n) || PLC_WEB_RESERVED_RE.test(n) || isSystemTagName(n);
}

export function normalizeTagType(type) {
  const t = String(type || '').toLowerCase();
  return ['bool', 'int', 'float'].includes(t) ? t : 'bool';
}

export function defaultTagValueForType(type) {
  if (type === 'float') return 0.0;
  if (type === 'int') return 0;
  return false;
}

function minMaxForType(type) {
  if (type === 'bool') return { min: 0, max: 1 };
  return { min: 0, max: 100 };
}

function boolField(value, fallback = false) {
  if (value === undefined || value === null) return !!fallback;
  if (typeof value === 'string') {
    const v = value.trim().toLowerCase();
    return !(v === '' || v === 'false' || v === '0' || v === 'off');
  }
  return !!value;
}

function coerceValue(type, value) {
  type = normalizeTagType(type);
  if (type === 'bool') return boolField(value, false);
  const n = Number(value);
  return Number.isFinite(n) ? n : 0;
}

function normalizeUserRow(raw) {
  const name = normalizeTagName(raw && raw.name);
  if (!name || isReservedOrSystem(name)) return null;
  const type = normalizeTagType(raw && raw.type);
  const mm = minMaxForType(type);
  return {
    name,
    type,
    value: coerceValue(type, raw ? raw.value : defaultTagValueForType(type)),
    units: String((raw && raw.units) ?? ''),
    min: Number.isFinite(Number(raw && raw.min)) ? Number(raw.min) : mm.min,
    max: Number.isFinite(Number(raw && raw.max)) ? Number(raw.max) : mm.max,
    writable: boolField(raw && raw.writable),
    retentive: boolField(raw && raw.retentive),
    hmi_visible: true,
    script_visible: true,
    description: String((raw && raw.description) ?? ''),
  };
}

function normalizeSystemRow(raw, source = 'registry') {
  const name = normalizeTagName(raw && raw.name);
  if (!name || !isSystemTagName(name)) return null;
  const type = normalizeTagType((raw && raw.type) || (/^(I\d+|Q\d+)$/.test(name) ? 'bool' : 'float'));
  const base = normalizeUserRow({
    name: `_${name}`, // bypass user-row system-name rejection, then restore below
    type,
    value: raw && raw.value,
    units: raw && raw.units,
    min: raw && raw.min,
    max: raw && raw.max,
    writable: raw && raw.writable,
    retentive: raw && raw.retentive,
    description: (raw && raw.description) || `PLC-owned ${source} tag ${name}.`,
  }) || { type, value: defaultTagValueForType(type), units: '', min: minMaxForType(type).min, max: minMaxForType(type).max, writable: false, retentive: false, description: `PLC-owned ${source} tag ${name}.` };
  return {
    ...base,
    name,
    system: true,
    __system: true,
    __source: source,
    __used: false,
    __imported: false,
    __edited: false,
  };
}

function payloadRows(payload) {
  const root = payload && typeof payload === 'object' ? payload : null;
  if (Array.isArray(root && root.tags)) return root.tags;
  if (Array.isArray(root)) return root;
  return [];
}

export function ingestTagRegistryPayload(payload, { preserveEdits = true } = {}) {
  const imported = {};
  const system = {};
  for (const raw of payloadRows(payload)) {
    const name = normalizeTagName(raw && raw.name);
    if (!name) continue;
    if (isSystemTagName(name)) {
      const row = normalizeSystemRow(raw, 'registry');
      if (row) system[row.name] = row;
      continue;
    }
    const row = normalizeUserRow(raw);
    if (row) imported[row.name] = row;
  }
  state.imported = imported;
  state.system = system;
  if (!preserveEdits) state.edits = {};
  state.rawPayload = payload || { tags: [] };
  state.loadedOnce = true;
  state.lastLoadedAt = Date.now();
  state.lastError = '';
  if (!preserveEdits) clearTagStoreDirty();
  return Object.keys(imported).length;
}

export async function ensureTagStoreLoaded({ force = false, preserveEdits = true } = {}) {
  if (!force && state.loadedOnce) return state.rawPayload || { tags: [] };
  if (inflightLoad) return inflightLoad;
  state.loading = true;
  state.status = force ? 'Reloading tags from PLC...' : 'Loading tags from PLC...';
  inflightLoad = (async () => {
    try {
      const payload = await loadTags();
      const count = ingestTagRegistryPayload(payload || { tags: [] }, { preserveEdits });
      state.status = `Loaded ${count} user tag${count === 1 ? '' : 's'} from PLC`;
      return payload || { tags: [] };
    } catch (e) {
      state.lastError = e?.message || String(e);
      state.status = `Load failed: ${state.lastError}`;
      throw e;
    } finally {
      state.loading = false;
      inflightLoad = null;
    }
  })();
  return inflightLoad;
}


function cloneRow(row) {
  return row ? JSON.parse(JSON.stringify(row)) : row;
}

export function getTagStoreUserRows() {
  const names = new Set([
    ...Object.keys(state.imported || {}),
    ...Object.keys(state.edits || {}),
  ]);
  return [...names]
    .map((name) => {
      const clean = normalizeTagName(name);
      if (!clean || isReservedOrSystem(clean)) return null;
      const base = state.imported?.[clean] || { name: clean };
      const patch = state.edits?.[clean] || {};
      return normalizeUserRow({ ...base, ...patch, name: clean });
    })
    .filter(Boolean)
    .sort((a, b) => a.name.localeCompare(b.name));
}

export function getTagStoreSystemRows() {
  return Object.values(state.system || {}).map(cloneRow).filter(Boolean).sort((a, b) => a.name.localeCompare(b.name));
}

export function getTagStoreNames({ includeSystem = true } = {}) {
  const names = new Set(getTagStoreUserRows().map((row) => row.name));
  if (includeSystem) {
    for (const row of getTagStoreSystemRows()) names.add(row.name);
  }
  return [...names].sort((a, b) => a.localeCompare(b));
}

export function getTagStorePointMap({ includeSystem = true } = {}) {
  const map = {};
  const add = (row, source) => {
    if (!row || !row.name) return;
    map[row.name] = {
      name: row.name,
      type: row.type,
      value: row.value,
      units: row.units,
      min: row.min,
      max: row.max,
      writable: !!row.writable,
      retentive: !!row.retentive,
      source,
    };
  };
  for (const row of getTagStoreUserRows()) add(row, 'registry');
  if (includeSystem) for (const row of getTagStoreSystemRows()) add(row, 'system');
  return map;
}

export function tagNameExistsInStore(name, { caseInsensitive = true, includeSystem = true } = {}) {
  const clean = normalizeTagName(name);
  if (!clean) return false;
  const names = getTagStoreNames({ includeSystem });
  return caseInsensitive
    ? names.some((n) => n.toLowerCase() === clean.toLowerCase())
    : names.includes(clean);
}

export function markTagStoreDirty(reason = 'Unsaved tag changes') {
  state.dirty = true;
  state.dirtyReason = reason;
  state.dirtyUpdatedAt = Date.now();
}

export function clearTagStoreDirty() {
  state.dirty = false;
  state.dirtyReason = '';
  state.dirtyUpdatedAt = 0;
}

export function mergeTagRowsIntoStore(rows, { reason = 'Merged tag rows into in-memory registry' } = {}) {
  const imported = { ...(state.imported || {}) };
  let added = 0;
  let updated = 0;
  let skipped = 0;
  for (const raw of rows || []) {
    const name = normalizeTagName(raw && raw.name);
    if (!name || isReservedOrSystem(name)) { skipped++; continue; }
    const row = normalizeUserRow({ ...raw, name });
    if (!row) { skipped++; continue; }
    if (imported[name]) updated++;
    else added++;
    imported[name] = row;
  }
  state.imported = imported;
  if (added || updated) markTagStoreDirty(reason);
  return { added, updated, skipped, total: added + updated };
}

export function useTagStore() {
  return state;
}
