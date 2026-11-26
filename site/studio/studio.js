import { hexColor } from './art-codec.js';
import { decodePng } from './png.js';
import { download, photoPixels, showcaseCard, sourceZip } from './asset-io.js';
import { StudioAudio } from './audio.js';
import { fetchCatalog, fetchManifest } from './catalog.js';
import { loadTextSize } from './echo-font.js';
import { HOME_ROWS, LIST_ROWS, MODES, paintEcho, renderEcho, roleAt, SCREENS, selectAsset, SETTINGS_ROWS } from './echo-renderer.js';
import { ART_SLOTS, assetKey, BASES, byteLength, decodeTheme, encodeTheme, ICONS, MAX_BYTES, METADATA,
  newThemeId, ROLES, STYLES, validateMetadata } from './gtheme-codec.js';
import { autoPalette, builtinPalette, fromHex, photoColors } from './palette.js';
import { decodeThemeLink, encodeThemeLink, themeDeepLink } from './theme-link.js';

const byId = (id) => document.getElementById(id);
const all = (selector) => Array.from(document.querySelectorAll(selector));
const on = (id, kind, fn) => byId(id).addEventListener(kind, fn);
const format = (number) => new Intl.NumberFormat('en-US').format(number);
const titleCase = (name) => name[0].toUpperCase() + name.slice(1).replaceAll('_', ' ');
const orientations = ['landscape', 'portrait'];
const textSizes = ['small', 'medium', 'large'];
const fontSizes = { small: 18, medium: 22, large: 26 };
const galleryPresets = ['vice-86', 'pocket-scope', 'desktop-98'];
const presets = { neon: 'Neon Noir', 'vice-86': 'Vice 86', 'pocket-scope': 'Pocket Scope',
  'desktop-98': 'Desktop 98', glass: 'Sea Glass', classic: 'Classic', tape: 'Tape Deck' };
const artNames = { home: 'Home', now_playing: 'Now Playing', list: 'Music & settings lists', splash: 'Splash', cassette_label: 'Cassette label' };
const initial = {
  ...builtinPalette('neon'), id: newThemeId(), name: 'Neon Noir', author: 'Your name',
  version: '1.0.0', licence: 'CC0-1.0', homepage: '', gallery: '', assets: [], creator: '', remixedFrom: null,
};
const past = [];
const future = [];
let theme = initial;
let view = { screen: 'home', mode: 'cover', orientation: 'landscape', textSize: 'medium', selection: 0 };
let frame;
let packed;
let paintedAssets = [];
let findings;
let volume = 72;
let activePanel = 'palette';
let activeView = 'preview';
let timer;
let pendingPaint = false;
let fileOperation = 0;
let audioOperation = 0;
let audioTitle = '';
let audioFormat;
let audioArtist;
let fontRequest = 0;
let lastFrame = 0;
let animationFrame = 0;
let lastVisual = {};
let volumeUntil = 0;
let iconCursor = { x: 0, y: 0 };
let stroke;
let metadataField;
const audio = new StudioAudio();
const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');

function create(tag, text = '', props = {}) {
  const node = Object.assign(document.createElement(tag), props);
  if (text) node.textContent = text;
  return node;
}

function options(select, labels, value) {
  select.replaceChildren(...labels.map(([name, label]) => new Option(label, name, false, value === name)));
}

function notice(message, error = false) {
  const node = byId('studio-status');
  node.textContent = message;
  node.dataset.error = String(error);
  node.hidden = false;
  clearTimeout(timer);
  timer = setTimeout(() => { node.hidden = true; }, error ? 9000 : 4200);
}

function action(fn) {
  return async (...args) => {
    try { await fn(...args); }
    catch (error) { notice(error.message || 'The operation failed.', true); }
  };
}

function validHandle(handle) {
  return handle.length >= 2 && handle.length <= 32 && !/[^a-z0-9_-]/i.test(handle) ? handle.toLowerCase() : '';
}

function event(name, format) {
  document.dispatchEvent(new CustomEvent('gnarlyr:studio', { detail: { name, format } }));
}

function commit(next, undoPoint = theme) {
  if (theme !== next) {
    past.push(undoPoint);
    if (past.length > 30) past.shift();
    future.length = 0;
    theme = next;
    sync();
  }
}

function historyMove(from, to) {
  if (!from.length) return;
  to.push(theme);
  theme = from.pop();
  sync();
}

function sync() {
  byId('current-theme').textContent = theme.name || 'Untitled theme';
  byId('undo').disabled = !past.length;
  byId('redo').disabled = !future.length;
  for (const [name] of METADATA) {
    const field = byId('meta-' + name);
    if (field !== metadataField) field.value = theme[name] ?? '';
    updateCounter(field);
  }
  byId('creator').value = theme.creator || '';
  const remix = byId('remix-credit');
  remix.textContent = theme.remixedFrom ? 'Remix of ' + theme.remixedFrom : '';
  remix.hidden = !theme.remixedFrom;
  byId('base').value = theme.base;
  for (const flag of ['horizon', 'header_rule']) byId(flag).checked = theme[flag];
  for (const style of STYLES) {
    const entry = theme.style?.find(({ kind, target }) => kind === style.kind && target === style.target);
    byId('style-' + style.target).value = entry ? String(entry.value) : '';
  }
  syncPalette();
  updatePack();
  syncArt();
  drawIconGrid();
  findings = undefined;
  if (activeView === 'preflight') {
    byId('preflight-status').textContent = 'The theme changed. Run preflight again to refresh the contact sheet.';
  }
  schedulePaint();
}

