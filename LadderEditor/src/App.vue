<template>
<div id="app" class="h-screen p-3 grid grid-rows-[76px_1fr] gap-3">
  <header class="glass rounded-2xl px-4 py-3 flex items-center justify-between gap-3 overflow-hidden">
    <div class="flex items-center gap-3 min-w-0">
      <div class="w-11 h-11 rounded-xl border border-cyan-300/40 bg-cyan-400/10 text-cyan-300 flex items-center justify-center font-black">P</div>
      <div class="min-w-0">
        <h1 class="text-2xl font-bold truncate">PiLab Ladder Logic Editor <span class="text-sm text-cyan-300 align-middle">v0.1.11-reset-visible</span></h1>
        <p class="text-sm text-slate-400 truncate">Correct branch model: branches connect wire-node to wire-node, not contact-center to contact-center.</p>
      </div>
    </div>
    <div class="flex flex-wrap justify-end gap-2 shrink-0">
      <button @click="addRung" class="btn px-4 py-2 rounded-xl border border-cyan-300/40 bg-cyan-500/15 text-cyan-100 font-semibold">+ Ladder Rung</button>
      <button @click="addScriptRung" class="btn px-4 py-2 rounded-xl border border-purple-300/40 bg-purple-500/15 text-purple-100 font-semibold">+ AngelScript Rung</button>
      <button @click="undo" :disabled="!history.length" class="btn px-4 py-2 rounded-xl border border-slate-500/60 bg-slate-700/50 font-semibold disabled:opacity-40 disabled:cursor-not-allowed">Undo</button>
      <button @click="saveLocal" class="btn px-4 py-2 rounded-xl border border-emerald-300/35 bg-emerald-500/15 text-emerald-100 font-semibold">Save</button>
      <button @click="loadLocal" class="btn px-4 py-2 rounded-xl border border-blue-300/35 bg-blue-500/15 text-blue-100 font-semibold">Load</button>
      <button @click="$refs.jsonImport.click()" class="btn px-4 py-2 rounded-xl border border-indigo-300/35 bg-indigo-500/15 text-indigo-100 font-semibold">Import JSON</button>
      <input ref="jsonImport" type="file" accept=".json,application/json" class="hidden" @change="importJsonFile"/>
      <button @click="downloadJson" class="btn px-4 py-2 rounded-xl border border-slate-500/60 bg-slate-700/50 font-semibold">Export JSON</button>
      <button @click="downloadAs" class="btn px-4 py-2 rounded-xl border border-amber-300/35 bg-amber-500/15 text-amber-100 font-semibold">Export AngelScript</button>
    </div>
  </header>

  <main class="grid grid-cols-[270px_minmax(620px,1fr)_380px] gap-3 min-h-0">

    <aside class="glass rounded-2xl p-4 min-h-0 overflow-auto scrollbar">
      <h2 class="text-xs font-bold tracking-widest text-slate-400 uppercase mb-3">Palette</h2>
      <div class="space-y-2">
        <button v-for="t in tools" :key="t.type"
          draggable="true"
          @dragstart="dragTool(t.type)"
          @click="selectedTool=t.type; mode='place'"
          class="w-full text-left rounded-xl p-3 border btn"
          :class="selectedTool===t.type && mode==='place' ? 'bg-cyan-500/15 border-cyan-300/50' : 'bg-slate-950/45 border-slate-700 hover:border-slate-500'">
          <div class="flex items-center gap-3">
            <div class="w-12 h-10 rounded-lg border border-slate-600 flex items-center justify-center" v-html="t.icon"></div>
            <div class="flex-1 min-w-0">
              <div class="font-semibold truncate">{{t.label}}</div>
              <div class="text-xs text-slate-400">{{t.desc}}</div>
            </div>
            <div class="mono text-xs text-slate-500">{{t.type}}</div>
          </div>
        </button>
      </div>

      <div class="mt-4 panel rounded-xl p-3">
        <h3 class="text-xs font-bold tracking-widest text-slate-400 uppercase mb-2">Tools</h3>
        <div class="space-y-2">
          <button @click="mode='select'; branchStart=null" class="w-full px-3 py-2 rounded-lg border text-left btn" :class="mode==='select'?'bg-cyan-500/15 border-cyan-300/50':'bg-slate-900 border-slate-700'">↖ Select / Move</button>
          <button @click="mode='branch'; branchStart=null; show('Branch Tool: click start wire node, then end wire node')" class="w-full px-3 py-2 rounded-lg border text-left btn" :class="mode==='branch'?'bg-blue-500/20 border-blue-300/60':'bg-slate-900 border-slate-700'">⎇ Draw Branch</button>
          <button @click="mode='delete'" class="w-full px-3 py-2 rounded-lg border text-left btn" :class="mode==='delete'?'bg-red-500/20 border-red-300/60':'bg-slate-900 border-slate-700'">⌫ Delete Element</button>
          <button v-if="selectedBranch" @click="deleteBranch(selectedBranch.rung, selectedBranch.branchId)" class="w-full px-3 py-2 rounded-lg border text-left btn bg-red-500/15 border-red-300/50 text-red-100">✕ Delete Selected Branch</button>
        </div>
      </div>

      <div class="mt-4 panel rounded-xl p-3">
        <h3 class="font-semibold mb-2">How to use</h3>
        <ol class="text-xs text-slate-400 space-y-1 list-decimal ml-4">
          <li>Select or drag a symbol into a faint slot.</li>
          <li>Select <b>Draw Branch</b>.</li>
          <li>Click a wire node where the branch starts.</li>
          <li>Click another wire node where the branch ends.</li>
          <li>Click any branch wire or branch contact to select/delete that branch.</li>
        </ol>
      </div>
    </aside>

    <section class="glass rounded-2xl min-h-0 overflow-hidden grid grid-rows-[54px_1fr_34px]">
      <div class="px-4 py-3 border-b border-slate-800 flex items-center justify-between gap-3">
        <div class="min-w-0">
          <h2 class="font-bold leading-tight">Ladder Canvas</h2>
          <p class="text-xs text-slate-400 truncate">
            {{ mode==='branch'
              ? (branchStart ? 'Click the end wire node. Main and branch wire nodes are valid.' : 'Click the start wire node. Main and branch wire nodes are valid.')
              : 'Branches attach to wire nodes between instructions.' }}
          </p>
        </div>
        <div class="flex items-center gap-2 shrink-0">
          <span class="text-xs text-slate-400">Mode:</span>
          <span class="mono text-xs px-2 py-1 rounded border border-cyan-300/30 bg-cyan-500/10 text-cyan-200">{{ mode }}</span>
          <button @click="sampleProject" class="btn px-3 py-1.5 rounded-lg bg-slate-800 border border-slate-600 text-xs">Load Sample</button>
          <button @click="clearAll" class="btn px-3 py-1.5 rounded-lg bg-red-500/10 border border-red-300/30 text-red-200 text-xs">Clear All</button>
        </div>
      </div>

      <div class="canvas-bg overflow-y-auto overflow-x-hidden scrollbar min-h-0">
        <div class="p-4">
          <div v-for="(rung, rIndex) in project.rungs" :key="rung.id" class="mb-4">
            <div class="flex items-center gap-2 mb-2 flex-wrap">
              <div class="mono text-sm text-slate-300 flex items-center gap-2 min-w-[360px]">
                <span class="shrink-0">Rung {{rIndex+1}} <span v-if="rung.kind==='script'" class="text-purple-300">[AS]</span> —</span>
                <input v-if="editingCommentId===rung.id"
                  v-model="rung.comment"
                  @focus="captureEditSnapshot"
                  @change="commitEditSnapshot('Comment changed')"
                  @keydown.enter.prevent="editingCommentId=null"
                  @keydown.esc.prevent="editingCommentId=null"
                  @blur="editingCommentId=null"
                  class="flex-1 min-w-[220px] px-2 py-1 rounded bg-slate-950 border border-cyan-300/45 text-slate-100 outline-none focus:border-cyan-300"
                  placeholder="Rung comment"
                  autofocus />
                <span v-else class="truncate">{{rung.comment || 'No comment'}}</span>
              </div>
              <button @click="startEditComment(rung)" class="text-xs px-2 py-1 rounded bg-slate-800 border border-slate-700">{{editingCommentId===rung.id ? 'Done' : 'Comment'}}</button>
              <button v-if="rung.kind!=='script'" @click="mode='branch'; branchStart=null; show('Click start wire node')" class="text-xs px-2 py-1 rounded bg-blue-500/10 border border-blue-300/40 text-blue-200">Branch Tool</button>
              <button @click="insertRungBefore(rIndex)" title="Insert a new ladder rung before this rung" class="text-xs px-2 py-1 rounded bg-cyan-500/10 border border-cyan-300/35 text-cyan-200">+ Before</button>
              <button @click="insertRungAfter(rIndex)" title="Insert a new ladder rung after this rung" class="text-xs px-2 py-1 rounded bg-cyan-500/10 border border-cyan-300/35 text-cyan-200">+ After</button>
              <button @click="insertScriptRungAfter(rIndex)" title="Insert a new AngelScript rung after this rung" class="text-xs px-2 py-1 rounded bg-purple-500/10 border border-purple-300/35 text-purple-200">+ AS After</button>
              <button @click="duplicateRung(rIndex)" class="text-xs px-2 py-1 rounded bg-emerald-500/10 border border-emerald-300/35 text-emerald-200">Duplicate</button>
              <button @click="moveRung(rIndex,-1)" :disabled="rIndex===0" title="Move rung up one position" class="text-xs px-2 py-1 rounded bg-slate-800 border border-slate-700 disabled:opacity-35">↑</button>
              <button @click="moveRung(rIndex,1)" :disabled="rIndex===project.rungs.length-1" title="Move rung down one position" class="text-xs px-2 py-1 rounded bg-slate-800 border border-slate-700 disabled:opacity-35">↓</button>
              <button @click="removeRung(rIndex)" class="text-xs px-2 py-1 rounded bg-red-500/10 border border-red-300/40 text-red-200">Delete Rung</button>
            </div>

            <div v-if="rung.kind==='script'" class="rounded-xl border border-purple-300/25 bg-slate-950/70 p-3">
              <div class="flex items-center justify-between mb-2">
                <div class="text-xs text-purple-200 font-semibold">AngelScript rung body</div>
                <div class="text-xs text-slate-500">Inserted verbatim inside <span class="mono">scan()</span></div>
              </div>
              <textarea v-model="rung.code"
                @focus="captureEditSnapshot"
                @change="commitEditSnapshot('AngelScript rung changed')"
                spellcheck="false"
                class="mono w-full min-h-[150px] p-3 rounded-lg bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-purple-300 resize-y scrollbar"
                placeholder="// Example:
// Q0 = I0 && !I1;
"></textarea>
            </div>

            <svg v-else :viewBox="'0 0 '+canvasW+' '+rungHeight(rung)" preserveAspectRatio="xMinYMin meet"
                 class="w-full rounded-xl border border-slate-800 bg-slate-950/65"
                 :style="{height: rungHeight(rung) + 'px'}"
                 @mouseenter="hoverRungId=rung.id" @mouseleave="hoverRungId=null">

              <!-- left and right power rails -->
              <line :x1="railLeft" y1="24" :x2="railLeft" :y2="rungHeight(rung)-24" stroke="#e2e8f0" stroke-width="3"/>
              <line :x1="railRight" y1="24" :x2="railRight" :y2="rungHeight(rung)-24" stroke="#e2e8f0" stroke-width="3"/>

              <!-- rail connection stubs -->
              <line :x1="railLeft" :y1="mainY" :x2="nodeX(0)" :y2="mainY" :stroke="simNodeOn(rung,0) ? '#34d399' : '#94a3b8'" stroke-width="3" :opacity="simNodeOn(rung,0) ? 1 : .88"/>
              <line :x1="nodeX(8)" :y1="mainY" :x2="railRight" :y2="mainY" :stroke="simNodeOn(rung,8) ? '#34d399' : '#94a3b8'" stroke-width="3" :opacity="simNodeOn(rung,8) ? 1 : .88"/>

              <!-- main wire segments. These are between wire nodes. -->
              <g>
                <line v-for="seg in mainSegments" :key="'seg'+seg"
                  :x1="nodeX(seg)" :y1="mainY" :x2="nodeX(seg+1)" :y2="mainY"
                   :stroke="simMainSegmentOn(rung,seg) ? '#34d399' : '#94a3b8'" stroke-width="3" :opacity="simMainSegmentOn(rung,seg) ? 1 : .88"/>
              </g>

              <!-- rail junction dots -->
              <circle :cx="railLeft" :cy="mainY" r="4" fill="#cbd5e1"/>
              <circle :cx="railRight" :cy="mainY" r="4" fill="#cbd5e1"/>

              <!-- branch creation wire nodes on the main wire -->
              <g>
                <circle v-for="n in nodes" :key="'node'+n"
                  :cx="nodeX(n)" :cy="mainY" :r="mode==='branch' ? 7 : 4"
                  :fill="nodeFill(rung,n)" :stroke="mode==='branch' ? '#67e8f9' : '#cbd5e1'"
                  :stroke-width="mode==='branch' ? 2.2 : 1.2"
                  @click.stop="wireNodeClick(rung,n)"
                  class="cursor-pointer"/>
              </g>

              <!-- main instruction slots -->
              <g v-for="slot in slots" :key="'mainSlot'+slot">
                <rect :x="slotCenterX(slot)-30" :y="mainY-28" width="60" height="56" rx="10"
                  :fill="simElementFill(rung,'main',null,slot)"
                  :stroke="simElementStroke(rung,'main',null,slot)"
                  :opacity="rung.main[slot] ? 1 : (hoverRungId===rung.id && mode!=='branch' ? .32 : .045)"
                  stroke-width="1.5"
                  @dragover.prevent
                  @drop="dropOnMain(rung,slot)"
                  @click.stop="slotClick(rung,'main',null,slot)"
                  class="cursor-pointer"/>
                <text v-if="!rung.main[slot]" :x="slotCenterX(slot)" :y="mainY+4" fill="#64748b" font-size="14" text-anchor="middle"
                  :opacity="hoverRungId===rung.id && mode!=='branch' ? .75 : .06">+</text>
                <g v-if="rung.main[slot]" @click.stop="symbolClick(rung,'main',null,slot)">
                  <SymbolRender :el="rung.main[slot]" :x="slotCenterX(slot)" :y="mainY" :sim="simBlockFor(rung.main[slot])"></SymbolRender>
                </g>
              </g>

              <!-- branches -->
              <g v-for="(br, bi) in rung.branches" :key="br.id">
                <!-- vertical drops -->
                <line :x1="nodeX(br.start)" :y1="mainY" :x2="nodeX(br.start)" :y2="branchY(rung,br)" :stroke="simBranchOn(rung,br) ? '#34d399' : '#94a3b8'" stroke-width="3" :opacity="simBranchOn(rung,br) ? 1 : .88"/>
                <line :x1="nodeX(br.end)" :y1="mainY" :x2="nodeX(br.end)" :y2="branchY(rung,br)" :stroke="simBranchOn(rung,br) ? '#34d399' : '#94a3b8'" stroke-width="3" :opacity="simBranchOn(rung,br) ? 1 : .88"/>

                <!-- optional branch rail stubs when branch starts/ends at outer wire nodes -->
                <line v-if="br.start===0" :x1="railLeft" :y1="branchY(rung,br)" :x2="nodeX(0)" :y2="branchY(rung,br)" :stroke="simBranchOn(rung,br) ? '#34d399' : '#94a3b8'" stroke-width="3" :opacity="simBranchOn(rung,br) ? 1 : .88"/>
                <line v-if="br.end===8" :x1="nodeX(8)" :y1="branchY(rung,br)" :x2="railRight" :y2="branchY(rung,br)" :stroke="simBranchOn(rung,br) ? '#34d399' : '#94a3b8'" stroke-width="3" :opacity="simBranchOn(rung,br) ? 1 : .88"/>

                <!-- branch wire only between start and end nodes -->
                <line :x1="nodeX(br.start)" :y1="branchY(rung,br)" :x2="nodeX(br.end)" :y2="branchY(rung,br)"
                  :stroke="selectedBranch && selectedBranch.branchId===br.id ? '#67e8f9' : (simBranchOn(rung,br) ? '#34d399' : '#94a3b8')"
                  stroke-width="3" :opacity="simBranchOn(rung,br) ? 1 : .92"/>

                <!-- fat transparent hit wire for branch selection/deletion -->
                <line :x1="nodeX(br.start)" :y1="branchY(rung,br)" :x2="nodeX(br.end)" :y2="branchY(rung,br)"
                  stroke="transparent" stroke-width="20"
                  @click.stop="branchClick(rung, br.id)"
                  class="cursor-pointer"/>

                <!-- junction dots -->
                <circle :cx="nodeX(br.start)" :cy="mainY" r="5" fill="#cbd5e1"/>
                <circle :cx="nodeX(br.end)" :cy="mainY" r="5" fill="#cbd5e1"/>
                <circle :cx="nodeX(br.start)" :cy="branchY(rung,br)" r="4" fill="#cbd5e1"/>
                <circle :cx="nodeX(br.end)" :cy="branchY(rung,br)" r="4" fill="#cbd5e1"/>

                <!-- branch creation wire nodes on existing branch wires -->
                <g v-if="mode==='branch'">
                  <circle v-for="n in branchNodes(br)" :key="br.id+'branchNode'+n"
                    :cx="nodeX(n)" :cy="branchY(rung,br)" r="7"
                    :fill="nodeFill(rung,n)" stroke="#67e8f9" stroke-width="2.2"
                    @click.stop="wireNodeClick(rung,n)"
                    class="cursor-pointer"/>
                </g>

                <!-- branch delete button visible near branch start -->
                <rect :x="nodeX(br.start)-38" :y="branchY(rung,br)-13" width="26" height="26" rx="7"
                  fill="rgba(239,68,68,.20)" stroke="rgba(252,165,165,.75)"
                  @click.stop="deleteBranch(rung,br.id)" class="cursor-pointer"/>
                <text :x="nodeX(br.start)-25" :y="branchY(rung,br)+6" text-anchor="middle" fill="#fecaca" font-size="17" font-weight="900" pointer-events="none">×</text>

                <!-- branch instruction slots: positions wrapped by start/end wire nodes -->
                <g v-for="slot in branchSlots(br)" :key="br.id+'slot'+slot">
                  <rect :x="slotCenterX(slot)-30" :y="branchY(rung,br)-28" width="60" height="56" rx="10"
                    :fill="simElementFill(rung,'branch',br,slot)"
                    :stroke="simElementStroke(rung,'branch',br,slot)"
                    :opacity="br.cells[slot] ? 1 : (hoverRungId===rung.id && mode!=='branch' ? .32 : .045)"
                    stroke-width="1.5"
                    @dragover.prevent
                    @drop="dropOnBranch(rung,br,slot)"
                    @click.stop="slotClick(rung,'branch',br,slot)"
                    class="cursor-pointer"/>
                  <text v-if="!br.cells[slot]" :x="slotCenterX(slot)" :y="branchY(rung,br)+4" fill="#64748b" font-size="14" text-anchor="middle"
                    :opacity="hoverRungId===rung.id && mode!=='branch' ? .75 : .06">+</text>
                  <g v-if="br.cells[slot]" @click.stop="symbolClick(rung,'branch',br,slot)">
                    <SymbolRender :el="br.cells[slot]" :x="slotCenterX(slot)" :y="branchY(rung,br)" :sim="simBlockFor(br.cells[slot])"></SymbolRender>
                  </g>
                </g>
              </g>
            </svg>
          </div>

          <div class="grid grid-cols-2 gap-3">
            <button @click="addRung" class="w-full py-4 rounded-xl border border-dashed border-blue-400/40 bg-blue-500/5 text-blue-200 hover:bg-blue-500/10 btn">
              + Add Ladder Rung
            </button>
            <button @click="addScriptRung" class="w-full py-4 rounded-xl border border-dashed border-purple-400/40 bg-purple-500/5 text-purple-200 hover:bg-purple-500/10 btn">
              + Add AngelScript Rung
            </button>
          </div>
        </div>
      </div>

      <div class="px-4 py-2 border-t border-slate-800 flex gap-6 text-xs overflow-hidden">
        <span><b class="text-emerald-300">Scan:</b> {{project.scan_ms}} ms</span>
        <span><b>Rungs:</b> {{project.rungs.length}}</span>
        <span><b>Branches:</b> {{branchCount()}}</span>
        <span><b>Symbols:</b> {{allElements().length}}</span>
        <span class="truncate"><b>Project:</b> <span class="text-cyan-300">{{project.name}}</span></span>
      </div>
    </section>

    <aside class="flex flex-col gap-3 min-h-0 overflow-hidden">
      <section class="glass rounded-2xl overflow-hidden shrink-0" :class="panelOpen('inspector') ? 'min-h-[160px] max-h-[42vh] grid grid-rows-[auto_1fr]' : ''">
        <button @click="togglePanel('inspector')" class="w-full px-4 py-2 border-b border-slate-800/70 flex justify-between items-center gap-2 text-left btn">
          <h2 class="text-xs font-bold tracking-widest text-slate-400 uppercase">Inspector</h2>
          <span class="mono text-xs text-slate-500">{{panelOpen('inspector') ? '−' : '+'}}</span>
        </button>

        <div v-show="panelOpen('inspector')" class="p-4 overflow-auto scrollbar min-h-0">
          <div v-if="selected" class="space-y-3">
            <div class="grid grid-cols-[92px_1fr] gap-2 items-center">
              <label class="text-xs text-slate-400">Type</label>
              <div class="mono text-cyan-300 text-xs px-2 py-1 rounded bg-slate-950 border border-slate-700">{{selected.el.type}}</div>

              <label class="text-xs text-slate-400">Tag</label>
              <input v-model="selected.el.tag" class="w-full min-w-0 px-2 py-1 rounded bg-slate-950 border border-slate-700 mono text-sm outline-none focus:border-cyan-400"/>

              <label v-if="hasPreset(selected.el)" class="text-xs text-slate-400">Preset</label>
              <input v-if="hasPreset(selected.el)" v-model.number="selected.el.preset" type="number" class="w-full min-w-0 px-2 py-1 rounded bg-slate-950 border border-slate-700 mono text-sm outline-none focus:border-cyan-400"/>

              <label v-if="isCounter(selected.el)" class="text-xs text-slate-400">{{selected.el.type==='CTD' ? 'Load Expr' : 'Reset Expr'}}</label>
              <input v-if="isCounter(selected.el)"
                v-model="selected.el.resetTag"
                @focus="captureEditSnapshot"
                @input="onCounterControlExprEdited(selected.el)"
                @change="commitEditSnapshot(selected.el.type==='CTD' ? 'Counter load expression changed' : 'Counter reset expression changed')"
                :placeholder="selected.el.type==='CTD' ? 'empty — type LoadPB here' : 'empty — type ResetPB here'"
                class="w-full min-w-0 px-2 py-1 rounded bg-slate-950 border border-slate-700 mono text-sm outline-none focus:border-cyan-400"/>

              <div v-if="isCounter(selected.el)" class="col-span-2 text-[11px] text-slate-500 leading-relaxed">
                <div>
                  Current {{selected.el.type==='CTD' ? 'load' : 'reset'}} expression:
                  <span class="mono" :class="counterControlExpr(selected.el).trim() ? 'text-cyan-200' : 'text-amber-300'">
                    {{ counterControlExpr(selected.el).trim() || '<empty>' }}
                  </span>
                </div>
                <div class="mt-1">
                  CTU resets to zero when this expression is true. CTD reloads to preset when this expression is true. Tags used here appear in the Simulator controls automatically.
                </div>
              </div>
            </div>

            <div class="pt-2 border-t border-slate-800/80 text-[11px] text-slate-500 leading-relaxed">
              Future symbol options can be added here without pushing into the output panels. This inspector area scrolls independently.
            </div>
          </div>

          <div v-else-if="selectedBranch" class="space-y-3">
            <div class="text-sm text-cyan-200">Branch selected.</div>
            <div class="text-xs text-slate-400">Use Delete Selected Branch or the branch × button on the canvas.</div>
          </div>

          <p v-else class="text-sm text-slate-500">Select a symbol or branch.</p>
        </div>
      </section>

      <section class="glass rounded-2xl overflow-hidden shrink-0" :class="panelOpen('validation') ? 'min-h-[120px] max-h-[34vh] grid grid-rows-[auto_1fr]' : ''">
        <button @click="togglePanel('validation')" class="w-full px-4 py-2 border-b border-slate-800 flex justify-between items-center gap-2 text-left btn">
          <h2 class="font-bold">Validation</h2>
          <div class="flex items-center gap-2 text-xs shrink-0">
            <span class="px-2 py-1 rounded border" :class="validationSummary.errors ? 'bg-red-500/15 border-red-300/40 text-red-200' : 'bg-emerald-500/10 border-emerald-300/35 text-emerald-200'">{{validationSummary.errors}} errors</span>
            <span class="px-2 py-1 rounded border" :class="validationSummary.warnings ? 'bg-amber-500/15 border-amber-300/40 text-amber-200' : 'bg-slate-800 border-slate-700 text-slate-400'">{{validationSummary.warnings}} warnings</span>
            <span class="mono text-xs text-slate-500">{{panelOpen('validation') ? '−' : '+'}}</span>
          </div>
        </button>
        <div v-show="panelOpen('validation')" class="overflow-auto scrollbar p-3 bg-slate-950/55">
          <div v-if="validationIssues.length===0" class="h-full min-h-[90px] flex items-center justify-center text-center text-sm text-emerald-200">
            No validation issues found.
          </div>
          <div v-else class="space-y-2">
            <div v-for="issue in validationIssues" :key="issue.id" class="rounded-xl border p-3" :class="issueClass(issue.level)">
              <div class="flex items-start justify-between gap-2">
                <div class="min-w-0">
                  <div class="text-sm font-semibold leading-tight">{{issue.title}}</div>
                  <div class="text-xs text-slate-400 mt-1">{{issue.where}}</div>
                </div>
                <span class="mono text-[10px] px-2 py-0.5 rounded border shrink-0" :class="issueBadgeClass(issue.level)">{{issue.level}}</span>
              </div>
              <div class="text-xs text-slate-300 mt-2 leading-relaxed">{{issue.message}}</div>
            </div>
          </div>
        </div>
      </section>

      <section class="glass rounded-2xl overflow-hidden" :class="panelOpen('simulator') ? 'flex-1 min-h-[260px] grid grid-rows-[auto_1fr]' : 'shrink-0'">
        <div class="px-4 py-2 border-b border-slate-800 flex justify-between items-center gap-2">
          <button @click="togglePanel('simulator')" class="flex items-center gap-2 text-left btn min-w-0">
            <h2 class="font-bold">Simulator</h2>
            <span class="mono text-xs text-slate-500">{{panelOpen('simulator') ? '−' : '+'}}</span>
          </button>
          <div class="flex items-center gap-2 shrink-0">
            <button @click="simStep" class="btn px-2 py-1 rounded-lg bg-slate-800 border border-slate-600 text-xs">Scan</button>
            <button @click="toggleSimRun" class="btn px-2 py-1 rounded-lg border text-xs" :class="simRunning ? 'bg-red-500/15 border-red-300/35 text-red-200' : 'bg-emerald-500/10 border-emerald-300/35 text-emerald-200'">{{simRunning ? 'Stop' : 'Run'}}</button>
            <button @click="simReset" class="btn px-2 py-1 rounded-lg bg-slate-800 border border-slate-600 text-xs">Reset</button>
          </div>
        </div>
        <div v-show="panelOpen('simulator')" class="overflow-auto scrollbar p-3 bg-slate-950/55 space-y-3">
          <div class="flex items-center justify-between text-xs text-slate-400">
            <span><b class="text-emerald-300">Scans:</b> {{simScanCount}}</span>
            <span><b>Rate:</b> {{project.scan_ms || 5}} ms</span>
            <span><b>Tags:</b> {{simTagList.length}}</span>
          </div>
          <div class="grid grid-cols-2 gap-2">
            <button v-for="tag in simTagList" :key="tag" @click="toggleSimTag(tag)"
              class="px-2 py-1.5 rounded-lg border text-left mono text-xs truncate"
              :class="simTagValue(tag) ? 'bg-emerald-500/15 border-emerald-300/45 text-emerald-100' : 'bg-slate-900 border-slate-700 text-slate-400'">
              <span class="inline-block w-2 h-2 rounded-full mr-1" :class="simTagValue(tag) ? 'bg-emerald-300' : 'bg-slate-600'"></span>{{tag}}
            </button>
          </div>
          <div v-if="simBlockList.length" class="pt-2 border-t border-slate-800">
            <div class="text-xs font-bold tracking-widest text-slate-500 uppercase mb-2">Blocks</div>
            <div class="space-y-1">
              <div v-for="b in simBlockList" :key="b.tag" class="flex justify-between gap-2 mono text-xs text-slate-300">
                <span class="truncate">{{b.tag}}</span>
                <span class="text-slate-500 shrink-0">{{b.text}}</span>
              </div>
            </div>
          </div>
          <div class="text-[11px] text-slate-500 leading-relaxed">
            Live mode evaluates the ladder JSON directly in the browser. <span class="text-emerald-300">Green</span> means energized power flow; <span class="text-amber-300">amber</span> means the contact/block condition is true but upstream power is not present. Timers show ET/remaining time; counters show CV. Counter reset/load supports tags plus !, &&, ||, and parentheses. CTU resets to 0; CTD reloads from PV. Script rungs are simulated only for simple boolean assignments such as <span class="mono">Q0 = I0 && !I1;</span>.
          </div>
        </div>
      </section>

      <section class="glass rounded-2xl overflow-hidden" :class="panelOpen('script') ? 'flex-1 min-h-[220px] grid grid-rows-[auto_1fr]' : 'shrink-0'">
        <div class="px-4 py-2 border-b border-slate-800 flex justify-between items-center gap-2">
          <button @click="togglePanel('script')" class="flex items-center gap-2 text-left btn min-w-0">
            <h2 class="font-bold">AngelScript Output</h2>
            <span class="mono text-xs text-slate-500">{{panelOpen('script') ? '−' : '+'}}</span>
          </button>
          <button @click="copy(transpile())" class="btn px-3 py-1 rounded-lg bg-cyan-500/10 border border-cyan-300/35 text-cyan-200 text-xs shrink-0">Copy</button>
        </div>
        <pre v-show="panelOpen('script')" class="mono text-xs text-slate-300 overflow-auto scrollbar p-4 bg-slate-950/70">{{transpile()}}</pre>
      </section>

      <section class="glass rounded-2xl overflow-hidden" :class="panelOpen('json') ? 'flex-1 min-h-[220px] grid grid-rows-[auto_1fr]' : 'shrink-0'">
        <div class="px-4 py-2 border-b border-slate-800 flex justify-between items-center gap-2">
          <button @click="togglePanel('json')" class="flex items-center gap-2 text-left btn min-w-0">
            <h2 class="font-bold">JSON Model</h2>
            <span class="mono text-xs text-slate-500">{{panelOpen('json') ? '−' : '+'}}</span>
          </button>
          <button @click="copy(jsonModel)" class="btn px-3 py-1 rounded-lg bg-slate-800 border border-slate-600 text-xs shrink-0">Copy</button>
        </div>
        <textarea v-show="panelOpen('json')" v-model="jsonDraft" @change="applyJson" class="mono text-xs text-slate-300 overflow-auto scrollbar p-4 bg-slate-950/70 outline-none resize-none"></textarea>
      </section>
    </aside>
  </main>

  <div v-if="toast" class="fixed bottom-4 right-4 glass rounded-xl px-4 py-3 text-cyan-100 border-cyan-300/30">{{toast}}</div>
