import { hexColor } from './art-codec.js';
import { encodePng } from './png.js';
import { crc32, ROLES } from './gtheme-codec.js';

function surface(width, height) {
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  return canvas;
}

export async function photoPixels(file) {
  if (!file.size || file.size > 32 * 1024 * 1024) throw new Error('Choose an image under 32 MiB.');
  const bitmap = await createImageBitmap(file, { resizeWidth: 64, resizeHeight: 64, resizeQuality: 'high' });
  try {
    const context = surface(64, 64).getContext('2d', { willReadFrequently: true });
    context.drawImage(bitmap, 0, 0);
    return context.getImageData(0, 0, 64, 64).data;
  } finally {
    bitmap.close();
  }
}

export function pngBlob(canvas) {
  return new Promise((resolve, reject) => canvas.toBlob((blob) =>
    blob ? resolve(blob) : reject(new Error('PNG export failed.')), 'image/png'));
}

export function makeZip(files) {
  // ZIP store mode. The browser already compressed the PNGs.
  const localFiles = [];
  const directory = [];
  const encoder = new TextEncoder();
  let offset = 0;
  let directoryLength = 0;
  for (const [filename, data] of files) {
    const name = encoder.encode(filename);
    const checksum = crc32(data);
    const local = new Uint8Array(30 + name.length);
    const entry = new Uint8Array(46 + name.length);
    const view = new DataView(local.buffer);
    const central = new DataView(entry.buffer);
    view.setUint32(0, 0x04034b50, true);
    central.setUint32(0, 0x02014b50, true);
    central.setUint16(4, 20, true);
    for (const [target, shift] of [[view, 0], [central, 2]]) {
      target.setUint16(4 + shift, 20, true);
      target.setUint16(6 + shift, 0x800, true);
      target.setUint16(12 + shift, 0x21, true);
      target.setUint32(14 + shift, checksum, true);
      target.setUint32(18 + shift, data.length, true);
      target.setUint32(22 + shift, data.length, true);
      target.setUint16(26 + shift, name.length, true);
    }
    central.setUint32(42, offset, true);
    local.set(name, 30);
    entry.set(name, 46);
    localFiles.push(local, data);
    directory.push(entry);
    offset += local.length + data.length;
    directoryLength += entry.length;
  }
  const end = new Uint8Array(22);
  const view = new DataView(end.buffer);
  view.setUint32(0, 0x06054b50, true);
  view.setUint16(8, files.length, true);
  view.setUint16(10, files.length, true);
  view.setUint32(12, directoryLength, true);
  view.setUint32(16, offset, true);
  return new Blob([...localFiles, ...directory, end], { type: 'application/zip' });
}

export async function sourceZip(theme, folder) {
  const manifest = { ...theme, palette: Object.fromEntries(ROLES.map((role, i) => [role, hexColor(theme.palette[i])])), assets: [] };
  delete manifest.creator;
  delete manifest.remixedFrom;
  const files = [];
  for (const { slot, orientation, text_size, codec, ...image } of theme.assets) {
    const file = [slot, orientation, text_size].join('-') + '.png';
    const png = await encodePng(image);
    manifest.assets.push({ slot, file, orientation, text_size, codec });
    files.push([folder + '/' + file, new Uint8Array(await png.arrayBuffer())]);
  }
  const json = new TextEncoder().encode(JSON.stringify(manifest, null, 2) + '\n');
  return makeZip([[folder + '/theme.json', json], ...files]);
}

export function download(blob, filename) {
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = filename;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 30_000);
}

export async function showcaseCard(theme, preview, orientation) {
  const canvas = surface(1200, 630);
  const context = canvas.getContext('2d');
  const gradient = context.createLinearGradient(0, 0, 1200, 630);
  gradient.addColorStop(0, '#171627');
  gradient.addColorStop(1, '#292837');
  context.fillStyle = gradient;
  context.fillRect(0, 0, 1200, 630);

  context.fillStyle = hexColor(theme.palette[16]);
  context.fillRect(72, 72, 64, 5);
  context.fillStyle = '#fff8ff';
  const line = (text, limit) => {
    const points = [...text];
    const original = points.length;
    while (points.length && context.measureText(points.join('') + (points.length < original ? '…' : '')).width > limit) points.pop();
    return points.join('') + (points.length < original ? '…' : '');
  };
  const name = theme.name || 'Untitled theme';
  let nameSize = 57;
  do { context.font = `800 ${nameSize--}px system-ui`; } while (context.measureText(name).width > 433 && nameSize >= 42);
  if (context.measureText(name).width <= 433) {
    context.fillText(name, 72, 222);
  } else {
    let split = name.length - 1;
    while (split > 1 && context.measureText(name.slice(0, split)).width > 433) split--;
    const boundary = name.lastIndexOf(' ', split);
    if (boundary >= split / 2) split = boundary;
    context.fillText(line(name.slice(0, split).trim(), 433), 72, 171);
    context.fillText(line(name.slice(split).trim(), 433), 72, 222);
  }
  context.fillStyle = '#d5cce7';
  context.font = '500 26px system-ui';
  context.fillText(line(theme.creator ? '@' + theme.creator : theme.author, 420), 72, 275);
  context.fillStyle = '#fff8ff';
  context.font = '650 22px system-ui';
  context.fillText('gnarlyr.com/studio', 72, 545);

  const height = orientation === 'portrait' ? 456 : 281;
  const width = Math.floor(height * preview.width / preview.height);
  const x = 855 - Math.floor(width / 2);
  const y = 294 - Math.floor(height / 2);
  context.fillStyle = '#08090d';
  context.beginPath();
  context.roundRect(x - 19, y - 17, width + 38, height + 52, 20);
  context.fill();
  context.strokeStyle = '#62636e';
  context.lineWidth = 2;
  context.stroke();
  context.imageSmoothingEnabled = false;
  context.drawImage(preview, x, y, width, height);
  context.fillStyle = '#e2e1ee';
  context.font = '600 12px system-ui';
  context.textAlign = 'right';
  context.fillText('SNOWSKY ECHO', x + width, y + height + 22);
  context.textAlign = 'left';
  [0, 4, 16, 17, 1].forEach((role, i) => {
    context.fillStyle = hexColor(theme.palette[role]);
    context.fillRect(72 + i * 52, 312, 47, 13);
  });
  return pngBlob(canvas);
}
