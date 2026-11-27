import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { runInNewContext } from 'node:vm';

const source = readFileSync(new URL('../site/analytics.js', import.meta.url), 'utf8');
const identity = JSON.parse(readFileSync(new URL('../firmware/identity.json', import.meta.url), 'utf8'));
const brand = identity.name.toLowerCase();
const maker = identity.manufacturer.toLowerCase();
const domain = `${brand}.com`;
const site = `https://${domain}`;
const repository = `https://github.com/0x${maker}/${brand}`;
const languageBuilderEvent = `${brand}:language-builder`;
const studioEvent = `${brand}:studio`;
const docsTitle = `${identity.name} Docs`;
const websiteId = '7776169d-25f5-4420-bed8-810b4fbacdc7';
const pages = ['index', 'firmware', 'docs', 'hardware', 'companion', 'install', 'build', 'recovery', 'languages', 'themes', 'comparison', 'studio'];
const paths = ['/', ...pages.map((page) => `/${page}.html`)];

function visit(overrides = {}) {
  const calls = [];
  const listeners = new Map();
  const environment = {
    location: new URL(`${site}/docs.html?token=private#secret`),
    navigator: { language: 'en-US' },
    localStorage: { getItem: () => null },
    document: {
      title: docsTitle,
      currentScript: { dataset: { domain, websiteId, paths: paths.join(',') } },
      referrer: 'https://search.example/results?q=private#secret',
    },
    URL, URLSearchParams, AbortSignal: { timeout: (ms) => ms },
    fetch: (url, options) => {
      calls.push({ url, ...options, body: JSON.parse(options.body) });
      return Promise.resolve();
    },
    ...overrides,
  };
  environment.document.addEventListener = (name, handler) => listeners.set(name, handler);
  runInNewContext(source, environment);
  return { calls, environment, emit: (type, event) => listeners.get(type)?.(event) };
}

function link(href, attributes = {}, selectors = []) {
  const anchor = {
    href: new URL(href, site).href,
    dataset: {},
    ...attributes,
    closest: (selector) => selectors.includes(selector) ? anchor : null,
  };
  return anchor;
}

function click(visit, target) {
  const event = { target, button: 0, preventDefault: () => assert.fail('analytics prevented a click') };
  visit.emit('click', event);
}

function clickLink(visit, anchor) {
  click(visit, { closest: (selector) => selector === 'a[href]' ? anchor : null });
}

function expectEvents({ calls }, events, path = '/docs.html') {
  assert.deepEqual(calls.map(({ body }) => body), [undefined, ...events].map((event) => ({
    type: 'event',
    payload: {
      website: websiteId, hostname: domain, url: path, title: docsTitle, language: 'en-US',
      referrer: 'https://search.example/',
      ...(event && { name: event[0], data: event[1] }),
    },
  })));
  for (const call of calls) {
    assert.equal(call.url, `https://umami.apps.${maker}.com/api/send`);
    assert.equal(call.method, 'POST');
    assert.equal(call.headers['Content-Type'], 'application/json');
    assert.equal(call.credentials, 'omit');
    assert.equal(call.referrerPolicy, 'no-referrer');
    assert.equal(call.keepalive, true);
    assert.equal(call.signal, 1500);
  }
}

test('each public page loads one deferred tracker with the same website and route scope', () => {
  for (const page of pages) {
    const html = readFileSync(new URL(`../site/${page}.html`, import.meta.url), 'utf8');
    const tags = html.match(/<script\b[^>]*\bsrc="analytics\.js"[^>]*><\/script>/g) ?? [];
    assert.equal(tags.length, 1, page);
    assert.match(tags[0], /\bdefer\b/);
    assert.ok(tags[0].includes(`data-website-id="${websiteId}"`));
    assert.ok(tags[0].includes(`data-domain="${domain}"`));
    assert.ok(tags[0].includes(`data-paths="${paths.join(',')}"`));
  }
});

