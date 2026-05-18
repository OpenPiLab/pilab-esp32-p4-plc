<template>
<div id="app" class="h-screen p-3 grid grid-rows-[76px_1fr] gap-3">
  <header class="glass rounded-2xl px-4 py-3 flex items-center justify-between gap-3 overflow-hidden">
    <div class="flex items-center gap-3 min-w-0">
      <div class="w-11 h-11 rounded-xl border border-cyan-300/40 bg-cyan-400/10 text-cyan-300 flex items-center justify-center font-black">P</div>
      <div class="min-w-0">
        <h1 class="text-2xl font-bold truncate">PiLab Ladder Logic Editor <span class="text-sm text-cyan-300 align-middle">v0.1.14</span></h1>
        <p class="text-sm text-slate-400 truncate">Alpha edition: changes may break saved files.</p>
      </div>
    </div>
    <div class="flex flex-wrap justify-end gap-2 shrink-0">
      <button @click="undo" :disabled="!history.length" class="btn px-4 py-2 rounded-xl border border-slate-500/60 bg-slate-700/50 font-semibold disabled:opacity-40 disabled:cursor-not-allowed">Undo</button>
      <button @click="saveLocal" class="btn px-4 py-2 rounded-xl border border-emerald-300/35 bg-emerald-500/15 text-emerald-100 font-semibold">Save</button>
      <button @click="loadLocal" class="btn px-4 py-2 rounded-xl border border-blue-300/35 bg-blue-500/15 text-blue-100 font-semibold">Load</button>
      <button @click="$refs.jsonImport.click()" class="btn px-4 py-2 rounded-xl border border-indigo-300/35 bg-indigo-500/15 text-indigo-100 font-semibold">Import JSON</button>
      <input ref="jsonImport" type="file" accept=".json,application/json" class="hidden" @change="importJsonFile"/>
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

    <section class="glass rounded-2xl min-h-0 overflow-hidden grid grid-rows-[auto_1fr_34px]">
      <div class="px-4 py-3 border-b border-slate-800 flex flex-wrap items-start justify-between gap-3">
        <div class="min-w-[260px] flex-1">
          <div class="flex items-center gap-2 flex-wrap">
            <button v-for="tab in viewTabs" :key="tab.id" @click="activeView=tab.id"
              class="btn px-3 py-1.5 rounded-lg border text-xs font-semibold"
              :class="activeView===tab.id ? 'bg-cyan-500/15 border-cyan-300/50 text-cyan-100' : 'bg-slate-900/70 border-slate-700 text-slate-300 hover:border-slate-500'">
              {{tab.label}}
            </button>
          </div>
          <p class="text-xs text-slate-400 mt-1 leading-relaxed">
            <span v-if="activeView==='ladder'">
              {{ mode==='branch'
                ? (branchStart ? 'Click the end wire node. Main and branch wire nodes are valid.' : 'Click the start wire node. Main and branch wire nodes are valid.')
                : 'Ladder, JSON, AngelScript, and JavaScript are different views of the same logic.' }}
            </span>
            <span v-else>{{activeViewDescription}}</span>
          </p>
        </div>
        <div class="flex flex-wrap items-center justify-end gap-2 shrink-0 max-w-full">
          <template v-if="activeView==='project'">
            <button @click="$refs.projectBundleImport.click()" class="btn px-3 py-1.5 rounded-lg bg-indigo-500/10 border border-indigo-300/35 text-indigo-200 text-xs whitespace-nowrap">Import Bundle</button>
            <input ref="projectBundleImport" type="file" accept=".zip,application/zip" class="hidden" @change="importProjectBundleFile"/>
            <button @click="downloadProjectBundle" class="btn px-3 py-1.5 rounded-lg bg-cyan-500/10 border border-cyan-300/35 text-cyan-200 text-xs whitespace-nowrap">Export Project Bundle</button>
          </template>
          <template v-else-if="activeView==='ladder'">
            <span class="text-xs text-slate-400">Mode:</span>
            <span class="mono text-xs px-2 py-1 rounded border border-cyan-300/30 bg-cyan-500/10 text-cyan-200">{{ mode }}</span>
            <button @click="sampleProject" class="btn px-3 py-1.5 rounded-lg bg-slate-800 border border-slate-600 text-xs">Load Sample</button>
            <button @click="clearAll" class="btn px-3 py-1.5 rounded-lg bg-red-500/10 border border-red-300/30 text-red-200 text-xs">Clear All</button>
          </template>
          <template v-else-if="activeView==='json'">
            <button @click="copy(jsonModel)" class="btn px-3 py-1.5 rounded-lg bg-slate-800 border border-slate-600 text-xs">Copy JSON</button>
            <button @click="downloadJson" class="btn px-3 py-1.5 rounded-lg bg-slate-800 border border-slate-600 text-xs">Export JSON</button>
          </template>
          <template v-else-if="activeView==='angelscript'">
            <label class="flex items-center gap-2 px-3 py-1.5 rounded-lg bg-slate-900/70 border border-slate-700 text-xs text-slate-300 whitespace-nowrap" title="Declare discovered tags as globals for standalone AngelScript testing. Disable this when the PLC runtime already provides tags.">
              <input type="checkbox" v-model="includeAngelScriptTags" class="accent-cyan-400" />
              <span>Add Tags</span>
            </label>
            <button @click="copy(transpile(includeAngelScriptTags))" class="btn px-3 py-1.5 rounded-lg bg-cyan-500/10 border border-cyan-300/35 text-cyan-200 text-xs">Copy AngelScript</button>
            <button @click="downloadAs" class="btn px-3 py-1.5 rounded-lg bg-amber-500/15 border border-amber-300/35 text-amber-100 text-xs">Export AngelScript</button>
          </template>
          <template v-else-if="activeView==='javascript'">
            <button @click="copy(transpileJavaScript())" class="btn px-3 py-1.5 rounded-lg bg-emerald-500/10 border border-emerald-300/35 text-emerald-200 text-xs">Copy JavaScript</button>
            <button @click="downloadJavaScript" class="btn px-3 py-1.5 rounded-lg bg-emerald-500/10 border border-emerald-300/35 text-emerald-200 text-xs">Export JavaScript</button>
          </template>
          <template v-else-if="activeView==='tags'">
            <button @click="$refs.tagRegistryImport.click()" class="btn px-3 py-1.5 rounded-lg bg-indigo-500/10 border border-indigo-300/35 text-indigo-200 text-xs whitespace-nowrap">Import Tags</button>
            <input ref="tagRegistryImport" type="file" accept=".json,application/json" class="hidden" @change="importTagRegistryFile"/>
            <button @click="copy(tagRegistryJson())" class="btn px-3 py-1.5 rounded-lg bg-sky-500/10 border border-sky-300/35 text-sky-200 text-xs whitespace-nowrap">Copy Tags</button>
            <button @click="deleteUnusedImportedTags" class="btn px-3 py-1.5 rounded-lg bg-red-500/10 border border-red-300/30 text-red-200 text-xs whitespace-nowrap">Delete Unused</button>
            <button @click="clearImportedTagRegistry" class="btn px-3 py-1.5 rounded-lg bg-red-500/10 border border-red-300/30 text-red-200 text-xs whitespace-nowrap">Clear Imported</button>
            <button @click="clearTagRegistryEdits" class="btn px-3 py-1.5 rounded-lg bg-amber-500/10 border border-amber-300/30 text-amber-200 text-xs whitespace-nowrap">Reset Edits</button>
            <button @click="downloadTagRegistry" class="btn px-3 py-1.5 rounded-lg bg-sky-500/10 border border-sky-300/35 text-sky-200 text-xs whitespace-nowrap">Export Tags</button>
          </template>
        </div>
      </div>

      <div v-show="activeView==='project'" class="min-h-0 h-full overflow-y-auto overflow-x-hidden scrollbar bg-slate-950/70 p-4 space-y-4">
        <div class="rounded-xl border border-cyan-300/25 bg-cyan-500/5 p-3 text-xs text-slate-300 leading-relaxed break-words">
          <div class="font-bold text-cyan-200 mb-1">Project Settings & Review</div>
          <div>Edit the project metadata stored in the PiLab ladder JSON. The combined bundle export includes the ladder project, tag registry, AngelScript, JavaScript, and a small README.</div>
        </div>

        <div class="grid grid-cols-1 xl:grid-cols-2 gap-4">
          <section class="rounded-xl border border-slate-800 bg-slate-950/45 p-4 space-y-3">
            <h3 class="font-bold text-slate-200">Project Metadata</h3>
            <label class="block text-xs text-slate-400 space-y-1">
              <span>Project name</span>
              <input v-model="project.name" @change="show('Project name updated')" class="w-full px-3 py-2 rounded-lg bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-cyan-400" />
            </label>
            <label class="block text-xs text-slate-400 space-y-1">
              <span>Description</span>
              <textarea v-model="project.description" rows="5" placeholder="Describe what this project does, expected inputs, outputs, and any safe-test notes." class="w-full px-3 py-2 rounded-lg bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-cyan-400 resize-y"></textarea>
            </label>
            <label class="block text-xs text-slate-400 space-y-1 max-w-[220px]">
              <span>Scan time (ms)</span>
              <input type="number" min="1" step="1" v-model.number="project.scan_ms" class="w-full px-3 py-2 rounded-lg bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-cyan-400" />
            </label>
            <div class="text-[11px] text-slate-500 leading-relaxed">
              The scan time is used by timers and by scan-counter style Script Lite examples. Changing it changes timer granularity and simulated real-time behavior.
            </div>
          </section>

          <section class="rounded-xl border border-slate-800 bg-slate-950/45 p-4 space-y-3">
            <div class="flex flex-wrap items-center justify-between gap-2">
              <h3 class="font-bold text-slate-200">Project Bundle</h3>
              <div class="flex flex-wrap gap-2">
                <button @click="$refs.projectBundleImport.click()" class="btn px-3 py-2 rounded-lg bg-indigo-500/10 border border-indigo-300/35 text-indigo-200 text-xs whitespace-nowrap">Import Bundle</button>
                <button @click="downloadProjectBundle" class="btn px-3 py-2 rounded-lg bg-cyan-500/10 border border-cyan-300/35 text-cyan-200 text-xs whitespace-nowrap">Export Project Bundle</button>
              </div>
            </div>
            <div class="text-xs text-slate-400 leading-relaxed">
              Export creates a ZIP package with the ladder project, tag registry, generated AngelScript, generated JavaScript, and README. Import reads the ladder JSON and tag registry JSON from the bundle and regenerates the other files from the current editor.
            </div>
            <div class="rounded-lg bg-black/30 border border-slate-800 p-3 mono text-xs text-slate-300 whitespace-pre-wrap">{{projectBundleManifestText()}}</div>
          </section>
        </div>

        <div class="grid grid-cols-1 xl:grid-cols-3 gap-4">
          <section class="rounded-xl border border-slate-800 bg-slate-950/45 p-4 space-y-3">
            <h3 class="font-bold text-slate-200">Logic Summary</h3>
            <div class="grid grid-cols-2 gap-2 text-xs">
              <div class="rounded-lg border border-slate-800 bg-black/20 p-2"><div class="text-slate-500">Rungs</div><div class="text-lg font-bold text-cyan-200">{{projectReview().rungCount}}</div></div>
              <div class="rounded-lg border border-slate-800 bg-black/20 p-2"><div class="text-slate-500">Script rungs</div><div class="text-lg font-bold text-purple-200">{{projectReview().scriptRungCount}}</div></div>
              <div class="rounded-lg border border-slate-800 bg-black/20 p-2"><div class="text-slate-500">Ladder rungs</div><div class="text-lg font-bold text-emerald-200">{{projectReview().ladderRungCount}}</div></div>
              <div class="rounded-lg border border-slate-800 bg-black/20 p-2"><div class="text-slate-500">User tags</div><div class="text-lg font-bold text-sky-200">{{projectReview().tagCount}}</div></div>
            </div>
          </section>

          <section class="rounded-xl border border-slate-800 bg-slate-950/45 p-4 space-y-3">
            <h3 class="font-bold text-slate-200">Physical Outputs Written</h3>
            <div v-if="projectReview().physicalOutputs.length" class="flex flex-wrap gap-2">
              <span v-for="tag in projectReview().physicalOutputs" :key="tag" class="mono text-xs px-2 py-1 rounded border border-emerald-300/30 bg-emerald-500/10 text-emerald-200">{{tag}}</span>
            </div>
            <div v-else class="text-xs text-slate-500">No physical Q outputs detected.</div>
          </section>

          <section class="rounded-xl border border-slate-800 bg-slate-950/45 p-4 space-y-3">
            <h3 class="font-bold text-slate-200">Validation Summary</h3>
            <div class="flex flex-wrap gap-2 text-xs">
              <span class="px-2 py-1 rounded border" :class="validationSummary.errors ? 'border-red-300/40 bg-red-500/10 text-red-200' : 'border-emerald-300/35 bg-emerald-500/10 text-emerald-200'">{{validationSummary.errors}} errors</span>
              <span class="px-2 py-1 rounded border" :class="validationSummary.warnings ? 'border-amber-300/40 bg-amber-500/10 text-amber-200' : 'border-slate-700 bg-slate-900 text-slate-400'">{{validationSummary.warnings}} warnings</span>
              <span class="px-2 py-1 rounded border border-blue-300/30 bg-blue-500/10 text-blue-200">{{validationSummary.info}} info</span>
            </div>
            <div class="text-xs text-slate-500 leading-relaxed">Use the Validation panel on the right for full details. Script Lite compatibility warnings are included there.</div>
          </section>
        </div>

        <section class="rounded-xl border border-slate-800 bg-slate-950/45 p-4 space-y-3">
          <h3 class="font-bold text-slate-200">Project Tags</h3>
          <div class="grid grid-cols-1 md:grid-cols-3 gap-3 text-xs">
            <div>
              <div class="text-slate-500 font-semibold mb-1">HMI / user tags</div>
              <div v-if="projectReview().hmiTags.length" class="flex flex-wrap gap-1.5">
                <span v-for="tag in projectReview().hmiTags" :key="tag" class="mono px-2 py-1 rounded border border-sky-300/30 bg-sky-500/10 text-sky-200">{{tag}}</span>
              </div>
              <div v-else class="text-slate-500">None detected.</div>
            </div>
            <div>
              <div class="text-slate-500 font-semibold mb-1">Internal memory tags</div>
              <div v-if="projectReview().memoryTags.length" class="flex flex-wrap gap-1.5">
                <span v-for="tag in projectReview().memoryTags" :key="tag" class="mono px-2 py-1 rounded border border-purple-300/30 bg-purple-500/10 text-purple-200">{{tag}}</span>
              </div>
              <div v-else class="text-slate-500">None detected.</div>
            </div>
            <div>
              <div class="text-slate-500 font-semibold mb-1">Other user tags</div>
              <div v-if="projectReview().otherTags.length" class="flex flex-wrap gap-1.5">
                <span v-for="tag in projectReview().otherTags" :key="tag" class="mono px-2 py-1 rounded border border-slate-600 bg-slate-900 text-slate-300">{{tag}}</span>
              </div>
              <div v-else class="text-slate-500">None detected.</div>
            </div>
          </div>
        </section>
      </div>

      <div v-show="activeView==='ladder'" class="canvas-bg overflow-y-auto overflow-x-hidden scrollbar min-h-0">
        <div class="p-4">
          <div v-for="(rung, rIndex) in project.rungs" :key="rung.id" class="mb-4">
            <div class="flex items-center gap-2 mb-2 flex-wrap">
              <div class="mono text-sm text-slate-300 flex items-center gap-2 min-w-[360px]">
                <span class="shrink-0">Rung {{rIndex+1}} <span v-if="rung.kind==='script'" class="text-purple-300">[Script]</span> —</span>
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
              <button @click="insertScriptRungAfter(rIndex)" title="Insert a new AngelScript rung after this rung" class="text-xs px-2 py-1 rounded bg-purple-500/10 border border-purple-300/35 text-purple-200">+ Script After</button>
              <button @click="duplicateRung(rIndex)" class="text-xs px-2 py-1 rounded bg-emerald-500/10 border border-emerald-300/35 text-emerald-200">Duplicate</button>
              <button @click="moveRung(rIndex,-1)" :disabled="rIndex===0" title="Move rung up one position" class="text-xs px-2 py-1 rounded bg-slate-800 border border-slate-700 disabled:opacity-35">↑</button>
              <button @click="moveRung(rIndex,1)" :disabled="rIndex===project.rungs.length-1" title="Move rung down one position" class="text-xs px-2 py-1 rounded bg-slate-800 border border-slate-700 disabled:opacity-35">↓</button>
              <button @click="removeRung(rIndex)" class="text-xs px-2 py-1 rounded bg-red-500/10 border border-red-300/40 text-red-200">Delete Rung</button>
            </div>

            <div v-if="rung.kind==='script'" class="rounded-xl border border-purple-300/25 bg-slate-950/70 p-3">
              <div class="flex items-center justify-between mb-2">
                <div class="text-xs text-purple-200 font-semibold">Script Lite / AngelScript rung body</div>
                <div class="text-xs text-slate-500">Exported to AngelScript and translated by the JavaScript simulator subset</div>
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
              + Add Script Lite Rung
            </button>
          </div>
        </div>
      </div>

      <div v-show="activeView==='json'" class="min-h-0 overflow-hidden bg-slate-950/70">
        <div ref="jsonEditorHost" class="code-editor-host ladder-code-editor ladder-json-editor"></div>
      </div>

      <div v-show="activeView==='angelscript'" class="min-h-0 overflow-hidden bg-slate-950/70">
        <div v-if="includeAngelScriptTags" class="m-4 mb-0 rounded-xl border border-cyan-300/25 bg-cyan-500/5 p-3 text-xs text-slate-300 leading-relaxed">
          <span class="font-semibold text-cyan-200">Add Tags is enabled.</span>
          The export includes optional global declarations for discovered tags so the script can be compiled/tested outside the PLC runtime. Turn this off when exporting for firmware that already provides PLC tags.
        </div>
        <div ref="angelScriptEditorHost" class="code-editor-host ladder-code-editor" :class="includeAngelScriptTags ? 'with-note' : ''"></div>
      </div>

      <div v-show="activeView==='javascript'" class="min-h-0 overflow-hidden bg-slate-950/70">
        <div ref="javascriptEditorHost" class="code-editor-host ladder-code-editor"></div>
      </div>

      <div v-show="activeView==='tags'" class="min-h-0 h-full overflow-y-auto overflow-x-hidden scrollbar bg-slate-950/70 p-4 space-y-4">
        <div class="rounded-xl border border-sky-300/25 bg-sky-500/5 p-3 text-xs text-slate-300 leading-relaxed break-words">
          <div class="font-bold text-sky-200 mb-1">PLC Web Interface Tag Registry Export</div>
          <div>This is a separate <span class="mono">pilab_tags.json</span> file for the PLC Web Interface Tag Registry. You can import an existing registry, merge it with tags discovered from this ladder project, edit metadata, and export it again. Physical I/O tags such as <span class="mono">I0</span>, <span class="mono">Q0</span>, <span class="mono">AI0</span>, and <span class="mono">AO0</span> are intentionally excluded because the PLC owns those as system tags.</div>
          <div class="mt-2 text-slate-400"><span class="text-red-200">Delete Unused</span> removes imported tags that are not referenced by the current ladder project. <span class="text-red-200">Clear Imported</span> removes imported metadata and unused imported tags, but tags still used by the ladder will be auto-discovered again.</div>
        </div>

        <div class="rounded-xl border border-slate-800 bg-slate-950/45 p-3 space-y-2">
          <div class="flex flex-wrap items-center justify-between gap-2">
            <div>
              <div class="font-bold text-slate-200 text-sm">Filter Tags</div>
              <div class="text-[11px] text-slate-500">Search by name, type, usage, or description. This affects the table and detailed cards only; export still includes all discovered user tags.</div>
            </div>
            <div class="text-[11px] text-slate-500 mono">{{filteredTagRegistryRows().length}} / {{buildTagRegistryRows().length}} shown</div>
          </div>
          <div class="flex flex-wrap items-center gap-2">
            <input v-model="tagRegistryFilter" placeholder="Filter tags, for example HMI, fault, counter, writable..." class="flex-1 min-w-[220px] px-3 py-2 rounded-lg bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400 text-xs" />
            <button v-if="tagRegistryFilter" @click="tagRegistryFilter=''" class="btn px-3 py-2 rounded-lg bg-slate-800 border border-slate-600 text-xs text-slate-200 whitespace-nowrap">Clear Filter</button>
          </div>
        </div>

        <div class="overflow-auto scrollbar rounded-xl border border-slate-800 max-h-[42vh]">
          <table class="w-full text-xs min-w-[1180px]">
            <thead class="bg-black/40 text-slate-500 uppercase text-[10px] tracking-widest">
              <tr>
                <th class="text-left p-2 w-[170px]">Name</th>
                <th class="text-left p-2 w-[120px]">Status</th>
                <th class="text-left p-2 w-[90px]">Type</th>
                <th class="text-left p-2 w-[110px]">Initial</th>
                <th class="text-left p-2 w-[70px]">Units</th>
                <th class="text-left p-2 w-[70px]">Min</th>
                <th class="text-left p-2 w-[70px]">Max</th>
                <th class="text-left p-2 w-[230px]">Flags</th>
                <th class="text-left p-2">Description</th>
                <th class="text-left p-2 w-[125px]">Actions</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="t in filteredTagRegistryRows()" :key="t.name" class="border-t border-slate-800 align-top">
                <td class="p-2">
                  <div class="mono text-sky-200">{{t.name}}</div>
                  <div class="text-[10px] text-slate-500 mt-1">{{tagRegistryUsageSummary(t)}}</div>
                </td>
                <td class="p-2">
                  <div class="flex flex-wrap gap-1">
                    <span v-for="badge in tagRegistryStatusBadges(t)" :key="t.name+'_'+badge" class="text-[10px] px-1.5 py-0.5 rounded border" :class="tagRegistryBadgeClass(badge)">{{badge}}</span>
                  </div>
                </td>
                <td class="p-2">
                  <select :value="t.type" @change="updateTagRegistryField(t.name,'type',$event.target.value)" class="w-full px-2 py-1 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400">
                    <option value="bool">bool</option>
                    <option value="int">int</option>
                    <option value="float">float</option>
                  </select>
                </td>
                <td class="p-2">
                  <select v-if="t.type==='bool'" :value="String(t.value)" @change="updateTagRegistryField(t.name,'value',$event.target.value === 'true')" class="w-full px-2 py-1 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400">
                    <option value="false">false</option>
                    <option value="true">true</option>
                  </select>
                  <input v-else :value="t.value" @change="updateTagRegistryField(t.name,'value',$event.target.value)" class="w-full px-2 py-1 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400" />
                </td>
                <td class="p-2"><input :value="t.units" @change="updateTagRegistryField(t.name,'units',$event.target.value)" class="w-full px-2 py-1 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400" /></td>
                <td class="p-2"><input :value="t.min" @change="updateTagRegistryField(t.name,'min',$event.target.value)" class="w-full px-2 py-1 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400" /></td>
                <td class="p-2"><input :value="t.max" @change="updateTagRegistryField(t.name,'max',$event.target.value)" class="w-full px-2 py-1 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400" /></td>
                <td class="p-2 text-slate-300 space-y-1">
                  <label class="inline-flex items-center gap-1 mr-2"><input type="checkbox" :checked="t.writable" @change="updateTagRegistryField(t.name,'writable',$event.target.checked)" /> writable</label>
                  <label class="inline-flex items-center gap-1 mr-2"><input type="checkbox" :checked="t.retentive" @change="updateTagRegistryField(t.name,'retentive',$event.target.checked)" /> retentive</label>
                  <label class="inline-flex items-center gap-1 mr-2"><input type="checkbox" :checked="t.hmi_visible" @change="updateTagRegistryField(t.name,'hmi_visible',$event.target.checked)" /> HMI</label>
                  <label class="inline-flex items-center gap-1 mr-2"><input type="checkbox" :checked="t.script_visible" @change="updateTagRegistryField(t.name,'script_visible',$event.target.checked)" /> script</label>
                </td>
                <td class="p-2"><textarea :value="t.description" @change="updateTagRegistryField(t.name,'description',$event.target.value)" rows="2" class="w-full px-2 py-1 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400 resize-y"></textarea></td>
                <td class="p-2">
                  <div class="flex flex-col gap-1 items-start">
                    <button v-if="t.__edited" @click="resetTagRegistryRow(t.name)" class="text-[10px] px-2 py-1 rounded bg-amber-500/10 border border-amber-300/30 text-amber-200">Reset Edit</button>
                    <button v-if="canDeleteTagRegistryRow(t)" @click="deleteTagRegistryRow(t.name)" class="text-[10px] px-2 py-1 rounded bg-red-500/10 border border-red-300/30 text-red-200">Delete</button>
                    <span v-if="!t.__edited && !canDeleteTagRegistryRow(t)" class="text-[10px] text-slate-600">locked</span>
                  </div>
                </td>
              </tr>
              <tr v-if="buildTagRegistryRows().length===0"><td colspan="10" class="p-4 text-center text-slate-500">No user/HMI/script tags discovered. System I/O tags are excluded by design.</td></tr>
              <tr v-else-if="filteredTagRegistryRows().length===0"><td colspan="10" class="p-4 text-center text-slate-500">No tags match the current filter.</td></tr>
            </tbody>
          </table>
        </div>

        <div class="rounded-xl border border-slate-800 bg-slate-950/45 p-3 space-y-3">
          <div class="flex flex-wrap items-start justify-between gap-2">
            <div>
              <div class="font-bold text-slate-200 text-sm">Detailed Tag Editor</div>
              <div class="text-[11px] text-slate-500 mt-1">The table above is optimized for scanning. These cards wrap on narrow screens so every editable field is reachable without horizontal scrolling.</div>
            </div>
            <div class="text-[11px] text-slate-500 mono">{{filteredTagRegistryRows().length}} / {{buildTagRegistryRows().length}} user tags</div>
          </div>

          <div v-for="t in filteredTagRegistryRows()" :key="'card_'+t.name" class="rounded-xl border border-slate-800 bg-black/25 p-3 space-y-3">
            <div class="flex flex-wrap items-start justify-between gap-2">
              <div>
                <div class="mono text-sky-200 text-sm">{{t.name}}</div>
                <div class="flex flex-wrap gap-1 mt-1">
                  <span v-for="badge in tagRegistryStatusBadges(t)" :key="'card_'+t.name+'_'+badge" class="text-[10px] px-1.5 py-0.5 rounded border" :class="tagRegistryBadgeClass(badge)">{{badge}}</span>
                </div>
                <div class="text-[10px] text-slate-500 mt-1">{{tagRegistryUsageSummary(t) || (t.__used ? 'auto-discovered' : 'not used by current ladder project')}}</div>
              </div>
              <div class="flex flex-wrap gap-1 justify-end">
                <button v-if="t.__edited" @click="resetTagRegistryRow(t.name)" class="text-[10px] px-2 py-1 rounded bg-amber-500/10 border border-amber-300/30 text-amber-200">Reset Edit</button>
                <button v-if="canDeleteTagRegistryRow(t)" @click="deleteTagRegistryRow(t.name)" class="text-[10px] px-2 py-1 rounded bg-red-500/10 border border-red-300/30 text-red-200">Delete Unused Tag</button>
                <span v-if="!t.__edited && !canDeleteTagRegistryRow(t)" class="text-[10px] text-slate-600 px-2 py-1">auto/used</span>
              </div>
            </div>

            <div class="grid grid-cols-1 md:grid-cols-2 xl:grid-cols-4 gap-3">
              <label class="text-[10px] uppercase tracking-widest text-slate-500">Type
                <select :value="t.type" @change="updateTagRegistryField(t.name,'type',$event.target.value)" class="mt-1 w-full px-2 py-1.5 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400 normal-case tracking-normal text-xs">
                  <option value="bool">bool</option>
                  <option value="int">int</option>
                  <option value="float">float</option>
                </select>
              </label>
              <label class="text-[10px] uppercase tracking-widest text-slate-500">Initial
                <select v-if="t.type==='bool'" :value="String(t.value)" @change="updateTagRegistryField(t.name,'value',$event.target.value === 'true')" class="mt-1 w-full px-2 py-1.5 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400 normal-case tracking-normal text-xs">
                  <option value="false">false</option>
                  <option value="true">true</option>
                </select>
                <input v-else :value="t.value" @change="updateTagRegistryField(t.name,'value',$event.target.value)" class="mt-1 w-full px-2 py-1.5 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400 normal-case tracking-normal text-xs" />
              </label>
              <label class="text-[10px] uppercase tracking-widest text-slate-500">Units
                <input :value="t.units" @change="updateTagRegistryField(t.name,'units',$event.target.value)" class="mt-1 w-full px-2 py-1.5 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400 normal-case tracking-normal text-xs" />
              </label>
              <div class="grid grid-cols-2 gap-2">
                <label class="text-[10px] uppercase tracking-widest text-slate-500">Min
                  <input :value="t.min" @change="updateTagRegistryField(t.name,'min',$event.target.value)" class="mt-1 w-full px-2 py-1.5 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400 normal-case tracking-normal text-xs" />
                </label>
                <label class="text-[10px] uppercase tracking-widest text-slate-500">Max
                  <input :value="t.max" @change="updateTagRegistryField(t.name,'max',$event.target.value)" class="mt-1 w-full px-2 py-1.5 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400 normal-case tracking-normal text-xs" />
                </label>
              </div>
            </div>

            <div class="grid grid-cols-2 md:grid-cols-4 gap-2 text-xs text-slate-300">
              <label class="inline-flex items-center gap-2 rounded-lg border border-slate-800 bg-slate-950/60 px-2 py-1.5"><input type="checkbox" :checked="t.writable" @change="updateTagRegistryField(t.name,'writable',$event.target.checked)" /> writable</label>
              <label class="inline-flex items-center gap-2 rounded-lg border border-slate-800 bg-slate-950/60 px-2 py-1.5"><input type="checkbox" :checked="t.retentive" @change="updateTagRegistryField(t.name,'retentive',$event.target.checked)" /> retentive</label>
              <label class="inline-flex items-center gap-2 rounded-lg border border-slate-800 bg-slate-950/60 px-2 py-1.5"><input type="checkbox" :checked="t.hmi_visible" @change="updateTagRegistryField(t.name,'hmi_visible',$event.target.checked)" /> HMI visible</label>
              <label class="inline-flex items-center gap-2 rounded-lg border border-slate-800 bg-slate-950/60 px-2 py-1.5"><input type="checkbox" :checked="t.script_visible" @change="updateTagRegistryField(t.name,'script_visible',$event.target.checked)" /> script visible</label>
            </div>

            <label class="block text-[10px] uppercase tracking-widest text-slate-500">Description
              <textarea :value="t.description" @change="updateTagRegistryField(t.name,'description',$event.target.value)" rows="2" class="mt-1 w-full px-2 py-1.5 rounded bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-sky-400 resize-y normal-case tracking-normal text-xs"></textarea>
            </label>
          </div>
        </div>

        <div class="rounded-xl border border-slate-800 bg-slate-950/80 overflow-hidden">
          <div class="px-3 py-2 border-b border-slate-800 text-[10px] uppercase tracking-widest text-slate-500">Export Preview: pilab_tags.json</div>
          <pre class="mono text-xs text-slate-300 p-4 whitespace-pre overflow-auto scrollbar max-h-[36vh]">{{tagRegistryJson()}}</pre>
        </div>
      </div>

      <div class="px-4 py-2 border-t border-slate-800 flex gap-6 text-xs overflow-hidden">
        <span><b class="text-emerald-300">Scan:</b> {{project.scan_ms}} ms</span>
        <span><b>Rungs:</b> {{project.rungs.length}}</span>
        <span><b>Branches:</b> {{branchCount()}}</span>
        <span><b>Symbols:</b> {{allElements().length}}</span>
        <span><b>User Tags:</b> {{buildTagRegistryPayload().tags.length}}</span>
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
            <span><b>Tags:</b> {{simVisibleTagList.length}} / {{simAllTagList.length}}</span>
          </div>
          <div class="grid grid-cols-[1fr_auto_auto] gap-2 items-center">
            <input v-model="simTagFilter" type="text" placeholder="Filter tags... e.g. Speed, Raw, Q_"
              class="w-full px-2 py-1.5 rounded-lg bg-slate-950 border border-slate-700 mono text-xs text-slate-100 outline-none focus:border-cyan-400"/>
            <button @click="simWatchOnly=!simWatchOnly" class="btn px-2 py-1.5 rounded-lg border text-xs"
              :class="simWatchOnly ? 'bg-cyan-500/15 border-cyan-300/45 text-cyan-100' : 'bg-slate-900 border-slate-700 text-slate-300'">Watch</button>
            <button @click="simTagFilter=''" class="btn px-2 py-1.5 rounded-lg bg-slate-900 border border-slate-700 text-xs text-slate-300">Clear</button>
          </div>
          <div class="space-y-1">
            <div class="grid grid-cols-[26px_1fr_70px_92px] gap-2 px-2 text-[10px] font-bold tracking-widest uppercase text-slate-500">
              <span></span>
              <span>Tag</span>
              <span>Type</span>
              <span>Value</span>
            </div>
            <div v-for="tag in simVisibleTagList" :key="tag"
              class="grid grid-cols-[26px_1fr_70px_92px] gap-2 items-center px-2 py-1.5 rounded-lg border bg-slate-900/70 border-slate-700">
              <button @click="toggleSimWatchTag(tag)" class="btn text-sm leading-none" :class="isSimTagWatched(tag) ? 'text-cyan-300' : 'text-slate-600'" :title="isSimTagWatched(tag) ? 'Remove from watch list' : 'Add to watch list'">★</button>
              <button @click="toggleSimTag(tag)"
                class="min-w-0 text-left mono text-xs truncate btn"
                :class="simTagBoolValue(tag) ? 'text-emerald-100' : 'text-slate-400'"
                :title="'Toggle boolean value for ' + tag">
                <span class="inline-block w-2 h-2 rounded-full mr-1" :class="simTagBoolValue(tag) ? 'bg-emerald-300' : 'bg-slate-600'"></span>{{tag}}
              </button>
              <select :value="simTagType(tag)" @change="setSimTagType(tag, $event.target.value)"
                class="w-full px-1 py-1 rounded bg-slate-950 border border-slate-700 mono text-[11px] text-slate-300 outline-none focus:border-cyan-400">
                <option value="bool">bool</option>
                <option value="number">number</option>
                <option value="string">string</option>
              </select>
              <input v-if="simTagType(tag)==='number'" type="number" step="any" :value="simTagEditText(tag)" @input="setSimTagFromInput(tag, $event.target.value)" @keydown.stop
                class="w-full px-2 py-1 rounded bg-slate-950 border border-slate-700 mono text-xs text-slate-100 outline-none focus:border-cyan-400"/>
              <input v-else-if="simTagType(tag)==='string'" type="text" :value="simTagEditText(tag)" @input="setSimTagFromInput(tag, $event.target.value)" @keydown.stop
                class="w-full px-2 py-1 rounded bg-slate-950 border border-slate-700 mono text-xs text-slate-100 outline-none focus:border-cyan-400"/>
              <button v-else @click="toggleSimTag(tag)"
                class="px-2 py-1 rounded border mono text-xs text-left"
                :class="simTagBoolValue(tag) ? 'bg-emerald-500/15 border-emerald-300/45 text-emerald-100' : 'bg-slate-950 border-slate-700 text-slate-400'">
                {{simTagBoolValue(tag) ? 'true' : 'false'}}
              </button>
            </div>
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
            Live mode executes the generated JavaScript backend in the browser. <span class="text-emerald-300">Green</span> means energized power flow; <span class="text-amber-300">amber</span> means the contact/block condition is true but upstream power is not present. Boolean tags can still be toggled. Numeric/string tags can be edited live, including while the simulator is running. Use the filter box or star tags and enable Watch mode to keep a small subset visible. Ladder contacts still treat nonzero numeric values as true. Timers show ET/remaining time; counters show CV.
          </div>
        </div>
      </section>

    </aside>
  </main>

  <div v-if="toast" class="fixed bottom-4 right-4 glass rounded-xl px-4 py-3 text-cyan-100 border-cyan-300/30">{{toast}}</div>
