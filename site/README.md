# Gnarlyr.com

Static firmware landing page. Serve this directory with any static host; there is no build step, dependency install, external font, or analytics. Open `index.html` directly for a local preview. Publish `index.html`, `favicon.svg`, `robots.txt`, and `sitemap.xml` together at the domain root. A small inline script copies commands with success and selection feedback and opens help sections when following their fragment links. Instructions and expandable help remain available without JavaScript.

The cassette artwork is an original inline SVG, not a device photograph or firmware screenshot. The page includes responsive layouts, keyboard focus, reduced-motion support, readable command wrapping, and native expandable installation help. The [UX pass](../docs/gnarlyr-ux.md) records the accepted design and checks.

Firmware screenshots will be added when ready. Keep final public image assets under `site/assets/`; the publisher includes that directory. Do not add empty screenshot placeholders or claim that the cassette illustration is a screenshot.

DoublePaaS hosting uses the [deployment contract](../docs/deployment.md). Only the four public root files and optional image assets are published; this README and `deploy/` stay outside the served release.

Feature and hardware claims come from `docs/goal.md`, `firmware/docs/mixtapes.md`, `firmware/docs/user-guide.md`, and the project’s installation/recovery guides. The latest project goal takes precedence over older inherited Podium test-status text. Build instructions use `tools/firmware.py` and Scratch output.

The page provides source-build instructions. When a public Gnarlyr release is ready, add the fork’s actual source/release URL, image version, and published SHA-256; link the release’s matching installation/recovery documentation. Do not substitute upstream Podium downloads for this fork. Keep engineering status in `docs/goal.md` and the public page focused on features and usage.
