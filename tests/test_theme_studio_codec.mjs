import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';

const studio = new URL('../site/studio/', import.meta.url);
const root = fileURLToPath(new URL('../', import.meta.url));
const scratch = '/Volumes/Scratch/SnowskyEcho/tmp/theme-studio';
const python = process.env.THEME_STUDIO_PYTHON || 'python3';
const validator = join(root, 'firmware/tools/theme_pack.py');

function temporaryDirectory() {
  assert.ok(existsSync('/Volumes/Scratch'), 'Mount Scratch before these checks');
  mkdirSync(scratch, { recursive: true });
  return mkdtempSync(join(scratch, 'studio-codec-'));
}

const codec = () => import(new URL('gtheme-codec.js', studio));

const theme = {
  id: 'ffccabcd-2000-4000-8000-000000000001', base: 'neon', horizon: true, header_rule: false,
  name: '夏のテープ', author: 'Test creator', version: '1.2.65535', licence: 'CC0-1.0',
  homepage: 'https://example.org/', gallery: '',
  palette: Uint16Array.from({ length: 20 }, (_, n) => n * 3217),
};

function asset(slot, width, height, options = {}) {
  const pixels = Uint16Array.from({ length: width * height }, (_, n) =>
    options.photo ? ((n * 79) ^ (n >> 1)) & 65535 : [0, 0xffdf, 0xffdf, 0xffdf, 0x0324][(n >> 4) % 5]);
  const alpha = new Uint8Array(pixels.length).fill(15);
  if (slot.startsWith('icon_')) alpha.set([0, 1, 2, 7, 14, 15]);
  const { photo, ...metadata } = options;
  return { slot, width, height, orientation: 'landscape', text_size: 'any', codec: 'indexed', pixels, alpha, ...metadata };
}

const styles = [
  { kind: 'scene', target: 'horizon', value: 75 },
  { kind: 'scene', target: 'sun_radius', value: 35 },
  { kind: 'scene', target: 'slats', value: 8 },
  { kind: 'scene', target: 'floor_lines', value: 10 },
  { kind: 'scene', target: 'rays', value: 16 },
  { kind: 'scene', target: 'pulse', value: 1 },
  { kind: 'wait_frame_ms', target: 'time', value: 500 },
  { kind: 'now_playing', target: 'align', value: 1 },
  { kind: 'now_playing', target: 'progress_height', value: 8 },
];

test('browser encoder round-trips each v2 asset through the Python CLI', async () => {
  const { encodeTheme, decodeTheme } = await codec();
  const { blend565 } = await import(new URL('art-codec.js', studio));
  const cases = [
    ['palette', []],
    ['indexed', [asset('now_playing', 480, 222, { photo: true })]],
    ['lossless', [asset('home', 480, 222, { codec: 'lossless', photo: true }),
      asset('splash', 222, 480, { codec: 'lossless', orientation: 'portrait' })]],
    ['tile', [asset('list', 17, 23, { orientation: 'any', tile: true })]],
    ['cassette', [asset('cassette_label', 160, 48, { orientation: 'any' })]],
    ['icons', [asset('icon_music', 20, 20, { photo: true }),
      asset('icon_play', 14, 14, { codec: 'lossless', text_size: 'large' }),
      asset('icon_bolt', 9, 14)]],
    ['style', [], styles],
  ];
  const dir = temporaryDirectory();
  try {
    for (const [label, assets, style = []] of cases) {
      const packed = encodeTheme({ ...theme, assets, style });
      const path = join(dir, label + '.gtheme');
      writeFileSync(path, packed.bytes);
      const info = JSON.parse(execFileSync(python, [validator, '--check', path]));
      assert.equal(info.name, theme.name, label);
      assert.equal(info.assets.length, assets.length, label);
      const decoded = decodeTheme(packed.bytes);
      assert.deepEqual(decoded.palette, theme.palette, label);
      assert.deepEqual(decoded.style, style, label);
      assert.deepEqual(encodeTheme(decoded).bytes, packed.bytes, label);
      if (assets.length) {
        const source = [
          'import struct, sys',
          'sys.path.insert(0, "firmware/tools")',
          'from theme_pack import art_codec, check',
          'data = open(sys.argv[1], "rb").read()',
          'for a in check(data)["assets"]:',
          '    w, h, at, n = (a[k] for k in ("width", "height", "offset", "bytes"))',
          '    rows = art_codec.decode(data[at:at+n], w, h, [[0x31C7]*w for _ in range(h)])',
          '    for row in rows: sys.stdout.buffer.write(struct.pack("<" + "H"*w, *row))',
        ].join('\n');
        const reference = execFileSync(python, ['-c', source, path], { cwd: root });
        let at = 0;
        for (const { pixels, alpha } of decoded.assets) {
          for (let i = 0; i < pixels.length; i++, at += 2) {
            assert.equal(blend565(0x31c7, pixels[i], alpha[i]), reference.readUInt16LE(at), label);
          }
        }
        assert.equal(at, reference.length);
      }
      for (const [index, original] of assets.entries()) {
        if (original.codec === 'lossless') {
          const decodedAsset = decoded.assets[index];
          assert.deepEqual(decodedAsset.alpha, original.alpha, label);
          for (let pixel = 0; pixel < original.pixels.length; pixel++) {
            if (original.alpha[pixel]) assert.equal(decodedAsset.pixels[pixel], original.pixels[pixel], label);
          }
        }
      }
    }
    const reference = join(dir, 'reference.gtheme');
    execFileSync(python, [validator, join(root, 'examples/themes/pocket-scope'), '--output', reference]);
    const original = readFileSync(reference);
    const decoded = decodeTheme(original);
    assert.equal(decoded.name, 'Pocket Scope');
    assert.equal(decoded.assets.length, 9);
    assert.deepEqual(encodeTheme(decoded).bytes, new Uint8Array(original));
    decoded.assets[0].pixels[0] ^= 0xffff;
    assert.notDeepEqual(encodeTheme(decoded).bytes, new Uint8Array(original));
  } finally {
    rmSync(dir, { recursive: true });
  }
});

