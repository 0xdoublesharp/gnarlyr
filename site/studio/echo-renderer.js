// Deterministic RGB565 preview. The surface stays independent of Canvas so a
// future native renderer can return the same pixels, text regions and role map.
import { blend565, channels, rgb565, rgb888 } from './art-codec.js';
import { drawText, descents, textRun } from './echo-font.js';
import { ROLES, STYLES } from './gtheme-codec.js';

export const SCREENS = { home: 'Home', list: 'Music', now_playing: 'Now Playing', settings: 'Settings', splash: 'Splash' };
export const MODES = { cover: 'Cover', cassette: 'Cassette', vu: 'VU', spectrum: 'Spectrum' };
export const HOME_ROWS = ['Music', 'Mixtapes', 'Recap', 'Clock', 'USB mode', 'Settings'];
export const LIST_ROWS = ['Albums', 'Artists', 'Album artists', 'Genres', 'Folders', 'Songs', 'Playlists', 'Favorites'];
export const SETTINGS_ROWS = ['Display & text', 'Playback', 'Audio', 'Storage', 'Bluetooth', 'Language', 'About'];
const homeIcons = ['icon_music', 'icon_mixtape', 'icon_diagnostics', 'icon_timeout', 'icon_usb', 'icon_settings'];
const roleIndex = Object.fromEntries(ROLES.map((role, i) => [role, i]));
const none = 255;

const rgbLookup = new Uint8Array(65536 * 3);
const luminances = new Uint16Array(65536);
for (let color = 0; color < 65536; color++) {
  const [r, g, b] = rgb888(color);
  rgbLookup.set([r, g, b], color * 3);
  luminances[color] = Math.floor((r * r * 2126 + g * g * 7152 + b * b * 722) / 10000);
}
export const luminance = (color) => luminances[color];
export function contrastRatio(foreground, background) {
  const a = luminance(foreground) + 3251;
  const b = luminance(background) + 3251;
  return Math.max(a, b) / Math.min(a, b);
}

export function interpolate(from, to, step, steps) {
  const first = channels(from);
  const last = channels(to);
  const [r, g, b] = first.map((value, i) => value + Math.trunc((last[i] - value) * step / steps));
  return r << 11 | g << 5 | b;
}

class Surface {
  constructor(width, height, palette, checkContrast) {
    this.width = width;
    this.height = height;
    this.pixels = new Uint16Array(width * height);
    this.roles = new Uint8Array(width * height).fill(none);
    this.labels = [];
    this.palette = palette;
    this.checkContrast = checkContrast;
  }

  pixel(x, y, color, opacity = 15) {
    if (x < 0 || y < 0 || x >= this.width || y >= this.height) return;
    const at = y * this.width + x;
    this.pixels[at] = blend565(this.pixels[at], color, opacity);
  }

  fill(x, y, width, height, color, role = none) {
    const left = Math.max(0, x);
    const right = Math.min(this.width, x + width);
    if (right <= left) return;
    for (let row = Math.max(0, y); row < Math.min(this.height, y + height); row++) {
      this.pixels.fill(color, row * this.width + left, row * this.width + right);
      this.roles.fill(role, row * this.width + left, row * this.width + right);
    }
  }

  role(x, y, width, height, role) {
    this.fill(x, y, width, height, this.palette[roleIndex[role]], roleIndex[role]);
  }

  frame(x, y, width, height, color) {
    this.fill(x, y, width, 1, color);
    this.fill(x, y + height - 1, width, 1, color);
    this.fill(x, y, 1, height, color);
    this.fill(x + width - 1, y, 1, height, color);
  }

  line(x, y, right, bottom, color) {
    const dx = Math.abs(right - x);
    const dy = -Math.abs(bottom - y);
    const sx = x < right ? 1 : -1;
    const sy = y < bottom ? 1 : -1;
    let error = dx + dy;
    while (true) {
      this.pixel(x, y, color);
      if (x === right && y === bottom) break;
      const twice = 2 * error;
      if (twice >= dy) { error += dy; x += sx; }
      if (twice <= dx) { error += dx; y += sy; }
    }
  }

  disc(x, y, radius, color) {
    for (let dy = -radius; dy <= radius; dy++) {
      const reach = Math.floor(Math.sqrt(radius * radius - dy * dy));
      this.fill(x - reach, y + dy, reach * 2 + 1, 1, color);
    }
  }

