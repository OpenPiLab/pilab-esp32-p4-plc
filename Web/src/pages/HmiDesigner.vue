<template>
  <section class="hmi-route flex flex-col overflow-hidden bg-slate-950" style="height:calc(100vh - var(--app-topbar-height)); min-height:0;">
    <header class="hmi-toolbar">
        <div class="hmi-toolbar-left">
            <div class="hmi-mode-switch" aria-label="HMI mode">
                <button @click="setEditMode(true)" :class="['hmi-mode-button', editMode ? 'active' : '']">Build</button>
                <button @click="setEditMode(false)" :class="['hmi-mode-button', !editMode ? 'active live' : '']">Live</button>
            </div>
            <span v-if="alarmCount > 0" class="hmi-alarm-inline">{{ alarmCount }} alarms</span>
        </div>

        <div v-if="editMode" class="hmi-widget-links" aria-label="Add HMI widget">
            <button v-for="t in controlTypes" :key="t" @click="addWidget(t)" class="hmi-text-link">
                + {{ t }}
            </button>
        </div>
        <div v-else class="hmi-live-note">
            Live operator view
        </div>

        <div class="hmi-toolbar-actions">
            <button @click="pollPlcData" class="hmi-text-link muted">Refresh</button>
            <button @click="saveLayoutLocal" class="hmi-text-link">Save</button>
            <button @click="loadLayoutLocal" class="hmi-text-link">Load</button>
            <button @click="downloadJSON" class="hmi-text-link">Export</button>
            <label class="hmi-text-link cursor-pointer">
                Import
                <input type="file" class="hidden" accept="application/json" @change="importJSON">
            </label>
        </div>
    </header>

    <div class="flex-1 min-h-0 flex overflow-hidden">
        <main ref="canvas"
              class="flex-1 min-h-0 relative overflow-auto"
              :class="editMode ? 'canvas-grid' : 'bg-slate-950'"
              @pointermove="handlePointerMove"
              @pointerup="stopInteraction"
              @pointerleave="stopInteraction"
              @click.self="selectedIndex = null">

            <div v-for="(w, index) in visibleWidgets" :key="w.id"
                 @click.stop="selectedIndex = widgetIndex(w)"
                 :style="{ left: w.x + 'px', top: w.y + 'px', width: w.w + 'px', height: w.h + 'px' }"
                 :class="widgetClass(w, widgetIndex(w))">

                <div v-if="editMode" @pointerdown.prevent.stop="startDrag($event, widgetIndex(w))"
                     class="h-5 bg-slate-800/50 border-b border-slate-700 cursor-move flex items-center justify-between px-2 shrink-0">
                    <span class="text-[7px] font-black text-slate-500 uppercase tracking-widest">{{ w.type }}</span>
                    <span class="text-[7px] font-black" :class="w.props.locked ? 'text-amber-400' : 'text-slate-600'">{{ w.props.locked ? 'LOCKED' : w.props.pin }}</span>
                </div>

                <div class="flex-1 p-3 flex flex-col items-center justify-center overflow-hidden" :class="editMode ? 'pointer-events-none' : 'pointer-events-auto'">

                    <div v-if="w.type === 'gauge'" class="w-full flex flex-col justify-center h-full">
                        <div class="flex justify-between text-[9px] mb-1 font-bold uppercase tracking-tight" :style="{ color: displayColor(w) }">
                            <span>{{ w.props.label }}</span>
                            <span>{{ formatValue(w) }}{{ w.props.unit }}</span>
                        </div>
                        <div class="h-1/3 bg-black rounded-sm border border-slate-800 p-0.5 relative">
                            <div class="h-full transition-all duration-500 shadow-lg"
                                 :style="{ width: percentValue(w) + '%', backgroundColor: displayColor(w) }"></div>
                        </div>
                    </div>

                    <div v-if="w.type === 'tank'" class="h-full w-full flex flex-col items-center">
                        <div class="flex-1 w-2/3 bg-slate-800 border-2 border-slate-700 rounded-b-xl relative overflow-hidden">
                            <div class="absolute bottom-0 w-full transition-all duration-700"
                                 :style="{ height: percentValue(w) + '%', backgroundColor: displayColor(w) }">
                                <div class="w-full h-2 bg-white/20 animate-pulse"></div>
                            </div>
                        </div>
                        <span class="text-[8px] font-bold mt-1 text-slate-500 uppercase">{{ w.props.label }}</span>
                    </div>

                    <div v-if="w.type === 'readout'" class="w-full h-full flex flex-col items-center justify-center bg-black/60 rounded border border-slate-800 shadow-inner">
                        <span class="text-[7px] text-slate-500 uppercase mb-1">{{ w.props.label }}</span>
                        <div class="font-lcd leading-none text-center truncate w-full px-2" :style="{ color: displayColor(w), fontSize: (w.h * 0.4) + 'px' }">
                            {{ formatValue(w) }}
                        </div>
                    </div>

                    <div v-if="w.type === 'toggle'" class="flex flex-col items-center gap-2">
                        <button @click.stop="toggleCommand(w)"
                                class="w-12 h-6 rounded-full p-1 transition-colors duration-300"
                                :style="{ backgroundColor: w.props.active ? displayColor(w) : '#1e293b' }">
                            <div class="w-4 h-4 bg-white rounded-full transition-transform duration-300"
                                 :style="{ transform: w.props.active ? 'translateX(24px)' : 'translateX(0)' }"></div>
                        </button>
                        <span class="text-[9px] font-bold uppercase">{{ w.props.label }}</span>
                        <span v-if="w.props.pendingWrite" class="text-[7px] font-black text-amber-300 uppercase">LOCAL</span>
                        <span v-if="w.props.writeError" class="text-[7px] font-black text-red-300 uppercase">WRITE N/A</span>
                    </div>

                    <div v-if="w.type === 'trend'" class="w-full h-full flex flex-col">
                        <div class="flex-1 bg-black/80 rounded p-1 flex items-end gap-px border border-slate-800">
                            <div v-for="(v, i) in w.history" :key="i"
                                 :style="{ height: v + '%', backgroundColor: displayColor(w) }"
                                 class="flex-1 opacity-70 min-w-[2px]"></div>
                        </div>
                    </div>

                    <div v-if="w.type === 'thermometer'" class="h-full flex items-center gap-2 w-full px-4">
                        <div class="h-full w-4 bg-slate-800 rounded-full border border-slate-700 p-0.5 relative overflow-hidden">
                            <div class="absolute bottom-0 left-0 w-full rounded-full transition-all"
                                 :style="{ height: percentValue(w) + '%', backgroundColor: displayColor(w) }"></div>
                        </div>
                        <div class="flex flex-col">
                            <span class="text-xs font-lcd text-white">{{ formatValue(w) }}{{ w.props.unit }}</span>
                            <span class="text-[7px] uppercase text-slate-500">{{ w.props.label }}</span>
                        </div>
                    </div>

                    <div v-if="w.type === 'led'" class="flex flex-col items-center gap-2">
                        <div class="rounded-full border-4 border-slate-800"
                             :style="{ height: (w.h * 0.5) + 'px', width: (w.h * 0.5) + 'px', backgroundColor: w.props.active ? displayColor(w) : '#0f172a', boxShadow: w.props.active ? `0 0 20px ${displayColor(w)}` : 'none' }"></div>
                        <span class="text-[9px] font-bold uppercase">{{ w.props.label }}</span>
                    </div>

                    <div v-if="w.props.stale" class="absolute right-2 bottom-2 text-[8px] font-black text-amber-300 bg-amber-950/80 border border-amber-800 rounded px-1">STALE</div>
                    <div v-if="w.props.alarm" class="absolute left-2 bottom-2 text-[8px] font-black text-red-300 bg-red-950/80 border border-red-800 rounded px-1">ALARM</div>
                </div>

                <div v-if="editMode && !w.props.locked" @pointerdown.prevent.stop="startResize($event, widgetIndex(w))" class="resizer"></div>
            </div>
        </main>

        <aside v-if="editMode" class="w-80 shrink-0 min-h-0 bg-slate-900 border-l border-slate-800 flex flex-col z-50">
            <div class="p-4 border-b border-slate-800 flex justify-between items-center bg-slate-900/50">
                <h2 class="text-[10px] font-black uppercase text-sky-500 tracking-widest">Inspector</h2>
                <span v-if="selectedIndex !== null" class="text-[8px] px-2 py-0.5 bg-sky-900 text-sky-200 rounded-full font-bold">#{{ widgets[selectedIndex].id.toString().slice(-4) }}</span>
            </div>

            <div v-if="selectedIndex !== null" class="flex-1 min-h-0 overflow-y-auto p-4 space-y-5">
                <section class="space-y-3">
                    <h3 class="text-[9px] font-bold text-slate-500 uppercase border-b border-slate-800 pb-1">Hardware Binding</h3>
                    <div>
                        <label class="text-[8px] uppercase text-slate-500">PLC Tag</label>
                        <input v-model="widgets[selectedIndex].props.pin"
                               class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px] font-mono"
                               placeholder="Manual tag, e.g. TankLevel or Q6">
                        <div class="mt-2 flex gap-2">
                            <input v-model="tagPickerFilter"
                                   class="flex-1 bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px] font-mono"
                                   placeholder="Filter live tags...">
                            <button @click="tagPickerFilter = ''"
                                    class="px-2 bg-slate-800 border border-slate-700 rounded text-[9px] font-bold text-slate-300">CLEAR</button>
                        </div>
                        <div class="mt-2 max-h-32 overflow-y-auto border border-slate-800 rounded bg-slate-950/80">
                            <button v-for="tag in filteredTagNames" :key="tag" @click="selectTag(tag)"
                                    class="w-full flex items-center justify-between gap-2 px-2 py-1 text-left text-[10px] font-mono hover:bg-sky-900/40 border-b border-slate-900 last:border-b-0"
                                    :class="widgets[selectedIndex].props.pin === tag ? 'text-sky-300 bg-sky-950/60' : 'text-slate-300'">
                                <span>{{ tag }}</span>
                                <span class="text-[9px] text-slate-500 truncate">{{ tagValueText(tag) }}</span>
                            </button>
                            <div v-if="filteredTagNames.length === 0" class="px-2 py-2 text-[10px] text-slate-600 italic">No matching live tags yet.</div>
                        </div>
                    </div>
                    <div class="grid grid-cols-2 gap-2">
                        <div>
                            <label class="text-[8px] uppercase text-slate-500">Unit</label>
                            <input v-model="widgets[selectedIndex].props.unit" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]">
                        </div>
                        <div>
                            <label class="text-[8px] uppercase text-slate-500">Decimals</label>
                            <input type="number" min="0" max="6" v-model.number="widgets[selectedIndex].props.decimals" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]">
                        </div>
                    </div>
                </section>

                <section class="space-y-3">
                    <h3 class="text-[9px] font-bold text-slate-500 uppercase border-b border-slate-800 pb-1">Scaling</h3>
                    <div class="grid grid-cols-2 gap-2">
                        <div><label class="text-[8px] uppercase text-slate-500">Raw Min</label><input type="number" v-model.number="widgets[selectedIndex].props.rawMin" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]"></div>
                        <div><label class="text-[8px] uppercase text-slate-500">Raw Max</label><input type="number" v-model.number="widgets[selectedIndex].props.rawMax" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]"></div>
                        <div><label class="text-[8px] uppercase text-slate-500">Display Min</label><input type="number" v-model.number="widgets[selectedIndex].props.min" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]"></div>
                        <div><label class="text-[8px] uppercase text-slate-500">Display Max</label><input type="number" v-model.number="widgets[selectedIndex].props.max" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]"></div>
                    </div>
                </section>

                <section class="space-y-3">
                    <h3 class="text-[9px] font-bold text-slate-500 uppercase border-b border-slate-800 pb-1">Alarms / Stale</h3>
                    <div class="grid grid-cols-2 gap-2">
                        <div><label class="text-[8px] uppercase text-slate-500">Low Alarm</label><input type="number" v-model.number="widgets[selectedIndex].props.alarmLow" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]"></div>
                        <div><label class="text-[8px] uppercase text-slate-500">High Alarm</label><input type="number" v-model.number="widgets[selectedIndex].props.alarmHigh" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]"></div>
                        <div><label class="text-[8px] uppercase text-slate-500">Stale ms</label><input type="number" v-model.number="widgets[selectedIndex].props.staleMs" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]"></div>
                        <div><label class="text-[8px] uppercase text-slate-500">Color</label><input type="color" v-model="widgets[selectedIndex].props.color" class="w-full h-8 bg-slate-950 border border-slate-800 p-1 rounded cursor-pointer"></div>
                    </div>
                </section>

                <section class="space-y-3">
                    <h3 class="text-[9px] font-bold text-slate-500 uppercase border-b border-slate-800 pb-1">Appearance & Tools</h3>
                    <div>
                        <label class="text-[8px] uppercase text-slate-500">Label</label>
                        <input type="text" v-model="widgets[selectedIndex].props.label" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]">
                    </div>
                    <div class="grid grid-cols-2 gap-2">
                        <button @click="duplicateWidget(selectedIndex)" class="py-2 bg-slate-800 text-slate-300 text-[9px] font-black border border-slate-700 rounded hover:bg-slate-700 uppercase">Duplicate</button>
                        <button @click="widgets[selectedIndex].props.locked = !widgets[selectedIndex].props.locked" class="py-2 bg-slate-800 text-slate-300 text-[9px] font-black border border-slate-700 rounded hover:bg-slate-700 uppercase">{{ widgets[selectedIndex].props.locked ? 'Unlock' : 'Lock' }}</button>
                        <button @click="alignSelected('left')" class="py-2 bg-slate-800 text-slate-300 text-[9px] font-black border border-slate-700 rounded hover:bg-slate-700 uppercase">Align L</button>
                        <button @click="alignSelected('top')" class="py-2 bg-slate-800 text-slate-300 text-[9px] font-black border border-slate-700 rounded hover:bg-slate-700 uppercase">Align T</button>
                    </div>
                </section>

                <button @click="removeWidget(selectedIndex)" class="w-full py-2 bg-red-950/40 text-red-500 text-[9px] font-black border border-red-900/50 rounded hover:bg-red-600 hover:text-white transition-all uppercase">
                    Destroy Component
                </button>
            </div>

            <div v-else class="flex-1 flex flex-col items-center justify-center p-8 text-center text-slate-600 text-xs italic gap-3">
                <div>Select a component on the design surface to configure properties.</div>
                <div class="text-[10px] text-slate-500">Tip: use PLC Tag picker to bind widgets to /api/plc_data.</div>
            </div>
        </aside>
    </div>
  </section>
