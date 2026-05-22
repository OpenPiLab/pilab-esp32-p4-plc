<template>

  <main class="app-page space-y-4">
    <section class="flex flex-wrap items-center justify-between gap-3 border-b border-slate-800 pb-3">
      <div><div class="text-sky-400 font-black tracking-widest text-sm uppercase">LittleFS File Manager</div><div class="text-slate-500 text-xs font-mono">{{ cwd }} · {{ plcMode }} mode · {{ flashWritesAllowed ? 'writes enabled' : 'writes locked' }}</div></div>
      <button @click="load" class="px-3 py-2 rounded bg-slate-800 border border-slate-700 text-xs font-bold hover:bg-slate-700">REFRESH</button>
    </section>
    <section class="grid grid-cols-1 md:grid-cols-4 gap-3">
      <div class="bg-slate-900 border border-slate-800 rounded-xl p-4">
        <div class="text-[10px] text-slate-500 uppercase font-black">Files</div>
        <div class="text-3xl font-black text-sky-300">{{ fileCount }}</div>
      </div>
      <div class="bg-slate-900 border border-slate-800 rounded-xl p-4">
        <div class="text-[10px] text-slate-500 uppercase font-black">Folders</div>
        <div class="text-3xl font-black text-emerald-300">{{ dirCount }}</div>
      </div>
      <div class="bg-slate-900 border border-slate-800 rounded-xl p-4">
        <div class="text-[10px] text-slate-500 uppercase font-black">Bytes</div>
        <div class="text-3xl font-black text-amber-300">{{ totalBytes }}</div>
      </div>
      <div class="bg-slate-900 border border-slate-800 rounded-xl p-4">
        <div class="text-[10px] text-slate-500 uppercase font-black">Status</div>
        <div class="text-sm font-bold" :class="statusClass">{{ status }}</div>
      </div>
    </section>

    <section class="bg-slate-900 border border-slate-800 rounded-xl p-4 shadow-2xl space-y-3">
      <div class="flex flex-wrap gap-2 items-center">
        <button @click="goUp" class="px-3 py-2 rounded bg-slate-800 border border-slate-700 text-xs font-black hover:bg-slate-700">UP</button>
        <input v-model="newDir" :disabled="!flashWritesAllowed" class="bg-slate-950 border border-slate-700 rounded px-3 py-2 text-xs font-mono disabled:opacity-40 disabled:cursor-not-allowed" placeholder="new folder name">
        <button @click="mkdir" :disabled="!flashWritesAllowed" class="px-3 py-2 rounded border text-xs font-black" :class="flashWritesAllowed ? 'bg-emerald-800 border-emerald-600 hover:bg-emerald-700' : 'bg-slate-900 border-slate-800 text-slate-600 cursor-not-allowed'">MKDIR</button>
        <input ref="fileInput" type="file" :disabled="!flashWritesAllowed" class="text-xs text-slate-300 disabled:opacity-40 disabled:cursor-not-allowed" @change="uploadSelected">
      </div>
      <div v-if="!flashWritesAllowed" class="rounded border border-amber-800 bg-amber-950/35 text-amber-200 px-3 py-2 text-xs font-bold">PLC RUN mode active — filesystem writes are disabled. Press STOP on the main page before uploading, deleting, or creating folders.</div>
      <div class="text-xs text-slate-500">Uploads are written to the current directory. Text files can be viewed directly in the panel below.</div>
    </section>

    <section class="bg-slate-900 border border-slate-800 rounded-xl overflow-hidden shadow-2xl">
      <table class="w-full text-xs">
        <thead class="bg-black/40 text-slate-500 uppercase text-[10px] tracking-widest">
          <tr>
            <th class="text-left p-3">Name</th>
            <th class="text-left p-3">Type</th>
            <th class="text-right p-3">Size</th>
            <th class="text-right p-3">Actions</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="e in entries" :key="e.path" class="border-t border-slate-800 hover:bg-slate-800/40">
            <td class="p-3 font-mono"><button v-if="e.dir" @click="openDir(e.path)" class="text-sky-300 hover:text-sky-100 font-bold">📁 {{ e.name }}</button><span v-else>📄 {{ e.name }}</span></td>
            <td class="p-3 text-slate-400">{{ e.dir ? 'directory' : 'file' }}</td>
            <td class="p-3 text-right font-mono text-slate-400">{{ e.dir ? '-' : e.size }}</td>
            <td class="p-3 text-right space-x-2">
              <button v-if="!e.dir" @click="viewFile(e.path)" class="px-2 py-1 rounded bg-slate-800 border border-slate-700 hover:bg-slate-700">VIEW</button>
              <button v-if="!e.dir" @click="downloadFile(e.path)" class="px-2 py-1 rounded bg-sky-900 border border-sky-700 text-sky-200 hover:bg-sky-800">DOWNLOAD</button>
              <button @click="deletePath(e.path)" :disabled="!flashWritesAllowed" class="px-2 py-1 rounded border" :class="flashWritesAllowed ? 'bg-red-950 border-red-800 text-red-300 hover:bg-red-800' : 'bg-slate-900 border-slate-800 text-slate-600 cursor-not-allowed'">DELETE</button>
            </td>
          </tr>
        </tbody>
      </table>
    </section>

    <section class="bg-slate-900 border border-slate-800 rounded-xl p-4 shadow-2xl">
      <div class="flex items-center justify-between mb-2">
        <div class="font-black text-sky-400 uppercase tracking-widest text-xs">Text Viewer</div>
        <div class="text-xs text-slate-500 font-mono">{{ viewedPath }}</div>
      </div>
      <pre class="bg-black/50 border border-slate-800 rounded p-3 overflow-auto text-slate-300 text-xs min-h-64 max-h-[520px]"><code>{{ viewedText }}</code></pre>
    </section>
  </main>

