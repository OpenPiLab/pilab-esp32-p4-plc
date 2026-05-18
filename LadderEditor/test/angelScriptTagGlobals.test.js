import { describe, expect, test } from 'vitest';
import { ladderModelMethods } from '../src/ladder/ladderModel.js';
import { ladderTranspilerMethods } from '../src/ladder/ladderTranspiler.js';
import { ladderTagRegistryMethods } from '../src/ladder/ladderTagRegistry.js';

function makeHarness(project){
  return Object.assign({ project, tagRegistryImported:{}, tagRegistryEdits:{} }, ladderModelMethods, ladderTranspilerMethods, ladderTagRegistryMethods);
}

describe('AngelScript optional tag globals', () => {
  test('does not emit tag globals unless requested', () => {
    const h = makeHarness({ name:'x', scan_ms:5, rungs:[{ id:'s', kind:'script', comment:'', code:'M_Count += 1;\nQ0 = HMI_StartPB;' }] });
    const out = h.transpile(false);
    expect(out).not.toContain('Optional tag globals');
    expect(out).not.toContain('int M_Count = 0;');
  });

  test('emits discovered HMI, memory, and physical I/O globals when requested', () => {
    const h = makeHarness({
      name:'x', scan_ms:5,
      rungs:[
        { id:'l', kind:'ladder', comment:'', branches:[], main:[{id:'a', type:'NO', tag:'I0'}, null,null,null,null,null,null,{id:'b', type:'OUT', tag:'Q0'}] },
        { id:'s', kind:'script', comment:'', code:'M_Count += 1;\nQ1 = HMI_StartPB && M_Count > 3;' }
      ]
    });
    h.importTagRegistryPayload({ tags:[{ name:'HMI_StartPB', type:'bool', value:true, description:'Start' }] });
    const out = h.transpile(true);
    expect(out).toContain('Optional tag globals for standalone AngelScript testing');
    expect(out).toContain('bool HMI_StartPB = true;');
    expect(out).toContain('int M_Count = 0;');
    expect(out).toContain('bool I0 = false;');
    expect(out).toContain('bool Q0 = false;');
    expect(out).toContain('bool Q1 = false;');
  });

  test('does not declare timer/counter block instance names as tag globals', () => {
    const h = makeHarness({ name:'x', scan_ms:5, rungs:[{ id:'l', kind:'ladder', comment:'', branches:[], main:[{id:'a', type:'NO', tag:'HMI_StartPB'}, null,null,{id:'t', type:'TON', tag:'T_Delay', preset:100}, null,null,null,{id:'b', type:'OUT', tag:'Q0'}] }] });
    const out = h.transpile(true);
    expect(out).toContain('TON T_Delay(100);');
    expect(out).not.toContain('bool T_Delay =');
  });
});
