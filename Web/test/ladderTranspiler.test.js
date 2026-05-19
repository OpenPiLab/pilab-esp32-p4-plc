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
const SET = (tag) => ({ id: `set_${tag}`, type: 'SET', tag });
const RST = (tag) => ({ id: `rst_${tag}`, type: 'RST', tag });
const ONS = (tag) => ({ id: `ons_${tag}`, type: 'ONS', tag });

describe('ladder transpiler expressions', () => {

  it('discovers script rung source and destination tags while excluding locals and math helpers', () => {
    const script = {
      id: 's_tags',
      kind: 'script',
      comment: 'tags',
      code: [
        'ScaledSpeed = HmiSpeedSetpoint * SpeedTrim;',
        'float span = RawMax - RawMin;',
        'TankLevelPct = max(0.0f, min(100.0f, RawAI0 / span));',
        'if (SpeedCmd > MaxSpeed) {',
        '    Q_SpeedHigh = true;',
        '}',
      ].join('\n'),
    };
    const h = makeHarness(projectWith([script]));
    expect(h.jsScriptRungTags(script.code).sort()).toEqual([
      'HmiSpeedSetpoint',
      'MaxSpeed',
      'Q_SpeedHigh',
      'RawAI0',
      'RawMax',
      'RawMin',
      'ScaledSpeed',
      'SpeedCmd',
      'SpeedTrim',
      'TankLevelPct',
    ]);
  });
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
    expect(as).toContain('bool rung_1 = T_RunHold.Q();');
  });

  it('emits TOF rung output from block Q instead of current input power', () => {
    const r = emptyRung('off delay output hold');
    r.main[0] = NO('RunCmd');
    r.main[2] = TOF('T_RunHold', 1500);
    r.main[7] = OUT('Q_Run');
    const h = makeHarness(projectWith([r], 10));
    const js = h.transpileJavaScript();

    expect(h.rungExpression(r)).toBe('T_RunHold.Q()');
    expect(js).toContain('const rung_1 = !!(ctx.block("T_RunHold").Q());');
    expect(js).toContain('ctx.block("T_RunHold", "TOF", 1500).update(!!(ctx.tag("RunCmd")), 10);');
  });



  it('emits CTU rung output from block Q instead of current input power', () => {
    const r = emptyRung('counter done output hold');
    r.main[0] = NO('PartSeen');
    r.main[1] = CTU('C_Parts', 3, 'ResetPB');
    r.main[7] = OUT('BatchDone');
    const h = makeHarness(projectWith([r], 10));
    const js = h.transpileJavaScript();

    expect(h.rungExpression(r)).toBe('C_Parts.Q()');
    expect(js).toContain('const rung_1 = !!(ctx.block("C_Parts").Q());');
    expect(js).toContain('ctx.block("C_Parts", "CTU", 3).update(!!(ctx.tag("PartSeen")), !!(ctx.tag("ResetPB")));');
  });

  it('emits CTD rung output from block Q instead of current input power', () => {
    const r = emptyRung('down counter done output hold');
    r.main[0] = NO('StepDone');
    r.main[1] = CTD('C_Remaining', 2, 'Reload');
    r.main[7] = OUT('AllDone');
    const h = makeHarness(projectWith([r], 10));
    const js = h.transpileJavaScript();

    expect(h.rungExpression(r)).toBe('C_Remaining.Q()');
    expect(js).toContain('const rung_1 = !!(ctx.block("C_Remaining").Q());');
    expect(js).toContain('ctx.block("C_Remaining", "CTD", 2).update(!!(ctx.tag("StepDone")), !!(ctx.tag("Reload")));');
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


  it('emits SET and RST latch coils using the power reaching each coil', () => {
    const setRung = emptyRung('set latch');
    setRung.main[0] = NO('StartPB');
    setRung.main[7] = SET('RunLatch');

    const rstRung = emptyRung('reset latch');
    rstRung.main[0] = NO('StopPB');
    rstRung.main[7] = RST('RunLatch');

    const h = makeHarness(projectWith([setRung, rstRung]));
    const as = h.transpile();
    const js = h.transpileJavaScript();

    expect(as).toContain('if (StartPB) RunLatch = true;');
    expect(as).toContain('if (StopPB) RunLatch = false;');
    expect(js).toContain('if (!!(ctx.tag("StartPB"))) ctx.set("RunLatch", true);');
    expect(js).toContain('if (!!(ctx.tag("StopPB"))) ctx.set("RunLatch", false);');
  });

  it('emits ONS as a stateful one-shot block before the rung output is evaluated', () => {
    const r = emptyRung('one shot');
    r.main[0] = NO('StartPB');
    r.main[1] = ONS('ONS_Start');
    r.main[7] = OUT('StartPulse');
    const h = makeHarness(projectWith([r]));
    const as = h.transpile();
    const js = h.transpileJavaScript();

    expect(h.rungExpression(r)).toBe('StartPB && ONS_Start.Q()');
    expect(as).toContain('class ONS');
    expect(as).toContain('ONS ONS_Start;');
    expect(as).toContain('ONS_Start.update((StartPB));');
    expect(as.indexOf('ONS_Start.update((StartPB));')).toBeLessThan(as.indexOf('bool rung_1 = StartPB && ONS_Start.Q();'));
    expect(js).toContain('ctx.ensureBlock("ONS", "ONS_Start", 0);');
    expect(js).toContain('ctx.block("ONS_Start", "ONS", 0).update(!!(ctx.tag("StartPB")));');
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

describe('JavaScript simulator transpiler', () => {
  it('emits a browser/Node-runnable JavaScript scan program', () => {
    const r = emptyRung('js backend');
    r.main[0] = NO('I0');
    r.main[1] = TON('T_Delay', 10);
    r.main[7] = OUT('Q0');
    const h = makeHarness(projectWith([r], 5));
    const js = h.transpileJavaScript();

    expect(js).toContain('export function createPiLabLadderProgram(ctx)');
    expect(js).toContain('ctx.ensureBlock("TON", "T_Delay", 10);');
    expect(js).toContain('const rung_1 = !!(ctx.tag("I0") && ctx.block("T_Delay").Q());');
    expect(js).toContain('ctx.block("T_Delay", "TON", 10).update(!!(ctx.tag("I0")), 5);');
    expect(js).toContain('ctx.set("Q0", rung_1);');
  });
});

describe('rich JavaScript script-rung simulator subset', () => {
  function compileJsProgram(h, tags = {}) {
    const code = h.transpileJavaScript().replace(/export\s+/g, '');
    const api = Function(code + '\nreturn { createPiLabLadderProgram, PiLabSimContext, runPiLabLadderScans };')();
    const program = api.createPiLabLadderProgram(new api.PiLabSimContext({ ...tags }));
    return { ...api, program };
  }

  it('simulates compare, move, math, and clamp-style script rungs', () => {
    const script = {
      id: 's_math',
      kind: 'script',
      comment: 'math compare clamp',
      code: [
        'ScaledSpeed = HmiSpeedSetpoint * SpeedTrim;',
        'Q_SpeedHigh = ScaledSpeed >= MaxSpeed;',
        'SpeedCmd = ScaledSpeed;',
        'if (SpeedCmd < MinSpeed) {',
        '    SpeedCmd = MinSpeed;',
        '}',
        'if (SpeedCmd > MaxSpeed) {',
        '    SpeedCmd = MaxSpeed;',
        '}',
      ].join('\n'),
    };
    const h = makeHarness(projectWith([script]));
    const { program } = compileJsProgram(h, {
      HmiSpeedSetpoint: 250,
      SpeedTrim: 0.5,
      MinSpeed: 20,
      MaxSpeed: 100,
    });

    program.scan();
    expect(program.ctx.tags.ScaledSpeed).toBe(125);
    expect(program.ctx.tags.Q_SpeedHigh).toBe(true);
    expect(program.ctx.tags.SpeedCmd).toBe(100);
  });

  it('simulates scaling, divide protection, locals, and Math helper functions', () => {
    const script = {
      id: 's_scale',
      kind: 'script',
      comment: 'analog scale',
      code: [
        'float span = RawMax - RawMin;',
        'if (span > 0.0f) {',
        '    TankLevelPct = ((RawAI0 - RawMin) * 100.0f) / span;',
        '} else {',
        '    TankLevelPct = 0.0f;',
        '}',
        'TankLevelPct = max(0.0f, min(100.0f, TankLevelPct));',
      ].join('\n'),
    };
    const h = makeHarness(projectWith([script]));
    const { program } = compileJsProgram(h, { RawAI0: 2048, RawMin: 0, RawMax: 4096 });

    program.scan();
    expect(program.ctx.tags.TankLevelPct).toBe(50);
  });

  it('simulates rising and falling edge snippets with persistent tag state', () => {
    const script = {
      id: 's_edges',
      kind: 'script',
      comment: 'manual edges',
      code: [
        'Q_RisingEdge = I0_Input && !I0_Last;',
        'Q_FallingEdge = !I0_Input && I0_Last;',
        'I0_Last = I0_Input;',
      ].join('\n'),
    };
    const h = makeHarness(projectWith([script]));
    const { program } = compileJsProgram(h, { I0_Input: false });

    program.scan();
    expect(program.ctx.tags.Q_RisingEdge).toBe(false);
    expect(program.ctx.tags.Q_FallingEdge).toBe(false);

    program.ctx.tags.I0_Input = true;
    program.scan();
    expect(program.ctx.tags.Q_RisingEdge).toBe(true);
    expect(program.ctx.tags.Q_FallingEdge).toBe(false);

    program.scan();
    expect(program.ctx.tags.Q_RisingEdge).toBe(false);

    program.ctx.tags.I0_Input = false;
    program.scan();
    expect(program.ctx.tags.Q_FallingEdge).toBe(true);
  });

  it('simulates a CTUD-style up/down counter snippet', () => {
    const script = {
      id: 's_ctud',
      kind: 'script',
      comment: 'ctud',
      code: [
        'if (CUD_Reset) {',
        '    CUD_Count = 0;',
        '} else {',
        '    if (CountUp && !CUD_UpLast) {',
        '        CUD_Count++;',
        '    }',
        '    if (CountDown && !CUD_DownLast && CUD_Count > 0) {',
        '        CUD_Count--;',
        '    }',
        '}',
        'CUD_UpLast = CountUp;',
        'CUD_DownLast = CountDown;',
        'CUD_Done = CUD_Count >= CUD_Preset;',
        'CUD_Empty = CUD_Count == 0;',
      ].join('\n'),
    };
    const h = makeHarness(projectWith([script]));
    const { program } = compileJsProgram(h, { CUD_Preset: 2 });

    program.ctx.tags.CountUp = true;
    program.scan();
    program.ctx.tags.CountUp = false;
    program.scan();
    program.ctx.tags.CountUp = true;
    program.scan();
    expect(program.ctx.tags.CUD_Count).toBe(2);
    expect(program.ctx.tags.CUD_Done).toBe(true);

    program.ctx.tags.CountUp = false;
    program.ctx.tags.CountDown = true;
    program.scan();
    expect(program.ctx.tags.CUD_Count).toBe(1);
    expect(program.ctx.tags.CUD_Done).toBe(false);
  });

  it('simulates a TP pulse timer snippet using compound assignment', () => {
    const script = {
      id: 's_tp',
      kind: 'script',
      comment: 'tp pulse',
      code: [
        'if (Trigger && !TrigLast) {',
        '    PulseActive = true;',
        '    PulseET_ms = 0;',
        '}',
        'TrigLast = Trigger;',
        'if (PulseActive) {',
        '    Q_Pulse = true;',
        '    PulseET_ms += 5;',
        '    if (PulseET_ms >= 15) {',
        '        PulseActive = false;',
        '        Q_Pulse = false;',
        '    }',
        '} else {',
        '    Q_Pulse = false;',
        '}',
      ].join('\n'),
    };
    const h = makeHarness(projectWith([script]));
    const { program } = compileJsProgram(h, { Trigger: true });

    program.scan();
    expect(program.ctx.tags.Q_Pulse).toBe(true);
    expect(program.ctx.tags.PulseET_ms).toBe(5);
    program.scan();
    expect(program.ctx.tags.Q_Pulse).toBe(true);
    program.scan();
    expect(program.ctx.tags.Q_Pulse).toBe(false);
  });
});
