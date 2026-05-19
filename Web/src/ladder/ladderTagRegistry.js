// Tag registry discovery and export helpers.
// These helpers intentionally keep tag metadata separate from the ladder
// project JSON so the exported file can be imported by the PiLab PLC Web
// Interface Tag Registry (/api/tags) without changing the ladder schema.

const PLC_WEB_RESERVED_RE = /^(scan|Scan|true|false|bool|int|float|void|uint|if|else|for|while|return|const|I\d+|Q\d+|AI\d+|AO\d+)$/;
const JS_RESERVED = new Set([
  'if','else','return','let','const','var','for','while','do','switch','case','break','continue','function',
  'new','class','this','typeof','void','delete','in','instanceof','true','false','null','undefined','NaN','Infinity',
  'Number','Boolean','Math','bool','int','uint','float','double','string','auto','scan'
]);

function defaultValueForType(type) {
  if (type === 'float') return 0.0;
  if (type === 'int') return 0;
  return false;
}

function minMaxForType(type) {
  if (type === 'bool') return { min: 0, max: 1 };
  return { min: 0, max: 100 };
}

function normalizeRegistryType(type) {
  return ['bool','int','float'].includes(String(type || '')) ? String(type) : 'bool';
}

function coerceRegistryValue(type, value) {
  type = normalizeRegistryType(type);
  if (type === 'bool') {
    if (typeof value === 'string') {
      const v = value.trim().toLowerCase();
      return !(v === '' || v === 'false' || v === '0' || v === 'off');
    }
    return !!value;
  }
  const n = Number(value);
  return Number.isFinite(n) ? n : 0;
}

function boolField(value, fallback=false) {
  if (value === undefined || value === null) return !!fallback;
  if (typeof value === 'string') {
    const v = value.trim().toLowerCase();
    return !(v === '' || v === 'false' || v === '0' || v === 'off');
  }
  return !!value;
}