test('a production navigation sends one pageview without URL details or credentials', () => {
  const { calls } = visit();
  assert.equal(calls.length, 1);
  assert.equal(calls[0].url, `https://umami.apps.${maker}.com/api/send`);
  assert.equal(calls[0].body.type, 'event');
  assert.equal(calls[0].body.payload.website, websiteId);
  assert.equal(calls[0].body.payload.url, '/docs.html');
  assert.equal(calls[0].body.payload.title, docsTitle);
  assert.equal(calls[0].body.payload.referrer, 'https://search.example/');
  assert.equal(calls[0].credentials, 'omit');
  assert.equal(calls[0].referrerPolicy, 'no-referrer');
  assert.doesNotMatch(JSON.stringify(calls), /private|secret|token/);
  expectEvents({ calls }, []);
  for (const pathname of paths) {
    assert.equal(visit({ location: new URL(`${site}${pathname}`) }).calls.length, 1);
  }
});

test('validation hosts, local files, health probes, and unknown pages do not collect', () => {
  for (const url of [
    `https://${brand}.apps.${maker}.com/`, `http://${domain}/`,
    'http://localhost/', 'file:///site/index.html', `${site}/healthz`,
    `${site}/not-a-page`, `${site}/assets/screens/home.png`,
  ]) {
    const session = visit({ location: new URL(url) });
    clickLink(session, link(repository));
    assert.equal(session.calls.length, 0, url);
  }
});

test('localized homepages collect only when explicitly listed as public routes', () => {
  const localized = ['es', 'de', 'ja', 'fr', 'pt'].flatMap((code) => [`/${code}/`, `/${code}/index.html`]);
  const document = {
    title: identity.name, referrer: '',
    currentScript: { dataset: { domain, websiteId, paths: [...paths, ...localized].join(',') } },
  };
  for (const pathname of localized) {
    const { calls } = visit({ document, location: new URL(`${site}${pathname}?private=1#secret`) });
    assert.equal(calls.length, 1, pathname);
    assert.equal(calls[0].body.payload.url, pathname);
    assert.doesNotMatch(JSON.stringify(calls), /private|secret/);
  }
  for (const pathname of ['/ru/', '/es/docs.html', '/i18n/es.json']) {
    assert.equal(visit({ document, location: new URL(`${site}${pathname}`) }).calls.length, 0, pathname);
  }
});

test('privacy preferences and unavailable local storage suppress collection', () => {
  for (const overrides of [
    { navigator: { doNotTrack: '1' } }, { navigator: { doNotTrack: 'yes' } },
    { navigator: { globalPrivacyControl: true } },
    { localStorage: { getItem: () => '1' } },
    { localStorage: { getItem: () => { throw Error('denied'); } } },
    { localStorage: new Proxy({}, { get() { throw Error('denied'); } }) },
  ]) {
    const session = visit({ location: new URL(site + '/languages.html'), ...overrides });
    clickLink(session, link(repository));
    session.emit(languageBuilderEvent, { detail: { action: 'download-pack', code: 'es' } });
    assert.equal(session.calls.length, 0);
  }
});

test('changing a privacy preference applies to subsequent interactions', () => {
  let disabled = null;
  const session = visit({
    location: new URL(site + '/comparison.html'),
    localStorage: { getItem: (key) => key === 'umami.disabled' ? disabled : null },
  });
  disabled = '1';
  const target = { tagName: 'SELECT', id: 'comparison-players', value: 'echo' };
  session.emit('change', { target });
  clickLink(session, link(repository));
  expectEvents(session, [], '/comparison.html');
});

test('a failed collector does not interrupt the page', async () => {
  for (const fail of [
    () => { throw Error('offline'); },
    () => Promise.reject(Error('offline')),
  ]) {
    let attempts = 0;
    const session = visit({ fetch: () => { attempts++; return fail(); } });
    assert.doesNotThrow(() => clickLink(session, link(repository)));
    assert.equal(attempts, 2);
  }
  await new Promise((resolve) => setImmediate(resolve));
});

