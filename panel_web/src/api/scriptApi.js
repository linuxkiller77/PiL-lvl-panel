import { api, postJson } from './http.js';
export const getScript = () => api('/api/script');
export const getRuntimeScript = () => api('/api/script/runtime');
export const saveScript = script => postJson('/api/script', { script });
export const compileScript = script => postJson('/api/script/compile', { script });
export const callScript = fn => postJson('/api/script/call', { function: fn });
export const getScriptStatus = () => api('/api/script/status');
