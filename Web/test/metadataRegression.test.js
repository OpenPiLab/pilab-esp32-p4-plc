import { describe, expect, test, vi, afterEach } from 'vitest';
import { ladderModelMethods } from '../src/ladder/ladderModel.js';
import { ladderTranspilerMethods } from '../src/ladder/ladderTranspiler.js';
import { ladderSimulatorMethods } from '../src/ladder/ladderSimulator.js';
import { ladderTagRegistryMethods } from '../src/ladder/ladderTagRegistry.js';

function makeHarness(project) {
  return {
    project,
    simTags: {},
    simBlocks: {},
    simRungs: {},
    simScanCount: 0,
    tagRegistryImported: {},
    tagRegistryEdits: {},
    show() {},
    stopSimRun() {},
    $forceUpdate() {},
    ...ladderModelMethods,
    ...ladderTranspilerMethods,
    ...ladderSimulatorMethods,
    ...ladderTagRegistryMethods,
  };
}

function emptyRung(comment = '') {
  return { id: 'r1', kind: 'ladder', comment, main: Array(8).fill(null), branches: [] };
}

function projectWith(rungs, scan_ms = 5) {
  return { name: 'Metadata Regression Project', scan_ms, rungs };
}

const NO = (tag) => ({ id: `no_${tag}`, type: 'NO', tag });
const OUT = (tag) => ({ id: `out_${tag}`, type: 'OUT', tag });
const TON = (tag, preset = 1000, param = undefined) => ({ id: `ton_${tag}`, type: 'TON', tag, preset, ...(param ? { param } : {}) });
const CTU = (tag, preset = 10, resetTag = '', param = undefined) => ({ id: `ctu_${tag}`, type: 'CTU', tag, preset, resetTag, ...(param ? { param } : {}) });

afterEach(() => {
  vi.restoreAllMocks();
});

describe('PiLab metadata-aware AngelScript generation', () => {
  test('emits PiLabParam and PiLabMonitor for an exposed timer preset', () => {
    const r = emptyRung('metadata timer');
    r.main[0] = NO('HMI_I0');
    r.main[2] = TON('T_Delay', 1000, { enabled: true, tag: 'T_Delay_PT', min: 0, max: 600000 });
    r.main[7] = OUT('Q0');

    const h = makeHarness(projectWith([r], 5));
    const as = h.transpile();

    expect(as).toContain('[PiLabParam name="PT" tag="T_Delay_PT" type="int" default="1000" min="0" max="600000"]');
    expect(as).toContain('[PiLabMonitor name="T_Delay" type="TON" fields="Q:bool,ET:int,PT:int"]');
    expect(as).toContain('TON T_Delay(1000);');
    expect(as).toContain('T_Delay.SetPreset(uint(T_Delay_PT));');
    expect(as).toContain('T_Delay.update((HMI_I0), 5);');
  });

  test('emits PiLabParam and PiLabMonitor for an exposed counter preset', () => {
    const r = emptyRung('metadata counter');
    r.main[0] = NO('PartSeen');
    r.main[1] = CTU('C_Parts', 10, 'ResetPB', { enabled: true, tag: 'C_Parts_PV', min: 0, max: 999999 });
    r.main[7] = OUT('BatchDone');

    const h = makeHarness(projectWith([r], 5));
    const as = h.transpile();

    expect(as).toContain('[PiLabParam name="PV" tag="C_Parts_PV" type="int" default="10" min="0" max="999999"]');
    expect(as).toContain('[PiLabMonitor name="C_Parts" type="CTU" fields="Q:bool,CV:int,PV:int"]');
    expect(as).toContain('CTU C_Parts(10);');
    expect(as).toContain('C_Parts.SetPreset(uint(C_Parts_PV));');
    expect(as).toContain('C_Parts.update((PartSeen), (ResetPB));');
  });
});

describe('JavaScript simulator remains sandboxed and metadata-free', () => {
  test('does not emit AngelScript metadata or firmware monitor-tag writes in JavaScript output', () => {
    const r = emptyRung('sim sandbox');
    r.main[0] = NO('HMI_I0');
    r.main[2] = TON('T_Delay', 1000, { enabled: true, tag: 'T_Delay_PT', min: 0, max: 600000 });
    r.main[7] = OUT('Q0');

    const h = makeHarness(projectWith([r], 5));
    const js = h.transpileJavaScript();

    expect(js).not.toContain('PiLabParam');
    expect(js).not.toContain('PiLabMonitor');
    expect(js).not.toContain('__obj_');
    expect(js).not.toContain('T_Delay_PT');
    expect(js).not.toContain('ctx.tags[');
    expect(js).toContain('ctx.block("T_Delay", "TON", 1000).update(!!(ctx.tag("HMI_I0")), 5);');
  });

  test('browser simulator updates timer internals from its own sim context when metadata is enabled', () => {
    const r = emptyRung('sim timer runs');
    r.main[0] = NO('HMI_I0');
    r.main[2] = TON('T_Delay', 20, { enabled: true, tag: 'T_Delay_PT', min: 0, max: 600000 });
    r.main[7] = OUT('Q0');

    const h = makeHarness(projectWith([r], 5));
    h.simTags.HMI_I0 = true;

    h.simStep(false);
    h.simStep(false);

    expect(h.simBlocks.T_Delay.elapsed_ms).toBe(10);
    expect(h.simBlocks.T_Delay.preset_ms).toBe(20);
    expect(h.simTags.T_Delay_PT).toBeUndefined();
    expect(h.simTags.__obj_T_Delay_ET).toBeUndefined();
  });
});

describe('Tag registry discovers metadata params and monitors for HMI/live PLC mode', () => {
  test('adds writable param tags and read-only monitor tags for metadata-aware blocks', () => {
    const r = emptyRung('registry metadata');
    r.main[0] = NO('HMI_I0');
    r.main[2] = TON('T_Delay', 1000, { enabled: true, tag: 'T_Delay_PT', min: 0, max: 600000 });
    r.main[7] = OUT('Q0');

    const h = makeHarness(projectWith([r], 5));
    const payload = h.buildTagRegistryPayload();

    const param = payload.tags.find(t => t.name === 'T_Delay_PT');
    const q = payload.tags.find(t => t.name === '__obj_T_Delay_Q');
    const et = payload.tags.find(t => t.name === '__obj_T_Delay_ET');
    const pt = payload.tags.find(t => t.name === '__obj_T_Delay_PT');

    expect(param).toMatchObject({
      type: 'int',
      value: 1000,
      units: 'ms',
      min: 0,
      max: 600000,
      writable: true,
      retentive: true,
      hmi_visible: true,
      script_visible: true,
    });
    expect(q).toMatchObject({ type: 'bool', writable: false, retentive: false, hmi_visible: true, script_visible: true });
    expect(et).toMatchObject({ type: 'int', units: 'ms', writable: false, retentive: false });
    expect(pt).toMatchObject({ type: 'int', units: 'ms', writable: false, retentive: false });
  });
});
