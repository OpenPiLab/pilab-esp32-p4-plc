import { describe, it, expect } from 'vitest';
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
const CTU = (tag,preset=3,resetTag='') => ({ id:`ctu_${tag}`, type:'CTU', tag, preset, resetTag });
const CTD = (tag,preset=3,resetTag='') => ({ id:`ctd_${tag}`, type:'CTD', tag, preset, resetTag });

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
