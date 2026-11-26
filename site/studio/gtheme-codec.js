import { decodeArt, encodeArt, psnr, rgb565 } from './art-codec.js';

export const ROLES = Object.freeze([
  'background', 'text', 'text_secondary', 'text_tertiary', 'selection_background', 'selection_top',
  'selection_shine', 'selection_text', 'header_background', 'header_top', 'header_text', 'divider',
  'scrollbar_thumb', 'danger', 'success', 'chevron', 'accent', 'accent_alt', 'backdrop_top', 'backdrop_bottom',
]);
export const BASES = ['graphite', 'neon', 'sunset', 'glass', 'classic'];
export const ORIENTATIONS = ['landscape', 'portrait', 'any'];
export const TEXT_SIZES = ['small', 'medium', 'large', 'any'];
export const MAX_BYTES = 2_097_152;
export const METADATA = [
  ['name', 36, 32], ['author', 68, 32], ['version', 100, 32], ['licence', 132, 64],
  ['homepage', 196, 128, true], ['gallery', 324, 128, true],
];

// IDs are append-only in firmware/src/ui/include/ui/icons.h.
const iconNames = ('music audiobook podcast now_playing mixtape audio settings cover_flow artist album_artist album ' +
  'genre folder song playlist favorite shuffle repeat display theme power sleep timeout controls start rescan ' +
  'diagnostics gain jack usb bluetooth key_plus key_minus key_play key_menu disc hub search computer ' +
  'play pause next previous playing queue speaker album_small shuffle_small bolt bluetooth_small lock ' +
  'song_small plug chevron logo info tag hide delete globe').split(' ');
const reserved = new Set(('shuffle display theme power sleep controls start rescan bluetooth key_plus key_minus ' +
  'key_play key_menu').split(' '));
export const ICONS = Object.fromEntries(iconNames.map((name, i) => {
  const small = i >= 39 && i <= 54;
  return ['icon_' + name, { id: 257 + i, width: i >= 48 && i <= 53 ? 9 : small ? 14 : 20,
    height: small ? 14 : 20, reserved: reserved.has(name) }];
}));
export const ART_SLOTS = ['home', 'now_playing', 'list', 'splash', 'cassette_label'];
const slots = new Map([
  ...ART_SLOTS.map((name, index) => [name, index + 1]),
  ...Object.entries(ICONS).map(([name, { id }]) => [name, id]),
]);
const slotNames = new Map(Array.from(slots, ([name, id]) => [id, name]));

const span = (start, end) => Array.from({ length: end - start + 1 }, (_, n) => start + n);
export const STYLES = [
  ['scene', 1, 'horizon', 0, span(40, 75), 60],
  ['scene', 1, 'sun_radius', 1, span(15, 35), 25],
  ['scene', 1, 'slats', 2, span(0, 8), 4],
  ['scene', 1, 'floor_lines', 3, span(3, 10), 6],
  ['scene', 1, 'rays', 4, span(0, 16), 8],
  ['scene', 1, 'pulse', 5, [0, 1], 1],
  ['wait_frame_ms', 3, 'time', 0, span(40, 500), 100],
  ['now_playing', 4, 'align', 0, [0, 1], 0],
  ['now_playing', 4, 'progress_height', 1, [4, 6, 8], 4],
].map(([kind, kindId, target, targetId, values, initial]) => ({ kind, kindId, target, targetId, values, initial }));

const headerSize = 512;
const descriptorSize = 24;
const signatureSize = 88;
const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true, ignoreBOM: true });
const retainedAssets = new WeakMap();
const retainedSignatures = new WeakMap();
const dataView = (bytes) => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
const same = (a, b) => a.length === b.length && a.every((value, index) => value === b[index]);

const crcTable = Uint32Array.from({ length: 256 }, (_, n) => {
  for (let bit = 0; bit < 8; bit++) n = (n >>> 1) ^ ((n & 1) ? 0xEDB88320 : 0);
  return n;
});

export function crc32(bytes, skipFrom = bytes.length, skipTo = bytes.length, end = bytes.length) {
  let crc = 0xffffffff;
  for (let i = 0; i < end; i++) {
    if (i < skipFrom || i >= skipTo) crc = (crc >>> 8) ^ crcTable[(crc ^ bytes[i]) & 255];
  }
  return (crc ^ 0xffffffff) >>> 0;
}

export const byteLength = (text) => encoder.encode(text).length;