test('invalid metadata, transparency, Now Playing codecs, and corrupt packets are rejected', async () => {
  const { encodeTheme, decodeTheme, crc32 } = await codec();
  assert.throws(() => encodeTheme({ ...theme, name: '🎵'.repeat(8) }), /31 UTF-8 bytes/);
  assert.throws(() => encodeTheme({ ...theme, name: '\ud800' }), /UTF-8/);
  assert.throws(() => encodeTheme({ ...theme, assets: [asset('now_playing', 480, 222, { codec: 'lossless' })] }), /indexed/);
  assert.throws(() => encodeTheme({ ...theme, assets: [asset('splash', 222, 480)] }), /dimensions/);
  const tile = asset('list', 1, 1, { tile: true });
  const { bytes } = encodeTheme({ ...theme, assets: [tile] });
  bytes[bytes.length - 1] = 255;
  assert.throws(() => decodeTheme(bytes), /CRC/);
  new DataView(bytes.buffer).setUint32(12, crc32(bytes, 12, 16), true);
  assert.throws(() => decodeTheme(bytes), /index/);
  tile.alpha[0] = 3;
  assert.throws(() => encodeTheme({ ...theme, assets: [tile] }), /opaque/);
});

test('60-character links preserve all colors, both flags, and every style override', async () => {
  const { encodeThemeLink, decodeThemeLink } = await import(new URL('theme-link.js', studio));
  for (const style of [[], styles, [styles[2], styles.at(-1)]]) {
    const link = encodeThemeLink({ ...theme, style });
    assert.match(link, /^[A-Za-z0-9_-]{60}$/);
    assert.deepEqual(decodeThemeLink(link), { palette: theme.palette, base: theme.base, horizon: true, header_rule: false, style });
  }
  assert.throws(() => decodeThemeLink('_'.repeat(60)));
});

test('quadratic luminance matches the firmware Python reference across every RGB565 color', async () => {
  const { luminance, contrastRatio } = await import(new URL('echo-renderer.js', studio));
  const source = [
    'import struct, sys',
    'sys.path.insert(0, "firmware/tools")',
    'from theme_pack import luminance',
    'sys.stdout.buffer.write(struct.pack("<65536H", *(luminance(c) for c in range(65536))))',
  ].join('\n');
  const reference = execFileSync(python, ['-c', source], { cwd: root });
  for (let color = 0; color < 65536; color++) assert.equal(luminance(color), reference.readUInt16LE(color * 2));
  assert.equal(contrastRatio(0, 0), 1);
  assert.equal(contrastRatio(65535, 0), (65025 + 3251) / 3251);
});