  label(value, x, y, width, height, role, size, { bold = false, align = 'left', under = 'background' } = {}) {
    if (!value || width < 1 || height < 1) return;
    const ink = this.palette[roleIndex[role]];
    const bounds = { x, y, width, height };
    const run = textRun(value, bounds, size, bold, align);
    let contrast = Infinity;
    let low = 0;
    const a = luminance(ink) + 3251;
    if (this.checkContrast) {
      for (let row = Math.max(y, 0); row < Math.min(y + height, this.height); row++) {
        for (let column = Math.max(run.pen, 0); column < Math.min(run.pen + run.width, this.width); column++) {
          const b = luminance(this.pixels[row * this.width + column]) + 3251;
          const high = Math.max(a, b);
          const dark = Math.min(a, b);
          contrast = Math.min(contrast, high / dark);
          if (high * 2 < dark * 9) low++;
        }
      }
    }
    this.labels.push({ ...bounds, x: run.pen, width: run.width, value, role, under, contrast, low });
    drawText(this, value, bounds, ink, run);
  }

  image(asset, x, y, width, height, screenCoordinates = false, role = none) {
    if (!asset) return false;
    const maxX = Math.min(this.width, x + width);
    const maxY = Math.min(this.height, y + height);
    for (let row = Math.max(0, y); row < maxY; row++) {
      for (let col = Math.max(0, x); col < maxX; col++) {
        const srcX = (screenCoordinates ? col : col - x) % asset.width;
        const srcY = (screenCoordinates ? row : row - y) % asset.height;
        const source = srcY * asset.width + srcX;
        const at = row * this.width + col;
        const opacity = asset.alpha[source];
        if (!opacity) continue;
        this.pixels[at] = blend565(this.pixels[at], asset.pixels[source], opacity);
        this.roles[at] = role;
      }
    }
    return true;
  }
}

export function selectAsset(assets, slot, orientation, textSize) {
  let best;
  let rank = -1;
  for (const asset of assets) {
    if (asset.slot !== slot || ![orientation, 'any'].includes(asset.orientation) ||
      ![textSize, 'any'].includes(asset.text_size)) continue;
    const score = (asset.orientation === orientation ? 2 : 0) + (asset.text_size === textSize ? 1 : 0);
    if (score > rank) { best = asset; rank = score; }
  }
  return best;
}

function cover(surface, theme, x, y, size, missing) {
  const p = surface.palette;
  for (let row = 0; row < size; row++) {
    surface.fill(x, y + row, size, 1, interpolate(p[roleIndex.backdrop_top], p[roleIndex.backdrop_bottom], row, size), roleIndex.backdrop_top);
  }
  const cx = x + (size >> 1);
  const cy = y + (size >> 1);
  if (missing) {
    surface.disc(cx, cy, Math.floor(size / 4), p[roleIndex.divider]);
    surface.disc(cx, cy, Math.floor(size / 8), p[roleIndex.accent]);
    surface.disc(cx, cy, Math.floor(size / 32), p[roleIndex.backdrop_bottom]);
  } else {
    const radius = Math.floor(size * .3);
    surface.disc(cx, cy, radius, p[roleIndex.accent_alt]);
    for (let stripe = 1; stripe <= 6; stripe++) {
      const step = Math.floor(stripe * radius / 7);
      const reach = Math.floor(Math.sqrt(radius * radius - step * step));
      surface.fill(cx - reach, cy + step, 2 * reach + 1, stripe < 4 ? 1 : 2, p[roleIndex.backdrop_top]);
    }
  }
  if (theme.base === 'classic') surface.frame(x, y, size, size, p[roleIndex.header_top]);
}

const sunsetColors = [rgb565(255, 210, 143), rgb565(255, 142, 149), rgb565(239, 85, 189)];