function fieldBytes(name, size, optional, value) {
  if (typeof value !== 'string' || (!value && !optional)) throw new Error(`${name} is required`);
  const bytes = encoder.encode(value);
  if (bytes.length >= size || /[\x00-\x1f\x7f]/.test(value) || decoder.decode(bytes) !== value) {
    throw new Error(`${name} must fit ${size - 1} UTF-8 bytes without control characters`);
  }
  if (optional && value && !(value.startsWith('https://') && value.length > 8)) {
    throw new Error(`${name} must be empty or start with https:// and name a host`);
  }
  if (name === 'version' && (!/^(0|[1-9][0-9]{0,4})(\.(0|[1-9][0-9]{0,4})){2}$/.test(value) ||
    value.split('.').some((part) => Number(part) > 65535))) {
    throw new Error('Version must be major.minor.patch, each 0 to 65535');
  }
  return bytes;
}

export function validateMetadata(theme) {
  return METADATA.map(([name, at, size, optional]) => [at, fieldBytes(name, size, optional, theme[name] ?? '')]);
}

export function styleValues(entries) {
  const values = new Map();
  for (const { kind, target, value } of entries ?? []) {
    const item = STYLES.find((style) => style.kind === kind && style.target === target);
    if (!item || !item.values.includes(value) || values.has(item)) throw new Error('Unknown, repeated or out-of-range style');
    values.set(item, value);
  }
  return values;
}

