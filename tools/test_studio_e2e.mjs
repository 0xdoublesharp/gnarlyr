import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { mkdir, mkdtemp, readFile, rm } from "node:fs/promises";
import { createServer } from "node:http";
import { createRequire } from "node:module";
import { dirname, extname, join, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";
import { hexColor } from "../site/studio/art-codec.js";
import { fromHex } from "../site/studio/palette.js";
import { decodeTheme } from "../site/studio/gtheme-codec.js";
import { decodeThemeLink } from "../site/studio/theme-link.js";

const root = resolve(fileURLToPath(new URL("../", import.meta.url)));
const run = promisify(execFile);
const identity = JSON.parse(
	await readFile(join(root, "firmware/identity.json"), "utf8"),
);
const siteName = identity.id.toLowerCase();
const canonical = `https://${siteName}.com/studio.html`;
const python = process.env.THEME_STUDIO_PYTHON ?? "python3";
const pythonEnv = { ...process.env, PYTHONDONTWRITEBYTECODE: "1" };
const checker = join(root, "firmware/tools/theme_pack.py");
const name = "Midnight E2E";
const style = [{ kind: "now_playing", target: "align", value: 1 }];

// Share the real site and generated publication resources with Companion E2E.
export async function serveStudio(work, extra = new Map()) {
	const generated = join(work, "studio-font-core.bin");
	await run(
		python,
		[
			"-c",
			"from pathlib import Path; from tools.publish_site import studio_base_font; import sys; Path(sys.argv[1]).write_bytes(studio_base_font())",
			generated,
		],
		{ cwd: root, env: { ...pythonEnv, TMPDIR: work } },
	);
	const types = {
		".html": "text/html",
		".js": "text/javascript",
		".css": "text/css",
		".json": "application/json",
		".png": "image/png",
		".svg": "image/svg+xml",
	};
	const requests = [];
	const server = createServer(async (request, response) => {
		let path;
		let status = 200;
		try {
			path = decodeURIComponent(
				new URL(request.url, "http://localhost").pathname,
			);
			let file = extra.get(path);
			if (Buffer.isBuffer(file)) {
				response.setHeader(
					"Content-Type",
					types[extname(path)] ?? "application/octet-stream",
				);
				response.end(file);
				return;
			}
			if (path === "/studio/fonts/inter_regular_16_core.bin") file = generated;
			else if (path.startsWith("/studio/fonts/"))
				file = join(root, "firmware/src/fonts/packs", path.slice(14));
			else if (path.startsWith("/studio/examples/"))
				file = join(root, "examples/themes", path.slice(17));
			else file ??= join(root, "site", path);
			assert(file.startsWith(root + sep) || file.startsWith(work + sep));
			response.setHeader(
				"Content-Type",
				types[extname(path)] ?? "application/octet-stream",
			);
			response.end(await readFile(file));
		} catch {
			status = 404;
			response.writeHead(status).end();
		} finally {
			requests.push({ path, status });
		}
	});
	await new Promise((ready, reject) =>
		server.once("error", reject).listen(0, "127.0.0.1", ready),
	);
	return {
		url: `http://127.0.0.1:${server.address().port}`,
		requests,
		close: () => new Promise((done) => server.close(done)),
	};
}

export async function checkTheme(path, work, installed = false) {
	return run(
		python,
		[checker, "--check", path, ...(installed ? ["--installed"] : [])],
		{ env: { ...pythonEnv, TMPDIR: work } },
	);
}

const palette = (page) =>
	page
		.locator("#palette-roles input")
		.evaluateAll((inputs) => inputs.map((input) => input.value));
const canvas = (page, id) =>
	page.locator(id).evaluate((element) => element.toDataURL());
const tab = (page, name) =>
	page.getByRole("tab", { name, exact: true }).click();
async function download(page, id, work) {
	const [file] = await Promise.all([
		page.waitForEvent("download"),
		page.locator(id).click(),
	]);
	const path = join(work, file.suggestedFilename());
	await file.saveAs(path);
	return path;
}
async function chooseColor(page, role, color) {
	await page.locator(`#role-${role} input`).fill(color);
	assert.equal(
		await page.locator(`#role-${role} input`).inputValue(),
		hexColor(fromHex(color)),
	);
}

export async function studioExports(
	chromium,
	url,
	work,
	{ full = false } = {},
) {
	const browser = await chromium.launch({
		downloadsPath: work,
		env: { ...process.env, TMPDIR: work },
		args: ["--disable-background-networking", "--mute-audio"],
	});
	try {
		const context = await browser.newContext({
			reducedMotion: "no-preference",
		});
		const page = await context.newPage();
		page.setDefaultTimeout(10000);
		const errors = [];
		page.on("pageerror", (error) => errors.push(error.message));
		page.on("console", (message) => {
			if (message.type() === "error") errors.push(message.text());
		});
		const issue = `https://github.com/0x${identity.manufacturer.toLowerCase()}/${siteName}/issues/new`;
		const issueRequest = [];
		await context.route("**/*", async (route) => {
			const target = route.request().url();
			if (target.startsWith(url + "/")) await route.continue();
			else if (target.startsWith(issue + "?")) {
				issueRequest.push(new URL(target));
				await route.fulfill({ body: "Captured issue draft" });
			} else {
				errors.push(`Unexpected request: ${target}`);
				await route.abort();
			}
		});
		const studio = url + "/studio.html";
		assert.equal((await page.goto(studio)).status(), 200);
		const before = await palette(page);
		await page.locator("#preset").selectOption("glass");
		assert.equal(
			await page.locator("#current-theme").textContent(),
			"Sea Glass",
		);
		const preset = await palette(page);
		assert.notDeepEqual(preset, before);
		await chooseColor(page, "background", "#174a77");
		const edited = await palette(page);
		assert.notDeepEqual(edited, preset);
		await page.locator("#undo").click();
		assert.deepEqual(await palette(page), preset);
		await page.locator("#redo").click();
		assert.deepEqual(await palette(page), edited);
		await chooseColor(page, "text", "#174a77");

		await tab(page, "Artwork");
		const artwork = await page.evaluate(() => {
			const canvas = document.createElement("canvas");
			[canvas.width, canvas.height] = [480, 222];
			const ctx = canvas.getContext("2d");
			ctx.fillStyle = "#102030";
			ctx.fillRect(0, 0, 480, 222);
			ctx.fillStyle = "#aa5544";
			ctx.fillRect(30, 30, 40, 40);
			return canvas.toDataURL().split(",")[1];
		});
		await page
			.locator("#art-file")
			.setInputFiles({
				name: "home.png",
				mimeType: "image/png",
				buffer: Buffer.from(artwork, "base64"),
			});
		await page.getByText("Artwork added.", { exact: true }).waitFor();
		await tab(page, "Icons");
		const icon = await canvas(page, "#icon-grid");
		await page.locator("#icon-grid").press("ArrowRight");
		await page.locator("#icon-grid").press("Space");
		assert.notEqual(await canvas(page, "#icon-grid"), icon);

		await tab(page, "Style");
		await page.locator("#base").selectOption("sunset");
		await page.locator("#horizon").check();
		await page.locator("#header_rule").uncheck();
		await page.locator("#style-align").selectOption("1");
		await tab(page, "Details");
		await page.locator("#meta-name").fill(name);
		await page.locator("#meta-author").fill("Studio E2E");
		await page.locator("#creator").fill("studio-e2e");
		await page.locator("#creator").press("Tab");

		if (full) {
			await page.locator("#mode").selectOption("cassette");
			assert.equal(await page.locator("#screen").inputValue(), "now_playing");
			const cassette = await canvas(page, "#echo");
			await page.locator("#screen").selectOption("home");
			await page.waitForFunction(
				(former) => document.querySelector("#echo").toDataURL() !== former,
				cassette,
			);
			await page.locator("#orientation").selectOption("portrait");
			await page.waitForFunction(
				() =>
					document.querySelector("#echo").width === 222 &&
					document.querySelector("#echo").height === 480,
			);
			await page.locator("#contrast").check();
			await page
				.getByLabel("Low contrast text")
				.getByRole("button")
				.first()
				.waitFor();
			await page
				.getByRole("button", { name: "Preflight / Stress-test" })
				.click();
			await page.locator("#run-preflight").click();
			await page.getByText(/^Pack passed binary validation/).waitFor();
			assert.equal(await page.locator("#contact-sheet canvas").count(), 8);
			await page.getByRole("button", { name: "Portrait layout" }).click();
			assert.equal(await page.locator("#orientation").inputValue(), "portrait");
			assert.equal(await page.locator("#contrast").isChecked(), true);
			await page.locator("#mode").selectOption("spectrum");
			await page.getByRole("button", { name: "Play synth demo" }).click();
			await page
				.getByText("Playing Midnight Circuit", { exact: true })
				.waitFor();
			const frame = await canvas(page, "#echo");
			await page.waitForFunction(
				(former) => document.querySelector("#echo").toDataURL() !== former,
				frame,
			);
			await page.getByRole("button", { name: "Pause", exact: true }).click();
			await page
				.getByText("Paused Midnight Circuit", { exact: true })
				.waitFor();
		}

		await page.locator("#copy-link").click();
		const link = await page.locator("#share-url").inputValue();
		assert(link.startsWith(canonical));
		assert.match(new URL(link).hash, /^#p=[A-Za-z0-9_-]{60}&ref=studio-e2e$/);
		const fragment = new URL(link).hash;
		const expected = {
			palette: (await palette(page)).map(fromHex),
			base: "sunset",
			horizon: true,
			header_rule: false,
			style,
		};
		const state = ({ palette, base, horizon, header_rule, style }) => ({
			palette: [...palette],
			base,
			horizon,
			header_rule,
			style,
		});
		const pack = await download(page, "#download-pack", work);
		await checkTheme(pack, work);
		const decoded = decodeTheme(await readFile(pack));
		assert.equal(decoded.name, name);
		assert.deepEqual(state(decoded), expected);
		assert.deepEqual(decoded.assets.map(({ slot }) => slot).sort(), [
			"home",
			"icon_music",
		]);
		assert.deepEqual(
			state(decodeThemeLink(new URLSearchParams(fragment.slice(1)).get("p"))),
			expected,
		);

		if (full) {
			const png = await download(page, "#social-card", work);
			const screenshot = await readFile(png);
			assert.equal(
				screenshot.subarray(0, 8).toString("hex"),
				"89504e470d0a1a0a",
			);
			assert.deepEqual(
				[screenshot.readUInt32BE(16), screenshot.readUInt32BE(20)],
				[1200, 630],
			);
			assert.deepEqual(
				await page.evaluate(async (base64) => {
					const image = new Image();
					image.src = "data:image/png;base64," + base64;
					await image.decode();
					return [image.naturalWidth, image.naturalHeight];
				}, screenshot.toString("base64")),
				[1200, 630],
			);
			const zip = await download(page, "#download-source", work);
			await run(
				python,
				[
					"-c",
					"from zipfile import ZipFile; import sys; z = ZipFile(sys.argv[1]); assert z.testzip() is None; assert any(n.endswith('/theme.json') for n in z.namelist()); assert len(z.namelist()) >= 3",
					zip,
				],
				{ env: { ...pythonEnv, TMPDIR: work } },
			);
			await page.getByRole("button", { name: "Submit to gallery" }).click();
			const dialog = page.getByRole("dialog");
			await dialog.waitFor();
			const prepare = dialog.getByRole("button", {
				name: "Prepare GitHub issue",
			});
			assert.equal(await prepare.isEnabled(), false);
			await dialog.getByLabel("Remix credit").fill("Example credit");
			await dialog.getByRole("checkbox").check();
			assert.equal(await prepare.isEnabled(), true);
			const summary = await dialog
				.getByLabel("Submission summary")
				.inputValue();
			for (const value of [
				name,
				"Studio E2E (@studio-e2e)",
				"CC0-1.0",
				"Example credit",
				link,
				"Valid v2 pack",
				"I can share every asset",
			])
				assert(summary.includes(value), value);
			await Promise.all([context.waitForEvent("page"), prepare.click()]);
			assert.equal(issueRequest.length, 1);
			assert.equal(
				issueRequest[0].searchParams.get("title"),
				"Theme gallery: " + name,
			);
			assert.equal(issueRequest[0].searchParams.get("body"), summary);
		}

		await page.goto(studio + fragment);
		await page.getByText("Shared palette restored.", { exact: true }).waitFor();
		await page.reload();
		await page.getByText("Shared palette restored.", { exact: true }).waitFor();
		assert.deepEqual((await palette(page)).map(fromHex), expected.palette);
		assert.deepEqual(await page.locator("#base").inputValue(), expected.base);
		assert.equal(await page.locator("#horizon").isChecked(), expected.horizon);
		assert.equal(
			await page.locator("#header_rule").isChecked(),
			expected.header_rule,
		);
		assert.equal(await page.locator("#style-align").inputValue(), "1");
		await page.locator("#copy-link").click();
		assert.equal(
			new URLSearchParams(
				new URL(await page.locator("#share-url").inputValue()).hash.slice(1),
			).get("p"),
			new URLSearchParams(fragment.slice(1)).get("p"),
		);
		assert.deepEqual(errors, [], "Studio page and console errors");
		return { pack, link, fragment, expected, name };
	} finally {
		await browser.close();
	}
}

if (
	process.argv[1] &&
	resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
	const companion = resolve(
		process.env.STUDIO_COMPANION_DIR ?? join(root, "..", `${siteName}-desktop`),
	);
	const require = createRequire(join(companion, "package.json"));
	const { chromium } = require("playwright");
	const scratch = "/Volumes/Scratch";
	if (process.platform === "darwin") {
		const { stdout } = await run("mount");
		assert(
			stdout.includes(` on ${scratch} `),
			"Mount Scratch before running Studio E2E",
		);
	}
	const temporary =
		process.platform === "darwin"
			? join(scratch, "SnowskyEcho/tmp")
			: resolve(process.env.TMPDIR ?? join(scratch, "SnowskyEcho/tmp"));
	await mkdir(temporary, { recursive: true });
	const work = await mkdtemp(join(temporary, "studio-e2e-"));
	let server;
	try {
		server = await serveStudio(work);
		await studioExports(chromium, server.url, work, { full: true });
		assert.deepEqual(
			server.requests.filter(({ status }) => status !== 200),
			[],
		);
		console.log(
			"PASS Studio: palette, artwork, icon, stage, preflight, audio, share, exports and gallery",
		);
	} finally {
		await server?.close();
		await rm(work, { recursive: true, force: true });
	}
}
