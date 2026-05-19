// Browser preview simulator. The generated AngelScript remains the source of truth.
export const ladderSimulatorMethods = {
counterControlExpr(el){
      if(!el) return '';
      return String(el.resetTag ?? el.resetExpr ?? el.loadTag ?? el.loadExpr ?? '');
    },
simCreateJsContext(){
      return {
        get: (name) => {
          const tag=this.sanitize(name);
          const b=this.simBlocks[tag];
          return b ? !!b.output : (this.simTags[tag] ?? false);
        },
        tag: (name) => this.simTagValue(name),
        set: (name, value) => { this.simTags[this.sanitize(name)] = value; },
        ensureBlock: (type, name, preset) => this.simEnsureBlock({ type, tag:name, preset }),
        block: (name, type=null, preset=undefined) => {
          const tag=this.sanitize(name);
          let existing=this.simBlocks[tag];
          if(!existing && !type) type='TON';
          if(type) return this.simEnsureBlock({ type, tag, preset });
          return existing || this.simEnsureBlock({ type:'TON', tag, preset:0 });
        }
      };
    },
simCompileGeneratedProgram(){
      const code = this.transpileJavaScript ? this.transpileJavaScript() : '';
      if(this.simCompiledCode === code && this.simProgram) return this.simProgram;
      const runnable = code.replace(/export\s+/g, '');
      const factory = Function(runnable + '\nreturn createPiLabLadderProgram;')();
      this.simCompiledCode = code;
      this.simProgram = factory(this.simCreateJsContext());
      return this.simProgram;
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
      for(const r of (this.project.rungs||[])){
        if(r.kind === 'script' && this.jsScriptRungTags){
          for(const t of this.jsScriptRungTags(r.code || '')) tags.add(this.sanitize(t));
        }
      }
      for(const k of Object.keys(this.simTags||{})) tags.add(this.sanitize(k));
      return [...tags].filter(t => !(this.simBlocks||{})[t]).sort();
    },
simEnsureBlock(e){
      if(!e || !['TON','TOF','CTU','CTD','ONS'].includes(e.type)) return null;
      const tag=this.sanitize(e.tag);
      const preset=Number(e.preset || (e.type==='TON'||e.type==='TOF' ? 1000 : 10));
      let b=this.simBlocks[tag];
      if(!b || b.type!==e.type){
        b={type:e.type, preset_ms:preset, preset:preset, elapsed_ms:0, remaining_ms:0, count:e.type==='CTD'?preset:0, last:false, input:false, output:false, resetExpr:this.counterControlExpr(e).trim()};
        if(e.type==='CTD') b.output = b.count===0;
        this.simBlocks[tag]=b;
      }
      this.simAttachBlockMethods(b);
      b.preset_ms=preset;
      b.preset=preset;
      b.resetExpr=this.counterControlExpr(e).trim();
      if(e.type==='TOF') b.remaining_ms = b.output && !b.input ? Math.max(0, (b.preset_ms||0) - (b.elapsed_ms||0)) : 0;
      return b;
    },
simAttachBlockMethods(b){
      if(!b || b.__pilabMethods) return b;
      b.__pilabMethods = true;
      b.setPreset = function(p){ this.preset_ms=Number(p||0); this.preset=Number(p||0); if(this.type==='CTD' && this.count === undefined) this.count=this.preset; };
      b.Q = function(){ return !!this.output; };
      b.ET = function(){ return this.elapsed_ms || 0; };
      b.CV = function(){ return this.count || 0; };
      b.update = function(input, arg=false){
        input=!!input; this.input=input;
        if(this.type==='TON'){
          const scan=Number(arg||0);
          if(input){ this.elapsed_ms=Math.min((this.elapsed_ms||0)+scan,this.preset_ms||0); this.output=this.elapsed_ms >= (this.preset_ms||0); }
          else { this.elapsed_ms=0; this.output=false; }
          this.remaining_ms=Math.max(0,(this.preset_ms||0)-(this.elapsed_ms||0));
        } else if(this.type==='TOF'){
          const scan=Number(arg||0);
          if(input){ this.output=true; this.elapsed_ms=0; this.remaining_ms=0; }
          else if(this.output){
            this.elapsed_ms=Math.min((this.elapsed_ms||0)+scan,this.preset_ms||0);
            this.remaining_ms=Math.max(0,(this.preset_ms||0)-(this.elapsed_ms||0));
            if(this.elapsed_ms >= (this.preset_ms||0)){ this.output=false; this.elapsed_ms=this.preset_ms||0; this.remaining_ms=0; }
          } else { this.elapsed_ms=this.preset_ms||0; this.remaining_ms=0; }
        } else if(this.type==='CTU'){
          const reset=!!arg;
          if(reset){ this.count=0; this.output=false; this.last=input; return; }
          if(input && !this.last && (this.count||0) < (this.preset||0)) this.count=(this.count||0)+1;
          this.last=input; this.output=(this.count||0) >= (this.preset||0);
        } else if(this.type==='CTD'){
          const load=!!arg;
          if(load){ this.count=this.preset||0; this.output=false; this.last=input; return; }
          if(input && !this.last && (this.count||0)>0) this.count--;
          this.last=input; this.output=(this.count||0)===0;
        } else if(this.type==='ONS'){
          this.output=input && !this.last;
          this.last=input;
        }
      };
      return b;
    },
simBlockFor(e){
      if(!e || !['TON','TOF','CTU','CTD','ONS'].includes(e.type)) return null;
      return this.simEnsureBlock(e);
    },
simRawTagValue(tag){
      tag=this.sanitize(tag);
      const b=this.simBlocks[tag];
      if(b) return !!b.output;
      return (this.simTags && Object.prototype.hasOwnProperty.call(this.simTags, tag)) ? this.simTags[tag] : false;
    },
simTagValue(tag){
      // Ladder contact semantics: booleans are used directly, numbers are false
      // only when zero/NaN, and strings are false only when empty/false/0.
      const v=this.simRawTagValue(tag);
      if(typeof v === 'number') return Number.isFinite(v) && v !== 0;
      if(typeof v === 'string'){
        const t=v.trim().toLowerCase();
        return !(t === '' || t === 'false' || t === '0');
      }
      return !!v;
    },
simTagBoolValue(tag){
      return this.simTagValue(tag);
    },
simTagType(tag){
      const v=this.simRawTagValue(tag);
      if(typeof v === 'number') return 'number';
      if(typeof v === 'string') return 'string';
      return 'bool';
    },
simTagEditText(tag){
      const v=this.simRawTagValue(tag);
      if(typeof v === 'number') return Number.isFinite(v) ? String(v) : '0';
      if(typeof v === 'string') return v;
      return v ? 'true' : 'false';
    },
isSimTagWatched(tag){
      tag=this.sanitize(tag);
      return !!(this.simWatchTags && this.simWatchTags[tag]);
    },
toggleSimWatchTag(tag){
      tag=this.sanitize(tag);
      if(!tag) return;
      if(!this.simWatchTags) this.simWatchTags={};
      if(this.simWatchTags[tag]) delete this.simWatchTags[tag];
      else this.simWatchTags[tag]=true;
      this.$forceUpdate();
    },
setSimTagType(tag, type){
      tag=this.sanitize(tag);
      const current=this.simRawTagValue(tag);
      if(type === 'number'){
        const n=Number(current);
        this.simTags[tag]=Number.isFinite(n) ? n : 0;
      } else if(type === 'string'){
        this.simTags[tag]=String(current ?? '');
      } else {
        this.simTags[tag]=this.simTagValue(tag);
      }
      this.simStep(false);
    },
setSimTagFromInput(tag, value){
      tag=this.sanitize(tag);
      const type=this.simTagType(tag);
      if(type === 'number'){
        const n=Number(value);
        this.simTags[tag]=Number.isFinite(n) ? n : 0;
      } else if(type === 'string'){
        const text=String(value ?? '');
        const lower=text.trim().toLowerCase();
        if(lower === 'true') this.simTags[tag]=true;
        else if(lower === 'false') this.simTags[tag]=false;
        else {
          const n=Number(text);
          this.simTags[tag]=text.trim() !== '' && Number.isFinite(n) ? n : text;
        }
      } else {
        const lower=String(value ?? '').trim().toLowerCase();
        this.simTags[tag]=!(lower === '' || lower === 'false' || lower === '0' || lower === 'off');
      }
      this.simStep(false);
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

      // Browser timers are not precise at very small intervals. Many browsers
      // clamp or coalesce setInterval callbacks, so asking setInterval() to run
      // every 5 ms can either run much slower than requested or behave
      // inconsistently under UI load. Keep the UI timer at a relaxed cadence and
      // use a real-time accumulator to execute as many PLC scans as actually
      // elapsed. A 5 ms project scan therefore performs about 4 scans if the
      // browser wakes us up after 20 ms, instead of making the PLC clock run 4x
      // slow.
      const scanMs=Math.max(1, Number(this.project.scan_ms||5));
      const now=()=> (globalThis.performance && typeof globalThis.performance.now==='function') ? globalThis.performance.now() : Date.now();
      this.simRunLastMs=now();
      this.simRunAccumMs=0;

      // Do one immediate scan so input changes show up right away when Run is
      // pressed, then let the accumulator keep simulated time aligned with real
      // time.
      this.simStep(false);

      const run=()=>{
        const t=now();
        const previous=Number.isFinite(this.simRunLastMs) ? this.simRunLastMs : t;
        const delta=Math.max(0, t - previous);
        this.simRunLastMs=t;
        this.simRunAccumMs=(this.simRunAccumMs || 0) + delta;

        let scans=0;
        const maxScansPerUiTick=200;
        while(this.simRunAccumMs >= scanMs && scans < maxScansPerUiTick){
          this.simStep(false);
          this.simRunAccumMs -= scanMs;
          scans++;
        }

        // If the browser tab was paused for a long time, do not spend seconds
        // trying to catch up. Drop the excess backlog after a generous cap.
        if(scans >= maxScansPerUiTick) this.simRunAccumMs=0;
      };

      this.simTimer=setInterval(run, 20);
    },
stopSimRun(){ if(this.simTimer) clearInterval(this.simTimer); this.simTimer=null; this.simRunning=false; this.simRunAccumMs=0; },
simEvalElement(e){
      if(!e) return true;
      const tag=this.sanitize(e.tag);
      if(e.type==='NO') return this.simTagValue(tag);
      if(e.type==='NC') return !this.simTagValue(tag);
      if(['TON','TOF','CTU','CTD','ONS'].includes(e.type)) return this.simTagValue(tag);
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
          const on=this.simPowerThroughElement(nodes[i], (r.main||[])[i]);
          if(on){ main[i]=true; if(!nodes[i+1]){ nodes[i+1]=true; changed=true; } }
        }
        for(const br of (r.branches||[])){
          if(!br || br.end<=br.start || br.start<0 || br.end>8) continue;
          const on=this.simPowerThroughSeries(nodes[br.start], br.cells||[], br.start, br.end);
          if(on){ branch[br.id]=true; if(!nodes[br.end]){ nodes[br.end]=true; changed=true; } }
        }
        if(!changed) break;
      }
      return {nodes, main, branch, output:!!nodes[8]};
    },
