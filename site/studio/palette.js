import { rgb565, rgb888 } from './art-codec.js';
import { contrastRatio, interpolate, luminance } from './echo-renderer.js';
import { BUILTINS } from './builtins.js';

export const fromHex = (hex) => rgb565(...hex.slice(1).match(/../g).map((value) => Number.parseInt(value, 16)));

function legible(ink, background) {
  if (contrastRatio(ink, background) >= 4.5) return ink;
  const target = contrastRatio(65535, background) > contrastRatio(0, background) ? 65535 : 0;
  for (let step = 1; step < 16; step++) {
    const color = interpolate(ink, target, step, 16);
    if (contrastRatio(color, background) >= 4.5) return color;
  }
  return target;
}

export function autoPalette(background, text, accent) {
  const between = (from, to, step) => interpolate(from, to, step, 16);
  const selected = between(background, accent, 5);
  const header = between(background, accent, 2);
  const secondary = legible(between(background, text, 12), background);
  const tertiary = legible(between(background, text, 9), background);
  const alt = between(accent, rgb565(249, 107, 182), 9);
  return Uint16Array.of(
    background, legible(text, background), secondary, tertiary, selected, between(selected, accent, 10),
    between(accent, text, 6), legible(text, between(selected, accent, 1)), header, between(header, accent, 5),
    legible(text, header), between(background, text, 4), between(background, accent, 12),
    legible(rgb565(247, 103, 111), background), legible(rgb565(108, 223, 161), background),
    secondary, accent, alt, between(background, accent, 4), background,
  );
}

export function builtinPalette(base) {
  const { horizon, header_rule, colors } = BUILTINS[base];
  return { base, horizon, header_rule, palette: Uint16Array.from(colors.split(' '), (hex) => fromHex('#' + hex)), style: [] };
}

export function photoColors(rgba) {
  const points = [];
  const stride = Math.max(4, Math.floor(rgba.length / (4096 * 4)) * 4);
  for (let at = 0; at < rgba.length; at += stride) {
    if (rgba[at + 3] >= 128) points.push([rgba[at], rgba[at + 1], rgba[at + 2]]);
  }
  if (!points.length) throw new Error('Choose a photo with visible pixels.');
  const distance = (a, b) => a.reduce((sum, n, channel) => sum + (n - b[channel]) ** 2, 0);
  const centers = [points[Math.floor(points.length / 2)]];
  while (centers.length < 6) {
    let best = points[0];
    let furthest = -1;
    for (const point of points) {
      const nearest = Math.min(...centers.map((center) => distance(center, point)));
      if (nearest > furthest) { best = point; furthest = nearest; }
    }
    centers.push(best);
  }
  let counts;
  for (let pass = 0; pass < 7; pass++) {
    counts = new Array(centers.length).fill(0);
    const sums = centers.map(() => [0, 0, 0]);
    for (const point of points) {
      const distances = centers.map((center) => distance(center, point));
      const closest = distances.indexOf(Math.min(...distances));
      counts[closest]++;
      for (let channel = 0; channel < 3; channel++) sums[closest][channel] += point[channel];
    }
    sums.forEach((sum, i) => {
      if (counts[i]) centers[i] = sum.map((value) => Math.round(value / counts[i]));
    });
  }
  const colors = centers.flatMap((center, i) => counts[i] ? [rgb565(...center)] : []);
  const ordered = [...new Set(colors)].sort((a, b) => luminance(a) - luminance(b));
  const background = ordered[0];
  const text = legible(ordered.at(-1), background);
  const vividness = (color) => {
    const rgb = rgb888(color);
    return Math.max(...rgb) - Math.min(...rgb) + Math.min(40, contrastRatio(color, background) * 8);
  };
  const options = ordered.filter((color) => color !== background);
  const accent = options.sort((a, b) => vividness(b) - vividness(a))[0] ?? interpolate(background, text, 1, 2);
  return { background, text, accent };
}