test('release and direct download clicks send one event with artifact metadata', () => {
  const session = visit();
  const releases = `${repository}/releases/download/`;
  const firmwareTag = 'v1.0.0-beta.1';
  const desktopTag = 'desktop-v1.0.0-beta.1';
  const downloads = [
    [desktopTag, `${brand}-desktop-macos-arm64.dmg`, 'companion', 'macos-arm64'],
    [desktopTag, `${brand}-desktop-macos-arm64.zip`, 'companion', 'macos-arm64'],
    [desktopTag, `${brand}-desktop-windows-x64.exe`, 'companion', 'windows-x64'],
    [desktopTag, `${brand}-desktop-linux-x86_64.AppImage`, 'companion', 'linux-x86_64'],
    [firmwareTag, 'ECHOECHO.img', 'firmware', 'echo'],
    ['direct', identity.update_file, 'firmware', 'echo'],
    [firmwareTag, 'vice-86.gtheme', 'theme', 'all'],
    ['direct', 'es.pack', 'language-pack', 'all'],
    [firmwareTag, 'SHA256SUMS', 'checksum', 'all'],
    [firmwareTag, `${brand}-source.tar.gz`, 'source', 'all'],
    [firmwareTag, 'firmware.json', 'other', 'all'],
    ['direct', 'examples.zip', 'other', 'all'],
  ];
  for (const [version, asset] of downloads) {
    const url = version === 'direct' ? '/files/' : releases + version + '/';
    clickLink(session, link(url + asset + '?lookup=private#secret'));
  }
  clickLink(session, link(`${repository}/archive/refs/tags/v1.2.3.zip`));
  expectEvents(session, [
    ...downloads.map(([version, asset, kind, platform]) => ['download', { kind, asset, platform, version }]),
    ['download', { kind: 'source', asset: 'v1.2.3.zip', platform: 'all', version: 'v1.2.3' }],
  ]);
});

test('retailer clicks report the product and placement without the offer or affiliate URL', () => {
  const cases = [
    ['/', 'https://www.amazon.com/dp/B0GK61Y1FD', ['.hero'], 'amazon', 'hero', 'echo'],
    ['/hardware.html', 'https://s.click.aliexpress.com/e/_c3l6aw9p', [], 'aliexpress', 'hardware', 'echo'],
    ['/install.html', 'https://www.aliexpress.us/item/echo-mini', [], 'aliexpress', 'install', 'echo-mini'],
    ['/comparison.html', 'https://www.amazon.com/dp/B0FCRML8L5', [], 'amazon', 'comparison', 'echo-mini'],
    ['/comparison.html', 'https://www.amazon.com/dp/B0DKTBW4NQ', [], 'amazon', 'comparison', 'hiby-r1'],
    ['/comparison.html', 'https://www.amazon.com/stores/SHANLING/page/EE647722-A4F9-491E-A153-A0DCE5A582F3', [], 'amazon', 'comparison', 'shanling-q2'],
    ['/install.html', 'https://s.click.aliexpress.com/e/_c3RytQbH', ['footer'], 'aliexpress', 'footer', 'echo'],
    ['/docs.html', 'https://aliexpress.com/item/hiby-r1', [], 'aliexpress', 'other', 'hiby-r1'],
  ];
  for (const [path, href, selectors, retailer, placement, product] of cases) {
    const session = visit({ location: new URL(site + path) });
    clickLink(session, link(href + `?tag=${maker}-20#private`, {}, selectors));
    expectEvents(session, [['retailer_click', { retailer, placement, product }]], path);
  }
});

