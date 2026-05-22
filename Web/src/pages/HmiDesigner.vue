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
            <button v-if="editMode" @click="clearLayout" class="hmi-text-link danger">New / Clear</button>
            <button @click="saveLayoutLocal" class="hmi-text-link">Save</button>
            <button @click="loadLayoutLocal" class="hmi-text-link">Load</button>
            <button @click="downloadJSON" class="hmi-text-link">Export</button>
            <label class="hmi-text-link cursor-pointer">
                Import
                <input type="file" class="hidden" accept="application/json" @change="importJSON">
            </label>
        </div>
    </header>

    <div class="hmi-pagebar" aria-label="HMI pages">
        <div class="hmi-page-tabs">
            <button v-for="page in pageNames"
                    :key="page"
                    type="button"
                    :class="['hmi-page-tab', currentPage === page ? 'active' : '']"
                    @click="selectPage(page)">
                <span>{{ page }}</span>
                <span class="hmi-page-count">{{ pageWidgetCount(page) }}</span>
            </button>
        </div>
        <div v-if="editMode" class="hmi-page-actions">
            <button type="button" class="hmi-text-link" @click="openPageDialog('add')">+ Page</button>
            <button type="button" class="hmi-text-link muted" :disabled="currentPage === 'Main'" @click="openPageDialog('rename')">Rename</button>
            <button type="button" class="hmi-text-link danger" :disabled="currentPage === 'Main'" @click="deleteCurrentPage">Delete Page</button>
        </div>
    </div>



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
                        <div class="flex justify-between items-center gap-2 mb-1">
                            <span class="hmi-widget-label">{{ w.props.label }}</span>
                            <span class="hmi-widget-value" :style="{ color: displayColor(w) }">{{ formatValue(w) }}{{ w.props.unit }}</span>
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
                        <span class="hmi-widget-label mt-1">{{ w.props.label }}</span>
                    </div>

                    <div v-if="w.type === 'readout'" class="w-full h-full flex flex-col items-center justify-center bg-black/60 rounded border border-slate-800 shadow-inner">
                        <span class="hmi-widget-label mb-1">{{ w.props.label }}</span>
                        <div class="font-lcd leading-none text-center truncate w-full px-2" :style="{ color: displayColor(w), fontSize: (w.h * 0.4) + 'px' }">
                            {{ formatValue(w) }}
                        </div>
                    </div>

                    <div v-if="w.type === 'toggle'" class="flex flex-col items-center justify-center gap-2">
                        <button @click.stop="toggleCommand(w)"
                                class="w-12 h-6 rounded-full p-1 transition-colors duration-300"
                                :style="{ backgroundColor: w.props.active ? displayColor(w) : '#1e293b' }">
                            <div class="w-4 h-4 bg-white rounded-full transition-transform duration-300"
                                 :style="{ transform: w.props.active ? 'translateX(24px)' : 'translateX(0)' }"></div>
                        </button>
                        <span class="hmi-widget-label">{{ w.props.label }}</span>
                        <div class="hmi-widget-tagline hmi-widget-label justify-center min-h-[12px]" :class="{ idle: !w.props.pendingWrite && !w.props.writeError }">
                            <span v-if="w.props.pendingWrite" class="text-amber-300">LOCAL</span>
                            <span v-if="w.props.writeError" class="text-red-300">WRITE N/A</span>
                            <span v-if="!w.props.pendingWrite && !w.props.writeError" aria-hidden="true">&nbsp;</span>
                        </div>
                    </div>


                    <div v-if="w.type === 'button'" class="hmi-button-widget w-full h-full flex flex-col items-center justify-center gap-2">
                        <button type="button"
                                class="hmi-command-button"
                                :class="{ active: w.props.active, pending: w.props.pendingWrite, error: w.props.writeError }"
                                :style="{ '--hmi-button-color': displayColor(w) }"
                                @pointerdown.stop.prevent="buttonPointerDown(w, $event)"
                                @pointerup.stop.prevent="buttonPointerUp(w, $event)"
                                @pointercancel.stop.prevent="buttonPointerCancel(w)"
                                @pointerleave.stop.prevent="buttonPointerLeave(w)"
                                @click.stop.prevent="buttonClick(w)">
                            <span class="hmi-command-button-label hmi-widget-label">{{ w.props.label }}</span>
                            <span class="hmi-widget-label hmi-widget-meta">{{ buttonModeLabel(w) }}</span>
                        </button>
                        <div class="hmi-widget-tagline hmi-widget-label justify-center" :class="{ idle: !w.props.pendingWrite && !w.props.writeError }">
                            <span v-if="w.props.pendingWrite" class="text-amber-300">LOCAL</span>
                            <span v-if="w.props.writeError" class="text-red-300">WRITE N/A</span>
                            <span v-if="!w.props.pendingWrite && !w.props.writeError" aria-hidden="true">&nbsp;</span>
                        </div>
                    </div>

                    <div v-if="w.type === 'setpoint'" class="hmi-setpoint-widget w-full h-full flex flex-col justify-center gap-2 bg-black/50 rounded border border-slate-800 shadow-inner p-3">
                        <div class="flex items-center justify-between gap-2">
                            <span class="hmi-widget-label truncate">{{ w.props.label }}</span>
                            <span class="hmi-widget-label hmi-widget-meta truncate">{{ w.props.pin || 'NO TAG' }}</span>
                        </div>
                        <div class="hmi-setpoint-main">
                            <button type="button" class="hmi-setpoint-step" @click.stop="stepSetpoint(w, -1)">−</button>
                            <input :value="setpointInputValue(w)"
                                   class="hmi-setpoint-input"
                                   :type="w.props.valueType === 'number' ? 'number' : 'text'"
                                   :step="w.props.step || 1"
                                   :min="w.props.setMin"
                                   :max="w.props.setMax"
                                   :readonly="editMode"
                                   @focus.stop="beginSetpointEdit(w)"
                                   @input.stop="updateSetpointDraft(w, $event.target.value)"
                                   @keydown.enter.prevent.stop="commitSetpoint(w)"
                                   @keydown.esc.prevent.stop="cancelSetpointEdit(w)"
                                   @click.stop>
                            <button type="button" class="hmi-setpoint-step" @click.stop="stepSetpoint(w, 1)">+</button>
                        </div>
                        <button type="button" class="hmi-setpoint-write" :disabled="editMode || w.props.pendingWrite" @click.stop="commitSetpoint(w)">
                            {{ w.props.pendingWrite ? 'Local…' : 'Write Setpoint' }}
                        </button>
                        <div class="hmi-widget-tagline hmi-widget-label">
                            <span>PV {{ formatValue(w) }}{{ w.props.unit }}</span>
                            <span v-if="w.props.writeError" class="text-red-300">WRITE N/A</span>
                        </div>
                    </div>

                    <div v-if="w.type === 'table'" class="hmi-table-widget w-full h-full flex flex-col bg-black/50 rounded border border-slate-800 shadow-inner overflow-hidden">
                        <div class="hmi-table-title hmi-widget-label">{{ w.props.label }}</div>
                        <div class="hmi-table-head">
                            <span>Tag</span>
                            <span>Value</span>
                        </div>
                        <div class="hmi-table-body">
                            <div v-for="tag in tableRows(w)" :key="tag" class="hmi-table-row" :class="tableTagStatus(tag)">
                                <span class="hmi-table-tag" :title="tag">{{ tag }}</span>
                                <span class="hmi-table-value" :title="tableTagValue(tag)">{{ tableTagValue(tag) }}</span>
                            </div>
                            <div v-if="tableRows(w).length === 0" class="hmi-table-empty">
                                Add tags in the Inspector
                            </div>
                        </div>
                    </div>

                    <div v-if="w.type === 'trend'" class="hmi-trend-widget w-full h-full">
                        <div class="hmi-trend-header">
                            <span class="hmi-widget-label">{{ w.props.label }}</span>
                            <span class="hmi-widget-value" :style="{ color: displayColor(w) }">{{ formatValue(w) }}{{ w.props.unit }}</span>
                        </div>
                        <div class="hmi-trend-plot">
                            <div class="hmi-trend-scale">
                                <span>{{ trendScaleMax(w) }}</span>
                                <span>{{ trendScaleMid(w) }}</span>
                                <span>{{ trendScaleMin(w) }}</span>
                            </div>
                            <svg class="hmi-trend-svg" viewBox="0 0 100 100" preserveAspectRatio="none" aria-hidden="true">
                                <line x1="0" y1="25" x2="100" y2="25" class="hmi-trend-grid-line" />
                                <line x1="0" y1="50" x2="100" y2="50" class="hmi-trend-grid-line major" />
                                <line x1="0" y1="75" x2="100" y2="75" class="hmi-trend-grid-line" />
                                <polyline class="hmi-trend-fill" :points="trendFillPoints(w)" :style="{ fill: displayColor(w) }" />
                                <polyline class="hmi-trend-line" :points="trendLinePoints(w)" :style="{ stroke: displayColor(w) }" />
                            </svg>
                        </div>
                    </div>

                    <div v-if="w.type === 'thermometer'" class="h-full flex items-center gap-2 w-full px-4">
                        <div class="h-full w-4 bg-slate-800 rounded-full border border-slate-700 p-0.5 relative overflow-hidden">
                            <div class="absolute bottom-0 left-0 w-full rounded-full transition-all"
                                 :style="{ height: percentValue(w) + '%', backgroundColor: displayColor(w) }"></div>
                        </div>
                        <div class="flex flex-col">
                            <span class="text-xs font-lcd text-white">{{ formatValue(w) }}{{ w.props.unit }}</span>
                            <span class="hmi-widget-label">{{ w.props.label }}</span>
                        </div>
                    </div>

                    <div v-if="w.type === 'led'" class="flex flex-col items-center gap-2">
                        <div class="rounded-full border-4 border-slate-800"
                             :style="{ height: (w.h * 0.5) + 'px', width: (w.h * 0.5) + 'px', backgroundColor: w.props.active ? displayColor(w) : '#0f172a', boxShadow: w.props.active ? `0 0 20px ${displayColor(w)}` : 'none' }"></div>
                        <span class="hmi-widget-label">{{ w.props.label }}</span>
                    </div>

                    <div v-if="w.props.stale" class="absolute right-2 bottom-2 text-[8px] font-black text-amber-300 bg-amber-950/80 border border-amber-800 rounded px-1">STALE</div>
                    <div v-if="w.props.alarm" class="absolute left-2 bottom-2 text-[8px] font-black text-red-300 bg-red-950/80 border border-red-800 rounded px-1">ALARM</div>
                </div>

                <button v-if="editMode && !w.props.locked"
                        type="button"
                        class="widget-delete"
                        title="Delete component"
                        aria-label="Delete component"
                        @click.prevent.stop="removeWidget(widgetIndex(w))">×</button>
                <div v-if="editMode && !w.props.locked" @pointerdown.prevent.stop="startResize($event, widgetIndex(w))" class="resizer"></div>
            </div>
        </main>

        <aside v-if="editMode" :class="['hmi-inspector', inspectorCollapsed ? 'collapsed' : '']">
            <button v-if="inspectorCollapsed"
                    type="button"
                    class="hmi-inspector-tab"
                    title="Open Inspector"
                    aria-label="Open Inspector"
                    @click="setInspectorCollapsed(false)">
                <span>Inspector</span>
                <span aria-hidden="true">‹</span>
            </button>

            <template v-else>
            <div class="p-4 border-b border-slate-800 flex justify-between items-center bg-slate-900/50 gap-3">
                <h2 class="text-[10px] font-black uppercase text-sky-500 tracking-widest">Inspector</h2>
                <div class="flex items-center gap-2">
                    <span v-if="selectedIndex !== null" class="text-[8px] px-2 py-0.5 bg-sky-900 text-sky-200 rounded-full font-bold">#{{ widgets[selectedIndex].id.toString().slice(-4) }}</span>
                    <button type="button"
                            class="hmi-inspector-collapse"
                            title="Collapse Inspector"
                            aria-label="Collapse Inspector"
                            @click="setInspectorCollapsed(true)">›</button>
                </div>
            </div>

            <div v-if="selectedIndex !== null" class="flex-1 min-h-0 overflow-y-auto p-4 space-y-5">
                <section class="space-y-3">
                    <h3 class="text-[9px] font-bold text-slate-500 uppercase border-b border-slate-800 pb-1">Page</h3>
                    <div>
                        <label class="text-[8px] uppercase text-slate-500">HMI Page</label>
                        <select v-model="widgets[selectedIndex].props.page"
                                class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]">
                            <option v-for="page in pageNames" :key="page" :value="page">{{ page }}</option>
                        </select>
                    </div>
                    <button v-if="widgets[selectedIndex].props.page !== currentPage"
                            type="button"
                            @click="widgets[selectedIndex].props.page = currentPage"
                            class="w-full py-2 bg-sky-950/40 text-sky-300 text-[9px] font-black border border-sky-900/50 rounded hover:bg-sky-900 uppercase">
                        Move to Current Page
                    </button>
                </section>

                <section v-if="widgets[selectedIndex].type !== 'table'" class="space-y-3">
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

                <section v-if="widgets[selectedIndex].type === 'button'" class="space-y-3">
                    <h3 class="text-[9px] font-bold text-slate-500 uppercase border-b border-slate-800 pb-1">Button Action</h3>
                    <div>
                        <label class="text-[8px] uppercase text-slate-500">Button Mode</label>
                        <select v-model="widgets[selectedIndex].props.buttonMode" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]">
                            <option value="momentary">Momentary</option>
                            <option value="toggle">Toggle</option>
                            <option value="pulse">Pulse</option>
                            <option value="write">Write Value</option>
                        </select>
                    </div>
                    <div class="grid grid-cols-2 gap-2">
                        <div>
                            <label class="text-[8px] uppercase text-slate-500">Pressed / On / Write</label>
                            <input v-model="widgets[selectedIndex].props.pressValue" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px] font-mono">
                        </div>
                        <div>
                            <label class="text-[8px] uppercase text-slate-500">Released / Off</label>
                            <input v-model="widgets[selectedIndex].props.releaseValue" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px] font-mono">
                        </div>
                    </div>
                    <div class="grid grid-cols-2 gap-2">
                        <div>
                            <label class="text-[8px] uppercase text-slate-500">Value Type</label>
                            <select v-model="widgets[selectedIndex].props.valueType" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]">
                                <option value="auto">Auto</option>
                                <option value="bool">Boolean</option>
                                <option value="number">Number</option>
                                <option value="string">String</option>
                            </select>
                        </div>
                        <div>
                            <label class="text-[8px] uppercase text-slate-500">Pulse ms</label>
                            <input type="number" min="20" max="5000" v-model.number="widgets[selectedIndex].props.pulseMs" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]">
                        </div>
                    </div>
                </section>

                <section v-if="widgets[selectedIndex].type === 'setpoint'" class="space-y-3">
                    <h3 class="text-[9px] font-bold text-slate-500 uppercase border-b border-slate-800 pb-1">Setpoint Entry</h3>
                    <div class="grid grid-cols-2 gap-2">
                        <div>
                            <label class="text-[8px] uppercase text-slate-500">Value Type</label>
                            <select v-model="widgets[selectedIndex].props.valueType" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]">
                                <option value="number">Number</option>
                                <option value="string">String</option>
                                <option value="bool">Boolean</option>
                            </select>
                        </div>
                        <div>
                            <label class="text-[8px] uppercase text-slate-500">Step</label>
                            <input type="number" v-model.number="widgets[selectedIndex].props.step" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]">
                        </div>
                        <div>
                            <label class="text-[8px] uppercase text-slate-500">Set Min</label>
                            <input type="number" v-model.number="widgets[selectedIndex].props.setMin" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]">
                        </div>
                        <div>
                            <label class="text-[8px] uppercase text-slate-500">Set Max</label>
                            <input type="number" v-model.number="widgets[selectedIndex].props.setMax" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]">
                        </div>
                    </div>
                </section>

                <section v-if="widgets[selectedIndex].type === 'table'" class="space-y-3">
                    <h3 class="text-[9px] font-bold text-slate-500 uppercase border-b border-slate-800 pb-1">Watch Tags</h3>
                    <div class="flex gap-2">
                        <input v-model="tagPickerFilter"
                               class="flex-1 bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px] font-mono"
                               placeholder="Filter/add tag...">
                        <button type="button"
                                @click="tableAddTag(widgets[selectedIndex], tagPickerFilter)"
                                class="px-2 bg-sky-950/40 text-sky-300 text-[9px] font-black border border-sky-900/50 rounded hover:bg-sky-900 uppercase">Add</button>
                    </div>
                    <div class="max-h-32 overflow-y-auto border border-slate-800 rounded bg-slate-950/80">
                        <button v-for="tag in filteredTagNames" :key="tag" type="button"
                                @click="tableAddTag(widgets[selectedIndex], tag)"
                                class="w-full flex items-center justify-between gap-2 px-2 py-1 text-left text-[10px] font-mono hover:bg-sky-900/40 border-b border-slate-900 last:border-b-0 text-slate-300">
                            <span>{{ tag }}</span>
                            <span class="text-[9px] text-slate-500 truncate">{{ tagValueText(tag) }}</span>
                        </button>
                        <div v-if="filteredTagNames.length === 0" class="px-2 py-2 text-[10px] text-slate-600 italic">No matching tags yet. Type a tag name and click Add.</div>
                    </div>
                    <div class="space-y-1">
                        <div v-for="(tag, i) in tableRows(widgets[selectedIndex])" :key="`${tag}-${i}`" class="flex items-center gap-2">
                            <input :value="tag"
                                   @input="tableSetTag(widgets[selectedIndex], i, $event.target.value)"
                                   class="flex-1 bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px] font-mono">
                            <button type="button"
                                    @click="tableRemoveTag(widgets[selectedIndex], i)"
                                    class="px-2 py-1 bg-red-950/40 text-red-400 text-[9px] font-black border border-red-900/50 rounded hover:bg-red-900 uppercase">×</button>
                        </div>
                        <div v-if="tableRows(widgets[selectedIndex]).length === 0" class="text-[10px] text-slate-600 italic border border-dashed border-slate-800 rounded p-2 text-center">No watch tags yet.</div>
                    </div>
                </section>

                <section v-if="widgets[selectedIndex].type !== 'table'" class="space-y-3">
                    <h3 class="text-[9px] font-bold text-slate-500 uppercase border-b border-slate-800 pb-1">Scaling</h3>
                    <div class="grid grid-cols-2 gap-2">
                        <div><label class="text-[8px] uppercase text-slate-500">Raw Min</label><input type="number" v-model.number="widgets[selectedIndex].props.rawMin" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]"></div>
                        <div><label class="text-[8px] uppercase text-slate-500">Raw Max</label><input type="number" v-model.number="widgets[selectedIndex].props.rawMax" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]"></div>
                        <div><label class="text-[8px] uppercase text-slate-500">Display Min</label><input type="number" v-model.number="widgets[selectedIndex].props.min" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]"></div>
                        <div><label class="text-[8px] uppercase text-slate-500">Display Max</label><input type="number" v-model.number="widgets[selectedIndex].props.max" class="w-full bg-slate-950 border border-slate-800 p-1.5 rounded text-[10px]"></div>
                    </div>
                </section>

                <section v-if="widgets[selectedIndex].type !== 'table'" class="space-y-3">
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
            </template>
        </aside>
    </div>
  </section>
