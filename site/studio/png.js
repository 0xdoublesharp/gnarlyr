// PNG pixels are read and written without Canvas premultiplication, which
// would otherwise round the straight RGB channels of partially opaque icons.
import { rgb565, rgb888 } from './art-codec.js';
import { crc32 } from './gtheme-codec.js';

const signature = Uint8Array.of(137, 80, 78, 71, 13, 10, 26, 10);
const adam7 = [[0, 0, 8, 8], [4, 0, 8, 8], [0, 4, 4, 8], [2, 0, 4, 4],
  [0, 2, 2, 4], [1, 0, 2, 2], [0, 1, 1, 2]];
const types = {
  0: { channels: 1, depths: [1, 2, 4, 8, 16] },
  2: { channels: 3, depths: [8, 16] },
  3: { channels: 1, depths: [1, 2, 4, 8] },
  4: { channels: 2, depths: [8, 16] },
  6: { channels: 4, depths: [8, 16] },
};
const viewBytes = (bytes) => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

function header(bytes) {
  if (bytes.length < 33 || !signature.every((byte, i) => bytes[i] === byte) ||
    String.fromCharCode(...bytes.subarray(12, 16)) !== 'IHDR') throw new Error('Choose a PNG image.');
  const view = viewBytes(bytes);
  const depth = bytes[24];
  const type = bytes[25];
  const interlaced = bytes[28] === 1;
  if (view.getUint32(8) !== 13 || !types[type]?.depths.includes(depth) ||
    bytes[26] || bytes[27] || bytes[28] > 1) throw new Error('Invalid PNG header.');
  return { width: view.getUint32(16), height: view.getUint32(20), type, depth, interlaced, channels: types[type].channels };
}

function chunks(bytes, type) {
  const view = viewBytes(bytes);
  const data = [];
  let palette;
  let transparency;
  let ended = false;
  let dataEnded = false;
  for (let at = 8; at + 12 <= bytes.length;) {
    const size = view.getUint32(at);
    const next = at + 12 + size;
    if (next > bytes.length || crc32(bytes.subarray(at + 4, next - 4)) !== view.getUint32(next - 4)) {
      throw new Error('PNG chunk failed its checksum or length check.');
    }
    const name = String.fromCharCode(...bytes.subarray(at + 4, at + 8));
    const content = bytes.subarray(at + 8, next - 4);
    if (name === 'IDAT') {
      if (dataEnded) throw new Error('PNG image data must be contiguous.');
      data.push(content);
    } else {
      dataEnded ||= data.length > 0;
      if (name === 'PLTE') {
        if (dataEnded || palette || !size || size > 768 || size % 3) throw new Error('Invalid PNG palette.');
        palette = content;
      } else if (name === 'tRNS') {
        if (dataEnded || transparency || !size) throw new Error('Invalid PNG transparency table.');
        transparency = content;
      } else if (name === 'IEND') {
        if (size || next !== bytes.length) throw new Error('Unexpected bytes after PNG image.');
        ended = true;
        break;
      } else if (name[0] === name[0].toUpperCase() && (name !== 'IHDR' || at !== 8)) {
        throw new Error('Unsupported critical PNG chunk.');
      }
    }
    at = next;
  }
  if (!data.length || !ended || (type === 3 && !palette)) throw new Error('Incomplete PNG image.');
  if (transparency) {
    const size = transparency.length;
    if ((type === 0 && size !== 2) || (type === 2 && size !== 6) ||
      (type === 3 && size > palette.length / 3) || type === 4 || type === 6) {
      throw new Error('Invalid PNG transparency table.');
    }
  }
  return { data, palette, transparency };
}

function passLayout({ width, height, interlaced, depth, channels }) {
  return (interlaced ? adam7 : [[0, 0, 1, 1]]).map(([x, y, dx, dy]) => {
    const w = Math.ceil((width - x) / dx);
    const h = Math.ceil((height - y) / dy);
    return { x, y, dx, dy, w, h, stride: Math.ceil(w * depth * channels / 8) };
  }).filter(({ w, h }) => w > 0 && h > 0);
}

async function inflate(chunks, expected) {
  const stream = new Blob(chunks).stream().pipeThrough(new DecompressionStream('deflate'));
  const reader = stream.getReader();
  const output = new Uint8Array(expected);
  let at = 0;
  while (true) {
    const { value, done } = await reader.read();
    if (done) break;
    if (value.length > expected - at) {
      await reader.cancel();
      throw new Error('PNG has more data than its dimensions allow.');
    }
    output.set(value, at);
    at += value.length;
  }
  if (at !== expected) throw new Error('PNG pixel data is truncated.');
  return output;
}

function prediction(filter, left, above, diagonal) {
  if (filter === 0) return 0;
  if (filter === 1) return left;
  if (filter === 2) return above;
  if (filter === 3) return (left + above) >> 1;
  const p = left + above - diagonal;
  const a = Math.abs(p - left);
  const b = Math.abs(p - above);
  const c = Math.abs(p - diagonal);
  if (a <= b && a <= c) return left;
  return b <= c ? above : diagonal;
}