function updatePack() {
  let failure;
  try {
    packed = encodeTheme(theme);
    paintedAssets = theme.assets.map((asset, index) => ({ ...asset, ...packed.reports[index].image }));
  } catch (error) {
    failure = error.message;
    packed = undefined;
    paintedAssets = theme.assets;
  }
  byId('pack-meter').value = packed?.bytes.length ?? 0;
  byId('pack-size').textContent = packed ?
    format(packed.bytes.length) + ' / ' + format(MAX_BYTES) + ' B' : failure;
  updateCpu();
  byId('open-companion').href = themeDeepLink(theme);
  for (const id of ['download-pack', 'download-source']) byId(id).disabled = !packed;
  const list = byId('asset-list');
  list.replaceChildren(...theme.assets.map((asset, index) => {
    const stats = packed?.reports[index];
    const label = artNames[asset.slot] || titleCase(asset.slot.slice(5));
    const quality = stats ? ' · ' + (Number.isFinite(stats.psnr) ? stats.psnr.toFixed(2) + ' dB PSNR' : 'RGB565 exact') : '';
    const row = create('li');
    const variant = asset.text_size === 'any' ? '' : ' · ' + asset.text_size;
    const link = create('button', `${label} · ${asset.orientation}${variant} · ${asset.codec} · ${asset.width} × ${asset.height}${quality}`, { type: 'button' });
    if (stats) link.title = format(stats.bytes) + ' encoded bytes';
    link.addEventListener('click', () => {
      if (Object.hasOwn(ICONS, asset.slot)) {
        byId('icon-slot').value = asset.slot;
        byId('icon-orientation').value = asset.orientation;
        byId('icon-text-size').value = asset.text_size;
        iconCursor = { x: 0, y: 0 };
        section('icons', true);
      } else {
        byId('art-slot').value = asset.slot;
        byId('art-tile').checked = asset.tile;
        syncArt(false);
        byId('art-orientation').value = asset.orientation;
        syncArt();
        showAsset(asset);
      }
    });
    row.append(link);
    return row;
  }));
}

function updateCpu() {
  const art = selectAsset(theme.assets, 'now_playing', view.orientation, view.textSize);
  byId('cpu-load').textContent = art ?
    '17.4% CPU · indexed Now Playing photo benchmark' : '12.7% CPU · palette-only Now Playing benchmark';
}

function section(name, focus = false) {
  activePanel = name;
  for (const tab of all('[data-panel]')) {
    const selected = tab.dataset.panel === name;
    tab.setAttribute('aria-selected', String(selected));
    tab.tabIndex = selected ? 0 : -1;
    byId(tab.getAttribute('aria-controls')).hidden = !selected;
    if (selected && focus) tab.focus();
  }
  if (name === 'icons') drawIconGrid();
  schedulePaint();
}

function showView(name) {
  activeView = name;
  for (const tab of all('[data-view]')) {
    const selected = tab.dataset.view === name;
    tab.setAttribute('aria-pressed', String(selected));
    byId('view-' + tab.dataset.view).hidden = !selected;
  }
  if (name === 'preview') {
    schedulePaint();
    startAnimation();
  }
}

function setView(changes) {
  const former = view;
  view = { ...view, ...changes };
  if (view.screen !== former.screen && changes.selection === undefined) view.selection = 0;
  if (changes.screen && !Object.hasOwn(changes, 'title')) delete view.title;
  if (changes.screen && !Object.hasOwn(changes, 'missingCover')) delete view.missingCover;
  for (const [id, prop] of [['screen', 'screen'], ['orientation', 'orientation'], ['mode', 'mode'], ['text-size', 'textSize']]) {
    byId(id).value = view[prop];
  }
  byId('echo-body').dataset.orientation = view.orientation;
  byId('display-resolution').textContent = view.orientation === 'portrait' ? '222 × 480' : '480 × 222';
  if (view.textSize !== former.textSize) void fonts();
  updateCpu();
  schedulePaint();
  startAnimation();
}

async function fonts() {
  const request = ++fontRequest;
  try { await loadTextSize(fontSizes[view.textSize]); }
  catch (error) { notice(error.message, true); }
  if (request === fontRequest) schedulePaint();
}

function formatTime(seconds) {
  return Math.floor(seconds / 60) + ':' + String(Math.floor(seconds % 60)).padStart(2, '0');
}

function previewTheme() {
  return { ...theme, assets: paintedAssets };
}

function currentStageOptions() {
  return { ...view, ...lastVisual, title: view.title || audioTitle || undefined,
    format: audioFormat, artist: audioArtist, contrast: byId('contrast').checked,
    statusSlot: activePanel === 'icons' ? byId('icon-slot').value : undefined,
    volume: performance.now() < volumeUntil ? volume : undefined };
}

function schedulePaint() {
  if (!pendingPaint) {
    pendingPaint = true;
    requestAnimationFrame(() => {
      pendingPaint = false;
      if (activeView !== 'preview') return;
      frame = renderEcho(previewTheme(), currentStageOptions());
      paintEcho(byId('echo'), frame, byId('contrast').checked);
      if (byId('contrast').checked && !audio.playing) listHotspots();
      else byId('contrast-results').hidden = true;
    });
  }
}

function highlightRole(role) {
  if (!role) return;
  section('palette');
  const row = byId('role-' + role);
  if (!row) return;
  all('.studio-role.is-highlighted').forEach((item) => item.classList.remove('is-highlighted'));
  row.closest('details').open = true;
  row.classList.add('is-highlighted');
  row.querySelector('input').focus({ preventScroll: true });
  row.scrollIntoView({ block: 'nearest' });
}

