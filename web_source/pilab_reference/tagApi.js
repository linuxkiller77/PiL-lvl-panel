import { apiJson, apiText } from './http';

export async function loadTags() {
  return apiJson('/api/tags');
}

export async function saveTags(payload) {
  return apiText('/api/tags', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  });
}
