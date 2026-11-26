// Production-only Umami pageviews and interactions.
(() => {
  const config = document.currentScript?.dataset;
  if (!config || location.protocol !== 'https:' || location.hostname !== config.domain) return;
  const allowedPaths = config.paths?.split(',') || [];
  if (!allowedPaths.includes(location.pathname)) return;

  let referrer = '';
  try {
    const source = new URL(document.referrer);
    if (source.protocol === 'https:' || source.protocol === 'http:') referrer = source.origin + '/';
  } catch { /* Direct navigation. */ }

  const page = {
    website: config.websiteId,
    hostname: location.hostname,
    url: location.pathname,
    title: document.title,
    language: navigator.language,
    referrer,
  };

  function send(name, data) {
    try {
      if (navigator.doNotTrack === '1' || navigator.doNotTrack === 'yes' ||
          navigator.globalPrivacyControl || localStorage.getItem('umami.disabled')) return;

      const payload = name ? { ...page, name, data } : page;
      void fetch('https://umami.apps.doublesharp.com/api/send', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        credentials: 'omit',
        referrerPolicy: 'no-referrer',
        keepalive: true,
        signal: AbortSignal.timeout(1500),
        body: JSON.stringify({ type: 'event', payload }),
      }).catch(() => {});
    } catch { /* Analytics must not interrupt the site. */ }
  }

  const localeCodes = ['en', 'es', 'de', 'ja', 'fr', 'pt'];
  const deckNames = ['now-playing', 'themes', 'hardware', 'companion'];
  const controlValues = {
    players: ['all', 'echo', 'mini', 'hardware'],
    priority: ['even', 'library', 'session', 'audio', 'connection'],
  };
  const retailerProducts = new Map([
    ['/dp/b0gk61y1fd', 'echo'],
    ['/dp/b0fcrml8l5', 'echo-mini'],
    ['/dp/b0dktbw4nq', 'hiby-r1'],
    ['/stores/shanling/page/ee647722-a4f9-491e-a153-a0dce5a582f3', 'shanling-q2'],
    ['/e/_c3rytqbh', 'echo'],
    ['/e/_c3l6aw9p', 'echo'],
    ['/e/_c3vny3ex', 'echo'],
  ]);
  const productNames = ['echo-mini', 'hiby-r1', 'shanling-q2', 'echo'];
  const isHost = (host, domain) => host === domain || host.endsWith('.' + domain);
  const assetName = (value) => /^[a-z0-9][a-z0-9._-]{0,199}$/i.test(value);
  const controlName = (value) => typeof value === 'string' && /^[a-z0-9-]{1,60}$/.test(value);

  function languageCode(code) {
    const normalized = code.toLowerCase();
    const parts = normalized.match(/^([a-z]{2,3})(?:-([a-z]{2}))?$/);
    if (!parts) return 'custom';
    const [, language, region] = parts;
    try {
      const languages = new Intl.DisplayNames('en', { type: 'language', fallback: 'none' });
      const regions = new Intl.DisplayNames('en', { type: 'region', fallback: 'none' });
      if (languages.of(language) && (!region || regions.of(region.toUpperCase()))) return normalized;
    } catch { /* Missing locale data leaves the code in the generic bucket. */ }
    return 'custom';
  }

  function getDownload(url, asset) {
    const github = url.hostname === 'github.com';
    const release = github ? url.pathname.match(/^\/[^/]+\/[^/]+\/releases\/download\/([^/]+)\/[^/]+$/) : null;
    const archive = github && /^\/[^/]+\/[^/]+\/archive\/.+\.(?:zip|tar\.gz)$/i.test(url.pathname);
    const file = asset.toLowerCase();
    const source = archive || /(?:^|[-_])source\.(?:zip|tar\.gz)$/.test(file);
    if (!release && !source && !/^sha256sums$|\.(?:dmg|zip|exe|appimage|img|gtheme|pack)$/.test(file)) return null;

    let kind = 'other';
    let platform = 'all';
    if (file === 'sha256sums') kind = 'checksum';
    else if (file.endsWith('.img')) {
      kind = 'firmware';
      platform = 'echo';
    } else if (file.endsWith('.gtheme')) kind = 'theme';
    else if (file.endsWith('.pack')) kind = 'language-pack';
    else if (source) kind = 'source';
    else if (/\.(?:dmg|exe|appimage)$/.test(file) || file.startsWith('gnarlyr-desktop-') ||
             release?.[1].startsWith('desktop-')) {
      kind = 'companion';
      platform = ['macos-arm64', 'windows-x64', 'linux-x86_64'].find((name) => file.includes(name)) || 'all';
    }

    const tag = release?.[1] || (archive && url.pathname.match(/\/archive\/refs\/tags\/([^/]+)\.(?:zip|tar\.gz)$/i)?.[1]);
    return { kind, asset, platform, version: tag && assetName(tag) ? tag : 'direct' };
  }

  function retailerPlacement(link) {
    if (link.closest('footer')) return 'footer';
    if (page.url === '/comparison.html') return 'comparison';
    if (page.url === '/install.html') return 'install';
    if (link.closest('.hero')) return 'hero';
    if (page.url === '/hardware.html' || link.closest('.home-hardware')) return 'hardware';
    return 'other';
  }

  function outboundCategory(url) {
    const host = url.hostname;
    const [owner, repo, area] = url.pathname.toLowerCase().split('/').slice(1);
    if (isHost(host, 'venmo.com') || (host === 'github.com' && owner === 'sponsors')) return 'sponsor';
    if (isHost(host, 'fiio.com')) return 'fiio';
    if (isHost(host, 'podium-echo.online') || (host === 'brnomt.github.io' && owner === 'rechord') ||
        (host === 'github.com' && ['echopod', 'rechord', 'q2-pod'].includes(repo))) return 'community';
    if (host === 'github.com') {
      if (area === 'releases') return 'github-releases';
      if (area === 'issues') return 'github-issues';
      return 'github-source';
    }
    return 'reference';
  }

  function trackLink(link, event) {
    const url = new URL(link.href);
    if (url.protocol !== 'https:' && url.protocol !== 'http:') return;

    if (link.closest('.language-picker a[hreflang]')) {
      const to = link.hreflang;
      const matchesLocale = to === 'en' ? allowedPaths.includes(url.pathname) &&
        /^\/(?:[^/]+\.html)?$/.test(url.pathname) : url.pathname === `/${to}/`;
      if (link.ariaCurrent !== 'page' && url.hostname === config.domain && localeCodes.includes(to) && matchesLocale) {
        send('language_switch', { to });
      }
      return;
    }

    let retailer;
    if (isHost(url.hostname, 'amazon.com')) retailer = 'amazon';
    else if (isHost(url.hostname, 'aliexpress.com') || isHost(url.hostname, 'aliexpress.us')) retailer = 'aliexpress';
    if (retailer) {
      const path = url.pathname.toLowerCase().replace(/\/$/, '');
      const product = retailerProducts.get(path) || retailerProducts.get(path.match(/\/dp\/[^/]+$/)?.[0]) ||
        productNames.find((name) => link.id === `buy-${name}` || path.split('/').includes(name));
      if (product) send('retailer_click', { retailer, placement: retailerPlacement(link), product });
      return;
    }

    const asset = url.pathname.split('/').pop();
    if (assetName(asset)) {
      const download = getDownload(url, asset);
      if (download) {
        send('download', download);
        return;
      }
      if (link.closest('a.screen-image, a.desktop-shot') && /\.(?:png|jpe?g|webp|avif|gif)$/i.test(asset)) {
        if (!event.defaultPrevented && !event.metaKey && !event.ctrlKey && !event.shiftKey && !event.altKey) {
          send('lightbox_open', { image: asset });
        }
        return;
      }
    }

    if (url.hostname !== config.domain) send('outbound_link', { category: outboundCategory(url), host: url.hostname });
  }

  document.addEventListener('click', (event) => {
    if (event.button !== 0) return;
    const target = event.target;
    const tab = target?.closest?.('[data-deck-tab]');
    if (tab) {
      const deck = tab.closest('[data-deck]')?.dataset.deck;
      if (controlName(tab.dataset.deckTab)) {
        send('preview_tab', { deck: deckNames.includes(deck) ? deck : 'deck', tab: tab.dataset.deckTab });
      }
      return;
    }
    const button = target?.closest?.('[data-copy]');
    if (button) {
      if (controlName(button.dataset.copy)) send('command_copy', { target: button.dataset.copy });
      return;
    }
    const link = target?.closest?.('a[href]');
    if (link) {
      try {
        trackLink(link, event);
      } catch { /* Malformed links must not interrupt a click. */ }
    }
  }, { capture: true, passive: true });

  if (page.url === '/comparison.html') {
    document.addEventListener('change', ({ target }) => {
      if (target?.tagName !== 'SELECT') return;
      const control = target.id === 'comparison-players' ? 'players' :
        target.id === 'comparison-priority' ? 'priority' : null;
      if (control && controlValues[control].includes(target.value)) {
        send('comparison_filter', { control, value: target.value });
      }
    });
  }

  if (page.url === '/languages.html') {
    document.addEventListener('gnarlyr:language-builder', ({ detail }) => {
      const { action, code } = detail || {};
      if ((action === 'load-starter' || action === 'download-pack') && typeof code === 'string' &&
          code.length >= 2 && code.length <= 8 && !/[^a-z0-9-]/i.test(code)) {
        send('language_builder', { action, code: languageCode(code) });
      }
    });
  }

  send();
})();