function listHotspots() {
  const list = byId('contrast-results');
  const low = frame.labels.filter(({ low }) => low);
  list.replaceChildren(...low.map(({ value, role, contrast }) => {
    const button = create('button', `${value} · ${contrast.toFixed(2)}:1 · Edit ${titleCase(role)}`, { type: 'button' });
    button.addEventListener('click', () => highlightRole(role));
    const item = create('li');
    item.append(button);
    return item;
  }));
  list.hidden = !low.length;
}

const seeds = ['background', 'text', 'accent'];
function syncPalette() {
  seeds.forEach((role) => { byId('seed-' + role).value = hexColor(theme.palette[ROLES.indexOf(role)]); });
  ROLES.forEach((role, i) => { byId('role-' + role).querySelector('input').value = hexColor(theme.palette[i]); });
}

function initPalette() {
  options(byId('preset'), [['', 'Choose a preset'], ...Object.entries(presets)], '');
  for (const role of seeds) {
    const label = create('label', titleCase(role));
    label.prepend(create('input', '', { id: 'seed-' + role, type: 'color' }));
    byId('seed-colors').append(label);
  }
  const groups = [
    ['Surface & type', ROLES.slice(0, 4)], ['Selection', ROLES.slice(4, 8)],
    ['Header & dividers', ROLES.slice(8, 13)], ['Status & accent', ROLES.slice(13, 18)], ['Backdrop', ROLES.slice(18)],
  ];
  for (const [name, roles] of groups) {
    const group = create('details', '', { className: 'studio-role-group', open: name === 'Surface & type' });
    const grid = create('div', '', { className: 'studio-role-grid' });
    for (const role of roles) {
      const label = create('label', titleCase(role), { className: 'studio-role', id: 'role-' + role });
      const input = create('input', '', { type: 'color' });
      let before;
      input.addEventListener('input', () => {
        before ??= theme;
        const palette = theme.palette.slice();
        palette[ROLES.indexOf(role)] = fromHex(input.value);
        theme = { ...theme, palette };
        if (seeds.includes(role)) byId('seed-' + role).value = hexColor(palette[ROLES.indexOf(role)]);
        schedulePaint();
      });
      input.addEventListener('change', () => {
        if (!before) return;
        const next = theme;
        theme = before;
        before = undefined;
        commit(next);
      });
      label.prepend(input);
      grid.append(label);
    }
    group.append(create('summary', name), grid);
    byId('palette-roles').append(group);
  }
}

async function galleryTheme(id, token) {
  const { themes } = await fetchCatalog();
  const record = themes.find((theme) => theme.id === id);
  if (!record) throw new Error('That theme is unavailable in the gallery.');
  const { manifest, url } = await fetchManifest(record);
  const assets = await Promise.all(manifest.assets.map(async ({ file, slot, orientation = 'any', text_size = 'any', codec = manifest.codec ?? 'indexed' }) => {
    if (!/^[a-z0-9_-]+\.png$/i.test(file)) throw new Error('Invalid sample artwork path');
    const response = await fetch(new URL(file, url));
    if (!response.ok) throw new Error('Could not load ' + file + '.');
    const pixels = await decodePng(await response.blob(), (w, h) => w <= 480 && h <= 480, !Object.hasOwn(ICONS, slot));
    return { ...pixels, slot, orientation, text_size, codec, tile: slot === 'list' && pixels.width <= 64 && pixels.height <= 64 };
  }));
  if (token !== fileOperation) return;
  const remix = `${record.name} (@${record.creator})`;
  commit({ ...manifest, style: manifest.style ?? [], assets, id: newThemeId(), remixedFrom: remix, creator: '' });
  notice('Loaded ' + record.name + ' for remixing.');
}

function replaceAsset(next, existing = assetKey(next)) {
  const index = theme.assets.findIndex((asset) => assetKey(asset) === existing);
  const assets = theme.assets.slice();
  if (index < 0) assets.push(next);
  else assets[index] = next;
  return { ...theme, assets };
}

function removeAsset(key) {
  commit({ ...theme, assets: theme.assets.filter((item) => assetKey(item) !== key) });
}

function artForm() {
  const slot = byId('art-slot').value;
  return { slot, orientation: byId('art-orientation').value, text_size: 'any', codec: byId('art-codec').value,
    tile: slot === 'list' && byId('art-tile').checked };
}

function syncArt(loadVariant = true) {
  const slot = byId('art-slot').value;
  const tile = slot === 'list' && byId('art-tile').checked;
  const unrestricted = tile || slot === 'cassette_label';
  const orientation = byId('art-orientation');
  if (!unrestricted && orientation.value === 'any') orientation.value = view.orientation;
  const allowed = unrestricted ? [...orientations, 'any'] : orientations;
  options(orientation, allowed.map((value) => [value, value === 'any' ? 'Either' : titleCase(value)]), orientation.value);
  const codec = byId('art-codec');
  if (slot === 'now_playing') codec.value = 'indexed';
  codec.querySelector('option[value="lossless"]').disabled = slot === 'now_playing';
  byId('art-tile-label').hidden = slot !== 'list';
  const key = assetKey(artForm());
  const existing = theme.assets.find((asset) => assetKey(asset) === key);
  if (existing && loadVariant) {
    codec.value = existing.codec;
    byId('art-tile').checked = Boolean(existing.tile);
  }
  const current = artForm();
  const dims = current.tile ? '1–64 px in each direction' : slot === 'cassette_label' ? '160 × 48 px' :
    current.orientation === 'portrait' ? '222 × 480 px' : '480 × 222 px';
  byId('art-guidance').textContent = dims + (slot === 'now_playing' ? ' · Indexed only for continuous redraw during playback.' : '');
  byId('remove-art').disabled = !existing;
}