</template>

<script setup>
import { ref, computed, nextTick, onMounted, onBeforeUnmount, watch } from 'vue';
import { usePlcStore } from '../stores/plcStore';
import { ensureTagStoreLoaded, getTagStoreNames, getTagStorePointMap } from '../stores/tagStore';
import { confirmDialog, messageDialog, promptDialog } from '../stores/appDialog';


        const plcStore = usePlcStore();
        let releasePlcData = null;

        const HMI_MODE_KEY = 'pilab_hmi_edit_mode_v1';
        const HMI_LAYOUT_KEY = 'pilab_hmi_layout_v2';
        const HMI_PAGE_KEY = 'pilab_hmi_current_page_v1';
        const HMI_PAGES_KEY = 'pilab_hmi_pages_v1';
        const HMI_INSPECTOR_KEY = 'pilab_hmi_inspector_collapsed_v1';
        const loadInitialEditMode = () => {
            try {
                const saved = localStorage.getItem(HMI_MODE_KEY);
                if (saved === 'live') return false;
                if (saved === 'build') return true;
            } catch {}
            return true;
        };

        const normalizePageName = (name) => {
            const cleaned = String(name || '').trim().replace(/\s+/g, ' ');
            return cleaned || 'Main';
        };
        const uniquePageList = (pages = []) => {
            const out = ['Main'];
            pages.map(normalizePageName).forEach(page => {
                if (page && !out.includes(page)) out.push(page);
            });
            return out;
        };
        const loadInitialPages = () => {
            try {
                const parsed = JSON.parse(localStorage.getItem(HMI_PAGES_KEY) || '[]');
                if (Array.isArray(parsed)) return uniquePageList(parsed);
            } catch {}
            return ['Main'];
        };

        const editMode = ref(loadInitialEditMode());
        const loadInitialInspectorCollapsed = () => {
            try { return localStorage.getItem(HMI_INSPECTOR_KEY) === '1'; } catch {}
            return false;
        };
        const inspectorCollapsed = ref(loadInitialInspectorCollapsed());
        const setInspectorCollapsed = (collapsed) => {
            inspectorCollapsed.value = !!collapsed;
            try { localStorage.setItem(HMI_INSPECTOR_KEY, inspectorCollapsed.value ? '1' : '0'); } catch {}
        };
        const canvas = ref(null);
        const widgets = ref([]);
        const hmiPages = ref(loadInitialPages());
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
        const loadInitialPage = () => {
            try {
                const saved = localStorage.getItem(HMI_PAGE_KEY);
                return normalizePageName(saved) || 'Main';
            } catch {}
            return 'Main';
        };
        const currentPage = ref(loadInitialPage());

        const controlTypes = ['gauge', 'readout', 'tank', 'led', 'trend', 'thermometer', 'toggle', 'button', 'setpoint', 'table'];
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
        watch(inspectorCollapsed, (collapsed) => {
            try { localStorage.setItem(HMI_INSPECTOR_KEY, collapsed ? '1' : '0'); } catch {}
        });
        const persistPages = () => {
            try { localStorage.setItem(HMI_PAGES_KEY, JSON.stringify(uniquePageList(hmiPages.value))); } catch {}
        };
        watch(hmiPages, persistPages, { deep: true });
        watch(currentPage, (page) => {
            const normalized = normalizePageName(page);
            if (!hmiPages.value.includes(normalized)) hmiPages.value = uniquePageList([...hmiPages.value, normalized]);
            try { localStorage.setItem(HMI_PAGE_KEY, normalized); } catch {}
        });

        const baseProps = (p) => ({
            label: p.label || 'TAG', color: p.color || '#38bdf8', pin: p.pin || '', min: p.min ?? 0, max: p.max ?? 100,
            rawMin: p.rawMin ?? 0, rawMax: p.rawMax ?? 100, value: p.value ?? 0, unit: p.unit || '', decimals: p.decimals ?? 1,
            active: p.active ?? false, alarmLow: p.alarmLow ?? null, alarmHigh: p.alarmHigh ?? null, staleMs: p.staleMs ?? 2500,
            stale: false, alarm: false, locked: false, writable: p.writable ?? false, page: p.page || 'Main',
            pendingWrite: false, writeError: false, localOverrideUntil: 0,
            buttonMode: p.buttonMode || 'momentary', pressValue: p.pressValue ?? 'true', releaseValue: p.releaseValue ?? 'false',
            pulseMs: p.pulseMs ?? 150, valueType: p.valueType || 'auto', editValue: p.editValue ?? '', editing: false,
            step: p.step ?? 1, setMin: p.setMin ?? null, setMax: p.setMax ?? null,
            tableTags: Array.isArray(p.tableTags) ? [...p.tableTags] : []
        });

        const normalizeWidgetPage = (w) => {
            if (!w || typeof w !== 'object') return w;
            if (!w.props || typeof w.props !== 'object') w.props = {};
            w.props.page = normalizePageName(w.props.page);
            return w;
        };

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
            toggle:      baseProps({ label: 'HMI_I0', color: '#38bdf8', pin: 'HMI_I0', active: false, writable: true }),
            button:      baseProps({ label: 'START', color: '#38bdf8', pin: 'HMI_I0', active: false, writable: true, buttonMode: 'momentary', pressValue: 'true', releaseValue: 'false', valueType: 'auto', pulseMs: 150 }),
            setpoint:    baseProps({ label: 'Setpoint', color: '#22c55e', pin: 'TankCommand', min: 0, max: 100, rawMin: 0, rawMax: 100, value: 0, unit: '', decimals: 1, writable: true, valueType: 'number', editValue: '0', step: 1, setMin: 0, setMax: 100 }),
            table:       baseProps({ label: 'Tag Watch', color: '#38bdf8', pin: '', tableTags: ['HMI_I0', 'Q0', 'TankCommand', 'TankLevel'], decimals: 3 })
        };

        const pageNames = computed(() => {
            const names = new Set(uniquePageList([...hmiPages.value, normalizePageName(currentPage.value)]));
            widgets.value.forEach(w => names.add(normalizePageName(w.props?.page)));
            return [...names].sort((a, b) => {
                if (a === 'Main') return -1;
                if (b === 'Main') return 1;
                return a.localeCompare(b);
            });
        });
        const pageWidgetCount = (page) => widgets.value.filter(w => normalizePageName(w.props?.page) === page).length;
        const visibleWidgets = computed(() => widgets.value.filter(w => normalizePageName(w.props?.page) === currentPage.value));
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

        const ensurePage = (page) => {
            const normalized = normalizePageName(page);
            if (!hmiPages.value.includes(normalized)) hmiPages.value = uniquePageList([...hmiPages.value, normalized]);
            return normalized;
        };
        const selectPage = (page) => {
            currentPage.value = ensurePage(page);
            selectedIndex.value = null;
            stopInteraction();
        };
        const nextPageName = () => {
            let i = pageNames.value.length;
            let name = `Page ${i}`;
            while (pageNames.value.includes(name)) {
                i += 1;
                name = `Page ${i}`;
            }
            return name;
        };
        const validatePageName = (name, mode = 'add', originalName = '') => {
            const normalized = normalizePageName(name);
            if (!normalized) return 'Enter a page name.';
            if (mode === 'rename' && normalized === originalName) return '';
            if (normalized === 'Main') return 'Main is the protected default page.';
            if (pageNames.value.includes(normalized)) return `A page named "${normalized}" already exists.`;
            return '';
        };
        const openPageDialog = async (mode) => {
            const isRename = mode === 'rename';
            if (isRename && currentPage.value === 'Main') return;
            if (isRename) {
                const from = currentPage.value;
                const name = await promptDialog({
                    kicker: 'HMI Page',
                    title: 'Rename Page',
                    label: 'Page Name',
                    initialValue: from,
                    confirmText: 'Rename Page',
                    help: 'Existing widgets on this page will move with the renamed page.',
                    validator: (value) => validatePageName(value, 'rename', from)
                });
                if (name === null) return;
                const normalized = normalizePageName(name);
                if (normalized && normalized !== from) {
                    widgets.value.forEach(w => {
                        if (normalizePageName(w.props?.page) === from) w.props.page = normalized;
                    });
                    hmiPages.value = uniquePageList(hmiPages.value.map(page => normalizePageName(page) === from ? normalized : page));
                    currentPage.value = normalized;
                    selectedIndex.value = null;
                }
                return;
            }

            const name = await promptDialog({
                kicker: 'HMI Page',
                title: 'New Page',
                label: 'Page Name',
                initialValue: nextPageName(),
                confirmText: 'Create Page',
                help: 'The new page is added to the local HMI layout.',
                validator: (value) => validatePageName(value, 'add')
            });
            if (name === null) return;
            const normalized = normalizePageName(name);
            hmiPages.value = uniquePageList([...hmiPages.value, normalized]);
            currentPage.value = normalized;
            selectedIndex.value = null;
        };
        const deleteCurrentPage = async () => {
            const page = normalizePageName(currentPage.value);
            if (!page || page === 'Main') return;
            const count = pageWidgetCount(page);
            const ok = await confirmDialog({
                kicker: 'HMI Page',
                title: 'Delete Page',
                message: page,
                detail: count > 0
                    ? `This will remove ${count} component${count === 1 ? '' : 's'} on this page.`
                    : 'This empty page will be removed from the local HMI layout.',
                help: 'This only changes the local HMI layout. PLC runtime values are not changed.',
                confirmText: 'Delete Page',
                tone: 'danger'
            });
            if (!ok) return;
            widgets.value = widgets.value.filter(w => normalizePageName(w.props?.page) !== page);
            hmiPages.value = uniquePageList(hmiPages.value.filter(name => normalizePageName(name) !== page));
            currentPage.value = 'Main';
            selectedIndex.value = null;
            stopInteraction();
        };

        const widgetIndex = (w) => widgets.value.findIndex(x => x.id === w.id);
        const selectTag = (tag) => {
            const w = widgets.value[selectedIndex.value];
            if (!w) return;
            w.props.pin = tag;
            if (!w.props.label || w.props.label === 'TAG') w.props.label = tag;
            if (['toggle', 'button', 'setpoint'].includes(w.type)) {
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

        const tableRows = (w) => Array.isArray(w?.props?.tableTags)
            ? w.props.tableTags.map(t => String(t || '').trim()).filter(Boolean)
            : [];
        const normalizeTableTags = (tags) => {
            const out = [];
            (Array.isArray(tags) ? tags : []).forEach(tag => {
                const t = String(tag || '').trim();
                if (t && !out.includes(t)) out.push(t);
            });
            return out;
        };
        const tableAddTag = (w, tag) => {
            if (!w) return;
            const t = String(tag || '').trim();
            if (!t) return;
            w.props.tableTags = normalizeTableTags([...(w.props.tableTags || []), t]);
            tagPickerFilter.value = '';
        };
        const tableSetTag = (w, index, tag) => {
            if (!w || !Array.isArray(w.props.tableTags)) return;
            const next = [...w.props.tableTags];
            next[index] = String(tag || '').trim();
            w.props.tableTags = normalizeTableTags(next);
        };
        const tableRemoveTag = (w, index) => {
            if (!w || !Array.isArray(w.props.tableTags)) return;
            w.props.tableTags = w.props.tableTags.filter((_, i) => i !== index);
        };
        const formatTagValue = (value) => {
            if (value === undefined || value === null) return '—';
            if (typeof value === 'boolean') return value ? 'TRUE' : 'FALSE';
            if (typeof value === 'number') return Number.isFinite(value) ? String(Number(value.toFixed(3))) : '—';
            return String(value);
        };
        const tableTagValue = (tag) => {
            if (tag in plcTags.value) return formatTagValue(plcTags.value[tag]);
            const p = plcPoints.value[tag] || getTagStorePointMap({ includeSystem: true })[tag];
            return formatTagValue(p?.value);
        };
        const tableTagStatus = (tag) => {
            if (!tag) return 'missing';
            const last = tagLastSeen.value[tag];
            if (tag in plcTags.value && last && (Date.now() - last) <= 2500) return 'online';
            const p = plcPoints.value[tag] || getTagStorePointMap({ includeSystem: true })[tag];
            return p ? 'stale' : 'missing';
        };

        const nextWidgetPosition = (type) => {
            const pageCount = widgets.value.filter(w => normalizePageName(w.props?.page) === currentPage.value).length;
            const scrollLeft = canvas.value?.scrollLeft ?? 0;
            const scrollTop = canvas.value?.scrollTop ?? 0;
            const width = type === 'trend' ? 380 : (type === 'table' ? 320 : (type === 'button' ? 180 : (type === 'setpoint' ? 240 : 225)));
            const height = type === 'trend' ? 150 : (type === 'table' ? 220 : (type === 'button' ? 120 : (type === 'setpoint' ? 150 : 150)));
            return {
                x: scrollLeft + 80 + (pageCount % 3) * 40,
                y: scrollTop + 80 + (pageCount % 3) * 40,
                w: width,
                h: height
            };
        };
        const addWidget = (type, overrides = {}) => {
            const pos = nextWidgetPosition(type);
            const w = {
                id: Date.now() + Math.floor(Math.random() * 10000), type,
                x: overrides.x ?? pos.x,
                y: overrides.y ?? pos.y,
                w: overrides.w ?? pos.w,
                h: overrides.h ?? pos.h,
                props: { ...JSON.parse(JSON.stringify(defaults[type])), ...(overrides.props || {}), page: normalizePageName(overrides.props?.page || currentPage.value) },
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

        const trendLinePoints = (w) => {
            const history = Array.isArray(w.history) && w.history.length ? w.history : [percentValue(w)];
            const denom = Math.max(1, history.length - 1);
            return history.map((v, i) => {
                const x = (i / denom) * 100;
                const y = 100 - clamp(Number(v) || 0, 0, 100);
                return `${x.toFixed(2)},${y.toFixed(2)}`;
            }).join(' ');
        };
        const trendFillPoints = (w) => {
            const line = trendLinePoints(w);
            if (!line) return '';
            return `0,100 ${line} 100,100`;
        };
        const formatScaleValue = (v, decimals = 1) => {
            const n = Number(v);
            if (!Number.isFinite(n)) return '—';
            const d = clamp(Number(decimals ?? 1), 0, 6);
            return n.toFixed(d);
        };
        const trendScaleMax = (w) => formatScaleValue(w.props.max ?? 100, w.props.decimals);
        const trendScaleMin = (w) => formatScaleValue(w.props.min ?? 0, w.props.decimals);
        const trendScaleMid = (w) => {
            const min = Number(w.props.min ?? 0);
            const max = Number(w.props.max ?? 100);
            return formatScaleValue((min + max) / 2, w.props.decimals);
        };

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
                if (['toggle', 'button', 'setpoint'].includes(w.type) && w.props.pin && isWritableTag(w.props.pin)) {
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
                if (w.type === 'table') {
                    w.props.stale = false;
                    w.props.alarm = false;
                    return;
                }
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
                } else if (w.type === 'button') {
                    const nowMs = Date.now();
                    const serverValue = raw;
                    w.props.value = typeof serverValue === 'boolean' ? (serverValue ? 1 : 0) : serverValue;
                    if (w.props.pendingWrite && nowMs > Number(w.props.localOverrideUntil || 0)) {
                        w.props.pendingWrite = false;
                    }
                    if (w.props.buttonMode === 'toggle' && !w.props.pendingWrite) {
                        w.props.active = valuesEquivalent(serverValue, coerceHmiValue(w.props.pressValue, w.props.valueType));
                    }
                } else if (w.type === 'setpoint') {
                    const val = rawToDisplay(w, raw);
                    w.props.value = val;
                    if (!w.props.editing && !w.props.pendingWrite) w.props.editValue = formatValue(w);
                    if (w.props.pendingWrite && Date.now() > Number(w.props.localOverrideUntil || 0)) w.props.pendingWrite = false;
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


        const coerceHmiValue = (value, valueType = 'auto') => {
            const s = String(value ?? '').trim();
            const type = valueType || 'auto';
            if (type === 'bool' || (type === 'auto' && /^(true|false)$/i.test(s))) return /^true$/i.test(s);
            if (type === 'number' || (type === 'auto' && s !== '' && Number.isFinite(Number(s)))) return Number(s);
            if (type === 'string') return String(value ?? '');
            return s;
        };
        const valuesEquivalent = (a, b) => {
            if (typeof a === 'boolean' || typeof b === 'boolean') return !!a === !!b;
            if (Number.isFinite(Number(a)) && Number.isFinite(Number(b))) return Number(a) === Number(b);
            return String(a) === String(b);
        };
        const writeWidgetValue = async (w, value, label = 'Widget') => {
            if (!w?.props?.pin) return false;
            w.props.pendingWrite = true;
            w.props.writeError = false;
            // Match the older toggle feel: show a brief local/pending acknowledgement,
            // then let the next PLC poll/readback own the displayed state again.
            w.props.localOverrideUntil = Date.now() + 1200;
            if (!w.props.writable) {
                w.props.pendingWrite = false;
                w.props.writeError = true;
                w.props.localOverrideUntil = Date.now() + 2500;
                console.warn(`${label} is not writable:`, w.props.pin);
                return false;
            }
            try {
                await plcStore.plcWrite(w.props.pin, value);
                w.props.pendingWrite = false;
                w.props.writeError = false;
                w.props.localOverrideUntil = Date.now() + 350;
                return true;
            } catch (e) {
                w.props.pendingWrite = false;
                w.props.writeError = true;
                w.props.localOverrideUntil = Date.now() + 5000;
                console.warn(`${label} write failed`, e);
                return false;
            }
        };
        const buttonModeLabel = (w) => ({ momentary: 'Hold', toggle: 'Toggle', pulse: `${Number(w.props.pulseMs || 150)} ms`, write: 'Write' }[w.props.buttonMode] || 'Button');
        const buttonWrite = async (w, value, active) => {
            w.props.active = !!active;
            const coerced = coerceHmiValue(value, w.props.valueType);
            w.props.value = typeof coerced === 'boolean' ? (coerced ? 1 : 0) : coerced;
            return writeWidgetValue(w, coerced, 'Button');
        };
        const buttonPointerDown = (w, e) => {
            if (editMode.value || w.props.locked) return;
            if (e?.currentTarget?.setPointerCapture && e.pointerId !== undefined) {
                try { e.currentTarget.setPointerCapture(e.pointerId); } catch {}
            }
            const mode = w.props.buttonMode || 'momentary';
            if (mode === 'momentary') buttonWrite(w, w.props.pressValue, true);
            if (mode === 'pulse') {
                buttonWrite(w, w.props.pressValue, true);
                const ms = clamp(Number(w.props.pulseMs || 150), 20, 5000);
                setTimeout(() => buttonWrite(w, w.props.releaseValue, false), ms);
            }
        };
        const buttonPointerUp = (w) => {
            if (editMode.value || w.props.locked) return;
            if ((w.props.buttonMode || 'momentary') === 'momentary') buttonWrite(w, w.props.releaseValue, false);
        };
        const buttonPointerCancel = (w) => {
            if ((w.props.buttonMode || 'momentary') === 'momentary') buttonWrite(w, w.props.releaseValue, false);
        };
        const buttonPointerLeave = (w) => {
            if ((w.props.buttonMode || 'momentary') === 'momentary' && w.props.active) buttonWrite(w, w.props.releaseValue, false);
        };
        const buttonClick = (w) => {
            if (editMode.value || w.props.locked) return;
            const mode = w.props.buttonMode || 'momentary';
            if (mode === 'toggle') {
                const nextActive = !w.props.active;
                buttonWrite(w, nextActive ? w.props.pressValue : w.props.releaseValue, nextActive);
            } else if (mode === 'write') {
                buttonWrite(w, w.props.pressValue, true);
                setTimeout(() => { w.props.active = false; }, 140);
            }
        };
        const releaseActiveMomentaryButtons = () => {
            widgets.value.forEach(w => {
                if (w.type === 'button' && (w.props.buttonMode || 'momentary') === 'momentary' && w.props.active) {
                    buttonWrite(w, w.props.releaseValue, false);
                }
            });
        };

        const setpointInputValue = (w) => w.props.editing ? w.props.editValue : (w.props.editValue ?? formatValue(w));
        const beginSetpointEdit = (w) => {
            if (editMode.value) return;
            w.props.editing = true;
            if (w.props.editValue === '' || w.props.editValue === null || w.props.editValue === undefined) w.props.editValue = formatValue(w);
        };
        const updateSetpointDraft = (w, value) => {
            w.props.editing = true;
            w.props.editValue = value;
        };
        const cancelSetpointEdit = (w) => {
            w.props.editing = false;
            w.props.editValue = formatValue(w);
        };
        const normalizeSetpointValue = (w, value) => {
            const type = w.props.valueType || 'number';
            if (type === 'bool') return /^(true|1|on|yes)$/i.test(String(value).trim());
            if (type === 'string') return String(value ?? '');
            let n = Number(value);
            if (!Number.isFinite(n)) n = Number(w.props.value || 0);
            if (w.props.setMin !== null && w.props.setMin !== '' && Number.isFinite(Number(w.props.setMin))) n = Math.max(Number(w.props.setMin), n);
            if (w.props.setMax !== null && w.props.setMax !== '' && Number.isFinite(Number(w.props.setMax))) n = Math.min(Number(w.props.setMax), n);
            return n;
        };
        const commitSetpoint = async (w) => {
            if (editMode.value) return;
            const value = normalizeSetpointValue(w, w.props.editValue);
            w.props.editValue = String(value);
            w.props.editing = false;
            w.props.value = typeof value === 'boolean' ? (value ? 1 : 0) : value;
            await writeWidgetValue(w, value, 'Setpoint');
        };
        const stepSetpoint = (w, direction) => {
            if (editMode.value) return;
            const step = Number(w.props.step || 1);
            const current = Number(w.props.editValue !== '' ? w.props.editValue : w.props.value || 0);
            const next = Number.isFinite(current) ? current + (direction * step) : direction * step;
            w.props.editing = true;
            w.props.editValue = String(normalizeSetpointValue(w, next));
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
        const removeWidget = (idx) => {
            if (idx === null || idx === undefined || idx < 0 || idx >= widgets.value.length) return;
            widgets.value.splice(idx, 1);
            selectedIndex.value = null;
        };
        const clearLayout = async () => {
            if (widgets.value.length > 0) {
                const ok = await confirmDialog({
                    title: 'Clear HMI Layout',
                    message: 'Clear all HMI components and start with an empty screen?',
                    detail: `${widgets.value.length} component${widgets.value.length === 1 ? '' : 's'} will be removed from the local HMI layout.`,
                    confirmText: 'Clear Layout',
                    tone: 'danger'
                });
                if (!ok) return;
            }
            stopInteraction();
            widgets.value = [];
            hmiPages.value = ['Main'];
            currentPage.value = 'Main';
            selectedIndex.value = null;
        };

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
                widgets.value = parsed.map(normalizeWidgetPage);
                hmiPages.value = uniquePageList([...hmiPages.value, ...widgets.value.map(w => w.props?.page)]);
                if (!pageNames.value.includes(currentPage.value)) currentPage.value = 'Main';
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
            try {
                const parsed = JSON.parse(await file.text());
                if (!Array.isArray(parsed)) throw new Error('Expected array');
                widgets.value = parsed.map(normalizeWidgetPage);
                hmiPages.value = uniquePageList(['Main', ...widgets.value.map(w => w.props?.page)]);
                currentPage.value = 'Main';
                selectedIndex.value = null;
            } catch (err) {
                await messageDialog({ title: 'Invalid HMI JSON', message: err?.message || 'The selected file could not be imported.', tone: 'danger' });
            }
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
            window.addEventListener('blur', releaseActiveMomentaryButtons);
        });
        onBeforeUnmount(() => {
            if (layoutSaveTimer) {
                clearTimeout(layoutSaveTimer);
                layoutSaveTimer = null;
            }
            saveLayoutLocal();
            if (releasePlcData) releasePlcData();
            window.removeEventListener('blur', releaseActiveMomentaryButtons);
        });
</script>
