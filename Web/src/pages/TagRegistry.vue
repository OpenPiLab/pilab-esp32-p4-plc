<template>
  <main class="app-page space-y-4">
    <section class="flex flex-wrap items-center justify-between gap-3 border-b border-slate-800 pb-3">
      <div class="min-w-0">
        <div class="text-sky-400 font-black tracking-widest text-sm uppercase">PLC Tag Registry</div>
        <div v-if="tagStore.dirty" class="mt-1 inline-flex items-center gap-2 rounded border border-amber-500/40 bg-amber-500/10 px-2 py-1 text-[10px] font-black uppercase tracking-wide text-amber-200">Unsaved changes</div>
      </div>
      <div class="flex flex-wrap items-center justify-end gap-2">
        <input ref="tagFileInput" type="file" accept=".json,application/json" class="hidden" @change="importTagsFromFile" />
        <button @click="triggerImportTags" class="px-3 py-2 rounded bg-indigo-900 border border-indigo-600 text-xs font-black hover:bg-indigo-800">IMPORT JSON</button>
        <button @click="downloadTagRegistry" class="px-3 py-2 rounded bg-slate-800 border border-slate-600 text-xs font-black hover:bg-slate-700">EXPORT JSON</button>
        <button @click="copy(tagRegistryJson())" class="px-3 py-2 rounded bg-slate-800 border border-slate-600 text-xs font-black hover:bg-slate-700">COPY JSON</button>
        <button @click="loadFromPlc" class="px-3 py-2 rounded bg-slate-800 border border-slate-700 text-xs font-bold hover:bg-slate-700">REFRESH</button>
        <button @click="saveToPlc" :disabled="savingTags" class="px-4 py-2 rounded bg-sky-700 border border-sky-500 text-xs font-black hover:bg-sky-600 disabled:opacity-50 disabled:cursor-not-allowed">{{ savingTags ? 'SAVING...' : 'SAVE TAGS' }}</button>
      </div>
    </section>

    <section class="grid grid-cols-2 md:grid-cols-5 gap-2">
      <div class="bg-slate-900 border border-slate-800 rounded-lg px-3 py-2">
        <div class="text-[10px] text-slate-500 uppercase font-black">User Tags</div>
        <div class="text-xl leading-tight font-black text-sky-300">{{ userRowCount }}</div>
      </div>
      <div class="bg-slate-900 border border-slate-800 rounded-lg px-3 py-2">
        <div class="text-[10px] text-slate-500 uppercase font-black">System</div>
        <div class="text-xl leading-tight font-black text-violet-300">{{ systemCount }}</div>
      </div>
      <div class="bg-slate-900 border border-slate-800 rounded-lg px-3 py-2">
        <div class="text-[10px] text-slate-500 uppercase font-black">Writable</div>
        <div class="text-xl leading-tight font-black text-amber-300">{{ writableCount }}</div>
      </div>
      <div class="bg-slate-900 border border-slate-800 rounded-lg px-3 py-2">
        <div class="text-[10px] text-slate-500 uppercase font-black">Unsaved</div>
        <div class="text-xl leading-tight font-black" :class="tagStore.dirty ? 'text-amber-300' : 'text-slate-500'">{{ tagStore.dirty ? 'Yes' : 'No' }}</div>
      </div>
      <div class="bg-slate-900 border border-slate-800 rounded-lg px-3 py-2">
        <div class="text-[10px] text-slate-500 uppercase font-black">Status</div>
        <div class="text-xs leading-tight font-bold truncate" :class="statusClass">{{ status }}</div>
        <div v-if="writesBlocked" class="text-[10px] leading-tight text-amber-300 mt-0.5">Flash writes are blocked while PLC is running.</div>
      </div>
    </section>

    <section class="rounded-xl border border-slate-800 bg-slate-900 p-3 space-y-3">
      <div class="flex flex-wrap items-end gap-2">
        <div class="flex-1 min-w-[220px]">
          <label class="block text-[10px] text-slate-500 uppercase font-black mb-1">New tag name</label>
          <input v-model.trim="newTagName" @keyup.enter="addManualTag" placeholder="MotorStart, HMI_Enable, M_LatchedFault..." class="w-full bg-slate-950 border rounded px-3 py-2 text-xs font-mono outline-none focus:border-sky-400" :class="newTagName && !canAddNewTag ? 'border-red-700 text-red-300' : 'border-slate-700'" />
        </div>
        <div>
          <label class="block text-[10px] text-slate-500 uppercase font-black mb-1">Type</label>
          <select v-model="newTagType" class="bg-slate-950 border border-slate-700 rounded px-3 py-2 text-xs font-bold">
            <option value="bool">bool</option>
            <option value="int">int</option>
            <option value="float">float</option>
          </select>
        </div>
        <button @click="addManualTag" class="px-3 py-2 rounded bg-emerald-800 border border-emerald-600 text-xs font-black hover:bg-emerald-700">+ ADD TAG</button>
        <button @click="deleteUnusedImportedTags" class="px-3 py-2 rounded bg-red-950 border border-red-800 text-xs font-black text-red-200 hover:bg-red-900">DELETE UNUSED IMPORTED</button>
        <button @click="clearImportedTagRegistry" class="px-3 py-2 rounded bg-red-950 border border-red-800 text-xs font-black text-red-200 hover:bg-red-900">CLEAR IMPORTED</button>
        <button @click="clearTagRegistryEdits" class="px-3 py-2 rounded bg-amber-950 border border-amber-700 text-xs font-black text-amber-200 hover:bg-amber-900">RESET EDITS</button>
      </div>

      <div class="flex flex-wrap items-center gap-2">
        <input v-model="tagRegistryFilter" placeholder="Filter tags by name, type, status, options, usage, or description..." class="flex-1 min-w-[260px] bg-slate-950 border border-slate-700 rounded px-3 py-2 text-xs font-mono outline-none focus:border-sky-400" />
        <button v-if="tagRegistryFilter" @click="tagRegistryFilter=''" class="px-3 py-2 rounded bg-slate-800 border border-slate-600 text-xs font-bold hover:bg-slate-700">CLEAR FILTER</button>
        <div class="text-[11px] text-slate-500 font-mono">{{ filteredRows.length }} / {{ rows.length }} shown</div>
      </div>
    </section>

    <section class="bg-slate-900 border border-slate-800 rounded-xl overflow-hidden shadow-2xl">
      <div class="overflow-x-auto">
        <table class="w-full text-xs min-w-[1040px]">
          <thead class="bg-black/40 text-slate-500 uppercase text-[10px] tracking-widest">
            <tr>
              <th class="text-left p-3 w-[180px]">Name</th>
              <th class="text-left p-3 w-[125px]">Status</th>
              <th class="text-left p-3 w-[90px]">Type</th>
              <th class="text-left p-3 w-[120px]">Initial</th>
              <th class="text-left p-3 w-[85px]">Units</th>
              <th class="text-left p-3 w-[85px]">Min</th>
              <th class="text-left p-3 w-[85px]">Max</th>
              <th class="text-left p-3 w-[150px]">Options</th>
              <th class="text-left p-3">Description</th>
              <th class="text-left p-3 w-[130px]">Actions</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="t in filteredRows" :key="t.name" class="border-t border-slate-800 align-top hover:bg-slate-800/40">
              <td class="p-2">
                <div class="font-mono text-sky-200">{{ t.name }}</div>
                <div class="text-[10px] text-slate-500 mt-1">{{ tagRegistryUsageSummary(t) || (t.__used ? 'auto-discovered' : 'manual / imported') }}</div>
              </td>
              <td class="p-2">
                <div class="flex flex-wrap gap-1">
                  <span v-for="badge in tagRegistryStatusBadges(t)" :key="t.name + '_' + badge" class="text-[10px] px-1.5 py-0.5 rounded border" :class="tagRegistryBadgeClass(badge)">{{ badge }}</span>
                </div>
              </td>
              <td class="p-2">
                <select :value="t.type" :disabled="t.system" @change="updateTagRegistryField(t.name, 'type', $event.target.value)" class="w-full px-2 py-1.5 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400 disabled:opacity-60 disabled:cursor-not-allowed">
                  <option value="bool">bool</option>
                  <option value="int">int</option>
                  <option value="float">float</option>
                </select>
              </td>
              <td class="p-2">
                <select v-if="t.type === 'bool'" :value="String(t.value)" :disabled="t.system" @change="updateTagRegistryField(t.name, 'value', $event.target.value === 'true')" class="w-full px-2 py-1.5 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400 disabled:opacity-60 disabled:cursor-not-allowed">
                  <option value="false">false</option>
                  <option value="true">true</option>
                </select>
                <input v-else :value="tagFieldDraftValue(t, 'value')" :disabled="t.system" @input="updateTagRegistryField(t.name, 'value', $event.target.value, true)" @change="commitTagFieldDraft(t.name, 'value')" class="w-full px-2 py-1.5 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400 disabled:opacity-60 disabled:cursor-not-allowed" />
              </td>
              <td class="p-2"><input :value="tagFieldDraftValue(t, 'units')" :disabled="t.system" @input="updateTagRegistryField(t.name, 'units', $event.target.value, true)" @change="commitTagFieldDraft(t.name, 'units')" class="w-full px-2 py-1.5 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400 disabled:opacity-60 disabled:cursor-not-allowed" /></td>
              <td class="p-2"><input :value="tagFieldDraftValue(t, 'min')" :disabled="t.system" @input="updateTagRegistryField(t.name, 'min', $event.target.value, true)" @change="commitTagFieldDraft(t.name, 'min')" class="w-full px-2 py-1.5 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400 disabled:opacity-60 disabled:cursor-not-allowed" /></td>
              <td class="p-2"><input :value="tagFieldDraftValue(t, 'max')" :disabled="t.system" @input="updateTagRegistryField(t.name, 'max', $event.target.value, true)" @change="commitTagFieldDraft(t.name, 'max')" class="w-full px-2 py-1.5 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400 disabled:opacity-60 disabled:cursor-not-allowed" /></td>
              <td class="p-2 text-slate-300 space-y-1">
                <label class="inline-flex items-center gap-1 mr-2"><input type="checkbox" :checked="t.writable" :disabled="t.system" @change="updateTagRegistryField(t.name, 'writable', $event.target.checked)" class="disabled:opacity-50" /> writable</label>
                <label class="inline-flex items-center gap-1 mr-2"><input type="checkbox" :checked="t.retentive" :disabled="t.system" @change="updateTagRegistryField(t.name, 'retentive', $event.target.checked)" class="disabled:opacity-50" /> retentive</label>
              </td>
              <td class="p-2"><textarea :value="tagFieldDraftValue(t, 'description')" :disabled="t.system" @input="updateTagRegistryField(t.name, 'description', $event.target.value, true)" @change="commitTagFieldDraft(t.name, 'description')" rows="2" class="w-full px-2 py-1.5 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400 resize-y disabled:opacity-60 disabled:cursor-not-allowed"></textarea></td>
              <td class="p-2">
                <div class="flex flex-col gap-1 items-start">
                  <button v-if="t.__edited" @click="resetTagRegistryRow(t.name)" class="text-[10px] px-2 py-1 rounded bg-amber-500/10 border border-amber-300/30 text-amber-200">Reset Edit</button>
                  <button v-if="canDeleteTagRegistryRow(t)" @click="deleteTagRegistryRow(t.name)" class="text-[10px] px-2 py-1 rounded bg-red-500/10 border border-red-300/30 text-red-200">Delete</button>
                  <span v-if="t.system" class="text-[10px] px-2 py-1 rounded bg-slate-800 border border-slate-700 text-slate-400 font-bold">SYSTEM</span>
                  <span v-else-if="!t.__edited && !canDeleteTagRegistryRow(t)" class="text-[10px] text-slate-600">locked</span>
                </div>
              </td>
            </tr>
            <tr v-if="rows.length === 0"><td colspan="10" class="p-5 text-center text-slate-500">No user tags loaded. Add a tag, import JSON, or refresh from the PLC.</td></tr>
            <tr v-else-if="filteredRows.length === 0"><td colspan="10" class="p-5 text-center text-slate-500">No tags match the current filter.</td></tr>
          </tbody>
        </table>
      </div>
    </section>

    <section class="grid grid-cols-1 xl:grid-cols-2 gap-4">
      <div class="bg-slate-900 border border-slate-800 rounded-xl p-4 text-xs text-slate-400 leading-relaxed">
        <div class="font-black text-sky-400 uppercase tracking-widest mb-2">Script usage</div>
        <pre class="bg-black/50 border border-slate-800 rounded p-3 overflow-auto text-slate-300"><code>void scan()
{
  if (AutoMode && PumpStart) {
    Q0 = true;
  }

  if (TankLevel &gt; TankSetpoint) {
    Q1 = false;
  }
}</code></pre>
        <div class="mt-3">After saving tag definitions, upload/compile the script again. The new tag names are registered as AngelScript globals before the script is built.</div>
      </div>

      <div class="bg-slate-900 border border-slate-800 rounded-xl p-4 text-xs text-slate-400 leading-relaxed">
        <div class="font-black text-emerald-400 uppercase tracking-widest mb-2">Live runtime points</div>
        <div class="mb-3">These come from <span class="font-mono text-slate-300">/api/plc_data</span>. They are shown here so the saved registry and runtime-visible symbols can be compared without leaving the tag page.</div>
        <div class="max-h-64 overflow-auto border border-slate-800 rounded bg-black/40">
          <table class="w-full text-xs">
            <thead class="text-slate-500 bg-black/50"><tr><th class="text-left p-2">Name</th><th class="text-left p-2">Type</th><th class="text-left p-2">Value</th></tr></thead>
            <tbody>
              <tr v-for="name in runtimeNames" :key="name" class="border-t border-slate-800">
                <td class="p-2 font-mono text-slate-200">{{ name }}</td>
                <td class="p-2 text-slate-500">{{ (store.pointsByName.value || {})[name]?.type ?? '' }}</td>
                <td class="p-2 font-mono text-slate-300">{{ runtimeValueText((store.pointsByName.value || {})[name]) }}</td>
              </tr>
              <tr v-if="runtimeNames.length === 0"><td colspan="3" class="p-3 text-center text-slate-500">No runtime points received yet.</td></tr>
            </tbody>
          </table>
        </div>
      </div>
    </section>
  </main>
