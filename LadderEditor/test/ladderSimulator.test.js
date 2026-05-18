import { describe, it, expect, vi, afterEach } from 'vitest';
import { ladderModelMethods } from '../src/ladder/ladderModel.js';
import { ladderSimulatorMethods } from '../src/ladder/ladderSimulator.js';
import { ladderTranspilerMethods } from '../src/ladder/ladderTranspiler.js';

function harness(project) {
  return {
    project,
    simTags: {},
    simBlocks: {},
    simRungs: {},
    simScanCount: 0,
    show() {},
    stopSimRun() {},
    ...ladderModelMethods,
    ...ladderTranspilerMethods,
    ...ladderSimulatorMethods,
  };
}
function rung() { return { id:'r1', kind:'ladder', comment:'', main:Array(8).fill(null), branches:[] }; }
const NO = tag => ({ id:`no_${tag}`, type:'NO', tag });
const OUT = tag => ({ id:`out_${tag}`, type:'OUT', tag });
const TOF = (tag,preset=20) => ({ id:`tof_${tag}`, type:'TOF', tag, preset });
const CTU = (tag,preset=3,resetTag='') => ({ id:`ctu_${tag}`, type:'CTU', tag, preset, resetTag });
const CTD = (tag,preset=3,resetTag='') => ({ id:`ctd_${tag}`, type:'CTD', tag, preset, resetTag });
const SET = tag => ({ id:`set_${tag}`, type:'SET', tag });
const RST = tag => ({ id:`rst_${tag}`, type:'RST', tag });
const ONS = tag => ({ id:`ons_${tag}`, type:'ONS', tag });

afterEach(() => {
  vi.restoreAllMocks();
});


describe('ladder simulator run timing', () => {
  it('uses a real-time accumulator so a 5 ms project scan keeps correct time with a 20 ms browser callback', () => {
    const h = harness({ name:'sim', scan_ms:5, rungs:[] });
    h.simStep = vi.fn();
    let intervalCallback = null;
    const intervalSpy = vi.spyOn(globalThis, 'setInterval').mockImplementation((cb, ms) => {
      intervalCallback = cb;
      return 123;
    });
    vi.spyOn(globalThis, 'clearInterval').mockImplementation(() => {});
    let nowMs = 0;
    vi.spyOn(globalThis.performance, 'now').mockImplementation(() => nowMs);

    h.toggleSimRun();
    expect(intervalSpy).toHaveBeenCalledTimes(1);
    expect(intervalSpy.mock.calls[0][1]).toBe(20);
    expect(h.simStep).toHaveBeenCalledTimes(1); // immediate UI response scan

    nowMs = 20;
    intervalCallback();
    expect(h.simStep).toHaveBeenCalledTimes(5); // +4 scans at 5 ms each

    h.stopSimRun();
  });

  it('defaults invalid project scan values to 5 ms simulated scans', () => {
    const h = harness({ name:'sim', scan_ms:0, rungs:[] });
    h.simStep = vi.fn();
    let intervalCallback = null;
    vi.spyOn(globalThis, 'setInterval').mockImplementation((cb, ms) => {
      intervalCallback = cb;
      return 123;
    });
    vi.spyOn(globalThis, 'clearInterval').mockImplementation(() => {});
    let nowMs = 0;
    vi.spyOn(globalThis.performance, 'now').mockImplementation(() => nowMs);

    h.toggleSimRun();
    nowMs = 5;
    intervalCallback();

    expect(h.simStep).toHaveBeenCalledTimes(2); // immediate + one default 5 ms scan
    h.stopSimRun();
  });
});