function sunsetDisc(surface, x, y, size) {
  const radius = size * 8;
  const gaps = [118, 129, 154, 167].map((step) => Math.floor((step * size + 96) / 192));
  gaps[1] = Math.max(gaps[0] + 1, gaps[1]);
  gaps[3] = Math.max(gaps[2] + 1, gaps[3]);
  for (let row = 0; row < size; row++) {
    if ((row >= gaps[0] && row < gaps[1]) || (row >= gaps[2] && row < gaps[3])) continue;
    const rise = Math.abs((2 * row + 1) * 8 - radius);
    let inset = radius - Math.floor(Math.sqrt(radius * radius - rise * rise));
    if (size < 24) inset = (inset + 8) & ~15;
    const first = Math.ceil(inset / 16);
    const last = Math.floor((size * 16 - inset) / 16);
    if (first >= last) continue;
    const stop = Math.floor((2 * row + 1) * 128 / size);
    const color = stop < 120 ? interpolate(sunsetColors[0], sunsetColors[1], stop, 120) :
      interpolate(sunsetColors[1], sunsetColors[2], stop - 120, 136);
    surface.fill(x + first, y + row, last - first, 1, color);
    surface.pixel(x + first - 1, y + row, color, Math.floor((first * 16 - inset) * 15 / 16));
    surface.pixel(x + last, y + row, color, Math.floor((size * 16 - inset - last * 16) * 15 / 16));
  }
}

const statusSlots = new Set(['icon_bolt', 'icon_plug', 'icon_bluetooth_small', 'icon_lock',
  'icon_playing', 'icon_album_small', 'icon_song_small', 'icon_shuffle_small', 'icon_repeat', 'icon_speaker']);

function drawHeader(surface, theme, get, title, statusSlot) {
  const w = surface.width;
  const p = surface.palette;
  surface.role(0, 0, w, 29, 'header_background');
  surface.role(0, 29, w, 1, theme.header_rule ? 'accent' : 'divider');

  const logo = get('icon_logo');
  if (logo) surface.image(logo, 12, 8, logo.width, logo.height);
  else sunsetDisc(surface, 12, 8, 14);
  const statusArt = statusSlots.has(statusSlot) ? get(statusSlot) : undefined;
  const charge = statusSlot === 'icon_bolt' || statusSlot === 'icon_plug';
  const battery = w - 41;
  const reserve = statusArt && !charge ? Math.min(14, statusArt.width) + 10 : 0;
  surface.label(title, 38, 0, w - 86 - reserve, 29, 'header_text', 22, { bold: true, under: 'header_background' });
  surface.frame(battery, 7, 26, 16, p[roleIndex.header_text]);
  surface.role(battery + 2, 9, 22, 12, charge ? 'success' : 'header_text');
  surface.role(battery + 26, 11, 3, 8, 'header_text');
  if (statusArt && statusArt.width <= 14 && statusArt.height <= 14) {
    surface.image(statusArt, charge ? battery + 1 : battery - reserve, 8, statusArt.width, statusArt.height);
  }
}

