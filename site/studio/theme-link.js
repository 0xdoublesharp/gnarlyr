import { BASES, readPalette, STYLES, styleValues } from './gtheme-codec.js';

const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';
const length = 60;

// Forty palette bytes plus a 40-bit mixed-radix field. Zero reserves "inherit"
// independently for each v2 style override.
export function encodeThemeLink(theme) {
  const base = BASES.indexOf(theme.base);
  if (base < 0) throw new Error('Invalid base theme');
  const style = styleValues(theme.style);
  let controls = BigInt(base * 4 + (theme.horizon ? 1 : 0) + (theme.header_rule ? 2 : 0));
  for (const item of STYLES) {
    controls = controls * BigInt(item.values.length + 1) + BigInt(item.values.indexOf(style.get(item)) + 1);
  }
  if (controls >= (1n << 40n)) throw new Error('Theme controls exceed the v2 link capacity');
  let bits = readPalette(theme.palette).reduce((all, color) => (all << 16n) | BigInt(color), 0n) << 40n | controls;
  let token = '';
  for (let i = 0; i < length; i++) {
    token = alphabet[Number(bits & 63n)] + token;
    bits >>= 6n;
  }
  return token;
}

export function decodeThemeLink(token) {
  if (typeof token !== 'string' || !/^[A-Za-z0-9_-]{60}$/.test(token)) throw new Error('Invalid theme link');
  let bits = [...token].reduce((all, char) => (all << 6n) | BigInt(alphabet.indexOf(char)), 0n);
  let controls = bits & ((1n << 40n) - 1n);
  bits >>= 40n;
  const palette = new Uint16Array(20);
  for (let i = palette.length - 1; i >= 0; i--) {
    palette[i] = Number(bits & 65535n);
    bits >>= 16n;
  }
  const style = [];
  for (const item of [...STYLES].reverse()) {
    const radix = BigInt(item.values.length + 1);
    const index = Number(controls % radix);
    controls /= radix;
    if (index) style.unshift({ kind: item.kind, target: item.target, value: item.values[index - 1] });
  }
  if (controls >= 20n) throw new Error('Unknown theme link version or invalid controls');
  const flags = Number(controls);
  return { palette, base: BASES[flags >>> 2], horizon: Boolean(flags & 1), header_rule: Boolean(flags & 2), style };
}

export const themeDeepLink = (theme) => 'gnarlyr://theme?p=' + encodeThemeLink(theme);
