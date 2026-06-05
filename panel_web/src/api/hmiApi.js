import { api, postJson } from './http.js';
export const getLayout = () => api('/api/hmi/layout');
export const getRuntimeLayout = () => api('/api/hmi/runtime');
export const saveLayout = layout => postJson('/api/hmi/layout', layout);
export const applyLayout = layout => postJson('/api/hmi/apply', layout);
export const reloadLayout = () => api('/api/hmi/reload', { method: 'POST' });