test('outbound links send one category and hostname without the destination path', () => {
  const session = visit();
  const links = [
    [`${repository}/releases/tag/v1.0.0-beta.1`, 'github-releases'],
    [`${repository}/issues/new`, 'github-issues'],
    [`${repository}/blob/main/firmware/README.md`, 'github-source'],
    [`https://github.com/sponsors/${maker}`, 'sponsor'],
    [`https://venmo.com/u/${maker}`, 'sponsor'],
    ['https://forum.fiio.com/note/showNoteContent.do', 'fiio'],
    ['https://podium-echo.online/changelog', 'community'],
    ['https://github.com/BasilSkyWalk/echopod/releases', 'community'],
    ['https://brnomt.github.io/reChord', 'community'],
    ['https://github.com/brnomt/ReChord', 'community'],
    ['https://reference.example/private', 'reference'],
    ['https://amazon.com.example/private', 'reference'],
  ];
  for (const [href] of links) clickLink(session, link(href + '?token=private#secret'));
  for (const href of ['#internal', '/install.html', 'mailto:private@example.com', `blob:${site}/private`]) {
    clickLink(session, link(href));
  }
  expectEvents(session, links.map(([href, category]) => ['outbound_link', { category, host: new URL(href).hostname }]));
});

test('preview tabs, copy controls, screenshots, and locale links send authored identifiers only', () => {
  const session = visit();
  const tabs = [['now-playing', 'spectrum'], ['themes', 'vice-86'], ['hardware', 'hw-home'], ['companion', 'c-dark'], ['', 'example']];
  for (const [deck, tab] of tabs) {
    const button = {
      dataset: { deckTab: tab },
      closest: (selector) => selector === '[data-deck]' ? { dataset: { deck } } : null,
    };
    click(session, { closest: (selector) => selector === '[data-deck-tab]' ? button : null });
  }
  click(session, { closest: (selector) => selector === '[data-copy]' ? { dataset: { copy: 'verify-command' }, textContent: 'secret command' } : null });
  clickLink(session, link('/assets/screens/home.png?cache=private#secret', {}, ['a.screen-image, a.desktop-shot']));
  clickLink(session, link('/assets/companion/library-dark.png#secret', {}, ['a.screen-image, a.desktop-shot']));
  clickLink(session, link('/assets/companion/readme.txt', {}, ['a.screen-image, a.desktop-shot']));
  for (const to of ['en', 'es', 'de', 'ja', 'fr', 'pt']) {
    clickLink(session, link(to === 'en' ? '/' : `/${to}/?token=private#secret`, { hreflang: to }, ['.language-picker a[hreflang]']));
  }
  clickLink(session, link('/ru/', { hreflang: 'ru' }, ['.language-picker a[hreflang]']));
  clickLink(session, link('/es/', { hreflang: 'es', ariaCurrent: 'page' }, ['.language-picker a[hreflang]']));
  clickLink(session, link('/es/', { hreflang: 'es' }, []));
  expectEvents(session, [
    ...tabs.map(([deck, tab]) => ['preview_tab', { deck: deck || 'deck', tab }]),
    ['command_copy', { target: 'verify-command' }],
    ['lightbox_open', { image: 'home.png' }],
    ['lightbox_open', { image: 'library-dark.png' }],
    ...['en', 'es', 'de', 'ja', 'fr', 'pt'].map((to) => ['language_switch', { to }]),
  ]);
});

test('comparison selects accept only their fixed values on the comparison page', () => {
  const session = visit({ location: new URL(site + '/comparison.html') });
  const controls = {
    players: ['all', 'echo', 'mini', 'hardware'],
    priority: ['even', 'library', 'session', 'audio', 'connection'],
  };
  const otherPage = visit();
  for (const [control, values] of Object.entries(controls)) {
    for (const value of [...values, 'private text', 'unknown-code']) {
      const target = { tagName: 'SELECT', id: `comparison-${control}`, value };
      session.emit('change', { target });
      otherPage.emit('change', { target });
    }
  }
  session.emit('change', { target: { tagName: 'INPUT', id: 'comparison-players', value: 'all' } });
  expectEvents(session, Object.entries(controls).flatMap(([control, values]) =>
    values.map((value) => ['comparison_filter', { control, value }])), '/comparison.html');
  expectEvents(otherPage, []);
});

