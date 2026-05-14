// Browser preview simulator. The generated AngelScript remains the source of truth.
export const ladderSimulatorMethods = {
counterControlExpr(el){
      if(!el) return '';
      return String(el.resetTag ?? el.resetExpr ?? el.loadTag ?? el.loadExpr ?? '');
    },
simInputTagList(){
      // Tags that should be manually toggleable from the simulator panel.
      // This includes normal symbol tags plus any tags referenced inside
      // counter reset/load expressions, such as "ResetPB || !AutoMode".
      // Function-block instance tags are removed below because they are shown
      // in the Blocks section instead of as manual input buttons.
      const tags=new Set();
      for(const e of this.allElements()){
        if(!e) continue;
        if(e.tag) tags.add(this.sanitize(e.tag));
        if(this.isCounter(e) && this.counterControlExpr(e)){
          for(const t of this.expressionTags(this.counterControlExpr(e))){
            tags.add(this.sanitize(t));
          }
        }
      }
      for(const k of Object.keys(this.simTags||{})) tags.add(this.sanitize(k));
      return [...tags].filter(t => !(this.simBlocks||{})[t]).sort();
    },
simEnsureBlock(e){
      if(!e || !['TON','TOF','CTU','CTD'].includes(e.type)) return null;
      const tag=this.sanitize(e.tag);
      const preset=Number(e.preset || (e.type==='TON'||e.type==='TOF' ? 1000 : 10));
      let b=this.simBlocks[tag];
      if(!b || b.type!==e.type){
        b={type:e.type, preset_ms:preset, preset:preset, elapsed_ms:0, remaining_ms:0, count:e.type==='CTD'?preset:0, last:false, input:false, output:false, resetExpr:this.counterControlExpr(e).trim()};
        if(e.type==='CTD') b.output = b.count===0;
        this.simBlocks[tag]=b;
      }
      b.preset_ms=preset;
      b.preset=preset;
      b.resetExpr=this.counterControlExpr(e).trim();
      if(e.type==='TOF') b.remaining_ms = b.output && !b.input ? Math.max(0, (b.preset_ms||0) - (b.elapsed_ms||0)) : 0;
      return b;
    },
simBlockFor(e){
      if(!e || !['TON','TOF','CTU','CTD'].includes(e.type)) return null;
      return this.simEnsureBlock(e);
    },
simTagValue(tag){
      tag=this.sanitize(tag);
      const b=this.simBlocks[tag];
      if(b) return !!b.output;
      return !!this.simTags[tag];
    },
simEvalBoolExpression(expr){
      const normalized=this.normalizeBoolExpression(expr);
      if(!normalized) return false;
      let js=normalized.replace(/\b[A-Za-z_][A-Za-z0-9_]*\b/g,(name)=>{
        if(name==='true' || name==='false') return name;
        return `this.simTagValue('${name}')`;
      });
      try{ return !!Function('return ('+js+');').call(this); }
      catch(e){ return false; }
    },
toggleSimTag(tag){
      // Toggle an input/internal simulator tag and immediately perform one scan so
      // wire/path highlighting updates at the same time as contact-state highlighting.
      // This preserves two separate cues:
      //   amber = contact/block condition is true
      //   green = energized power flow through wires/elements
      this.simTags[this.sanitize(tag)] = !this.simTagValue(tag);
      this.simStep(false);
    },
simReset(){
      this.stopSimRun();
      this.simTags={}; this.simBlocks={}; this.simRungs={}; this.simScanCount=0;
      this.show('Simulator reset');
      this.simStep(false);
    },
toggleSimRun(){
      if(this.simRunning) return this.stopSimRun();
      this.simRunning=true;
      const run=()=>{ this.simStep(false); };
      run();
      this.simTimer=setInterval(run, Math.max(20, Number(this.project.scan_ms||5)));
    },
stopSimRun(){ if(this.simTimer) clearInterval(this.simTimer); this.simTimer=null; this.simRunning=false; },
simEvalElement(e){
      if(!e) return true;
      const tag=this.sanitize(e.tag);
      if(e.type==='NO') return this.simTagValue(tag);
      if(e.type==='NC') return !this.simTagValue(tag);
      if(['TON','TOF','CTU','CTD'].includes(e.type)) return this.simTagValue(tag);
      return true;
    },
simEvalSeries(cells,start=0,end=8){
      for(let i=start;i<end;i++){
        const e=(cells||[])[i];
        if(!e || e.type==='OUT') continue;
        if(!this.simEvalElement(e)) return false;
      }
      return true;
    },
simEvaluateRungFlow(r){
      const nodes=Array(9).fill(false); nodes[0]=true;
      const main=Array(8).fill(false);
      const branch={};
      for(let pass=0; pass<12; pass++){
        let changed=false;
        for(let i=0;i<8;i++){
          const on=nodes[i] && this.simEvalElement((r.main||[])[i]);
          if(on){ main[i]=true; if(!nodes[i+1]){ nodes[i+1]=true; changed=true; } }
        }
        for(const br of (r.branches||[])){
          if(!br || br.end<=br.start || br.start<0 || br.end>8) continue;
          const on=nodes[br.start] && this.simEvalSeries(br.cells||[],br.start,br.end);
          if(on){ branch[br.id]=true; if(!nodes[br.end]){ nodes[br.end]=true; changed=true; } }
        }
        if(!changed) break;
      }
      return {nodes, main, branch, output:!!nodes[8]};
    },
simInputToMainSlot(r,slot){ return this.simEvaluateRungFlowToNode(r, slot); },
simInputToBranchSlot(r,br,slot){ return this.simEvaluateRungFlowToNode(r, br.start) && this.simEvalSeries(br.cells||[], br.start, slot); },
simEvaluateRungFlowToNode(r,target){
      const nodes=Array(9).fill(false); nodes[0]=true;
      for(let pass=0; pass<12; pass++){
        let changed=false;
        for(let i=0;i<Math.min(8,target);i++){
          const on=nodes[i] && this.simEvalElement((r.main||[])[i]);
          if(on && i+1<=target && !nodes[i+1]){ nodes[i+1]=true; changed=true; }
        }
        for(const br of (r.branches||[])){
          if(!br || br.end<=br.start || br.start<0 || br.end>target) continue;
          const on=nodes[br.start] && this.simEvalSeries(br.cells||[],br.start,br.end);
          if(on && !nodes[br.end]){ nodes[br.end]=true; changed=true; }
        }
        if(!changed) break;
      }
      return !!nodes[target];
    },
simUpdateBlock(e,input){
      const b=this.simEnsureBlock(e); if(!b) return;
      const scan=Number(this.project.scan_ms||5);
      const controlExpr = this.counterControlExpr(e);
      const reset = controlExpr ? this.simEvalBoolExpression(controlExpr) : false;
      b.input=!!input;
      if(e.type==='TON'){
        if(input){ b.elapsed_ms=Math.min((b.elapsed_ms||0)+scan,b.preset_ms||0); b.output=b.elapsed_ms >= (b.preset_ms||0); }
        else { b.elapsed_ms=0; b.output=false; }
        b.remaining_ms=Math.max(0,(b.preset_ms||0)-(b.elapsed_ms||0));
      } else if(e.type==='TOF'){
        // IEC-style off-delay:
        // IN true  -> Q true immediately, ET reset to 0.
        // IN false -> Q stays true while ET counts up to PT, then Q false.
        if(input){
          b.output=true;
          b.elapsed_ms=0;
          b.remaining_ms=0;
        } else if(b.output){
          b.elapsed_ms=Math.min((b.elapsed_ms||0)+scan,b.preset_ms||0);
          b.remaining_ms=Math.max(0,(b.preset_ms||0)-(b.elapsed_ms||0));
          if(b.elapsed_ms >= (b.preset_ms||0)){
            b.output=false;
            b.elapsed_ms=b.preset_ms||0;
            b.remaining_ms=0;
          }
        } else {
          b.elapsed_ms=b.preset_ms||0;
          b.remaining_ms=0;
        }
      } else if(e.type==='CTU'){
        if(reset){ b.count=0; b.output=false; b.last=!!input; return; }
        if(input && !b.last && (b.count||0) < (b.preset||0)) b.count=(b.count||0)+1;
        b.last=!!input; b.output=(b.count||0) >= (b.preset||0);
      } else if(e.type==='CTD'){
        if(reset){ b.count=b.preset||0; b.output=false; b.last=!!input; return; }
        if(input && !b.last && (b.count||0)>0) b.count--;
        b.last=!!input; b.output=(b.count||0)===0;
      }
    },
simApplyScriptRung(r){
      // Safe mini-simulator for simple boolean AngelScript assignment lines.
      // It intentionally does not eval arbitrary user code.
      const code=String(r.code||'');
      for(const raw of code.split('\n')){
        const line=raw.replace(/\/\/.*$/,'').trim();
        const m=line.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+);$/);
        if(!m) continue;
        const target=this.sanitize(m[1]);
        const expr=m[2];
        if(!/^[A-Za-z0-9_!&|()\s.]+$/.test(expr)) continue;
        let js=expr.replace(/\b([A-Za-z_][A-Za-z0-9_]*)(\.Q\(\))?\b/g,(all,name,q)=>`this.simTagValue('${this.sanitize(name)}')`);
        try{ this.simTags[target]=!!Function('return ('+js+');').call(this); } catch(e){}
      }
    },
