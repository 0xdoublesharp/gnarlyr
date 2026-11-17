// Production-only Umami pageviews, with URL details kept out of analytics.
(() => {
  try {
    const config = document.currentScript.dataset;
    if (location.protocol !== 'https:' || location.hostname !== config.domain) return;
    if (!config.paths.split(',').includes(location.pathname)) return;
    if (navigator.doNotTrack === '1' || navigator.doNotTrack === 'yes' ||
        navigator.globalPrivacyControl || localStorage.getItem('umami.disabled')) return;

    let referrer = '';
    try {
      const source = new URL(document.referrer);
      if (source.protocol === 'https:' || source.protocol === 'http:') referrer = source.origin + '/';
    } catch { /* Direct navigation. */ }

    void fetch('https://umami.apps.doublesharp.com/api/send', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      credentials: 'omit',
      referrerPolicy: 'no-referrer',
      keepalive: true,
      signal: AbortSignal.timeout(1500),
      body: JSON.stringify({ type: 'event', payload: {
        website: config.websiteId,
        hostname: location.hostname,
        url: location.pathname,
        title: document.title,
        language: navigator.language,
        referrer,
      } }),
    }).catch(() => {});
  } catch { /* Analytics must not interrupt the site. */ }
})();