test('the language builder bridge sends only its fixed action and a short normalized code', () => {
  const session = visit({ location: new URL(site + '/languages.html') });
  const otherPage = visit();
  for (const [action, code] of [
    ['load-starter', 'es'], ['download-pack', 'pt-BR'], ['download-pack', 'alice'], ['download-pack', 'en-ZX'],
    ['export-secret', 'es'],
    ['download-pack', 'contains user text'], ['download-pack', 'toolong99'],
    ['download-pack', 'x'], ['download-pack', 'es\n'], ['download-pack', 'es?private'],
  ]) {
    const event = { detail: { action, code, text: 'private translations', file: 'secret.pack' } };
    session.emit(languageBuilderEvent, event);
    otherPage.emit(languageBuilderEvent, event);
  }
  expectEvents(session, [
    ['language_builder', { action: 'load-starter', code: 'es' }],
    ['language_builder', { action: 'download-pack', code: 'pt-br' }],
    ['language_builder', { action: 'download-pack', code: 'custom' }],
    ['language_builder', { action: 'download-pack', code: 'custom' }],
  ], '/languages.html');
  expectEvents(otherPage, []);
});

test('the language builder reports downloaded packs and verified official imports', async () => {
  const fields = new Map();
  const downloads = [];
  const session = visit({ location: new URL(site + '/languages.html') });
  const { environment } = session;
  let digest = Buffer.alloc(32);
  Object.assign(environment, {
    Blob, TextEncoder, TextDecoder,
    setTimeout: (fn) => fn(),
    CustomEvent: class { constructor(type, { detail }) { Object.assign(this, { type, detail }); } },
    crypto: { subtle: { digest: async () => digest } },
  });
  Object.assign(environment.document, {
    getElementById: (id) => {
      if (!fields.has(id)) {
        const handlers = new Map();
        fields.set(id, {
          addEventListener: (type, handler) => handlers.set(type, handler),
          emit: (type, event) => handlers.get(type)(event),
          focus() {},
          scrollIntoView() {},
        });
      }
      return fields.get(id);
    },
    dispatchEvent: (event) => session.emit(event.type, event),
    createElement: () => ({ click() { downloads.push(this.download); }, remove() {} }),
    body: { append() {} },
  });
  for (const script of ['language-catalog.js', 'language-packs.js']) {
    runInNewContext(readFileSync(new URL(`../site/${script}`, import.meta.url), 'utf8'), environment);
  }

  environment.document.getElementById('language-code').value = 'es';
  environment.document.getElementById('language-name').value = 'Private language name';
  environment.document.getElementById('translation-text').value = 'STR_SETTINGS = Private translation';
  fields.get('language-builder').emit('submit', { preventDefault() {} });
  expectEvents(session, [], '/languages.html');
  assert.doesNotMatch(fields.get('pack-summary').textContent, /extra\.font/);
  fields.get('download-pack').emit('click');
  assert.deepEqual(downloads, ['es.pack']);
  const downloadEvent = ['language_builder', { action: 'download-pack', code: 'es' }];
  expectEvents(session, [downloadEvent], '/languages.html');

  const pack = environment.LanguagePacks.buildPack('es', 'Español', '');
  const file = { size: pack.byteLength, arrayBuffer: async () => pack.buffer };
  await fields.get('pack-file').emit('change', { target: { files: [file] } });
  assert.doesNotMatch(fields.get('inspection-status').textContent, /extra\.font/);
  fields.get('edit-pack').emit('click');
  await new Promise((resolve) => setImmediate(resolve));
  expectEvents(session, [downloadEvent], '/languages.html');

  digest = Buffer.from('10596e03145a575910d120ddd8c8c7a78ea64d23501c78f3eddd5193ed5974f3', 'hex');
  await fields.get('pack-file').emit('change', { target: { files: [file] } });
  fields.get('edit-pack').emit('click');
  await new Promise((resolve) => setImmediate(resolve));
  expectEvents(session, [
    downloadEvent,
    ['language_builder', { action: 'load-starter', code: 'es' }],
  ], '/languages.html');

  fields.get('language-code').value = 'pl';
  fields.get('language-name').value = 'Polski';
  fields.get('translation-text').value = 'STR_CHIP_VOL = Głośność';
  fields.get('language-builder').emit('submit', { preventDefault() {} });
  const extraFontHelp = /needs .*extra\.font.*USB.*Companion.*python3 tools\/echo_sync\.py/;
  assert.match(fields.get('pack-summary').textContent, extraFontHelp);
  assert.equal(fields.get('download-pack').disabled, false);
  const polish = environment.LanguagePacks.buildPack('pl', 'Polski', 'STR_CHIP_VOL = Głośność');
  const polishFile = { size: polish.byteLength, arrayBuffer: async () => polish.buffer };
  await fields.get('pack-file').emit('change', { target: { files: [polishFile] } });
  assert.match(fields.get('inspection-status').textContent, extraFontHelp);
  assert.equal(fields.get('edit-pack').disabled, false);
});

