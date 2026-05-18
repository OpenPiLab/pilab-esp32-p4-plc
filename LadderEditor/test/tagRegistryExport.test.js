import { describe, expect, test } from 'vitest';
import { ladderModelMethods } from '../src/ladder/ladderModel.js';
import { ladderTranspilerMethods } from '../src/ladder/ladderTranspiler.js';
import { ladderTagRegistryMethods } from '../src/ladder/ladderTagRegistry.js';

function makeHarness(project){
  return Object.assign({ project }, ladderModelMethods, ladderTranspilerMethods, ladderTagRegistryMethods);
}

describe('PLC Web Interface tag registry export', () => {
  test('exports HMI and memory tags but excludes physical I/O system tags', () => {
    const project = {
      name: 'tag test',
      scan_ms: 5,
      rungs: [
        {
          id: 'r1', kind: 'ladder', comment: '', branches: [],
          main: [
            { id:'a', type:'NO', tag:'HMI_StartPB' },
            { id:'b', type:'NC', tag:'I0' },
            null,null,null,null,null,
            { id:'c', type:'OUT', tag:'Q0' }
          ]
        },
        {
          id:'r2', kind:'script', comment:'',
          code: 'M_HeartbeatCounter += 1;\nQ6 = M_HeartbeatCounter < 50;\nHMI_StatusLamp = Q6;'
        }
      ]
    };
    const h = makeHarness(project);
    const payload = h.buildTagRegistryPayload();
    const names = payload.tags.map(t => t.name);
    expect(names).toContain('HMI_StartPB');
    expect(names).toContain('M_HeartbeatCounter');
    expect(names).toContain('HMI_StatusLamp');
    expect(names).not.toContain('I0');
    expect(names).not.toContain('Q0');
    expect(names).not.toContain('Q6');
  });

  test('uses PLC Web Interface tag field names and infers numeric counter type', () => {
    const project = { name:'x', scan_ms:5, rungs:[{ id:'s', kind:'script', comment:'', code:'M_DelayCounter += 1;\nHMI_StartPB = false;' }] };
    const h = makeHarness(project);
    const payload = h.buildTagRegistryPayload();
    const counter = payload.tags.find(t => t.name === 'M_DelayCounter');
    expect(counter).toMatchObject({ type:'int', value:0, units:'', min:0, max:100, retentive:true, script_visible:true });
    expect(Object.keys(counter).sort()).toEqual(['description','hmi_visible','max','min','name','retentive','script_visible','type','units','value','writable'].sort());
  });
});

test('applies editable tag metadata overrides without changing system I/O exclusions', () => {
  const project = { name:'x', scan_ms:5, rungs:[{ id:'s', kind:'script', comment:'', code:'HMI_StartPB = false;\nM_DelayCounter += 1;\nQ0 = HMI_StartPB;' }] };
  const h = makeHarness(project);
  h.tagRegistryEdits = {
    HMI_StartPB: {
      type: 'bool',
      value: true,
      units: 'cmd',
      writable: false,
      retentive: true,
      hmi_visible: false,
      script_visible: true,
      description: 'Edited start command metadata.'
    },
    Q0: { description: 'Should still be excluded because it is physical I/O.' }
  };
  const payload = h.buildTagRegistryPayload();
  const start = payload.tags.find(t => t.name === 'HMI_StartPB');
  expect(start).toMatchObject({ value:true, units:'cmd', writable:false, retentive:true, hmi_visible:false, description:'Edited start command metadata.' });
  expect(payload.tags.map(t => t.name)).not.toContain('Q0');
});

