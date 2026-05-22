<template>
<div class="pilab-ladder-page p-3 grid grid-rows-[auto_1fr] gap-3">
  <section class="glass rounded-2xl px-4 py-2 flex flex-wrap items-center justify-between gap-3 overflow-hidden">
    <div class="min-w-0 shrink-0">
      <h1 class="text-lg font-bold truncate">Ladder Editor</h1>
    </div>

    <div class="panel rounded-xl px-2 py-1.5 flex flex-wrap items-center gap-1.5 min-w-0 flex-1" aria-label="Ladder tools">
      <span class="text-[10px] font-bold tracking-widest text-slate-500 uppercase mr-1 shrink-0">Tools</span>
      <button @click="mode='select'; branchStart=null" class="px-3 py-1.5 rounded-lg border text-left btn text-xs whitespace-nowrap" :class="mode==='select'?'bg-cyan-500/15 border-cyan-300/50':'bg-slate-900 border-slate-700'">↖ Select / Move</button>
      <button @click="mode='branch'; branchStart=null; show('Branch Tool: click start wire node, then end wire node')" class="px-3 py-1.5 rounded-lg border text-left btn text-xs whitespace-nowrap" :class="mode==='branch'?'bg-blue-500/20 border-blue-300/60':'bg-slate-900 border-slate-700'">⎇ Draw Branch</button>
      <button @click="mode='delete'" class="px-3 py-1.5 rounded-lg border text-left btn text-xs whitespace-nowrap" :class="mode==='delete'?'bg-red-500/20 border-red-300/60':'bg-slate-900 border-slate-700'">⌫ Delete Element</button>
      <button v-if="selectedBranch" @click="deleteBranch(selectedBranch.rung, selectedBranch.branchId)" class="px-3 py-1.5 rounded-lg border text-left btn text-xs whitespace-nowrap bg-red-500/15 border-red-300/50 text-red-100">✕ Delete Selected Branch</button>
      <span v-if="ladderDirty" class="ml-auto px-2.5 py-1 rounded-lg border border-amber-300/35 bg-amber-500/10 text-amber-200 text-[11px] font-bold whitespace-nowrap">Unsaved changes</span>
    </div>

    <div class="flex flex-wrap justify-end gap-2 shrink-0">
      <button @click="undo" :disabled="!history.length" class="btn px-3 py-1.5 rounded-xl border border-slate-500/60 bg-slate-700/50 font-semibold text-xs disabled:opacity-40 disabled:cursor-not-allowed">Undo</button>
      <button @click="$refs.jsonImport.click()" class="btn px-3 py-1.5 rounded-xl border border-indigo-300/35 bg-indigo-500/15 text-indigo-100 font-semibold text-xs">Open</button>
      <button @click="downloadJson" class="btn px-3 py-1.5 rounded-xl border border-slate-500/60 bg-slate-700/50 font-semibold text-xs">Save</button>
      <button @click="syncDiscoveredTagsToTagStore" class="btn px-3 py-1.5 rounded-xl border border-emerald-300/35 bg-emerald-500/15 text-emerald-100 font-semibold text-xs" title="Merge ladder-discovered tags into the Web app Tag Registry memory store.">Sync Tags</button>
      <button @click="sendGeneratedAngelScriptToScriptEditor" class="btn px-3 py-1.5 rounded-xl border border-amber-300/35 bg-amber-500/15 text-amber-100 font-semibold text-xs" title="Generate AngelScript from this ladder project and send it to the Script page editor without uploading to the PLC.">Send to Script</button>
      <button @click="uploadGeneratedAngelScriptToPlc" :disabled="ladderUploadBusy" class="btn px-3 py-1.5 rounded-xl border border-purple-300/35 bg-purple-500/15 text-purple-100 font-semibold text-xs disabled:opacity-40" title="Generate AngelScript from this ladder project and upload it to the PLC.">{{ ladderUploadBusy ? 'Uploading...' : 'Upload PLC' }}</button>
      <input ref="jsonImport" type="file" accept=".piLadder,.json,application/json" class="hidden" @change="importJsonFile"/>
    </div>
  </section>

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
            <button v-for="tab in visibleViewTabs" :key="tab.id" @click="activeView=tab.id"
              class="btn px-3 py-1.5 rounded-lg border text-xs font-semibold"
              :class="activeView===tab.id ? 'bg-cyan-500/15 border-cyan-300/50 text-cyan-100' : 'bg-slate-900/70 border-slate-700 text-slate-300 hover:border-slate-500'">
              {{tab.label}}
            </button>
            <button @click="toggleCodeTabs" class="btn px-2.5 py-1.5 rounded-lg border border-slate-700 bg-slate-900/70 text-xs font-bold text-slate-300 hover:border-slate-500" :title="showCodeTabs ? 'Hide JSON, AngelScript, and JavaScript tabs' : 'Show JSON, AngelScript, and JavaScript tabs'">
              {{ showCodeTabs ? '<' : '>' }}
            </button>
          </div>
        </div>
        <div class="flex flex-wrap items-center justify-end gap-2 shrink-0 max-w-full">
          <template v-if="activeView==='ladder'">
            <span class="text-xs text-slate-400">Mode:</span>
            <span class="mono text-xs px-2 py-1 rounded border border-cyan-300/30 bg-cyan-500/10 text-cyan-200">{{ mode }}</span>
            <button @click="openLadderPrintView" class="btn px-3 py-1.5 rounded-lg bg-white/10 border border-white/25 text-slate-100 text-xs" title="Open a clean printable ladder view. Use the browser print dialog to save as PDF.">Print</button>
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
            <button @click="copy(generatedAngelScriptSource(includeAngelScriptTags))" class="btn px-3 py-1.5 rounded-lg bg-cyan-500/10 border border-cyan-300/35 text-cyan-200 text-xs">Copy AngelScript</button>
            <button @click="downloadAs" class="btn px-3 py-1.5 rounded-lg bg-amber-500/15 border border-amber-300/35 text-amber-100 text-xs">Export AngelScript</button>
            <button @click="sendGeneratedAngelScriptToScriptEditor" class="btn px-3 py-1.5 rounded-lg bg-amber-500/15 border border-amber-300/35 text-amber-100 text-xs">Send to Script</button>
            <button @click="uploadGeneratedAngelScriptToPlc" :disabled="ladderUploadBusy" class="btn px-3 py-1.5 rounded-lg bg-purple-500/10 border border-purple-300/35 text-purple-200 text-xs disabled:opacity-40">{{ ladderUploadBusy ? 'Uploading...' : 'Upload PLC' }}</button>
          </template>
          <template v-else-if="activeView==='javascript'">
            <button @click="copy(transpileJavaScript())" class="btn px-3 py-1.5 rounded-lg bg-emerald-500/10 border border-emerald-300/35 text-emerald-200 text-xs">Copy JavaScript</button>
            <button @click="downloadJavaScript" class="btn px-3 py-1.5 rounded-lg bg-emerald-500/10 border border-emerald-300/35 text-emerald-200 text-xs">Export JavaScript</button>
          </template>

        </div>
      </div>

      <div v-show="activeView==='project'" class="min-h-0 h-full overflow-y-auto overflow-x-hidden scrollbar bg-slate-950/70 p-4 space-y-4">
        <section class="rounded-xl border border-slate-800 bg-slate-950/45 p-4 space-y-3">
          <div class="flex flex-wrap items-center justify-between gap-3">
            <div>
              <h3 class="font-bold text-slate-200">Current Ladder File</h3>
              <div class="text-xs text-slate-500 mono mt-1">
                {{ currentLadderFilePath || '/ladder/(unsaved).piLadder' }}
                <span v-if="ladderDirty" class="text-amber-300 font-bold"> · unsaved changes</span>
                <span v-else class="text-emerald-300"> · saved</span>
              </div>
            </div>
            <div class="flex flex-wrap gap-2">
              <button @click="newLadderFile" class="btn px-3 py-2 rounded-lg bg-slate-800 border border-slate-600 text-xs">New</button>
              <button @click="saveLadderFile" :disabled="ladderFileBusy" class="btn px-3 py-2 rounded-lg bg-emerald-500/10 border border-emerald-300/35 text-emerald-200 text-xs disabled:opacity-40">Save</button>
              <button @click="saveLadderFileAs" :disabled="ladderFileBusy" class="btn px-3 py-2 rounded-lg bg-cyan-500/10 border border-cyan-300/35 text-cyan-200 text-xs disabled:opacity-40">Save As</button>
              <button @click="loadSelectedLadderFile" :disabled="ladderFileBusy || !selectedLadderFile" class="btn px-3 py-2 rounded-lg bg-indigo-500/10 border border-indigo-300/35 text-indigo-200 text-xs disabled:opacity-40">Load Selected</button>
              <button @click="deleteSelectedLadderFile" :disabled="ladderFileBusy || !selectedLadderFile" class="btn px-3 py-2 rounded-lg bg-red-500/10 border border-red-300/35 text-red-200 text-xs disabled:opacity-40">Delete</button>
              <button @click="downloadJson" class="btn px-3 py-2 rounded-lg bg-slate-800 border border-slate-600 text-slate-200 text-xs">Export JSON</button>
              <button @click="uploadGeneratedAngelScriptToPlc" :disabled="ladderUploadBusy" class="btn px-3 py-2 rounded-lg bg-purple-500/10 border border-purple-300/35 text-purple-200 text-xs disabled:opacity-40">{{ ladderUploadBusy ? 'Uploading...' : 'Upload PLC' }}</button>
            </div>
          </div>

          <div class="grid grid-cols-1 lg:grid-cols-[minmax(260px,420px)_1fr] gap-3">
            <label class="block text-xs text-slate-400 space-y-1">
              <span>File name</span>
              <input v-model="ladderFileNameDraft" @keydown.enter.prevent="downloadJson" class="w-full px-3 py-2 rounded-lg bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-cyan-400 mono" placeholder="main.piLadder" />
            </label>
            <div class="rounded-lg border border-slate-800 bg-black/20 p-3 text-xs text-slate-400 leading-relaxed">
              Ladder JSON is optional project/source storage under <span class="mono text-slate-300">/ladder</span>. <b>Upload PLC</b> generates AngelScript and uses the same script upload/compile path as the Script page, so it does not require saving the ladder JSON first. Use <b>Export JSON</b> to keep a local copy.
            </div>
          </div>

          <div v-if="!flashWritesAllowedNow" class="rounded-lg border border-amber-800 bg-amber-950/35 text-amber-200 px-3 py-2 text-xs font-bold">
            PLC RUN mode active — filesystem writes are locked. Stop the PLC before saving or deleting ladder files.
          </div>
          <div class="space-y-1 text-xs">
            <div :class="ladderFileStatusClass">{{ ladderFileStatus }}</div>
            <div v-if="ladderUploadStatus" :class="ladderUploadStatusClass">{{ ladderUploadStatus }}</div>
          </div>

          <div v-if="ladderCompileDiagnostics.errors.length || ladderCompileDiagnostics.warnings.length || ladderCompileDiagnostics.raw" class="rounded-lg border border-slate-800 bg-black/25 p-3 text-xs space-y-2">
            <div class="flex flex-wrap items-center justify-between gap-2">
              <h4 class="font-bold text-slate-200">P4 Compile Result</h4>
              <button @click="clearLadderCompileDiagnostics" class="btn px-2 py-1 rounded bg-slate-800 border border-slate-700 text-[11px]">Clear</button>
            </div>
            <div v-if="ladderCompileDiagnostics.errors.length" class="space-y-1">
              <div v-for="(err, idx) in ladderCompileDiagnostics.errors" :key="'e'+idx" class="text-red-300 mono">ERR {{err.line}}:{{err.col}} — {{err.msg}}</div>
            </div>
            <div v-if="ladderCompileDiagnostics.warnings.length" class="space-y-1">
              <div v-for="(warn, idx) in ladderCompileDiagnostics.warnings" :key="'w'+idx" class="text-amber-300 mono">WARN {{warn.line}}:{{warn.col}} — {{warn.msg}}</div>
            </div>
            <pre v-if="ladderCompileDiagnostics.raw && !ladderCompileDiagnostics.errors.length && !ladderCompileDiagnostics.warnings.length" class="whitespace-pre-wrap text-slate-400 mono max-h-36 overflow-auto">{{ ladderCompileDiagnostics.raw }}</pre>
          </div>
        </section>

        <section class="rounded-xl border border-slate-800 bg-slate-950/45 overflow-hidden">
          <div class="px-4 py-3 border-b border-slate-800 flex flex-wrap items-center justify-between gap-2">
            <h3 class="font-bold text-slate-200">PLC Ladder Files</h3>
            <button @click="refreshLadderFiles" :disabled="ladderFileBusy" class="btn px-3 py-1.5 rounded-lg bg-slate-800 border border-slate-600 text-xs disabled:opacity-40">Refresh List</button>
          </div>
          <div class="max-h-[260px] overflow-auto scrollbar">
            <table class="w-full text-xs">
              <thead class="bg-black/30 text-slate-500 uppercase text-[10px] tracking-widest sticky top-0">
                <tr>
                  <th class="text-left p-3">Name</th>
                  <th class="text-right p-3">Size</th>
                  <th class="text-left p-3">Path</th>
                </tr>
              </thead>
              <tbody>
                <tr v-if="!ladderFiles.length">
                  <td colspan="3" class="p-4 text-slate-500">No ladder JSON files found in /ladder.</td>
                </tr>
                <tr v-for="file in ladderFiles" :key="file.path" @click="selectLadderFile(file)" @dblclick="loadLadderFile(file.path)" class="border-t border-slate-800 cursor-pointer hover:bg-slate-800/45" :class="selectedLadderFile===file.path ? 'bg-cyan-500/10' : ''">
                  <td class="p-3 font-mono text-slate-200">📄 {{ file.name }}</td>
                  <td class="p-3 text-right font-mono text-slate-400">{{ file.size || 0 }}</td>
                  <td class="p-3 font-mono text-slate-500 truncate">{{ file.path }}</td>
                </tr>
              </tbody>
            </table>
          </div>
        </section>

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
            <h3 class="font-bold text-slate-200">Validation Summary</h3>
            <div class="flex flex-wrap gap-2 text-xs">
              <span class="px-2 py-1 rounded border" :class="validationSummary.errors ? 'border-red-300/40 bg-red-500/10 text-red-200' : 'border-emerald-300/35 bg-emerald-500/10 text-emerald-200'">{{validationSummary.errors}} errors</span>
              <span class="px-2 py-1 rounded border" :class="validationSummary.warnings ? 'border-amber-300/40 bg-amber-500/10 text-amber-200' : 'border-slate-700 bg-slate-900 text-slate-400'">{{validationSummary.warnings}} warnings</span>
              <span class="px-2 py-1 rounded border border-blue-300/30 bg-blue-500/10 text-blue-200">{{validationSummary.info}} info</span>
            </div>
            <button @click="activeView='ladder'" class="btn px-3 py-1.5 rounded-lg bg-slate-800 border border-slate-700 text-xs">Go to Ladder</button>
          </section>

          <section class="rounded-xl border border-slate-800 bg-slate-950/45 p-4 space-y-3">
            <div class="flex flex-wrap items-center justify-between gap-2">
              <h3 class="font-bold text-slate-200">Discovered Tags</h3>
              <button @click="syncDiscoveredTagsToTagStore" class="btn px-3 py-1.5 rounded-lg bg-emerald-500/10 border border-emerald-300/35 text-emerald-200 text-xs" title="Merge tags discovered from this ladder project into the Web app Tag Registry memory store. Use the Tags page to edit/save them to the PLC.">Sync Tags</button>
            </div>
            <div class="text-xs text-slate-500">{{projectReview().tagCount}} ladder-discovered user tags. Review and save them from the Web app Tags page.</div>
          </section>
        </div>

        <section class="rounded-xl border border-slate-800 bg-slate-950/45 p-4 space-y-3">
          <h3 class="font-bold text-slate-200">Project Metadata</h3>
          <div class="grid grid-cols-1 xl:grid-cols-[1fr_220px] gap-3">
            <label class="block text-xs text-slate-400 space-y-1">
              <span>Project name</span>
              <input v-model="project.name" @change="show('Project name updated')" class="w-full px-3 py-2 rounded-lg bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-cyan-400" />
            </label>
            <label class="block text-xs text-slate-400 space-y-1">
              <span>Scan time (ms)</span>
              <input type="number" min="1" step="1" v-model.number="project.scan_ms" class="w-full px-3 py-2 rounded-lg bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-cyan-400" />
            </label>
          </div>
          <label class="block text-xs text-slate-400 space-y-1">
            <span>Description</span>
            <textarea v-model="project.description" rows="3" placeholder="Describe what this ladder file does." class="w-full px-3 py-2 rounded-lg bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-cyan-400 resize-y"></textarea>
          </label>
        </section>

        <section class="rounded-xl border border-slate-800 bg-slate-950/45 p-4 space-y-3">
          <div class="flex flex-wrap items-center justify-between gap-2">
            <h3 class="font-bold text-slate-200">Advanced Backup / Transfer</h3>
            <div class="flex flex-wrap gap-2">
              <button @click="$refs.projectBundleImport.click()" class="btn px-3 py-2 rounded-lg bg-indigo-500/10 border border-indigo-300/35 text-indigo-200 text-xs whitespace-nowrap">Import Bundle</button>
              <input ref="projectBundleImport" type="file" accept=".zip,application/zip" class="hidden" @change="importProjectBundleFile"/>
              <button @click="downloadProjectBundle" class="btn px-3 py-2 rounded-lg bg-cyan-500/10 border border-cyan-300/35 text-cyan-200 text-xs whitespace-nowrap">Export Bundle</button>
            </div>
          </div>
          <div class="text-xs text-slate-500 leading-relaxed">
            Bundle export/import is for backup, sharing, or moving work to another P4. Day-to-day ladder programs should be saved as JSON files in <span class="mono">/ladder</span>.
          </div>
          <div class="rounded-lg bg-black/30 border border-slate-800 p-3 mono text-xs text-slate-300 whitespace-pre-wrap">{{projectBundleManifestText()}}</div>
        </section>
      </div>
      <div v-show="activeView==='ladder'" class="canvas-bg overflow-y-auto overflow-x-hidden scrollbar min-h-0 relative">
        <div v-if="ladderUploadStatus || ladderCompileDiagnostics.errors.length || ladderCompileDiagnostics.warnings.length || ladderCompileDiagnostics.raw" class="absolute top-4 right-4 z-20 w-[min(520px,calc(100%-2rem))] rounded-2xl border border-slate-700 bg-slate-950/95 shadow-2xl p-4 space-y-3">
          <div class="flex items-start justify-between gap-3">
            <div>
              <h3 class="font-bold text-slate-100 text-sm">PLC Upload Status</h3>
              <div v-if="ladderUploadStatus" :class="ladderUploadStatusClass" class="text-xs mt-1 leading-relaxed">{{ ladderUploadStatus }}</div>
            </div>
            <button @click="ladderUploadStatus=''; clearLadderCompileDiagnostics()" class="btn px-2 py-1 rounded-lg bg-slate-800 border border-slate-700 text-[11px] text-slate-300">Close</button>
          </div>
          <div v-if="ladderCompileDiagnostics.errors.length || ladderCompileDiagnostics.warnings.length || ladderCompileDiagnostics.raw" class="rounded-lg border border-slate-800 bg-black/25 p-3 text-xs space-y-2 max-h-56 overflow-auto scrollbar">
            <div v-if="ladderCompileDiagnostics.errors.length" class="space-y-1">
              <div v-for="(err, idx) in ladderCompileDiagnostics.errors" :key="'le'+idx" class="text-red-300 mono">ERR {{err.line}}:{{err.col}} — {{err.msg}}</div>
            </div>
            <div v-if="ladderCompileDiagnostics.warnings.length" class="space-y-1">
              <div v-for="(warn, idx) in ladderCompileDiagnostics.warnings" :key="'lw'+idx" class="text-amber-300 mono">WARN {{warn.line}}:{{warn.col}} — {{warn.msg}}</div>
            </div>
            <pre v-if="ladderCompileDiagnostics.raw && !ladderCompileDiagnostics.errors.length && !ladderCompileDiagnostics.warnings.length" class="whitespace-pre-wrap text-slate-400 mono">{{ ladderCompileDiagnostics.raw }}</pre>
          </div>
        </div>

        <div class="p-4 space-y-4">
          <section class="rounded-xl border border-slate-800 bg-slate-950/70 p-3 space-y-3">
            <div class="grid grid-cols-1 xl:grid-cols-[minmax(220px,320px)_1fr_120px] gap-3 items-start">
              <label class="block text-xs text-slate-400 space-y-1">
                <span>File name</span>
                <input v-model="ladderFileNameDraft" @keydown.enter.prevent="downloadJson" class="w-full px-3 py-2 rounded-lg bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-cyan-400 mono" placeholder="main.piLadder" />
              </label>
              <label class="block text-xs text-slate-400 space-y-1">
                <span>Description</span>
                <input v-model="project.description" class="w-full px-3 py-2 rounded-lg bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-cyan-400" placeholder="Describe what this ladder file does." />
              </label>
              <label class="block text-xs text-slate-400 space-y-1">
                <span>Scan ms</span>
                <input type="number" min="1" step="1" v-model.number="project.scan_ms" class="w-full px-3 py-2 rounded-lg bg-slate-950 border border-slate-700 text-slate-200 outline-none focus:border-cyan-400" />
              </label>
            </div>
          </section>

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
                 @click="canvasBlankClick"
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


      <div class="px-4 py-2 border-t border-slate-800 flex gap-6 text-xs overflow-hidden">
        <span><b class="text-emerald-300">Scan:</b> {{project.scan_ms}} ms</span>
        <span><b>Rungs:</b> {{project.rungs.length}}</span>
        <span><b>Branches:</b> {{branchCount()}}</span>
        <span><b>Symbols:</b> {{allElements().length}}</span>
        <span><b>Discovered Tags:</b> {{discoverTagRegistryEntries().length}}</span>
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
import SymbolRender from '../components/ladder/SymbolRender.vue';
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
import { ladderModelMethods } from '../ladder/ladderModel.js';
import { ladderValidationMethods } from '../ladder/ladderValidation.js';
import { ladderSimulatorMethods } from '../ladder/ladderSimulator.js';
import { ladderTranspilerMethods } from '../ladder/ladderTranspiler.js';
import { ladderTagRegistryMethods } from '../ladder/ladderTagRegistry.js';
import { addFormalSchemaMetadata } from '../ladder/ladderSchema.js';
import { ensureTagStoreLoaded, mergeTagRowsIntoStore, useTagStore } from '../stores/tagStore';
import { useLadderStore, saveLadderEditorSnapshot, restoreLadderEditorSnapshot, markLadderProjectDirty, markLadderProjectSaved, setLadderCurrentFile } from '../stores/ladderStore';
import { listFiles, mkdir as mkdirApi, uploadFile, viewFile as viewFileApi, deletePath as deletePathApi } from '../api/fileApi';
import { uploadScriptText, getScriptStatus } from '../api/scriptApi';
import { usePlcStore } from '../stores/plcStore';
import { confirmDialog, messageDialog } from '../stores/appDialog';

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
      activeView:'ladder',
      showCodeTabs:false,
      viewTabs:[
        {id:'ladder', label:'Ladder'},
        {id:'json', label:'JSON'},
        {id:'angelscript', label:'AngelScript'},
        {id:'javascript', label:'JavaScript'}
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
      tagStore: useTagStore(),
      ladderStore: useLadderStore(),
      plcStore: usePlcStore(),
      ladderFiles: [],
      selectedLadderFile: '',
      ladderFileNameDraft: '',
      ladderFileStatus: 'Ladder files not loaded yet.',
      ladderFileBusy: false,
      ladderUploadStatus: '',
      ladderUploadBusy: false,
      ladderCompileDiagnostics: { errors: [], warnings: [], raw: '', state: '' },
      suppressLadderDirty: false,
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
    tagRegistryEdits:{
      get(){ return this.tagStore.edits; },
      set(v){ this.tagStore.edits = v || {}; }
    },
    tagRegistryImported:{
      get(){ return this.tagStore.imported; },
      set(v){ this.tagStore.imported = v || {}; }
    },
    ladderDirty(){ return !!this.ladderStore.dirty; },
    currentLadderFilePath(){ return this.ladderStore.currentFilePath || ''; },
    currentLadderFileLabel(){
      return this.ladderStore.currentFileName ? this.ladderStore.currentFileName : 'Untitled ladder';
    },
    visibleViewTabs(){
      return this.showCodeTabs ? this.viewTabs : this.viewTabs.filter(tab => tab.id === 'ladder');
    },
    flashWritesAllowedNow(){
      return this.plcStore && this.plcStore.flashWritesAllowed ? this.plcStore.flashWritesAllowed.value !== false : true;
    },
    ladderFileStatusClass(){
      const s = String(this.ladderFileStatus || '');
      if(s.startsWith('Error')) return 'text-red-300';
      if(s.startsWith('Saved') || s.startsWith('Loaded') || s.startsWith('Deleted') || s.startsWith('Created')) return 'text-emerald-300';
      if(s.startsWith('Warning')) return 'text-amber-300';
      return 'text-slate-400';
    },
    ladderUploadStatusClass(){
      const s = String(this.ladderUploadStatus || '');
      if(s.startsWith('Error') || s.startsWith('Compile failed')) return 'text-red-300';
      if(s.startsWith('Warning') || s.startsWith('Upload accepted')) return 'text-amber-300';
      if(s.startsWith('Uploaded') || s.startsWith('Compile OK')) return 'text-emerald-300';
      return 'text-slate-400';
    },
    activeViewDescription(){
      if(this.activeView==='project') return 'Save, load, and review ladder JSON files stored on the P4 PLC.';
      if(this.activeView==='json') return 'Editable JSON project model. Change it and leave the field to apply.';
      if(this.activeView==='angelscript') return 'Generated AngelScript target. Enable Add Tags to include optional standalone test globals.';
      if(this.activeView==='javascript') return 'Generated JavaScript target used by the browser simulator.';
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
    if(!restoreLadderEditorSnapshot(this)) {
      this.suppressLadderDirty = true;
      this.project = this.blankLadderProject();
      this.jsonDraft = this.jsonModel;
      setLadderCurrentFile('', '');
      markLadderProjectSaved();
      saveLadderEditorSnapshot(this);
      nextTick(() => { this.suppressLadderDirty = false; markLadderProjectSaved(); saveLadderEditorSnapshot(this); });
    }
    this.activeView = 'ladder';
    this.showCodeTabs = false;
    this.ladderFileNameDraft = this.ladderFileNameDraft || this.ladderStore.currentFileName || 'main.piLadder';
    this.selectedLadderFile = this.ladderStore.currentFilePath || '';
    this.refreshLadderFiles().catch(() => {});
    ensureTagStoreLoaded().catch(() => {});
    window.addEventListener('keydown', this.onLadderKeyDown);
    nextTick(() => this.initPrismOutputEditors());
  },
  beforeUnmount(){
    window.removeEventListener('keydown', this.onLadderKeyDown);
    saveLadderEditorSnapshot(this);
    this.destroyPrismOutputEditors();
  },
  watch:{
    project:{
      deep:true,
      handler(){
        this.jsonDraft=this.jsonModel;
        this.refreshPrismOutputEditors();
        if(!this.suppressLadderDirty) markLadderProjectDirty();
        saveLadderEditorSnapshot(this);
      }
    },
    activeView(){
      saveLadderEditorSnapshot(this);
      nextTick(() => this.refreshPrismOutputEditors());
    },
    includeAngelScriptTags(){
      this.refreshPrismOutputEditors();
      saveLadderEditorSnapshot(this);
    },
    showCodeTabs(){
      if(!this.showCodeTabs && ['json','angelscript','javascript'].includes(this.activeView)) this.activeView = 'ladder';
      saveLadderEditorSnapshot(this);
    },
    ladderFileNameDraft(newName, oldName){
      if(newName === oldName) return;
      setLadderCurrentFile(String(newName || '').trim(), '');
      if(!this.suppressLadderDirty) markLadderProjectDirty();
      saveLadderEditorSnapshot(this);
    }
  },
  methods:{
    ...ladderModelMethods,
    ...ladderValidationMethods,
    ...ladderSimulatorMethods,
    ...ladderTranspilerMethods,
    ...ladderTagRegistryMethods,
    toggleCodeTabs(){
      this.showCodeTabs = !this.showCodeTabs;
      if(!this.showCodeTabs && ['json','angelscript','javascript'].includes(this.activeView)) this.activeView = 'ladder';
      nextTick(() => this.refreshPrismOutputEditors());
    },
    isEditableKeyboardTarget(target){
      if(!target) return false;
      const tag = String(target.tagName || '').toLowerCase();
      if(tag === 'input' || tag === 'textarea' || tag === 'select') return true;
      if(target.isContentEditable) return true;
      if(target.closest && target.closest('.prism-code-editor, .pce-wrapper, [contenteditable="true"]')) return true;
      return false;
    },
    onLadderKeyDown(event){
      if(!event) return;
      const key = String(event.key || '').toLowerCase();
      const wantsUndo = (event.ctrlKey || event.metaKey) && !event.altKey && !event.shiftKey && key === 'z';
      if(!wantsUndo) return;
      if(this.isEditableKeyboardTarget(event.target)) return;
      event.preventDefault();
      this.undo();
    },
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
      this.prismEditors.angelscript = makeReadonly(this.$refs.angelScriptEditorHost, 'cpp', this.generatedAngelScriptSource(this.includeAngelScriptTags));
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
      this.prismEditors.angelscript?.setOptions({ value:this.generatedAngelScriptSource(this.includeAngelScriptTags) });
      this.prismEditors.javascript?.setOptions({ value:this.transpileJavaScript() });
    },
    destroyPrismOutputEditors(){
      for(const key of ['json','angelscript','javascript']){
        try{ this.prismEditors?.[key]?.remove?.(); } catch(_){}
      }
      this.prismEditors = { json:null, angelscript:null, javascript:null };
      this.prismEditorsReady = false;
    },
    escapePrintHtml(value){
      return String(value ?? '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
    },
    syntaxHighlightAngelScript(code){
      const keywords = /\b(if|else|for|while|do|switch|case|break|continue|return|class|void|bool|int|uint|float|double|string|auto|const|true|false|null|and|or|not)\b/g;
      const numbers = /\b(\d+(?:\.\d+)?)\b/g;
      return String(code || '').split(/\r?\n/).map((rawLine) => {
        let line = this.escapePrintHtml(rawLine);
        const trimmed = line.trim();
        if(trimmed.startsWith('//')) return '<span class="tok-comment">' + line + '</span>';
        line = line.replace(/(&quot;.*?&quot;|&#39;.*?&#39;)/g, '<span class="tok-string">$1</span>');
        line = line.replace(keywords, '<span class="tok-keyword">$1</span>');
        line = line.replace(numbers, '<span class="tok-number">$1</span>');
        const idx = line.indexOf('//');
        if(idx >= 0) line = line.slice(0, idx) + '<span class="tok-comment">' + line.slice(idx) + '</span>';
        return line;
      }).join('\n');
    },
    printBlockColor(t){ return (t==='TON'||t==='TOF') ? '#a16207' : (t==='ONS' ? '#0369a1' : '#7e22ce'); },
    printCoilColor(t){ return t==='SET' ? '#1d4ed8' : (t==='RST' ? '#dc2626' : '#15803d'); },
    printPresetLabel(el){
      if(!el) return '';
      if(el.type==='TON'||el.type==='TOF') return `${el.preset || 1000}ms`;
      if(el.type==='CTU'||el.type==='CTD') return `PV ${el.preset || 10}`;
      if(el.type==='ONS') return 'one scan';
      return '';
    },
    printSymbolLeadBounds(el, x){
      if(!el) return null;
      if(['TON','TOF','CTU','CTD','ONS'].includes(el.type)) return { left:x-31, right:x+31 };
      return { left:x-30, right:x+30 };
    },
    printCircuitSlotSvg(cells, slot, y, wire){
      const leftNode = this.nodeX(slot);
      const rightNode = this.nodeX(slot + 1);
      const el = cells && cells[slot];
      if(!el) return `<line x1="${leftNode}" y1="${y}" x2="${rightNode}" y2="${y}" stroke="${wire}" stroke-width="3" stroke-linecap="round"/>`;
      const x = this.slotCenterX(slot);
      const leads = this.printSymbolLeadBounds(el, x);
      return `<line x1="${leftNode}" y1="${y}" x2="${leads.left}" y2="${y}" stroke="${wire}" stroke-width="3" stroke-linecap="round"/><line x1="${leads.right}" y1="${y}" x2="${rightNode}" y2="${y}" stroke="${wire}" stroke-width="3" stroke-linecap="round"/>`;
    },
    printCircuitSlotsSvg(cells, start, end, y, wire){
      const out = [];
      for(let slot = start; slot < end; slot++) out.push(this.printCircuitSlotSvg(cells || [], slot, y, wire));
      return out.join('');
    },
    printCopyAttr(text){
      return encodeURIComponent(String(text || ''));
    },
    printElementText(el){
      if(!el) return '';
      const tag = el.tag || '(untagged)';
      const preset = this.printPresetLabel(el);
      if(['TON','TOF','CTU','CTD','ONS'].includes(el.type)) {
        return `[${el.type} ${tag}${preset ? ' ' + preset : ''}]`;
      }
      return `[${el.type || '?'} ${tag}]`;
    },
    printCellRangeText(cells, start = 0, end = 8){
      const parts = [];
      for(let slot = start; slot < end; slot++) {
        const text = this.printElementText(cells && cells[slot]);
        if(text) parts.push(text);
      }
      return parts.length ? parts.join(' -- ') : '(empty)';
    },
    printRungCopyText(rung, index){
      const kind = rung && rung.kind === 'script' ? 'AngelScript rung' : 'Ladder rung';
      const lines = [`Rung ${index + 1} - ${kind}`];
      if(rung && rung.comment) lines.push(`Comment: ${rung.comment}`);
      if(rung && rung.kind === 'script') {
        lines.push('', String(rung.code || '').trimEnd());
        return lines.join('\n');
      }
      lines.push(`Main: ${this.printCellRangeText((rung && rung.main) || [], 0, 8)}`);
      const branches = (rung && rung.branches) || [];
      branches.forEach((br, i) => {
        lines.push(`Branch ${i + 1} (${br.start} to ${br.end}): ${this.printCellRangeText(br.cells || [], br.start, br.end)}`);
      });
      return lines.join('\n');
    },
    printTagRole(entry){
      if(!entry) return 'Internal';
      if(entry.output) return 'Output';
      if(entry.hmi || /^HMI_/i.test(entry.name || '')) return 'HMI/Input';
      if(entry.memory || /^M_/i.test(entry.name || '')) return 'Memory';
      if(entry.writes && !entry.reads) return 'Write';
      if(entry.reads && !entry.writes) return 'Read';
      return 'Read/Write';
    },
    printFormatTagXrefSource(source){
      const s = String(source || '').trim();
      let m = s.match(/^Rung\s+(\d+)\s+main\s+slot\s+(\d+)$/i);
      if(m) return `R${m[1]} Main S${m[2]}`;
      m = s.match(/^Rung\s+(\d+)\s+branch\s+(\d+)\s+slot\s+(\d+)$/i);
      if(m) return `R${m[1]} Br${m[2]} S${m[3]}`;
      m = s.match(/^Rung\s+(\d+)\s+line\s+(\d+)$/i);
      if(m) return `R${m[1]} Line ${m[2]}`;
      m = s.match(/^Rung\s+(\d+)$/i);
      if(m) return `R${m[1]}`;
      return s || '—';
    },
    printFormatTagXrefContext(context, access){
      const c = String(context || '').trim();
      const a = String(access || '').trim();
      const contextMap = {
        NO: 'NO contact',
        NC: 'NC contact',
        OUT: 'coil',
        SET: 'set coil',
        RST: 'reset coil',
        'counter-control': 'counter control',
        'script-write': 'script write',
        'script-expression': 'script expression',
        'script-condition': 'script condition',
        'script-incdec': 'script inc/dec',
        'script-local-init': 'script init',
        script: 'script'
      };
      const label = contextMap[c] || c;
      if(label && a) return `${label}, ${a}`;
      return label || a || 'used';
    },
    printTagUsedInHtml(entry){
      const xrefs = Array.isArray(entry && entry.xrefs) ? entry.xrefs : [];
      if(xrefs.length){
        const lines = xrefs.map(x => {
          const where = this.escapePrintHtml(this.printFormatTagXrefSource(x.source));
          const detail = this.escapePrintHtml(this.printFormatTagXrefContext(x.context, x.access));
          return `<div class="xref-line"><span class="xref-where">${where}</span><span class="xref-detail">${detail}</span></div>`;
        }).join('');
        return `<div class="xref-list">${lines}</div>`;
      }
      const sources = entry && entry.sources instanceof Set ? [...entry.sources] : (Array.isArray(entry && entry.sources) ? entry.sources : []);
      if(!sources.length) return '—';
      return `<div class="xref-list">${sources.map(source => `<div class="xref-line"><span class="xref-where">${this.escapePrintHtml(this.printFormatTagXrefSource(source))}</span></div>`).join('')}</div>`;
    },
    printTagAppendixHtml(entries){
      if(!entries || !entries.length) return '';
      const rows = entries.map(entry => {
        const row = this.normalizeTagRegistryRow(this.getTagRegistryOverride(entry.name) || this.defaultTagRegistryRow(entry));
        const usedIn = this.printTagUsedInHtml(entry);
        return `<tr><td class="mono">${this.escapePrintHtml(row.name)}</td><td>${this.escapePrintHtml(row.type)}</td><td>${row.retentive ? 'Yes' : 'No'}</td><td>${row.writable ? 'Yes' : 'No'}</td><td class="used-in">${usedIn}</td><td>${this.escapePrintHtml(row.description || '')}</td></tr>`;
      }).join('');
      return `<section class="tag-appendix"><div class="appendix-title">Tag Reference Appendix</div><table><thead><tr><th>Tag</th><th>Type</th><th>Retentive</th><th>Writable</th><th>Used In</th><th>Description</th></tr></thead><tbody>${rows}</tbody></table></section>`;
    },
    printSymbolSvg(el, x, y){
      if(!el) return '';
      const tag = this.escapePrintHtml(el.tag || '');
      if(el.type === 'NO') {
        return `<g><line x1="${x-30}" y1="${y}" x2="${x-15}" y2="${y}" stroke="#111827" stroke-width="2"/><line x1="${x+15}" y1="${y}" x2="${x+30}" y2="${y}" stroke="#111827" stroke-width="2"/><line x1="${x-15}" y1="${y-20}" x2="${x-15}" y2="${y+20}" stroke="#0369a1" stroke-width="3"/><line x1="${x+15}" y1="${y-20}" x2="${x+15}" y2="${y+20}" stroke="#0369a1" stroke-width="3"/><text x="${x}" y="${y+41}" text-anchor="middle" fill="#111827" font-size="11" font-family="ui-monospace, SFMono-Regular, Menlo, Consolas, monospace">${tag}</text></g>`;
      }
      if(el.type === 'NC') {
        return `<g><line x1="${x-30}" y1="${y}" x2="${x-15}" y2="${y}" stroke="#111827" stroke-width="2"/><line x1="${x+15}" y1="${y}" x2="${x+30}" y2="${y}" stroke="#111827" stroke-width="2"/><line x1="${x-15}" y1="${y-20}" x2="${x-15}" y2="${y+20}" stroke="#b91c1c" stroke-width="3"/><line x1="${x+15}" y1="${y-20}" x2="${x+15}" y2="${y+20}" stroke="#b91c1c" stroke-width="3"/><line x1="${x-22}" y1="${y+20}" x2="${x+22}" y2="${y-20}" stroke="#b91c1c" stroke-width="2"/><text x="${x}" y="${y+41}" text-anchor="middle" fill="#111827" font-size="11" font-family="ui-monospace, SFMono-Regular, Menlo, Consolas, monospace">${tag}</text></g>`;
      }
      if(['OUT','SET','RST'].includes(el.type)) {
        const color = this.printCoilColor(el.type);
        const label = el.type === 'SET' ? 'S' : (el.type === 'RST' ? 'R' : '');
        return `<g><line x1="${x-30}" y1="${y}" x2="${x-17}" y2="${y}" stroke="#111827" stroke-width="2"/><line x1="${x+17}" y1="${y}" x2="${x+30}" y2="${y}" stroke="#111827" stroke-width="2"/><path d="M ${x-10} ${y-21} Q ${x-24} ${y} ${x-10} ${y+21}" fill="none" stroke="${color}" stroke-width="3"/><path d="M ${x+10} ${y-21} Q ${x+24} ${y} ${x+10} ${y+21}" fill="none" stroke="${color}" stroke-width="3"/>${label ? `<text x="${x}" y="${y+4}" text-anchor="middle" fill="${color}" font-size="13" font-weight="900">${label}</text>` : ''}<text x="${x}" y="${y+41}" text-anchor="middle" fill="#111827" font-size="11" font-family="ui-monospace, SFMono-Regular, Menlo, Consolas, monospace">${tag}</text></g>`;
      }
      const color = this.printBlockColor(el.type);
      const preset = this.escapePrintHtml(this.printPresetLabel(el));
      const type = this.escapePrintHtml(el.type || '');
      return `<g><rect x="${x-31}" y="${y-24}" width="62" height="48" rx="8" fill="#ffffff" stroke="${color}" stroke-width="2"/><text x="${x}" y="${y-4}" text-anchor="middle" fill="${color}" font-size="13" font-weight="800">${type}</text><text x="${x}" y="${y+9}" text-anchor="middle" fill="#4b5563" font-size="9">${preset}</text><text x="${x}" y="${y+41}" text-anchor="middle" fill="#111827" font-size="11" font-family="ui-monospace, SFMono-Regular, Menlo, Consolas, monospace">${tag}</text></g>`;
    },
    printLadderRungSvg(rung){
      const h = this.rungHeight(rung);
      const rail = '#111827';
      const wire = '#374151';
      let out = [];
      out.push(`<svg class="print-ladder-svg" viewBox="0 0 ${this.canvasW} ${h}" preserveAspectRatio="xMinYMin meet" role="img" aria-label="Ladder rung diagram">`);
      out.push(`<rect x="0" y="0" width="${this.canvasW}" height="${h}" rx="10" fill="#ffffff"/>`);
      out.push(`<line x1="${this.railLeft}" y1="24" x2="${this.railLeft}" y2="${h-24}" stroke="${rail}" stroke-width="3"/>`);
      out.push(`<line x1="${this.railRight}" y1="24" x2="${this.railRight}" y2="${h-24}" stroke="${rail}" stroke-width="3"/>`);
      out.push(`<line x1="${this.railLeft}" y1="${this.mainY}" x2="${this.nodeX(0)}" y2="${this.mainY}" stroke="${wire}" stroke-width="3" stroke-linecap="round"/>`);
      out.push(this.printCircuitSlotsSvg(rung.main || [], 0, 8, this.mainY, wire));
      out.push(`<line x1="${this.nodeX(8)}" y1="${this.mainY}" x2="${this.railRight}" y2="${this.mainY}" stroke="${wire}" stroke-width="3" stroke-linecap="round"/>`);
      for(const br of (rung.branches || [])) {
        const by = this.branchY(rung, br);
        out.push(`<line x1="${this.nodeX(br.start)}" y1="${this.mainY}" x2="${this.nodeX(br.start)}" y2="${by}" stroke="${wire}" stroke-width="3" stroke-linecap="round"/>`);
        out.push(`<line x1="${this.nodeX(br.end)}" y1="${this.mainY}" x2="${this.nodeX(br.end)}" y2="${by}" stroke="${wire}" stroke-width="3" stroke-linecap="round"/>`);
        if(br.start === 0) out.push(`<line x1="${this.railLeft}" y1="${by}" x2="${this.nodeX(0)}" y2="${by}" stroke="${wire}" stroke-width="3" stroke-linecap="round"/>`);
        if(br.end === 8) out.push(`<line x1="${this.nodeX(8)}" y1="${by}" x2="${this.railRight}" y2="${by}" stroke="${wire}" stroke-width="3" stroke-linecap="round"/>`);
        out.push(this.printCircuitSlotsSvg(br.cells || [], br.start, br.end, by, wire));
        out.push(`<circle cx="${this.nodeX(br.start)}" cy="${this.mainY}" r="4.2" fill="#374151"/><circle cx="${this.nodeX(br.end)}" cy="${this.mainY}" r="4.2" fill="#374151"/><circle cx="${this.nodeX(br.start)}" cy="${by}" r="3.8" fill="#374151"/><circle cx="${this.nodeX(br.end)}" cy="${by}" r="3.8" fill="#374151"/>`);
      }
      out.push(`<circle cx="${this.railLeft}" cy="${this.mainY}" r="4" fill="${rail}"/><circle cx="${this.railRight}" cy="${this.mainY}" r="4" fill="${rail}"/>`);
      for(const n of this.nodes) out.push(`<circle cx="${this.nodeX(n)}" cy="${this.mainY}" r="3.2" fill="#4b5563"/>`);
      for(const slot of this.slots) if(rung.main && rung.main[slot]) out.push(this.printSymbolSvg(rung.main[slot], this.slotCenterX(slot), this.mainY));
      for(const br of (rung.branches || [])) {
        const by = this.branchY(rung, br);
        for(const slot of this.branchSlots(br)) if(br.cells && br.cells[slot]) out.push(this.printSymbolSvg(br.cells[slot], this.slotCenterX(slot), by));
      }
      out.push('</svg>');
      return out.join('');
    },
    openLadderPrintView(){
      const project = this.project || {};
      const generatedAt = new Date().toLocaleString();
      const ladderCount = (project.rungs || []).filter(r => r.kind !== 'script').length;
      const scriptCount = (project.rungs || []).filter(r => r.kind === 'script').length;
      const fileName = this.ladderStore.currentFileName || this.ladderFileNameDraft || 'Unsaved ladder project';
      const desc = String(project.description || '').trim();
      const tagEntries = this.discoverTagRegistryEntries();
      const tagAppendix = this.printTagAppendixHtml(tagEntries);
      const sections = (project.rungs || []).map((rung, idx) => {
        const comment = this.escapePrintHtml(rung.comment || 'No comment');
        const kind = rung.kind === 'script' ? 'AngelScript rung' : 'Ladder rung';
        if(rung.kind === 'script') {
          const copyText = this.printCopyAttr(this.printRungCopyText(rung, idx));
          return `<section class="print-rung script-rung"><div class="rung-head"><div><span class="rung-index">Rung ${idx + 1}</span><span class="rung-kind">${kind}</span></div><div class="rung-comment">${comment}</div><div class="rung-actions"><button class="copy-rung" type="button" data-copy="${copyText}" onclick="copyPrintRungImage(this)">Copy Image</button></div></div><pre class="script-code"><code>${this.syntaxHighlightAngelScript(rung.code || '')}</code></pre></section>`;
        }
        const copyText = this.printCopyAttr(this.printRungCopyText(rung, idx));
        return `<section class="print-rung"><div class="rung-head"><div><span class="rung-index">Rung ${idx + 1}</span><span class="rung-kind">${kind}</span></div><div class="rung-comment">${comment}</div><div class="rung-actions"><button class="copy-rung" type="button" data-copy="${copyText}" onclick="copyPrintRungImage(this)">Copy Image</button></div></div>${this.printLadderRungSvg(rung)}</section>`;
      }).join('');
      const html = `<!doctype html><html><head><meta charset="utf-8"><title>${this.escapePrintHtml(project.name || 'PiLab Ladder Print')}</title><style>
        :root { color-scheme: light; }
        * { box-sizing: border-box; }
        body { margin: 0; background: #e5e7eb; color: #111827; font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
        .toolbar { position: sticky; top: 0; z-index: 10; display: flex; justify-content: space-between; align-items: center; gap: 18px; padding: 12px 20px; background: #111827; color: #f9fafb; box-shadow: 0 8px 24px rgba(15, 23, 42, .22); }
        .toolbar-title { font-weight: 900; }
        .toolbar-note { margin-top: 2px; font-size: 12px; color: #cbd5e1; }
        .toolbar-actions { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; justify-content: flex-end; }
        .toolbar label { display: inline-flex; align-items: center; gap: 6px; font-size: 12px; color: #e5e7eb; white-space: nowrap; }
        .toolbar input { accent-color: #38bdf8; }
        .toolbar button { border: 1px solid rgba(255,255,255,.28); background: #ffffff; color: #111827; border-radius: 12px; padding: 9px 14px; font-weight: 800; cursor: pointer; }
        .paper { max-width: 1040px; margin: 24px auto; background: #ffffff; box-shadow: 0 24px 70px rgba(15,23,42,.18); border-radius: 18px; overflow: hidden; }
        .doc-header { padding: 34px 42px 24px; border-bottom: 1px solid #d1d5db; background: linear-gradient(135deg, #ffffff 0%, #f8fafc 58%, #eef2ff 100%); }
        .brand { display: flex; align-items: center; justify-content: space-between; gap: 16px; margin-bottom: 22px; }
        .brand-mark { letter-spacing: .20em; font-size: 11px; font-weight: 900; color: #1d4ed8; text-transform: uppercase; }
        h1 { margin: 0; font-size: 30px; line-height: 1.15; letter-spacing: -0.03em; }
        .subtitle { margin-top: 7px; color: #4b5563; font-size: 13px; }
        .description { margin: 16px 0 0; max-width: 860px; color: #374151; line-height: 1.5; }
        .print-advice { margin-top: 16px; padding: 10px 12px; border: 1px solid #bfdbfe; border-radius: 12px; background: #eff6ff; color: #1e3a8a; font-size: 12px; line-height: 1.45; }
        .meta-grid { display: grid; grid-template-columns: repeat(5, minmax(0, 1fr)); gap: 10px; padding: 18px 42px; border-bottom: 1px solid #e5e7eb; background: #f9fafb; }
        .meta { border: 1px solid #e5e7eb; background: #ffffff; border-radius: 12px; padding: 10px 12px; min-width: 0; }
        .meta-label { color: #6b7280; font-size: 10px; font-weight: 900; letter-spacing: .12em; text-transform: uppercase; }
        .meta-value { margin-top: 3px; font-size: 13px; font-weight: 800; overflow-wrap: anywhere; }
        .content { padding: 22px 42px 36px; }
        .print-rung { break-inside: avoid; page-break-inside: avoid; margin: 0 0 20px; border: 1px solid #d1d5db; border-radius: 14px; overflow: hidden; background: #ffffff; }
        .rung-head { display: grid; grid-template-columns: 190px 1fr auto; gap: 14px; align-items: start; padding: 12px 14px; border-bottom: 1px solid #e5e7eb; background: #f8fafc; }
        .rung-index { display: inline-block; font-weight: 900; color: #111827; margin-right: 8px; }
        .rung-kind { display: inline-block; font-size: 10px; font-weight: 900; color: #1d4ed8; text-transform: uppercase; letter-spacing: .11em; }
        .rung-comment { color: #374151; font-size: 13px; font-weight: 650; line-height: 1.4; }
        .rung-actions { display: flex; justify-content: flex-end; }
        .copy-rung { border: 1px solid #cbd5e1; background: #ffffff; color: #1f2937; border-radius: 10px; padding: 6px 10px; font-size: 11px; font-weight: 850; cursor: pointer; white-space: nowrap; box-shadow: 0 1px 2px rgba(15,23,42,.06); }
        .copy-rung:hover { border-color: #60a5fa; color: #1d4ed8; }
        .print-ladder-svg { display: block; width: 100%; height: auto; background: white; }
        .script-rung { border-color: #c7d2fe; }
        .script-code { margin: 0; padding: 16px 18px; background: #ffffff; color: #111827; overflow-x: auto; font-size: 12px; line-height: 1.55; font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace; white-space: pre-wrap; border-top: 1px solid #e5e7eb; }
        .tok-comment { color: #64748b; font-style: italic; }
        .tok-keyword { color: #1d4ed8; font-weight: 800; }
        .tok-string { color: #047857; }
        .tok-number { color: #92400e; }
        .tag-appendix { break-before: page; page-break-before: always; margin-top: 28px; }
        .appendix-title { margin: 0 0 12px; font-size: 20px; font-weight: 950; letter-spacing: -.02em; color: #111827; }
        .tag-appendix table { width: 100%; border-collapse: collapse; font-size: 11px; }
        .tag-appendix th { text-align: left; padding: 8px 7px; color: #1f2937; background: #f1f5f9; border: 1px solid #d1d5db; font-size: 10px; text-transform: uppercase; letter-spacing: .06em; }
        .tag-appendix td { vertical-align: top; padding: 7px; border: 1px solid #e5e7eb; color: #374151; line-height: 1.35; }
        .tag-appendix .mono { font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace; color: #111827; font-weight: 800; }
        .tag-appendix .used-in { min-width: 142px; }
        .xref-list { display: grid; gap: 3px; }
        .xref-line { display: grid; grid-template-columns: 68px 1fr; gap: 6px; align-items: baseline; }
        .xref-where { font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace; color: #111827; font-weight: 850; white-space: nowrap; }
        .xref-detail { color: #475569; }
        .footer { padding: 14px 42px 24px; color: #6b7280; font-size: 11px; border-top: 1px solid #e5e7eb; }
        body.compact .doc-header { padding-top: 24px; padding-bottom: 18px; }
        body.compact h1 { font-size: 24px; }
        body.compact .content { padding-top: 16px; padding-bottom: 22px; }
        body.compact .print-rung { margin-bottom: 12px; border-radius: 10px; }
        body.compact .rung-head { padding: 8px 12px; }
        body.compact .print-ladder-svg { max-height: 108px; }
        body.mono .brand-mark, body.mono .rung-kind { color: #111827; }
        body.mono .doc-header { background: #ffffff; }
        body.mono .print-advice { background: #ffffff; color: #111827; border-color: #9ca3af; }
        body.mono .script-code { background: #ffffff; color: #111827; border-top: 1px solid #d1d5db; }
        body.mono .tok-comment, body.mono .tok-keyword, body.mono .tok-string, body.mono .tok-number { color: #111827; }
        body.mono svg * { stroke: #111827 !important; fill: none; }
        body.mono svg text { fill: #111827 !important; stroke: none !important; }
        body.mono svg rect { fill: #ffffff !important; }
        @media print {
          @page { margin: 0.45in; }
          body { background: #ffffff; }
          .toolbar, .print-advice, .copy-rung { display: none !important; }
          .paper { max-width: none; margin: 0; box-shadow: none; border-radius: 0; }
          .doc-header, .meta-grid, .content, .footer { padding-left: 0; padding-right: 0; }
          .meta-grid { grid-template-columns: repeat(5, 1fr); }
          .print-rung { margin-bottom: 16px; }
          body.compact .print-rung { margin-bottom: 10px; }
          .script-code, .doc-header, .meta, .rung-head { print-color-adjust: exact; -webkit-print-color-adjust: exact; }
        }
      </style><script>
        function setCopyStatus(btn, text, delay) {
          const original = btn.getAttribute('data-original-label') || btn.textContent;
          btn.setAttribute('data-original-label', original);
          btn.textContent = text;
          window.setTimeout(function(){ btn.textContent = original; }, delay || 1400);
        }
        function copyPlainTextFallback(btn) {
          const text = decodeURIComponent(btn.getAttribute('data-copy') || '');
          if(navigator.clipboard && navigator.clipboard.writeText) {
            return navigator.clipboard.writeText(text).then(function(){ return 'Copied Text'; }).catch(function(){ return legacyCopyText(text); });
          }
          return legacyCopyText(text);
        }
        function legacyCopyText(text) {
          return new Promise(function(resolve, reject) {
            const area = document.createElement('textarea');
            area.value = text;
            area.setAttribute('readonly', '');
            area.style.position = 'fixed';
            area.style.left = '-9999px';
            area.style.top = '0';
            document.body.appendChild(area);
            area.focus();
            area.select();
            try {
              const ok = document.execCommand('copy');
              document.body.removeChild(area);
              ok ? resolve('Copied Text') : reject(new Error('execCommand copy returned false'));
            } catch (err) {
              document.body.removeChild(area);
              reject(err);
            }
          });
        }
        function cloneSvgForCopy(svg) {
          const clone = svg.cloneNode(true);
          clone.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
          const box = svg.viewBox && svg.viewBox.baseVal ? svg.viewBox.baseVal : null;
          const width = box && box.width ? box.width : Math.ceil(svg.getBoundingClientRect().width || 820);
          const height = box && box.height ? box.height : Math.ceil(svg.getBoundingClientRect().height || 140);
          clone.setAttribute('width', String(width));
          clone.setAttribute('height', String(height));
          clone.setAttribute('viewBox', '0 0 ' + width + ' ' + height);
          return { clone: clone, width: width, height: height };
        }
        function svgToPngBlob(svg, width, height) {
          return new Promise(function(resolve, reject) {
            const serialized = new XMLSerializer().serializeToString(svg);
            const blob = new Blob([serialized], { type: 'image/svg+xml;charset=utf-8' });
            const url = URL.createObjectURL(blob);
            const img = new Image();
            img.onload = function() {
              try {
                const scale = Math.max(2, Math.min(3, window.devicePixelRatio || 2));
                const canvas = document.createElement('canvas');
                canvas.width = Math.ceil(width * scale);
                canvas.height = Math.ceil(height * scale);
                const ctx = canvas.getContext('2d');
                ctx.scale(scale, scale);
                ctx.fillStyle = '#ffffff';
                ctx.fillRect(0, 0, width, height);
                ctx.drawImage(img, 0, 0, width, height);
                URL.revokeObjectURL(url);
                canvas.toBlob(function(pngBlob) {
                  pngBlob ? resolve(pngBlob) : reject(new Error('PNG encode failed'));
                }, 'image/png');
              } catch (err) {
                URL.revokeObjectURL(url);
                reject(err);
              }
            };
            img.onerror = function() { URL.revokeObjectURL(url); reject(new Error('SVG image load failed')); };
            img.src = url;
          });
        }
        function scriptRungToPngBlob(section) {
          return new Promise(function(resolve, reject) {
            try {
              const rect = section.getBoundingClientRect();
              const width = Math.max(760, Math.ceil(rect.width || 900));
              const codeEl = section.querySelector('.script-code');
              const title = (section.querySelector('.rung-index') ? section.querySelector('.rung-index').textContent : 'Script Rung') + '  ' + (section.querySelector('.rung-kind') ? section.querySelector('.rung-kind').textContent : 'ANGELSCRIPT RUNG');
              const comment = section.querySelector('.rung-comment') ? section.querySelector('.rung-comment').textContent : '';
              const code = codeEl ? codeEl.innerText : '';
              const lines = code.replace(/\r/g, '').split('\n');
              const lineHeight = 18;
              const height = Math.max(150, 76 + lines.length * lineHeight + 28);
              const scale = Math.max(2, Math.min(3, window.devicePixelRatio || 2));
              const canvas = document.createElement('canvas');
              canvas.width = Math.ceil(width * scale);
              canvas.height = Math.ceil(height * scale);
              const ctx = canvas.getContext('2d');
              ctx.scale(scale, scale);
              ctx.fillStyle = '#ffffff';
              ctx.fillRect(0, 0, width, height);
              ctx.strokeStyle = '#c7d2fe';
              ctx.lineWidth = 1;
              ctx.strokeRect(0.5, 0.5, width - 1, height - 1);
              ctx.fillStyle = '#f8fafc';
              ctx.fillRect(1, 1, width - 2, 52);
              ctx.strokeStyle = '#e5e7eb';
              ctx.beginPath();
              ctx.moveTo(0, 52.5);
              ctx.lineTo(width, 52.5);
              ctx.stroke();
              ctx.fillStyle = '#111827';
              ctx.font = 'bold 16px ui-sans-serif, system-ui, sans-serif';
              ctx.fillText(title, 16, 24);
              ctx.fillStyle = '#374151';
              ctx.font = 'bold 13px ui-sans-serif, system-ui, sans-serif';
              ctx.fillText(comment, 16, 43);
              ctx.font = '12px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace';
              let y = 78;
              lines.forEach(function(line) {
                const trimmed = line.trim();
                if(trimmed.startsWith('//')) ctx.fillStyle = '#64748b';
                else if(/\b(if|else|for|while|return|true|false)\b/.test(line)) ctx.fillStyle = '#1d4ed8';
                else ctx.fillStyle = '#111827';
                ctx.fillText(line, 18, y);
                y += lineHeight;
              });
              canvas.toBlob(function(blob) { blob ? resolve(blob) : reject(new Error('PNG encode failed')); }, 'image/png');
            } catch (err) { reject(err); }
          });
        }
        async function writeImageBlobToClipboard(blob) {
          if(!(navigator.clipboard && navigator.clipboard.write && window.ClipboardItem)) {
            throw new Error('Image clipboard API unavailable');
          }
          await navigator.clipboard.write([new ClipboardItem({ 'image/png': blob })]);
        }
        async function copyPrintRungImage(btn) {
          btn.textContent = 'Copying...';
          try {
            const section = btn.closest('.print-rung');
            if(!section) throw new Error('Rung element not found');
            const rungSvg = section.querySelector('.print-ladder-svg');
            let pngBlob;
            if(rungSvg) {
              const prepared = cloneSvgForCopy(rungSvg);
              pngBlob = await svgToPngBlob(prepared.clone, prepared.width, prepared.height);
            } else {
              pngBlob = await scriptRungToPngBlob(section);
            }
            await writeImageBlobToClipboard(pngBlob);
            setCopyStatus(btn, 'Copied Image', 1400);
          } catch (err) {
            console.warn('PiLab Copy Image fell back to text:', err);
            try {
              const status = await copyPlainTextFallback(btn);
              setCopyStatus(btn, status, 1600);
            } catch (fallbackErr) {
              console.warn('PiLab Copy Text fallback failed:', fallbackErr);
              setCopyStatus(btn, 'Copy Failed', 1800);
            }
          }
        }
      <\/script></head><body><div class="toolbar"><div><div class="toolbar-title">PiLab Ladder Print Preview</div><div class="toolbar-note">Use Copy Image to place a rung visualization on the clipboard. For the cleanest PDF, disable browser Headers and footers in the print dialog.</div></div><div class="toolbar-actions"><label><input type="checkbox" onchange="document.body.classList.toggle('compact', this.checked)"> Compact</label><label><input type="checkbox" onchange="document.body.classList.toggle('mono', this.checked)"> Monochrome</label><button onclick="window.print()">Print / Save PDF</button></div></div><main class="paper"><header class="doc-header"><div class="brand"><div class="brand-mark">PiLab PLC Ladder Documentation</div><div class="subtitle">Generated ${this.escapePrintHtml(generatedAt)}</div></div><h1>${this.escapePrintHtml(project.name || 'Untitled Ladder Program')}</h1><div class="subtitle">${this.escapePrintHtml(fileName)}</div>${desc ? `<p class="description">${this.escapePrintHtml(desc)}</p>` : ''}<div class="print-advice"><strong>Print tip:</strong> In Chrome/Edge, turn off <em>Headers and footers</em> for a clean generated PDF without the browser URL/date/footer text.</div></header><section class="meta-grid"><div class="meta"><div class="meta-label">Scan Time</div><div class="meta-value">${this.escapePrintHtml(project.scan_ms || 0)} ms</div></div><div class="meta"><div class="meta-label">Total Rungs</div><div class="meta-value">${(project.rungs || []).length}</div></div><div class="meta"><div class="meta-label">Ladder</div><div class="meta-value">${ladderCount}</div></div><div class="meta"><div class="meta-label">AngelScript</div><div class="meta-value">${scriptCount}</div></div><div class="meta"><div class="meta-label">Tags</div><div class="meta-value">${tagEntries.length}</div></div></section><section class="content">${sections || '<p>No rungs to print.</p>'}${tagAppendix}</section><footer class="footer">Generated from the PiLab browser Ladder editor. Logic shown without editor controls, slots, drag targets, or simulation overlays.</footer></main></body></html>`;
      const win = window.open('', '_blank');
      if(!win) { this.show('Popup blocked. Allow popups to open the print preview.'); return; }
      win.document.open();
      win.document.write(html);
      win.document.close();

      const setPrintCopyStatus = (btn, label, delay = 1500) => {
        const original = btn.getAttribute('data-original-label') || btn.textContent || 'Copy Image';
        btn.setAttribute('data-original-label', original);
        btn.textContent = label;
        win.setTimeout(() => { btn.textContent = original; }, delay);
      };

      const legacyCopyTextFromPrintWindow = (text) => {
        const area = win.document.createElement('textarea');
        area.value = text;
        area.setAttribute('readonly', '');
        area.style.position = 'fixed';
        area.style.left = '-9999px';
        area.style.top = '0';
        win.document.body.appendChild(area);
        area.focus();
        area.select();
        let ok = false;
        try { ok = win.document.execCommand('copy'); }
        finally { win.document.body.removeChild(area); }
        if(!ok) throw new Error('Text clipboard fallback failed');
      };

      const writeTextFromPrintWindow = async (text) => {
        if(navigator.clipboard && navigator.clipboard.writeText && window.isSecureContext) {
          try {
            await navigator.clipboard.writeText(text);
            return;
          } catch (err) {
            // Fall through to the older selection-based copy path.
          }
        }
        legacyCopyTextFromPrintWindow(text);
      };

      const svgElementToPngBlob = (svg) => new Promise((resolve, reject) => {
        try {
          const box = svg.viewBox && svg.viewBox.baseVal ? svg.viewBox.baseVal : null;
          const width = box && box.width ? box.width : Math.ceil(svg.getBoundingClientRect().width || 820);
          const height = box && box.height ? box.height : Math.ceil(svg.getBoundingClientRect().height || 140);
          const clone = svg.cloneNode(true);
          clone.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
          clone.setAttribute('width', String(width));
          clone.setAttribute('height', String(height));
          clone.setAttribute('viewBox', '0 0 ' + width + ' ' + height);
          const serialized = new XMLSerializer().serializeToString(clone);
          const blob = new Blob([serialized], { type: 'image/svg+xml;charset=utf-8' });
          const url = URL.createObjectURL(blob);
          const img = new Image();
          img.onload = () => {
            try {
              const scale = Math.max(2, Math.min(3, window.devicePixelRatio || 2));
              const canvas = win.document.createElement('canvas');
              canvas.width = Math.ceil(width * scale);
              canvas.height = Math.ceil(height * scale);
              const ctx = canvas.getContext('2d');
              ctx.scale(scale, scale);
              ctx.fillStyle = '#ffffff';
              ctx.fillRect(0, 0, width, height);
              ctx.drawImage(img, 0, 0, width, height);
              URL.revokeObjectURL(url);
              canvas.toBlob((pngBlob) => pngBlob ? resolve({ blob: pngBlob, dataUrl: canvas.toDataURL('image/png') }) : reject(new Error('PNG encode failed')), 'image/png');
            } catch (err) {
              URL.revokeObjectURL(url);
              reject(err);
            }
          };
          img.onerror = () => { URL.revokeObjectURL(url); reject(new Error('SVG image load failed')); };
          img.src = url;
        } catch (err) {
          reject(err);
        }
      });

      const scriptElementToPngBlob = (section) => new Promise((resolve, reject) => {
        try {
          const rect = section.getBoundingClientRect();
          const width = Math.max(760, Math.ceil(rect.width || 900));
          const codeEl = section.querySelector('.script-code');
          const title = ((section.querySelector('.rung-index') || {}).textContent || 'Script Rung') + '  ' + ((section.querySelector('.rung-kind') || {}).textContent || 'ANGELSCRIPT RUNG');
          const comment = (section.querySelector('.rung-comment') || {}).textContent || '';
          const code = codeEl ? codeEl.innerText : '';
          const lines = code.replace(/\r/g, '').split('\n');
          const lineHeight = 18;
          const height = Math.max(150, 76 + lines.length * lineHeight + 28);
          const scale = Math.max(2, Math.min(3, window.devicePixelRatio || 2));
          const canvas = win.document.createElement('canvas');
          canvas.width = Math.ceil(width * scale);
          canvas.height = Math.ceil(height * scale);
          const ctx = canvas.getContext('2d');
          ctx.scale(scale, scale);
          ctx.fillStyle = '#ffffff';
          ctx.fillRect(0, 0, width, height);
          ctx.strokeStyle = '#c7d2fe';
          ctx.lineWidth = 1;
          ctx.strokeRect(0.5, 0.5, width - 1, height - 1);
          ctx.fillStyle = '#f8fafc';
          ctx.fillRect(1, 1, width - 2, 52);
          ctx.strokeStyle = '#e5e7eb';
          ctx.beginPath();
          ctx.moveTo(0, 52.5);
          ctx.lineTo(width, 52.5);
          ctx.stroke();
          ctx.fillStyle = '#111827';
          ctx.font = 'bold 16px ui-sans-serif, system-ui, sans-serif';
          ctx.fillText(title, 16, 24);
          ctx.fillStyle = '#374151';
          ctx.font = 'bold 13px ui-sans-serif, system-ui, sans-serif';
          ctx.fillText(comment, 16, 43);
          ctx.font = '12px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace';
          let y = 78;
          lines.forEach((line) => {
            const trimmed = line.trim();
            if(trimmed.startsWith('//')) ctx.fillStyle = '#64748b';
            else if(/\b(if|else|for|while|return|true|false)\b/.test(line)) ctx.fillStyle = '#1d4ed8';
            else ctx.fillStyle = '#111827';
            ctx.fillText(line, 18, y);
            y += lineHeight;
          });
          canvas.toBlob((blob) => blob ? resolve({ blob, dataUrl: canvas.toDataURL('image/png') }) : reject(new Error('PNG encode failed')), 'image/png');
        } catch (err) {
          reject(err);
        }
      });

      const execCommandCopyImage = (dataUrl) => {
        const box = win.document.createElement('div');
        box.contentEditable = 'true';
        box.style.position = 'fixed';
        box.style.left = '-9999px';
        box.style.top = '0';
        box.style.width = '1px';
        box.style.height = '1px';
        box.innerHTML = '<img src="' + dataUrl + '" alt="PiLab rung image">';
        win.document.body.appendChild(box);
        const range = win.document.createRange();
        range.selectNodeContents(box);
        const selection = win.getSelection();
        selection.removeAllRanges();
        selection.addRange(range);
        const ok = win.document.execCommand('copy');
        selection.removeAllRanges();
        win.document.body.removeChild(box);
        if(!ok) throw new Error('Image clipboard fallback failed');
      };

      const writeImageFromPrintWindow = async (payload) => {
        if(navigator.clipboard && navigator.clipboard.write && window.ClipboardItem && window.isSecureContext) {
          try {
            await navigator.clipboard.write([new ClipboardItem({ 'image/png': payload.blob })]);
            return;
          } catch (err) {
            // Fall through to the older rich HTML image copy path.
          }
        }
        execCommandCopyImage(payload.dataUrl);
      };

      win.document.querySelectorAll('.copy-rung').forEach((btn) => {
        btn.removeAttribute('onclick');
        btn.addEventListener('click', async () => {
          setPrintCopyStatus(btn, 'Copying...', 200000);
          const section = btn.closest('.print-rung');
          try {
            if(!section) throw new Error('Rung element not found');
            const rungSvg = section.querySelector('.print-ladder-svg');
            const payload = rungSvg ? await svgElementToPngBlob(rungSvg) : await scriptElementToPngBlob(section);
            await writeImageFromPrintWindow(payload);
            setPrintCopyStatus(btn, 'Copied Image', 1500);
          } catch (err) {
            try {
              await writeTextFromPrintWindow(decodeURIComponent(btn.getAttribute('data-copy') || ''));
              setPrintCopyStatus(btn, 'Copied Text', 1600);
            } catch (fallbackErr) {
              setPrintCopyStatus(btn, 'Copy Failed', 2000);
              console.warn('PiLab copy rung failed', err, fallbackErr);
            }
          }
        });
      });

      win.focus();
    },
    show(m){ this.toast=m; setTimeout(()=>this.toast='',1500); },
    blankLadderProject(){
      return addFormalSchemaMetadata({ name:'Untitled Ladder Program', description:'', scan_ms:5, rungs:[this.createRung('')] });
    },
    fileApiJoin(a,b){ return (a === '/' ? '/' + b : String(a || '').replace(/\/+$/,'') + '/' + b).replace(/\/+/g,'/'); },
    normalizeLadderFileName(name){
      let clean = String(name || '').trim().replace(/[\\/]+/g, '_');
      if(!clean) clean = this.ladderStore.currentFileName || 'main.piLadder';
      clean = clean.replace(/\.json$/i, '.piLadder');
      if(!/\.piLadder$/i.test(clean)) clean += '.piLadder';
      return clean;
    },
    ladderFilePathForName(name){ return this.fileApiJoin('/ladder', this.normalizeLadderFileName(name)); },
    async refreshPlcModeForFiles(){
      try { await this.plcStore.refreshCommandCenter(); } catch(_) {}
    },
    async ensureLadderFolder(){
      try { await mkdirApi('/ladder'); } catch(_) {}
    },
    async refreshLadderFiles(){
      this.ladderFileBusy = true;
      await this.refreshPlcModeForFiles();
      try {
        let j;
        try {
          j = await listFiles('/ladder');
        } catch(e) {
          if(this.flashWritesAllowedNow) {
            await this.ensureLadderFolder();
            j = await listFiles('/ladder');
          } else {
            throw e;
          }
        }
        this.ladderFiles = (j.entries || [])
          .filter(e => !e.dir && /\.(?:piLadder|json)$/i.test(e.name || e.path || ''))
          .sort((a,b) => String(a.name || '').localeCompare(String(b.name || ''), undefined, { numeric:true }));
        if(this.ladderStore.currentFilePath && this.ladderFiles.some(f => f.path === this.ladderStore.currentFilePath)) {
          this.selectedLadderFile = this.ladderStore.currentFilePath;
        }
        this.ladderFileStatus = `Loaded ${this.ladderFiles.length} ladder file${this.ladderFiles.length===1?'':'s'}.`;
      } catch(e) {
        this.ladderFiles = [];
        this.ladderFileStatus = 'Error loading /ladder: ' + (e?.message || e);
      } finally {
        this.ladderFileBusy = false;
      }
    },
    selectLadderFile(file){
      this.selectedLadderFile = file?.path || '';
      if(file?.name) this.ladderFileNameDraft = file.name;
    },
    resetLadderRuntimeState(){
      this.selected = null;
      this.selectedBranch = null;
      this.branchStart = null;
      this.editingCommentId = null;
      this.mode = 'select';
      this.stopSimRun();
      this.simTags = {};
      this.simBlocks = {};
      this.simRungs = {};
      this.simProgram = null;
      this.simCompiledCode = '';
      this.simScanCount = 0;
    },
    async saveLadderFile(){
      const name = this.ladderStore.currentFileName || this.ladderFileNameDraft || 'main.piLadder';
      return this.saveLadderFileToName(name);
    },
    async saveLadderFileAs(){
      return this.saveLadderFileToName(this.ladderFileNameDraft || this.ladderStore.currentFileName || 'main.piLadder');
    },
    async saveLadderFileToName(name){
      await this.refreshPlcModeForFiles();
      if(!this.flashWritesAllowedNow) {
        this.ladderFileStatus = 'Error: PLC must be STOPPED before saving ladder files.';
        return;
      }
      const safeName = this.normalizeLadderFileName(name);
      const path = this.ladderFilePathForName(safeName);
      this.ladderFileBusy = true;
      try {
        await this.ensureLadderFolder();
        const payload = new TextEncoder().encode(this.jsonModel).buffer;
        await uploadFile(path, payload);
        setLadderCurrentFile(safeName, path);
        markLadderProjectSaved();
        this.ladderFileNameDraft = safeName;
        this.selectedLadderFile = path;
        saveLadderEditorSnapshot(this);
        this.show('Saved ' + safeName);
        await this.refreshLadderFiles();
        this.ladderFileStatus = 'Saved ' + path;
      } catch(e) {
        this.ladderFileStatus = 'Error saving ladder file: ' + (e?.message || e);
      } finally {
        this.ladderFileBusy = false;
      }
    },
    async loadSelectedLadderFile(){
      if(!this.selectedLadderFile) {
        this.ladderFileStatus = 'Error: select a ladder JSON file first.';
        return;
      }
      return this.loadLadderFile(this.selectedLadderFile);
    },
    async loadLadderFile(path){
      if(this.ladderDirty) {
        const ok = await confirmDialog({ title: 'Load Ladder File', message: 'Load this ladder file and discard unsaved changes?', confirmText: 'Load File', tone: 'warning' });
        if(!ok) return;
      }
      this.ladderFileBusy = true;
      try {
        const text = await viewFileApi(path);
        const parsed = JSON.parse(String(text || ''));
        this.normalizeImportedProject(parsed);
        this.suppressLadderDirty = true;
        this.pushHistory('Load ladder file');
        this.project = parsed;
        this.jsonDraft = this.jsonModel;
        this.resetLadderRuntimeState();
        const name = String(path || '').split('/').pop() || 'main.piLadder';
        setLadderCurrentFile(name, path);
        markLadderProjectSaved();
        this.ladderFileNameDraft = name;
        this.selectedLadderFile = path;
        this.ladderFileStatus = 'Loaded ' + path;
        this.show('Loaded ' + name);
        await nextTick();
        this.suppressLadderDirty = false;
        saveLadderEditorSnapshot(this);
      } catch(e) {
        this.suppressLadderDirty = false;
        this.ladderFileStatus = 'Error loading ladder file: ' + (e?.message || e);
      } finally {
        this.ladderFileBusy = false;
      }
    },
    async newLadderFile(){
      if(this.ladderDirty) {
        const ok = await confirmDialog({ title: 'New Ladder Program', message: 'Create a new ladder program and discard unsaved changes?', confirmText: 'Create New', tone: 'warning' });
        if(!ok) return;
      }
      this.suppressLadderDirty = true;
      this.pushHistory('New ladder file');
      this.project = this.blankLadderProject();
      this.jsonDraft = this.jsonModel;
      this.resetLadderRuntimeState();
      setLadderCurrentFile('', '');
      markLadderProjectSaved();
      this.ladderFileNameDraft = 'main.piLadder';
      this.selectedLadderFile = '';
      this.ladderFileStatus = 'Created a new unsaved ladder program.';
      this.show('New ladder program');
      nextTick(() => { this.suppressLadderDirty = false; saveLadderEditorSnapshot(this); });
    },
    async deleteSelectedLadderFile(){
      await this.refreshPlcModeForFiles();
      if(!this.flashWritesAllowedNow) {
        this.ladderFileStatus = 'Error: PLC must be STOPPED before deleting ladder files.';
        return;
      }
      const path = this.selectedLadderFile;
      if(!path) return;
      const ok = await confirmDialog({ title: 'Delete Ladder File', message: 'Delete ' + path + '?', confirmText: 'Delete', tone: 'danger' });
      if(!ok) return;
      this.ladderFileBusy = true;
      try {
        await deletePathApi(path);
        if(this.ladderStore.currentFilePath === path) {
          setLadderCurrentFile('', '');
          markLadderProjectDirty();
        }
        this.selectedLadderFile = '';
        this.show('Deleted ladder file');
        await this.refreshLadderFiles();
        this.ladderFileStatus = 'Deleted ' + path;
      } catch(e) {
        this.ladderFileStatus = 'Error deleting ladder file: ' + (e?.message || e);
      } finally {
        this.ladderFileBusy = false;
      }
    },
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
    async clearAll(){
      const ok = await confirmDialog({ title: 'Clear Ladder', message: 'Clear all rungs?', detail: 'The project will be reduced to one empty rung.', confirmText: 'Clear Rungs', tone: 'danger' });
      if(ok) { this.withHistory('Clear all',()=>{ this.project.rungs=[]; this.project.rungs.push(this.createRung('')); this.editingCommentId=null; }); }
    },
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
    canvasBlankClick(){
      // Branch selection is only meaningful while the branch wire/contact is selected.
      // Clear it when the user clicks ordinary canvas/wire space so the Toolbox
      // delete action does not stay visible indefinitely.
      if(this.mode==='branch') return;
      this.selected=null;
      this.selectedBranch=null;
      this.branchStart=null;
    },
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
        this.selectedBranch=null;
        return;
      }
      if(!arr[slot]) { this.pushHistory('Add symbol'); arr[slot]=this.newSymbol(this.selectedTool); }
      this.selected={rung:r,lane,branch:br,slot,el:arr[slot]};
      this.selectedBranch = (lane==='branch' && br) ? {rung:r,branchId:br.id} : null;
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
        this.selectedBranch=null;
        return;
      }
      this.selected={rung:r,lane,branch:br,slot,el:arr[slot]};
      this.selectedBranch = (lane==='branch' && br) ? {rung:r,branchId:br.id} : null;
    },
    dropOnMain(r,slot){
      const t=this.draggedTool || this.selectedTool;
      this.pushHistory('Drop symbol');
      r.main[slot]=this.newSymbol(t);
      this.selected={rung:r,lane:'main',branch:null,slot,el:r.main[slot]};
      this.selectedBranch=null;
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
    async applyJson(){
      try{
        const parsed=JSON.parse(this.jsonDraft);
        this.normalizeImportedProject(parsed);
        this.pushHistory('Apply JSON');
        this.project=parsed;
        this.selected=null; this.selectedBranch=null; this.show('JSON applied');
      }
      catch(e){ await messageDialog({ title: 'Invalid JSON', message: 'Invalid JSON: '+e.message, tone: 'danger' }); }
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
          const analyzer = this.analyzeScriptRungForJsSimulator || this.jsAnalyzeScriptRungForSimulator;
          if(analyzer){
            const analysis=analyzer(r.code || '');
            if(analysis && analysis.unsupportedLines && analysis.unsupportedLines.length) scriptCompatibility.push({ rung:ri+1, unsupported:analysis.unsupportedLines.length });
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
        'pilab_ladder_project.piLadder  - PiLab ladder/script project',
        'pilab_ladder_generated.as  - AngelScript export',
        'pilab_ladder_generated.js  - JavaScript simulator/runtime export',
        'README.txt                 - Bundle summary and notes'
      ].join('\n');
    },
    projectBundleReadmeText(){
      const review=this.projectReview();
      return `PiLab Project Bundle

Project: ${this.project.name || 'Untitled'}
Scan time: ${this.project.scan_ms || 5} ms

Description:
${this.project.description || '(none)'}

Summary:
- Rungs: ${review.rungCount}
- Ladder rungs: ${review.ladderRungCount}
- Script rungs: ${review.scriptRungCount}
- Ladder-discovered user tags: ${review.tagCount}
- Physical outputs written: ${review.physicalOutputs.join(', ') || '(none)'}

Files:
${this.projectBundleManifestText()}

Notes:
- Import this ZIP from the Files tab to restore the ladder project.
- Tags are managed by the Web app Tags page and shared in-memory tag store.
- Use Sync Discovered Tags to merge ladder-discovered tags into the Web app Tag Registry before saving to the PLC.
- The AngelScript export can include optional tag globals when Add Tags is enabled in the editor.
`;
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
        {name:'pilab_ladder_project.piLadder', text:this.jsonModel},
        {name:'pilab_ladder_generated.as', text:this.generatedAngelScriptSource(this.includeAngelScriptTags)},
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
      const projectName = this.findBundleFile(files, 'pilab_ladder_project.piLadder') || this.findBundleFile(files, 'pilab_ladder_project.json');
      if(!projectName) throw new Error('Bundle is missing pilab_ladder_project.piLadder.');
      const parsedProject = JSON.parse(files[projectName]);
      this.normalizeImportedProject(parsedProject);

      this.suppressLadderDirty = true;
      this.pushHistory('Import project bundle');
      this.project = parsedProject;
      this.jsonDraft = this.jsonModel;
      this.resetLadderRuntimeState();
      const importedName = String(projectName || 'pilab_ladder_project.piLadder').split('/').pop() || 'pilab_ladder_project.piLadder';
      setLadderCurrentFile(importedName, '');
      this.ladderFileNameDraft = importedName;
      markLadderProjectSaved();
      saveLadderEditorSnapshot(this);
      nextTick(() => { this.suppressLadderDirty = false; markLadderProjectSaved(); saveLadderEditorSnapshot(this); });
      return { projectName };
    },
    importProjectBundleFile(ev){
      const file = ev && ev.target && ev.target.files ? ev.target.files[0] : null;
      if(!file) return;
      const reader = new FileReader();
      reader.onload = async () => {
        try {
          const files = this.parseUncompressedZipFiles(reader.result);
          const result = this.importProjectBundlePayload(files);
          this.show('Imported project bundle');
        } catch(e) {
          await messageDialog({ title: 'Import Failed', message: 'Could not import project bundle: '+e.message, tone: 'danger' });
        } finally {
          ev.target.value = '';
        }
      };
      reader.onerror = async () => {
        await messageDialog({ title: 'Read Failed', message: 'Could not read project bundle: '+(reader.error ? reader.error.message : 'unknown error'), tone: 'danger' });
        ev.target.value = '';
      };
      reader.readAsArrayBuffer(file);
    },
    downloadJson(){
      const safeName = this.normalizeLadderFileName(this.ladderFileNameDraft || this.ladderStore.currentFileName || 'pilab_ladder_project.piLadder');
      this.download(safeName, this.jsonModel, 'application/json');
      this.suppressLadderDirty = true;
      this.ladderFileNameDraft = safeName;
      setLadderCurrentFile(safeName, '');
      markLadderProjectSaved();
      saveLadderEditorSnapshot(this);
      nextTick(() => { this.suppressLadderDirty = false; saveLadderEditorSnapshot(this); });
      this.show('Exported ' + safeName);
    },
    importJsonFile(ev){
      const file = ev && ev.target && ev.target.files ? ev.target.files[0] : null;
      if(!file) return;
      const reader = new FileReader();
      reader.onload = async () => {
        try {
          const parsed = JSON.parse(String(reader.result || ''));
          this.normalizeImportedProject(parsed);
          this.suppressLadderDirty = true;
          this.pushHistory('Import JSON file');
          this.project = parsed;
          this.jsonDraft = this.jsonModel;
          this.resetLadderRuntimeState();
          const importedName = this.normalizeLadderFileName(file.name || 'imported_ladder.piLadder');
          setLadderCurrentFile(importedName, '');
          this.ladderFileNameDraft = importedName;
          markLadderProjectSaved();
          saveLadderEditorSnapshot(this);
          nextTick(() => { this.suppressLadderDirty = false; markLadderProjectSaved(); saveLadderEditorSnapshot(this); });
          this.show('Imported '+(file.name || 'JSON project')+'; simulator reset');
        } catch(e) {
          await messageDialog({ title: 'Import Failed', message: 'Could not import JSON: '+e.message, tone: 'danger' });
        } finally {
          ev.target.value = '';
        }
      };
      reader.onerror = async () => {
        await messageDialog({ title: 'Read Failed', message: 'Could not read file: '+(reader.error ? reader.error.message : 'unknown error'), tone: 'danger' });
        ev.target.value = '';
      };
      reader.readAsText(file);
    },

    angelScriptGenerationMetadata(){
      const review = this.projectReview ? this.projectReview() : null;
      const now = new Date();
      const sourceFileName = this.normalizeLadderFileName(this.ladderFileNameDraft || this.ladderStore.currentFileName || 'main.piLadder');
      const rungSummary = review
        ? `${review.rungCount} total, ${review.ladderRungCount} ladder, ${review.scriptRungCount} script`
        : `${(this.project.rungs || []).length} total`;
      const tagSummary = review
        ? `${review.tagCount} ladder-discovered`
        : '';
      return {
        sourceFileName,
        generatedAt: now.toLocaleString(),
        description: this.project.description || '',
        rungSummary,
        tagSummary,
        notes: 'Generated from the Ladder page. Edit the .piLadder source file for normal ladder changes.'
      };
    },
    generatedAngelScriptSource(includeTagGlobals=false){
      return this.transpile(includeTagGlobals, this.angelScriptGenerationMetadata());
    },

    generatedScriptName(){
      const base = this.ladderStore.currentFileName
        ? String(this.ladderStore.currentFileName).replace(/\.(?:piLadder|json)$/i, '')
        : 'main';
      const safe = base.replace(/[^A-Za-z0-9_.-]+/g, '_').replace(/^\.+/, '') || 'main';
      return /\.as$/i.test(safe) ? safe : safe + '.as';
    },
    generatedScriptEditorName(){
      const sourceName = this.ladderFileNameDraft || this.ladderStore.currentFileName || 'main.piLadder';
      const base = String(sourceName)
        .trim()
        .replace(/\.(?:piLadder|json|as|piAS)$/i, '')
        .replace(/[^A-Za-z0-9_.-]+/g, '_')
        .replace(/^\.+/, '') || 'main';
      return base + '.piAS';
    },
    async sendGeneratedAngelScriptToScriptEditor(){
      const source = this.generatedAngelScriptSource(false);
      const name = this.generatedScriptEditorName();
      const previousSource = localStorage.getItem('pilab_script_source_v2') || '';
      if(previousSource && previousSource !== source) {
        const ok = await confirmDialog({ title: 'Replace Script Editor Contents', message: 'Send generated AngelScript to the Script page editor and replace the current Script editor contents?', confirmText: 'Send Script', tone: 'warning' });
        if(!ok) return;
      }
      localStorage.setItem('pilab_script_source_v2', source);
      localStorage.setItem('pilab_script_name', name);
      try {
        window.dispatchEvent(new CustomEvent('pilab:set-script-editor', { detail: { source, name, from: 'ladder' } }));
      } catch(_) {}
      this.show('Sent generated AngelScript to Script editor as ' + name);
    },
    clearLadderCompileDiagnostics(){
      this.ladderCompileDiagnostics = { errors: [], warnings: [], raw: '', state: '' };
    },
    parseLadderAngelScriptDiagnostics(text){
      const errors = [];
      const warnings = [];
      // Scan the entire compile result so multiple diagnostics concatenated
      // onto one line are all captured.
      const source = String(text || '').replace(/\r\n/g, '\n');
      const diagRe = /(?:^|\s)(?:(?:[A-Za-z0-9_./\\:-]+)\s+)?\((\d+)\s*,\s*(\d+)\)\s*:\s*(ERR|WARN|INFO)\s*:\s*/gi;
      const matches = [];
      let m;
      while((m = diagRe.exec(source)) !== null) {
        matches.push({
          index: m.index,
          bodyStart: diagRe.lastIndex,
          line: Math.max(1, parseInt(m[1], 10) || 1),
          col: Math.max(1, parseInt(m[2], 10) || 1),
          level: String(m[3] || '').toUpperCase(),
        });
      }
      for(let i = 0; i < matches.length; i += 1) {
        const cur = matches[i];
        const next = matches[i + 1];
        let msg = source.slice(cur.bodyStart, next ? next.index : source.length)
          .replace(/\s*Build failed\s*$/i, '')
          .trim();
        if(!msg) msg = `${cur.level} at ${cur.line}:${cur.col}`;
        const item = { line: cur.line, col: cur.col, msg };
        if(cur.level === 'ERR') errors.push(item);
        else if(cur.level === 'WARN') warnings.push(item);
      }
      return { errors, warnings };
    },
    async waitForLadderCompileResult(timeoutMs = 7000){
      const start = performance.now();
      let lastState = '';
      while(performance.now() - start < timeoutMs) {
        await new Promise(resolve => setTimeout(resolve, 250));
        let status = null;
        try { status = await getScriptStatus(); }
        catch(e) {
          this.ladderUploadStatus = 'Warning: upload accepted, but script status check failed: ' + (e?.message || e);
          return null;
        }
        const state = String(status?.state || '').toUpperCase();
        if(state && state !== lastState) {
          lastState = state;
          if(state === 'COMPILING' || state === 'PENDING') this.ladderUploadStatus = 'Upload accepted; P4 compile is ' + state.toLowerCase() + '...';
        }
        if(status && !status.compile_busy && !status.pending && ['OK','FAILED','QUEUE_FULL','ERROR'].includes(state)) {
          const raw = String(status.last_result || '');
          const parsed = this.parseLadderAngelScriptDiagnostics(raw);
          this.ladderCompileDiagnostics = { ...parsed, raw, state };
          if(state === 'OK') {
            this.ladderUploadStatus = `Compile OK. ${this.generatedScriptName()} is the active/generated PLC script.`;
          } else {
            this.ladderUploadStatus = `Compile failed (${state}). See P4 Compile Result below.`;
          }
          try { await this.plcStore.refreshCommandCenter(); } catch(_) {}
          try { await this.plcStore.refreshPlcData(); } catch(_) {}
          return status;
        }
      }
      this.ladderUploadStatus = 'Warning: upload accepted, but compile result did not arrive before timeout. Check Script page/status if needed.';
      return null;
    },
    async uploadGeneratedAngelScriptToPlc(){
      if(this.ladderUploadBusy) return;
      const issues = this.validationIssues || [];
      const errorCount = issues.filter(i => i && i.level === 'error').length;
      if(errorCount > 0) {
        const ok = await confirmDialog({ title: 'Upload With Validation Errors', message: `This ladder project has ${errorCount} validation error${errorCount===1?'':'s'}. Upload anyway?`, confirmText: 'Upload Anyway', tone: 'warning' });
        if(!ok) return;
      }
      if(this.tagStore && this.tagStore.dirty) {
        const ok = await confirmDialog({ title: 'Unsaved Tag Registry', message: 'The shared tag registry has unsaved changes. The PLC runtime may not expose those new tags until you save tags and upload/compile. Upload generated AngelScript anyway?', confirmText: 'Upload Anyway', tone: 'warning' });
        if(!ok) return;
      }
      this.ladderUploadBusy = true;
      this.clearLadderCompileDiagnostics();
      this.ladderUploadStatus = 'Uploading generated AngelScript to PLC...';
      try {
        // This intentionally does NOT save the ladder JSON first. The ladder
        // file is optional source/project storage; the runtime artifact is the
        // generated AngelScript sent through the same compile-safe upload path
        // used by the Script page.
        const source = this.generatedAngelScriptSource(false);
        const name = this.generatedScriptName();
        const result = await uploadScriptText(source, name);
        if(!result.ok) {
          const raw = result.text || '';
          const parsed = this.parseLadderAngelScriptDiagnostics(raw);
          this.ladderCompileDiagnostics = { ...parsed, raw, state: 'REJECTED' };
          this.ladderUploadStatus = `Error uploading generated script: HTTP ${result.status}. See P4 Compile Result below.`;
          return;
        }
        const msg = result.json?.message || result.text || 'Upload accepted; compile pending.';
        const savedPath = result.json?.saved_path ? ` Will save: ${result.json.saved_path}` : '';
        this.ladderUploadStatus = `Upload accepted for ${name}. ${msg}${savedPath}`;
        this.show('Uploaded generated AngelScript');
        await this.waitForLadderCompileResult();
      } catch(e) {
        this.ladderUploadStatus = 'Error uploading generated script: ' + (e?.message || e);
      } finally {
        this.ladderUploadBusy = false;
      }
    },
    async syncDiscoveredTagsToTagStore(){
      try { await ensureTagStoreLoaded().catch(() => {}); } catch(e) {}
      const rows = this.discoverTagRegistryEntries()
        .map(entry => this.normalizeTagRegistryRow(this.defaultTagRegistryRow(entry)))
        .filter(row => row && row.name && !this.tagRegistryIsReservedOrSystem(row.name));
      const result = mergeTagRowsIntoStore(rows, { reason: 'Synced ladder-discovered tags' });
      this.tagStore.loadedOnce = true;
      this.tagStore.status = `Synced ${result.total} ladder-discovered tag${result.total===1?'':'s'} (${result.added} added, ${result.updated} updated, ${result.skipped} skipped). Open Tags to review/save.`;
      this.show(this.tagStore.status);
    },
    importTagRegistryFile(ev){
      const file = ev && ev.target && ev.target.files ? ev.target.files[0] : null;
      if(!file) return;
      const reader = new FileReader();
      reader.onload = async () => {
        try {
          const parsed = JSON.parse(String(reader.result || ''));
          const count = this.importTagRegistryPayload(parsed);
          this.show('Imported '+count+' tag metadata row'+(count===1?'':'s'));
          this.$forceUpdate();
        } catch(e) {
          await messageDialog({ title: 'Import Failed', message: 'Could not import tag registry JSON: '+e.message, tone: 'danger' });
        } finally {
          ev.target.value = '';
        }
      };
      reader.onerror = async () => {
        await messageDialog({ title: 'Read Failed', message: 'Could not read tag registry file: '+(reader.error ? reader.error.message : 'unknown error'), tone: 'danger' });
        ev.target.value = '';
      };
      reader.readAsText(file);
    },
    downloadAs(){ this.download('pilab_ladder_generated.as',this.generatedAngelScriptSource(this.includeAngelScriptTags),'text/plain'); },
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
    async deleteTagRegistryRow(name){
      const row = this.buildTagRegistryRows().find(t => t.name === name);
      if(!this.canDeleteTagRegistryRow(row)) {
        this.show('Only unused imported tags can be deleted here');
        return;
      }
      const okConfirm = await confirmDialog({ title: 'Delete Imported Tag', message: 'Delete unused imported tag "'+name+'" from the Tag Registry view?', confirmText: 'Delete Tag', tone: 'danger' });
      if(!okConfirm) return;
      const ok = this.deleteImportedTagRegistryRow(name);
      this.show(ok ? 'Deleted unused imported tag' : 'Tag was not deleted');
      this.$forceUpdate();
    },
    async deleteUnusedImportedTags(){
      const count = Object.values(this.tagRegistryImported || {}).filter(row => !this.tagRegistryUsedNameSet().has(row.name)).length;
      if(count < 1){ this.show('No unused imported tags to delete'); return; }
      const okConfirm = await confirmDialog({ title: 'Delete Unused Tags', message: 'Delete '+count+' unused imported tag'+(count===1?'':'s')+' from the Tag Registry view?', confirmText: 'Delete Tags', tone: 'danger' });
      if(!okConfirm) return;
      const removed = this.deleteUnusedImportedTagRegistryRows();
      this.show('Deleted '+removed+' unused imported tag'+(removed===1?'':'s'));
      this.$forceUpdate();
    },
    async clearImportedTagRegistry(){
      const count = Object.keys(this.tagRegistryImported || {}).length;
      if(count < 1){ this.show('No imported tag metadata to clear'); return; }
      const okConfirm = await confirmDialog({ title: 'Clear Imported Tag Metadata', message: 'Clear all imported tag metadata?', detail: 'Tags still used by the ladder project will be auto-discovered again.', confirmText: 'Clear Metadata', tone: 'danger' });
      if(!okConfirm) return;
      this.clearImportedTagRegistryRows();
      this.pruneUnusedTagRegistryEdits();
      this.show('Imported tag metadata cleared');
      this.$forceUpdate();
    },
    async clearTagRegistryEdits(){
      if(Object.keys(this.tagRegistryEdits || {}).length) {
        const okConfirm = await confirmDialog({ title: 'Reset Tag Metadata', message: 'Reset all edited tag metadata back to auto-discovered/imported defaults?', confirmText: 'Reset', tone: 'warning' });
        if(!okConfirm) return;
      }
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

<style scoped>
  .pilab-ladder-page {
    height: calc(100vh - var(--app-topbar-height));
    min-height: 0;
    overflow: hidden;
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