describe('ladder simulator counter reset/load expressions', () => {
  it('includes counter reset/load expression tags in simulator input tag list', () => {
    const r = rung();
    r.main[0] = NO('PartSeen');
    r.main[1] = CTU('C_Parts', 3, 'ResetPB || !AutoMode');
    r.main[7] = OUT('BatchDone');
    const h = harness({ name:'sim', scan_ms:5, rungs:[r] });

    expect(h.simInputTagList()).toEqual(['AutoMode', 'BatchDone', 'C_Parts', 'PartSeen', 'ResetPB']);

    h.simStep(false);
    expect(h.simInputTagList()).toEqual(['AutoMode', 'BatchDone', 'PartSeen', 'ResetPB']);
  });

  it('resets CTU when reset expression becomes true', () => {
    const r = rung();
    r.main[0] = NO('PartSeen');
    r.main[1] = CTU('C_Parts', 3, 'ResetPB');
    r.main[7] = OUT('BatchDone');
    const h = harness({ name:'sim', scan_ms:5, rungs:[r] });

    h.simTags.PartSeen = true;
    h.simStep(false);
    expect(h.simBlocks.C_Parts.count).toBe(1);

    h.simTags.ResetPB = true;
    h.simStep(false);
    expect(h.simBlocks.C_Parts.count).toBe(0);
    expect(h.simBlocks.C_Parts.output).toBe(false);
  });



  it('keeps CTU done output powering a downstream coil after the count input falls', () => {
    const r = rung();
    r.main[0] = NO('PartSeen');
    r.main[1] = CTU('C_Parts', 2, 'ResetPB');
    r.main[7] = OUT('BatchDone');
    const h = harness({ name:'sim', scan_ms:5, rungs:[r] });

    h.simTags.PartSeen = true;
    h.simStep(false);
    h.simTags.PartSeen = false;
    h.simStep(false);
    h.simTags.PartSeen = true;
    h.simStep(false);

    expect(h.simBlocks.C_Parts.output).toBe(true);
    expect(h.simTags.BatchDone).toBe(true);

    h.simTags.PartSeen = false;
    h.simStep(false);
    expect(h.simBlocks.C_Parts.output).toBe(true);
    expect(h.simTags.BatchDone).toBe(true);

    h.simTags.ResetPB = true;
    h.simStep(false);
    expect(h.simBlocks.C_Parts.output).toBe(false);
    expect(h.simTags.BatchDone).toBe(false);
  });

  it('keeps CTD done output powering a downstream coil after the count input falls', () => {
    const r = rung();
    r.main[0] = NO('StepDone');
    r.main[1] = CTD('C_Remaining', 1, 'Reload');
    r.main[7] = OUT('AllDone');
    const h = harness({ name:'sim', scan_ms:5, rungs:[r] });

    h.simTags.StepDone = true;
    h.simStep(false);
    expect(h.simBlocks.C_Remaining.output).toBe(true);
    expect(h.simTags.AllDone).toBe(true);

    h.simTags.StepDone = false;
    h.simStep(false);
    expect(h.simBlocks.C_Remaining.output).toBe(true);
    expect(h.simTags.AllDone).toBe(true);

    h.simTags.Reload = true;
    h.simStep(false);
    expect(h.simBlocks.C_Remaining.output).toBe(false);
    expect(h.simTags.AllDone).toBe(false);
  });

  it('loads CTD from preset when load/reset expression becomes true', () => {
    const r = rung();
    r.main[0] = NO('StepDone');
    r.main[1] = CTD('C_Remaining', 2, 'Reload');
    r.main[7] = OUT('AllDone');
    const h = harness({ name:'sim', scan_ms:5, rungs:[r] });

    h.simTags.StepDone = true;
    h.simStep(false);
    h.simTags.StepDone = false;
    h.simStep(false);
    h.simTags.StepDone = true;
    h.simStep(false);
    expect(h.simBlocks.C_Remaining.count).toBe(0);
    expect(h.simBlocks.C_Remaining.output).toBe(true);

    h.simTags.Reload = true;
    h.simStep(false);
    expect(h.simBlocks.C_Remaining.count).toBe(2);
    expect(h.simBlocks.C_Remaining.output).toBe(false);
  });
});


describe('ladder simulator TOF behavior', () => {
  it('holds the output on for the preset time after input falls', () => {
    const r = rung();
    r.main[0] = NO('RunCmd');
    r.main[2] = TOF('T_RunHold', 20);
    r.main[7] = OUT('Q_Run');
    const h = harness({ name:'sim', scan_ms:5, rungs:[r] });

    h.simTags.RunCmd = true;
    h.simStep(false);
    h.simStep(false);
    expect(h.simBlocks.T_RunHold.output).toBe(true);
    expect(h.simTags.Q_Run).toBe(true);

    h.simTags.RunCmd = false;
    h.simStep(false);
    expect(h.simBlocks.T_RunHold.output).toBe(true);
    expect(h.simTags.Q_Run).toBe(true);
    expect(h.simBlocks.T_RunHold.elapsed_ms).toBe(5);

    h.simStep(false);
    h.simStep(false);
    h.simStep(false);
    expect(h.simBlocks.T_RunHold.output).toBe(false);
    expect(h.simTags.Q_Run).toBe(true);

    h.simStep(false);
    expect(h.simBlocks.T_RunHold.output).toBe(false);
    expect(h.simTags.Q_Run).toBe(false);
  });

  it('resets the TOF elapsed time if input returns high during the off-delay', () => {
    const r = rung();
    r.main[0] = NO('RunCmd');
    r.main[2] = TOF('T_RunHold', 20);
    r.main[7] = OUT('Q_Run');
    const h = harness({ name:'sim', scan_ms:5, rungs:[r] });

    h.simTags.RunCmd = true;
    h.simStep(false);
    h.simTags.RunCmd = false;
    h.simStep(false);
    expect(h.simBlocks.T_RunHold.elapsed_ms).toBe(5);

    h.simTags.RunCmd = true;
    h.simStep(false);
    expect(h.simBlocks.T_RunHold.output).toBe(true);
    expect(h.simBlocks.T_RunHold.elapsed_ms).toBe(0);
    expect(h.simTags.Q_Run).toBe(true);
  });
});

