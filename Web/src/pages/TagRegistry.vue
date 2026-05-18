<template>

  <main class="app-page space-y-4">
    <section class="flex flex-wrap items-center justify-between gap-3 border-b border-slate-800 pb-3">
      <div class="min-w-0"><div class="text-sky-400 font-black tracking-widest text-sm uppercase">PLC Tag Registry</div><div class="text-slate-500 text-xs leading-relaxed">User tags become AngelScript globals on the next script upload. Runtime digital I/O tags such as <span class="font-mono text-slate-300">I0</span>/<span class="font-mono text-slate-300">Q0</span> and firmware diagnostics such as <span class="font-mono text-slate-300">PLC_ScanOverrunCount</span> are shown for reference but are system-owned. Import/export uses the same <span class="font-mono text-slate-300">pilab_tags.json</span> format as the Ladder Editor.</div></div>
      <div class="flex flex-wrap items-center justify-end gap-2">
        <input ref="tagFileInput" type="file" accept=".json,application/json" class="hidden" @change="importTagsFromFile">
        <select v-model="importMode" class="bg-slate-950 border border-slate-700 rounded px-2 py-2 text-xs font-bold" title="Import mode">
          <option value="merge">MERGE IMPORT</option>
          <option value="replace">REPLACE IMPORT</option>
        </select>
        <button @click="triggerImportTags" class="px-3 py-2 rounded bg-indigo-900 border border-indigo-600 text-xs font-black hover:bg-indigo-800">IMPORT TAGS</button>
        <button @click="exportTagsFile" class="px-3 py-2 rounded bg-slate-800 border border-slate-600 text-xs font-black hover:bg-slate-700">EXPORT TAGS</button>
        <button @click="load" class="px-3 py-2 rounded bg-slate-800 border border-slate-700 text-xs font-bold hover:bg-slate-700">REFRESH</button>
        <button @click="save" class="px-4 py-2 rounded bg-sky-700 border border-sky-500 text-xs font-black hover:bg-sky-600">SAVE TAGS</button>
      </div>
    </section>
    <section class="grid grid-cols-1 md:grid-cols-4 gap-3">
      <div class="bg-slate-900 border border-slate-800 rounded-xl p-4">
        <div class="text-[10px] text-slate-500 uppercase font-black">Tags</div>
        <div class="text-3xl font-black text-sky-300">{{ tags.length }}</div>
      </div>
      <div class="bg-slate-900 border border-slate-800 rounded-xl p-4">
        <div class="text-[10px] text-slate-500 uppercase font-black">Globals</div>
        <div class="text-3xl font-black text-emerald-300">{{ scriptVisibleCount }}</div>
      </div>
      <div class="bg-slate-900 border border-slate-800 rounded-xl p-4">
        <div class="text-[10px] text-slate-500 uppercase font-black">Writable</div>
        <div class="text-3xl font-black text-amber-300">{{ writableCount }}</div>
      </div>
      <div class="bg-slate-900 border border-slate-800 rounded-xl p-4">
        <div class="text-[10px] text-slate-500 uppercase font-black">Status</div>
        <div class="text-sm font-bold" :class="statusClass">{{ status }}</div>
      </div>
    </section>

    <section class="bg-slate-900 border border-slate-800 rounded-xl overflow-hidden shadow-2xl">
      <div class="p-3 border-b border-slate-800 space-y-2">
        <div class="flex flex-wrap items-center justify-between gap-3">
          <div class="text-[11px] text-slate-400 leading-relaxed">
            Import expects <span class="font-mono text-slate-300">{ "tags": [...] }</span>. Merge keeps existing tags and imported duplicates overwrite by name. Replace clears the table first. Exact legacy demo names <span class="font-mono text-slate-300">AI0..AI3</span>/<span class="font-mono text-slate-300">AO0..AO3</span> are skipped; use descriptive tags such as <span class="font-mono text-slate-300">TankLevel</span>.
          </div>
          <button @click="addTag" class="px-3 py-2 rounded bg-emerald-800 border border-emerald-600 text-xs font-black hover:bg-emerald-700 whitespace-nowrap">+ ADD TAG</button>
        </div>
        <input v-model="filter" class="w-full bg-slate-950 border border-slate-700 rounded px-3 py-2 text-xs font-mono" placeholder="Filter tags by name, type, description, units, or value...">
      </div>

      <div class="overflow-x-auto">
        <table class="w-full text-xs">
          <thead class="bg-black/40 text-slate-500 uppercase text-[10px] tracking-widest">
            <tr>
              <th class="text-left p-3">Name / Global</th>
              <th class="text-left p-3">Type</th>
              <th class="text-left p-3">Value</th>
              <th class="text-left p-3">Units</th>
              <th class="text-left p-3">Min</th>
              <th class="text-left p-3">Max</th>
              <th class="text-left p-3">Flags</th>
              <th class="text-left p-3">Description</th>
              <th class="text-right p-3">Action</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="(t, idx) in filteredTags" :key="t.uid" class="border-t border-slate-800 hover:bg-slate-800/40">
              <td class="p-2 min-w-48"><input v-model.trim="t.name" @input="validate" :disabled="t.system" class="w-full bg-slate-950 border rounded px-2 py-1.5 font-mono disabled:opacity-60 disabled:cursor-not-allowed" :class="(t.system || nameOk(t.name)) ? 'border-slate-700' : 'border-red-600 text-red-300'"></td>
              <td class="p-2"><select v-model="t.type" :disabled="t.system" class="bg-slate-950 border border-slate-700 rounded px-2 py-1.5 disabled:opacity-60"><option>bool</option><option>int</option><option>float</option></select></td>
              <td class="p-2 min-w-32">
                <select v-if="t.type==='bool'" v-model="t.value" :disabled="t.system" class="w-full bg-slate-950 border border-slate-700 rounded px-2 py-1.5 disabled:opacity-60"><option :value="true">true</option><option :value="false">false</option></select>
                <input v-else-if="t.type==='int'" type="number" step="1" v-model.number="t.value" :disabled="t.system" class="w-full bg-slate-950 border border-slate-700 rounded px-2 py-1.5 disabled:opacity-60">
                <input v-else type="number" step="0.001" v-model.number="t.value" :disabled="t.system" class="w-full bg-slate-950 border border-slate-700 rounded px-2 py-1.5 disabled:opacity-60">
              </td>
              <td class="p-2"><input v-model="t.units" :disabled="t.system" class="w-20 bg-slate-950 border border-slate-700 rounded px-2 py-1.5 disabled:opacity-60"></td>
              <td class="p-2"><input type="number" v-model.number="t.min" :disabled="t.system" class="w-20 bg-slate-950 border border-slate-700 rounded px-2 py-1.5 disabled:opacity-60"></td>
              <td class="p-2"><input type="number" v-model.number="t.max" :disabled="t.system" class="w-20 bg-slate-950 border border-slate-700 rounded px-2 py-1.5 disabled:opacity-60"></td>
              <td class="p-2 min-w-40 space-y-1">
                <label class="flex items-center gap-2"><input type="checkbox" v-model="t.writable" :disabled="t.system" class="disabled:opacity-50"> Writable</label>
                <label class="flex items-center gap-2"><input type="checkbox" v-model="t.retentive" :disabled="t.system" class="disabled:opacity-50"> Retentive</label>
                <label class="flex items-center gap-2"><input type="checkbox" v-model="t.hmi_visible" :disabled="t.system" class="disabled:opacity-50"> HMI</label>
                <label class="flex items-center gap-2"><input type="checkbox" v-model="t.script_visible" :disabled="t.system" class="disabled:opacity-50"> Script global</label>
              </td>
              <td class="p-2 min-w-64"><input v-model="t.description" :disabled="t.system" class="w-full bg-slate-950 border border-slate-700 rounded px-2 py-1.5 disabled:opacity-60"></td>
              <td class="p-2 text-right"><button v-if="!t.system" @click="removeTag(t)" class="px-2 py-1 rounded bg-red-950 border border-red-800 text-red-300 font-bold hover:bg-red-800">DELETE</button><span v-else class="px-2 py-1 rounded bg-slate-800 border border-slate-700 text-slate-400 font-bold">SYSTEM</span></td>
            </tr>
          </tbody>
        </table>
      </div>
    </section>

    <section class="bg-slate-900 border border-slate-800 rounded-xl p-4 text-xs text-slate-400 leading-relaxed">
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
    </section>
  </main>

