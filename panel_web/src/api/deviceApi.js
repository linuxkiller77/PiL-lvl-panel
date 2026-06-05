import { api } from './http.js';
export const getDeviceStatus = () => api('/api/device/status');