function drawList(surface, theme, get, options, size, rowHeight, portrait) {
  const { width: w, height: h, palette: p } = surface;
  const home = options.screen === 'home';
  const settings = options.screen === 'settings';
  const items = home ? HOME_ROWS : settings ? SETTINGS_ROWS : LIST_ROWS;
  const wide = home && !portrait ? w - 140 : w;
  const top = 30;
  const bottom = home && portrait ? top + Math.min(items.length, Math.floor((h - top - 128) / rowHeight)) * rowHeight : h;
  const art = get(home ? 'home' : 'list');
  surface.image(art, 0, top, wide, bottom - top, true, roleIndex.background);
  const visible = Math.floor((bottom - top) / rowHeight);
  const offset = Math.max(0, options.selection - visible + 1);
  for (const [i, label] of items.slice(offset, offset + visible).entries()) {
    const y = top + i * rowHeight;
    const active = i + offset === options.selection;
    if (active) {
      surface.fill(0, y, wide, rowHeight, interpolate(p[4], p[5], 1, 10), roleIndex.selection_background);
      surface.role(0, y, 3, rowHeight, 'accent');
    }
    let left = 12;
    if (home) {
      const replacement = get(homeIcons[i + offset]);
      const yIcon = y + Math.floor((rowHeight - 20) / 2);
      if (replacement) surface.image(replacement, left, yIcon, replacement.width, replacement.height);
      else drawHomeIcon(surface, i + offset, left, yIcon, active ? p[7] : p[16], active ? p[7] : p[2]);
      left += 28;
    }
    surface.label(label, left, y, wide - left - 28, rowHeight, active ? 'selection_text' : 'text', size,
      { bold: active, under: active ? 'selection_background' : 'background' });
    const ink = p[active ? roleIndex.selection_text : roleIndex.chevron];
    const mid = y + (rowHeight >> 1);
    surface.line(wide - 20, mid - 6, wide - 15, mid, ink);
    surface.line(wide - 15, mid, wide - 20, mid + 6, ink);
  }
  if (items.length > visible) {
    const track = bottom - top - 4;
    const thumb = Math.max(12, Math.floor(track * visible / items.length));
    const shift = Math.floor((track - thumb) * offset / (items.length - visible));
    surface.role(wide - 6, top + 2 + shift, 4, thumb, 'scrollbar_thumb');
  }
  if (home) {
    const x = portrait ? 0 : wide;
    const y = portrait ? bottom : top;
    const panelWidth = w - x;
    const panelHeight = h - y;
    for (let row = 0; row < panelHeight; row++) {
      surface.fill(x, y + row, panelWidth, 1, interpolate(p[18], p[19], row, panelHeight - 1), roleIndex.backdrop_top);
    }
    if (theme.horizon) {
      const middle = panelHeight >> 1;
      const floor = panelHeight - middle;
      const grid = interpolate(p[11], p[16], 1, 3);
      for (let line = 0; line < 7; line++) surface.fill(x, y + middle + Math.floor(line * line * floor / 36), panelWidth, 1, grid);
      for (let ray = -5; ray <= 5; ray++) {
        for (let row = 0; row < floor; row++) {
          const col = x + (panelWidth >> 1) + ray * (6 + Math.floor(row * panelWidth / (floor * 5)));
          if (col >= x && col < w) surface.pixel(col, y + middle + row, grid);
        }
      }
    }
    surface.role(x, y, portrait ? w : 1, portrait ? 1 : panelHeight, 'divider');
    cover(surface, theme, x + ((panelWidth - 112) >> 1), y + 8, 112, options.missingCover);
    surface.label('Late Night Signals', x + 10, y + 126, panelWidth - 20, 18, 'text', 16,
      { align: 'center', under: 'backdrop_top' });
    surface.label('The Night Drive', x + 10, y + 146, panelWidth - 20, 18, 'text_secondary', 16,
      { align: 'center', under: 'backdrop_top' });
  }
}

function drawHomeIcon(s, i, x, y, accent, ink) {
  const line = (a, b, c, d, color = ink) => s.line(x + a, y + b, x + c, y + d, color);
  if (i === 0) {
    line(8, 5, 17, 3, accent); line(8, 6, 8, 15, accent); line(17, 4, 17, 13);
    s.disc(x + 5, y + 15, 2, ink); s.disc(x + 14, y + 13, 2, accent);
  } else if (i === 1) {
    s.frame(x + 1, y + 4, 18, 12, accent); s.disc(x + 6, y + 9, 2, ink); s.disc(x + 14, y + 9, 2, ink);
  } else if (i === 2) {
    s.frame(x + 2, y + 3, 16, 14, accent); line(4, 13, 8, 7); line(8, 7, 11, 12); line(11, 12, 16, 6);
  } else if (i === 3) {
    const ground = s.pixels[(y + 10) * s.width + x + 10];
    s.disc(x + 10, y + 10, 8, accent); s.disc(x + 10, y + 10, 6, ground);
    line(10, 5, 10, 10); line(10, 10, 14, 12);
  } else if (i === 4) {
    line(10, 3, 10, 17, accent); line(4, 5, 4, 11); line(4, 11, 10, 14); line(16, 7, 16, 10); line(16, 10, 10, 12);
  } else {
    for (const [j, at] of [6, 13, 8].entries()) {
      line(1, 4 + 6 * j, 19, 4 + 6 * j);
      s.disc(x + at, y + 4 + 6 * j, 2, accent);
    }
  }
}