</template>

<script setup>
import { ref, computed, onMounted } from 'vue';
import { usePlcStore } from '../stores/plcStore';
import { listFiles, mkdir as mkdirApi, uploadFile, viewFile as viewFileApi, deletePath as deletePathApi, filesDownloadUrl } from '../api/fileApi';
import { confirmDialog } from '../stores/appDialog';


    const store = usePlcStore();
    const cwd = ref('/'), entries = ref([]), status = ref('Loading...'), viewedText = ref(''), viewedPath = ref(''), newDir = ref(''), fileInput = ref(null);
    const plcMode = store.plcMode;
    const flashWritesAllowed = store.flashWritesAllowed;
    const enc = p => encodeURIComponent(p);
    const join = (a,b) => (a === '/' ? '/' + b : a + '/' + b).replace(/\/+/g,'/');
    const fileCount = computed(()=>entries.value.filter(e=>!e.dir).length);
    const dirCount = computed(()=>entries.value.filter(e=>e.dir).length);
    const totalBytes = computed(()=>entries.value.filter(e=>!e.dir).reduce((s,e)=>s+(e.size||0),0));
    const statusClass = computed(()=> status.value.startsWith('Error') ? 'text-red-300' : status.value.startsWith('Saved') || status.value.startsWith('Loaded') || status.value.startsWith('Uploaded') ? 'text-emerald-300' : 'text-slate-300');
    async function refreshMode(){ await store.refreshCommandCenter().catch(()=>{}); }
    async function load(){
      await refreshMode();
      try { const j = await listFiles(cwd.value); entries.value = j.entries || []; status.value = `Loaded ${entries.value.length} entries`; }
      catch(e){ status.value = 'Error: ' + (e.message || e); }
    }
    function openDir(p){ cwd.value = p; viewedText.value=''; viewedPath.value=''; load(); }
    function goUp(){ if(cwd.value === '/') return; const p = cwd.value.replace(/\/$/,'').split('/'); p.pop(); cwd.value = p.join('/') || '/'; load(); }
    async function mkdir(){
      await refreshMode();
      if(!flashWritesAllowed.value){ status.value='Error: PLC must be STOPPED before creating folders'; return; }
      const name = newDir.value.trim(); if(!name) return;
      try { const p = join(cwd.value, name); await mkdirApi(p); newDir.value=''; status.value='Saved folder'; await load(); }
      catch(e){ status.value='Error: ' + (e.message||e); }
    }
    async function uploadSelected(){
      await refreshMode();
      if(!flashWritesAllowed.value){ status.value='Error: PLC must be STOPPED before uploading files'; if(fileInput.value) fileInput.value.value=''; return; }
      const f = fileInput.value?.files?.[0]; if(!f) return;
      try { const p = join(cwd.value, f.name); await uploadFile(p, await f.arrayBuffer()); status.value='Uploaded ' + f.name; fileInput.value.value=''; await load(); }
      catch(e){ status.value='Error: ' + (e.message||e); }
    }
    async function viewFile(p){
      try { viewedPath.value = p; viewedText.value = await viewFileApi(p); status.value='Loaded text file'; }
      catch(e){ status.value='Error: ' + (e.message||e); }
    }
    function downloadFile(p){ window.location = filesDownloadUrl(p); }
    async function deletePath(p){
      await refreshMode();
      if(!flashWritesAllowed.value){ status.value='Error: PLC must be STOPPED before deleting files'; return; }
      const ok = await confirmDialog({ title: 'Delete File', message: 'Delete ' + p + '?', detail: 'This deletes the selected file or folder from the PLC filesystem.', confirmText: 'Delete', tone: 'danger' });
      if(!ok) return;
      try { await deletePathApi(p); status.value='Deleted'; await load(); }
      catch(e){ status.value='Error: ' + (e.message||e); }
    }
    onMounted(()=>{ load(); });
</script>