test('imports tag registry metadata, merges with discovered tags, and preserves unused imported tags', () => {
  const project = {
    name:'x', scan_ms:5,
    rungs:[{ id:'s', kind:'script', comment:'', code:'HMI_StartPB = false;\nM_NewCounter += 1;\nQ0 = HMI_StartPB;' }]
  };
  const h = makeHarness(project);
  h.tagRegistryImported = {};
  h.tagRegistryEdits = {};
  const count = h.importTagRegistryPayload({ tags: [
    { name:'HMI_StartPB', type:'bool', value:true, units:'cmd', min:0, max:1, writable:false, retentive:true, hmi_visible:true, script_visible:true, description:'Imported start button.' },
    { name:'OldUnusedTag', type:'float', value:12.5, units:'psi', min:0, max:200, writable:true, retentive:false, hmi_visible:true, script_visible:true, description:'Imported but not currently used.' },
    { name:'Q0', type:'bool', value:false, description:'System tag should not import.' }
  ]});
  expect(count).toBe(2);
  const rows = h.buildTagRegistryRows();
  const start = rows.find(t => t.name === 'HMI_StartPB');
  const unused = rows.find(t => t.name === 'OldUnusedTag');
  const fresh = rows.find(t => t.name === 'M_NewCounter');
  expect(start).toMatchObject({ description:'Imported start button.', units:'cmd', value:true, retentive:true, __used:true, __imported: expect.any(Object) });
  expect(unused).toMatchObject({ type:'float', value:12.5, units:'psi', __used:false, __imported: expect.any(Object) });
  expect(fresh).toMatchObject({ type:'int', __used:true });
  expect(rows.map(t => t.name)).not.toContain('Q0');
});

test('edited tag metadata overrides imported metadata without removing imported unused tags', () => {
  const project = { name:'x', scan_ms:5, rungs:[{ id:'s', kind:'script', comment:'', code:'HMI_StartPB = false;' }] };
  const h = makeHarness(project);
  h.tagRegistryImported = {};
  h.tagRegistryEdits = {};
  h.importTagRegistryPayload({ tags: [{ name:'HMI_StartPB', type:'bool', value:false, description:'Imported description.' }] });
  h.setTagRegistryOverride('HMI_StartPB', { description:'Edited description.', value:true });
  const payload = h.buildTagRegistryPayload();
  expect(payload.tags.find(t => t.name === 'HMI_StartPB')).toMatchObject({ description:'Edited description.', value:true });
});

test('deletes only unused imported tags and preserves used imported metadata until cleared', () => {
  const project = { name:'x', scan_ms:5, rungs:[{ id:'s', kind:'script', comment:'', code:'HMI_StartPB = false;\nM_NewCounter += 1;' }] };
  const h = makeHarness(project);
  h.tagRegistryImported = {};
  h.tagRegistryEdits = {};
  h.importTagRegistryPayload({ tags: [
    { name:'HMI_StartPB', type:'bool', value:true, description:'Imported used tag.' },
    { name:'OldUnusedTag', type:'float', value:12.5, description:'Imported unused tag.' }
  ]});

  expect(h.deleteImportedTagRegistryRow('HMI_StartPB')).toBe(false);
  expect(h.buildTagRegistryRows().find(t => t.name === 'HMI_StartPB')).toMatchObject({ __used:true, __imported: expect.any(Object) });

  expect(h.deleteImportedTagRegistryRow('OldUnusedTag')).toBe(true);
  expect(h.buildTagRegistryRows().map(t => t.name)).not.toContain('OldUnusedTag');

  h.clearImportedTagRegistryRows();
  const start = h.buildTagRegistryRows().find(t => t.name === 'HMI_StartPB');
  expect(start).toMatchObject({ __used:true, __imported:null });
  expect(start.description).toContain('HMI/simulator tag');
});

test('deleteUnusedImportedTagRegistryRows removes all unused imported rows in one call', () => {
  const project = { name:'x', scan_ms:5, rungs:[{ id:'s', kind:'script', comment:'', code:'HMI_StartPB = false;' }] };
  const h = makeHarness(project);
  h.tagRegistryImported = {};
  h.tagRegistryEdits = {};
  h.importTagRegistryPayload({ tags: [
    { name:'HMI_StartPB', type:'bool', value:true, description:'Imported used tag.' },
    { name:'OldUnusedA', type:'bool', value:false, description:'unused a' },
    { name:'OldUnusedB', type:'int', value:7, description:'unused b' }
  ]});
  const removed = h.deleteUnusedImportedTagRegistryRows();
  expect(removed).toBe(2);
  const names = h.buildTagRegistryRows().map(t => t.name);
  expect(names).toContain('HMI_StartPB');
  expect(names).not.toContain('OldUnusedA');
  expect(names).not.toContain('OldUnusedB');
});
