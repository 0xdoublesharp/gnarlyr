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
const docsTitle = `${identity.name} Docs`;
const websiteId = '7776169d-25f5-4420-bed8-810b4fbacdc7';
const pages = ['index', 'firmware', 'docs', 'hardware', 'companion', 'install', 'build', 'recovery', 'languages', 'themes'];
const paths = ['/', ...pages.map((page) => `/${page}.html`)];

function visit(overrides = {}) {
  const calls = [];
  const environment = {
    location: new URL(`${site}/docs.html?token=private#secret`),
    navigator: { language: 'en-US' },
    localStorage: { getItem: () => null },
    document: {
      title: docsTitle,
      currentScript: { dataset: { domain, websiteId, paths: paths.join(',') } },
      referrer: 'https://search.example/results?q=private#secret',
    },
    URL, AbortSignal,
    fetch: (url, options) => {
      calls.push({ url, ...options, body: JSON.parse(options.body) });
      return Promise.resolve();
    },
    ...overrides,
  };
  runInNewContext(source, environment);
  return calls;
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
  const calls = visit();
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
  for (const pathname of paths) {
    assert.equal(visit({ location: new URL(`${site}${pathname}`) }).length, 1);
  }
});

test('validation hosts, local files, health probes, and unknown pages do not collect', () => {
  for (const url of [
    `https://${brand}.apps.${maker}.com/`, `http://${domain}/`,
    'http://localhost/', 'file:///site/index.html', `${site}/healthz`,
    `${site}/not-a-page`, `${site}/assets/screens/home.png`,
  ]) assert.equal(visit({ location: new URL(url) }).length, 0, url);
});

test('localized homepages collect only when explicitly listed as public routes', () => {
  const localized = ['es', 'de', 'ja', 'fr', 'pt'].flatMap((code) => [`/${code}/`, `/${code}/index.html`]);
  const document = {
    title: identity.name, referrer: '',
    currentScript: { dataset: { domain, websiteId, paths: [...paths, ...localized].join(',') } },
  };
  for (const pathname of localized) {
    const calls = visit({ document, location: new URL(`${site}${pathname}?private=1#secret`) });
    assert.equal(calls.length, 1, pathname);
    assert.equal(calls[0].body.payload.url, pathname);
    assert.doesNotMatch(JSON.stringify(calls), /private|secret/);
  }
  for (const pathname of ['/ru/', '/es/docs.html', '/i18n/es.json']) {
    assert.equal(visit({ document, location: new URL(`${site}${pathname}`) }).length, 0, pathname);
  }
});

test('privacy preferences and unavailable local storage suppress collection', () => {
  for (const overrides of [
    { navigator: { doNotTrack: '1' } }, { navigator: { doNotTrack: 'yes' } },
    { navigator: { globalPrivacyControl: true } },
    { localStorage: { getItem: () => '1' } },
    { localStorage: { getItem: () => { throw Error('denied'); } } },
  ]) assert.equal(visit(overrides).length, 0);
});

test('a failed collector does not interrupt the page', async () => {
  assert.doesNotThrow(() => visit({ fetch: () => { throw Error('offline'); } }));
  assert.doesNotThrow(() => visit({ fetch: () => Promise.reject(Error('offline')) }));
  await new Promise((resolve) => setImmediate(resolve));
});
