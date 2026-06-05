import { ref } from 'vue';

// Tiny singleton Vue store for the panel web app.
// This intentionally avoids adding Pinia as a dependency while still keeping
// editor state alive across tab changes and component remounts.
export const designerLayout = ref(null);
export const designerTags = ref([]);
export const designerSelected = ref(-1);
export const designerActivePageId = ref('main');
export const designerLive = ref(false);
export const designerMessage = ref('Loading from panel...');
export const designerRaw = ref('');
export const designerDirty = ref(false);
export const designerRuntimeDirty = ref(false);
export const designerSourceLabel = ref('device flash');
export const designerZoom = ref(1);
export const designerHistory = ref([{ label: 'Open', icon: '▱', kind: 'open', at: 'now' }]);
export const designerLoaded = ref(false);

export const appActiveTab = ref('designer');

export const scriptStatus = ref({ status: 'loading' });
export const scriptConsoleText = ref('');
export const scriptCallName = ref('ToggleMotor_Click');
export const scriptSourceLabel = ref('device flash');
export const scriptSource = ref('');
export const scriptLoaded = ref(false);
export const scriptDirty = ref(false);
export const scriptRuntimeDirty = ref(false);
export const scriptExternalRevision = ref(0);
export const scriptJumpRequest = ref(null);

export const tagsLoaded = ref(false);
export const tagsDirty = ref(false);
export const tagsSourceLabel = ref('device runtime');
export const projectMessage = ref('');

export const mqttConfigExternalRevision = ref(0);

export const mqttConfig = ref(null);
export const mqttLoaded = ref(false);
export const mqttDirty = ref(false);
export const mqttSourceLabel = ref('device flash');