</template>

<script setup>
import { ref, computed, onMounted } from 'vue';
import { usePlcStore } from '../stores/plcStore';
import { loadTags, saveTags } from '../api/tagApi';


    const store = usePlcStore();
    const tags = ref([]), filter = ref(''), status = ref('Loading...');
    const tagFileInput = ref(null);
    const importMode = ref('merge');
    const runtimeName = /^(I\d+|Q\d+)$/;
    const legacyAnalogDemoName = /^(AI[0-3]|AO[0-3])$/;
    const plcSystemName = /^PLC_/;
    const reserved = /^(scan|Scan|true|false|bool|int|float|void|uint|if|else|for|while|return)$/;
    const isRuntimeTagName = n => runtimeName.test(String(n||''));
    const isLegacyAnalogDemoName = n => legacyAnalogDemoName.test(String(n||''));
    const isSystemTagName = n => isRuntimeTagName(n) || plcSystemName.test(String(n||''));
    const nameOk = n => /^[A-Za-z_][A-Za-z0-9_]{0,31}$/.test(n||'') && !reserved.test(n||'') && !plcSystemName.test(String(n||'')) && !isLegacyAnalogDemoName(n);
    const validate = () => {};
    const normalize = t => {
      const type = ['bool','int','float'].includes(String(t.type||'').toLowerCase()) ? String(t.type).toLowerCase() : 'bool';
      let value = t.value;
      if(value === undefined || value === null) value = type==='float' ? 0.0 : (type==='int' ? 0 : false);
      if(type === 'bool') {
        if(typeof value === 'string') value = !['', '0', 'false', 'off', 'no'].includes(value.trim().toLowerCase());
        else value = !!value;
      } else if(type === 'int') {
        const n = Number(value);
        value = Number.isFinite(n) ? Math.trunc(n) : 0;
      } else {
        const n = Number(value);
        value = Number.isFinite(n) ? n : 0.0;
      }
      return {
        uid: crypto.randomUUID ? crypto.randomUUID() : String(Date.now()+Math.random()),
        name:String(t.name||'').trim(), type, value,
        system: t.system === true || isSystemTagName(String(t.name||'').trim()),
        units:t.units||'', min:t.min ?? 0, max:t.max ?? 100,
        writable:t.writable ?? true, retentive:t.retentive ?? true,
        hmi_visible:t.hmi_visible ?? true, script_visible:t.script_visible ?? true,
        description:t.description||''
      };
    };
    const filteredTags = computed(()=>{ const q=filter.value.toLowerCase(); return q ? tags.value.filter(t => ([t.name,t.description,t.type,t.units,String(t.value),String(t.min),String(t.max)].join(' ')).toLowerCase().includes(q)) : tags.value; });
    const scriptVisibleCount = computed(()=>tags.value.filter(t=>t.script_visible).length);
    const writableCount = computed(()=>tags.value.filter(t=>t.writable).length);
    const statusClass = computed(()=> status.value.startsWith('Saved') || status.value.startsWith('Loaded') || status.value.startsWith('Imported') || status.value.startsWith('Exported') ? 'text-emerald-300' : status.value.startsWith('Error') ? 'text-red-300' : 'text-slate-300');
    function validatePayloadShape(j){
      if(!j || typeof j !== 'object' || !Array.isArray(j.tags)) throw new Error('Expected JSON object with a tags array.');
      return j.tags;
    }
    function publicTagsPayload(){
      // Runtime I/O tags returned by the firmware are shown for discovery, but
      // they are system-owned and must not be saved as user Tag Registry rows.
      return { tags: tags.value.filter(t => !t.system).map(({uid,system,...t})=>t) };
    }
    async function load(){
      try { const j = await loadTags(); tags.value = validatePayloadShape(j).map(normalize); status.value = `Loaded ${tags.value.length} tags`; }
      catch(e){ status.value = 'Error loading tags: ' + (e.message||e); }
    }
    function triggerImportTags(){ tagFileInput.value?.click(); }
    async function importTagsFromFile(event){
      const file = event?.target?.files?.[0];
      if(event?.target) event.target.value = '';
      if(!file) return;
      try {
        const text = await file.text();
        const rawTags = validatePayloadShape(JSON.parse(text));
        const incoming = rawTags.map(normalize).filter(t => t.system || nameOk(t.name));
        const skipped = rawTags.length - incoming.length;
        if(importMode.value === 'replace') {
          tags.value = incoming;
        } else {
          const byName = new Map(tags.value.map(t => [t.name, t]));
          for(const t of incoming) byName.set(t.name, t);
          tags.value = [...byName.values()].sort((a,b)=>a.name.localeCompare(b.name));
        }
        status.value = `Imported ${incoming.length} tags${skipped ? `, skipped ${skipped} invalid/reserved tags` : ''}`;
      } catch(e){ status.value = 'Error importing tags: ' + (e.message||e); }
    }
    function exportTagsFile(){
      const blob = new Blob([JSON.stringify(publicTagsPayload(), null, 2)], { type:'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = 'pilab_tags.json';
      document.body.appendChild(a);
      a.click();
      a.remove();
      URL.revokeObjectURL(url);
      status.value = `Exported ${tags.value.length} tags`;
    }
    function addTag(){ tags.value.push(normalize({name:'NewTag'+(tags.value.length+1), type:'bool', value:false, description:'User tag'})); }
    function removeTag(t){ tags.value = tags.value.filter(x=>x.uid!==t.uid); }
    async function save(){
      const seen = new Set();
      for (const t of tags.value) {
        if(t.system) continue;
        if(!nameOk(t.name)){ status.value='Error: invalid/reserved user tag name: '+t.name; return; }
        if(seen.has(t.name)){ status.value='Error: duplicate user tag name: '+t.name; return; }
        seen.add(t.name);
      }
      const payload = publicTagsPayload();
      try { await saveTags(payload); status.value='Saved. Upload the script again to use new globals.'; await store.refreshPlcData().catch(()=>{}); }
      catch(e){ status.value = 'Error saving tags: ' + (e.message||e); }
    }
    onMounted(load);
</script>