simStep(showToast=true){
      // Ensure blocks exist before evaluating, then update each rung in scan order.
      for(const e of this.allElements()) this.simEnsureBlock(e);
      const rungStates={};
      for(const r of (this.project.rungs||[])){
        if(r.kind==='script') { this.simApplyScriptRung(r); continue; }
        // Update function blocks from the power reaching their input side.
        for(let slot=0; slot<(r.main||[]).length; slot++){
          const e=r.main[slot];
          if(e && ['TON','TOF','CTU','CTD'].includes(e.type)) this.simUpdateBlock(e, this.simInputToMainSlot(r,slot));
        }
        for(const br of (r.branches||[])){
          for(let slot=br.start; slot<br.end; slot++){
            const e=(br.cells||[])[slot];
            if(e && ['TON','TOF','CTU','CTD'].includes(e.type)) this.simUpdateBlock(e, this.simInputToBranchSlot(r,br,slot));
          }
        }
        const flow=this.simEvaluateRungFlow(r);
        rungStates[r.id]=flow;
        for(const o of this.outputs(r)) this.simTags[this.sanitize(o.tag)] = flow.output;
      }
      this.simRungs=rungStates;
      this.simScanCount++;
      if(showToast) this.show('Simulator scan complete');
    },
simDisplayFlow(r){
      // Prefer the last scanned state, but provide a live display fallback so newly
      // imported/edited rungs do not appear to have dead wires before the first scan.
      return (this.simRungs && this.simRungs[r.id]) ? this.simRungs[r.id] : this.simEvaluateRungFlow(r);
    },