function stage(surface, theme, art, mode, levels, style) {
  const { x, y, width: w, height: h } = art;
  const p = surface.palette;
  const line = y + Math.floor(h * style.horizon / 100);
  const cx = x + (w >> 1);
  const amp = style.pulse ? levels?.bass ?? 0 : 0;
  const radius = Math.min(Math.floor(h * .36), Math.floor(h * style.sun_radius / 100 + amp * 8));
  for (let row = 0; row < h; row++) {
    surface.fill(x, y + row, w, 1, interpolate(p[18], p[19], row, h), roleIndex.backdrop_top);
  }
  const sunY = line - Math.floor(radius * .45);
  const sunColor = interpolate(p[17], p[16], Math.round(amp * 6), 15);
  for (let dy = -radius; dy <= radius && sunY + dy < line; dy++) {
    if (sunY + dy < y) continue;
    const row = dy + radius;
    if (dy > 0 && style.slats && row % Math.max(2, Math.floor(radius / style.slats)) === 0) continue;
    const reach = Math.floor(Math.sqrt(radius * radius - dy * dy));
    surface.fill(cx - reach, sunY + dy, reach * 2 + 1, 1, interpolate(sunColor, p[16], row, radius * 2), roleIndex.accent_alt);
  }
  const floor = y + h - line;
  const grid = interpolate(p[11], p[16], 1, 3);
  if (theme.horizon || theme.base === 'sunset' || mode === 'vu' || theme.style?.some(({ target }) => ['floor_lines', 'rays'].includes(target))) {
    for (let n = 0; n < style.floor_lines; n++) {
      const at = line + Math.floor(n * n * (floor - 1) / (style.floor_lines - 1) ** 2);
      surface.fill(x, at, w, 1, grid, roleIndex.divider);
    }
    for (let ray = 0; ray < style.rays; ray++) {
      const share = style.rays === 1 ? .5 : ray / (style.rays - 1);
      surface.line(cx + Math.floor((share - .5) * 30), line, x + Math.floor(share * (w - 1)), y + h - 1, grid);
    }
  }
  if (mode === 'spectrum') {
    const step = Math.floor((w - 12) / 16);
    const bars = levels?.bands ?? Array.from({ length: 16 }, (_, i) => .12 + (.6 * ((i * 7 + 5) % 17) / 16));
    for (const [i, value] of bars.entries()) {
      const height = Math.min(line - y, Math.max(1, Math.floor((line - y - 6) * value)));
      const col = x + 6 + i * step;
      surface.role(col, line - height, step - 3, height, 'accent');
      surface.role(col, line - height, step - 3, 1, 'selection_shine');
      const reflected = Math.min(y + h - line - 1, Math.floor(height * .55));
      for (let row = 0; row < reflected; row++) {
        surface.fill(col, line + row + 1, step - 3, 1, interpolate(grid, p[19], row, reflected), roleIndex.accent);
      }
    }
  } else {
    const meters = levels?.vu ?? [.63, .49];
    const across = Math.floor((w - 24) / 16);
    for (const [channel, value] of meters.entries()) {
      const at = line + 6 + channel * 11;
      for (let step = 0; step < 16; step++) {
        const role = step >= 13 ? 'danger' : 'accent';
        surface.fill(x + 12 + step * across, at, across - 2, 6,
          step <= value * 15 ? p[roleIndex[role]] : interpolate(p[19], p[roleIndex[role]], 1, 5), roleIndex[role]);
      }
    }
  }
}

const sine = [0, 6, 11, 15, 16, 15, 11, 6, 0, -6, -11, -15, -16, -15, -11, -6];
function reelPoint(x, y, phase, radius) {
  return [x + Math.trunc(sine[phase & 15] * radius / 16), y + Math.trunc(sine[(phase + 4) & 15] * radius / 16)];
}

