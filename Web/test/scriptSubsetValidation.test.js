import { describe, expect, test } from 'vitest';
import { ladderTranspilerMethods } from '../src/ladder/ladderTranspiler.js';

const h = Object.assign({}, ladderTranspilerMethods);

describe('Script Lite JavaScript emitter', () => {
  test('emits brace-style if without unsupported comments', () => {
    const out = h.jsEmitScriptRung('if (HMI_Enable) {\nQ_Debug = true;\n}\nelse {\nQ_Debug = false;\n}');
    expect(out).toContain('if (ctx.get("HMI_Enable"))');
    expect(out).toContain('ctx.set("Q_Debug", true);');
    expect(out).not.toContain('Unsupported script simulator line');
  });

  test('flags compact inline if as unsupported', () => {
    const out = h.jsEmitScriptRung('if(HMI_Enable)Q_Debug=true;');
    expect(out).toContain('Unsupported script simulator line');
  });
});


test('analyzes unsupported simulator lines without blocking AngelScript export', () => {
  const analysis = h.analyzeScriptRungForJsSimulator('if(HMI_Enable)Q_Debug=true;\nfor(int i=0;i<3;i++){ Q_Debug = true; }');
  expect(analysis.unsupportedLines.length).toBeGreaterThanOrEqual(2);
  expect(analysis.unsupportedLines[0]).toHaveProperty('line');
});

test('accepts brace-style script subset in analyzer', () => {
  const analysis = h.analyzeScriptRungForJsSimulator('if (HMI_Enable) {\n  Q_Debug = true;\n} else {\n  Q_Debug = false;\n}');
  expect(analysis.unsupportedLines).toEqual([]);
  expect(analysis.braceBalance).toBe(0);
});