function allowedArt({ slot, tile, orientation }, width, height) {
  if (tile) return width <= 64 && height <= 64;
  if (slot === 'cassette_label') return width === 160 && height === 48;
  return orientation === 'portrait' ? width === 222 && height === 480 : width === 480 && height === 222;
}

function showAsset(target) {
  if (target.slot === 'cassette_label') setView({ screen: 'now_playing', mode: 'cassette' });
  else if (!Object.hasOwn(ICONS, target.slot)) setView({ screen: target.slot });
  if (target.orientation !== 'any') setView({ orientation: target.orientation });
}

async function addArt(file) {
  const current = artForm();
  const token = ++fileOperation;
  const pixels = await decodePng(file, allowedArt.bind(null, current), true);
  if (token !== fileOperation) return;
  const art = { ...current, ...pixels };
  const next = replaceAsset(art);
  encodeTheme(next);
  commit(next);
  showAsset(art);
  notice('Artwork added.');
}

function iconForm() {
  const slot = byId('icon-slot').value;
  return { slot, orientation: byId('icon-orientation').value, text_size: byId('icon-text-size').value,
    ...ICONS[slot], codec: 'indexed' };
}

function activeIcon() {
  const form = iconForm();
  if (!form.slot) return;
  return theme.assets.find((asset) => assetKey(asset) === assetKey(form));
}

function drawIconGrid() {
  const form = iconForm();
  if (!form.slot) return;
  const grid = byId('icon-grid');
  const zoom = Number.parseInt(byId('icon-zoom').value, 10);
  grid.width = form.width * zoom;
  grid.height = form.height * zoom;
  const context = grid.getContext('2d');
  const icon = stroke?.asset ?? activeIcon();
  for (let y = 0; y < form.height; y++) {
    for (let x = 0; x < form.width; x++) {
      const i = y * form.width + x;
      context.fillStyle = (x + y) % 2 ? '#626570' : '#41434c';
      context.fillRect(x * zoom, y * zoom, zoom, zoom);
      if (icon?.alpha[i]) {
        context.fillStyle = hexColor(icon.pixels[i]);
        context.globalAlpha = icon.alpha[i] / 15;
        context.fillRect(x * zoom, y * zoom, zoom - 1, zoom - 1);
        context.globalAlpha = 1;
      }
    }
  }
  if (grid === document.activeElement) {
    context.strokeStyle = '#ffffff';
    context.strokeRect(iconCursor.x * zoom + .5, iconCursor.y * zoom + .5, zoom - 1, zoom - 1);
  }
  byId('icon-guidance').textContent = `${form.width} × ${form.height} px${form.reserved ?
    ' · Reserved slot. Current firmware keeps its built-in drawing.' : ' · Native slot grid'}.`;
  byId('remove-icon').disabled = !icon;
}

function inkAt(x, y) {
  const { asset } = stroke;
  if (x < 0 || y < 0 || x >= asset.width || y >= asset.height) return;
  iconCursor = { x, y };
  const at = y * asset.width + x;
  asset.alpha[at] = byId('icon-erase').checked ? 0 : Number(byId('icon-opacity').value);
  asset.pixels[at] = asset.alpha[at] ? fromHex(byId('icon-color').value) : 0;
  drawIconGrid();
}

function startStroke() {
  const form = iconForm();
  const existing = activeIcon();
  stroke = { asset: { ...form, pixels: existing?.pixels.slice() ?? new Uint16Array(form.width * form.height),
    alpha: existing?.alpha.slice() ?? new Uint8Array(form.width * form.height) } };
}

function finishStroke() {
  if (!stroke) return;
  const { asset } = stroke;
  stroke = undefined;
  commit(replaceAsset(asset));
}

function iconPoint(event) {
  const { left, top, width, height } = byId('icon-grid').getBoundingClientRect();
  const form = iconForm();
  return [Math.floor((event.clientX - left) * form.width / width), Math.floor((event.clientY - top) * form.height / height)];
}

async function addIcon(file) {
  const target = iconForm();
  const token = ++fileOperation;
  const { width, height } = target;
  const image = await decodePng(file, (w, h) => w === width && h === height, false);
  if (token !== fileOperation) return;
  const next = replaceAsset({ ...target, ...image });
  encodeTheme(next);
  commit(next);
  notice('Icon added.');
}

function updateCounter(field) {
  const limit = Number(field.dataset.limit);
  const used = byteLength(field.value);
  const counter = byId(field.id + '-count');
  counter.value = `${used}/${limit} bytes`;
  counter.dataset.over = String(used > limit);
  field.setAttribute('aria-invalid', String(used > limit));
}

function initMetadata() {
  const labels = { name: 'Theme name', author: 'Author', version: 'Pack version', licence: 'Licence',
    homepage: 'Homepage URL', gallery: 'Gallery URL' };
  for (const [name, , size] of METADATA) {
    const label = create('label', '', { className: 'studio-field' });
    const caption = create('span', labels[name]);
    const input = create('input', '', { id: 'meta-' + name, type: 'text', spellcheck: false });
    const counter = create('output', '', { id: input.id + '-count', className: 'studio-counter' });
    counter.setAttribute('for', input.id);
    input.setAttribute('aria-describedby', counter.id);
    input.dataset.limit = size - 1;
    caption.append(counter);
    label.append(caption, input);
    byId('metadata-fields').append(label);
    let before;
    input.addEventListener('focus', () => { metadataField = input; });
    input.addEventListener('input', () => {
      before ??= theme;
      theme = { ...theme, [name]: input.value };
      updateCounter(input);
      byId('current-theme').textContent = theme.name || 'Untitled theme';
      updatePack();
      findings = undefined;
    });
    input.addEventListener('change', () => {
      if (!before) return;
      const next = theme;
      theme = before;
      before = undefined;
      commit(next);
    });
    input.addEventListener('blur', () => { metadataField = undefined; });
  }
}

