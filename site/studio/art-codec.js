// Row packets and RGB565 arithmetic follow firmware/tools/art_codec.py.
export const channels = (color) => [color >>> 11, (color >>> 5) & 63, color & 31];
export const rgb565 = (r, g, b) => (r >>> 3) << 11 | (g >>> 2) << 5 | b >>> 3;
export const rgb888 = (color) => channels(color).map((n, i) => Math.floor(n * 255 / (i === 1 ? 63 : 31)));
export const hexColor = (color) => '#' + rgb888(color).map((n) => n.toString(16).padStart(2, '0')).join('');

export function blend565(under, over, alpha) {
  if (!alpha) return under;
  if (alpha === 15) return over;
  const a = channels(under);
  const b = channels(over);
  const [r, g, blue] = a.map((n, i) => Math.floor((n * (15 - alpha) + b[i] * alpha + 7) / 15));
  return r << 11 | g << 5 | blue;
}

function colorKey(color, alpha) {
  return (alpha ? color : 0) * 16 + alpha;
}

function samples(pixels, alpha) {
  const histogram = new Map();
  for (let i = 0; i < pixels.length; i++) {
    const key = colorKey(pixels[i], alpha[i]);
    histogram.set(key, (histogram.get(key) || 0) + 1);
  }
  return Array.from(histogram, ([key, count]) => {
    const [r, g, b] = channels(key >>> 4);
    return { key, count, channels: [r, g, b, key & 15] };
  });
}

function representative(bucket) {
  const total = bucket.reduce((sum, color) => sum + color.count, 0);
  const [r, g, b, a] = [0, 1, 2, 3].map((axis) => Math.round(
    bucket.reduce((sum, color) => sum + color.channels[axis] * color.count, 0) / total));
  return colorKey(r << 11 | g << 5 | b, a);
}

function range(bucket) {
  return [0, 1, 2].map((axis) => {
    let low = 64;
    let high = 0;
    for (const color of bucket) {
      low = Math.min(low, color.channels[axis]);
      high = Math.max(high, color.channels[axis]);
    }
    return (high - low) * (axis === 1 ? 1 : 2);
  });
}

function medianCut(colors, limit) {
  const buckets = [{ colors, range: range(colors) }];
  while (buckets.length < limit) {
    let best = -1;
    let spread = 0;
    let axis = 0;
    for (const [i, bucket] of buckets.entries()) {
      for (const [candidate, value] of bucket.range.entries()) {
        if (value > spread) {
          best = i;
          spread = value;
          axis = candidate;
        }
      }
    }
    if (best < 0) break;
    const sorted = buckets[best].colors.sort((a, b) => a.channels[axis] - b.channels[axis]);
    let remaining = sorted.reduce((sum, color) => sum + color.count, 0) / 2;
    let split = 0;
    do { remaining -= sorted[split++].count; } while (remaining > 0 && split < sorted.length - 1);
    buckets.splice(best, 1, ...[sorted.slice(0, split), sorted.slice(split)].map((part) =>
      ({ colors: part, range: range(part) })));
  }
  return buckets.map((bucket) => bucket.colors);
}

function octree(colors, limit) {
  // Keep clear, translucent, and opaque colors in separate trees. Quantize
  // translucent RGB and alpha together without introducing alpha into opaque ink.
  const classes = [colors.filter((c) => !c.channels[3]), colors.filter((c) => c.channels[3] % 15),
    colors.filter((c) => c.channels[3] === 15)];
  const leaves = classes.filter((bucket) => bucket.length).map((bucket) => ({ bucket, level: 4 }));
  let split = true;
  while (split && leaves.length < limit) {
    split = false;
    for (let index = 0; index < leaves.length; index++) {
      const { bucket, level } = leaves[index];
      if (bucket.length < 2 || level < -1) continue;
      const branches = new Map();
      for (const color of bucket) {
        const [r, g, b, a] = color.channels;
        const bit = (value, at) => at >= 0 ? (value >> at) & 1 : 0;
        const key = bit(r, level) | bit(g, level + 1) << 1 | bit(b, level) << 2 | bit(a, level - 1) << 3;
        if (!branches.has(key)) branches.set(key, []);
        branches.get(key).push(color);
      }
      if (leaves.length + branches.size - 1 > limit) continue;
      leaves.splice(index, 1, ...Array.from(branches.values(), (part) => ({ bucket: part, level: level - 1 })));
      index += branches.size - 1;
      split = true;
    }
  }
  return leaves.map((leaf) => leaf.bucket);
}

