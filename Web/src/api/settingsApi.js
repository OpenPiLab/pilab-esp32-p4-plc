import { apiJson, apiPostJson } from './http';

export async function getSettingsPage(page) {
  return apiJson(`/api/settings/${encodeURIComponent(page)}`);
}

export async function applyRuntimeSettings(page, payload) {
  return apiPostJson(`/api/settings/${encodeURIComponent(page)}`, payload);
}

export async function saveSettingsPage(page) {
  return apiPostJson(`/api/settings/${encodeURIComponent(page)}/save`, {});
}