simNodeOn(r,n){ const f=this.simDisplayFlow(r); return !!(f && f.nodes && f.nodes[n]); },
simMainSegmentOn(r,seg){ const f=this.simDisplayFlow(r); return !!(f && f.main && f.main[seg]); },
simBranchOn(r,br){ const f=this.simDisplayFlow(r); return !!(f && f.branch && f.branch[br.id]); },
simElementObject(r,lane,br,slot){
      if(lane==='main') return (r.main||[])[slot] || null;
      return br ? ((br.cells||[])[slot] || null) : null;
    },
simElementInputPower(r,lane,br,slot){
      if(lane==='main') return this.simInputToMainSlot(r,slot);
      return br ? this.simInputToBranchSlot(r,br,slot) : false;
    },
simElementTrue(r,lane,br,slot){
      const e=this.simElementObject(r,lane,br,slot);
      return !!e && this.simEvalElement(e);
    },
simElementPowered(r,lane,br,slot){
      const e=this.simElementObject(r,lane,br,slot);
      return !!e && this.simElementInputPower(r,lane,br,slot) && this.simEvalElement(e);
    },
simElementFill(r,lane,br,slot){
      const e=this.simElementObject(r,lane,br,slot);
      if(!e) return 'rgba(2,6,23,.45)';
      if(this.simElementPowered(r,lane,br,slot)) return 'rgba(16,185,129,.22)';
      if(this.simElementTrue(r,lane,br,slot)) return 'rgba(245,158,11,.16)';
      return 'rgba(15,23,42,.96)';
    },
simElementStroke(r,lane,br,slot){
      const e=this.simElementObject(r,lane,br,slot);
      if(!e) return '#334155';
      if(this.simElementPowered(r,lane,br,slot)) return '#34d399';
      if(this.simElementTrue(r,lane,br,slot)) return '#f59e0b';
      return '#22d3ee';
    }
};
