// Minimal browser-side ZIP import/export for PiLab Panel projects.
// This intentionally avoids adding a dependency. Exported zips use ZIP
// method 0 (store/no compression), and import supports the same format.

const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();

let crcTable = null;
function getCrcTable() {
  if (crcTable) return crcTable;
  crcTable = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
    crcTable[n] = c >>> 0;
  }
  return crcTable;
}

function crc32(bytes) {
  const table = getCrcTable();
  let c = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) c = table[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function dosTimeDate(date = new Date()) {
  const year = Math.max(1980, date.getFullYear());
  const time = (date.getHours() << 11) | (date.getMinutes() << 5) | Math.floor(date.getSeconds() / 2);
  const day = (year - 1980) << 9 | ((date.getMonth() + 1) << 5) | date.getDate();
  return { time, day };
}

function writeU16(view, offset, value) { view.setUint16(offset, value & 0xffff, true); }
function writeU32(view, offset, value) { view.setUint32(offset, value >>> 0, true); }
function readU16(view, offset) { return view.getUint16(offset, true); }
function readU32(view, offset) { return view.getUint32(offset, true); }

function concatParts(parts) {
  const size = parts.reduce((sum, p) => sum + p.length, 0);
  const out = new Uint8Array(size);
  let pos = 0;
  for (const p of parts) { out.set(p, pos); pos += p.length; }
  return out;
}

export function makeProjectFileName(now = new Date()) {
  const pad = n => String(n).padStart(2, '0');
  return `PiPanel_${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}_${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}.zip`;
}

export function buildManifest({ layout, script, tags, mqtt } = {}) {
  const now = new Date();
  return {
    format: 'PiLabPanelProject',
    version: 1,
    created: now.toISOString(),
    app: 'PiLab Panel',
    screenFile: 'screen.json',
    scriptFile: 'script.as',
    tagsFile: 'tags.json',
    mqttFile: 'mqtt_config.json',
    screen: {
      width: Number(layout?.screen?.width ?? 800),
      height: Number(layout?.screen?.height ?? 480),
      widgetCount: Array.isArray(layout?.pages) ? layout.pages.reduce((sum, p) => sum + (Array.isArray(p?.widgets) ? p.widgets.length : 0), 0) : (Array.isArray(layout?.widgets) ? layout.widgets.length : 0),
      pageCount: Array.isArray(layout?.pages) ? layout.pages.length : 1,
    },
    script: {
      bytes: textEncoder.encode(script || '').length,
    },
    tags: {
      count: Array.isArray(tags) ? tags.length : 0,
    },
    mqtt: {
      included: !!mqtt,
      enabled: !!mqtt?.enabled,
      activeServer: Number(mqtt?.activeServer ?? 0),
      brokerCount: Array.isArray(mqtt?.servers) ? mqtt.servers.length : 0,
      publishCount: Array.isArray(mqtt?.publish) ? mqtt.publish.length : 0,
      subscribeCount: Array.isArray(mqtt?.subscribe) ? mqtt.subscribe.length : 0,
    },
  };
}

export function createZip(files) {
  const localParts = [];
  const centralParts = [];
  let offset = 0;
  const now = new Date();
  const { time, day } = dosTimeDate(now);

  for (const file of files) {
    const nameBytes = textEncoder.encode(file.name);
    const dataBytes = file.data instanceof Uint8Array ? file.data : textEncoder.encode(String(file.data ?? ''));
    const crc = crc32(dataBytes);

    const local = new Uint8Array(30 + nameBytes.length);
    const lv = new DataView(local.buffer);
    writeU32(lv, 0, 0x04034b50);
    writeU16(lv, 4, 20); // version needed
    writeU16(lv, 6, 0);  // flags
    writeU16(lv, 8, 0);  // method: store
    writeU16(lv, 10, time);
    writeU16(lv, 12, day);
    writeU32(lv, 14, crc);
    writeU32(lv, 18, dataBytes.length);
    writeU32(lv, 22, dataBytes.length);
    writeU16(lv, 26, nameBytes.length);
    writeU16(lv, 28, 0);
    local.set(nameBytes, 30);
    localParts.push(local, dataBytes);

    const central = new Uint8Array(46 + nameBytes.length);
    const cv = new DataView(central.buffer);
    writeU32(cv, 0, 0x02014b50);
    writeU16(cv, 4, 20); // version made by
    writeU16(cv, 6, 20); // version needed
    writeU16(cv, 8, 0);
    writeU16(cv, 10, 0);
    writeU16(cv, 12, time);
    writeU16(cv, 14, day);
    writeU32(cv, 16, crc);
    writeU32(cv, 20, dataBytes.length);
    writeU32(cv, 24, dataBytes.length);
    writeU16(cv, 28, nameBytes.length);
    writeU16(cv, 30, 0);
    writeU16(cv, 32, 0);
    writeU16(cv, 34, 0);
    writeU16(cv, 36, 0);
    writeU32(cv, 38, 0);
    writeU32(cv, 42, offset);
    central.set(nameBytes, 46);
    centralParts.push(central);

    offset += local.length + dataBytes.length;
  }

  const centralOffset = offset;
  const central = concatParts(centralParts);
  const end = new Uint8Array(22);
  const ev = new DataView(end.buffer);
  writeU32(ev, 0, 0x06054b50);
  writeU16(ev, 4, 0);
  writeU16(ev, 6, 0);
  writeU16(ev, 8, files.length);
  writeU16(ev, 10, files.length);
  writeU32(ev, 12, central.length);
  writeU32(ev, 16, centralOffset);
  writeU16(ev, 20, 0);

  return new Blob([concatParts(localParts), central, end], { type: 'application/zip' });
}

export function downloadBlob(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.style.display = 'none';
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 2000);
}

export async function readZipFile(file) {
  const buffer = await file.arrayBuffer();
  const view = new DataView(buffer);
  const bytes = new Uint8Array(buffer);
  const entries = {};

  let pos = 0;
  while (pos + 30 <= bytes.length) {
    const sig = readU32(view, pos);
    if (sig === 0x02014b50 || sig === 0x06054b50) break;
    if (sig !== 0x04034b50) throw new Error('Invalid PiPanel zip: missing local file header');
    const flags = readU16(view, pos + 6);
    const method = readU16(view, pos + 8);
    const compressedSize = readU32(view, pos + 18);
    const uncompressedSize = readU32(view, pos + 22);
    const nameLen = readU16(view, pos + 26);
    const extraLen = readU16(view, pos + 28);
    const nameStart = pos + 30;
    const dataStart = nameStart + nameLen + extraLen;
    const name = textDecoder.decode(bytes.slice(nameStart, nameStart + nameLen));

    if (flags & 0x08) throw new Error('Unsupported zip: data descriptors are not supported');
    if (method !== 0) throw new Error(`Unsupported zip compression for ${name}. Please import a PiPanel-exported zip.`);
    if (dataStart + compressedSize > bytes.length) throw new Error('Invalid PiPanel zip: file data is truncated');
    if (compressedSize !== uncompressedSize) throw new Error(`Unsupported compressed entry: ${name}`);

    entries[name] = textDecoder.decode(bytes.slice(dataStart, dataStart + compressedSize));
    pos = dataStart + compressedSize;
  }
  return entries;
}