</div>
</template>

<script>
import { nextTick } from 'vue';
import SymbolRender from './components/SymbolRender.vue';
import { minimalEditor, readonlyEditor } from 'prism-code-editor-lightweight/setups';
import { defaultCommands, editHistory } from 'prism-code-editor-lightweight/commands';
import { matchBrackets } from 'prism-code-editor-lightweight/match-brackets';
import { highlightBracketPairs } from 'prism-code-editor-lightweight/highlight-brackets';
import { indentGuides } from 'prism-code-editor-lightweight/guides';
import { cursorPosition } from 'prism-code-editor-lightweight/cursor';
import 'prism-code-editor-lightweight/prism/languages/clike';
import 'prism-code-editor-lightweight/prism/languages/cpp';
import 'prism-code-editor-lightweight/prism/languages/javascript';
import 'prism-code-editor-lightweight/prism/languages/json';
import { ladderModelMethods } from './ladder/ladderModel.js';
import { ladderValidationMethods } from './ladder/ladderValidation.js';
import { ladderSimulatorMethods } from './ladder/ladderSimulator.js';
import { ladderTranspilerMethods } from './ladder/ladderTranspiler.js';
import { ladderTagRegistryMethods } from './ladder/ladderTagRegistry.js';
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
      activeView:'project',
      viewTabs:[
        {id:'project', label:'Project'},
        {id:'ladder', label:'Ladder'},
        {id:'json', label:'JSON'},
        {id:'angelscript', label:'AngelScript'},
        {id:'javascript', label:'JavaScript'},
        {id:'tags', label:'Tag Registry'}
      ],
      mode:'select',
      branchStart:null,
      selected:null,
      selectedBranch:null,
      editingCommentId:null,
      hoverRungId:null,
      toast:'',
      jsonDraft:'',
      collapsedPanels:{ inspector:false, validation:false, simulator:false },
      history:[],
      editSnapshot:null,
      maxHistory:60,
      simRunning:false,
      simTimer:null,
      simTags:{},
      simBlocks:{},
      simRungs:{},
      simProgram:null,
      simCompiledCode:'',
      simScanCount:0,
      simRunLastMs:0,
      simRunAccumMs:0,
      simTagFilter:'',
      simWatchOnly:false,
      simWatchTags:{},
      tagRegistryEdits:{},
      tagRegistryImported:{},
      tagRegistryFilter:'',
      includeAngelScriptTags:false,
      prismEditors:{ json:null, angelscript:null, javascript:null },
      prismEditorsReady:false,
      syncingJsonEditor:false,
      project:{ name:'PiLab Ladder Project', description:'', scan_ms:5, rungs:[] },
      tools:[
        {type:'NO', label:'Normally Open', desc:'True when input/tag is true.', icon:'<svg width="48" height="32"><line x1="2" y1="16" x2="14" y2="16" stroke="#e2e8f0" stroke-width="2"/><line x1="34" y1="16" x2="46" y2="16" stroke="#e2e8f0" stroke-width="2"/><line x1="15" y1="4" x2="15" y2="28" stroke="#67e8f9" stroke-width="3"/><line x1="33" y1="4" x2="33" y2="28" stroke="#67e8f9" stroke-width="3"/></svg>'},
        {type:'NC', label:'Normally Closed', desc:'True when input/tag is false.', icon:'<svg width="48" height="32"><line x1="2" y1="16" x2="14" y2="16" stroke="#e2e8f0" stroke-width="2"/><line x1="34" y1="16" x2="46" y2="16" stroke="#e2e8f0" stroke-width="2"/><line x1="15" y1="4" x2="15" y2="28" stroke="#fca5a5" stroke-width="3"/><line x1="33" y1="4" x2="33" y2="28" stroke="#fca5a5" stroke-width="3"/><line x1="9" y1="28" x2="39" y2="4" stroke="#fca5a5" stroke-width="2"/></svg>'},
        {type:'OUT', label:'Output Coil', desc:'Writes rung result.', icon:'<svg width="48" height="32"><path d="M13 4 Q2 16 13 28" fill="none" stroke="#86efac" stroke-width="3"/><path d="M35 4 Q46 16 35 28" fill="none" stroke="#86efac" stroke-width="3"/></svg>'},
        {type:'SET', label:'Set Coil', desc:'Latches the tag true when powered.', icon:'<svg width="48" height="32"><path d="M13 4 Q2 16 13 28" fill="none" stroke="#60a5fa" stroke-width="3"/><path d="M35 4 Q46 16 35 28" fill="none" stroke="#60a5fa" stroke-width="3"/><text x="24" y="20" text-anchor="middle" fill="#bfdbfe" font-size="12" font-weight="900">S</text></svg>'},
        {type:'RST', label:'Reset Coil', desc:'Resets the tag false when powered.', icon:'<svg width="48" height="32"><path d="M13 4 Q2 16 13 28" fill="none" stroke="#f87171" stroke-width="3"/><path d="M35 4 Q46 16 35 28" fill="none" stroke="#f87171" stroke-width="3"/><text x="24" y="20" text-anchor="middle" fill="#fecaca" font-size="12" font-weight="900">R</text></svg>'},
        {type:'ONS', label:'One Shot Rising', desc:'Passes power for one scan on a rising edge.', icon:'<svg width="48" height="32"><rect x="7" y="4" width="34" height="24" rx="5" fill="#0f172a" stroke="#38bdf8" stroke-width="2"/><text x="24" y="20" text-anchor="middle" fill="#bae6fd" font-size="10" font-weight="700">ONS</text></svg>'},
        {type:'TON', label:'On Delay Timer', desc:'Timer on delay.', icon:'<svg width="48" height="32"><rect x="7" y="4" width="34" height="24" rx="5" fill="#0f172a" stroke="#fbbf24" stroke-width="2"/><text x="24" y="20" text-anchor="middle" fill="#fde68a" font-size="10" font-weight="700">TON</text></svg>'},
        {type:'TOF', label:'Off Delay Timer', desc:'Timer off delay.', icon:'<svg width="48" height="32"><rect x="7" y="4" width="34" height="24" rx="5" fill="#0f172a" stroke="#fbbf24" stroke-width="2"/><text x="24" y="20" text-anchor="middle" fill="#fde68a" font-size="10" font-weight="700">TOF</text></svg>'},
        {type:'CTU', label:'Up Counter', desc:'Counts up.', icon:'<svg width="48" height="32"><rect x="7" y="4" width="34" height="24" rx="5" fill="#0f172a" stroke="#c084fc" stroke-width="2"/><text x="24" y="20" text-anchor="middle" fill="#e9d5ff" font-size="10" font-weight="700">CTU</text></svg>'},
        {type:'CTD', label:'Down Counter', desc:'Counts down.', icon:'<svg width="48" height="32"><rect x="7" y="4" width="34" height="24" rx="5" fill="#0f172a" stroke="#c084fc" stroke-width="2"/><text x="24" y="20" text-anchor="middle" fill="#e9d5ff" font-size="10" font-weight="700">CTD</text></svg>'}
      ]
    };
  },
  computed:{
    activeViewDescription(){
      if(this.activeView==='project') return 'Project metadata, scan settings, output/tag review, and combined project bundle export.';
      if(this.activeView==='json') return 'Editable JSON project model. Change it and leave the field to apply.';
      if(this.activeView==='angelscript') return 'Generated AngelScript target. Enable Add Tags to include optional standalone test globals.';
      if(this.activeView==='javascript') return 'Generated JavaScript target used by the browser simulator.';
      if(this.activeView==='tags') return 'Separate PLC Web Interface-compatible tag registry JSON. Import existing tags, merge with discovered tags, edit metadata, then export.';
      return '';
    },
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
    simAllTagList(){
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

        if (['TON','TOF','CTU','CTD','ONS'].includes(e.type)) {
          if (tag) blockTags.add(tag);
          if (this.isCounter(e)) addExpressionTags(e.resetTag || e.resetExpr || e.loadTag || e.loadExpr || '');
        } else if (tag) {
          addTag(tag);
        }
      }

      // Script rung identifiers are simulator-visible tags too. This is what
      // makes math/setpoint tags such as HmiSpeedSetpoint and RawAI0 editable
      // before a scan has created them. The tag extractor is intentionally
      // best-effort and filters out locals, type names, and Math helper names.
      for (const r of (this.project.rungs || [])) {
        if (r && r.kind === 'script' && this.jsScriptRungTags) {
          for (const t of this.jsScriptRungTags(r.code || '')) addTag(t);
        }
      }

      // Keep any manually created simulator tags visible too.
      for (const k of Object.keys(this.simTags || {})) addTag(k);

      // Block instance tags belong in the Blocks panel, not as manual simulator inputs.
      return [...tags].filter(t => !blockTags.has(t)).sort();
    },
    simVisibleTagList(){
      const filter=String(this.simTagFilter || '').trim().toLowerCase();
      return this.simAllTagList.filter(tag=>{
        if(this.simWatchOnly && !this.isSimTagWatched(tag)) return false;
        if(filter && !tag.toLowerCase().includes(filter)) return false;
        return true;
      });
    },
    simTagList(){
      // Backwards-compatible alias used by older code/tests.
      return this.simAllTagList;
    },
    simBlockList(){
      return Object.keys(this.simBlocks||{}).sort().map(tag=>{
        const b=this.simBlocks[tag];
        if(!b) return {tag,text:''};
        if(b.type==='TON') return {tag,text:`TON Q=${b.output?'1':'0'} ET=${Math.round(b.elapsed_ms||0)}/${b.preset_ms||0}ms`};
        if(b.type==='TOF') return {tag,text:`TOF Q=${b.output?'1':'0'} ET=${Math.round(b.elapsed_ms||0)}/${b.preset_ms||0}ms REM=${Math.round(b.remaining_ms||0)}ms`};
        if(b.type==='ONS') return {tag,text:`ONS Q=${b.output?'1':'0'} LAST=${b.last?'1':'0'}`};
        return {tag,text:`${b.type} Q=${b.output?'1':'0'} CV=${b.count||0}/${b.preset||0}${b.resetExpr?' R='+b.resetExpr:''}`};
      });
    }
  },
  mounted(){
    this.sampleProject();
    nextTick(() => this.initPrismOutputEditors());
  },
  beforeUnmount(){
    this.destroyPrismOutputEditors();
  },
  watch:{
    project:{
      deep:true,
      handler(){
        this.jsonDraft=this.jsonModel;
        this.refreshPrismOutputEditors();
      }
    },
    includeAngelScriptTags(){
      this.refreshPrismOutputEditors();
    }
  },
  methods:{
    ...ladderModelMethods,
    ...ladderValidationMethods,
    ...ladderSimulatorMethods,
    ...ladderTranspilerMethods,
    ...ladderTagRegistryMethods,
    initPrismOutputEditors(){
      if(this.prismEditorsReady) return;
      const common = {
        theme:'github-dark',
        tabSize:2,
        insertSpaces:true,
        lineNumbers:true,
        wordWrap:false
      };

      const makeEditable = (host, language, value, onUpdate) => {
        if(!host) return null;
        const ed = minimalEditor(host, { ...common, language, value, onUpdate }, () => {
          this.installLadderPrismHostStyles(host);
        });
        ed.addExtensions(
          defaultCommands(),
          editHistory(),
          indentGuides(),
          matchBrackets(),
          highlightBracketPairs(),
          cursorPosition()
        );
        ed.textarea?.addEventListener('blur', () => this.applyJson());
        return ed;
      };

      const makeReadonly = (host, language, value) => {
        if(!host) return null;
        const ed = readonlyEditor(host, { ...common, language, value, readOnly:true }, () => {
          this.installLadderPrismHostStyles(host);
        });
        return ed;
      };

      this.prismEditors.json = makeEditable(this.$refs.jsonEditorHost, 'json', this.jsonDraft || this.jsonModel, (value) => {
        if(this.syncingJsonEditor) return;
        this.jsonDraft = value;
      });
      this.prismEditors.angelscript = makeReadonly(this.$refs.angelScriptEditorHost, 'cpp', this.transpile(this.includeAngelScriptTags));
      this.prismEditors.javascript = makeReadonly(this.$refs.javascriptEditorHost, 'javascript', this.transpileJavaScript());
      this.prismEditorsReady = true;
      this.refreshPrismOutputEditors();
    },
    installLadderPrismHostStyles(host){
      const root = host && host.shadowRoot;
      if(!root || root.querySelector('#pilab-ladder-prism-style')) return;
      const style = document.createElement('style');
      style.id = 'pilab-ladder-prism-style';
      style.textContent = `
        .prism-code-editor {
          height: 100%;
          min-height: 0;
          background: transparent;
          font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace;
          font-size: 12px;
          line-height: 1.55;
        }
        .prism-code-editor textarea {
          outline: none;
          caret-color: #67e8f9;
        }
        .prism-code-editor .active-line {
          background: rgba(14, 165, 233, .08);
        }
      `;
      root.appendChild(style);
    },
    refreshPrismOutputEditors(){
      if(!this.prismEditorsReady) return;
      const jsonValue = this.jsonModel;
      if(this.prismEditors.json && this.prismEditors.json.value !== jsonValue){
        this.syncingJsonEditor = true;
        this.prismEditors.json.setOptions({ value: jsonValue });
        this.syncingJsonEditor = false;
      }
      this.prismEditors.angelscript?.setOptions({ value:this.transpile(this.includeAngelScriptTags) });
      this.prismEditors.javascript?.setOptions({ value:this.transpileJavaScript() });
    },
    destroyPrismOutputEditors(){
      for(const key of ['json','angelscript','javascript']){
        try{ this.prismEditors?.[key]?.remove?.(); } catch(_){}
      }
      this.prismEditors = { json:null, angelscript:null, javascript:null };
      this.prismEditorsReady = false;
    },
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
    addScriptRung(){ this.withHistory('Add Script Lite rung',()=>this.project.rungs.push(this.createScriptRung())); },
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

      const r4=this.createScriptRung('Custom Script Lite rung example');
      r4.code='// This rung is passed through exactly as typed inside scan().\n// Use it for logic that is clearer in text than in ladder.\nQ_Debug = I0_Auto && !I1_Stop;';

      this.project=addFormalSchemaMetadata({name:'PiLab Ladder Project',description:'Sample mixed ladder and Script Lite project for testing editor behavior.',scan_ms:5,rungs:[r1,r2,r3,r4]});
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
    projectReview(){
      const rungs = (this.project && this.project.rungs) || [];
      const physicalOutputs = new Set();
      const scriptCompatibility = [];
      const addPhysical = (tag) => { const clean=this.sanitize(tag); if(/^Q\d+$/.test(clean)) physicalOutputs.add(clean); };
      for(const [ri,r] of rungs.entries()){
        if(!r) continue;
        if(r.kind === 'script'){
          const tags = this.jsScriptRungTags ? this.jsScriptRungTags(r.code || '') : [];
          for(const t of tags) addPhysical(t);
          if(this.analyzeScriptRungForJsSimulator){
            const analysis=this.analyzeScriptRungForJsSimulator(r.code || '');
            if(analysis.unsupportedLines && analysis.unsupportedLines.length) scriptCompatibility.push({ rung:ri+1, unsupported:analysis.unsupportedLines.length });
          }
          continue;
        }
        const visit=(e)=>{ if(e && ['OUT','SET','RST'].includes(e.type)) addPhysical(e.tag); };
        (r.main||[]).forEach(visit);
        for(const br of (r.branches||[])) (br.cells||[]).forEach(visit);
      }
      const rows=this.buildTagRegistryRows ? this.buildTagRegistryRows() : [];
      const hmiTags=rows.filter(t=>/^HMI_/i.test(t.name)).map(t=>t.name).sort();
      const memoryTags=rows.filter(t=>/^M_/i.test(t.name)).map(t=>t.name).sort();
      const otherTags=rows.filter(t=>!/^HMI_/i.test(t.name) && !/^M_/i.test(t.name)).map(t=>t.name).sort();
      return {
        rungCount:rungs.length,
        ladderRungCount:rungs.filter(r=>r && r.kind!=='script').length,
        scriptRungCount:rungs.filter(r=>r && r.kind==='script').length,
        tagCount:rows.length,
        physicalOutputs:[...physicalOutputs].sort((a,b)=>a.localeCompare(b, undefined, { numeric:true })),
        hmiTags,
        memoryTags,
        otherTags,
        scriptCompatibility
      };
    },
    projectBundleManifestText(){
      return [
        'pilab_ladder_project.json  - PiLab ladder/script project',
        'pilab_tags.json            - PLC Web Interface-compatible tag registry',
        'pilab_ladder_generated.as  - AngelScript export',
        'pilab_ladder_generated.js  - JavaScript simulator/runtime export',
        'README.txt                 - Bundle summary and notes'
      ].join('\n');
    },
    projectBundleReadmeText(){
      const review=this.projectReview();
      return `PiLab Project Bundle\n\nProject: ${this.project.name || 'Untitled'}\nScan time: ${this.project.scan_ms || 5} ms\n\nDescription:\n${this.project.description || '(none)'}\n\nSummary:\n- Rungs: ${review.rungCount}\n- Ladder rungs: ${review.ladderRungCount}\n- Script rungs: ${review.scriptRungCount}\n- User tag registry rows: ${review.tagCount}\n- Physical outputs written: ${review.physicalOutputs.join(', ') || '(none)'}\n\nFiles:\n${this.projectBundleManifestText()}\n\nNotes:\n- Import this ZIP from the Project tab to restore the ladder project and tag registry metadata together.\n- Only pilab_ladder_project.json and pilab_tags.json are imported; generated .as/.js files are recreated from the current editor/exporters.\n- Import pilab_tags.json into the PLC Web Interface Tag Registry when needed.\n- The AngelScript export can include optional tag globals when Add Tags is enabled in the editor.\n`;
    },
    crc32Bytes(bytes){
      let table=this.__zipCrcTable;
      if(!table){
        table=[];
        for(let n=0;n<256;n++){
          let c=n;
          for(let k=0;k<8;k++) c=(c&1) ? (0xedb88320 ^ (c>>>1)) : (c>>>1);
          table[n]=c>>>0;
        }
        this.__zipCrcTable=table;
      }
      let crc=0xffffffff;
      for(const b of bytes) crc=table[(crc ^ b) & 0xff] ^ (crc >>> 8);
      return (crc ^ 0xffffffff) >>> 0;
    },
    makeZipBlob(files){
      const enc=new TextEncoder();
      const chunks=[];
      const central=[];
      let offset=0;
      const pushU16=(arr,v)=>{ arr.push(v&255,(v>>>8)&255); };
      const pushU32=(arr,v)=>{ arr.push(v&255,(v>>>8)&255,(v>>>16)&255,(v>>>24)&255); };
      for(const file of files){
        const nameBytes=enc.encode(file.name);
        const data=enc.encode(String(file.text ?? ''));
        const crc=this.crc32Bytes(data);
        const local=[];
        pushU32(local,0x04034b50); pushU16(local,20); pushU16(local,0); pushU16(local,0); pushU16(local,0); pushU16(local,0);
        pushU32(local,crc); pushU32(local,data.length); pushU32(local,data.length); pushU16(local,nameBytes.length); pushU16(local,0);
        chunks.push(new Uint8Array(local), nameBytes, data);
        const centralOffset=offset;
        offset += local.length + nameBytes.length + data.length;
        const c=[];
        pushU32(c,0x02014b50); pushU16(c,20); pushU16(c,20); pushU16(c,0); pushU16(c,0); pushU16(c,0); pushU16(c,0);
        pushU32(c,crc); pushU32(c,data.length); pushU32(c,data.length); pushU16(c,nameBytes.length); pushU16(c,0); pushU16(c,0); pushU16(c,0); pushU16(c,0); pushU32(c,0); pushU32(c,centralOffset);
        central.push(new Uint8Array(c), nameBytes);
      }
      const centralStart=offset;
      let centralSize=0;
      for(const c of central){ chunks.push(c); centralSize += c.length; }
      offset += centralSize;
      const end=[];
      pushU32(end,0x06054b50); pushU16(end,0); pushU16(end,0); pushU16(end,files.length); pushU16(end,files.length); pushU32(end,centralSize); pushU32(end,centralStart); pushU16(end,0);
      chunks.push(new Uint8Array(end));
      return new Blob(chunks,{type:'application/zip'});
    },
    downloadProjectBundle(){
      const safe=String(this.project.name || 'pilab_project').toLowerCase().replace(/[^a-z0-9]+/g,'_').replace(/^_+|_+$/g,'') || 'pilab_project';
      const files=[
        {name:'pilab_ladder_project.json', text:this.jsonModel},
        {name:'pilab_tags.json', text:this.tagRegistryJson()},
        {name:'pilab_ladder_generated.as', text:this.transpile(this.includeAngelScriptTags)},
        {name:'pilab_ladder_generated.js', text:this.transpileJavaScript()},
        {name:'README.txt', text:this.projectBundleReadmeText()}
      ];
      const blob=this.makeZipBlob(files);
      const url=URL.createObjectURL(blob);
      const a=document.createElement('a');
      a.href=url; a.download=safe+'_pilab_bundle.zip'; a.click();
      URL.revokeObjectURL(url);
      this.show('Project bundle exported');
    },
    parseUncompressedZipFiles(buffer){
      // Minimal ZIP reader for project bundles generated by this editor.
      // It intentionally supports stored/uncompressed entries only because
      // makeZipBlob() writes method 0 files.
      const view = new DataView(buffer);
      const dec = new TextDecoder();
      const files = {};
      let offset = 0;
      const len = buffer.byteLength;
      while(offset + 30 <= len){
        const sig = view.getUint32(offset, true);
        if(sig !== 0x04034b50) break;
        const method = view.getUint16(offset + 8, true);
        const compressedSize = view.getUint32(offset + 18, true);
        const uncompressedSize = view.getUint32(offset + 22, true);
        const nameLen = view.getUint16(offset + 26, true);
        const extraLen = view.getUint16(offset + 28, true);
        const nameStart = offset + 30;
        const dataStart = nameStart + nameLen + extraLen;
        const dataEnd = dataStart + compressedSize;
        if(dataEnd > len) throw new Error('ZIP entry is truncated.');
        const name = dec.decode(new Uint8Array(buffer, nameStart, nameLen));
        if(method !== 0) throw new Error('ZIP entry "'+name+'" is compressed. Import supports PiLab bundles exported by this editor.');
        if(compressedSize !== uncompressedSize) throw new Error('ZIP entry "'+name+'" has unsupported size metadata.');
        files[name] = dec.decode(new Uint8Array(buffer, dataStart, compressedSize));
        offset = dataEnd;
      }
      return files;
    },
    findBundleFile(files, baseName){
      const names = Object.keys(files || {});
      return names.find(n => n === baseName) || names.find(n => String(n).split('/').pop() === baseName) || null;
    },
    importProjectBundlePayload(files){
      const projectName = this.findBundleFile(files, 'pilab_ladder_project.json');
      if(!projectName) throw new Error('Bundle is missing pilab_ladder_project.json.');
      const tagName = this.findBundleFile(files, 'pilab_tags.json');
      const parsedProject = JSON.parse(files[projectName]);
      this.normalizeImportedProject(parsedProject);
      const parsedTags = tagName ? JSON.parse(files[tagName]) : { tags: [] };

      this.pushHistory('Import project bundle');
      this.project = parsedProject;
      this.jsonDraft = this.jsonModel;
      this.selected = null;
      this.selectedBranch = null;
      this.branchStart = null;
      this.editingCommentId = null;
      this.mode = 'select';

      // A bundle import should align the editor with the bundle. Clear previous
      // local tag metadata layers before loading the bundle's tag registry.
      this.tagRegistryImported = {};
      this.tagRegistryEdits = {};
      const importedCount = tagName ? this.importTagRegistryPayload(parsedTags) : 0;

      // Reset simulator runtime state because the loaded project/tag set changed.
      this.stopSimRun();
      this.simTags = {};
      this.simBlocks = {};
      this.simRungs = {};
      this.simProgram = null;
      this.simCompiledCode = '';
      this.simScanCount = 0;

      return { projectName, tagName, importedCount };
    },
    importProjectBundleFile(ev){
      const file = ev && ev.target && ev.target.files ? ev.target.files[0] : null;
      if(!file) return;
      const reader = new FileReader();
      reader.onload = () => {
        try {
          const files = this.parseUncompressedZipFiles(reader.result);
          const result = this.importProjectBundlePayload(files);
          this.show('Imported project bundle'+(result.tagName ? ' with '+result.importedCount+' tag row'+(result.importedCount===1?'':'s') : ''));
        } catch(e) {
          alert('Could not import project bundle: '+e.message);
        } finally {
          ev.target.value = '';
        }
      };
      reader.onerror = () => {
        alert('Could not read project bundle: '+(reader.error ? reader.error.message : 'unknown error'));
        ev.target.value = '';
      };
      reader.readAsArrayBuffer(file);
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
    importTagRegistryFile(ev){
      const file = ev && ev.target && ev.target.files ? ev.target.files[0] : null;
      if(!file) return;
      const reader = new FileReader();
      reader.onload = () => {
        try {
          const parsed = JSON.parse(String(reader.result || ''));
          const count = this.importTagRegistryPayload(parsed);
          this.show('Imported '+count+' tag metadata row'+(count===1?'':'s'));
          this.$forceUpdate();
        } catch(e) {
          alert('Could not import tag registry JSON: '+e.message);
        } finally {
          ev.target.value = '';
        }
      };
      reader.onerror = () => {
        alert('Could not read tag registry file: '+(reader.error ? reader.error.message : 'unknown error'));
        ev.target.value = '';
      };
      reader.readAsText(file);
    },
    downloadAs(){ this.download('pilab_ladder_generated.as',this.transpile(this.includeAngelScriptTags),'text/plain'); },
    downloadJavaScript(){ this.download('pilab_ladder_generated.js',this.transpileJavaScript(),'text/javascript'); },
    updateTagRegistryField(name, field, value){
      this.setTagRegistryOverride(name, { [field]: value });
      this.show('Tag metadata updated');
      this.$forceUpdate();
    },
    resetTagRegistryRow(name){
      this.clearTagRegistryOverride(name);
      this.show('Tag metadata reset');
      this.$forceUpdate();
    },
    canDeleteTagRegistryRow(t){
      return !!(t && !t.__used && t.__imported);
    },
    deleteTagRegistryRow(name){
      const row = this.buildTagRegistryRows().find(t => t.name === name);
      if(!this.canDeleteTagRegistryRow(row)) {
        this.show('Only unused imported tags can be deleted here');
        return;
      }
      if(!confirm('Delete unused imported tag "'+name+'" from the Tag Registry view?')) return;
      const ok = this.deleteImportedTagRegistryRow(name);
      this.show(ok ? 'Deleted unused imported tag' : 'Tag was not deleted');
      this.$forceUpdate();
    },
    deleteUnusedImportedTags(){
      const count = Object.values(this.tagRegistryImported || {}).filter(row => !this.tagRegistryUsedNameSet().has(row.name)).length;
      if(count < 1){ this.show('No unused imported tags to delete'); return; }
      if(!confirm('Delete '+count+' unused imported tag'+(count===1?'':'s')+' from the Tag Registry view?')) return;
      const removed = this.deleteUnusedImportedTagRegistryRows();
      this.show('Deleted '+removed+' unused imported tag'+(removed===1?'':'s'));
      this.$forceUpdate();
    },
    clearImportedTagRegistry(){
      const count = Object.keys(this.tagRegistryImported || {}).length;
      if(count < 1){ this.show('No imported tag metadata to clear'); return; }
      if(!confirm('Clear all imported tag metadata? Tags still used by the ladder project will be auto-discovered again.')) return;
      this.clearImportedTagRegistryRows();
      this.pruneUnusedTagRegistryEdits();
      this.show('Imported tag metadata cleared');
      this.$forceUpdate();
    },
    clearTagRegistryEdits(){
      if(Object.keys(this.tagRegistryEdits || {}).length && !confirm('Reset all edited tag metadata back to auto-discovered/imported defaults?')) return;
      this.tagRegistryEdits = {};
      this.show('Tag metadata edits reset');
    },
    tagRegistryUsageSummary(t){
      const u = t && t.__usage ? t.__usage : null;
      if(!u) return '';
      const parts=[];
      if(u.reads) parts.push('read '+u.reads);
      if(u.writes) parts.push('write '+u.writes);
      if(u.numeric) parts.push('numeric');
      return parts.join(' · ');
    },
    tagRegistryStatusBadges(t){
      const badges=[];
      badges.push(t && t.__used ? 'used' : 'unused');
      if(t && t.__imported) badges.push('imported');
      if(t && !t.__imported && t.__used) badges.push('auto');
      if(t && t.__edited) badges.push('edited');
      return badges;
    },
    tagRegistryBadgeClass(badge){
      if(badge==='used') return 'bg-emerald-500/10 border-emerald-300/30 text-emerald-200';
      if(badge==='unused') return 'bg-slate-800/70 border-slate-600 text-slate-400';
      if(badge==='imported') return 'bg-indigo-500/10 border-indigo-300/35 text-indigo-200';
      if(badge==='edited') return 'bg-amber-500/10 border-amber-300/35 text-amber-200';
      return 'bg-slate-900 border-slate-700 text-slate-400';
    },
    filteredTagRegistryRows(){
      const rows = this.buildTagRegistryRows();
      const q = String(this.tagRegistryFilter || '').trim().toLowerCase();
      if(!q) return rows;
      const hay = (t) => {
        const usage = this.tagRegistryUsageSummary(t);
        const flags = [t.writable ? 'writable' : '', t.retentive ? 'retentive' : '', t.hmi_visible ? 'hmi hmi_visible' : '', t.script_visible ? 'script script_visible' : ''].join(' ');
        const status = this.tagRegistryStatusBadges(t).join(' ');
        return [t.name, t.type, t.value, t.units, t.min, t.max, t.description, usage, flags, status].join(' ').toLowerCase();
      };
      return rows.filter(t => hay(t).includes(q));
    },
    downloadTagRegistry(){ this.download('pilab_tags.json',this.tagRegistryJson(),'application/json'); },
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
  .code-editor-host {
    display:grid;
    height:100%;
    min-height:0;
    overflow:hidden;
  }
  .ladder-code-editor {
    background:rgba(2,6,23,.42);
  }
  .ladder-code-editor.with-note {
    height:calc(100% - 76px);
    margin:1rem;
    margin-top:.75rem;
    border:1px solid rgba(51,65,85,.9);
    border-radius:.75rem;
    overflow:hidden;
  }
  .ladder-json-editor {
    height:100%;
  }
</style>
