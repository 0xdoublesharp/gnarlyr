// Capture the real companion with disposable sample music and player folders.
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { createRequire } from "node:module";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { mkdir, mkdtemp, readFile, readdir, rename, rm, writeFile } from "node:fs/promises";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const identity = JSON.parse(await readFile(join(root, "firmware/identity.json"), "utf8"));
const scratch = "/Volumes/Scratch/SnowskyEcho";
const args = process.argv.slice(2);
function option(name, fallback) {
  const index = args.indexOf(name);
  if (index < 0) return fallback;
  if (!args[index + 1] || args[index + 1].startsWith("--")) throw new Error(`Missing ${name} value`);
  return resolve(args[index + 1]);
}
const source = option("--source-root", resolve(root, `../${identity.name.toLowerCase()}-companion`));
const configPath = option("--config", join(root, "tools/site_companion_screenshots.json"));
const config = JSON.parse(await readFile(configPath, "utf8"));
assert.equal(config.version, 1);
assert(config.viewport.width >= 900 && config.viewport.width <= 1600);
assert(config.viewport.height >= 700 && config.viewport.height <= 1200);
const known = new Set(["library-dark", "library-light", "sync-preview", "player-settings", "firmware-review", "languages"]);
assert(config.screens.length && config.screens.every((name) => known.has(name)));
assert.equal(new Set(config.screens).size, config.screens.length);
assert(config.albums.length >= 2 && config.albums.length <= 12);
for (const album of config.albums) {
  for (const value of [album.artist, album.title, ...album.tracks]) {
    assert(typeof value === "string" && value.trim() && !/[\\/\0\r\n]/.test(value));
  }
  assert(album.tracks.length > 0 && album.tracks.length <= 8);
}
assert.equal(spawnSync("mount", { encoding: "utf8" }).stdout.includes(" on /Volumes/Scratch "), true, "Mount Scratch before capturing");
const temporary = join(scratch, "tmp/site-companion");
await mkdir(temporary, { recursive: true });
const work = await mkdtemp(join(temporary, "run-"));
const env = { ...process.env, TMPDIR: work };
delete env.ELECTRON_RUN_AS_NODE;
let app, device;
const rendered = new Map();
try {
  if (!args.includes("--no-build")) {
    const result = spawnSync(process.execPath, [join(source, "tools/build.mjs")], {
      cwd: source, env, encoding: "utf8", timeout: 180000,
    });
    await writeFile(join(work, "build.log"), (result.stdout ?? "") + (result.stderr ?? ""));
    assert.equal(result.status, 0, `Companion build failed; see ${join(work, "build.log")}`);
  }
  const require = createRequire(join(source, "package.json"));
  const electron = require("electron");
  const { _electron } = require("playwright");
  const { deviceFixture } = await import(pathToFileURL(join(source, "tools/fixture.mjs")).href);
  const { imageLanguages, packBytes } = await import(pathToFileURL(join(source, "tests/language-fixture.ts")).href);
  device = await deviceFixture();
  await rename(device.volume, join(device.root, identity.name));
  device.volume = join(device.root, identity.name);
  await writeFile(device.image, imageLanguages(device.data));
  const music = join(work, "Music"), target = join(work, "Sync preview"), profile = join(work, "profile");
  for (const path of [music, target, profile]) await mkdir(path);
  for (const album of config.albums) {
    const folder = join(music, album.artist, album.title);
    await mkdir(folder, { recursive: true });
    for (const [index, title] of album.tracks.entries()) {
      const result = spawnSync("ffmpeg", ["-v", "error", "-f", "lavfi", "-i", "anullsrc=r=44100:cl=stereo",
        "-t", String(180 + index * 30), "-c:a", "flac", "-metadata", `artist=${album.artist}`,
        "-metadata", `album=${album.title}`, "-metadata", `title=${title}`, "-metadata", `track=${index + 1}`,
        "-metadata", "date=2026", join(folder, `${String(index + 1).padStart(2, "0")} ${title}.flac`)],
        { env, encoding: "utf8", timeout: 30000 });
      assert.equal(result.status, 0, result.stderr);
    }
  }
  const card = join(work, "GnarlySD");
  await mkdir(join(card, `${identity.data_directory}/lang`), { recursive: true });
  await writeFile(join(card, `${identity.data_directory}/lang/es.pack`), packBytes());
  await writeFile(join(profile, "library.json"), JSON.stringify({ version: 1, selected: [], theme: "dark" }));
  app = await _electron.launch({ executablePath: electron, args: [join(source, ".build/app")],
    env: { ...env, [`${identity.id}_USER_DATA_DIR`]: profile, [`${identity.id}_BACKUP_DIR`]: join(work, "backups") }, timeout: 30000 });
  const page = await app.firstWindow();
  const errors = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.setViewportSize(config.viewport);
  const pick = (path) => app.evaluate(({ dialog }, path) => {
    dialog.showOpenDialog = async () => ({ canceled: false, filePaths: [path] });
  }, path);
  const shot = async (name) => {
    if (!config.screens.includes(name)) return;
    await page.mouse.move(0, 0);
    await page.evaluate(async () => {
      await document.fonts.ready;
      for (const animation of document.getAnimations()) {
        if (animation.effect?.getComputedTiming().iterations !== Infinity) animation.finish();
      }
    });
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
    rendered.set(`${name}.png`, await page.screenshot({ scale: "css", animations: "disabled" }));
  };
  await page.getByRole("button", { name: "Add music folder" }).waitFor();
  await pick(music);
  await page.getByRole("button", { name: "Add music folder" }).click();
  await page.waitForFunction((count) => document.querySelectorAll(".album-card").length === count, config.albums.length);
  await page.locator(".album-card").nth(0).click();
  await page.locator(".album-card").nth(1).click();
  await page.getByText("2 albums selected", { exact: true }).waitFor();
  await shot("library-dark");
  await page.getByRole("button", { name: "Switch to light mode" }).click();
  await page.waitForFunction(() => document.documentElement.dataset.theme === "light");
  await shot("library-light");
  await page.getByRole("button", { name: "Switch to dark mode" }).click();
  await page.waitForFunction(() => document.documentElement.dataset.theme === "dark");
  await pick(target);
  await page.getByRole("button", { name: "Preview sync" }).click();
  await page.getByRole("dialog").waitFor();
  await shot("sync-preview");
  assert.deepEqual(await readdir(target), [], "Preview must not copy files");
  await page.keyboard.press("Escape");
  await page.getByRole("dialog").waitFor({ state: "hidden" });
  await page.getByRole("button", { name: "Your Echo", exact: true }).click();
  await pick(device.volume);
  await page.getByRole("button", { name: "Choose player drive" }).click();
  await page.getByLabel("Brightness", { exact: false }).waitFor();
  await shot("player-settings");
  await pick(device.image);
  await page.getByRole("button", { name: "Choose firmware image" }).click();
  await page.getByText("Build a1b2c3d", { exact: false }).waitFor();
  await page.getByRole("button", { name: "Review firmware", exact: true }).click();
  await page.getByRole("dialog").waitFor();
  await shot("firmware-review");
  await page.keyboard.press("Escape");
  await page.getByRole("dialog").waitFor({ state: "hidden" });
  await page.getByRole("button", { name: "Languages", exact: true }).click();
  await pick(card);
  await page.getByRole("button", { name: "Choose microSD drive" }).click();
  await pick(device.image);
  await page.getByRole("button", { name: "Choose installed firmware image" }).click();
  await page.getByRole("button", { name: "Preview Español" }).waitFor();
  await page.getByRole("button", { name: "Languages", exact: true }).click();
  await page.getByRole("button", { name: "Preview Español" }).scrollIntoViewIfNeeded();
  await page.getByRole("button", { name: "Preview Español" }).click();
  await page.getByRole("dialog").waitFor();
  await shot("languages");
  assert.deepEqual(errors, []);
  assert.deepEqual(await readFile(join(device.volume, `${identity.data_directory}/settings.bin`)), device.settings);
  assert.equal((await readdir(device.volume)).some((name) => /\.img$/i.test(name)), false);
  assert.equal(rendered.size, config.screens.length);
  const output = join(root, "site/assets/companion");
  await mkdir(output, { recursive: true });
  for (const [name, data] of rendered) {
    const staged = join(output, ".capture-" + name);
    await writeFile(staged, data);
    await rename(staged, join(output, name));
  }
  console.log(JSON.stringify({ source, output, screens: [...rendered.keys()], viewport: config.viewport,
    sampleData: true, physicalPlayerAccess: false }));
} finally {
  if (app) await app.close();
  if (device) await rm(device.root, { recursive: true, force: true });
  await rm(work, { recursive: true, force: true });
}
