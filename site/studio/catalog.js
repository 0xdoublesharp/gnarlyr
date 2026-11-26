import { readPalette } from './gtheme-codec.js';

const siteRoot = new URL('../', import.meta.url);
let catalog;
export async function fetchCatalog() {
  if (!catalog) {
    const response = await fetch(new URL('themes-catalog.json', siteRoot));
    if (!response.ok) throw new Error('The theme gallery is unavailable.');
    const data = await response.json();
    if (data.version !== 1) throw new Error('Unknown theme gallery version.');
    catalog = data;
  }
  return catalog;
}

export async function fetchManifest(theme) {
  const pathname = `studio/examples/${theme.id}/theme.json`;
  if (!/^[a-z0-9-]+$/.test(theme.id) || theme.source !== pathname) throw new Error('Unknown gallery theme source.');
  const url = new URL(pathname, siteRoot);
  const response = await fetch(url);
  if (!response.ok) throw new Error(`Could not load ${theme.name}.`);
  const manifest = await response.json();
  return { manifest: { ...manifest, palette: readPalette(manifest.palette) }, url };
}