function cassette(surface, art, portrait, options, get, style, title, size) {
  const { x, y, width: w, height: h } = art;
  const p = surface.palette;
  for (let row = 0; row < h; row++) {
    surface.fill(x, y + row, w, 1, interpolate(p[18], p[19], row, h), roleIndex.backdrop_top);
  }
  surface.frame(x + 2, y + 2, w - 4, h - 4, p[17]);
  const reelSize = portrait ? 48 : 38;
  const reelX = x + (w >> 1);
  const gap = portrait ? 14 : 50;
  const reelTop = y + (portrait ? Math.floor(h * 4 / 7) : h - 46);
  surface.role(x + 4, y + 4, w - 8, reelTop - y - 8, 'backdrop_top');
  surface.role(reelX - gap - reelSize - 6, reelTop - 2, gap * 2 + reelSize * 2 + 12, reelSize + 4, 'backdrop_top');
  surface.frame(reelX - gap - reelSize - 6, reelTop - 2, gap * 2 + reelSize * 2 + 12, reelSize + 4, p[16]);
  surface.line(x + 10, reelTop - 4, x + w - 10, reelTop - 4, p[16]);
  const label = get('cassette_label');
  if (label) surface.image(label, x + ((w - 160) >> 1), y + 8, 160, 48, false, roleIndex.backdrop_top);
  else {
    const line = size + descents[size];
    const bold = { bold: true, under: 'backdrop_top', align: style.align ? 'center' : 'left' };
    surface.label(title, x + 18, y + 6, w - 36, line, 'text', size, bold);
    surface.label(options.artist || 'The Night Drive', x + 18, y + 6 + line, w - 36, Math.min(line, reelTop - y - 12 - line), 'accent', size, { ...bold, bold: false });
    if (portrait) surface.label('After Hours', x + 18, y + 6 + line * 2, w - 36, 20, 'text_secondary', 16, { under: 'backdrop_top' });
  }
  for (const [index, cx] of [reelX - gap - reelSize / 2, reelX + gap + reelSize / 2].entries()) {
    const cy = reelTop + (reelSize >> 1);
    const ratio = Math.max(0, Math.min(1, options.progress ?? .36));
    const max = reelSize >> 1;
    const min = Math.floor(max / 3);
    const inner = Math.floor(Math.sqrt(min ** 2 + (index ? ratio : 1 - ratio) * (max ** 2 - min ** 2)));
    for (const [radius, color] of [[max - 2, p[17]], [inner - 2, p[3]]]) {
      for (let step = 0; step < 16; step++) {
        surface.line(...reelPoint(cx, cy, step, radius), ...reelPoint(cx, cy, step + 1, radius), color);
      }
    }
    const phase = Math.floor((options.position ?? 0) / 60);
    for (let spoke = 0; spoke < 3; spoke++) surface.line(cx, cy, ...reelPoint(cx, cy, phase + Math.floor(spoke * 16 / 3), max - 5), p[16]);
    surface.disc(cx, cy, 2, p[1]);
  }
}

function playingFrame(width, height, portrait, staged, large) {
  const place = (x, y, width, height) => ({ x, y, width, height });
  if (portrait) {
    const side = large && !staged ? 150 : 188;
    return {
      art: place(staged ? 12 : (width - side) >> 1, 42, staged ? width - 24 : side, side),
      column: place(12, 56 + side, width - 24, height - 148 - side),
      bar: place(12, height - 78, width - 24, 3), time: height - 70,
    };
  }
  const left = staged ? 260 : 144;
  return {
    art: staged ? place(12, 38, 236, 124) : place(16, 42, 112, 112),
    column: place(left, 38, width - 12 - left, height - 96),
    bar: place(12, height - 54, width - 24, 3), time: height - 48,
  };
}

function drawPlaying(surface, theme, get, options, size, portrait) {
  const { width: w, height: h, palette: p } = surface;
  const mode = options.mode;
  const title = options.title || 'Late Night Signals';
  const style = Object.fromEntries(STYLES.map(({ target, initial }) => [target, initial]));
  for (const { target, value } of theme.style ?? []) style[target] = value;
  surface.image(get('now_playing'), 0, 30, w, h - 30, true, roleIndex.background);
  const { art, column, bar, time } = playingFrame(w, h, portrait, mode !== 'cover', size === 26);
  if (mode === 'cassette') {
    cassette(surface, portrait ? art : { x: 12, y: 34, width: w - 24, height: 130 }, portrait, options, get, style, title, size);
  } else if (mode === 'cover') {
    cover(surface, theme, art.x, art.y, art.height, options.missingCover);
  } else {
    stage(surface, theme, art, mode, options.levels, style);
  }
  const align = style.align ? 'center' : 'left';
  const line = size + descents[size];
  if (mode !== 'cassette') {
    surface.label(title, column.x, column.y, column.width, line, 'text', size, { bold: true, align });
    surface.label(options.artist || 'The Night Drive', column.x, column.y + line + 2, column.width, line, 'accent', size, { align });
    surface.label('After Hours', column.x, column.y + (line + 2) * 2, column.width, 20, 'text_secondary', 16, { align });
  }
  if (mode !== 'cassette' || portrait) {
    surface.label(options.format || 'FLAC · 96 kHz · 24 bit', column.x, column.y + (mode === 'cassette' ? 0 : (line + 2) * 2 + 22),
      column.width, 20, 'text_secondary', 16, { align });
  }
  bar.height = theme.style?.some((entry) => entry.target === 'progress_height') ? style.progress_height : 3;
  const progress = options.volume === undefined ? options.progress ?? .36 : options.volume / 100;
  const filled = Math.max(0, Math.min(bar.width, Math.floor(bar.width * progress)));
  surface.role(bar.x, bar.y, bar.width, bar.height, 'divider');
  surface.role(bar.x, bar.y, filled, bar.height, options.volume === undefined ? 'accent' : 'accent_alt');
  surface.role(bar.x + Math.max(0, filled - 2), bar.y - 2, 2, bar.height + 4, 'text');
  surface.label(options.volume === undefined ? options.elapsed || '1:24' : 'Volume', 32, time, 70, 20, 'text_secondary', 16);
  surface.label(options.volume === undefined ? options.remaining || '-2:30' : String(options.volume), w - 90, time, 78, 20,
    'text_secondary', 16, { align: 'right' });
  const y = time + 10;
  const transport = get(options.playing ? 'icon_play' : 'icon_pause');
  // Transport icons are built-in draw calls in the current firmware.
  if (transport && options.previewTransport) surface.image(transport, 12, y - 7, transport.width, transport.height);
  else if (options.playing) {
    for (let step = 0; step < 8; step++) surface.fill(13 + step, y - (8 - step) / 2 | 0, 1, 8 - step, p[16]);
  } else {
    surface.role(13, y - 4, 2, 8, 'accent');
    surface.role(17, y - 4, 2, 8, 'accent');
  }
}

