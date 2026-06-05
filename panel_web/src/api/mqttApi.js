import { api, postJson } from './http.js';

export function getMqttConfig() { return api('/api/mqtt/config'); }
export function saveMqttConfig(config) { return postJson('/api/mqtt/config', config); }
export function getMqttStatus() { return api('/api/mqtt/status'); }
export function startMqtt() { return postJson('/api/mqtt/start', {}); }
export function stopMqtt() { return postJson('/api/mqtt/stop', {}); }
export function testMqttPublish(topic, payload, qos = 0, retain = false) {
  return postJson('/api/mqtt/test_publish', { topic, payload, qos, retain });
}
