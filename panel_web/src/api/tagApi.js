import { api, postJson } from './http.js';

export const getTags = () => api('/api/tags');
export const saveTagsToRam = (payload) => postJson('/api/tags', payload);
export const saveTagsToFlash = (payload) => postJson('/api/tags/save', payload);
export const writeTag = (name, value) => postJson('/api/tags/write', { name, value });
