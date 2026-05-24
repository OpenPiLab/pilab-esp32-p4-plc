import { describe, it, expect } from 'vitest';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  addFormalSchemaMetadata,
  assertValidLadderProjectShape,
  validateLadderProjectShape,
  LADDER_PROJECT_SCHEMA_ID,
  LADDER_PROJECT_SCHEMA_VERSION,
} from '../src/ladder/ladderSchema.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const schemaPath = path.resolve(__dirname, '../schema/pilab-ladder-project.schema.json');

const emptySlots = () => Array(8).fill(null);
const NO = (tag) => ({ id: `no_${tag}`, type: 'NO', tag });
const OUT = (tag) => ({ id: `out_${tag}`, type: 'OUT', tag });
const SET = (tag) => ({ id: `set_${tag}`, type: 'SET', tag });
const RST = (tag) => ({ id: `rst_${tag}`, type: 'RST', tag });
const ONS = (tag) => ({ id: `ons_${tag}`, type: 'ONS', tag });
const TON = (tag, preset = 1000) => ({ id: `ton_${tag}`, type: 'TON', tag, preset });
const CTU = (tag, preset = 10, resetTag = '') => ({ id: `ctu_${tag}`, type: 'CTU', tag, preset, resetTag });

function validProject() {
  const main = emptySlots();
  main[0] = NO('I0_Start');
  main[3] = TON('T_Delay', 250);
  main[7] = OUT('Q0_Motor');

  const cells = emptySlots();
  cells[0] = NO('Q0_Motor');

  return {
    schema: LADDER_PROJECT_SCHEMA_ID,
    schema_version: LADDER_PROJECT_SCHEMA_VERSION,
    name: 'Schema Test Project',
    scan_ms: 5,
    rungs: [
      {
        id: 'r1',
        kind: 'ladder',
        comment: 'Latch with timer',
        main,
        branches: [{ id: 'b1', start: 0, end: 1, cells }],
      },
      {
        id: 'r2',
        kind: 'ladder',
        comment: 'Counter reset expression',
        main: [NO('PartSeen'), CTU('C_Parts', 3, 'ResetPB || !AutoMode'), null, null, null, null, null, OUT('BatchDone')],
        branches: [],
      },
      {
        id: 's1',
        kind: 'script',
        comment: 'Script rung',
        code: 'Q_Debug = I0_Start && !I1_Stop;',
      },
    ],
  };
}

describe('formal ladder JSON schema document', () => {
  it('is present and parseable', () => {
    const schema = JSON.parse(fs.readFileSync(schemaPath, 'utf8'));
    expect(schema.title).toBe('PiLab Ladder Project');
    expect(schema.$defs.symbol.properties.type.enum).toContain('TON');
    expect(schema.$defs.symbol.properties.type.enum).toContain('SET');
    expect(schema.$defs.symbol.properties.type.enum).toContain('RST');
    expect(schema.$defs.symbol.properties.type.enum).toContain('ONS');
    expect(schema.$defs.slotArray8.minItems).toBe(8);
    expect(schema.$defs.slotArray8.maxItems).toBe(8);
  });
});