const styleLabels = { horizon: 'Horizon (%)', sun_radius: 'Sun radius (%)', slats: 'Sun slats', floor_lines: 'Floor lines',
  rays: 'Floor rays', pulse: 'Bass pulse', time: 'Animation interval (ms)', align: 'Track title alignment', progress_height: 'Progress height (px)' };

function initStyle() {
  options(byId('base'), BASES.map((base) => [base, titleCase(base)]), theme.base);
  on('base', 'change', ({ target }) => commit({ ...theme, base: target.value }));
  for (const flag of ['horizon', 'header_rule']) {
    on(flag, 'change', ({ target }) => commit({ ...theme, [flag]: target.checked }));
  }
  for (const item of STYLES) {
    const { kind, target, values, initial } = item;
    const row = create('label', styleLabels[target], { className: 'studio-style-row studio-field' });
    const select = create('select', '', { id: 'style-' + target });
    const labelFor = (value) => target === 'align' ? ['Left', 'Center'][value] :
      target === 'pulse' ? ['Off', 'On'][value] : String(value);
    options(select, [['', `Default (${labelFor(initial)})`], ...values.map((value) => [String(value), labelFor(value)])], '');
    select.addEventListener('change', () => {
      const style = theme.style.filter((entry) => entry.target !== target || entry.kind !== kind);
      if (select.value !== '') style.push({ kind, target, value: Number(select.value) });
      commit({ ...theme, style });
      const mode = ['cover', 'cassette'].includes(view.mode) && kind === 'scene' ? 'vu' : view.mode;
      setView({ screen: 'now_playing', mode });
      showView('preview');
    });
    row.append(select);
    byId('style-controls').append(row);
  }
}

const cases = [
  ['Long title · large text', { screen: 'now_playing', textSize: 'large',
    title: 'Late Night Signals from the Farthest Side of the City, Extended Mix' }],
  ['Missing cover art', { screen: 'now_playing', missingCover: true }],
  ['Portrait layout', { screen: 'home', orientation: 'portrait' }],
  ['Selection highlight', { screen: 'list', selection: 2 }],
  ['Now Playing · Cover', { screen: 'now_playing', mode: 'cover' }],
  ['Now Playing · Cassette', { screen: 'now_playing', mode: 'cassette' }],
  ['Now Playing · VU', { screen: 'now_playing', mode: 'vu' }],
  ['Now Playing · Spectrum', { screen: 'now_playing', mode: 'spectrum' }],
];

async function preflight() {
  showView('preflight');
  const sheet = byId('contact-sheet');
  const status = byId('preflight-status');
  status.textContent = 'Rendering contact sheet…';
  const checked = theme;
  try {
    const result = encodeTheme(checked);
    decodeTheme(result.bytes);
    await Promise.all(Object.values(fontSizes).map(loadTextSize));
    if (theme !== checked) return false;
    const model = previewTheme();
    let hotspots = 0;
    const figures = cases.map(([label, options]) => {
      const output = renderEcho(model, options);
      hotspots += output.labels.filter(({ low }) => low).length;
      const figure = create('figure');
      const canvas = create('canvas');
      canvas.setAttribute('aria-label', label);
      const wrap = create('div', '', { className: 'contact-frame' });
      paintEcho(canvas, output);
      wrap.append(canvas);
      const caption = create('figcaption');
      const inspect = create('button', label, { type: 'button' });
      inspect.addEventListener('click', () => {
        setView({ screen: 'home', mode: 'cover', orientation: 'landscape', textSize: 'medium', selection: 0, ...options });
        byId('contrast').checked = true;
        showView('preview');
        byId('echo').focus({ preventScroll: true });
      });
      caption.append(inspect);
      figure.append(wrap, caption);
      return figure;
    });
    sheet.replaceChildren(...figures);
    findings = { theme: checked, hotspots };
    status.textContent = `Pack passed binary validation. ${hotspots ? hotspots + ' text regions below 4.5:1 across the sheet.' : 'No text regions below 4.5:1.'}`;
    return true;
  } catch (error) {
    if (theme !== checked) return false;
    findings = undefined;
    sheet.replaceChildren();
    status.textContent = 'Preflight failed. ' + error.message;
    return false;
  }
}

function handleHardware(key) {
  const list = { home: HOME_ROWS, settings: SETTINGS_ROWS, list: LIST_ROWS }[view.screen];
  if (key === 'up' || key === 'down') {
    const delta = key === 'up' ? -1 : 1;
    if (list) setView({ selection: (list.length + view.selection + delta) % list.length });
    else if (view.screen === 'now_playing') {
      volume = Math.max(0, Math.min(100, volume - delta * 4));
      audio.volume(volume);
      volumeUntil = performance.now() + 1600;
      setTimeout(schedulePaint, 1650);
      schedulePaint();
    }
  } else if (key === 'menu') {
    setView({ screen: view.screen === 'settings' ? 'home' : 'settings' });
  } else if (key === 'back') {
    setView({ screen: 'home' });
  } else if (view.screen === 'now_playing') {
    if (audio.buffer) toggleAudio();
    else {
      lastVisual = { ...lastVisual, playing: !lastVisual.playing };
      schedulePaint();
    }
  } else if (view.screen === 'home') {
    setView({ screen: view.selection === HOME_ROWS.length - 1 ? 'settings' : 'list' });
  } else {
    setView({ screen: 'now_playing' });
  }
}

