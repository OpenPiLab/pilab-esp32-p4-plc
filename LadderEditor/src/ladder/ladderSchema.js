// Formal project shape validation for the PiLab Ladder Project JSON format.
// This intentionally avoids a runtime JSON Schema dependency in the browser.
// The canonical JSON Schema file is schema/pilab-ladder-project.schema.json.

export const LADDER_PROJECT_SCHEMA_ID = 'pilab.ladder.project';
export const LADDER_PROJECT_SCHEMA_VERSION = 1;
export const LADDER_SLOT_COUNT = 8;
export const LADDER_NODE_COUNT = 9;
export const LADDER_SYMBOL_TYPES = Object.freeze(['NO', 'NC', 'OUT', 'SET', 'RST', 'ONS', 'TON', 'TOF', 'CTU', 'CTD']);
export const LADDER_FUNCTION_BLOCK_TYPES = Object.freeze(['TON', 'TOF', 'CTU', 'CTD']);
export const LADDER_COUNTER_TYPES = Object.freeze(['CTU', 'CTD']);

export function isPlainObject(value) {
  return !!value && typeof value === 'object' && !Array.isArray(value);
}

export function isValidTagIdentifier(tag) {
  return /^[A-Za-z_][A-Za-z0-9_]*$/.test(String(tag || ''));
}

function pathJoin(base, key) {
  if (typeof key === 'number') return `${base}[${key}]`;
  return base ? `${base}.${key}` : key;
}

function issue(level, path, message) {
  return { level, path, message };
}

function validateSymbolShape(symbol, path) {
  const issues = [];

  if (symbol === null) return issues;
  if (!isPlainObject(symbol)) {
    issues.push(issue('error', path, 'Slot entry must be null or a symbol object.'));
    return issues;
  }

  if (typeof symbol.id !== 'string' || !symbol.id.trim()) {
    issues.push(issue('error', pathJoin(path, 'id'), 'Symbol id is required and must be a non-empty string.'));
  }

  if (!LADDER_SYMBOL_TYPES.includes(symbol.type)) {
    issues.push(issue('error', pathJoin(path, 'type'), `Symbol type must be one of ${LADDER_SYMBOL_TYPES.join(', ')}.`));
  }

  if (typeof symbol.tag !== 'string' || !symbol.tag.trim()) {
    issues.push(issue('error', pathJoin(path, 'tag'), 'Symbol tag is required and must be a non-empty string.'));
  } else if (!isValidTagIdentifier(symbol.tag)) {
    issues.push(issue('error', pathJoin(path, 'tag'), 'Symbol tag must be a valid AngelScript identifier.'));
  }

  if (LADDER_FUNCTION_BLOCK_TYPES.includes(symbol.type)) {
    if (!(Number(symbol.preset) > 0)) {
      issues.push(issue('error', pathJoin(path, 'preset'), `${symbol.type} symbols require a positive numeric preset.`));
    }
  }

  if (symbol.resetTag !== undefined && typeof symbol.resetTag !== 'string') {
    issues.push(issue('error', pathJoin(path, 'resetTag'), 'resetTag must be a string when present.'));
  }

  return issues;
}

function validateSlotArray(slots, path) {
  const issues = [];

  if (!Array.isArray(slots)) {
    issues.push(issue('error', path, 'Expected an array of exactly 8 slots.'));
    return issues;
  }

  if (slots.length !== LADDER_SLOT_COUNT) {
    issues.push(issue('error', path, `Expected exactly ${LADDER_SLOT_COUNT} slots.`));
  }

  slots.forEach((slot, index) => {
    issues.push(...validateSymbolShape(slot, pathJoin(path, index)));
  });

  return issues;
}