describe('ladder project shape validation', () => {
  it('accepts a valid formal project', () => {
    const issues = validateLadderProjectShape(validProject());
    expect(issues).toEqual([]);
  });

  it('adds schema metadata for new/normalized projects', () => {
    const project = { name: 'Legacy Export', scan_ms: 5, rungs: [] };
    addFormalSchemaMetadata(project);
    expect(project.schema).toBe(LADDER_PROJECT_SCHEMA_ID);
    expect(project.schema_version).toBe(LADDER_PROJECT_SCHEMA_VERSION);
  });


  it('accepts SET, RST, and ONS symbols without presets', () => {
    const main = emptySlots();
    main[0] = NO('StartPB');
    main[1] = ONS('ONS_Start');
    main[6] = SET('RunLatch');
    main[7] = RST('RunLatch');
    const project = {
      schema: LADDER_PROJECT_SCHEMA_ID,
      schema_version: LADDER_PROJECT_SCHEMA_VERSION,
      name: 'Latch Symbols',
      scan_ms: 5,
      rungs: [{ id: 'r_latch', kind: 'ladder', comment: 'Latch symbols', main, branches: [] }],
    };

    expect(validateLadderProjectShape(project)).toEqual([]);
  });

  it('rejects malformed project roots', () => {
    const issues = validateLadderProjectShape({ name: '', scan_ms: 0, rungs: 'bad' });
    expect(issues.map((item) => item.path)).toContain('name');
    expect(issues.map((item) => item.path)).toContain('scan_ms');
    expect(issues.map((item) => item.path)).toContain('rungs');
  });

  it('rejects invalid slot counts', () => {
    const project = validProject();
    project.rungs[0].main = Array(7).fill(null);
    const issues = validateLadderProjectShape(project);
    expect(issues.some((item) => item.path === 'rungs[0].main' && item.message.includes('8'))).toBe(true);
  });

  it('rejects invalid tags and missing function block presets', () => {
    const project = validProject();
    project.rungs[0].main[0].tag = '0 Bad Tag';
    delete project.rungs[0].main[3].preset;
    const issues = validateLadderProjectShape(project);
    expect(issues.some((item) => item.path === 'rungs[0].main[0].tag')).toBe(true);
    expect(issues.some((item) => item.path === 'rungs[0].main[3].preset')).toBe(true);
  });

  it('rejects branch spans and branch symbols outside their span', () => {
    const project = validProject();
    project.rungs[0].branches[0].start = 3;
    project.rungs[0].branches[0].end = 1;
    project.rungs[0].branches[0].cells[7] = OUT('WrongPlace');
    const issues = validateLadderProjectShape(project);
    expect(issues.some((item) => item.message.includes('greater than branch start'))).toBe(true);
    expect(issues.some((item) => item.message.includes('outside the branch span'))).toBe(true);
  });

  it('throws a useful error summary for invalid imports', () => {
    const project = validProject();
    project.rungs[0].main[0].tag = 'Bad Tag With Spaces';
    expect(() => assertValidLadderProjectShape(project)).toThrow(/Invalid PiLab ladder project JSON/);
  });
});

describe('ladder schema metadata parameter validation', () => {
  it('documents metadata block params in the formal JSON schema', () => {
    const schema = JSON.parse(fs.readFileSync(schemaPath, 'utf8'));
    expect(schema.$defs.blockParam.properties.enabled.type).toBe('boolean');
    expect(schema.$defs.blockParam.properties.tag.$ref).toBe('#/$defs/identifier');
    expect(schema.$defs.blockParam.properties.min.type).toBe('number');
    expect(schema.$defs.blockParam.properties.max.type).toBe('number');
    expect(schema.$defs.symbol.properties.param.$ref).toBe('#/$defs/blockParam');
    expect(schema.$defs.symbol.examples.some((item) => item.param && item.param.tag === 'T_StartDelay_PT')).toBe(true);
  });

  it('accepts metadata params on TON and CTU function blocks', () => {
    const project = validProject();
    project.rungs[0].main[3].param = {
      enabled: true,
      tag: 'T_Delay_PT',
      min: 0,
      max: 600000,
      units: 'ms',
    };
    project.rungs[1].main[1].param = {
      enabled: true,
      tag: 'C_Parts_PV',
      min: 0,
      max: 999999,
      units: 'count',
    };

    expect(validateLadderProjectShape(project)).toEqual([]);
  });

  it('rejects malformed metadata params', () => {
    const project = validProject();
    project.rungs[0].main[3].param = {
      enabled: 'yes',
      tag: 'Bad Tag',
      min: 100,
      max: 10,
      units: 123,
    };

    const issues = validateLadderProjectShape(project);
    const paths = issues.map((item) => item.path);
    expect(paths).toContain('rungs[0].main[3].param.enabled');
    expect(paths).toContain('rungs[0].main[3].param.tag');
    expect(paths).toContain('rungs[0].main[3].param.units');
    expect(issues.some((item) => item.path === 'rungs[0].main[3].param' && item.message.includes('param.max'))).toBe(true);
  });

  it('requires a valid param tag when metadata param exposure is enabled', () => {
    const project = validProject();
    project.rungs[0].main[3].param = { enabled: true, min: 0, max: 600000 };

    const issues = validateLadderProjectShape(project);
    expect(issues.some((item) => item.path === 'rungs[0].main[3].param.tag')).toBe(true);
  });

  it('rejects metadata params on non-function-block symbols but still accepts legacy blocks without params', () => {
    const legacy = validProject();
    expect(validateLadderProjectShape(legacy)).toEqual([]);

    const project = validProject();
    project.rungs[0].main[0].param = { enabled: true, tag: 'I0_Start_PT', min: 0, max: 1 };

    const issues = validateLadderProjectShape(project);
    expect(issues.some((item) => item.path === 'rungs[0].main[0].param' && item.message.includes('only valid'))).toBe(true);
  });
});