</template>

<script setup>
import { ref, computed, onMounted, onBeforeUnmount, watch } from 'vue';
import { usePlcStore } from '../stores/plcStore';
import { ensureTagStoreLoaded, getTagStoreNames, getTagStorePointMap } from '../stores/tagStore';


        const plcStore = usePlcStore();
        let releasePlcData = null;

        const HMI_MODE_KEY = 'pilab_hmi_edit_mode_v1';
        const HMI_LAYOUT_KEY = 'pilab_hmi_layout_v2';
        const loadInitialEditMode = () => {
            try {
                const saved = localStorage.getItem(HMI_MODE_KEY);
                if (saved === 'live') return false;
                if (saved === 'build') return true;
            } catch {}
            return true;
        };

        const editMode = ref(loadInitialEditMode());
        const widgets = ref([]);
        const selectedIndex = ref(null);
        const activeIndex = ref(null);
        const interactionMode = ref(null);
        const plcTags = ref({});
        const plcPoints = ref({});
        const tagLastSeen = ref({});
        const tagNames = ref([]);
        const tagPickerFilter = ref('');
        const plcOnline = ref(false);
        const pointCount = ref(0);
        const currentPage = ref('Main');

        const controlTypes = ['gauge', 'readout', 'tank', 'led', 'trend', 'thermometer', 'toggle'];
        let startX, startY, initialX, initialY, initialW, initialH;
        let layoutSaveTimer = null;
        let layoutHydrated = false;

        const persistEditMode = () => {
            try {
                localStorage.setItem(HMI_MODE_KEY, editMode.value ? 'build' : 'live');
            } catch {}
        };

        const setEditMode = (mode) => {
            editMode.value = !!mode;
            if (!editMode.value) {
                selectedIndex.value = null;
                stopInteraction();
            }
            persistEditMode();
        };

        watch(editMode, persistEditMode);

        const baseProps = (p) => ({
            label: p.label || 'TAG', color: p.color || '#38bdf8', pin: p.pin || '', min: p.min ?? 0, max: p.max ?? 100,
            rawMin: p.rawMin ?? 0, rawMax: p.rawMax ?? 100, value: p.value ?? 0, unit: p.unit || '', decimals: p.decimals ?? 1,
            active: p.active ?? false, alarmLow: p.alarmLow ?? null, alarmHigh: p.alarmHigh ?? null, staleMs: p.staleMs ?? 2500,
            stale: false, alarm: false, locked: false, writable: p.writable ?? false, page: p.page || 'Main',
            pendingWrite: false, writeError: false, localOverrideUntil: 0
        });

        const isWritableTag = (tag) => {
            if (!tag) return false;
            const p = plcPoints.value[tag] || getTagStorePointMap({ includeSystem: true })[tag];
            if (!p) return false;
            // User-created / unsaved registry tags are intended to be HMI-writable command/state tags.
            // Physical outputs may also be useful for manual testing, but inputs,
            // analogs, and simulated tags should stay read-only from the HMI.
            return p.source === 'user' || p.source === 'registry' || p.writable === true || /^Q[0-7]$/.test(tag);
        };

        const defaults = {
            gauge:       baseProps({ label: 'TankLevel', color: '#0ea5e9', pin: 'TankLevel', min: 0, max: 100, rawMin: 0, rawMax: 1, value: 0, unit: '', decimals: 3 }),
            readout:     baseProps({ label: 'Q0', color: '#f59e0b', pin: 'Q0', value: 0, unit: '', decimals: 0 }),
            tank:        baseProps({ label: 'TankCommand', color: '#22c55e', pin: 'TankCommand', min: 0, max: 100, rawMin: 0, rawMax: 100, value: 0, unit: '%' }),
            led:         baseProps({ label: 'Q0', color: '#22c55e', pin: 'Q0', active: false }),
            trend:       baseProps({ label: 'TankCommand', color: '#a855f7', pin: 'TankCommand', min: 0, max: 100, rawMin: 0, rawMax: 100 }),
            thermometer: baseProps({ label: 'TemperaturePV', color: '#f43f5e', pin: 'TemperaturePV', min: 0, max: 200, rawMin: 0, rawMax: 1024, value: 0, unit: '°', decimals: 1 }),
            toggle:      baseProps({ label: 'HMI_I0', color: '#38bdf8', pin: 'HMI_I0', active: false, writable: true })
        };

        const visibleWidgets = computed(() => widgets.value.filter(w => (w.props.page || 'Main') === currentPage.value));
        const alarmCount = computed(() => widgets.value.filter(w => w.props.alarm).length);
        const combinedTagNames = computed(() => {
            const names = new Set([...(getTagStoreNames({ includeSystem: true }) || []), ...(tagNames.value || [])]);
            return [...names].sort((a, b) => a.localeCompare(b));
        });
        const filteredTagNames = computed(() => {
            const q = String(tagPickerFilter.value || '').trim().toLowerCase();
            const list = q ? combinedTagNames.value.filter(t => t.toLowerCase().includes(q)) : combinedTagNames.value;
            return list.slice(0, 80);
        });

        const widgetIndex = (w) => widgets.value.findIndex(x => x.id === w.id);
        const selectTag = (tag) => {
            const w = widgets.value[selectedIndex.value];
            if (!w) return;
            w.props.pin = tag;
            if (!w.props.label || w.props.label === 'TAG') w.props.label = tag;
            if (w.type === 'toggle') {
                w.props.writable = isWritableTag(tag);
                w.props.writeError = false;
                w.props.pendingWrite = false;
            }
            tagPickerFilter.value = tag;
        };
        const tagValueText = (tag) => {
            const p = plcPoints.value[tag] || getTagStorePointMap({ includeSystem: true })[tag];
            if (!p) return '';
            const v = p.value;
            if (v === undefined) return p.source === 'registry' ? 'unsaved registry' : '';
            if (typeof v === 'number') return String(Number(v.toFixed ? v.toFixed(3) : v));
            return String(v);
        };

        const addWidget = (type, overrides = {}) => {
            const count = widgets.value.length;
            const w = {
                id: Date.now() + Math.floor(Math.random() * 10000), type,
                x: overrides.x ?? (100 + (count % 3) * 275),
                y: overrides.y ?? (100 + Math.floor(count / 3) * 200),
                w: overrides.w ?? (type === 'trend' ? 380 : 225),
                h: overrides.h ?? (type === 'trend' ? 150 : 150),
                props: { ...JSON.parse(JSON.stringify(defaults[type])), ...(overrides.props || {}) },
                history: type === 'trend' ? Array(40).fill(0) : []
            };
            widgets.value.push(w);
            selectedIndex.value = widgets.value.length - 1;
        };

        const rawToDisplay = (w, raw) => {
            if (typeof raw === 'boolean') return raw ? 1 : 0;
            const r = Number(raw);
            if (!Number.isFinite(r)) return 0;
            const rawMin = Number(w.props.rawMin ?? w.props.min ?? 0);
            const rawMax = Number(w.props.rawMax ?? w.props.max ?? 100);
            const dMin = Number(w.props.min ?? 0);
            const dMax = Number(w.props.max ?? 100);
            if (rawMax === rawMin) return r;
            return dMin + ((r - rawMin) * (dMax - dMin)) / (rawMax - rawMin);
        };

        const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
        const percentValue = (w) => {
            const min = Number(w.props.min ?? 0), max = Number(w.props.max ?? 100);
            if (max === min) return 0;
            return clamp(((Number(w.props.value) - min) / (max - min)) * 100, 0, 100);
        };
        const formatValue = (w) => {
            const v = Number(w.props.value);
            if (!Number.isFinite(v)) return String(w.props.value ?? '---');
            const d = Number(w.props.decimals ?? 1);
            return v.toFixed(clamp(d, 0, 6));
        };
        const displayColor = (w) => w.props.alarm ? '#ef4444' : (w.props.stale ? '#f59e0b' : w.props.color);

        const widgetClass = (w, index) => {
            const selected = selectedIndex.value === index && editMode.value;
            return [
                'absolute widget-box bg-slate-900/90 border flex flex-col rounded shadow-xl overflow-hidden',
                selected ? 'border-sky-500 ring-2 ring-sky-500/20 z-40' : 'border-slate-800 z-10',
                !editMode.value ? 'bg-transparent border-transparent shadow-none' : '',
                w.props.alarm ? 'alarm-pulse !border-red-600' : '',
                w.props.stale ? 'opacity-70 !border-amber-600' : ''
            ];
        };

        function ingestPlcData(data) {
            const now = Date.now();
            const map = {};
            const names = [];
            for (const p of data.points || []) {
                map[p.name] = p.value;
                plcPoints.value[p.name] = p;
                names.push(p.name);
                tagLastSeen.value[p.name] = now;
            }
            plcTags.value = map;
            tagNames.value = names.sort();
            pointCount.value = Number(data.point_count ?? names.length);
            plcOnline.value = plcStore.plcDataOnline.value;
            widgets.value.forEach(w => {
                if (w.type === 'toggle' && w.props.pin && isWritableTag(w.props.pin)) {
                    w.props.writable = true;
                }
            });
            applyTags(now);
        }

        async function pollPlcData() {
            try {
                ingestPlcData(await plcStore.refreshPlcData());
            } catch (e) {
                plcOnline.value = false;
                markStale(Date.now(), true);
                console.warn('PLC data poll failed', e);
            }
        }

        watch(plcStore.dataLastUpdated, () => ingestPlcData(plcStore.plcData.value));

        const applyTags = (now) => {
            widgets.value.forEach(w => {
                const tag = w.props.pin;
                const last = tag ? tagLastSeen.value[tag] : 0;
                w.props.stale = !tag || !last || (now - last) > Number(w.props.staleMs ?? 2500);
                if (!tag || !(tag in plcTags.value)) return;
                const raw = plcTags.value[tag];
                if (w.type === 'led') {
                    w.props.active = !!raw;
                    w.props.value = w.props.active ? 1 : 0;
                } else if (w.type === 'toggle') {
                    const nowMs = Date.now();
                    const serverActive = !!raw;
                    if (w.props.pendingWrite && serverActive === !!w.props.active) {
                        w.props.pendingWrite = false;
                        w.props.writeError = false;
                        w.props.localOverrideUntil = 0;
                    }
                    if (!w.props.pendingWrite || nowMs > Number(w.props.localOverrideUntil || 0)) {
                        w.props.pendingWrite = false;
                        w.props.active = serverActive;
                        w.props.value = w.props.active ? 1 : 0;
                    }
                } else {
                    const val = rawToDisplay(w, raw);
                    w.props.value = val;
                    if (w.type === 'trend') {
                        w.history.shift();
                        w.history.push(percentValue(w));
                    }
                }
                const n = Number(w.props.value);
                const low = w.props.alarmLow;
                const high = w.props.alarmHigh;
                w.props.alarm = Number.isFinite(n) && ((low !== null && low !== '' && n < Number(low)) || (high !== null && high !== '' && n > Number(high)));
            });
        };

        const markStale = (now, force) => {
            widgets.value.forEach(w => {
                if (force || ((now - (tagLastSeen.value[w.props.pin] || 0)) > Number(w.props.staleMs ?? 2500))) {
                    w.props.stale = true;
                }
            });
        };

        const toggleCommand = async (w) => {
            const next = !w.props.active;
            w.props.active = next;
            w.props.value = next ? 1 : 0;
            w.props.pendingWrite = true;
            w.props.writeError = false;
            // Keep the clicked state visible even if the readback endpoint has not caught up yet.
            w.props.localOverrideUntil = Date.now() + 2000;

            if (!w.props.writable) {
                w.props.pendingWrite = false;
                w.props.writeError = true;
                w.props.localOverrideUntil = Date.now() + 2500;
                console.warn('Toggle is not writable:', w.props.pin);
                return;
            }
            try {
                await plcStore.plcWrite(w.props.pin, next);
            } catch (e) {
                // There is no PLC write endpoint yet, so keep this as a local/manual HMI action for now.
                w.props.writeError = true;
                w.props.localOverrideUntil = Date.now() + 5000;
                console.warn('Write endpoint not available yet', e);
            }
        };

        const startDrag = (e, index) => {
            if (widgets.value[index]?.props.locked) return;
            interactionMode.value = 'move'; activeIndex.value = index; selectedIndex.value = index;
            startX = e.clientX; startY = e.clientY; initialX = widgets.value[index].x; initialY = widgets.value[index].y;
        };
        const startResize = (e, index) => {
            if (widgets.value[index]?.props.locked) return;
            interactionMode.value = 'resize'; activeIndex.value = index; selectedIndex.value = index;
            startX = e.clientX; startY = e.clientY; initialW = widgets.value[index].w; initialH = widgets.value[index].h;
        };
        const handlePointerMove = (e) => {
            if (activeIndex.value === null) return;
            const grid = 20, dx = e.clientX - startX, dy = e.clientY - startY;
            const w = widgets.value[activeIndex.value];
            if (!w || w.props.locked) return;
            if (interactionMode.value === 'move') {
                w.x = Math.round((initialX + dx) / grid) * grid;
                w.y = Math.round((initialY + dy) / grid) * grid;
            } else if (interactionMode.value === 'resize') {
                w.w = Math.max(80, Math.round((initialW + dx) / grid) * grid);
                w.h = Math.max(60, Math.round((initialH + dy) / grid) * grid);
            }
        };
        const stopInteraction = () => { activeIndex.value = null; interactionMode.value = null; };

        const duplicateWidget = (idx) => {
            const src = widgets.value[idx];
            if (!src) return;
            const copy = JSON.parse(JSON.stringify(src));
            copy.id = Date.now() + Math.floor(Math.random() * 10000);
            copy.x += 20; copy.y += 20;
            widgets.value.push(copy);
            selectedIndex.value = widgets.value.length - 1;
        };
        const alignSelected = (mode) => {
            const w = widgets.value[selectedIndex.value];
            if (!w) return;
            if (mode === 'left') w.x = Math.round(w.x / 20) * 20;
            if (mode === 'top') w.y = Math.round(w.y / 20) * 20;
        };
        const removeWidget = (idx) => { widgets.value.splice(idx, 1); selectedIndex.value = null; };

        const saveLayoutLocal = () => {
            try {
                localStorage.setItem(HMI_LAYOUT_KEY, JSON.stringify(widgets.value));
            } catch (e) {
                console.warn('Layout save failed', e);
            }
        };
        const scheduleLayoutSave = () => {
            if (!layoutHydrated) return;
            if (layoutSaveTimer) clearTimeout(layoutSaveTimer);
            layoutSaveTimer = setTimeout(() => {
                layoutSaveTimer = null;
                saveLayoutLocal();
            }, 250);
        };
        const loadLayoutLocal = () => {
            try {
                const saved = localStorage.getItem(HMI_LAYOUT_KEY);
                if (!saved) return false;
                const parsed = JSON.parse(saved);
                if (!Array.isArray(parsed)) return false;
                widgets.value = parsed;
                selectedIndex.value = null;
                return true;
            } catch (e) {
                console.warn('Layout load failed', e);
                return false;
            }
        };
        const seedDefaultLayout = () => {
            widgets.value = [];

            // Release demo layout:
            // - Four HMI toggle command bits drive Q0..Q3 through the default AngelScript program.
            // - Four output LEDs display Q0..Q3.
            // - Four chase LEDs display Q4..Q7 from the default light-chase program.
            // Physical inputs I0..I3 remain read-only hardware inputs, so the HMI uses writable user tags
            // named HMI_I0..HMI_I3 and labels them as demo input switches.
            const toggleTags = ['HMI_I0', 'HMI_I1', 'HMI_I2', 'HMI_I3'];
            const outputTags = ['Q0', 'Q1', 'Q2', 'Q3'];
            const chaseTags = ['Q4', 'Q5', 'Q6', 'Q7'];

            toggleTags.forEach((tag, i) => {
                addWidget('toggle', {
                    x: 80 + i * 190,
                    y: 90,
                    w: 160,
                    h: 110,
                    props: {
                        label: `Switch I${i}`,
                        pin: tag,
                        color: '#38bdf8',
                        active: false,
                        writable: true
                    }
                });
            });

            outputTags.forEach((tag, i) => {
                addWidget('led', {
                    x: 80 + i * 190,
                    y: 240,
                    w: 160,
                    h: 110,
                    props: {
                        label: `${tag} Output`,
                        pin: tag,
                        color: '#22c55e',
                        active: false
                    }
                });
            });

            chaseTags.forEach((tag, i) => {
                addWidget('led', {
                    x: 80 + i * 190,
                    y: 390,
                    w: 160,
                    h: 110,
                    props: {
                        label: `${tag} Chase`,
                        pin: tag,
                        color: '#f59e0b',
                        active: false
                    }
                });
            });

            selectedIndex.value = null;
        };
        const downloadJSON = () => {
            const blob = new Blob([JSON.stringify(widgets.value, null, 2)], { type: 'application/json' });
            const a = document.createElement('a');
            a.href = URL.createObjectURL(blob); a.download = 'hmi_blueprint.json'; a.click();
        };
        const importJSON = async (e) => {
            const file = e.target.files?.[0];
            if (!file) return;
            try { widgets.value = JSON.parse(await file.text()); selectedIndex.value = null; } catch (err) { alert('Invalid HMI JSON'); }
            e.target.value = '';
        };

        watch(widgets, scheduleLayoutSave, { deep: true });

        onMounted(() => {
            ensureTagStoreLoaded().catch(() => {});
            const loaded = loadLayoutLocal();
            if (!loaded) seedDefaultLayout();
            selectedIndex.value = null;
            layoutHydrated = true;
            releasePlcData = plcStore.usePlcData();
            pollPlcData();
        });
        onBeforeUnmount(() => {
            if (layoutSaveTimer) {
                clearTimeout(layoutSaveTimer);
                layoutSaveTimer = null;
            }
            saveLayoutLocal();
            if (releasePlcData) releasePlcData();
        });
</script>