test('PNG import and source ZIP export preserve straight RGB565 and alpha through Pillow', async () => {
  const { encodePng, decodePng } = await import(new URL('png.js', studio));
  const { sourceZip } = await import(new URL('asset-io.js', studio));
  const { encodeTheme, decodeTheme } = await codec();
  const dir = temporaryDirectory();
  try {
    const { pixels, alpha, width, height } = asset('icon_music', 20, 20, { photo: true });
    pixels[0] = 0;
    const image = { pixels, alpha, width, height };
    const png = await encodePng(image);
    assert.deepEqual(await decodePng(png, (w, h) => w === 20 && h === 20, false), image);
    await assert.rejects(decodePng(png, () => true, true), /solid background/);

    const zip = await sourceZip({ ...theme, assets: [
      { ...image, slot: 'icon_music', orientation: 'any', text_size: 'any', codec: 'lossless' },
      asset('list', 11, 10, { tile: true, orientation: 'any' }),
    ] }, 'test-theme');
    const archive = join(dir, 'theme.zip');
    writeFileSync(archive, new Uint8Array(await zip.arrayBuffer()));
    execFileSync(python, ['-m', 'zipfile', '-e', archive, dir]);
    const result = join(dir, 'source.gtheme');
    execFileSync(python, [validator, join(dir, 'test-theme'), '--output', result]);
    const pack = decodeTheme(readFileSync(result));
    assert.deepEqual(pack.assets[0].pixels, pixels);
    assert.deepEqual(pack.assets[0].alpha, alpha);
    assert.equal(pack.assets[1].tile, true);
    assert.deepEqual(decodeTheme(encodeTheme(pack).bytes).palette, theme.palette);

    const palettePNG = join(dir, 'indexed-alpha.png');
    execFileSync(python, ['-c', [
      'from PIL import Image',
      'import sys',
      'im = Image.new("P", (4, 1))',
      'im.putpalette([1, 2, 3, 9, 17, 25, 241, 15, 133, 193, 232, 17])',
      'im.putdata([0, 1, 2, 3])',
      'im.save(sys.argv[1], bits=2, transparency=bytes([0, 17, 128, 255]))',
    ].join('\n'), palettePNG]);
    const samples = await decodePng(new Blob([readFileSync(palettePNG)]), () => true, false);
    const { rgb565 } = await import(new URL('art-codec.js', studio));
    assert.deepEqual([...samples.pixels], [0, rgb565(9, 17, 25), rgb565(241, 15, 133), rgb565(193, 232, 17)]);
    assert.deepEqual([...samples.alpha], [0, 1, 8, 15]);

    const corrupted = new Uint8Array(await png.arrayBuffer());
    corrupted[corrupted.length - 1] ^= 1;
    await assert.rejects(decodePng(new Blob([corrupted]), () => true, false), /checksum/);
  } finally {
    rmSync(dir, { recursive: true });
  }
});

test('renderer clips artwork to the native regions and measures contrast against real art pixels', async () => {
  const { renderEcho, roleAt } = await import(new URL('echo-renderer.js', studio));
  const base = {
    ...theme, palette: Uint16Array.from({ length: 20 }, () => 0), style: [],
    assets: [asset('home', 480, 222), asset('home', 222, 480, { orientation: 'portrait' }),
      asset('list', 2, 2, { orientation: 'any', tile: true }), asset('now_playing', 480, 222)],
  };
  for (const art of base.assets) art.pixels.fill(65535);
  base.palette[1] = base.palette[2] = base.palette[7] = base.palette[10] = 65535;
  base.palette[8] = 0xa412;
  const landscape = renderEcho(base);
  assert.deepEqual([landscape.width, landscape.height], [480, 222]);
  assert.equal(landscape.pixels[1], base.palette[8]);
  assert.equal(landscape.pixels[91 * 480 + 250], 65535);
  assert.equal(landscape.pixels[91 * 480 + 479], base.palette[18]);
  assert.equal(roleAt(landscape, 41, 45), 'selection_text');
  assert.equal(landscape.labels.find(({ value }) => value === 'Music').low, 0);
  assert.ok(landscape.labels.find(({ value }) => value === 'Mixtapes').low > 0);
  const portrait = renderEcho(base, { orientation: 'portrait', textSize: 'medium' });
  assert.deepEqual([portrait.width, portrait.height], [222, 480]);
  assert.equal(portrait.pixels[30 * 222 + 210], base.palette[4]);
  assert.equal(portrait.pixels[200 * 222 + 150], 65535);
  assert.equal(portrait.pixels[223 * 222], base.palette[18]);
  const text = renderEcho(base, { screen: 'now_playing', mode: 'spectrum' });
  assert.ok(text.labels.find(({ value }) => value === 'Late Night Signals').low > 0);
  for (const mode of ['cover', 'cassette', 'vu', 'spectrum']) {
    const a = renderEcho(base, { screen: 'now_playing', mode, contrast: false });
    assert.equal(a.pixels.length, 480 * 222);
  }
});