function updateAudio() {
  const button = byId('audio-stop');
  button.disabled = !audio.buffer;
  button.textContent = audio.playing ? 'Pause' : 'Resume';
  byId('audio-status').textContent = audioTitle ? (audio.playing ? 'Playing ' : 'Paused ') + audioTitle :
    'Drop audio here to drive the visualizers.';
}

function toggleAudio() {
  if (audio.playing) audio.pause();
  else audio.resume();
  lastVisual.playing = audio.playing;
  updateAudio();
  schedulePaint();
  startAnimation();
}

function animation(now) {
  animationFrame = 0;
  if (audio.playing && !document.hidden && activeView === 'preview' && view.screen === 'now_playing') {
    const style = theme.style.find(({ kind }) => kind === 'wait_frame_ms')?.value;
    const interval = reducedMotion.matches ? 1000 : style ?? (view.mode === 'cassette' ? 80 : 40);
    if (now - lastFrame >= interval) {
      lastFrame = now;
      const position = audio.position;
      lastVisual = { playing: true, progress: position / audio.duration, elapsed: formatTime(position),
        remaining: '-' + formatTime(Math.max(0, audio.duration - position)),
        position: reducedMotion.matches ? 0 : position * 1000,
        levels: reducedMotion.matches ? undefined : audio.levels() };
      schedulePaint();
    }
  }
  startAnimation();
}

function startAnimation() {
  if (!animationFrame && audio.playing && !document.hidden &&
    activeView === 'preview' && view.screen === 'now_playing') {
    animationFrame = requestAnimationFrame(animation);
  }
}

async function useAudio(file) {
  if (file && (!file.size || file.size > 128 * 1024 * 1024)) throw new Error('Choose an audio file under 128 MiB.');
  const token = ++audioOperation;
  const current = () => token === audioOperation;
  if (file) await audio.playFile(file, current);
  else await audio.demo(current);
  if (!current()) return;
  const extension = file?.name.match(/\.([a-z0-9]{1,8})$/i)?.[1];
  audioTitle = file ? file.name.replace(/\.[^.]+$/, '') : 'Midnight Circuit';
  audioArtist = file ? 'Local audio' : 'Synth demo';
  audioFormat = extension ? extension.toUpperCase() : file ? 'Local file' : 'Web Audio';
  audio.volume(volume);
  setView({ screen: 'now_playing', mode: view.mode === 'cover' ? 'spectrum' : view.mode });
  showView('preview');
  updateAudio();
}

function selectFile(id, fn) {
  on(id, 'change', action(async ({ target }) => {
    const file = target.files[0];
    target.value = '';
    if (file) await fn(file);
  }));
}

function safelyDrop(node, recognizes, fn) {
  node.addEventListener('dragover', (event) => {
    if (!event.dataTransfer.types.includes('Files')) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = 'copy';
    node.dataset.dragging = 'true';
  });
  node.addEventListener('dragleave', ({ relatedTarget }) => {
    if (!(relatedTarget instanceof Node) || !node.contains(relatedTarget)) node.dataset.dragging = 'false';
  });
  node.addEventListener('drop', action(async (event) => {
    event.preventDefault();
    node.dataset.dragging = 'false';
    const file = [...event.dataTransfer.files].find(recognizes);
    if (file) {
      event.stopPropagation();
      await fn(file);
    }
  }));
}

async function importPack(file) {
  if (file.size < 512 || file.size > MAX_BYTES) throw new Error('Choose a .gtheme pack from 512 B to 2 MiB.');
  const token = ++fileOperation;
  const imported = decodeTheme(await file.arrayBuffer());
  if (token !== fileOperation) return;
  commit(imported);
  byId('preset').value = '';
  notice('Opened ' + imported.name + '.');
}

function fileStem() {
  return theme.name.normalize('NFKD').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'gnarlyr-theme';
}

function mustPack() {
  if (!packed) return encodeTheme(theme);
  return packed;
}

function validCreator() {
  const raw = byId('creator').value;
  const creator = validHandle(raw);
  if (raw && !creator) {
    section('details');
    byId('creator').focus();
    throw new Error('Creator handles use 2–32 letters, digits, underscores or hyphens.');
  }
  return creator;
}

function shareURL() {
  const creator = validCreator();
  const url = new URL('https://gnarlyr.com/studio.html');
  url.hash = new URLSearchParams({ p: encodeThemeLink(theme), ...(creator && { ref: creator }) });
  return url.href;
}

function submission() {
  const credit = byId('submission-credit').value.trim();
  const attested = byId('licence-attestation').checked;
  const text = [
    '## Theme gallery submission', '',
    '- Theme: ' + theme.name,
    '- Creator: ' + theme.author + (theme.creator ? ' (@' + theme.creator + ')' : ''),
    '- Version: ' + theme.version,
    '- Licence: ' + theme.licence,
    '- Remix credit: ' + (credit || 'Original work'),
    '- Theme link: ' + shareURL(),
    '- Preflight: Valid v2 pack; ' + findings.hotspots + ' text regions below 4.5:1 across the contact sheet.',
    '- Licence attestation: ' + (attested ? 'I can share every asset and have credited remixed work.' : 'Pending'),
    '', 'Attach the .gtheme pack and source ZIP here.',
  ].join('\n');
  byId('submission-summary').value = text;
  byId('gallery-issue').disabled = !attested;
  return text;
}

