// Ladder wire-node graph and AngelScript transpiler.
export const ladderTranspilerMethods = {
sanitize(s){ return String(s||'TAG').replace(/[^A-Za-z0-9_]/g,'_').replace(/^([0-9])/,'_$1'); },
blockParamName(e){
      if(!e) return '';
      if(e.type==='TON'||e.type==='TOF') return 'PT';
      if(e.type==='CTU'||e.type==='CTD') return 'PV';
      return '';
    },
blockParamDefault(e){
      if(!e) return 0;
      return Number(e.preset || ((e.type==='TON'||e.type==='TOF') ? 1000 : 10));
    },
blockParamUnits(e){ return e && (e.type==='TON'||e.type==='TOF') ? 'ms' : 'count'; },
blockParamIsEnabled(e){ return !!(e && e.param && e.param.enabled); },
blockParamTag(e){
      if(!e) return '';
      const explicit = e.param && e.param.tag;
      if(explicit) return this.sanitize(explicit);
      const name = this.blockParamName(e);
      return name ? `${this.sanitize(e.tag)}_${name}` : '';
    },
blockParamType(e){ return 'int'; },
blockParamMin(e){
      const v = e && e.param ? Number(e.param.min) : NaN;
      return Number.isFinite(v) ? v : 0;
    },
blockParamMax(e){
      const v = e && e.param ? Number(e.param.max) : NaN;
      return Number.isFinite(v) ? v : ((e && (e.type==='TON'||e.type==='TOF')) ? 600000 : 999999);
    },
blockPresetExpression(e){ return this.blockParamIsEnabled(e) ? this.blockParamTag(e) : String(this.blockParamDefault(e)); },
blockMonitorFields(e){
      if(!e) return [];
      if(e.type==='TON'||e.type==='TOF') return ['Q:bool','ET:int','PT:int'];
      if(e.type==='CTU'||e.type==='CTD') return ['Q:bool','CV:int','PV:int'];
      return [];
    },
blockMonitorAnnotation(e){
      const fields = this.blockMonitorFields(e);
      if(!e || !fields.length) return '';
      return `[PiLabMonitor name="${this.sanitize(e.tag)}" type="${e.type}" fields="${fields.join(',')}"]`;
    },
blockParamAnnotation(e){
      if(!this.blockParamIsEnabled(e)) return '';
      const paramName = this.blockParamName(e);
      if(!paramName) return '';
      return `[PiLabParam name="${paramName}" tag="${this.blockParamTag(e)}" type="${this.blockParamType(e)}" default="${this.blockParamDefault(e)}" min="${this.blockParamMin(e)}" max="${this.blockParamMax(e)}"]`;
    },
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
      if(['TON','TOF','CTU','CTD','ONS'].includes(e.type)) return tag+'.Q()';
      // Coils and unknown symbols do not block power flow.
      return 'true';
    },
blockOutputCondition(e){
      // Function blocks are evaluated as devices with an input side and an
      // output side. Most block outputs still require left-side power plus Q.
      // Some function blocks have stored output semantics. TOF remains true
      // during its off-delay after input power falls, and CTU/CTD done outputs
      // remain true after their terminal count is reached until reset/load.
      // Therefore power to the right of these blocks is the block Q itself,
      // not input_power && Q. The block update input is still calculated
      // separately using power arriving at the block's left node.
      return e && ['TOF','CTU','CTD'].includes(e.type) ? this.sanitize(e.tag)+'.Q()' : null;
    },
exprWithElementPower(inputExpr, e){
      if(!e || ['OUT','SET','RST'].includes(e.type)) return inputExpr || 'false';
      const blockOut = this.blockOutputCondition(e);
      if(blockOut) return blockOut;
      return this.exprAnd([inputExpr, this.elementCondition(e)]);
    },
seriesPowerExpression(inputExpr, cells, startSlot=0, endSlot=8){
      let power = inputExpr || 'false';
      for(let i=startSlot; i<endSlot; i++){
        const e=(cells||[])[i];
        if(!e) continue;
        power=this.exprWithElementPower(power, e);
      }
      return power || 'false';
    },
powerExpressionsByNode(r, endNode=8){
      // Topological solve for the left-to-right ladder DAG while respecting
      // function-block output semantics. Stored-output blocks are allowed to
      // carry power to the right from their Q state even after the input path
      // has gone false.
      const nodes=Array(9).fill('false');
      nodes[0]='true';
      const branchesByStart=new Map();
      for(const br of (r.branches||[])){
        if(!br || !Number.isInteger(br.start) || !Number.isInteger(br.end)) continue;
        if(br.start < 0 || br.end > endNode || br.end <= br.start) continue;
        if(!branchesByStart.has(br.start)) branchesByStart.set(br.start, []);
        branchesByStart.get(br.start).push(br);
      }
      const orInto=(idx, expr)=>{
        nodes[idx]=this.exprOr([nodes[idx], expr]);
      };
      for(let i=0; i<Math.min(8,endNode); i++){
        orInto(i+1, this.exprWithElementPower(nodes[i], (r.main||[])[i]));
        for(const br of (branchesByStart.get(i)||[])){
          const cand=this.seriesPowerExpression(nodes[br.start], br.cells||[], br.start, br.end);
          orInto(br.end, cand);
        }
      }
      return nodes;
    },
seriesCondition(cells, startSlot=0, endSlot=8){
      const expr=[];
      for(let i=startSlot; i<endSlot; i++){
        const e=cells[i];
        if(!e) continue;
        if(['OUT','SET','RST'].includes(e.type)) continue;
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
      // Used for correct function-block input conditions and live monitor mode.
      if(targetNode <= 0) return 'true';
      return this.powerExpressionsByNode(r, targetNode)[targetNode] || 'false';
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
        const cond = e && !['OUT','SET','RST'].includes(e.type) ? this.elementCondition(e) : 'true';
        const tail = solve(node+1, stopNode, ignored);
        if(tail === null) return null;
        return this.exprAnd([cond, tail]);
      };

      return solve(startNode, endNode, ignoredBranchIds);
    },
