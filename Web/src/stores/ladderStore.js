import { reactive } from 'vue';

const state = reactive({
  initialized: false,
  savedAt: 0,
  snapshot: null,
  currentFileName: '',
  currentFilePath: '',
  dirty: false,
  lastSavedAt: 0
});

function clone(value) {
  try {
    return JSON.parse(JSON.stringify(value));
  } catch (_) {
    return null;
  }
}

export function useLadderStore() {
  return state;
}

export function saveLadderEditorSnapshot(vm) {
  if (!vm) return;
  state.snapshot = {
    project: clone(vm.project),
    activeView: vm.activeView || 'project',
    jsonDraft: typeof vm.jsonDraft === 'string' ? vm.jsonDraft : '',
    selectedTool: vm.selectedTool || 'NO',
    mode: vm.mode || 'select',
    collapsedPanels: clone(vm.collapsedPanels) || { inspector: false, validation: false, simulator: false },
    history: clone(vm.history) || [],
    includeAngelScriptTags: !!vm.includeAngelScriptTags,
    simTags: clone(vm.simTags) || {},
    simBlocks: clone(vm.simBlocks) || {},
    simRungs: clone(vm.simRungs) || {},
    simScanCount: Number(vm.simScanCount || 0),
    simTagFilter: vm.simTagFilter || '',
    simWatchOnly: !!vm.simWatchOnly,
    simWatchTags: clone(vm.simWatchTags) || {},
    currentFileName: state.currentFileName || '',
    currentFilePath: state.currentFilePath || '',
    dirty: !!state.dirty,
    lastSavedAt: Number(state.lastSavedAt || 0)
  };
  state.initialized = true;
  state.savedAt = Date.now();
}

export function restoreLadderEditorSnapshot(vm) {
  const s = state.snapshot;
  if (!vm || !state.initialized || !s) return false;

  if (s.project) vm.project = clone(s.project) || s.project;
  vm.activeView = s.activeView || 'project';
  vm.jsonDraft = typeof s.jsonDraft === 'string' ? s.jsonDraft : vm.jsonModel;
  vm.selectedTool = s.selectedTool || 'NO';
  vm.mode = s.mode || 'select';
  vm.collapsedPanels = clone(s.collapsedPanels) || { inspector: false, validation: false, simulator: false };
  vm.history = clone(s.history) || [];
  vm.includeAngelScriptTags = !!s.includeAngelScriptTags;
  vm.simTags = clone(s.simTags) || {};
  vm.simBlocks = clone(s.simBlocks) || {};
  vm.simRungs = clone(s.simRungs) || {};
  vm.simScanCount = Number(s.simScanCount || 0);
  vm.simTagFilter = s.simTagFilter || '';
  vm.simWatchOnly = !!s.simWatchOnly;
  vm.simWatchTags = clone(s.simWatchTags) || {};
  state.currentFileName = s.currentFileName || state.currentFileName || '';
  state.currentFilePath = s.currentFilePath || state.currentFilePath || '';
  state.dirty = !!s.dirty;
  state.lastSavedAt = Number(s.lastSavedAt || state.lastSavedAt || 0);

  vm.selected = null;
  vm.selectedBranch = null;
  vm.branchStart = null;
  vm.editingCommentId = null;
  vm.hoverRungId = null;
  vm.simRunning = false;
  vm.simTimer = null;
  vm.simProgram = null;
  vm.simCompiledCode = '';
  return true;
}

export function resetLadderEditorSnapshot() {
  state.initialized = false;
  state.savedAt = 0;
  state.snapshot = null;
  state.currentFileName = '';
  state.currentFilePath = '';
  state.dirty = false;
  state.lastSavedAt = 0;
}

export function markLadderProjectDirty() {
  state.dirty = true;
}

export function markLadderProjectSaved() {
  state.dirty = false;
  state.lastSavedAt = Date.now();
}

export function setLadderCurrentFile(name, path) {
  state.currentFileName = name || '';
  state.currentFilePath = path || '';
}