function initEvents() {
  all('[data-view]').forEach((button) => button.addEventListener('click', () => {
    if (button.dataset.view === 'preflight') void preflight();
    else showView('preview');
  }));
  const panels = all('[data-panel]');
  panels.forEach((tab, index) => {
    tab.addEventListener('click', () => section(tab.dataset.panel));
    tab.addEventListener('keydown', (event) => {
      const target = { ArrowLeft: (index + panels.length - 1) % panels.length,
        ArrowRight: (index + 1) % panels.length, Home: 0, End: panels.length - 1 }[event.key];
      if (target !== undefined) {
        event.preventDefault();
        section(panels[target].dataset.panel, true);
      }
    });
  });
  for (const [id, field] of [['screen', 'screen'], ['mode', 'mode'], ['orientation', 'orientation'], ['text-size', 'textSize']]) {
    on(id, 'change', ({ target }) => setView({ [field]: target.value, ...(field === 'mode' && { screen: 'now_playing' }) }));
  }
  all('[data-key]').forEach((button) => button.addEventListener('click', () => handleHardware(button.dataset.key)));
  document.addEventListener('keydown', (event) => {
    if (event.defaultPrevented || byId('gallery-dialog').open ||
      event.target.closest?.('input, select, textarea, [contenteditable]')) return;
    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'z') {
      event.preventDefault();
      if (event.shiftKey) historyMove(future, past);
      else historyMove(past, future);
      return;
    }
    if (event.altKey || event.metaKey || event.ctrlKey || activeView !== 'preview' ||
      event.target.closest?.('button, a, summary, #icon-grid')) return;
    const key = { ArrowUp: 'up', ArrowDown: 'down', Enter: 'play', Escape: 'back', m: 'menu', M: 'menu' }[event.key];
    if (key) {
      event.preventDefault();
      handleHardware(key);
    }
  });
  on('echo', 'click', (event) => {
    if (!frame) return;
    const { left, top, width, height } = event.currentTarget.getBoundingClientRect();
    highlightRole(roleAt(frame, Math.floor((event.clientX - left) * frame.width / width), Math.floor((event.clientY - top) * frame.height / height)));
  });
  on('contrast', 'change', schedulePaint);
  on('undo', 'click', () => historyMove(past, future));
  on('redo', 'click', () => historyMove(future, past));
  on('generate-palette', 'click', () => {
    commit({ ...theme, palette: autoPalette(...seeds.map((role) => fromHex(byId('seed-' + role).value))) });
  });
  on('preset', 'change', action(async ({ target }) => {
    const preset = target.value;
    if (!preset) return;
    const token = ++fileOperation;
    if (galleryPresets.includes(preset)) await galleryTheme(preset, token);
    else {
      const next = preset === 'tape' ?
        { ...builtinPalette('graphite'), palette: autoPalette(fromHex('#251b18'), fromHex('#fff3d0'), fromHex('#e9a150')) } : builtinPalette(preset);
      commit({ ...theme, ...next, name: presets[preset] });
    }
  }));
  selectFile('palette-photo', async (file) => {
    const token = ++fileOperation;
    const { background, text, accent } = photoColors(await photoPixels(file));
    if (token === fileOperation) commit({ ...theme, palette: autoPalette(background, text, accent) });
  });
  for (const id of ['art-slot', 'art-orientation']) on(id, 'change', () => syncArt(true));
  on('art-tile', 'change', () => syncArt(false));
  on('art-codec', 'change', action(() => {
    const current = artForm();
    const existing = theme.assets.find((asset) => assetKey(asset) === assetKey(current));
    if (!existing || existing.codec === current.codec) return;
    const next = replaceAsset({ ...existing, codec: current.codec });
    try {
      encodeTheme(next);
      commit(next);
    } catch (error) {
      byId('art-codec').value = existing.codec;
      throw error;
    }
  }));
  on('remove-art', 'click', () => removeAsset(assetKey(artForm())));
  selectFile('art-file', addArt);
  for (const id of ['icon-slot', 'icon-orientation', 'icon-text-size', 'icon-zoom']) {
    on(id, 'change', () => {
      iconCursor = { x: 0, y: 0 };
      drawIconGrid();
      schedulePaint();
    });
  }
  const icon = byId('icon-grid');
  icon.addEventListener('focus', drawIconGrid);
  icon.addEventListener('blur', drawIconGrid);
  icon.addEventListener('pointerdown', (event) => {
    if (event.button !== 0 || stroke) return;
    icon.focus();
    icon.setPointerCapture(event.pointerId);
    startStroke();
    inkAt(...iconPoint(event));
  });
  icon.addEventListener('pointermove', (event) => {
    if (!stroke || !icon.hasPointerCapture(event.pointerId)) return;
    const [x, y] = iconPoint(event);
    if (x === iconCursor.x && y === iconCursor.y) return;
    const { x: oldX, y: oldY } = iconCursor;
    const steps = Math.max(Math.abs(x - oldX), Math.abs(y - oldY));
    for (let step = 1; step <= steps; step++) {
      inkAt(Math.round(oldX + (x - oldX) * step / steps), Math.round(oldY + (y - oldY) * step / steps));
    }
  });
  icon.addEventListener('lostpointercapture', finishStroke);
  icon.addEventListener('keydown', (event) => {
    const delta = { ArrowUp: [0, -1], ArrowDown: [0, 1], ArrowLeft: [-1, 0], ArrowRight: [1, 0] }[event.key];
    const form = iconForm();
    if (delta) {
      event.preventDefault();
      iconCursor.x = Math.max(0, Math.min(form.width - 1, iconCursor.x + delta[0]));
      iconCursor.y = Math.max(0, Math.min(form.height - 1, iconCursor.y + delta[1]));
      drawIconGrid();
    } else if (event.key === ' ' && !stroke) {
      event.preventDefault();
      startStroke();
      inkAt(iconCursor.x, iconCursor.y);
      finishStroke();
    }
  });
  selectFile('icon-file', addIcon);
  on('remove-icon', 'click', () => removeAsset(assetKey(iconForm())));
  selectFile('pack-file', importPack);
  const packFile = (file) => /\.gtheme$/i.test(file.name);
  const audioFile = (file) => file.type.startsWith('audio/') || /\.(flac|m4a|opus|ogg|wav|mp3|aac|aiff)$/i.test(file.name);
  safelyDrop(byId('workspace'), (file) => packFile(file) || audioFile(file),
    (file) => packFile(file) ? importPack(file) : useAudio(file));
  safelyDrop(byId('audio-drop'), audioFile, useAudio);
  selectFile('audio-file', useAudio);
  on('audio-demo', 'click', action(() => useAudio()));
  on('audio-stop', 'click', toggleAudio);
  audio.onend = () => { lastVisual = { playing: false }; updateAudio(); schedulePaint(); };
  document.addEventListener('visibilitychange', startAnimation);
  reducedMotion.addEventListener('change', () => {
    lastVisual = { playing: audio.playing };
    schedulePaint();
  });
  on('creator', 'change', action(({ target }) => {
    const creator = validHandle(target.value);
    if (target.value && !creator) throw new Error('Creator handles use 2–32 letters, digits, underscores or hyphens.');
    commit({ ...theme, creator });
  }));
  on('fork-pack', 'click', () => {
    commit({ ...theme, id: newThemeId(), version: '1.0.0', remixedFrom: theme.remixedFrom ||
      `${theme.name} by ${theme.author} (${theme.id})` });
    notice('Created a separate pack identity for this remix.');
  });
  on('run-preflight', 'click', preflight);
  on('download-pack', 'click', action(() => {
    download(new Blob([mustPack().bytes], { type: 'application/octet-stream' }), fileStem() + '.gtheme');
    event('studio_export', 'gtheme');
  }));
  on('download-source', 'click', action(async () => {
    mustPack();
    const name = fileStem();
    download(await sourceZip(theme, name), name + '-source.zip');
    event('studio_export', 'source');
  }));
  on('copy-link', 'click', action(async () => {
    const link = shareURL();
    const field = byId('share-url');
    field.value = link;
    field.hidden = false;
    try {
      await navigator.clipboard.writeText(link);
      notice('Theme link copied.');
    } catch {
      field.focus();
      field.select();
      notice('The link is selected. Copy it to share.');
    }
    event('theme_share', 'link');
  }));
  on('social-card', 'click', action(async () => {
    const name = fileStem();
    const surface = create('canvas');
    const options = { ...currentStageOptions(), contrast: false };
    paintEcho(surface, renderEcho(previewTheme(), options));
    download(await showcaseCard({ ...theme, creator: validCreator() }, surface, options.orientation), name + '-showcase.png');
    event('studio_export', 'showcase');
  }));
  on('open-companion', 'click', () => event('theme_share', 'companion'));
  on('submit-gallery', 'click', action(async () => {
    validateMetadata(theme);
    validCreator();
    if (findings?.theme !== theme && !await preflight()) return;
    byId('submission-credit').value = theme.remixedFrom || '';
    byId('licence-attestation').checked = false;
    submission();
    byId('gallery-dialog').showModal();
  }));
  on('submission-credit', 'input', submission);
  on('licence-attestation', 'change', submission);
  on('gallery-issue', 'click', () => {
    const body = submission();
    const url = new URL('https://github.com/0xdoublesharp/gnarlyr/issues/new');
    url.search = new URLSearchParams({ title: 'Theme gallery: ' + theme.name, body });
    window.open(url, '_blank', 'noopener,noreferrer');
    event('theme_share', 'gallery');
    byId('gallery-dialog').close();
  });
}