export const ladderTagRegistryMethods = {
  tagRegistryReservedPattern(){ return PLC_WEB_RESERVED_RE; },

  tagRegistryIsReservedOrSystem(tag){
    const name = this.sanitize ? this.sanitize(tag) : String(tag || '');
    return !name || JS_RESERVED.has(name) || PLC_WEB_RESERVED_RE.test(name);
  },

  tagRegistryNormalizeName(tag){
    const name = this.sanitize ? this.sanitize(tag) : String(tag || '').replace(/[^A-Za-z0-9_]/g,'_').replace(/^([0-9])/,'_$1');
    return /^[A-Za-z_][A-Za-z0-9_]*$/.test(name) ? name : '';
  },

  tagRegistryAddUsage(map, rawName, usage = {}){
    const name = this.tagRegistryNormalizeName(rawName);
    if(!name || this.tagRegistryIsReservedOrSystem(name)) return;
    if(!map.has(name)) map.set(name, { name, reads:0, writes:0, hmi:false, memory:false, output:false, numeric:false, sources:new Set(), contexts:new Set() });
    const item = map.get(name);
    if(usage.read) item.reads++;
    if(usage.write) item.writes++;
    if(usage.hmi || /^HMI_/i.test(name)) item.hmi = true;
    if(usage.memory || /^M_/i.test(name)) item.memory = true;
    if(usage.output) item.output = true;
    if(usage.numeric) item.numeric = true;
    if(usage.source) item.sources.add(usage.source);
    if(usage.context) item.contexts.add(usage.context);
  },

  tagRegistryExpressionTags(expr){
    if(this.expressionTags) return this.expressionTags(expr);
    const out=[];
    const re=/\b[A-Za-z_][A-Za-z0-9_]*\b/g;
    let m;
    while((m=re.exec(String(expr || '')))) out.push(m[0]);
    return [...new Set(out)];
  },

  tagRegistryAnalyzeScriptCode(code, map, sourceLabel='script'){
    const text = String(code || '').replace(/\r\n/g,'\n').replace(/\r/g,'\n');
    const locals = new Set();
    const strip = (line) => this.jsStripLineComment ? this.jsStripLineComment(line) : String(line || '').replace(/\/\/.*$/,'');

    // Local declarations are not tag registry entries.
    for(const raw of text.split('\n')){
      const line = strip(raw).trim();
      const m = line.match(/^(?:bool|int|uint|float|double|string|auto)\s+([A-Za-z_][A-Za-z0-9_]*)\b/);
      if(m) locals.add(this.tagRegistryNormalizeName(m[1]));
    }

    const markNames = (expr, usage) => {
      const scrubbed = String(expr || '').replace(/("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')/g, ' ');
      const re=/\b[A-Za-z_][A-Za-z0-9_]*\b/g;
      let m;
      while((m=re.exec(scrubbed))){
        const name=this.tagRegistryNormalizeName(m[0]);
        if(!name || locals.has(name) || this.tagRegistryIsReservedOrSystem(name)) continue;
        const prev=scrubbed[m.index-1];
        if(prev === '.') continue;
        this.tagRegistryAddUsage(map, name, usage);
      }
    };

    for(const [idx, raw] of text.split('\n').entries()){
      const line = strip(raw).trim();
      if(!line) continue;
      const source = `${sourceLabel} line ${idx+1}`;

      let m = line.match(/^(?:bool|int|uint|float|double|string|auto)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*(.*))?;$/);
      if(m){ if(m[2]) markNames(m[2], { read:true, numeric:/^(?:int|uint|float|double)$/.test(line.split(/\s+/)[0]), source, context:'script-local-init' }); continue; }

      m = line.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*(\+\+|--);$/);
      if(m){
        const target=this.tagRegistryNormalizeName(m[1]);
        if(!locals.has(target)) this.tagRegistryAddUsage(map, target, { read:true, write:true, numeric:true, source, context:'script-incdec' });
        continue;
      }

      m = line.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*(=|\+=|-=|\*=|\/=|%=)\s*(.+);$/);
      if(m){
        const target=this.tagRegistryNormalizeName(m[1]);
        const numeric = /(?:^|[^=!<>])[-+]?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?[fF]?\b/.test(m[3]) || ['+=','-=','*=','/=','%='].includes(m[2]);
        if(!locals.has(target)) this.tagRegistryAddUsage(map, target, { write:true, read:m[2] !== '=', numeric, source, context:'script-write' });
        markNames(m[3], { read:true, numeric, source, context:'script-expression' });
        continue;
      }

      // Conditions and unsupported lines still reveal tag dependencies.
      const cond = line.match(/\b(?:if|else\s+if|while)\s*\((.*)\)/);
      if(cond) markNames(cond[1], { read:true, source, context:'script-condition' });
      else markNames(line, { read:true, source, context:'script' });
    }
  },

  discoverTagRegistryEntries(){
    const map = new Map();
    const add = (name, usage) => this.tagRegistryAddUsage(map, name, usage);

    for(const r of (this.project && this.project.rungs) || []){
      if(!r) continue;
      if(r.kind === 'script'){
        this.tagRegistryAnalyzeScriptCode(r.code || '', map, `Rung ${((this.project.rungs || []).indexOf(r))+1}`);
        continue;
      }

      const visitElement = (e, source) => {
        if(!e) return;
        if(['TON','TOF','CTU','CTD','ONS'].includes(e.type)){
          if((e.type === 'CTU' || e.type === 'CTD') && (e.resetTag || e.resetExpr || e.loadTag || e.loadExpr)){
            for(const t of this.tagRegistryExpressionTags(e.resetTag || e.resetExpr || e.loadTag || e.loadExpr)) add(t, { read:true, source, context:'counter-control' });
          }
          return;
        }
        if(e.type === 'OUT' || e.type === 'SET' || e.type === 'RST') add(e.tag, { write:true, output:e.type === 'OUT', source, context:e.type });
        else if(e.type === 'NO' || e.type === 'NC') add(e.tag, { read:true, hmi:/^HMI_/i.test(e.tag || ''), memory:/^M_/i.test(e.tag || ''), source, context:e.type });
      };

      (r.main || []).forEach((e, i) => visitElement(e, `Rung ${((this.project.rungs || []).indexOf(r))+1} main slot ${i+1}`));
      for(const [bi, br] of (r.branches || []).entries()){
        (br.cells || []).forEach((e, i) => visitElement(e, `Rung ${((this.project.rungs || []).indexOf(r))+1} branch ${bi+1} slot ${i+1}`));
      }
    }

    return [...map.values()].sort((a,b)=>a.name.localeCompare(b.name));
  },

  inferTagRegistryType(entry){
    const name = entry.name || '';
    if(entry.numeric) return /setpoint|level|speed|rpm|position|temperature|temp|pressure|flow|analog|percent|ratio|gain|scale|raw/i.test(name) ? 'float' : 'int';
    if(/counter|count|timer|delay|pulse|state|step|index|position|total/i.test(name)) return 'int';
    if(/setpoint|level|speed|rpm|temperature|temp|pressure|flow|analog|percent|ratio|gain|scale|raw/i.test(name)) return 'float';
    return 'bool';
  },

  inferTagRegistryDescription(entry){
    const name = entry.name || '';
    if(/^HMI_/i.test(name)) return `HMI/simulator tag ${name}. Auto-discovered from the ladder project.`;
    if(/^M_/i.test(name)) return `Internal memory tag ${name}. Auto-discovered from the ladder project.`;
    if(entry.output) return `User-defined output/status tag ${name}. Auto-discovered from project coils or script writes.`;
    if(entry.writes && !entry.reads) return `Script/logic output tag ${name}. Auto-discovered from project writes.`;
    if(entry.reads && !entry.writes) return `Input or permissive tag ${name}. Auto-discovered from project reads.`;
    return `Auto-discovered tag ${name} used by the ladder/script project.`;
  },

  defaultTagRegistryRow(entry){
    const type = normalizeRegistryType(this.inferTagRegistryType(entry));
    const mm = minMaxForType(type);
    const isHmi = /^HMI_/i.test(entry.name) || entry.hmi;
    const isMemory = /^M_/i.test(entry.name) || entry.memory;
    return {
      name: entry.name,
      type,
      value: defaultValueForType(type),
      units: '',
      min: mm.min,
      max: mm.max,
      writable: isHmi || (!isMemory && !entry.output),
      retentive: isMemory,
      // PiLab treats user tags as visible to both HMI and script by default.
      // The old per-tag HMI/script visibility flags are kept in the JSON for
      // compatibility, but the UI no longer exposes them.
      hmi_visible: true,
      script_visible: true,
      description: this.inferTagRegistryDescription(entry)
    };
  },

  normalizeTagRegistryRow(row){
    const type = normalizeRegistryType(row && row.type);
    const mm = minMaxForType(type);
    return {
      name: this.tagRegistryNormalizeName(row && row.name),
      type,
      value: coerceRegistryValue(type, row ? row.value : defaultValueForType(type)),
      units: String((row && row.units) ?? ''),
      min: Number.isFinite(Number(row && row.min)) ? Number(row.min) : mm.min,
      max: Number.isFinite(Number(row && row.max)) ? Number(row.max) : mm.max,
      writable: boolField(row && row.writable),
      retentive: boolField(row && row.retentive),
      hmi_visible: true,
      script_visible: true,
      description: String((row && row.description) ?? '')
    };
  },

  getTagRegistryOverride(name){
    const key = this.tagRegistryNormalizeName(name);
    const edits = this.tagRegistryEdits || {};
    return key && edits[key] ? edits[key] : null;
  },

  setTagRegistryOverride(name, patch){
    const key = this.tagRegistryNormalizeName(name);
    if(!key) return;
    if(!this.tagRegistryEdits) this.tagRegistryEdits = {};
    const current = this.tagRegistryEdits[key] || {};
    this.tagRegistryEdits[key] = { ...current, ...(patch || {}) };
  },

  clearTagRegistryOverride(name){
    const key = this.tagRegistryNormalizeName(name);
    if(this.tagRegistryEdits && key) delete this.tagRegistryEdits[key];
  },

  getImportedTagRegistryRow(name){
    const key = this.tagRegistryNormalizeName(name);
    const imported = this.tagRegistryImported || {};
    return key && imported[key] ? imported[key] : null;
  },

  setImportedTagRegistryRows(rows){
    const imported = {};
    for(const raw of (rows || [])){
      const row = this.normalizeTagRegistryRow(raw);
      if(!row.name || this.tagRegistryIsReservedOrSystem(row.name)) continue;
      imported[row.name] = row;
    }
    this.tagRegistryImported = imported;
  },

  clearImportedTagRegistryRows(){
    this.tagRegistryImported = {};
  },

  tagRegistryUsedNameSet(){
    return new Set(this.discoverTagRegistryEntries().map(entry => entry.name));
  },

  deleteImportedTagRegistryRow(name){
    const key = this.tagRegistryNormalizeName(name);
    if(!key || !this.tagRegistryImported || !this.tagRegistryImported[key]) return false;
    if(this.tagRegistryUsedNameSet().has(key)) return false;
    delete this.tagRegistryImported[key];
    if(this.tagRegistryEdits && this.tagRegistryEdits[key]) delete this.tagRegistryEdits[key];
    return true;
  },

  deleteUnusedImportedTagRegistryRows(){
    const used = this.tagRegistryUsedNameSet();
    let removed = 0;
    for(const name of Object.keys(this.tagRegistryImported || {})){
      if(used.has(name)) continue;
      delete this.tagRegistryImported[name];
      if(this.tagRegistryEdits && this.tagRegistryEdits[name]) delete this.tagRegistryEdits[name];
      removed++;
    }
    return removed;
  },

  pruneUnusedTagRegistryEdits(){
    const used = this.tagRegistryUsedNameSet();
    let removed = 0;
    for(const name of Object.keys(this.tagRegistryEdits || {})){
      if(used.has(name)) continue;
      if(this.tagRegistryImported && this.tagRegistryImported[name]) continue;
      delete this.tagRegistryEdits[name];
      removed++;
    }
    return removed;
  },

  importTagRegistryPayload(payload){
    const root = payload && typeof payload === 'object' ? payload : null;
    const rows = Array.isArray(root && root.tags) ? root.tags : (Array.isArray(root) ? root : null);
    if(!rows) throw new Error('Tag registry JSON must contain a tags array.');
    this.setImportedTagRegistryRows(rows);
    return Object.keys(this.tagRegistryImported || {}).length;
  },

  buildTagRegistryRows(){
    const entries = this.discoverTagRegistryEntries();
    const discovered = new Map(entries.map(entry => [entry.name, entry]));
    const names = new Set(entries.map(entry => entry.name));
    for(const name of Object.keys(this.tagRegistryImported || {})) names.add(name);
    for(const name of Object.keys(this.tagRegistryEdits || {})){
      const clean = this.tagRegistryNormalizeName(name);
      if(clean && !this.tagRegistryIsReservedOrSystem(clean)) names.add(clean);
    }

    return [...names].sort((a,b)=>a.localeCompare(b)).map(name => {
      const entry = discovered.get(name) || { name, reads:0, writes:0, hmi:/^HMI_/i.test(name), memory:/^M_/i.test(name), output:false, numeric:false, sources:new Set(), contexts:new Set() };
      const auto = discovered.has(name) ? this.defaultTagRegistryRow(entry) : null;
      const imported = this.getImportedTagRegistryRow(name);
      const base = imported || auto || this.normalizeTagRegistryRow({ name, description:`Imported tag ${name}. Not currently used by this ladder project.` });
      const override = this.getTagRegistryOverride(name) || {};
      const merged = this.normalizeTagRegistryRow({ ...base, ...override, name });
      return {
        ...merged,
        __auto: auto,
        __imported: imported,
        __usage: discovered.has(name) ? entry : null,
        __used: discovered.has(name),
        __edited: Object.keys(override).length > 0
      };
    });
  },

  buildTagRegistryPayload(){
    const tags = this.buildTagRegistryRows().map(({__auto, __imported, __usage, __used, __edited, ...tag}) => this.normalizeTagRegistryRow(tag));
    return { tags };
  },

  tagRegistryJson(){
    return JSON.stringify(this.buildTagRegistryPayload(), null, 2);
  }
};