</template>

<script>
import { saveTags } from '../api/tagApi';
import { usePlcStore } from '../stores/plcStore';
import { ensureTagStoreLoaded, ingestTagRegistryPayload, markTagStoreDirty, mergeTagRowsIntoStore, tagNameExistsInStore, useTagStore } from '../stores/tagStore';
import { ladderTagRegistryMethods } from '../ladder/ladderTagRegistry';

export default {
  name: 'TagRegistry',
  data() {
    return {
      store: usePlcStore(),
      tagStore: useTagStore(),
      tagRegistryFilter: '',
      newTagName: '',
      newTagType: 'bool',
      project: { name: 'PLC Tag Registry', rungs: [] },
      releasePlcData: null,
      savingTags: false,
      cellDrafts: {},
    };
  },
  computed: {
    status: {
      get() { return this.tagStore.status; },
      set(v) { this.tagStore.status = v; },
    },
    tagRegistryEdits: {
      get() { return this.tagStore.edits; },
      set(v) { this.tagStore.edits = v || {}; },
    },
    tagRegistryImported: {
      get() { return this.tagStore.imported; },
      set(v) { this.tagStore.imported = v || {}; },
    },
    tagRegistrySystem: {
      get() { return this.tagStore.system; },
      set(v) { this.tagStore.system = v || {}; },
    },
    userRows() { return this.buildTagRegistryRows(); },
    userRowCount() { return this.userRows.length; },
    systemRows() { return this.buildSystemTagRows(); },
    systemCount() { return this.systemRows.length; },
    rows() { return [...this.userRows, ...this.systemRows].sort((a, b) => {
      if (!!a.system !== !!b.system) return a.system ? 1 : -1;
      return a.name.localeCompare(b.name);
    }); },
    filteredRows() { return this.filterTagRegistryRows(this.rows); },
    writableCount() { return this.userRows.filter(t => t.writable).length; },
    runtimeNames() { return (this.store.tagNames.value || []).slice().sort(); },
    writesBlocked() { return !!(this.store.online.value && !this.store.flashWritesAllowed.value); },
    statusClass() {
      if (/error|failed|blocked/i.test(this.status)) return 'text-red-300';
      if (/saved|loaded|imported|added|copied/i.test(this.status)) return 'text-emerald-300';
      return 'text-slate-300';
    },
    canAddNewTag() {
      const name = this.tagRegistryNormalizeName(this.newTagName);
      if (!name || name !== this.newTagName) return false;
      if (this.tagRegistryIsReservedOrSystem(name)) return false;
      return !tagNameExistsInStore(name, { caseInsensitive: true, includeSystem: true }) && !this.rows.some(t => String(t.name).toLowerCase() === name.toLowerCase());
    },
  },
  mounted() {
    this.store.start();
    this.releasePlcData = this.store.usePlcData();
    this.store.refreshPlcData().catch(() => {});
    if (this.tagStore.loadedOnce) {
      this.status = `Using ${this.userRowCount} cached/in-memory user tag${this.userRowCount === 1 ? '' : 's'}`;
    } else {
      ensureTagStoreLoaded().catch(() => {});
    }
  },
  beforeUnmount() {
    if (this.releasePlcData) this.releasePlcData();
  },
  methods: {
    ...ladderTagRegistryMethods,
    show(message) { this.status = message; },
    markTagsDirty(reason = 'Unsaved tag changes') {
      markTagStoreDirty(reason);
    },
    isSystemTagName(name) {
      const n = String(name || '').trim();
      return /^(I\d+|Q\d+|AI\d+|AO\d+)$/.test(n) || /^PLC_/.test(n);
    },
    normalizeSystemTagRow(raw, source = 'registry') {
      const name = this.tagRegistryNormalizeName(raw && raw.name);
      if (!name || !this.isSystemTagName(name)) return null;
      const points = this.store.pointsByName.value || {};
      const point = points[name];
      const type = ['bool', 'int', 'float'].includes(String((raw && raw.type) || (point && point.type) || '').toLowerCase())
        ? String((raw && raw.type) || (point && point.type)).toLowerCase()
        : (/^(I\d+|Q\d+)$/.test(name) ? 'bool' : 'float');
      const row = this.normalizeTagRegistryRow({
        name,
        type,
        value: raw && raw.value !== undefined ? raw.value : (point ? this.runtimePointValue(point) : undefined),
        units: raw && raw.units,
        min: raw && raw.min,
        max: raw && raw.max,
        writable: raw && raw.writable,
        retentive: raw && raw.retentive,
        hmi_visible: raw && raw.hmi_visible,
        script_visible: raw && raw.script_visible,
        description: (raw && raw.description) || `PLC-owned ${source} tag ${name}.`,
      });
      return {
        ...row,
        system: true,
        __system: true,
        __source: source,
        __used: false,
        __imported: false,
        __edited: false,
      };
    },
    runtimePointValue(point) {
      if (!point) return undefined;
      if (point.value !== undefined) return point.value;
      if (point.bool_value !== undefined) return point.bool_value;
      if (point.float_value !== undefined) return point.float_value;
      if (point.int_value !== undefined) return point.int_value;
      return undefined;
    },
    captureSystemTagRows(payload) {
      const rows = Array.isArray(payload && payload.tags) ? payload.tags : (Array.isArray(payload) ? payload : []);
      const system = {};
      for (const raw of rows) {
        const row = this.normalizeSystemTagRow(raw, 'registry');
        if (row) system[row.name] = row;
      }
      this.tagRegistrySystem = system;
    },
    buildSystemTagRows() {
      const byName = { ...(this.tagRegistrySystem || {}) };
      for (const name of this.runtimeNames) {
        if (!this.isSystemTagName(name)) continue;
        byName[name] = this.normalizeSystemTagRow({ name, ...(byName[name] || {}) }, byName[name] ? byName[name].__source : 'runtime') || byName[name];
      }
      return Object.values(byName).filter(Boolean).sort((a, b) => a.name.localeCompare(b.name));
    },
    filterTagRegistryRows(rows) {
      const q = String(this.tagRegistryFilter || '').trim().toLowerCase();
      if (!q) return rows;
      const hay = (t) => {
        const usage = this.tagRegistryUsageSummary(t);
        const flags = [t.writable ? 'writable' : '', t.retentive ? 'retentive' : ''].join(' ');
        const status = this.tagRegistryStatusBadges(t).join(' ');
        return [t.name, t.type, t.value, t.units, t.min, t.max, t.description, usage, flags, status, t.system ? 'system plc runtime locked' : 'user'].join(' ').toLowerCase();
      };
      return rows.filter(t => hay(t).includes(q));
    },
    runtimeValueText(point) {
      if (!point) return '';
      if (point.value !== undefined) return String(point.value);
      if (point.bool_value !== undefined) return String(point.bool_value);
      if (point.float_value !== undefined) return String(point.float_value);
      if (point.int_value !== undefined) return String(point.int_value);
      return JSON.stringify(point);
    },
    async loadFromPlc() {
      if (this.tagStore.dirty && !confirm('Reload tags from the PLC and discard unsaved in-memory tag edits?')) return;
      this.status = 'Reloading from PLC...';
      this.cellDrafts = {};
      try {
        await ensureTagStoreLoaded({ force: true, preserveEdits: false });
        this.status = `Loaded ${this.userRowCount} user tag${this.userRowCount === 1 ? '' : 's'} and ${this.systemCount} system tag${this.systemCount === 1 ? '' : 's'} from PLC`;
      } catch (e) {
        this.status = `Load failed: ${e?.message || e}`;
      }
    },
    async saveToPlc() {
      if (this.savingTags) return;
      if (this.writesBlocked) {
        this.status = 'Save blocked while PLC is running. Press STOP before saving tag definitions.';
        return;
      }
      this.savingTags = true;
      this.status = 'Saving tags...';
      try {
        const payload = this.buildTagRegistryPayload();
        await saveTags(payload);
        const existingSystem = { ...(this.tagStore.system || {}) };
        ingestTagRegistryPayload(payload, { preserveEdits: false });
        this.tagStore.system = existingSystem;
        this.cellDrafts = {};
        this.tagStore.lastSavedAt = Date.now();
        this.status = `Saved ${payload.tags.length} tag${payload.tags.length === 1 ? '' : 's'} to PLC`;
        this.store.refreshCommandCenter().catch(() => {});
      } catch (e) {
        this.status = `Save failed: ${e?.message || e}`;
      } finally {
        this.savingTags = false;
      }
    },
    addManualTag() {
      const name = this.tagRegistryNormalizeName(this.newTagName);
      if (!this.canAddNewTag) {
        this.status = name ? `Cannot add ${name}: invalid, reserved, or duplicate name` : 'Enter a valid tag name';
        return;
      }
      const row = this.normalizeTagRegistryRow({
        name,
        type: this.newTagType,
        writable: true,
        retentive: false,
        hmi_visible: true,
        script_visible: true,
        description: `Manual user tag ${name}.`,
      });
      this.setTagRegistryOverride(name, row);
      this.markTagsDirty(`Added tag ${name}`);
      this.newTagName = '';
      this.status = `Added ${name} (unsaved)`;
    },
    triggerImportTags() {
      this.$refs.tagFileInput.click();
    },
    importTagsFromFile(ev) {
      const file = ev?.target?.files?.[0];
      if (!file) return;
      const reader = new FileReader();
      reader.onload = () => {
        try {
          const parsed = JSON.parse(String(reader.result || ''));
          this.captureSystemTagRows(parsed || { tags: [] });
          const rows = Array.isArray(parsed?.tags) ? parsed.tags : (Array.isArray(parsed) ? parsed : []);
          const result = mergeTagRowsIntoStore(rows, { reason: 'Imported tag JSON' });
          this.tagStore.loadedOnce = true;
          this.status = `Merged ${result.total} tag row${result.total === 1 ? '' : 's'} from JSON (${result.added} added, ${result.updated} updated, ${result.skipped} skipped)`;
        } catch (e) {
          this.status = `Import failed: ${e?.message || e}`;
          alert(`Could not import tag registry JSON: ${e?.message || e}`);
        } finally {
          ev.target.value = '';
        }
      };
      reader.onerror = () => {
        this.status = `Read failed: ${reader.error ? reader.error.message : 'unknown error'}`;
        ev.target.value = '';
      };
      reader.readAsText(file);
    },
    download(filename, text, type = 'application/json') {
      const blob = new Blob([text], { type });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = filename;
      document.body.appendChild(a);
      a.click();
      a.remove();
      URL.revokeObjectURL(url);
    },
    downloadTagRegistry() {
      this.download('pilab_tags.json', this.tagRegistryJson(), 'application/json');
      this.status = 'Exported pilab_tags.json';
    },
    async copy(text) {
      try {
        await navigator.clipboard.writeText(text);
        this.status = 'Copied tag JSON';
      } catch (e) {
        this.status = `Copy failed: ${e?.message || e}`;
      }
    },
    tagFieldDraftKey(name, field) {
      return `${this.tagRegistryNormalizeName(name)}::${field}`;
    },
    tagFieldDraftValue(row, field) {
      const key = this.tagFieldDraftKey(row && row.name, field);
      return Object.prototype.hasOwnProperty.call(this.cellDrafts || {}, key) ? this.cellDrafts[key] : (row ? row[field] : '');
    },
    setTagFieldDraft(name, field, value) {
      this.cellDrafts = { ...(this.cellDrafts || {}), [this.tagFieldDraftKey(name, field)]: value };
    },
    commitTagFieldDraft(name, field) {
      const key = this.tagFieldDraftKey(name, field);
      if (!Object.prototype.hasOwnProperty.call(this.cellDrafts || {}, key)) return;
      const value = this.cellDrafts[key];
      this.setTagRegistryOverride(name, { [field]: value });
    },
    clearTagFieldDraftsFor(name) {
      const prefix = `${this.tagRegistryNormalizeName(name)}::`;
      const next = {};
      for (const [key, value] of Object.entries(this.cellDrafts || {})) {
        if (!key.startsWith(prefix)) next[key] = value;
      }
      this.cellDrafts = next;
    },
    updateTagRegistryField(name, field, value, fromInput = false) {
      if (this.isSystemTagName(name)) {
        this.status = `System tag ${name} is PLC-owned and cannot be edited here`;
        return;
      }
      if (fromInput) this.setTagFieldDraft(name, field, value);
      this.setTagRegistryOverride(name, { [field]: value });
      this.markTagsDirty(`Updated tag ${name}`);
      this.status = 'Tag metadata updated (unsaved)';
    },
    resetTagRegistryRow(name) {
      this.clearTagRegistryOverride(name);
      this.clearTagFieldDraftsFor(name);
      this.markTagsDirty(`Reset tag ${name}`);
      this.status = 'Tag metadata reset (unsaved)';
    },
    canDeleteTagRegistryRow(t) {
      return !!(t && !t.system && !t.__used && (t.__imported || t.__edited));
    },
    deleteTagRegistryRow(name) {
      const row = this.buildTagRegistryRows().find(t => t.name === name);
      if (!this.canDeleteTagRegistryRow(row)) {
        this.status = 'Only unused imported/manual tags can be deleted here';
        return;
      }
      if (!confirm(`Delete unused tag "${name}" from the registry view?`)) return;
      const key = this.tagRegistryNormalizeName(name);
      if (this.tagRegistryImported && this.tagRegistryImported[key]) delete this.tagRegistryImported[key];
      if (this.tagRegistryEdits && this.tagRegistryEdits[key]) delete this.tagRegistryEdits[key];
      this.clearTagFieldDraftsFor(key);
      this.markTagsDirty(`Deleted tag ${key}`);
      this.status = 'Deleted unused tag (unsaved)';
    },
    deleteUnusedImportedTags() {
      const count = Object.values(this.tagRegistryImported || {}).filter(row => !this.tagRegistryUsedNameSet().has(row.name)).length;
      if (count < 1) { this.status = 'No unused imported tags to delete'; return; }
      if (!confirm(`Delete ${count} unused imported tag${count === 1 ? '' : 's'} from the registry view?`)) return;
      const removed = this.deleteUnusedImportedTagRegistryRows();
      if (removed) this.markTagsDirty('Deleted unused imported tags');
      this.status = `Deleted ${removed} unused imported tag${removed === 1 ? '' : 's'} (unsaved)`;
    },
    clearImportedTagRegistry() {
      const count = Object.keys(this.tagRegistryImported || {}).length;
      if (count < 1) { this.status = 'No imported/manual tag metadata to clear'; return; }
      if (!confirm(`Clear ${count} imported/manual tag${count === 1 ? '' : 's'} from the in-memory registry?`)) return;
      this.clearImportedTagRegistryRows();
      this.pruneUnusedTagRegistryEdits();
      this.markTagsDirty('Cleared imported/manual tags');
      this.status = `Cleared ${count} imported/manual tag${count === 1 ? '' : 's'} (unsaved)`;
    },
    clearTagRegistryEdits() {
      if (Object.keys(this.tagRegistryEdits || {}).length && !confirm('Reset all edited tag metadata back to imported/default values?')) return;
      this.tagRegistryEdits = {};
      this.cellDrafts = {};
      this.markTagsDirty('Reset tag metadata edits');
      this.status = 'Tag metadata edits reset (unsaved)';
    },
    tagRegistryUsageSummary(t) {
      const u = t && t.__usage ? t.__usage : null;
      if (!u) return '';
      const parts = [];
      if (u.reads) parts.push(`read ${u.reads}`);
      if (u.writes) parts.push(`write ${u.writes}`);
      if (u.numeric) parts.push('numeric');
      return parts.join(' · ');
    },
    tagRegistryStatusBadges(t) {
      const badges = [];
      if (t && t.system) return ['system'];
      badges.push(t && t.__used ? 'used' : 'unused');
      if (t && t.__imported) badges.push('imported');
      if (t && !t.__imported && t.__used) badges.push('auto');
      if (t && t.__edited) badges.push('edited');
      return badges;
    },
    tagRegistryBadgeClass(badge) {
      if (badge === 'system') return 'bg-violet-500/10 border-violet-300/30 text-violet-200';
      if (badge === 'used') return 'bg-emerald-500/10 border-emerald-300/30 text-emerald-200';
      if (badge === 'unused') return 'bg-slate-800/70 border-slate-600 text-slate-400';
      if (badge === 'imported') return 'bg-indigo-500/10 border-indigo-300/35 text-indigo-200';
      if (badge === 'edited') return 'bg-amber-500/10 border-amber-300/35 text-amber-200';
      return 'bg-slate-900 border-slate-700 text-slate-400';
    },
    filteredTagRegistryRows() {
      const rows = this.buildTagRegistryRows();
      const q = String(this.tagRegistryFilter || '').trim().toLowerCase();
      if (!q) return rows;
      const hay = (t) => {
        const usage = this.tagRegistryUsageSummary(t);
        const flags = [t.writable ? 'writable' : '', t.retentive ? 'retentive' : ''].join(' ');
        const status = this.tagRegistryStatusBadges(t).join(' ');
        return [t.name, t.type, t.value, t.units, t.min, t.max, t.description, usage, flags, status].join(' ').toLowerCase();
      };
      return rows.filter(t => hay(t).includes(q));
    },
  },
};
</script>
