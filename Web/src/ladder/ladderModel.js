// Core ladder data model, geometry helpers, and JSON normalization.
import { addFormalSchemaMetadata, assertValidLadderProjectShape } from './ladderSchema.js';

export const ladderModelMethods = {
uid(){ return Math.random().toString(16).slice(2,10); },
tagIsLegal(tag){ return /^[A-Za-z_][A-Za-z0-9_]*$/.test(String(tag||'')); },
nodeX(n){ return this.nodeStartX + n*this.nodeStep; },
slotCenterX(s){ return (this.nodeX(s)+this.nodeX(s+1))/2; },
branchLane(r, br){
      // Assign visual branch rows by horizontal overlap instead of insertion order.
      // Non-overlapping branches can share the same vertical lane, which keeps
      // rungs compact and easier to read. This is a drawing-only concern and
      // does not alter the JSON model or transpiler semantics.
      const lanes=[];
      for(const b of r.branches){
        let lane=0;
        while(true){
          const conflict=(lanes[lane]||[]).some(x => !(b.end <= x.start || b.start >= x.end));
          if(!conflict) break;
          lane++;
        }
        if(!lanes[lane]) lanes[lane]=[];
        lanes[lane].push(b);
        if(b.id===br.id) return lane;
      }
      return 0;
    },
branchY(r, br){ return this.mainY + 78*(this.branchLane(r, br)+1); },
branchLaneCount(r){
      const lanes=[];
      for(const b of r.branches){
        let lane=0;
        while(true){
          const conflict=(lanes[lane]||[]).some(x => !(b.end <= x.start || b.start >= x.end));
          if(!conflict) break;
          lane++;
        }
        if(!lanes[lane]) lanes[lane]=[];
        lanes[lane].push(b);
      }
      return lanes.length;
    },
rungHeight(r){ return 120 + this.branchLaneCount(r)*78; },
branchNodes(br){ const out=[]; for(let n=br.start; n<=br.end; n++) out.push(n); return out; },
branchSlots(br){ const out=[]; for(let s=br.start; s<br.end; s++) out.push(s); return out; },
createRung(comment=''){ return { id:this.uid(), kind:'ladder', comment, main:Array(8).fill(null), branches:[] }; },
createScriptRung(comment='Script Lite rung'){ return { id:this.uid(), kind:'script', comment, code:`// PiLab Script Lite rung.
// Simulator-safe rules:
// - one statement per line
// - use braces for if / else
// - persistent memory should be stored in tags

if (HMI_Enable) {
    Q_Debug = true;
}
else {
    Q_Debug = false;
}` }; },
createBranch(start,end){ return { id:this.uid(), start, end, cells:Array(8).fill(null) }; },
cloneRungDeep(r){ const c=JSON.parse(JSON.stringify(r)); const renew=(obj)=>{ if(obj&&typeof obj==='object'){ if(obj.id) obj.id=this.uid(); for(const k in obj) renew(obj[k]); }}; renew(c); return c; },
newSymbol(t){
      const e={id:this.uid(), type:t, tag:this.defaultTag(t)};
      if(t==='TON'||t==='TOF') { e.preset=1000; e.param={enabled:false, tag:`${e.tag}_PT`, min:0, max:600000}; }
      if(t==='CTU') { e.preset=10; e.resetTag='ResetPB'; e.param={enabled:false, tag:`${e.tag}_PV`, min:0, max:999999}; }
      if(t==='CTD') { e.preset=10; e.resetTag='ReloadPB'; e.param={enabled:false, tag:`${e.tag}_PV`, min:0, max:999999}; }
      return e;
    },
defaultTag(t){
      if(t==='NO') return 'I0';
      if(t==='NC') return 'I1';
      if(t==='OUT') return 'Q0';
      if(t==='SET') return 'M_Latched';
      if(t==='RST') return 'M_Latched';
      if(t==='ONS') return 'ONS_Edge';
      if(t==='TON') return 'T_OnDelay';
      if(t==='TOF') return 'T_OffDelay';
      if(t==='CTU') return 'C_Up';
      if(t==='CTD') return 'C_Down';
      return 'TAG';
    },
hasPreset(e){ return e && ['TON','TOF','CTU','CTD'].includes(e.type); },
isCounter(e){ return e && ['CTU','CTD'].includes(e.type); },
nodeFill(r,n){
      if(this.branchStart && this.branchStart.rung===r && this.branchStart.node===n) return '#22d3ee';
      return this.mode==='branch' ? 'rgba(34,211,238,.16)' : '#0f172a';
    },
normalizeImportedProject(p){
      if(!p || typeof p !== 'object') throw new Error('Project root must be a JSON object.');
      if(!Array.isArray(p.rungs)) throw new Error('Project must contain a rungs array.');
      if(!p.name) p.name = 'Imported PiLab Ladder Project';
      if(typeof p.description !== 'string') p.description = p.description ? String(p.description) : '';
      if(!p.scan_ms) p.scan_ms = 5;
      addFormalSchemaMetadata(p);
      for(const r of p.rungs){
        if(!r.id) r.id = this.uid();
        if(typeof r.comment !== 'string') r.comment = r.comment ? String(r.comment) : '';
        if(r.kind === 'script'){
          if(typeof r.code !== 'string') r.code = '';
          continue;
        }
        r.kind = r.kind || 'ladder';
        if(!Array.isArray(r.main)) r.main = Array(8).fill(null);
        while(r.main.length < 8) r.main.push(null);
        if(r.main.length > 8) r.main = r.main.slice(0,8);
        if(!Array.isArray(r.branches)) r.branches = [];
        for(const b of r.branches){
          if(!b.id) b.id = this.uid();
          if(typeof b.start !== 'number') b.start = Number(b.start)||0;
          if(typeof b.end !== 'number') b.end = Number(b.end)||0;
          if(!Array.isArray(b.cells)) b.cells = Array(8).fill(null);
          while(b.cells.length < 8) b.cells.push(null);
          if(b.cells.length > 8) b.cells = b.cells.slice(0,8);
        }
        const normalizeSymbolParam = (e) => {
          if(!e || !['TON','TOF','CTU','CTD'].includes(e.type)) return;
          const paramName = (e.type==='TON'||e.type==='TOF') ? 'PT' : 'PV';
          const defaultMax = (e.type==='TON'||e.type==='TOF') ? 600000 : 999999;
          if(!e.param || typeof e.param !== 'object') e.param = { enabled:false };
          if(typeof e.param.enabled !== 'boolean') e.param.enabled = !!e.param.enabled;
          if(!e.param.tag) e.param.tag = `${this.sanitize(e.tag)}_${paramName}`;
          if(!Number.isFinite(Number(e.param.min))) e.param.min = 0;
          if(!Number.isFinite(Number(e.param.max))) e.param.max = defaultMax;
        };
        (r.main||[]).forEach(normalizeSymbolParam);
        for(const b of (r.branches||[])) (b.cells||[]).forEach(normalizeSymbolParam);
      }
      assertValidLadderProjectShape(p);
    },
branchCount(){ return this.project.rungs.reduce((a,r)=>a+((r.branches&&r.kind!=='script')?r.branches.length:0),0); },
allElements(){
      const a=[];
      for(const r of this.project.rungs){
        if(r.kind==='script') continue;
        for(const e of (r.main||[])) if(e) a.push(e);
        for(const b of (r.branches||[])) for(const e of (b.cells||[])) if(e) a.push(e);
      }
      return a;
    },
expressionTags(expr){
      const out=[];
      const reserved=new Set(['true','false']);
      const text=String(expr||'');
      const re=/\b[A-Za-z_][A-Za-z0-9_]*\b/g;
      let m;
      while((m=re.exec(text))){
        if(!reserved.has(m[0])) out.push(this.sanitize(m[0]));
      }
      return [...new Set(out)];
    },
normalizeBoolExpression(expr){
      const text=String(expr||'').trim();
      if(!text) return '';
      // Keep this deliberately tiny: identifiers, true/false, !, &&, ||, parentheses, and whitespace.
      // A reset expression is meant to be PLC-style Boolean logic, not arbitrary script.
      if(!/^[A-Za-z0-9_!&|()\s]+$/.test(text)) return '';
      return text.replace(/\b[A-Za-z_][A-Za-z0-9_]*\b/g,(name)=>{
        if(name==='true' || name==='false') return name;
        return this.sanitize(name);
      });
    }
};