export function renderEcho(theme, options = {}) {
  const { orientation = 'landscape', screen = 'home', mode = 'cover', textSize = 'medium', selection = 0, contrast = true } = options;
  const portrait = orientation === 'portrait';
  const [width, height] = portrait ? [222, 480] : [480, 222];
  const [size, rowHeight] = { small: [18, 24], medium: [22, 32], large: [26, 38] }[textSize];
  const surface = new Surface(width, height, theme.palette, contrast);
  const get = (slot) => selectAsset(theme.assets ?? [], slot, orientation, textSize);
  surface.role(0, 0, width, height, 'background');
  if (screen === 'splash') {
    const art = surface.image(get('splash'), 0, 0, width, height, true, roleIndex.background);
    const mark = Math.floor(Math.min(width * 3, height * 2) / 7);
    const line = size + descents[size];
    const top = Math.floor((height - mark - line - 6) / 2);
    if (!art) {
      sunsetDisc(surface, (width - mark) >> 1, top, mark);
    }
    surface.label('Gnarlyr', 8, art ? (height - line) >> 1 : top + mark + 6, width - 16, line,
      'text', size, { bold: true, align: 'center' });
  } else {
    drawHeader(surface, theme, get, SCREENS[screen], options.statusSlot);
    if (screen === 'now_playing') drawPlaying(surface, theme, get, { ...options, mode }, size, portrait);
    else drawList(surface, theme, get, { ...options, screen, selection }, size, rowHeight, portrait);
  }
  return { width, height, pixels: surface.pixels, roles: surface.roles, labels: surface.labels };
}

export function roleAt(frame, x, y) {
  if (x < 0 || x >= frame.width || y < 0 || y >= frame.height) return undefined;
  const label = [...frame.labels].reverse().find(({ x: l, y: t, width, height }) => x >= l && y >= t && x < l + width && y < t + height);
  return label?.role ?? ROLES[frame.roles[y * frame.width + x]];
}

export function paintEcho(canvas, frame, overlay = false) {
  if (canvas.width !== frame.width) canvas.width = frame.width;
  if (canvas.height !== frame.height) canvas.height = frame.height;
  const context = canvas.getContext('2d');
  const data = context.createImageData(frame.width, frame.height);
  for (let n = 0, at = 0; n < frame.pixels.length; n++, at += 4) {
    const color = frame.pixels[n] * 3;
    data.data[at] = rgbLookup[color];
    data.data[at + 1] = rgbLookup[color + 1];
    data.data[at + 2] = rgbLookup[color + 2];
    data.data[at + 3] = 255;
  }
  context.putImageData(data, 0, 0);
  if (overlay) {
    context.strokeStyle = '#ff5375';
    context.fillStyle = '#ff537533';
    for (const { x, y, width, height, low } of frame.labels) {
      if (!low) continue;
      context.fillRect(x, y, width, height);
      context.strokeRect(x + .5, y + .5, width - 1, height - 1);
    }
  }
}
