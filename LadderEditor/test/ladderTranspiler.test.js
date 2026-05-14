import { describe, it, expect } from 'vitest';
import { ladderModelMethods } from '../src/ladder/ladderModel.js';
import { ladderTranspilerMethods } from '../src/ladder/ladderTranspiler.js';

function makeHarness(project) {
  return {
    project,
    ...ladderModelMethods,
    ...ladderTranspilerMethods,
  };
}

function emptyRung(comment = '') {
  return {
    id: 'r1',
    kind: 'ladder',
    comment,
    main: Array(8).fill(null),
    branches: [],
  };
}

function branch(id, start, end, cells = {}) {
  const br = { id, start, end, cells: Array(8).fill(null) };
  for (const [slot, el] of Object.entries(cells)) br.cells[Number(slot)] = el;
  return br;
}

function projectWith(rungs, scan_ms = 5) {
  return { name: 'Unit Test Project', scan_ms, rungs };
}

const NO = (tag) => ({ id: `no_${tag}`, type: 'NO', tag });
const NC = (tag) => ({ id: `nc_${tag}`, type: 'NC', tag });
const OUT = (tag) => ({ id: `out_${tag}`, type: 'OUT', tag });
const TON = (tag, preset = 1000) => ({ id: `ton_${tag}`, type: 'TON', tag, preset });
const TOF = (tag, preset = 1000) => ({ id: `tof_${tag}`, type: 'TOF', tag, preset });
const CTU = (tag, preset = 10, resetTag = '') => ({ id: `ctu_${tag}`, type: 'CTU', tag, preset, resetTag });
const CTD = (tag, preset = 10, resetTag = '') => ({ id: `ctd_${tag}`, type: 'CTD', tag, preset, resetTag });

describe('ladder transpiler expressions', () => {
  it('transpiles simple series contacts into one rung expression', () => {
    const r = emptyRung('series');
    r.main[0] = NO('I0');
    r.main[1] = NC('I1');
    r.main[7] = OUT('Q0');
    const h = makeHarness(projectWith([r]));

    expect(h.rungExpression(r)).toBe('I0 && !I1');
    expect(h.transpile()).toContain('bool rung_1 = I0 && !I1;');
    expect(h.transpile()).toContain('Q0 = rung_1;');
  });

  it('keeps a classic seal-in branch readable', () => {
    const r = emptyRung('seal in');
    r.main[0] = NO('I0_Start');
    r.main[2] = NC('I1_Stop');
    r.main[7] = OUT('Q0_Motor');
    r.branches.push(branch('b1', 0, 1, { 0: NO('Q0_Motor') }));
    const h = makeHarness(projectWith([r]));

    expect(h.rungExpression(r)).toBe('(I0_Start || Q0_Motor) && !I1_Stop');
    expect(h.transpile()).toContain('bool rung_1 = (I0_Start || Q0_Motor) && !I1_Stop;');
  });

  it('combines same-span parallel branches as an OR group', () => {
    const r = emptyRung('parallel modes');
    r.main[0] = NO('I0_Auto');
    r.main[7] = OUT('Q_Run');
    r.branches.push(branch('b1', 0, 5, { 0: NO('I1_Manual') }));
    r.branches.push(branch('b2', 0, 5, { 0: NO('I2_Remote') }));
    const h = makeHarness(projectWith([r]));

    expect(h.rungExpression(r)).toBe('(I0_Auto || I1_Manual || I2_Remote)');
  });

  it('falls back to graph solving for crossing branches without dropping paths', () => {
    const r = emptyRung('crossing branch graph');
    r.main[0] = NO('A');
    r.main[2] = NO('B');
    r.main[4] = NO('C');
    r.main[7] = OUT('Q');
    r.branches.push(branch('x', 0, 3, { 0: NO('X') }));
    r.branches.push(branch('y', 2, 5, { 2: NO('Y') }));
    const h = makeHarness(projectWith([r]));
    const expr = h.rungExpression(r);

    expect(expr).toContain('X');
    expect(expr).toContain('Y');
    expect(expr).toContain('A');
    expect(expr).toContain('B');
    expect(expr).toContain('C');
  });
});

describe('function block transpilation', () => {
  it('updates TON from the power reaching the timer input, not the final rung output', () => {
    const r = emptyRung('timer');
    r.main[0] = NO('TempHigh');
    r.main[3] = TON('T_OverTemp', 2000);
    r.main[7] = OUT('Q_Alarm');
    const h = makeHarness(projectWith([r], 5));
    const as = h.transpile();

    expect(as).toContain('TON T_OverTemp(2000);');
    expect(as).toContain('T_OverTemp.update((TempHigh), 5);');
    expect(as).toContain('bool rung_1 = TempHigh && T_OverTemp.Q();');
  });

  it('emits TOF class and update call', () => {
    const r = emptyRung('off delay');
    r.main[0] = NO('RunCmd');
    r.main[2] = TOF('T_RunHold', 1500);
    r.main[7] = OUT('Q_Run');
    const h = makeHarness(projectWith([r], 10));
    const as = h.transpile();

    expect(as).toContain('class TOF');
    expect(as).toContain('TOF T_RunHold(1500);');
    expect(as).toContain('T_RunHold.update((RunCmd), 10);');
  });

  it('emits CTU reset expressions using the safe Boolean subset', () => {
    const r = emptyRung('counter');
    r.main[0] = NO('PartSeen');
    r.main[1] = CTU('C_Parts', 3, 'ResetPB || !AutoMode');
    r.main[7] = OUT('BatchDone');
    const h = makeHarness(projectWith([r]));
    const as = h.transpile();

    expect(as).toContain('class CTU');
    expect(as).toContain('if (input && !last && count < preset) count++;');
    expect(as).toContain('CTU C_Parts(3);');
    expect(as).toContain('C_Parts.update((PartSeen), (ResetPB || !AutoMode));');
  });

  it('emits CTD declarations and reload/load update calls', () => {
    const r = emptyRung('down counter');
    r.main[0] = NO('StepDone');
    r.main[1] = CTD('C_Remaining', 5, 'Reload');
    r.main[7] = OUT('AllDone');
    const h = makeHarness(projectWith([r]));
    const as = h.transpile();

    expect(as).toContain('class CTD');
    expect(as).toContain('void update(bool input, bool load=false)');
    expect(as).toContain('if (load) { count = preset; output = false; last = input; return; }');
    expect(as).toContain('CTD C_Remaining(5);');
    expect(as).toContain('C_Remaining.update((StepDone), (Reload));');
  });
});

describe('script rungs', () => {
  it('passes AngelScript rung bodies through inside scan()', () => {
    const script = {
      id: 's1',
      kind: 'script',
      comment: 'custom logic',
      code: 'Q_Debug = I0_Auto && !I1_Stop;',
    };
    const h = makeHarness(projectWith([script]));
    const as = h.transpile();

    expect(as).toContain('// Rung 1: custom logic');
    expect(as).toContain('Q_Debug = I0_Auto && !I1_Stop;');
  });
});