describe('ladder simulator element highlighting', () => {
  it('does not mark unpowered output coils amber', () => {
    const r = rung();
    r.main[0] = NO('Start');
    r.main[7] = OUT('Motor');
    const h = harness({ name:'sim', scan_ms:5, rungs:[r] });

    h.simStep(false);
    expect(h.simElementPowered(r, 'main', null, 7)).toBe(false);
    expect(h.simElementTrue(r, 'main', null, 7)).toBe(false);
    expect(h.simElementStroke(r, 'main', null, 7)).toBe('#22d3ee');

    h.simTags.Start = true;
    h.simStep(false);
    expect(h.simElementPowered(r, 'main', null, 7)).toBe(true);
    expect(h.simElementStroke(r, 'main', null, 7)).toBe('#34d399');

    h.simTags.Start = false;
    h.simStep(false);
    expect(h.simElementPowered(r, 'main', null, 7)).toBe(false);
    expect(h.simElementTrue(r, 'main', null, 7)).toBe(false);
    expect(h.simElementStroke(r, 'main', null, 7)).toBe('#22d3ee');
  });
});


describe('ladder simulator SET/RST and ONS behavior', () => {
  it('latches and unlatches a tag with SET and RST coils', () => {
    const setRung = rung();
    setRung.id = 'set';
    setRung.main[0] = NO('StartPB');
    setRung.main[7] = SET('RunLatch');

    const resetRung = rung();
    resetRung.id = 'reset';
    resetRung.main[0] = NO('StopPB');
    resetRung.main[7] = RST('RunLatch');

    const h = harness({ name:'sim', scan_ms:5, rungs:[setRung, resetRung] });

    h.simStep(false);
    expect(h.simTags.RunLatch).toBeFalsy();

    h.simTags.StartPB = true;
    h.simStep(false);
    expect(h.simTags.RunLatch).toBe(true);

    h.simTags.StartPB = false;
    h.simStep(false);
    expect(h.simTags.RunLatch).toBe(true);

    h.simTags.StopPB = true;
    h.simStep(false);
    expect(h.simTags.RunLatch).toBe(false);
  });


  it('renders SET and RST coils as powered only by incoming rung power, not as always-true contacts', () => {
    const setRung = rung();
    setRung.id = 'setDisplay';
    setRung.main[0] = NO('StartPB');
    setRung.main[7] = SET('RunLatch');

    const resetRung = rung();
    resetRung.id = 'resetDisplay';
    resetRung.main[0] = NO('StopPB');
    resetRung.main[7] = RST('RunLatch');

    const h = harness({ name:'sim', scan_ms:5, rungs:[setRung, resetRung] });

    h.simStep(false);
    expect(h.simElementTrue(setRung, 'main', null, 7)).toBe(false);
    expect(h.simElementTrue(resetRung, 'main', null, 7)).toBe(false);
    expect(h.simElementPowered(setRung, 'main', null, 7)).toBe(false);
    expect(h.simElementPowered(resetRung, 'main', null, 7)).toBe(false);

    h.simTags.StartPB = true;
    h.simStep(false);
    expect(h.simTags.RunLatch).toBe(true);
    expect(h.simElementPowered(setRung, 'main', null, 7)).toBe(true);
    expect(h.simElementPowered(resetRung, 'main', null, 7)).toBe(false);

    h.simTags.StartPB = false;
    h.simStep(false);
    expect(h.simTags.RunLatch).toBe(true);
    expect(h.simElementPowered(setRung, 'main', null, 7)).toBe(false);
    expect(h.simElementTrue(setRung, 'main', null, 7)).toBe(true);
    expect(h.simElementStroke(setRung, 'main', null, 7)).toBe('#f59e0b');

    h.simTags.StopPB = true;
    h.simStep(false);
    expect(h.simTags.RunLatch).toBe(false);
    expect(h.simElementPowered(resetRung, 'main', null, 7)).toBe(true);
    expect(h.simElementTrue(resetRung, 'main', null, 7)).toBe(false);

    h.simTags.StopPB = false;
    h.simStep(false);
    expect(h.simElementPowered(resetRung, 'main', null, 7)).toBe(false);
    expect(h.simElementTrue(resetRung, 'main', null, 7)).toBe(false);
    expect(h.simElementStroke(resetRung, 'main', null, 7)).toBe('#22d3ee');
  });

  it('ONS passes power for one scan only on the rising edge', () => {
    const r = rung();
    r.main[0] = NO('StartPB');
    r.main[1] = ONS('ONS_Start');
    r.main[7] = OUT('StartPulse');
    const h = harness({ name:'sim', scan_ms:5, rungs:[r] });

    h.simStep(false);
    expect(h.simTags.StartPulse).toBe(false);

    h.simTags.StartPB = true;
    h.simStep(false);
    expect(h.simTags.StartPulse).toBe(true);
    expect(h.simBlocks.ONS_Start.output).toBe(true);

    h.simStep(false);
    expect(h.simTags.StartPulse).toBe(false);
    expect(h.simBlocks.ONS_Start.output).toBe(false);

    h.simTags.StartPB = false;
    h.simStep(false);
    h.simTags.StartPB = true;
    h.simStep(false);
    expect(h.simTags.StartPulse).toBe(true);
  });
});