</div>
</template>

<script>
import SymbolRender from './components/SymbolRender.vue';
import { ladderModelMethods } from './ladder/ladderModel.js';
import { ladderValidationMethods } from './ladder/ladderValidation.js';
import { ladderSimulatorMethods } from './ladder/ladderSimulator.js';
import { ladderTranspilerMethods } from './ladder/ladderTranspiler.js';
import { addFormalSchemaMetadata } from './ladder/ladderSchema.js';

export default {
  components:{SymbolRender},
  data(){
    return {
      canvasW: 820,
      railLeft: 52,
      railRight: 780,
      mainY: 72,
      nodeStartX: 92,
      nodeStep: 82,
      nodes:[0,1,2,3,4,5,6,7,8],
      slots:[0,1,2,3,4,5,6,7],
      mainSegments:[0,1,2,3,4,5,6,7],
      selectedTool:'NO',
      draggedTool:null,
      mode:'select',
      branchStart:null,
      selected:null,
      selectedBranch:null,
      editingCommentId:null,
      hoverRungId:null,
      toast:'',
      jsonDraft:'',
      collapsedPanels:{ inspector:false, validation:false, simulator:false, script:true, json:true },
      history:[],
      editSnapshot:null,
      maxHistory:60,
      simRunning:false,
      simTimer:null,
      simTags:{},
      simBlocks:{},
      simRungs:{},
      simScanCount:0,
      project:{ name:'PiLab Ladder Project', scan_ms:5, rungs:[] },
      tools:[
        {type:'NO', label:'Normally Open', desc:'True when input/tag is true.', icon:'<svg width="48" height="32"><line x1="2" y1="16" x2="14" y2="16" stroke="#e2e8f0" stroke-width="2"/><line x1="34" y1="16" x2="46" y2="16" stroke="#e2e8f0" stroke-width="2"/><line x1="15" y1="4" x2="15" y2="28" stroke="#67e8f9" stroke-width="3"/><line x1="33" y1="4" x2="33" y2="28" stroke="#67e8f9" stroke-width="3"/></svg>'},
        {type:'NC', label:'Normally Closed', desc:'True when input/tag is false.', icon:'<svg width="48" height="32"><line x1="2" y1="16" x2="14" y2="16" stroke="#e2e8f0" stroke-width="2"/><line x1="34" y1="16" x2="46" y2="16" stroke="#e2e8f0" stroke-width="2"/><line x1="15" y1="4" x2="15" y2="28" stroke="#fca5a5" stroke-width="3"/><line x1="33" y1="4" x2="33" y2="28" stroke="#fca5a5" stroke-width="3"/><line x1="9" y1="28" x2="39" y2="4" stroke="#fca5a5" stroke-width="2"/></svg>'},
        {type:'OUT', label:'Output Coil', desc:'Writes rung result.', icon:'<svg width="48" height="32"><path d="M13 4 Q2 16 13 28" fill="none" stroke="#86efac" stroke-width="3"/><path d="M35 4 Q46 16 35 28" fill="none" stroke="#86efac" stroke-width="3"/></svg>'},
        {type:'TON', label:'On Delay Timer', desc:'Timer on delay.', icon:'<svg width="48" height="32"><rect x="7" y="4" width="34" height="24" rx="5" fill="#0f172a" stroke="#fbbf24" stroke-width="2"/><text x="24" y="20" text-anchor="middle" fill="#fde68a" font-size="10" font-weight="700">TON</text></svg>'},
        {type:'TOF', label:'Off Delay Timer', desc:'Timer off delay.', icon:'<svg width="48" height="32"><rect x="7" y="4" width="34" height="24" rx="5" fill="#0f172a" stroke="#fbbf24" stroke-width="2"/><text x="24" y="20" text-anchor="middle" fill="#fde68a" font-size="10" font-weight="700">TOF</text></svg>'},
        {type:'CTU', label:'Up Counter', desc:'Counts up.', icon:'<svg width="48" height="32"><rect x="7" y="4" width="34" height="24" rx="5" fill="#0f172a" stroke="#c084fc" stroke-width="2"/><text x="24" y="20" text-anchor="middle" fill="#e9d5ff" font-size="10" font-weight="700">CTU</text></svg>'},
        {type:'CTD', label:'Down Counter', desc:'Counts down.', icon:'<svg width="48" height="32"><rect x="7" y="4" width="34" height="24" rx="5" fill="#0f172a" stroke="#c084fc" stroke-width="2"/><text x="24" y="20" text-anchor="middle" fill="#e9d5ff" font-size="10" font-weight="700">CTD</text></svg>'}
      ]
    };
  },
  computed:{
    jsonModel(){ return JSON.stringify(this.project,null,2); },
    validationIssues(){ return this.validateProject(); },
    validationSummary(){
      const issues=this.validationIssues;
      return {
        errors: issues.filter(i=>i.level==='error').length,
        warnings: issues.filter(i=>i.level==='warning').length,
        info: issues.filter(i=>i.level==='info').length
      };
    },
    simTagList(){
      const tags = new Set();
      const blockTags = new Set();

      const addTag = (tag) => {
        const clean = this.sanitize(tag);
        if (clean) tags.add(clean);
      };

      const addExpressionTags = (expr) => {
        for (const t of this.expressionTags(String(expr || ''))) addTag(t);
      };

      for (const e of this.allElements()) {
        if (!e) continue;
        const tag = this.sanitize(e.tag || '');

        if (['TON','TOF','CTU','CTD'].includes(e.type)) {
          if (tag) blockTags.add(tag);
          if (this.isCounter(e)) addExpressionTags(e.resetTag || e.resetExpr || e.loadTag || e.loadExpr || '');
        } else if (tag) {
          addTag(tag);
        }
      }

      // Keep any manually created simulator tags visible too.
      for (const k of Object.keys(this.simTags || {})) addTag(k);

      // Block instance tags belong in the Blocks panel, not as manual simulator inputs.
      return [...tags].filter(t => !blockTags.has(t)).sort();
    },
    simBlockList(){
      return Object.keys(this.simBlocks||{}).sort().map(tag=>{
        const b=this.simBlocks[tag];
        if(!b) return {tag,text:''};
        if(b.type==='TON') return {tag,text:`TON Q=${b.output?'1':'0'} ET=${Math.round(b.elapsed_ms||0)}/${b.preset_ms||0}ms`};
        if(b.type==='TOF') return {tag,text:`TOF Q=${b.output?'1':'0'} ET=${Math.round(b.elapsed_ms||0)}/${b.preset_ms||0}ms REM=${Math.round(b.remaining_ms||0)}ms`};
        return {tag,text:`${b.type} Q=${b.output?'1':'0'} CV=${b.count||0}/${b.preset||0}${b.resetExpr?' R='+b.resetExpr:''}`};
      });
    }
  },
  mounted(){ this.sampleProject(); },
  watch:{ project:{ deep:true, handler(){ this.jsonDraft=this.jsonModel; }}},
  methods:{
    ...ladderModelMethods,
    ...ladderValidationMethods,
    ...ladderSimulatorMethods,
    ...ladderTranspilerMethods,
    show(m){ this.toast=m; setTimeout(()=>this.toast='',1500); },
    panelOpen(name){ return !this.collapsedPanels[name]; },
    togglePanel(name){ this.collapsedPanels[name] = !this.collapsedPanels[name]; },
    counterControlExpr(el){
      if(!el) return '';
      return String(el.resetTag ?? el.resetExpr ?? el.loadTag ?? el.loadExpr ?? '');
    },
    setCounterControlExpr(el, value){
      if(!el || !['CTU','CTD'].includes(el.type)) return;
      const text = String(value ?? '');
      el.resetTag = text;
      delete el.resetExpr;
      delete el.loadTag;
      delete el.loadExpr;

      const tag = this.sanitize(el.tag);
      if(this.simBlocks && this.simBlocks[tag]) this.simBlocks[tag].resetExpr = text.trim();
      this.$forceUpdate();
    },
    onCounterControlExprEdited(el){
      this.setCounterControlExpr(el, el ? el.resetTag : '');
    },
    cloneProject(){ return JSON.parse(JSON.stringify(this.project)); },
    pushHistory(label='Edit'){
      if(!this.project) return;
      const snap=JSON.stringify(this.project);
      if(this.history.length && this.history[this.history.length-1].snapshot===snap) return;
      this.history.push({label, snapshot:snap});
      if(this.history.length>this.maxHistory) this.history.shift();
    },
    undo(){
      const item=this.history.pop();
      if(!item) return this.show('Nothing to undo');
      this.project=JSON.parse(item.snapshot);
      this.selected=null; this.selectedBranch=null; this.branchStart=null; this.editingCommentId=null; this.editSnapshot=null;
      this.show('Undid: '+item.label);
    },
    captureEditSnapshot(){ if(!this.editSnapshot) this.editSnapshot=JSON.stringify(this.project); },
    commitEditSnapshot(label='Edit'){
      if(this.editSnapshot && this.editSnapshot!==JSON.stringify(this.project)){
        this.history.push({label, snapshot:this.editSnapshot});
        if(this.history.length>this.maxHistory) this.history.shift();
      }
      this.editSnapshot=null;
    },
    withHistory(label, fn){ this.pushHistory(label); fn(); },
    addRung(){ this.withHistory('Add ladder rung',()=>this.project.rungs.push(this.createRung(''))); },
    addScriptRung(){ this.withHistory('Add AngelScript rung',()=>this.project.rungs.push(this.createScriptRung())); },
    insertRungAt(index, rungFactory, label){
      const safeIndex=Math.max(0, Math.min(index, this.project.rungs.length));
      this.withHistory(label,()=>{
        this.project.rungs.splice(safeIndex,0,rungFactory());
        this.selected=null;
        this.selectedBranch=null;
        this.branchStart=null;
        this.editingCommentId=null;
      });
    },
    insertRungBefore(i){ this.insertRungAt(i, ()=>this.createRung(''), 'Insert ladder rung before'); },
    insertRungAfter(i){ this.insertRungAt(i+1, ()=>this.createRung(''), 'Insert ladder rung after'); },
    insertScriptRungAfter(i){ this.insertRungAt(i+1, ()=>this.createScriptRung(), 'Insert AngelScript rung after'); },
    duplicateRung(i){ this.withHistory('Duplicate rung',()=>{ const copy=this.cloneRungDeep(this.project.rungs[i]); copy.comment=(copy.comment||'Rung')+' copy'; this.project.rungs.splice(i+1,0,copy); }); },
    moveRung(i,dir){ const j=i+dir; if(j<0||j>=this.project.rungs.length) return; this.withHistory('Move rung',()=>{ const [r]=this.project.rungs.splice(i,1); this.project.rungs.splice(j,0,r); }); },
    removeRung(i){ this.withHistory('Delete rung',()=>{ this.project.rungs.splice(i,1); this.selected=null; this.selectedBranch=null; if(this.editingCommentId) this.editingCommentId=null; }); },
    clearAll(){ if(confirm('Clear all rungs?')) { this.withHistory('Clear all',()=>{ this.project.rungs=[]; this.project.rungs.push(this.createRung('')); this.editingCommentId=null; }); }},
    startEditComment(r){
      if(this.editingCommentId===r.id){ this.editingCommentId=null; return; }
      this.editingCommentId=r.id;
      this.$nextTick(()=>{
        const input=document.activeElement;
        // autofocus usually handles this; this fallback keeps the interaction reliable after Vue updates.
        const rungInputs=[...document.querySelectorAll('input[placeholder="Rung comment"]')];
        const last=rungInputs[rungInputs.length-1];
        if(last){ last.focus(); last.select(); }
      });
    },
    dragTool(t){ this.draggedTool=t; },
    wireNodeClick(r,n){
      if(this.mode!=='branch'){
        this.mode='branch';
        this.branchStart={rung:r,node:n};
        this.show('Start node selected. Click end node.');
        return;
      }
      if(!this.branchStart || this.branchStart.rung!==r){
        this.branchStart={rung:r,node:n};
        this.show('Start node selected. Click end node.');
        return;
      }
      const a=Math.min(this.branchStart.node,n);
      const b=Math.max(this.branchStart.node,n);
      if(b<=a){
        this.show('Pick a different end node');
        return;
      }
      this.pushHistory('Add branch');
      const br=this.createBranch(a,b);
      r.branches.push(br);
      this.selectedBranch={rung:r,branchId:br.id};
      this.branchStart=null;
      this.mode='select';
      this.show('Branch added');
    },
    branchClick(r, branchId){
      this.selected=null;
      this.selectedBranch={rung:r,branchId};
      if(this.mode==='delete') this.deleteBranch(r,branchId);
      else this.show('Branch selected');
    },
    deleteBranch(r, branchId){
      const idx=r.branches.findIndex(b=>b.id===branchId);
      if(idx>=0){ this.pushHistory('Delete branch'); r.branches.splice(idx,1); }
      this.selectedBranch=null;
      this.branchStart=null;
      this.show('Branch deleted');
    },
    slotClick(r, lane, br, slot){
      if(this.mode==='branch'){
        this.show('Branches attach to wire nodes, not contacts');
        return;
      }
      const arr = lane==='main' ? r.main : br.cells;
      if(this.mode==='delete'){
        this.pushHistory('Delete symbol');
        arr[slot]=null;
        this.selected=null;
        return;
      }
      if(!arr[slot]) { this.pushHistory('Add symbol'); arr[slot]=this.newSymbol(this.selectedTool); }
      this.selected={rung:r,lane,branch:br,slot,el:arr[slot]};
      if(lane==='branch' && br) this.selectedBranch={rung:r,branchId:br.id};
    },
    symbolClick(r,lane,br,slot){
      if(this.mode==='branch'){
        this.show('Branches attach to wire nodes, not contacts');
        return;
      }
      const arr = lane==='main' ? r.main : br.cells;
      if(this.mode==='delete'){
        this.pushHistory('Delete symbol');
        arr[slot]=null;
        this.selected=null;
        return;
      }
      this.selected={rung:r,lane,branch:br,slot,el:arr[slot]};
      if(lane==='branch' && br) this.selectedBranch={rung:r,branchId:br.id};
    },
    dropOnMain(r,slot){
      const t=this.draggedTool || this.selectedTool;
      this.pushHistory('Drop symbol');
      r.main[slot]=this.newSymbol(t);
      this.selected={rung:r,lane:'main',branch:null,slot,el:r.main[slot]};
    },
    dropOnBranch(r,br,slot){
      const t=this.draggedTool || this.selectedTool;
      this.pushHistory('Drop branch symbol');
      br.cells[slot]=this.newSymbol(t);
      this.selected={rung:r,lane:'branch',branch:br,slot,el:br.cells[slot]};
      this.selectedBranch={rung:r,branchId:br.id};
    },
    sampleProject(){
      const r1=this.createRung('Start/stop seal-in motor latch');
      r1.main[0]={id:this.uid(),type:'NO',tag:'I0_Start'};
      r1.main[2]={id:this.uid(),type:'NC',tag:'I1_Stop'};
      r1.main[7]={id:this.uid(),type:'OUT',tag:'Q0_Motor'};
      // Seal-in branch bypasses only the Start contact.
      // Stop remains downstream in series, so it always breaks the latch.
      const b1=this.createBranch(0,1);
      b1.cells[0]={id:this.uid(),type:'NO',tag:'Q0_Motor'};
      r1.branches.push(b1);

      const r2=this.createRung('Alarm if over temperature');
      r2.main[0]={id:this.uid(),type:'NO',tag:'TempHigh'};
      r2.main[3]={id:this.uid(),type:'TON',tag:'T_OverTemp',preset:2000};
      r2.main[7]={id:this.uid(),type:'OUT',tag:'Q_Alarm'};

      const r3=this.createRung('Multiple branches example');
      r3.main[0]={id:this.uid(),type:'NO',tag:'I0_Auto'};
      r3.main[7]={id:this.uid(),type:'OUT',tag:'Q_Run'};
      const b31=this.createBranch(0,5);
      b31.cells[0]={id:this.uid(),type:'NO',tag:'I1_Manual'};
      const b32=this.createBranch(0,5);
      b32.cells[0]={id:this.uid(),type:'NO',tag:'I2_Remote'};
      r3.branches.push(b31,b32);

      const r4=this.createScriptRung('Custom AngelScript rung example');
      r4.code='// This rung is passed through exactly as typed inside scan().\n// Use it for logic that is clearer in text than in ladder.\nQ_Debug = I0_Auto && !I1_Stop;';

      this.project=addFormalSchemaMetadata({name:'PiLab Ladder Project',scan_ms:5,rungs:[r1,r2,r3,r4]});
      this.jsonDraft=this.jsonModel;
      this.selected=null;
      this.selectedBranch=null;
      this.mode='select';
      this.branchStart=null;
      this.history=[];
      this.editSnapshot=null;
    },
    saveLocal(){ localStorage.setItem('pilab_ladder_wire_model', JSON.stringify(this.project)); this.show('Saved locally'); },
    loadLocal(){
      const raw=localStorage.getItem('pilab_ladder_wire_model');
      if(!raw) return this.show('No saved project');
      try {
        const parsed=JSON.parse(raw);
        this.normalizeImportedProject(parsed);
        this.pushHistory('Load local project');
        this.project=parsed;
        this.selected=null; this.selectedBranch=null;
        this.show('Loaded');
      } catch(e) {
        alert('Could not load saved project: '+e.message);
      }
    },
    applyJson(){
      try{
        const parsed=JSON.parse(this.jsonDraft);
        this.normalizeImportedProject(parsed);
        this.pushHistory('Apply JSON');
        this.project=parsed;
        this.selected=null; this.selectedBranch=null; this.show('JSON applied');
      }
      catch(e){ alert('Invalid JSON: '+e.message); }
    },
    download(name,text,mime){
      const blob=new Blob([text],{type:mime});
      const url=URL.createObjectURL(blob);
      const a=document.createElement('a');
      a.href=url; a.download=name; a.click();
      URL.revokeObjectURL(url);
    },
    downloadJson(){ this.download('pilab_ladder_project.json',this.jsonModel,'application/json'); },
    importJsonFile(ev){
      const file = ev && ev.target && ev.target.files ? ev.target.files[0] : null;
      if(!file) return;
      const reader = new FileReader();
      reader.onload = () => {
        try {
          const parsed = JSON.parse(String(reader.result || ''));
          this.normalizeImportedProject(parsed);
          this.pushHistory('Import JSON file');
          this.project = parsed;
          this.jsonDraft = this.jsonModel;
          this.selected = null;
          this.selectedBranch = null;
          this.branchStart = null;
          this.editingCommentId = null;
          this.mode = 'select';
          this.show('Imported '+(file.name || 'JSON project'));
        } catch(e) {
          alert('Could not import JSON: '+e.message);
        } finally {
          ev.target.value = '';
        }
      };
      reader.onerror = () => {
        alert('Could not read file: '+(reader.error ? reader.error.message : 'unknown error'));
        ev.target.value = '';
      };
      reader.readAsText(file);
    },
    downloadAs(){ this.download('pilab_ladder_generated.as',this.transpile(),'text/plain'); },
    copy(t){ navigator.clipboard.writeText(t); this.show('Copied'); }
  }
};
</script>