test('creator referrals persist per visit and only attach to download and Studio export events', () => {
  const values = new Map();
  const sessionStorage = { getItem: (key) => values.get(key), setItem: (key, value) => values.set(key, value) };
  const arrival = visit({ location: new URL(`${site}/themes.html?ref=Pocket_Scope`), sessionStorage });
  expectEvents(arrival, [], '/themes.html');
  const studio = visit({ location: new URL(`${site}/studio.html#p=opaque&ref=invalid%20handle`), sessionStorage });
  studio.emit(studioEvent, { detail: { name: 'studio_export', format: 'gtheme', private: 'local-file' } });
  studio.emit(studioEvent, { detail: { name: 'theme_share', format: 'link' } });
  studio.emit(studioEvent, { detail: { name: 'studio_export', format: 'local-file' } });
  clickLink(studio, link('/download/test.gtheme'));
  expectEvents(studio, [
    ['studio_export', { format: 'gtheme', ref: 'pocket_scope' }],
    ['theme_share', { format: 'link', ref: 'pocket_scope' }],
    ['download', { kind: 'theme', asset: 'test.gtheme', platform: 'all', version: 'direct', ref: 'pocket_scope' }],
  ], '/studio.html');
  assert.doesNotMatch(JSON.stringify(studio.calls), /opaque|local-file|invalid/);
});

test('referral handles reject invalid characters and are never stored when tracking is disabled', () => {
  for (const handle of ['a', 'a'.repeat(33), 'email@example.org', 'path/name', 'name\n', 'name ']) {
    const sessionStorage = { getItem: () => handle, setItem: () => assert.fail('invalid ref stored') };
    const session = visit({ location: new URL(`${site}/studio.html?ref=${encodeURIComponent(handle)}`), sessionStorage });
    session.emit(studioEvent, { detail: { name: 'theme_share', format: 'link' } });
    expectEvents(session, [['theme_share', { format: 'link' }]], '/studio.html');
  }
  const sessionStorage = { getItem: () => assert.fail('opt-out read'), setItem: () => assert.fail('opt-out write') };
  const disabled = visit({ location: new URL(`${site}/studio.html?ref=valid`), navigator: { doNotTrack: '1' }, sessionStorage });
  assert.equal(disabled.calls.length, 0);
});


test('shared theme links attribute only the sanitized handle, even when session storage is unavailable', () => {
  const session = visit({
    location: new URL(`${site}/studio.html#p=palette_token&ref=Mix_Tape`),
    sessionStorage: { getItem: () => { throw new Error('storage unavailable'); } },
  });
  session.emit(studioEvent, { detail: { name: 'theme_share', format: 'companion' } });
  expectEvents(session, [['theme_share', { format: 'companion', ref: 'mix_tape' }]], '/studio.html');
  assert.doesNotMatch(JSON.stringify(session.calls), /palette_token/);
});