rungExpression(r){
      const hasStoredOutputBlock = [...(r.main||[]), ...(r.branches||[]).flatMap(b => b.cells||[])].some(e => e && ['TOF','CTU','CTD'].includes(e.type));
      if(!hasStoredOutputBlock){
        const readable = this.readableExpressionFromNode(r,0,8);
        if(readable && readable !== 'false') return readable;
        const fallback=this.expressionFromNode(r,0,8);
        return fallback && fallback !== 'false' ? fallback : 'false';
      }
      // Use the node-power solver for stored-output block rungs. These blocks can
      // continue powering their right side from Q after the input side is false.
      const expr=(this.powerExpressionsByNode(r, 8)[8] || 'false');
      return expr && expr !== 'false' ? expr : 'false';
    },
outputs(r){
      const out=[];
      for(const e of r.main) if(e && e.type==='OUT') out.push(e);
      for(const b of r.branches) for(const e of b.cells) if(e && e.type==='OUT') out.push(e);
      return out;
    },
    latchWrites(r){
      const writes=[];
      for(let slot=0; slot<(r.main||[]).length; slot++){
        const e=(r.main||[])[slot];
        if(e && (e.type==='SET'||e.type==='RST')) writes.push({e, cond:this.mainInputToSlotExpression(r, slot)});
      }
      for(const b of (r.branches||[])){
        for(let slot=b.start; slot<b.end; slot++){
          const e=(b.cells||[])[slot];
          if(e && (e.type==='SET'||e.type==='RST')) writes.push({e, cond:this.branchInputToSlotExpression(r, b, slot)});
        }
      }
      return writes;
    },
    jsString(value){
      return JSON.stringify(String(value ?? ''));
    },
    jsTransformExpression(expr){
      // Convert the same simple C/AngelScript-style boolean expression used by
      // the AngelScript backend into simulator-safe JavaScript context calls.
      const blockNames = new Set(this.uniqueTypes(['TON','TOF','CTU','CTD','ONS']).map(e => this.sanitize(e.tag)));
      let source = String(expr || 'false');
      const placeholders = [];
      source = source.replace(/\b([A-Za-z_][A-Za-z0-9_]*)\s*\.\s*Q\s*\(\s*\)/g, (all, name) => {
        const token = `__PILAB_EXPR_${placeholders.length}__`;
        placeholders.push(`ctx.block(${this.jsString(name)}).Q()`);
        return token;
      });
      source = source.replace(/\b[A-Za-z_][A-Za-z0-9_]*\b/g, (name) => {
        if(name === 'true' || name === 'false') return name;
        const m = name.match(/^__PILAB_EXPR_(\d+)__$/);
        if(m) return placeholders[Number(m[1])];
        if(blockNames.has(name)) return `ctx.block(${this.jsString(name)}).Q()`;
        return `ctx.tag(${this.jsString(name)})`;
      });
      return source;
    },
    jsScriptBuiltinNames(){
      return new Set([
        'abs','min','max','sqrt','sin','cos','tan','asin','acos','atan','atan2','floor','ceil','round','pow','exp','log',
        'true','false','null','undefined','NaN','Infinity'
      ]);
    },
    jsScriptReservedNames(){
      return new Set([
        'if','else','return','let','const','var','for','while','do','switch','case','break','continue','function',
        'new','class','this','typeof','void','delete','in','instanceof','true','false','null','undefined','NaN','Infinity','Number','Boolean','Math'
      ]);
    },
    jsStripLineComment(line){
      // Strip // comments without treating // inside a quoted string as a comment.
      let out='', quote=null, esc=false;
      for(let i=0; i<String(line||'').length; i++){
        const ch=line[i], next=line[i+1];
        if(quote){ out+=ch; if(esc) esc=false; else if(ch==='\\') esc=true; else if(ch===quote) quote=null; continue; }
        if(ch==='"' || ch==="'"){ quote=ch; out+=ch; continue; }
        if(ch==='/' && next==='/') break;
        out+=ch;
      }
      return out;
    },
    jsScriptLiteralPlaceholders(source){
      const values=[];
      const text=String(source||'').replace(/("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')/g, (m)=>{
        const token=`__PILAB_LITERAL_${values.length}__`;
        values.push(m);
        return token;
      });
      return { text, values };
    },
    jsScriptRestorePlaceholders(source, values){
      return String(source||'').replace(/__PILAB_LITERAL_(\d+)__/g, (m,i)=>values[Number(i)] ?? m);
    },
    jsTransformScriptExpression(expr, locals=new Set()){
      // AngelScript-ish expression subset for simulator script rungs.
      // Supports arithmetic, comparisons, boolean logic, ternary expressions,
      // numeric/string/bool literals, local variables, tag variables, block Q/ET/CV,
      // and common math helpers like abs(), min(), max(), sqrt().
      let source=String(expr||'');
      const literalState=this.jsScriptLiteralPlaceholders(source);
      source=literalState.text;
      // JavaScript does not accept AngelScript/C++ float suffixes like 1.0f.
      source=source.replace(/(\b\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\s*[fF]\b/g, '$1');
      const placeholders=[];
      const hold=(replacement)=>{
        const token=`__PILAB_EXPR_${placeholders.length}__`;
        placeholders.push(replacement);
        return token;
      };

      source=source.replace(/\b([A-Za-z_][A-Za-z0-9_]*)\s*\.\s*(Q|ET|CV)\s*\(\s*\)/g, (all, name, method) =>
        hold(`ctx.block(${this.jsString(this.sanitize(name))}).${method}()`)
      );

      // AngelScript-style aliases that are useful in simple pasted snippets.
      source=source.replace(/\buint\s*\(/g, 'Number(')
                   .replace(/\bint\s*\(/g, 'Number(')
                   .replace(/\bfloat\s*\(/g, 'Number(')
                   .replace(/\bdouble\s*\(/g, 'Number(')
                   .replace(/\bbool\s*\(/g, 'Boolean(');

      const builtins=this.jsScriptBuiltinNames();
      const reserved=this.jsScriptReservedNames();
      source=source.replace(/\b[A-Za-z_][A-Za-z0-9_]*\b/g, (name, offset, whole) => {
        const ph=name.match(/^__PILAB_EXPR_(\d+)__$/);
        if(ph) return placeholders[Number(ph[1])];
        const lit=name.match(/^__PILAB_LITERAL_(\d+)__$/);
        if(lit) return name;
        if(locals && locals.has(name)) return name;
        if(reserved.has(name)) return name;
        if(builtins.has(name)){
          if(['true','false','null','undefined','NaN','Infinity'].includes(name)) return name;
          return `Math.${name}`;
        }
        // Do not transform property names after a dot. This keeps Math.max,
        // ctx.get, and block method calls intact after placeholders restore.
        const prev=whole[offset-1];
        if(prev==='.') return name;
        return `ctx.get(${this.jsString(this.sanitize(name))})`;
      });
      return this.jsScriptRestorePlaceholders(source, literalState.values);
    },
    jsScriptRungTags(code){
      // Best-effort tag discovery for simulator input buttons. This is not a
      // validator; it only keeps obvious locals, language words, and math
      // helper names out of the manual tag list.
      const reserved=this.jsScriptReservedNames();
      const builtins=this.jsScriptBuiltinNames();
      const types=new Set(['bool','int','uint','float','double','string','auto']);
      const locals=new Set();
      const tags=new Set();
      const scrub=(line)=>{
        let x=this.jsStripLineComment(line);
        x=x.replace(/("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')/g, ' ');
        return x;
      };
      for(const raw of String(code||'').replace(/\r\n/g,'\n').replace(/\r/g,'\n').split('\n')){
        const line=scrub(raw);
        const decl=line.match(/^\s*(?:bool|int|uint|float|double|string|auto)\s+([A-Za-z_][A-Za-z0-9_]*)/);
        if(decl) locals.add(this.sanitize(decl[1]));
      }
      for(const raw of String(code||'').replace(/\r\n/g,'\n').replace(/\r/g,'\n').split('\n')){
        const line=scrub(raw);
        const re=/\b[A-Za-z_][A-Za-z0-9_]*\b/g;
        let m;
        while((m=re.exec(line))){
          const name=m[0];
          const prev=line[m.index-1];
          if(prev==='.') continue; // Q/ET/CV and object-style properties
          if(reserved.has(name) || builtins.has(name) || types.has(name)) continue;
          const safe=this.sanitize(name);
          if(locals.has(safe)) continue;
          tags.add(safe);
        }
      }
      return [...tags];
    },
    analyzeScriptRungForJsSimulator(code){
      // Compatibility analyzer for Script Lite / AngelScript rungs used by the
      // browser/Node simulator. The generated AngelScript is still emitted as-is,
      // but the JavaScript simulator only supports a deterministic subset.
      const unsupportedLines=[];
      const locals=new Set();
      let braceBalance=0;
      const strip=(line)=>this.jsStripLineComment ? this.jsStripLineComment(line) : String(line||'').replace(/\/\/.*$/,'');
      const supportedExpr=(expr)=>{
        if(!String(expr||'').trim()) return false;
        // Reject obviously C/C++/AngelScript constructs that the mini emitter does
        // not model safely. Normal arithmetic, comparisons, logical ops, function
        // calls, block property reads, and literals are allowed.
        if(/\b(?:switch|for|while|do|return|break|continue|try|catch|throw|class|namespace)\b/.test(expr)) return false;
        return true;
      };
      const markUnsupported=(lineNo, text, reason='Not supported by JavaScript simulator subset')=>{
        unsupportedLines.push({ line:lineNo, text:String(text||''), reason });
      };

      const lines=String(code||'').replace(/\r\n/g,'\n').replace(/\r/g,'\n').split('\n');
      for(let i=0;i<lines.length;i++){
        const rawOriginal=lines[i];
        const raw=strip(rawOriginal).trim();
        if(!raw) continue;

        for(const ch of raw){
          if(ch==='{') braceBalance++;
          else if(ch==='}') braceBalance--;
        }
        if(braceBalance < 0){
          markUnsupported(i+1, rawOriginal, 'Closing brace appears before a matching opening brace');
          braceBalance=0;
        }

        if(/^\}$/.test(raw)) continue;
        if(/^\}\s*else\s*\{$/.test(raw)) continue;
        if(/^else\s*\{$/.test(raw)) continue;
        let m=raw.match(/^\}\s*else\s+if\s*\((.*)\)\s*\{$/);
        if(m){ if(!supportedExpr(m[1])) markUnsupported(i+1, rawOriginal, 'Unsupported else-if expression'); continue; }
        m=raw.match(/^else\s+if\s*\((.*)\)\s*\{$/);
        if(m){ if(!supportedExpr(m[1])) markUnsupported(i+1, rawOriginal, 'Unsupported else-if expression'); continue; }
        m=raw.match(/^if\s*\((.*)\)\s*\{$/);
        if(m){ if(!supportedExpr(m[1])) markUnsupported(i+1, rawOriginal, 'Unsupported if expression'); continue; }

        // Local declarations are scan-local.
        m=raw.match(/^(?:bool|int|uint|float|double|string|auto)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*(.*))?;$/);
        if(m){
          locals.add(this.sanitize ? this.sanitize(m[1]) : m[1]);
          if(m[2] !== undefined && !supportedExpr(m[2])) markUnsupported(i+1, rawOriginal, 'Unsupported local initializer expression');
          continue;
        }

        // ++ / -- on a tag or local.
        if(/^([A-Za-z_][A-Za-z0-9_]*)\s*(\+\+|--);$/.test(raw)) continue;

        // Assignment and compound assignment.
        m=raw.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*(=|\+=|-=|\*=|\/=|%=)\s*(.+);$/);
        if(m){
          if(!supportedExpr(m[3])) markUnsupported(i+1, rawOriginal, 'Unsupported assignment expression');
          continue;
        }

        markUnsupported(i+1, rawOriginal);
      }

      const issues=[];
      if(braceBalance !== 0){
        issues.push({ level:'warning', title:'Unbalanced script braces', message:`Script rung has ${braceBalance > 0 ? braceBalance + ' unmatched opening' : Math.abs(braceBalance) + ' unmatched closing'} brace(s).` });
      }
      if(unsupportedLines.length){
        issues.push({
          level:'warning',
          title:'Script simulator subset warning',
          message:`${unsupportedLines.length} line(s) will be copied to AngelScript but cannot be simulated by the JavaScript preview emitter.`
        });
      }
      return { unsupportedLines, braceBalance, issues };
    },

    jsAnalyzeScriptRungForSimulator(code){
      // Backward-compatible name used by early integrated builds.
      return this.analyzeScriptRungForJsSimulator(code);
    },

    jsEmitScriptRung(code){
      // Richer, deterministic mini-transpiler for AngelScript-style script rungs
      // used by the browser/Node simulator. This is not a full AngelScript parser.
      // It intentionally supports the PLC-oriented subset that is useful in rungs:
      // assignments, local declarations, arithmetic/comparison expressions,
      // if/else blocks, increment/decrement, block Q/ET/CV reads, and Math helpers.
      const out=[];
      const locals=new Set();
      const unsupported=(line)=>out.push('    // Unsupported script simulator line: '+String(line||'').replace(/\*\//g,'* /'));

      for(const raw0 of String(code||'').replace(/\r\n/g,'\n').replace(/\r/g,'\n').split('\n')){
        const raw=this.jsStripLineComment(raw0).trim();
        if(!raw) continue;

        // Allow brace-only and else-only lines.
        if(/^\}$/.test(raw)){ out.push('    }'); continue; }
        if(/^\}\s*else\s*\{$/.test(raw)){ out.push('    } else {'); continue; }
        let m=raw.match(/^\}\s*else\s+if\s*\((.*)\)\s*\{$/);
        if(m){ out.push(`    } else if (${this.jsTransformScriptExpression(m[1], locals)}) {`); continue; }
        if(/^else\s*\{$/.test(raw)){ out.push('    else {'); continue; }
        m=raw.match(/^else\s+if\s*\((.*)\)\s*\{$/);
        if(m){ out.push(`    else if (${this.jsTransformScriptExpression(m[1], locals)}) {`); continue; }
        m=raw.match(/^if\s*\((.*)\)\s*\{$/);
        if(m){ out.push(`    if (${this.jsTransformScriptExpression(m[1], locals)}) {`); continue; }

        // Local declarations are scan-local, matching code emitted inside scan().
        m=raw.match(/^(?:bool|int|uint|float|double|string|auto)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*(.*))?;$/);
        if(m){
          const name=this.sanitize(m[1]);
          locals.add(name);
          const init=m[2] !== undefined ? this.jsTransformScriptExpression(m[2], locals) : 'undefined';
          out.push(`    let ${name} = ${init};`);
          continue;
        }

        // ++ / -- on a tag or local.
        m=raw.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*(\+\+|--);$/);
        if(m){
          const target=this.sanitize(m[1]);
          if(locals.has(target)) out.push(`    ${target}${m[2]};`);
          else {
            const op=m[2]==='++' ? '+ 1' : '- 1';
            out.push(`    ctx.set(${this.jsString(target)}, Number(ctx.get(${this.jsString(target)})) ${op});`);
          }
          continue;
        }

        // Assignment and compound assignment. For tags, writes go back through ctx.set().
        m=raw.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*(=|\+=|-=|\*=|\/=|%=)\s*(.+);$/);
        if(m){
          const target=this.sanitize(m[1]);
          const op=m[2];
          const expr=this.jsTransformScriptExpression(m[3], locals);
          if(locals.has(target)) out.push(`    ${target} ${op} ${expr};`);
          else if(op==='=') out.push(`    ctx.set(${this.jsString(target)}, ${expr});`);
          else {
            const jsop=op[0];
            out.push(`    ctx.set(${this.jsString(target)}, ctx.get(${this.jsString(target)}) ${jsop} (${expr}));`);
          }
          continue;
        }

        unsupported(raw);
      }
      if(!out.length) out.push('    // Empty script rung');
      return out.join('\n')+'\n';
    },
    transpileJavaScript(){
      const timers=this.uniqueTypes(['TON','TOF']);
      const counters=this.uniqueTypes(['CTU','CTD']);
      const oneShots=this.uniqueTypes(['ONS']);
      const scanMs=this.project.scan_ms||5;
      let s='// Generated by PiLab Ladder Logic Editor\n';
      s+='// Complete self-contained JavaScript simulator/runtime target.\n';
      s+='// Node.js usage: save as pilab_ladder_generated.mjs, then import it from another .mjs file.\n';
      s+='//   import { createPiLabLadderProgram, PiLabSimContext } from "./pilab_ladder_generated.mjs";\n';
      s+='//   const program = createPiLabLadderProgram(new PiLabSimContext({ I0_Start: true }));\n';
      s+='//   program.scan();\n';
      s+='//   console.log(program.ctx.tags);\n';
      s+='// Project: '+(this.project.name||'Untitled')+'\n\n';
      s+='// ============================================================\n';
      s+='// Generated Ladder Program\n';
      s+='// ============================================================\n\n';
      s+='export function createPiLabLadderProgram(ctx) {\n';
      s+='  ctx = ctx || new PiLabSimContext();\n';
      for(const t of timers) s+=`  ctx.ensureBlock(${this.jsString(t.type)}, ${this.jsString(this.sanitize(t.tag))}, ${Number(t.preset||1000)});\n`;
      for(const c of counters) s+=`  ctx.ensureBlock(${this.jsString(c.type)}, ${this.jsString(this.sanitize(c.tag))}, ${Number(c.preset||10)});\n`;
      for(const o of oneShots) s+=`  ctx.ensureBlock("ONS", ${this.jsString(this.sanitize(o.tag))}, 0);\n`;
      if(timers.length||counters.length||oneShots.length) s+='\n';
      s+='  function scan() {\n';
      this.project.rungs.forEach((r,i)=>{
        s+='    // Rung '+(i+1)+(r.comment?': '+r.comment:'')+'\n';
        if(r.kind==='script'){
          s+=this.jsEmitScriptRung(r.code||'');
          s+='\n';
          return;
        }
        const updateBlocks=[];
        for(let slot=0; slot<(r.main||[]).length; slot++){
          const e = r.main[slot];
          if(e&&['TON','TOF','CTU','CTD','ONS'].includes(e.type)) updateBlocks.push({e,cond:this.mainInputToSlotExpression(r, slot)});
        }
        for(const b of (r.branches||[])){
          for(let slot=b.start; slot<b.end; slot++){
            const e = (b.cells||[])[slot];
            if(e&&['TON','TOF','CTU','CTD','ONS'].includes(e.type)) updateBlocks.push({e,cond:this.branchInputToSlotExpression(r, b, slot)});
          }
        }
        // ONS and counters must update before the rung output is read so their
        // current-scan Q() value can drive downstream coils. TON/TOF are left
        // after the coil write to avoid changing existing timer timing behavior.
        for(const u of updateBlocks.filter(x=>x.e.type==='ONS')){
          const tag=this.sanitize(u.e.tag);
          const cond=this.jsTransformExpression(u.cond);
          s+=`    ctx.block(${this.jsString(tag)}, "ONS", 0).update(!!(${cond}));\n`;
        }
        for(const u of updateBlocks.filter(x=>x.e.type==='CTU'||x.e.type==='CTD')){
          const tag=this.sanitize(u.e.tag);
          const cond=this.jsTransformExpression(u.cond);
          const controlExpr = this.counterControlExpr ? this.counterControlExpr(u.e) : String(u.e.resetTag || '');
          const resetExpr = controlExpr ? (this.normalizeBoolExpression(controlExpr) || 'false') : 'false';
          s+=`    ctx.block(${this.jsString(tag)}, ${this.jsString(u.e.type)}, ${Number(u.e.preset||10)}).update(!!(${cond}), !!(${this.jsTransformExpression(resetExpr)}));\n`;
        }
        const rungExpr = this.jsTransformExpression(this.rungExpression(r));
        s+=`    const rung_${i+1} = !!(${rungExpr});\n`;
        for(const w of this.latchWrites(r)){
          const cond=this.jsTransformExpression(w.cond);
          s+=`    if (!!(${cond})) ctx.set(${this.jsString(this.sanitize(w.e.tag))}, ${w.e.type==='SET' ? 'true' : 'false'});\n`;
        }
        for(const o of this.outputs(r)) s+=`    ctx.set(${this.jsString(this.sanitize(o.tag))}, rung_${i+1});\n`;
        for(const u of updateBlocks.filter(x=>x.e.type==='TON'||x.e.type==='TOF')){
          const tag=this.sanitize(u.e.tag);
          const cond=this.jsTransformExpression(u.cond);
          s+=`    ctx.block(${this.jsString(tag)}, ${this.jsString(u.e.type)}, ${Number(u.e.preset||1000)}).update(!!(${cond}), ${Number(scanMs)});\n`;
        }
        s+='\n';
      });
      s+='  }\n\n';
      s+='  return { ctx, scan };\n';
      s+='}\n\n';
      s+='// ============================================================\n';
      s+='// PiLab JavaScript Runtime Support\n';
      s+='// Implements TON, TOF, CTU, CTD, and ONS for browser/Node execution.\n';
      s+='// ============================================================\n\n';
      s+='export class PiLabSimContext {\n';
      s+='  constructor(tags = {}, blocks = {}) { this.tags = tags; this.blocks = blocks; }\n';
      s+='  get(name) { const b = this.blocks[name]; return b ? !!b.output : (this.tags[name] ?? false); }\n';
      s+='  tag(name) { return !!this.get(name); }\n';
      s+='  set(name, value) { this.tags[name] = value; }\n';
      s+='  ensureBlock(type, name, preset) { return this.block(name, type, preset); }\n';
      s+='  block(name, type, preset) {\n';
      s+='    let b = this.blocks[name];\n';
      s+='    if (!b || (type && b.type !== type)) {\n';
      s+='      b = makePiLabBlock(type || "TON", preset || 0);\n';
      s+='      this.blocks[name] = b;\n';
      s+='    }\n';
      s+='    if (type && preset !== undefined) b.setPreset(preset);\n';
      s+='    return b;\n';
      s+='  }\n';
      s+='}\n\n';
      s+='function makePiLabBlock(type, preset) {\n';
      s+='  const b = { type, preset_ms:preset, preset, elapsed_ms:0, remaining_ms:0, count:type === "CTD" ? preset : 0, last:false, input:false, output:type === "CTD" ? preset === 0 : false,\n';
      s+='    setPreset(p){ this.preset_ms=p; this.preset=p; if(this.type === "CTD" && this.count === undefined) this.count=p; },\n';
      s+='    Q(){ return !!this.output; }, ET(){ return this.elapsed_ms || 0; }, CV(){ return this.count || 0; },\n';
      s+='    update(input, control=false){\n';
      s+='      input=!!input; control=!!control; this.input=input;\n';
      s+='      if(this.type === "TON"){ if(input){ this.elapsed_ms=Math.min((this.elapsed_ms||0)+arguments[1], this.preset_ms||0); this.output=this.elapsed_ms >= (this.preset_ms||0); } else { this.elapsed_ms=0; this.output=false; } this.remaining_ms=Math.max(0,(this.preset_ms||0)-(this.elapsed_ms||0)); return; }\n';
      s+='      if(this.type === "TOF"){ const scan_ms=arguments[1]; if(input){ this.output=true; this.elapsed_ms=0; this.remaining_ms=0; } else if(this.output){ this.elapsed_ms=Math.min((this.elapsed_ms||0)+scan_ms, this.preset_ms||0); this.remaining_ms=Math.max(0,(this.preset_ms||0)-(this.elapsed_ms||0)); if(this.elapsed_ms >= (this.preset_ms||0)){ this.elapsed_ms=this.preset_ms||0; this.output=false; this.remaining_ms=0; } } else { this.elapsed_ms=this.preset_ms||0; this.remaining_ms=0; } return; }\n';
      s+='      if(this.type === "CTU"){ if(control){ this.count=0; this.output=false; this.last=input; return; } if(input && !this.last && (this.count||0) < (this.preset||0)) this.count=(this.count||0)+1; this.last=input; this.output=(this.count||0) >= (this.preset||0); return; }\n';
      s+='      if(this.type === "CTD"){ if(control){ this.count=this.preset||0; this.output=false; this.last=input; return; } if(input && !this.last && (this.count||0)>0) this.count--; this.last=input; this.output=(this.count||0)===0; return; }\n';
      s+='      if(this.type === "ONS"){ this.output=input && !this.last; this.last=input; return; }\n';
      s+='    }\n';
      s+='  };\n';
      s+='  return b;\n';
      s+='}\n\n';
      s+='// ============================================================\n';
      s+='// Convenience helpers for browser or Node.js tests\n';
      s+='// ============================================================\n\n';
      s+='export function runPiLabLadderScans(program, count = 1) {\n';
      s+='  for (let i = 0; i < count; i++) program.scan();\n';
      s+='  return program.ctx;\n';
      s+='}\n\n';
      s+='export function createAndRunPiLabLadder(tags = {}, scanCount = 1) {\n';
      s+='  const program = createPiLabLadderProgram(new PiLabSimContext(tags));\n';
      s+='  runPiLabLadderScans(program, scanCount);\n';
      s+='  return program;\n';
      s+='}\n';
      return s;
    },


    angelScriptReservedNames(){
      return new Set([
        'true','false','null','void','bool','int','uint','float','double','string','auto','class','const',
        'if','else','for','while','do','switch','case','break','continue','return','scan'
      ]);
    },
    angelScriptBlockInstanceNames(){
      return new Set(this.uniqueTypes(['TON','TOF','CTU','CTD','ONS']).map(e => this.sanitize(e.tag)));
    },
    angelScriptPhysicalType(name){
      if(/^AI\d+$/i.test(name) || /^AO\d+$/i.test(name)) return 'float';
      if(/^I\d+$/i.test(name) || /^Q\d+$/i.test(name)) return 'bool';
      return null;
    },
    angelScriptDefaultForType(type){
      if(type === 'int' || type === 'uint') return '0';
      if(type === 'float' || type === 'double') return '0.0f';
      if(type === 'string') return '""';
      return 'false';
    },
    angelScriptNormalizeType(type){
      type = String(type || '').toLowerCase();
      if(type === 'integer') return 'int';
      if(type === 'number') return 'float';
      if(['bool','int','uint','float','double','string'].includes(type)) return type;
      return 'bool';
    },
    angelScriptCoerceInitial(type, value){
      type = this.angelScriptNormalizeType(type);
      if(type === 'bool'){
        if(typeof value === 'string'){
          const v=value.trim().toLowerCase();
          return (v === 'true' || v === '1' || v === 'on') ? 'true' : 'false';
        }
        return value ? 'true' : 'false';
      }
      if(type === 'string') return JSON.stringify(String(value ?? ''));
      const n=Number(value);
      if(!Number.isFinite(n)) return this.angelScriptDefaultForType(type);
      if(type === 'float' || type === 'double') return String(n) + (String(n).includes('.') ? 'f' : '.0f');
      return String(Math.trunc(n));
    },
    angelScriptAddGlobalUsage(map, rawName, usage={}){
      const name=this.sanitize(rawName);
      if(!name || !/^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) return;
      if(this.angelScriptReservedNames().has(name)) return;
      if(this.angelScriptBlockInstanceNames().has(name)) return;
      if(!map.has(name)) map.set(name, { name, read:false, write:false, numeric:false });
      const item=map.get(name);
      if(usage.read) item.read=true;
      if(usage.write) item.write=true;
      if(usage.numeric) item.numeric=true;
    },
    angelScriptCollectGlobals(){
      const map=new Map();
      const add=(name, usage)=>this.angelScriptAddGlobalUsage(map, name, usage);
      const markExpr=(expr, usage={})=>{
        const scrubbed=String(expr || '').replace(/("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')/g, ' ');
        const re=/\b[A-Za-z_][A-Za-z0-9_]*\b/g;
        let m;
        while((m=re.exec(scrubbed))){
          const prev=scrubbed[m.index-1];
          if(prev === '.') continue;
          add(m[0], usage);
        }
      };
      const strip=(line)=> this.jsStripLineComment ? this.jsStripLineComment(line) : String(line || '').replace(/\/\/.*$/,'');
      const localTypes='bool|int|uint|float|double|string|auto';

      for(const r of (this.project && this.project.rungs) || []){
        if(!r) continue;
        if(r.kind === 'script'){
          const lines=String(r.code || '').replace(/\r\n/g,'\n').replace(/\r/g,'\n').split('\n');
          const locals=new Set();
          for(const raw of lines){
            const line=strip(raw).trim();
            const m=line.match(new RegExp('^(?:'+localTypes+')\\s+([A-Za-z_][A-Za-z0-9_]*)\\b'));
            if(m) locals.add(this.sanitize(m[1]));
          }
          const addIfNotLocal=(name, usage)=>{ const clean=this.sanitize(name); if(!locals.has(clean)) add(clean, usage); };
          const markExprScript=(expr, usage={})=>{
            const scrubbed=String(expr || '').replace(/("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')/g, ' ');
            const re=/\b[A-Za-z_][A-Za-z0-9_]*\b/g;
            let m;
            while((m=re.exec(scrubbed))){
              const name=this.sanitize(m[0]);
              const prev=scrubbed[m.index-1];
              if(prev === '.' || locals.has(name)) continue;
              add(name, usage);
            }
          };
          for(const raw of lines){
            const line=strip(raw).trim();
            if(!line) continue;
            let m=line.match(new RegExp('^(?:'+localTypes+')\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*(?:=\\s*(.*))?;$'));
            if(m){ if(m[2]) markExprScript(m[2], {read:true, numeric:/^(?:int|uint|float|double)\b/.test(line)}); continue; }
            m=line.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*(\+\+|--);$/);
            if(m){ addIfNotLocal(m[1], {read:true, write:true, numeric:true}); continue; }
            m=line.match(/^([A-Za-z_][A-Za-z0-9_]*)\s*(=|\+=|-=|\*=|\/=|%=)\s*(.+);$/);
            if(m){
              const numeric=/(?:^|[^=!<>])[-+]?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?[fF]?\b/.test(m[3]) || ['+=','-=','*=','/=','%='].includes(m[2]);
              addIfNotLocal(m[1], {write:true, read:m[2] !== '=', numeric});
              markExprScript(m[3], {read:true, numeric});
              continue;
            }
            const cond=line.match(/\b(?:if|else\s+if|while)\s*\((.*)\)/);
            if(cond) markExprScript(cond[1], {read:true});
            else markExprScript(line, {read:true});
          }
          continue;
        }

        const visit=(e)=>{
          if(!e) return;
          if(['TON','TOF','CTU','CTD','ONS'].includes(e.type)){
            if((e.type === 'CTU' || e.type === 'CTD') && (e.resetTag || e.resetExpr || e.loadTag || e.loadExpr)) markExpr(e.resetTag || e.resetExpr || e.loadTag || e.loadExpr, {read:true});
            return;
          }
          if(['OUT','SET','RST'].includes(e.type)) add(e.tag, {write:true});
          else if(['NO','NC'].includes(e.type)) add(e.tag, {read:true});
        };
        (r.main || []).forEach(visit);
        for(const br of (r.branches || [])) (br.cells || []).forEach(visit);
      }
      return [...map.values()].sort((a,b)=>a.name.localeCompare(b.name));
    },
    angelScriptGlobalRows(){
      const globals=this.angelScriptCollectGlobals();
      let registryRows=[];
      try { registryRows = this.buildTagRegistryRows ? this.buildTagRegistryRows() : []; } catch(e) { registryRows = []; }
      const registryByName=new Map(registryRows.map(row => [row.name, row]));
      return globals.map(g => {
        const registry=registryByName.get(g.name);
        let type=this.angelScriptPhysicalType(g.name) || (registry ? registry.type : null);
        if(!type) type = (g.numeric || /counter|count|timer|delay|pulse|state|step|index|total/i.test(g.name)) ? 'int' : 'bool';
        type=this.angelScriptNormalizeType(type);
        const value = registry ? registry.value : undefined;
        return { name:g.name, type, value, initial:this.angelScriptCoerceInitial(type, value) };
      });
    },
    angelScriptGlobalDeclarations(){
      const rows=this.angelScriptGlobalRows();
      if(!rows.length) return '';
      let s='// ------------------------------------------------------------\n';
      s+='// Optional tag globals for standalone AngelScript testing.\n';
      s+='// Disable Add Tags when exporting for a runtime that already\n';
      s+='// provides PLC/HMI/script tags as built-in globals.\n';
      s+='// ------------------------------------------------------------\n';
      for(const row of rows) s+=`${row.type} ${row.name} = ${row.initial};\n`;
      return s+'\n';
    },
angelScriptIndentBlock(code, indent='    '){
      return String(code || '')
        .replace(/\r\n/g, '\n')
        .replace(/\r/g, '\n')
        .split('\n')
        .map(line => line.length ? indent + line : '')
        .join('\n')
        .replace(/\n?$/, '\n');
    },
    angelScriptHeader(metadata={}){
      const commentLines = [];
      const addLine = (label, value) => {
        const text = String(value == null ? '' : value).trim();
        if(!text) return;
        const lines = text.split(/\r?\n/);
        commentLines.push(`// ${label}: ${lines[0]}`);
        for(const line of lines.slice(1)) commentLines.push(`// ${' '.repeat(label.length)}  ${line}`);
      };

      commentLines.push('// Generated by PiLab Ladder Logic Editor');
      addLine('Project', this.project.name || 'Untitled');
      addLine('Description', metadata.description || this.project.description || '');
      addLine('Source Ladder File', metadata.sourceFileName || '');
      addLine('Generated At', metadata.generatedAt || '');
      addLine('Scan Time', `${this.project.scan_ms || 5} ms`);
      if(metadata.rungSummary) addLine('Rungs', metadata.rungSummary);
      else addLine('Rungs', `${(this.project.rungs || []).length}`);
      if(metadata.tagSummary) addLine('Discovered Tags', metadata.tagSummary);
      if(metadata.notes) addLine('Notes', metadata.notes);
      return commentLines.join('\n') + '\n\n';
    },
    transpile(includeTagGlobals=false, metadata={}){
      const timers=this.uniqueTypes(['TON','TOF']);
      const counters=this.uniqueTypes(['CTU','CTD']);
      const oneShots=this.uniqueTypes(['ONS']);
      const scanMs=this.project.scan_ms||5;
      let s=this.angelScriptHeader(metadata);

      if(timers.length){
        s+='class TON\n{\n';
        s+='    uint preset_ms;\n    uint elapsed_ms = 0;\n    bool output = false;\n\n';
        s+='    TON(uint preset) { preset_ms = preset; }\n';
        s+='    void SetPreset(uint preset) { preset_ms = preset; if (elapsed_ms > preset_ms) elapsed_ms = preset_ms; }\n\n';
        s+='    void update(bool input, uint scan_ms)\n    {\n';
        s+='        if (input)\n        {\n';
        s+='            if (elapsed_ms < preset_ms) elapsed_ms += scan_ms;\n';
        s+='            if (elapsed_ms >= preset_ms) output = true;\n';
        s+='        }\n        else\n        {\n';
        s+='            elapsed_ms = 0;\n            output = false;\n';
        s+='        }\n    }\n\n';
        s+='    bool Q() const { return output; }\n    uint ET() const { return elapsed_ms; }\n    uint PT() const { return preset_ms; }\n};\n\n';

        s+='class TOF\n{\n';
        s+='    uint preset_ms;\n    uint elapsed_ms = 0;\n    bool output = false;\n\n';
        s+='    TOF(uint preset) { preset_ms = preset; }\n';
        s+='    void SetPreset(uint preset) { preset_ms = preset; if (elapsed_ms > preset_ms) elapsed_ms = preset_ms; }\n\n';
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
        s+='    bool Q() const { return output; }\n    uint ET() const { return elapsed_ms; }\n    uint PT() const { return preset_ms; }\n};\n\n';
      }

      if(counters.length){
        if(counters.some(c=>c.type==='CTU')){
          s+='class CTU\n{\n';
          s+='    uint preset;\n    uint count = 0;\n    bool last = false;\n    bool output = false;\n\n';
          s+='    CTU(uint pv) { preset = pv; }\n';
          s+='    void SetPreset(uint pv) { preset = pv; }\n\n';
          s+='    void update(bool input, bool reset=false)\n    {\n';
          s+='        if (reset) { count = 0; output = false; last = input; return; }\n';
          s+='        if (input && !last && count < preset) count++;\n        last = input;\n        output = count >= preset;\n';
          s+='    }\n\n    bool Q() const { return output; }\n    uint CV() const { return count; }\n    uint PV() const { return preset; }\n};\n\n';
        }
        if(counters.some(c=>c.type==='CTD')){
          s+='class CTD\n{\n';
          s+='    uint preset;\n    uint count;\n    bool last = false;\n    bool output = false;\n\n';
          s+='    CTD(uint pv) { preset = pv; count = pv; output = count == 0; }\n';
          s+='    void SetPreset(uint pv) { preset = pv; if (count > preset) count = preset; output = count == 0; }\n\n';
          s+='    void update(bool input, bool load=false)\n    {\n';
          s+='        if (load) { count = preset; output = false; last = input; return; }\n';
          s+='        if (input && !last && count > 0) count--;\n        last = input;\n        output = count == 0;\n';
          s+='    }\n\n    bool Q() const { return output; }\n    uint CV() const { return count; }\n    uint PV() const { return preset; }\n};\n\n';
        }
      }

      if(oneShots.length){
        s+='class ONS\n{\n';
        s+='    bool last = false;\n    bool output = false;\n\n';
        s+='    void update(bool input)\n    {\n';
        s+='        output = input && !last;\n        last = input;\n';
        s+='    }\n\n    bool Q() const { return output; }\n};\n\n';
      }

      for(const t of timers){
        const param = this.blockParamAnnotation(t);
        const monitor = this.blockMonitorAnnotation(t);
        if(param) s+=param+'\n';
        if(monitor) s+=monitor+'\n';
        s+=t.type+' '+this.sanitize(t.tag)+'('+(t.preset||1000)+');\n';
      }
      for(const c of counters){
        const param = this.blockParamAnnotation(c);
        const monitor = this.blockMonitorAnnotation(c);
        if(param) s+=param+'\n';
        if(monitor) s+=monitor+'\n';
        s+=c.type+' '+this.sanitize(c.tag)+'('+(c.preset||10)+');\n';
      }
      for(const o of oneShots) s+='ONS '+this.sanitize(o.tag)+';\n';
      if(timers.length||counters.length||oneShots.length) s+='\n';

      if(includeTagGlobals) s += this.angelScriptGlobalDeclarations();

      s+='void scan()\n{\n';
      this.project.rungs.forEach((r,i)=>{
        s+='    // Rung '+(i+1)+(r.comment?': '+r.comment:'')+'\n';
        if(r.kind==='script'){
          const code=String(r.code||'').replace(/\r\n/g,'\n').replace(/\r/g,'\n');
          if(code.trim()) s+=this.angelScriptIndentBlock(code);
          else s+='    // Empty AngelScript rung\n';
          s+='\n';
          return;
        }
        const updateBlocks=[];
        for(let slot=0; slot<r.main.length; slot++){
          const e = r.main[slot];
          if(e&&['TON','TOF','CTU','CTD','ONS'].includes(e.type)) updateBlocks.push({e,cond:this.mainInputToSlotExpression(r, slot)});
        }
        for(const b of r.branches){
          for(let slot=b.start; slot<b.end; slot++){
            const e = b.cells[slot];
            if(e&&['TON','TOF','CTU','CTD','ONS'].includes(e.type)) updateBlocks.push({e,cond:this.branchInputToSlotExpression(r, b, slot)});
          }
        }

        // ONS and counters update before the rung output is read so their
        // current-scan Q() value can drive downstream coils. TON/TOF are left
        // after the coil write to preserve existing timer timing behavior.
        for(const u of updateBlocks.filter(x=>x.e.type==='ONS')) s+='    '+this.sanitize(u.e.tag)+'.update(('+u.cond+'));\n';
        for(const u of updateBlocks.filter(x=>x.e.type==='CTU'||x.e.type==='CTD')){
          const controlExpr = this.counterControlExpr ? this.counterControlExpr(u.e) : String(u.e.resetTag || '');
          const resetExpr = controlExpr ? (this.normalizeBoolExpression(controlExpr) || 'false') : 'false';
          if(this.blockParamIsEnabled(u.e)) s+='    '+this.sanitize(u.e.tag)+'.SetPreset(uint('+this.blockParamTag(u.e)+'));\n';
          s+='    '+this.sanitize(u.e.tag)+'.update(('+u.cond+'), ('+resetExpr+'));\n';
        }

        const rungExpr = this.rungExpression(r);
        s+='    bool rung_'+(i+1)+' = '+rungExpr+';\n';
        for(const w of this.latchWrites(r)) s+='    if ('+w.cond+') '+this.sanitize(w.e.tag)+' = '+(w.e.type==='SET'?'true':'false')+';\n';
        for(const o of this.outputs(r)) s+='    '+this.sanitize(o.tag)+' = rung_'+(i+1)+';\n';
        for(const u of updateBlocks.filter(x=>x.e.type==='TON'||x.e.type==='TOF')){
          if(this.blockParamIsEnabled(u.e)) s+='    '+this.sanitize(u.e.tag)+'.SetPreset(uint('+this.blockParamTag(u.e)+'));\n';
          s+='    '+this.sanitize(u.e.tag)+'.update(('+u.cond+'), '+scanMs+');\n';
        }
        s+='\n';
      });
      s+='}\n';
      return s;
    }
};
