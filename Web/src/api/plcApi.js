import { apiJson, apiText } from './http';

export function getCommandCenter() {
  return apiJson('/api/command_center');
}

export function getPlcData() {
  return apiJson('/api/plc_data');
}

export function getTagData() {
  return apiJson('/api/tag_data');
}

export async function setPlcMode(run) {
  await apiText('/api/plc_mode?run=' + (run ? '1' : '0'), { method: 'POST' });
}

export async function writePlcTag(tag, value) {
  await apiText('/api/plc_write', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ tag, value }),
  });
}