function validateBranchShape(branch, path) {
  const issues = [];

  if (!isPlainObject(branch)) {
    issues.push(issue('error', path, 'Branch must be an object.'));
    return issues;
  }

  if (typeof branch.id !== 'string' || !branch.id.trim()) {
    issues.push(issue('error', pathJoin(path, 'id'), 'Branch id is required and must be a non-empty string.'));
  }

  if (!Number.isInteger(branch.start) || branch.start < 0 || branch.start > 8) {
    issues.push(issue('error', pathJoin(path, 'start'), 'Branch start must be an integer wire node from 0 through 8.'));
  }

  if (!Number.isInteger(branch.end) || branch.end < 0 || branch.end > 8) {
    issues.push(issue('error', pathJoin(path, 'end'), 'Branch end must be an integer wire node from 0 through 8.'));
  }

  if (Number.isInteger(branch.start) && Number.isInteger(branch.end) && branch.end <= branch.start) {
    issues.push(issue('error', path, 'Branch end must be greater than branch start.'));
  }

  issues.push(...validateSlotArray(branch.cells, pathJoin(path, 'cells')));

  if (Array.isArray(branch.cells) && Number.isInteger(branch.start) && Number.isInteger(branch.end)) {
    branch.cells.forEach((slot, index) => {
      if (slot && (index < branch.start || index >= branch.end)) {
        issues.push(issue('error', pathJoin(pathJoin(path, 'cells'), index), 'Branch symbol is outside the branch span.'));
      }
    });
  }

  return issues;
}

function validateRungShape(rung, path) {
  const issues = [];

  if (!isPlainObject(rung)) {
    issues.push(issue('error', path, 'Rung must be an object.'));
    return issues;
  }

  if (typeof rung.id !== 'string' || !rung.id.trim()) {
    issues.push(issue('error', pathJoin(path, 'id'), 'Rung id is required and must be a non-empty string.'));
  }

  if (typeof rung.comment !== 'string') {
    issues.push(issue('error', pathJoin(path, 'comment'), 'Rung comment is required and must be a string.'));
  }

  if (rung.kind === 'script') {
    if (typeof rung.code !== 'string') {
      issues.push(issue('error', pathJoin(path, 'code'), 'Script rung code is required and must be a string.'));
    }
    return issues;
  }

  if (rung.kind !== 'ladder') {
    issues.push(issue('error', pathJoin(path, 'kind'), 'Rung kind must be either ladder or script.'));
    return issues;
  }

  issues.push(...validateSlotArray(rung.main, pathJoin(path, 'main')));

  if (!Array.isArray(rung.branches)) {
    issues.push(issue('error', pathJoin(path, 'branches'), 'Ladder rung branches must be an array.'));
  } else {
    rung.branches.forEach((branch, index) => {
      issues.push(...validateBranchShape(branch, pathJoin(pathJoin(path, 'branches'), index)));
    });
  }

  return issues;
}

export function validateLadderProjectShape(project) {
  const issues = [];

  if (!isPlainObject(project)) {
    return [issue('error', '', 'Project root must be an object.')];
  }

  if (project.schema !== undefined && project.schema !== LADDER_PROJECT_SCHEMA_ID) {
    issues.push(issue('error', 'schema', `When present, schema must be ${LADDER_PROJECT_SCHEMA_ID}.`));
  }

  if (project.schema_version !== undefined) {
    if (!Number.isInteger(project.schema_version) || project.schema_version < 1) {
      issues.push(issue('error', 'schema_version', 'schema_version must be a positive integer.'));
    }
  }

  if (typeof project.name !== 'string' || !project.name.trim()) {
    issues.push(issue('error', 'name', 'Project name is required and must be a non-empty string.'));
  }

  if (!(Number(project.scan_ms) > 0)) {
    issues.push(issue('error', 'scan_ms', 'scan_ms is required and must be a positive number.'));
  }

  if (!Array.isArray(project.rungs)) {
    issues.push(issue('error', 'rungs', 'Project rungs must be an array.'));
  } else {
    project.rungs.forEach((rung, index) => {
      issues.push(...validateRungShape(rung, pathJoin('rungs', index)));
    });
  }

  return issues;
}

export function assertValidLadderProjectShape(project) {
  const issues = validateLadderProjectShape(project);
  const errors = issues.filter(item => item.level === 'error');
  if (errors.length) {
    const message = errors.slice(0, 5).map(item => `${item.path || '<root>'}: ${item.message}`).join('\n');
    throw new Error(`Invalid PiLab ladder project JSON:\n${message}`);
  }
  return project;
}

export function addFormalSchemaMetadata(project) {
  if (!isPlainObject(project)) return project;
  project.schema = project.schema || LADDER_PROJECT_SCHEMA_ID;
  project.schema_version = project.schema_version || LADDER_PROJECT_SCHEMA_VERSION;
  return project;
}
