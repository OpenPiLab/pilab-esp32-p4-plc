// Ladder wire-node graph and AngelScript transpiler.
export const ladderTranspilerMethods = {
sanitize(s){ return String(s||'TAG').replace(/[^A-Za-z0-9_]/g,'_').replace(/^([0-9])/,'_$1'); },
uniqueTypes(types){
      const m=new Map();
      for(const e of this.allElements()) if(types.includes(e.type)) m.set(e.type+':'+this.sanitize(e.tag), e);
      return [...m.values()];
    },
exprIsTrue(x){ return !x || x === 'true'; },
isWrappedExpr(expr){
      // True only when one outer parenthesis pair wraps the entire expression.
      // This prevents readable ladder output like ((A || B)) && C.
      expr = String(expr || '').trim();
      if(!expr.startsWith('(') || !expr.endsWith(')')) return false;
      let depth = 0;
      for(let i=0; i<expr.length; i++){
        const ch = expr[i];
        if(ch === '(') depth++;
        else if(ch === ')') depth--;
        if(depth === 0 && i < expr.length - 1) return false;
        if(depth < 0) return false;
      }
      return depth === 0;
    },
stripRedundantOuterParens(expr){
      expr = String(expr || '').trim();
      while(this.isWrappedExpr(expr)){
        const inner = expr.slice(1, -1).trim();
        if(!this.isWrappedExpr(inner) && inner === expr.slice(1, -1).trim()) {
          // Remove only one wrapper unless the result is still wrapped.
          expr = inner;
          break;
        }
        expr = inner;
      }
      return expr;
    },
exprAnd(parts){
      const clean = parts.filter(p => p && p !== 'true').map(p => String(p).trim());
      if(!clean.length) return 'true';
      if(clean.length === 1) return clean[0];
      return clean.map(p => {
        const expr = this.stripRedundantOuterParens(p);
        return expr.includes(' || ') ? '('+expr+')' : expr;
      }).join(' && ');
    },
exprOr(parts){
      const clean=[];
      for(const p of parts){
        if(!p || p === 'false') continue;
        if(p === 'true') return 'true';
        clean.push(this.stripRedundantOuterParens(String(p).trim()));
      }
      if(!clean.length) return 'false';
      if(clean.length === 1) return clean[0];
      return '(' + clean.join(' || ') + ')';
    },
elementCondition(e){
      if(!e) return 'true';
      const tag=this.sanitize(e.tag);
      if(e.type==='NO') return tag;
      if(e.type==='NC') return '!'+tag;
      if(['TON','TOF','CTU','CTD'].includes(e.type)) return tag+'.Q()';
      // Coils and unknown symbols do not block power flow.
      return 'true';
    },
seriesCondition(cells, startSlot=0, endSlot=8){
      const expr=[];
      for(let i=startSlot; i<endSlot; i++){
        const e=cells[i];
        if(!e) continue;
        if(e.type==='OUT') continue;
        expr.push(this.elementCondition(e));
      }
      return this.exprAnd(expr);
    },
ladderEdges(r){
      // Formal wire-node graph model.
      // Nodes are 0..8. Each main slot is an edge i -> i+1.
      // Each branch is a shortcut edge start -> end whose condition is the
      // series condition of all symbols on that branch path.
      // Because all legal edges move left-to-right, this is a tiny DAG and can
      // be solved with dynamic programming instead of fragile branch grouping.
      const edges=[];
      for(let i=0; i<8; i++){
        edges.push({kind:'main', from:i, to:i+1, slot:i, cond:this.elementCondition((r.main||[])[i])});
      }
      for(const br of (r.branches||[])){
        if(!Number.isInteger(br.start) || !Number.isInteger(br.end)) continue;
        if(br.start < 0 || br.end > 8 || br.end <= br.start) continue;
        edges.push({kind:'branch', from:br.start, to:br.end, branch:br, cond:this.seriesCondition(br.cells||[], br.start, br.end)});
      }
      return edges;
    },
expressionFromNode(r, startNode=0, endNode=8){
      const edges=this.ladderEdges(r);
      const byFrom=new Map();
      for(const e of edges){
        if(!byFrom.has(e.from)) byFrom.set(e.from, []);
        byFrom.get(e.from).push(e);
      }
      const memo=new Map();
      const solve=(node)=>{
        if(node===endNode) return 'true';
        if(node>endNode) return 'false';
        if(memo.has(node)) return memo.get(node);
        const alternatives=[];
        for(const e of (byFrom.get(node)||[])){
          if(e.to > endNode) continue;
          const tail=solve(e.to);
          if(tail==='false') continue;
          alternatives.push(this.exprAnd([e.cond, tail]));
        }
        const out=this.exprOr(alternatives);
        memo.set(node,out);
        return out;
      };
      return solve(startNode);
    },
powerToNodeExpression(r, targetNode){
      // Expression for power arriving at a wire node from the left.
      // Used for correct function-block input conditions and future live monitor mode.
      if(targetNode <= 0) return 'true';
      return this.expressionFromNode(r, 0, targetNode);
    },
branchInputToSlotExpression(r, br, slot){
      // Power arriving at a symbol on a branch = power at branch start node AND
      // all preceding symbols on that same branch path.
      return this.exprAnd([
        this.powerToNodeExpression(r, br.start),
        this.seriesCondition(br.cells||[], br.start, slot)
      ]);
    },
mainInputToSlotExpression(r, slot){
      // Power arriving at a main-path symbol = all valid paths from node 0 to
      // that symbol's left wire node.
      return this.powerToNodeExpression(r, slot);
    },
readableExpressionFromNode(r, startNode=0, endNode=8, ignoredBranchIds=null){
      // Human-readable ladder expression builder.
      //
      // The graph solver is excellent for correctness, but it expands classic
      // ladder into a sum-of-paths form:
      //     Start && Stop || SealIn && Stop
      //
      // This routine tries to preserve the way a human reads ladder left-to-right:
      //     (Start || SealIn) && Stop
      //
      // It handles ordinary branch groups where one or more branches leave the
      // same wire node and rejoin at the same wire node. More complex mixed-end
      // or partially-overlapping structures fall back to the formal graph solver.
      ignoredBranchIds = ignoredBranchIds || new Set();

      const validBranches = (r.branches||[]).filter(br =>
        br && !ignoredBranchIds.has(br.id) &&
        Number.isInteger(br.start) && Number.isInteger(br.end) &&
        br.start >= 0 && br.end <= 8 && br.end > br.start
      );

      const solve = (node, stopNode, ignored) => {
        if(node >= stopNode) return node === stopNode ? 'true' : null;

        const here = validBranches.filter(br =>
          !ignored.has(br.id) && br.start === node && br.end <= stopNode
        );

        if(here.length){
          const end = here[0].end;

          // If branches starting at the same node rejoin at different nodes,
          // readability grouping becomes ambiguous. Use graph fallback.
          if(here.some(br => br.end !== end)) return null;

          // Do not compress this branch group if another branch starts inside
          // the section we are about to group but rejoins after it. That is a
          // crossing/overlapping branch. Grouping the earlier branch would hide
          // the later branch from the main-section expression and could drop a
          // valid power-flow path. Let the formal graph solver handle it.
          const hasCrossingBranch = validBranches.some(br =>
            !ignored.has(br.id) &&
            !here.some(h => h.id === br.id) &&
            br.start > node && br.start < end &&
            br.end > end && br.end <= stopNode
          );
          if(hasCrossingBranch) return null;

          const nextIgnored = new Set(ignored);
          for(const br of here) nextIgnored.add(br.id);

          const mainSection = solve(node, end, nextIgnored);
          if(mainSection === null) return null;

          const alternatives = [];
          if(mainSection !== 'true') alternatives.push(mainSection);

          for(const br of here){
            const bx = this.seriesCondition(br.cells||[], br.start, br.end);
            if(bx !== 'true') alternatives.push(bx);
          }

          const group = alternatives.length ? this.exprOr(alternatives) : 'true';
          const tail = solve(end, stopNode, ignored);
          if(tail === null) return null;
          return this.exprAnd([group, tail]);
        }

        const e = (r.main||[])[node];
        const cond = e && e.type !== 'OUT' ? this.elementCondition(e) : 'true';
        const tail = solve(node+1, stopNode, ignored);
        if(tail === null) return null;
        return this.exprAnd([cond, tail]);
      };

      return solve(startNode, endNode, ignoredBranchIds);
    },
rungExpression(r){
      const readable = this.readableExpressionFromNode(r,0,8);
      if(readable && readable !== 'false') return readable;

      // Fallback for complex graph shapes that cannot be expressed cleanly as
      // nested left-to-right ladder groups. This preserves correctness.
      const expr=this.expressionFromNode(r,0,8);
      return expr && expr !== 'false' ? expr : 'false';
    },
outputs(r){
      const out=[];
      for(const e of r.main) if(e && e.type==='OUT') out.push(e);
      for(const b of r.branches) for(const e of b.cells) if(e && e.type==='OUT') out.push(e);
      return out;
    },
transpile(){
      const timers=this.uniqueTypes(['TON','TOF']);
      const counters=this.uniqueTypes(['CTU','CTD']);
      const scanMs=this.project.scan_ms||5;
      let s='// Generated by PiLab Ladder Logic Editor\n';
      s+='// Project: '+(this.project.name||'Untitled')+'\n\n';

      if(timers.length){
        s+='class TON\n{\n';
        s+='    uint preset_ms;\n    uint elapsed_ms = 0;\n    bool output = false;\n\n';
        s+='    TON(uint preset) { preset_ms = preset; }\n\n';
        s+='    void update(bool input, uint scan_ms)\n    {\n';
        s+='        if (input)\n        {\n';
        s+='            if (elapsed_ms < preset_ms) elapsed_ms += scan_ms;\n';
        s+='            if (elapsed_ms >= preset_ms) output = true;\n';
        s+='        }\n        else\n        {\n';
        s+='            elapsed_ms = 0;\n            output = false;\n';
        s+='        }\n    }\n\n';
        s+='    bool Q() const { return output; }\n    uint ET() const { return elapsed_ms; }\n};\n\n';

        s+='class TOF\n{\n';
        s+='    uint preset_ms;\n    uint elapsed_ms = 0;\n    bool output = false;\n\n';
        s+='    TOF(uint preset) { preset_ms = preset; }\n\n';
        s+='    void update(bool input, uint scan_ms)\n    {\n';
        s+='        if (input)\n        {\n';
        s+='            output = true;\n            elapsed_ms = 0;\n';
        s+='        }\n';
        s+='        else if (output)\n        {\n';
        s+='            if (elapsed_ms < preset_ms) elapsed_ms += scan_ms;\n';
        s+='            if (elapsed_ms >= preset_ms) { elapsed_ms = preset_ms; output = false; }\n';
        s+='        }\n';
        s+='        else\n        {\n';
        s+='            elapsed_ms = preset_ms;\n';
        s+='        }\n';
        s+='    }\n\n';
        s+='    bool Q() const { return output; }\n    uint ET() const { return elapsed_ms; }\n};\n\n';
      }

      if(counters.length){
        if(counters.some(c=>c.type==='CTU')){
          s+='class CTU\n{\n';
          s+='    uint preset;\n    uint count = 0;\n    bool last = false;\n    bool output = false;\n\n';
          s+='    CTU(uint pv) { preset = pv; }\n\n';
          s+='    void update(bool input, bool reset=false)\n    {\n';
          s+='        if (reset) { count = 0; output = false; last = input; return; }\n';
          s+='        if (input && !last && count < preset) count++;\n        last = input;\n        output = count >= preset;\n';
          s+='    }\n\n    bool Q() const { return output; }\n    uint CV() const { return count; }\n};\n\n';
        }
        if(counters.some(c=>c.type==='CTD')){
          s+='class CTD\n{\n';
          s+='    uint preset;\n    uint count;\n    bool last = false;\n    bool output = false;\n\n';
          s+='    CTD(uint pv) { preset = pv; count = pv; output = count == 0; }\n\n';
          s+='    void update(bool input, bool load=false)\n    {\n';
          s+='        if (load) { count = preset; output = false; last = input; return; }\n';
          s+='        if (input && !last && count > 0) count--;\n        last = input;\n        output = count == 0;\n';
          s+='    }\n\n    bool Q() const { return output; }\n    uint CV() const { return count; }\n};\n\n';
        }
      }

      for(const t of timers) s+=t.type+' '+this.sanitize(t.tag)+'('+(t.preset||1000)+');\n';
      for(const c of counters) s+=c.type+' '+this.sanitize(c.tag)+'('+(c.preset||10)+');\n';
      if(timers.length||counters.length) s+='\n';

      s+='void scan()\n{\n';
      this.project.rungs.forEach((r,i)=>{
        s+='    // Rung '+(i+1)+(r.comment?': '+r.comment:'')+'\n';
        if(r.kind==='script'){
          const code=String(r.code||'').replace(/\r\n/g,'\n').replace(/\r/g,'\n');
          if(code.trim()) s+=code.replace(/\n?$/,'\n');
          else s+='    // Empty AngelScript rung\n';
          s+='\n';
          return;
        }
        const rungExpr = this.rungExpression(r);
        s+='    bool rung_'+(i+1)+' = '+rungExpr+';\n';

        const updateBlocks=[];
        for(let slot=0; slot<r.main.length; slot++){
          const e = r.main[slot];
          if(e&&['TON','TOF','CTU','CTD'].includes(e.type)){
            updateBlocks.push({e,cond:this.mainInputToSlotExpression(r, slot)});
          }
        }
        for(const b of r.branches){
          for(let slot=b.start; slot<b.end; slot++){
            const e = b.cells[slot];
            if(e&&['TON','TOF','CTU','CTD'].includes(e.type)){
              updateBlocks.push({e,cond:this.branchInputToSlotExpression(r, b, slot)});
            }
          }
        }

        for(const u of updateBlocks){
          if(u.e.type==='TON'||u.e.type==='TOF') s+='    '+this.sanitize(u.e.tag)+'.update(('+u.cond+'), '+scanMs+');\n';
          else {
            const controlExpr = this.counterControlExpr ? this.counterControlExpr(u.e) : String(u.e.resetTag || '');
            const resetExpr = controlExpr ? (this.normalizeBoolExpression(controlExpr) || 'false') : 'false';
            s+='    '+this.sanitize(u.e.tag)+'.update(('+u.cond+'), ('+resetExpr+'));\n';
          }
        }

        for(const o of this.outputs(r)) s+='    '+this.sanitize(o.tag)+' = rung_'+(i+1)+';\n';
        s+='\n';
      });
      s+='}\n';
      return s;
    }
};