export function readPalette(palette) {
  if (Array.isArray(palette) || palette instanceof Uint16Array) {
    if (palette.length !== 20 || palette.some((n) => !Number.isInteger(n) || n < 0 || n > 65535)) {
      throw new Error('Supply all 20 RGB565 colors');
    }
    return Uint16Array.from(palette);
  }
  return Uint16Array.from(ROLES, (name) => {
    const hex = palette?.[name];
    if (typeof hex !== 'string' || !/^#[a-f0-9]{6}$/i.test(hex)) throw new Error(`Supply a #RRGGBB color for ${name}`);
    return rgb565(...hex.slice(1).match(/../g).map((component) => parseInt(component, 16)));
  });
}

function uuidBytes(id) {
  if (typeof id !== 'string' || !/^[\da-f]{8}(-[\da-f]{4}){3}-[\da-f]{12}$/i.test(id)) throw new Error('Invalid pack UUID');
  const bytes = Uint8Array.from(id.replaceAll('-', '').match(/../g), (part) => parseInt(part, 16));
  if (!bytes.some(Boolean)) throw new Error('Pack UUID must be nonzero');
  return bytes;
}

export function newThemeId() {
  const bytes = crypto.getRandomValues(new Uint8Array(16));
  bytes[6] = (bytes[6] & 15) | 64;
  bytes[8] = (bytes[8] & 63) | 128;
  return uuidText(bytes);
}

function uuidText(bytes) {
  const text = Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('');
  if (!bytes.some(Boolean)) throw new Error('Pack UUID must be nonzero');
  return [text.slice(0, 8), text.slice(8, 12), text.slice(12, 16), text.slice(16, 20), text.slice(20)].join('-');
}

export function assetKey({ slot, orientation, text_size: size }) {
  return [slot, orientation, size].join('/');
}

function assetLayout(asset) {
  const { slot, orientation, text_size: size, tile, codec, width, height } = asset;
  const id = slots.get(slot);
  const o = ORIENTATIONS.indexOf(orientation);
  const t = TEXT_SIZES.indexOf(size);
  const icon = Object.hasOwn(ICONS, slot) ? ICONS[slot] : null;
  if (!id || o < 0 || t < 0 || !['indexed', 'lossless'].includes(codec)) throw new Error('Unknown asset slot, variant or encoding');
  if (slot === 'now_playing' && codec === 'lossless') {
    throw new Error('Now Playing requires indexed art to keep CPU load down during playback.');
  }
  let valid;
  if (tile) valid = slot === 'list' && width >= 1 && height >= 1 && width <= 64 && height <= 64;
  else if (icon) valid = width === icon.width && height === icon.height;
  else if (slot === 'cassette_label') valid = width === 160 && height === 48;
  else valid = (o === 0 && width === 480 && height === 222) || (o === 1 && width === 222 && height === 480);
  if (!valid || !Number.isInteger(width) || !Number.isInteger(height) || (!icon && t !== 3)) {
    throw new Error(`Wrong dimensions or variant for ${slot}: ${width}×${height}`);
  }
  return { id, orientation: o, size: t === 3 ? 255 : t, icon };
}

function encodeAsset(asset) {
  const layout = assetLayout(asset);
  if (!(asset.pixels instanceof Uint16Array) || !(asset.alpha instanceof Uint8Array) ||
    asset.pixels.length !== asset.width * asset.height || asset.alpha.length !== asset.pixels.length ||
    asset.alpha.some((alpha) => alpha > 15)) throw new Error('Invalid RGB565 pixels or 4-bit alpha');
  if (!layout.icon && asset.alpha.some((alpha) => alpha !== 15)) throw new Error(`${asset.slot} must be opaque`);
  const saved = retainedAssets.get(asset);
  const keep = saved && saved.key === assetKey(asset) && saved.width === asset.width && saved.height === asset.height &&
    saved.codec === asset.codec && same(saved.pixels, asset.pixels) && same(saved.alpha, asset.alpha);
  if (keep) return { ...layout, payload: saved.payload, image: saved.image, psnr: saved.psnr };
  const payload = encodeArt(asset);
  const image = decodeArt(payload, asset, payload.subarray(0, 4));
  const quality = psnr(asset.pixels, image.pixels, asset.alpha, image.alpha);
  retainedAssets.set(asset, { key: assetKey(asset), width: asset.width, height: asset.height, codec: asset.codec,
    pixels: asset.pixels.slice(), alpha: asset.alpha.slice(), payload, image, psnr: quality });
  return { ...layout, payload, image, psnr: quality };
}

export function encodeTheme(theme) {
  const base = BASES.indexOf(theme.base);
  if (base < 0) throw new Error('Invalid base theme');
  const metadata = validateMetadata(theme);
  const palette = readPalette(theme.palette);
  const id = uuidBytes(theme.id);
  const style = styleValues(theme.style);
  const assets = theme.assets ?? [];
  if (assets.length > 64) throw new Error('A pack can hold at most 64 asset variants');
  const keys = new Set();
  const encoded = assets.map((asset) => {
    const key = assetKey(asset);
    if (keys.has(key)) throw new Error('Duplicate asset variant');
    keys.add(key);
    return encodeAsset(asset);
  });
  const styleAt = headerSize + assets.length * descriptorSize;
  let payloadAt = styleAt + style.size * 4;
  const length = payloadAt + encoded.reduce((sum, item) => sum + item.payload.length, 0);
  if (length > MAX_BYTES) throw new Error(`Pack is ${length.toLocaleString()} B. Limit is 2,097,152 B.`);
  const bytes = new Uint8Array(length);
  const view = dataView(bytes);
  bytes.set([68, 83, 84, 80]);
  view.setUint16(4, 2, true);
  view.setUint16(6, headerSize, true);
  view.setUint32(8, length, true);
  bytes.set([base, Number(Boolean(theme.horizon)) | Number(Boolean(theme.header_rule)) << 1, 20, assets.length], 16);
  bytes.set(id, 20);
  metadata.forEach(([at, field]) => bytes.set(field, at));
  palette.forEach((color, i) => view.setUint16(452 + i * 2, color, true));
  view.setUint16(494, style.size * 4, true);
  let cursor = styleAt;
  for (const [{ kindId, targetId }, value] of style) {
    bytes.set([kindId, targetId], cursor);
    view.setUint16(cursor + 2, value, true);
    cursor += 4;
  }
  const reports = encoded.map(({ id: slot, orientation, size, payload, image, psnr }, index) => {
    const asset = assets[index];
    const at = headerSize + index * descriptorSize;
    view.setUint16(at, slot, true);
    bytes.set([orientation, size], at + 2);
    view.setUint16(at + 4, asset.width, true);
    view.setUint16(at + 6, asset.height, true);
    view.setUint32(at + 8, payloadAt, true);
    view.setUint32(at + 12, payload.length, true);
    bytes.set([payload[0], payload[1], payload[2], Number(Boolean(asset.tile)), 0, 0, 0, 1], at + 16);
    bytes.set(payload, payloadAt);
    payloadAt += payload.length;
    return { key: assetKey(asset), bytes: payload.length, psnr, image };
  });
  view.setUint32(12, crc32(bytes, 12, 16), true);
  const signed = retainedSignatures.get(theme);
  if (signed && signed.length === length + signatureSize) {
    const candidate = bytes.slice();
    const view = dataView(candidate);
    view.setUint32(8, signed.length, true);
    view.setUint16(492, signatureSize, true);
    view.setUint32(12, crc32(candidate, 12, 16), true);
    if (same(candidate, signed.subarray(0, length))) return { bytes: signed.slice(), reports };
  }
  return { bytes, reports };
}

export function decodeTheme(input) {
  const bytes = input instanceof Uint8Array
    ? new Uint8Array(input.buffer, input.byteOffset, input.byteLength) : new Uint8Array(input);
  if (bytes.length < headerSize || bytes.length > MAX_BYTES) throw new Error('Pack size is outside 512 bytes to 2 MiB');
  const view = dataView(bytes);
  const signature = view.getUint16(492, true);
  const styleLength = view.getUint16(494, true);
  const count = bytes[19];
  const end = bytes.length - signature;
  const styleAt = headerSize + count * descriptorSize;
  let next = styleAt + styleLength;
  if (!same(bytes.subarray(0, 4), [68, 83, 84, 80]) || view.getUint16(4, true) !== 2 ||
    view.getUint16(6, true) !== headerSize || view.getUint32(8, true) !== bytes.length ||
    bytes[16] >= BASES.length || bytes[17] > 3 || bytes[18] !== 20 || count > 64 ||
    (signature !== 0 && signature !== signatureSize) || styleLength > 256 || styleLength % 4 ||
    bytes.subarray(496, 512).some(Boolean) || next > end) throw new Error('Invalid v2 pack header');
  if (view.getUint32(12, true) !== crc32(bytes, 12, 16, end)) throw new Error('Pack CRC mismatch');
  if (signature && !same(bytes.subarray(end, end + 8), [68, 83, 83, 71, 1, 0, 0, 0])) {
    throw new Error('Invalid signature envelope');
  }
  const theme = {
    id: uuidText(bytes.subarray(20, 36)), base: BASES[bytes[16]], horizon: Boolean(bytes[17] & 1),
    header_rule: Boolean(bytes[17] & 2), palette: Uint16Array.from({ length: 20 }, (_, i) => view.getUint16(452 + i * 2, true)),
    style: [], assets: [],
  };
  for (const [name, at, size, optional] of METADATA) {
    const field = bytes.subarray(at, at + size);
    const zero = field.indexOf(0);
    if (zero < 0 || field.subarray(zero).some(Boolean)) throw new Error('Invalid metadata padding');
    const value = decoder.decode(field.subarray(0, zero));
    fieldBytes(name, size, optional, value);
    theme[name] = value;
  }
  for (let at = styleAt; at < next; at += 4) {
    const item = STYLES.find(({ kindId, targetId }) => kindId === bytes[at] && targetId === bytes[at + 1]);
    if (!item) throw new Error('Unknown style entry');
    theme.style.push({ kind: item.kind, target: item.target, value: view.getUint16(at + 2, true) });
  }
  styleValues(theme.style);
  const seen = new Set();
  for (let index = 0; index < count; index++) {
    const at = headerSize + index * descriptorSize;
    const slot = slotNames.get(view.getUint16(at, true));
    const size = bytes[at + 3];
    const format = bytes[at + 16];
    const palette = bytes[at + 17];
    const alphaCount = bytes[at + 18];
    const flag = bytes[at + 19];
    const asset = {
      slot, orientation: ORIENTATIONS[bytes[at + 2]], text_size: size === 255 ? 'any' : TEXT_SIZES.slice(0, 3)[size],
      width: view.getUint16(at + 4, true), height: view.getUint16(at + 6, true),
      codec: [null, 'indexed', 'lossless'][format], tile: Boolean(flag),
    };
    const layout = assetLayout(asset);
    const key = assetKey(asset);
    const offset = view.getUint32(at + 8, true);
    const length = view.getUint32(at + 12, true);
    if (seen.has(key) || offset !== next || length > end - next || flag > 1 ||
      bytes.subarray(at + 20, at + 23).some(Boolean) || bytes[at + 23] !== 1 ||
      (format === 2 && palette) || alphaCount > (layout.icon ? (format === 1 ? palette + 1 : 1) : 0)) {
      throw new Error('Invalid, duplicate or non-contiguous asset descriptor');
    }
    const payload = bytes.slice(offset, next + length);
    Object.assign(asset, decodeArt(payload, asset, [format, palette, alphaCount, 1]));
    retainedAssets.set(asset, { key, width: asset.width, height: asset.height, codec: asset.codec,
      pixels: asset.pixels.slice(), alpha: asset.alpha.slice(), payload,
      image: { pixels: asset.pixels.slice(), alpha: asset.alpha.slice() }, psnr: Infinity });
    theme.assets.push(asset);
    seen.add(key);
    next += length;
  }
  if (next !== end) throw new Error('Unreferenced payload bytes');
  if (signature) retainedSignatures.set(theme, bytes.slice());
  return theme;
}
