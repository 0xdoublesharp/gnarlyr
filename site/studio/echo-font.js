// Inter bitmap faces shipped with Gnarlyr. See studio/fonts/OFL.txt.
export const descents = { 16: 4, 18: 5, 22: 6, 26: 7 };
const faces = new Map();
const pending = new Map();

export function parseFace(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const magic = view.getUint32(0, true);
  const core = magic === 0x36314647; // The published 16px built-in bitmap.
  if (!core && magic !== 0x464c4745) throw new Error('Invalid ECHO font face');
  const count = view.getUint32(core ? 4 : 8, true);
  const roots = core ? 0 : view.getUint16(20, true);
  const glyphs = new Map();
  for (let i = 0; i < count; i++) {
    const at = core ? 8 + i * 12 : (1 + roots + Math.floor(i / 42)) * 512 + i % 42 * 12;
    const offset = view.getUint32(at + 8, true);
    const width = bytes[at + 2];
    const height = bytes[at + 3];
    glyphs.set(view.getUint16(at, true), {
      width, height, left: view.getInt8(at + 4), top: view.getInt8(at + 5), advance: bytes[at + 6],
      bitmap: bytes.subarray(offset, offset + Math.ceil(width * height / 2)),
    });
  }
  return glyphs;
}

async function fetchFace(name) {
  const response = await fetch(new URL('fonts/' + name + '.bin', import.meta.url));
  if (!response.ok) throw new Error('Could not load the ECHO font');
  return parseFace(new Uint8Array(await response.arrayBuffer()));
}

export function loadFace(size, bold) {
  const key = size + '/' + Number(bold);
  if (!pending.has(key)) {
    const names = size === 16 ? ['inter_regular_16_latin1', 'inter_regular_16_core'] :
      [`inter_${bold ? 'semibold' : 'regular'}_${size}`];
    const loading = Promise.all(names.map(fetchFace))
      .then((sources) => { faces.set(key, new Map(sources.flatMap((source) => [...source]))); })
      .catch((error) => { pending.delete(key); throw error; });
    pending.set(key, loading);
  }
  return pending.get(key);
}

export function loadTextSize(size) {
  return Promise.all([loadFace(size, false), loadFace(size, true), loadFace(16, false),
    loadFace(22, true), ...(size === 26 ? [loadFace(22, false)] : [])]);
}

export function textRun(value, area, size, bold, align) {
  const glyphs = faces.get(size + '/' + Number(bold));
  const advance = Math.floor(size * 0.55);
  const glyphFor = (letter) => glyphs?.get(letter.codePointAt(0)) ?? glyphs?.get(63);
  const measure = (text) => [...text].reduce((width, letter) => width + (glyphFor(letter)?.advance ?? advance), 0);
  const baseline = area.y + Math.trunc((area.height - size - descents[size]) / 2) + size;
  const width = measure(value);
  const pen = area.x + (align === 'center' ? Math.max(0, (area.width - width) >> 1) :
    align === 'right' ? Math.max(0, area.width - width) : 0);
  return { pen, width: Math.min(width, area.width), baseline, advance, glyphFor };
}

export function drawText(surface, value, area, ink, run) {
  const { baseline, advance, glyphFor } = run;
  let pen = run.pen;
  for (const letter of value) {
    if (pen >= area.x + area.width) break;
    const glyph = glyphFor(letter);
    if (!glyph) {
      if (letter !== ' ') surface.frame(pen + 1, baseline - Math.floor(advance * 1.1), advance - 3, Math.floor(advance * 1.1), ink);
      pen += advance;
      continue;
    }
    for (let y = 0; y < glyph.height; y++) {
      for (let x = 0; x < glyph.width; x++) {
        const at = y * glyph.width + x;
        const packed = glyph.bitmap[at >> 1];
        const alpha = at & 1 ? packed & 15 : packed >>> 4;
        const left = pen + glyph.left + x;
        const top = baseline - glyph.top + y;
        if (alpha && left >= area.x && left < area.x + area.width && top >= area.y && top < area.y + area.height) {
          surface.pixel(left, top, ink, alpha);
        }
      }
    }
    pen += glyph.advance;
  }
}