async function readLocation() {
  const params = new URLSearchParams(location.hash.slice(1));
  const themeId = new URL(location.href).searchParams.get('theme');
  const link = params.get('p');
  if (link) {
    fileOperation++;
    const decoded = decodeThemeLink(link);
    commit({ ...initial, ...decoded, name: 'Shared theme', id: newThemeId() });
    notice('Shared palette restored.');
  } else if (themeId) {
    await galleryTheme(themeId, ++fileOperation);
    if (Object.hasOwn(presets, themeId)) byId('preset').value = themeId;
  }
}

function init() {
  options(byId('screen'), Object.entries(SCREENS), view.screen);
  options(byId('mode'), Object.entries(MODES), view.mode);
  options(byId('orientation'), orientations.map((name) => [name, titleCase(name)]), view.orientation);
  options(byId('text-size'), textSizes.map((name) => [name, titleCase(name)]), view.textSize);
  options(byId('art-slot'), ART_SLOTS.map((slot) => [slot, artNames[slot]]), 'home');
  options(byId('art-orientation'), orientations.map((name) => [name, titleCase(name)]), view.orientation);
  options(byId('art-codec'), [['indexed', 'Indexed'], ['lossless', 'Lossless RGB565']], 'indexed');
  options(byId('icon-slot'), Object.entries(ICONS).map(([name, { reserved }]) =>
    [name, titleCase(name.slice(5)) + (reserved ? ' (reserved)' : '')]), 'icon_music');
  options(byId('icon-orientation'), [['any', 'Either'], ...orientations.map((name) => [name, titleCase(name)])], 'any');
  options(byId('icon-text-size'), [['any', 'All'], ...textSizes.map((name) => [name, titleCase(name)])], 'any');
  initPalette();
  initMetadata();
  initStyle();
  initEvents();
  sync();
  setView(view);
  void fonts();
  void action(readLocation)();
  window.addEventListener('hashchange', action(readLocation));
}

init();