<style>
:root { color-scheme: dark; }
  body {
    margin:0;
    overflow:hidden;
    background:
      radial-gradient(circle at 18% 0%, rgba(34,211,238,.08), transparent 28%),
      radial-gradient(circle at 88% 12%, rgba(59,130,246,.08), transparent 30%),
      #07111d;
    color:#e5edf7;
    font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  }
  .glass {
    background: rgba(15,23,42,.84);
    border: 1px solid rgba(100,116,139,.34);
    box-shadow: 0 18px 55px rgba(0,0,0,.34);
    backdrop-filter: blur(12px);
  }
  .panel {
    background: rgba(2,6,23,.72);
    border:1px solid rgba(51,65,85,.9);
  }
  .canvas-bg {
    background-color:#081321;
    background-image:
      linear-gradient(rgba(148,163,184,.07) 1px, transparent 1px),
      linear-gradient(90deg, rgba(148,163,184,.07) 1px, transparent 1px);
    background-size: 32px 32px;
  }
  .mono { font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace; }
  .btn { transition: 120ms ease; user-select:none; }
  .btn:hover { transform: translateY(-1px); }
  .scrollbar::-webkit-scrollbar { width:10px; height:10px; }
  .scrollbar::-webkit-scrollbar-thumb { background:#334155; border-radius:999px; }
  .scrollbar::-webkit-scrollbar-track { background:#020617; }
</style>