describe('ladder simulator rich script rung support', () => {
  it('executes AngelScript-style math and if blocks through the generated JavaScript simulator', () => {
    const script = {
      id: 'script_math',
      kind: 'script',
      comment: 'script math',
      code: [
        'ScaledSpeed = HmiSpeedSetpoint * SpeedTrim;',
        'SpeedCmd = ScaledSpeed;',
        'if (SpeedCmd > MaxSpeed) {',
        '    SpeedCmd = MaxSpeed;',
        '}',
        'Q_SpeedHigh = ScaledSpeed >= MaxSpeed;',
      ].join('\n'),
    };
    const h = harness({ name:'sim', scan_ms:5, rungs:[script] });
    h.simTags.HmiSpeedSetpoint = 300;
    h.simTags.SpeedTrim = 0.5;
    h.simTags.MaxSpeed = 100;

    expect(h.simInputTagList()).toEqual(['HmiSpeedSetpoint', 'MaxSpeed', 'Q_SpeedHigh', 'ScaledSpeed', 'SpeedCmd', 'SpeedTrim']);

    h.simStep(false);
    expect(h.simTags.ScaledSpeed).toBe(150);
    expect(h.simTags.SpeedCmd).toBe(100);
    expect(h.simTags.Q_SpeedHigh).toBe(true);
  });
});

describe('ladder simulator editable numeric tag helpers', () => {
  it('keeps numeric tag values numeric and evaluates ladder contacts as nonzero true', () => {
    const h = harness({ name:'sim', scan_ms:5, rungs:[] });

    h.setSimTagType('HmiSpeedSetpoint', 'number');
    expect(h.simTags.HmiSpeedSetpoint).toBe(0);
    expect(h.simTagType('HmiSpeedSetpoint')).toBe('number');
    expect(h.simTagValue('HmiSpeedSetpoint')).toBe(false);

    h.setSimTagFromInput('HmiSpeedSetpoint', '250');
    expect(h.simTags.HmiSpeedSetpoint).toBe(250);
    expect(h.simTagType('HmiSpeedSetpoint')).toBe('number');
    expect(h.simTagEditText('HmiSpeedSetpoint')).toBe('250');
    expect(h.simTagValue('HmiSpeedSetpoint')).toBe(true);

    h.setSimTagFromInput('HmiSpeedSetpoint', '0');
    expect(h.simTags.HmiSpeedSetpoint).toBe(0);
    expect(h.simTagValue('HmiSpeedSetpoint')).toBe(false);
  });

  it('allows boolean tags to be converted to number and back to bool', () => {
    const h = harness({ name:'sim', scan_ms:5, rungs:[] });

    h.toggleSimTag('PartSensor');
    expect(h.simTags.PartSensor).toBe(true);
    expect(h.simTagType('PartSensor')).toBe('bool');

    h.setSimTagType('PartSensor', 'number');
    expect(h.simTags.PartSensor).toBe(1);
    expect(h.simTagType('PartSensor')).toBe('number');

    h.setSimTagType('PartSensor', 'bool');
    expect(h.simTags.PartSensor).toBe(true);
    expect(h.simTagType('PartSensor')).toBe('bool');
  });
});