function indexedColors(pixels, alpha) {
  const colors = samples(pixels, alpha);
  const transparent = colors.filter((c) => c.channels[3] < 15).length;
  let buckets;
  if (colors.length <= 256 && transparent <= 255) buckets = colors.map((color) => [color]);
  else if (transparent) buckets = octree(colors, 255);
  else buckets = medianCut(colors, 256);

  const entries = buckets.map((bucket) => ({ bucket, key: representative(bucket) }))
    .sort((a, b) => (a.key & 15) - (b.key & 15) || a.key - b.key);
  const indices = new Map();
  entries.forEach(({ bucket }, index) => bucket.forEach(({ key }) => indices.set(key, index)));
  return {
    palette: entries.map(({ key }) => key >>> 4),
    alphas: entries.filter(({ key }) => (key & 15) < 15).map(({ key }) => key & 15),
    indices: Uint8Array.from(pixels, (color, index) => indices.get(colorKey(color, alpha[index]))),
  };
}

function indexedRow(indices) {
  const out = [];
  let literal = [];
  function flush() {
    if (literal.length) out.push(literal.length - 1, ...literal);
    literal = [];
  }
  for (let x = 0; x < indices.length;) {
    let run = 1;
    while (run < 128 && indices[x + run] === indices[x] && x + run < indices.length) run++;
    if (run >= 3) {
      flush();
      out.push(0x80 | (run - 1), indices[x]);
      x += run;
    } else {
      literal.push(indices[x++]);
      if (literal.length === 128) flush();
    }
  }
  flush();
  return out;
}

function deltaByte(previous, color) {
  const [pr, pg, pb] = channels(previous);
  const [r, g, b] = channels(color);
  const dg = ((g - pg + 32) & 63) - 32;
  const dr = (((r - pr + 16) & 31) - 16) - (dg >> 1);
  const db = (((b - pb + 16) & 31) - 16) - (dg >> 1);
  return dg >= -8 && dg <= 7 && dr >= -2 && dr <= 1 && db >= -2 && db <= 1
    ? ((dg + 8) << 4) | ((dr + 2) << 2) | (db + 2) : -1;
}

function applyDelta(previous, delta) {
  const [r, g, b] = channels(previous);
  const half = (delta >> 5) - 6;
  return ((r + half + ((delta >> 2) & 3)) & 31) << 11 |
    ((g + (delta >> 4) - 8) & 63) << 5 | ((b + half + (delta & 3)) & 31);
}

function losslessRow(pixels, alpha) {
  let previous = 0;
  const kind = new Uint8Array(pixels.length);
  const deltas = new Uint8Array(pixels.length);
  for (let x = 0; x < pixels.length; x++) {
    if (!alpha[x]) {
      kind[x] = 3;
      continue;
    }
    const color = pixels[x];
    const delta = deltaByte(previous, color);
    kind[x] = color === previous ? 0 : delta >= 0 ? 1 : 2;
    deltas[x] = delta;
    previous = color;
  }

  // A short repeat/delta between literal packets can cost more than folding
  // it into one literal. Compare all legal packet lengths on this bounded row.
  const cost = new Uint16Array(pixels.length + 1);
  const choice = new Uint8Array(pixels.length);
  const lengths = new Uint8Array(pixels.length);
  for (let start = pixels.length - 1; start >= 0; start--) {
    let best = Infinity;
    for (const packet of kind[start] === 3 ? [3] : [kind[start], ...(kind[start] === 0 ? [1, 2] : kind[start] === 1 ? [2] : [])]) {
      const overhead = packet === 3 || alpha[start] === 15 ? 1 : 2;
      const unit = packet === 3 ? 0 : packet;
      const limit = Math.min(packet === 3 ? 32 : 64, pixels.length - start);
      for (let count = 1; count <= limit; count++) {
        const x = start + count - 1;
        if (alpha[x] !== alpha[start] || kind[x] > packet) break;
        const total = overhead + unit * count + cost[x + 1];
        if (total <= best) {
          best = total;
          choice[start] = packet;
          lengths[start] = count;
        }
      }
    }
    cost[start] = best;
  }

  const out = [];
  for (let x = 0; x < pixels.length;) {
    const packet = choice[x];
    const count = lengths[x];
    if (alpha[x] && alpha[x] < 15) out.push(0xE0 + alpha[x]);
    out.push((packet << 6) | (count - 1));
    for (let end = x + count; x < end; x++) {
      if (packet === 1) out.push(deltas[x]);
      if (packet === 2) out.push(pixels[x] & 255, pixels[x] >>> 8);
    }
  }
  return out;
}

