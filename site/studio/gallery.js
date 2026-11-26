import { fetchCatalog, fetchManifest } from './catalog.js';
import { themeDeepLink } from './theme-link.js';

const profiles = document.getElementById('creator-profiles');

function append(parent, tag, text, attributes = {}) {
  const element = document.createElement(tag);
  element.textContent = text;
  for (const [name, value] of Object.entries(attributes)) element.setAttribute(name, value);
  parent.append(element);
  return element;
}

async function main() {
  const { themes, creators } = await fetchCatalog();
  for (const { handle, name, url } of creators) {
    const profile = append(profiles, 'article', '', { id: 'creator-' + handle });
    append(profile, 'h3', name);
    const credit = append(profile, 'p', '@' + handle + ' · ');
    if (url) append(credit, 'a', 'Creator website ↗', { href: url });
    const list = append(profile, 'ul', '');
    for (const theme of themes.filter(({ creator }) => creator === handle)) {
      append(append(list, 'li', ''), 'a', theme.name, { href: '#' + theme.id });
    }
  }
  for (const theme of themes) {
    let card = document.getElementById(theme.id);
    if (!card) {
      card = append(document.getElementById('catalog-themes'), 'article', '', { id: theme.id, class: 'theme-showcase-block' });
      const heading = append(card, 'div', '', { class: 'theme-showcase-head' });
      const intro = append(heading, 'div', '');
      append(intro, 'h3', theme.name);
      if (theme.description) append(intro, 'p', theme.description);
    }
    const host = card.querySelector('.theme-showcase-head > div');
    const credits = append(host, 'p', '');
    append(credits, 'a', '@' + theme.creator, { href: '#creator-' + theme.creator });
    credits.append(' · ' + theme.licence);
    if (theme.remixedFrom) {
      const parent = themes.find(({ id }) => id === theme.remixedFrom);
      if (parent) append(credits, 'a', ' · Remix of ' + parent.name, { href: '#' + parent.id });
    }
    const counts = theme.counts ?? {};
    if (Number.isInteger(counts.downloads)) credits.append(' · ' + counts.downloads.toLocaleString() + ' downloads');
    if (Number.isInteger(counts.remixes)) credits.append(' · ' + counts.remixes.toLocaleString() + ' remixes');
    const actions = append(host, 'div', '', { class: 'download-actions' });
    append(actions, 'a', 'Open in Studio / Remix', {
      class: 'button', href: 'studio.html?' + new URLSearchParams({ theme: theme.id, ref: theme.creator }),
    });
    try {
      const { manifest } = await fetchManifest(theme);
      append(actions, 'a', 'Open palette in Companion', { class: 'text-link', href: themeDeepLink(manifest) });
    } catch { /* The Studio link and screenshots remain available. */ }
  }
  profiles.parentElement.hidden = false;
}

main().catch(() => { /* Gallery screenshots and existing downloads are static. */ });
