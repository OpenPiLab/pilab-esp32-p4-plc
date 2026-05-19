// Project validation helpers. These are Vue method mixins for now.
export const ladderValidationMethods = {
issueClass(level){
      if(level==='error') return 'bg-red-500/10 border-red-300/35';
      if(level==='warning') return 'bg-amber-500/10 border-amber-300/35';
      return 'bg-blue-500/10 border-blue-300/30';
    },
issueBadgeClass(level){
      if(level==='error') return 'bg-red-500/15 border-red-300/40 text-red-200';
      if(level==='warning') return 'bg-amber-500/15 border-amber-300/40 text-amber-200';
      return 'bg-blue-500/15 border-blue-300/40 text-blue-200';
    },
validateProject(){
      const issues=[];
      const add=(level,title,where,message)=>issues.push({id:issues.length+'_'+level+'_'+title,level,title,where,message});
      const reserved=new Set(['true','false','class','void','bool','uint','int','float','double','string','if','else','for','while','return','const']);
      const allOuts=[];
      const functionBlocks=[];

      if(!this.project || !Array.isArray(this.project.rungs)){
        add('error','Project model is invalid','Project','The project must contain a rungs array.');
        return issues;
      }

      this.project.rungs.forEach((r,ri)=>{
        const rungName='Rung '+(ri+1)+(r.comment ? ' — '+r.comment : '');
        if(r.kind==='script') {
          const code = String(r.code || '');
          if(!code.trim()) add('warning','Empty AngelScript rung',rungName,'This custom code rung is empty.');

          const analyzer = this.analyzeScriptRungForJsSimulator || this.jsAnalyzeScriptRungForSimulator;
          if(analyzer && code.trim()){
            const analysis = analyzer.call(this, code) || {};
            if(analysis.braceBalance){
              add('warning','Unbalanced AngelScript braces',rungName,`This script rung has ${analysis.braceBalance > 0 ? analysis.braceBalance + ' unmatched opening' : Math.abs(analysis.braceBalance) + ' unmatched closing'} brace(s). The PLC compiler may reject it, and the browser simulator may not match your intent.`);
            }
            const unsupported = Array.isArray(analysis.unsupportedLines) ? analysis.unsupportedLines : [];
            if(unsupported.length){
              const examples = unsupported.slice(0, 3).map(x => `line ${x.line}: ${String(x.text || '').trim()}`).join('; ');
              add('warning','AngelScript not simulated in browser',rungName,`${unsupported.length} line(s) are kept in the AngelScript export but are outside the JavaScript simulator subset${examples ? ': ' + examples : '.'}`);
            }
          }

          const tags = this.jsScriptRungTags ? this.jsScriptRungTags(code) : [];
          for(const raw of tags){
            if(!this.tagIsLegal(raw)) add('error','Illegal script tag name',rungName,'Script tag "'+raw+'" is not a valid AngelScript identifier.');
            else if(reserved.has(raw)) add('error','Reserved script tag name',rungName,'Script tag "'+raw+'" is an AngelScript keyword or reserved literal.');
          }
          return;
        }
        const elements=[];
        const pushEl=(e,lane,slot,branchIndex=null)=>{
          if(!e) return;
          elements.push({e,lane,slot,branchIndex});
          const raw=String(e.tag||'').trim();
          const where=branchIndex===null ? rungName+', main slot '+(slot+1) : rungName+', branch '+(branchIndex+1)+', slot '+(slot+1);
          if(!raw) add('error','Missing tag',where,'Every symbol needs a tag name before the generated AngelScript can be trusted.');
          else if(!this.tagIsLegal(raw)) add('error','Illegal tag name',where,'Tag "'+raw+'" is not a valid AngelScript identifier. Use letters, numbers, and underscores, and do not start with a number.');
          else if(reserved.has(raw)) add('error','Reserved tag name',where,'Tag "'+raw+'" is an AngelScript keyword or reserved literal. Rename it before compiling.');

          if(['OUT','SET','RST'].includes(e.type)) allOuts.push({tag:this.sanitize(raw), raw, type:e.type, rung:ri, where});
          if(['TON','TOF','CTU','CTD'].includes(e.type)){
            functionBlocks.push({tag:this.sanitize(raw), raw, type:e.type, preset:e.preset, where});
            if(!(Number(e.preset)>0)) add('error','Invalid preset',where,e.type+' preset must be a positive number.');
            if((e.type==='CTU'||e.type==='CTD') && e.resetTag && !this.normalizeBoolExpression(e.resetTag)) add('error','Invalid reset expression',where,'Counter reset/load expression may only use tags, true/false, !, &&, ||, parentheses, and spaces.');
          }
          if(['OUT','SET','RST'].includes(e.type) && slot < 7) add('warning','Coil before end of rung',where,'Coils normally belong near the right side of the rung. The transpiler treats coils as non-blocking, but this may be visually confusing.');
        };

        if(!Array.isArray(r.main) || r.main.length!==8){
          add('error','Invalid main path',rungName,'Each rung must have exactly 8 main instruction slots.');
        }

        (r.main||[]).forEach((e,slot)=>pushEl(e,'main',slot));

        if(!Array.isArray(r.branches)){
          add('error','Invalid branch list',rungName,'Each rung must contain a branches array.');
        }

        (r.branches||[]).forEach((br,bi)=>{
          const where=rungName+', branch '+(bi+1);
          if(!Number.isInteger(br.start) || !Number.isInteger(br.end)) add('error','Invalid branch nodes',where,'Branch start and end must be integer wire-node indexes.');
          else {
            if(br.start < 0 || br.end > 8) add('error','Branch outside rung',where,'Branch nodes must be between 0 and 8.');
            if(br.end <= br.start) add('error','Invalid branch span',where,'A branch must end to the right of where it starts.');
          }
          if(!Array.isArray(br.cells) || br.cells.length!==8) add('error','Invalid branch cells',where,'Each branch must have an 8-slot cells array so it aligns with the main wire-node model.');
          (br.cells||[]).forEach((e,slot)=>{
            if(e && (slot < br.start || slot >= br.end)) add('error','Branch cell outside span',where+', slot '+(slot+1),'This branch contains a symbol outside its start/end wire-node span. It will not be rendered or transpiled correctly.');
            pushEl(e,'branch',slot,bi);
          });
          const spanCells=(br.cells||[]).slice(br.start,br.end).filter(Boolean);
          if(spanCells.length===0) add('warning','Empty branch path',where,'This branch creates a parallel wire path with no symbols. That may intentionally bypass logic, but it is usually suspicious.');
        });

        const branches=r.branches||[];
        for(let a=0; a<branches.length; a++){
          for(let b=a+1; b<branches.length; b++){
            const A=branches[a], B=branches[b];
            const same=A.start===B.start && A.end===B.end;
            const disjoint=A.end<=B.start || B.end<=A.start;
            const nested=(A.start<=B.start && A.end>=B.end) || (B.start<=A.start && B.end>=A.end);
            const overlap=!disjoint;
            if(overlap && !same){
              add('info','Complex branch structure',rungName,'This rung uses overlapping or nested branch spans. The graph-based transpiler now supports this, but it is still worth testing the generated AngelScript against your intended power flow.');
            }
          }
        }

        if(elements.length===0) add('warning','Empty rung',rungName,'This rung contains no symbols. It will generate rung logic that is always true, but it has no output.');
        const outs=elements.filter(x=>['OUT','SET','RST'].includes(x.e.type));
        if(elements.length>0 && outs.length===0) add('warning','Outputless rung',rungName,'This rung has logic but no output coil. It may be unfinished or intended only for function block updates.');
      });

      const coilMap=new Map();
      for(const o of allOuts.filter(x=>x.type==='OUT')){
        if(!coilMap.has(o.tag)) coilMap.set(o.tag,[]);
        coilMap.get(o.tag).push(o);
      }
      for(const [tag,items] of coilMap){
        if(tag && items.length>1) add('error','Duplicate output coil','Project','Output tag "'+items[0].raw+'" is written by '+items.length+' coils. Multiple ladder rungs writing the same coil can create scan-order bugs.');
      }

      const fbMap=new Map();
      for(const fb of functionBlocks){
        const key=fb.type+':'+fb.tag;
        if(!fbMap.has(key)) fbMap.set(key,[]);
        fbMap.get(key).push(fb);
      }
      for(const [key,items] of fbMap){
        if(items.length>1) add('error','Duplicate function block instance','Project',items[0].type+' instance "'+items[0].raw+'" appears '+items.length+' times. Timers/counters should have unique instance tags.');
      }

      return issues;
    }
};