export function encodeArt(asset) {
  const { width, height, pixels, alpha } = asset;
  const indexed = asset.codec === 'indexed';
  const { palette, alphas, indices } = indexed ? indexedColors(pixels, alpha) : {};
  const entries = indexed ? palette.length - 1 : 0;
  const alphaCount = indexed ? alphas.length : Number(alpha.some((value) => value < 15));
  const table = 4 + (indexed ? palette.length * 2 + Math.ceil(alphaCount / 2) : 0);
  let end = table + (height + 1) * 4;
  const rows = [];
  for (let y = 0; y < height; y++) {
    const start = y * width;
    const row = indexed
      ? indexedRow(indices.subarray(start, start + width))
      : losslessRow(pixels.subarray(start, start + width), alpha.subarray(start, start + width));
    if (row.length > 1024) throw new Error('Lossless row exceeds 1,024 bytes. Try indexed.');
    rows.push(row);
    end += row.length;
  }
  const payload = new Uint8Array(end);
  const view = new DataView(payload.buffer);
  payload.set([indexed ? 1 : 2, entries, alphaCount, 1]);
  if (indexed) {
    palette.forEach((color, i) => view.setUint16(4 + i * 2, color, true));
    alphas.forEach((a, i) => { payload[4 + palette.length * 2 + (i >>> 1)] |= a << ((i & 1) * 4); });
  }
  let offset = table + (height + 1) * 4;
  rows.forEach((row, y) => {
    view.setUint32(table + y * 4, offset, true);
    payload.set(row, offset);
    offset += row.length;
  });
  view.setUint32(table + height * 4, offset, true);
  return payload;
}

export function decodeArt(payload, { width, height }, header) {
  const [code, entries, alphaCount, frames] = header;
  const indexed = code === 1;
  const count = indexed ? entries + 1 : 0;
  const alphaAt = 4 + count * 2;
  const table = alphaAt + (indexed ? Math.ceil(alphaCount / 2) : 0);
  const start = table + 4 * (height + 1);
  if (payload.length < start || !header.every((value, i) => payload[i] === value) || frames !== 1) {
    throw new Error('Payload header differs from its descriptor');
  }
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  const palette = Uint16Array.from({ length: count }, (_, i) => view.getUint16(4 + i * 2, true));
  const alphas = Uint8Array.from({ length: count }, (_, i) => i >= alphaCount ? 15 :
    (payload[alphaAt + (i >>> 1)] >>> ((i & 1) * 4)) & 15);
  if (indexed && (alphas.subarray(0, alphaCount).includes(15) ||
    ((alphaCount & 1) && (payload[table - 1] >>> 4)))) throw new Error('Invalid indexed alpha entries');
  if (view.getUint32(table, true) !== start || view.getUint32(table + height * 4, true) !== payload.length) {
    throw new Error('Invalid row table boundaries');
  }

  const pixels = new Uint16Array(width * height);
  const alpha = new Uint8Array(pixels.length);
  let offset = start;
  for (let y = 0; y < height; y++) {
    const end = view.getUint32(table + (y + 1) * 4, true);
    if (end > payload.length || end <= offset || end - offset > 1024) throw new Error('Invalid row table');
    let x = 0;
    let previous = 0;
    let opacity = 15;
    while (offset < end && x < width) {
      const packet = payload[offset++];
      if (!indexed && packet >= 0xE0) {
        if (!alphaCount || opacity !== 15 || packet === 0xE0 || packet > 0xEE) throw new Error('Invalid alpha prefix');
        opacity = packet - 0xE0;
        continue;
      }
      const kind = packet >>> 6;
      const size = (packet & (indexed ? 127 : kind === 3 ? 31 : 63)) + 1;
      const used = indexed ? (packet & 128 ? 1 : size) : (kind % 3) * size;
      if (size > width - x || offset + used > end) throw new Error('Packet overruns its row');
      for (let run = 0; run < size; run++, x++) {
        const i = y * width + x;
        if (indexed) {
          const index = payload[offset + (used === 1 ? 0 : run)];
          if (index >= count) throw new Error('Palette index out of range');
          pixels[i] = palette[index];
          alpha[i] = alphas[index];
        } else if (kind === 3) {
          if (!alphaCount || opacity !== 15) throw new Error('Invalid transparent packet');
        } else {
          if (kind === 1) previous = applyDelta(previous, payload[offset + run]);
          if (kind === 2) previous = view.getUint16(offset + run * 2, true);
          pixels[i] = previous;
          alpha[i] = opacity;
        }
      }
      offset += used;
      opacity = 15;
    }
    if (offset !== end || x !== width || opacity !== 15) throw new Error('Row must end at its last pixel');
  }
  return { pixels, alpha };
}

export function psnr(reference, decoded, referenceAlpha, decodedAlpha) {
  const scales = [255 / 31, 255 / 63, 255 / 31];
  let error = 0;
  for (let i = 0; i < reference.length; i++) {
    const a = channels(blend565(0, reference[i], referenceAlpha[i]));
    const b = channels(blend565(0, decoded[i], decodedAlpha[i]));
    for (let j = 0; j < 3; j++) error += ((a[j] - b[j]) * scales[j]) ** 2;
  }
  return error ? 10 * Math.log10(255 ** 2 * 3 * reference.length / error) : Infinity;
}