simInputToMainSlot(r,slot){ return this.simEvaluateRungFlowToNode(r, slot); },
simInputToBranchSlot(r,br,slot){ return this.simEvaluateRungFlowToNode(r, br.start) && this.simEvalSeries(br.cells||[], br.start, slot); },
simPowerThroughElement(inputPower, e){
      if(!e || e.type === 'OUT') return !!inputPower;
      if(['TOF','CTU','CTD'].includes(e.type)) return this.simEvalElement(e);
      return !!inputPower && this.simEvalElement(e);
    },
simPowerThroughSeries(inputPower, cells, startSlot, endSlot){
      let power=!!inputPower;
      for(let i=startSlot; i<endSlot; i++){
        power=this.simPowerThroughElement(power, (cells||[])[i]);
      }
      return power;
    },
simEvaluateRungFlowToNode(r,target){
      const nodes=Array(9).fill(false); nodes[0]=true;
      for(let pass=0; pass<12; pass++){
        let changed=false;
        for(let i=0;i<Math.min(8,target);i++){
          const on=this.simPowerThroughElement(nodes[i], (r.main||[])[i]);
          if(on && i+1<=target && !nodes[i+1]){ nodes[i+1]=true; changed=true; }
        }
        for(const br of (r.branches||[])){
          if(!br || br.end<=br.start || br.start<0 || br.end>target) continue;
          const on=this.simPowerThroughSeries(nodes[br.start], br.cells||[], br.start, br.end);
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
      } else if(e.type==='ONS'){
        b.output=!!input && !b.last;
        b.last=!!input;
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
      // Execute the generated JavaScript backend instead of directly updating
      // simulator state from the ladder JSON. This keeps preview behavior aligned
      // with the AngelScript backend's emitted scan order.
      for(const e of this.allElements()) this.simEnsureBlock(e);
      try {
        const program=this.simCompileGeneratedProgram();
        program.scan();
      } catch(e) {
        console.error('Generated JavaScript simulator failed', e);
        if(showToast) this.show('Generated JS simulator failed');
      }

      // Wire highlighting is still computed from the current post-scan tag/block
      // state so the editor can show energized paths. The actual state update
      // above comes from the generated JavaScript program.
      const rungStates={};
      for(const r of (this.project.rungs||[])){
        if(r.kind==='script') continue;
        rungStates[r.id]=this.simEvaluateRungFlow(r);
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
      if(!e) return false;
      // Output/reset coils are actions, not contact conditions. They show green
      // only while rung power reaches them. SET is special for display: after it
      // latches its target tag true, the SET coil should show amber while the
      // latch is held but this SET instruction is not currently energized. RST
      // must never show amber because reset is only a momentary action.
      if(e.type==='OUT' || e.type==='RST') return false;
      if(e.type==='SET') return this.simTagValue(e.tag);
      return this.simEvalElement(e);
    },
simElementPowered(r,lane,br,slot){
      const e=this.simElementObject(r,lane,br,slot);
      if(!e) return false;
      if(['OUT','SET','RST'].includes(e.type)) return this.simElementInputPower(r,lane,br,slot);
      return this.simElementInputPower(r,lane,br,slot) && this.simEvalElement(e);
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