function sampleReader(row, depth) {
  if (depth === 8) return (index) => row[index];
  if (depth === 16) return (index) => row[index * 2] * 256 + row[index * 2 + 1];
  const mask = (1 << depth) - 1;
  return (index) => (row[(index * depth) >> 3] >> (8 - depth - (index * depth & 7))) & mask;
}

function readRow(row, { w, x, y, dx, dy }, rowNumber, info, sources, pixels, alpha, opaque) {
  const { type, depth, channels, width } = info;
  const { transparency, palette } = sources;
  const sample = sampleReader(row, depth);
  const toByte = depth === 16 ? (n) => n >>> 8 : depth < 8 ? (n) => n * 255 / ((1 << depth) - 1) : (n) => n;
  const transparent = transparency && type !== 3 ?
    Array.from({ length: transparency.length / 2 }, (_, i) => viewBytes(transparency).getUint16(i * 2)) : [];
  for (let column = 0; column < w; column++) {
    const i = column * channels;
    let r, g, b;
    let a = 255;
    if (type === 3) {
      const index = sample(i);
      if (index * 3 >= palette.length) throw new Error('PNG palette index is out of range.');
      [r, g, b] = palette.subarray(index * 3, index * 3 + 3);
      a = transparency?.[index] ?? 255;
    } else if (type === 0 || type === 4) {
      const value = sample(i);
      r = g = b = type === 0 && depth === 16 ? Math.min(value, 255) : toByte(value);
      if (type === 4) a = toByte(sample(i + 1));
      else if (value === transparent[0]) a = 0;
    } else {
      const red = sample(i);
      const green = sample(i + 1);
      const blue = sample(i + 2);
      r = toByte(red); g = toByte(green); b = toByte(blue);
      if (type === 6) a = toByte(sample(i + 3));
      else if (red === transparent[0] && green === transparent[1] && blue === transparent[2]) a = 0;
    }
    if (opaque && a !== 255) throw new Error('Composite this PNG onto a solid background before importing.');
    const at = (y + rowNumber * dy) * width + x + column * dx;
    alpha[at] = Math.floor((a * 15 + 127) / 255);
    if (alpha[at]) pixels[at] = rgb565(r, g, b);
  }
}

export async function decodePng(file, dimensions, opaque) {
  if (!file.size || file.size > 8 * 1024 * 1024) throw new Error('Choose a PNG under 8 MiB.');
  const bytes = new Uint8Array(await file.arrayBuffer());
  const info = header(bytes);
  const { type, width, height, depth, channels } = info;
  if (!width || !height || width > 480 || height > 480 || !dimensions(width, height)) {
    throw new Error(`PNG dimensions ${width} × ${height} do not fit this slot.`);
  }
  const parts = chunks(bytes, type);
  const passes = passLayout(info);
  const data = await inflate(parts.data, passes.reduce((sum, { h, stride }) => sum + h * (stride + 1), 0));
  const pixels = new Uint16Array(width * height);
  const alpha = new Uint8Array(pixels.length);
  const step = Math.max(1, Math.ceil(depth * channels / 8));
  let at = 0;
  for (const pass of passes) {
    const { h, stride } = pass;
    let previous = new Uint8Array(stride);
    for (let y = 0; y < h; y++) {
      const filter = data[at++];
      if (filter > 4) throw new Error('Invalid PNG filter.');
      const row = data.subarray(at, at + stride);
      for (let k = 0; k < stride; k++) {
        row[k] += prediction(filter, row[k - step] ?? 0, previous[k], previous[k - step] ?? 0);
      }
      readRow(row, pass, y, info, parts, pixels, alpha, opaque);
      previous = row;
      at += stride;
    }
  }
  return { width, height, pixels, alpha };
}

function makeChunk(name, content) {
  const bytes = new Uint8Array(12 + content.length);
  const view = viewBytes(bytes);
  view.setUint32(0, content.length);
  bytes.set(new TextEncoder().encode(name), 4);
  bytes.set(content, 8);
  view.setUint32(bytes.length - 4, crc32(bytes.subarray(4, bytes.length - 4)));
  return bytes;
}

export async function encodePng(asset) {
  const { width, height, pixels, alpha } = asset;
  const info = new Uint8Array(13);
  viewBytes(info).setUint32(0, width);
  viewBytes(info).setUint32(4, height);
  info.set([8, 6], 8);
  const rowLength = 1 + width * 4;
  const raw = new Uint8Array(height * rowLength);
  for (let y = 0; y < height; y++) {
    const left = [0, 0, 0, 0];
    const row = y * rowLength;
    raw[row] = 1;
    for (let x = 0; x < width; x++) {
      const i = y * width + x;
      const rgba = [...rgb888(pixels[i]), alpha[i] * 17];
      for (let channel = 0; channel < 4; channel++) {
        raw[row + 1 + x * 4 + channel] = rgba[channel] - left[channel];
        left[channel] = rgba[channel];
      }
    }
  }
  const stream = new Blob([raw]).stream().pipeThrough(new CompressionStream('deflate'));
  const compressed = new Uint8Array(await new Response(stream).arrayBuffer());
  return new Blob([signature, makeChunk('IHDR', info), makeChunk('IDAT', compressed),
    makeChunk('IEND', new Uint8Array())], { type: 'image/png' });
}
