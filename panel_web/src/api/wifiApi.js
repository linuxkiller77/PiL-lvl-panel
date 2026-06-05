import { api, postJson } from './http.js';

export const getWifiStatus = () => api('/api/wifi/status');
export const scanWifiNetworks = () => api('/api/wifi/scan');
export const connectWifi = (ssid, password, hostname) => postJson('/api/wifi/connect', { ssid, password, hostname });
export const saveWifiHostname = (hostname) => postJson('/api/wifi/hostname', { hostname });
export const forgetWifi = () => postJson('/api/wifi/forget', {});
