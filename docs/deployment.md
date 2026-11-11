# Gnarlyr website deployment

The app session owns this static website’s manual publication and DoublePaaS
runtime. Website deployment does not build, distribute, or flash firmware.
There is no public Gnarlyr source or firmware release URL yet. The visual Docs guide uses simulator screenshots under `site/assets/screens/`;
regenerate them with `tools/site_screenshots.py` before publishing interface changes.

## Production

| Resource | Value |
| --- | --- |
| Project | Gnarlyr — `5isiqhmcdbv60uo542qmvik0` |
| Environment | production — `frayzgbvpya7jpnvnooicwwl` |
| Server | DoublePaaS Production — `luapxijivv47xmmbg1zww1ba` |
| Destination | coolify — `ck0q4yhwqvybejsz18yaw4ag` |
| Compose Service | gnarlyr.com — `qmr9bkgudrovynocs2tlbdlt` |
| Web child | `qwiocekeoqdh3pakchdihv9b` |
| Container | `web-qmr9bkgudrovynocs2tlbdlt` |
| Primary URL | https://gnarlyr.com; www redirects to the apex |
| Secondary validation URL | https://gnarlyr.apps.doublesharp.com |
| Origin address | `159.89.222.183` |
| Current content release | `9305332909d03209c3ef0f2513bca2e873775062100fb8e0f628c9cee41236b1` |
| Rollback content release | `9f3e22ba452260621afd8e56935d7a9a23c4fd7901a86b39d6c2461438fa113e` |

This is a user-defined Compose Service with normal Coolify parsing, not a
Git-backed Application. `site/deploy/compose.yaml` owns the runtime, mounts,
limits and network. Coolify’s web child owns the three HTTPS domain routes;
Traefik terminates TLS and forwards to internal port 8080. No host port is
published and no additional Docker network is allocated.

The nginx image is independently pinned by digest in Compose. The container
runs as 101:101, with a read-only root, dropped capabilities,
no-new-privileges, a 16 MiB writable tmpfs, 64 MiB memory and 0.25 CPU.
It mounts only this site’s frontend and nginx configuration, both read-only.
There is no database, cache, migration, background worker, or runtime secret.

## Publish content

From this checkout:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/publish_site.py --check
PYTHONDONTWRITEBYTECODE=1 python3 tools/publish_site.py
```

The first command validates files and prints the content release ID without
connecting. The second uses the existing
`~/Workspaces/DoublePaaS/scripts/admin.py` owner helper, which decrypts the
operator’s sudo credential in memory and sends it over SSH stdin. Credential
values are never stored here or printed. The connected `double-paas` MCP owns
normal service configuration/start operations. There is no GitHub release
workflow or automatic push deployment for this local-only source repository.

The publisher includes only `index.html`, `firmware.html`, `hardware.html`,
`docs.html`, `recovery.html`, `install.html`, `build.html`, `styles.css`, `site.js`, `favicon.svg`, `robots.txt`,
`sitemap.xml`, `languages.html`, `language-catalog.js`, `language-packs.js`, and regular public image files under `site/assets/`, up to
64 MiB total. Firmware, README files, deployment files and hidden files are
excluded. The publisher versions local stylesheet, script and image URLs in each HTML page
with the SHA-256 prefix of the referenced asset. Social-preview PNGs under
`site/assets/social/` use content-hashed filenames and absolute HTTPS metadata URLs.
Regenerate them with `tools/site_social.py` after changing card copy or artwork. This keeps cached styling and
screenshots in sync with page updates. The source HTML stays unchanged. SHA-256 of the ordered published paths and file hashes identifies a release.
Existing releases must match before reuse. Publication stages and reads back
files, then atomically replaces a relative `current` symlink while holding
`frontend/.publish.lock`. A busy lock fails without stealing custody.
Content publication does not restart nginx. Prior releases remain for rollback.

Host layout:

```text
/srv/hosted-data/gnarlyr/
  frontend/current -> releases/CONTENT_SHA256
  frontend/releases/CONTENT_SHA256/
  frontend/.publish.lock
  runtime/nginx.conf
  ops/compose.yaml
```

Keep the host nginx configuration equal to `site/deploy/nginx.conf`.
Runtime changes are separate: update the owned Service via native MCP, then
restart that Service and verify its actual limits, mounts, health and HTTPS.
Do not restart the shared proxy for an ordinary site release.

## Verify and recover

Require HTTP 200 for `/`, `/firmware.html`, `/docs.html`, `/hardware.html`, `/recovery.html`, `/install.html`, `/build.html`,
`/styles.css`, `/site.js`, `/favicon.svg`, `/robots.txt`, `/sitemap.xml`,
`/languages.html`, `/language-catalog.js`, `/language-packs.js` and
`/healthz`; compare all fifteen public root files and every published image byte-for-byte against the publisher’s `public_files()` output at
the origin, including the generated asset URLs. At the Cloudflare edge, HTML includes an injected analytics beacon;
remove only that known script when comparing HTML. Other files must match exactly. Require 404 for
`/README.md`, `/deploy/compose.yaml` and an unknown path. Verify origin TLS
separately with `curl --resolve HOST:443:159.89.222.183 https://HOST/`;
Cloudflare edge HTTPS alone does not verify the origin certificate.

If publication fails before activation, the previous pointer remains active.
For rollback, use the owner helper to acquire the same `.publish.lock`, select
an existing verified release directory, create a temporary relative symlink to
`releases/CONTENT_SHA256`, and rename it atomically over `frontend/current`.
Recheck public bytes and HTTPS. Never edit files inside an existing release or
delete a release required for recovery. The initial publication has no previous
Gnarlyr release. The pinned runtime image remains the runtime rollback reference.

The site’s host files are under the platform’s `/srv/hosted-data` Restic backup
scope (six-hour schedule); source also stays on backed-up Data locally.
No application database backup is needed. Use the platform recovery procedure
for a host restore; an isolated restore rehearsal is not part of this release.

## Feature refresh and editorial pass — 2026-11-10

Published release:
`bbb8c41c4c4df48779faf86722e66b4b8fde83406e18523ecff7302dda8eda30`.
Rollback: `32fd364a86a506703d2cba7d3cee0d6456fe38ecf4c860ede3c14811ae4064ae`.
Firmware content follows the active `gnarlyr` branch through `e8ce027a`.
Features cover listening endpoint/idle transitions, failed history and lyrics
reads, index-publication storage errors and deferred-search cancellation.
Developers documents the Ogg packet reader and bounded Vorbis decoder as
integration components, separate from the supported player formats.

All five public pages received an editorial and meaning pass. The fixes list
is grouped by task; hardware and installation explanations are shorter and
terminology is consistent. Commands, checksums, quantities and update order
are preserved. Static structure/link checks and five standalone desktop/mobile
renders pass. All ten files match origin bytes over TLS for `gnarlyr.com`;
public content matches after removing only Cloudflare's injected beacon.
Health is 200, private/unknown paths are 404, and nginx was not restarted.

## Firmware feature refresh — 2026-11-10

Published release:
`32fd364a86a506703d2cba7d3cee0d6456fe38ecf4c860ede3c14811ae4064ae`.
Rollback: `6279229fb53f4d5f58936fa73b6125b4fb54fad3f03b178b66c1a7790aa2759b`.
Content follows the active firmware worktree's `gnarlyr` branch through
`8545978`, covering commits after the previous `c45f854` source baseline.
Home and Features cover M4B shelves, chapter indexing, nested resume and
lower-rate speech. The fixes list adds wake timing and accurate pause
diagnostics; Developers uses the current canonical identity-transition guide.
Install retains its user setup flow and links audiobook guidance.

All ten files match origin bytes over TLS for `gnarlyr.com`. Public content
matches after removing only Cloudflare's injected beacon from HTML. Health is
200 and private/unknown paths are 404. Static HTML/link/copy-command checks and
mobile/desktop standalone renders pass. Firmware and device state were untouched.

## Install and developer guide split — 2026-11-10

Published release:
`6279229fb53f4d5f58936fa73b6125b4fb54fad3f03b178b66c1a7790aa2759b`.
Rollback: `57f83f3939eb1439914c69f691b277f5fbf3a27f803b17ea368e80a4ed66ece6`.
Install covers ready-made firmware, first installation, music storage, initial
settings and later USB updates. Build setup, developer updates, USB internals
and legacy migration references are on `build.html`. The homepage action leads
to Install; navigation and the sitemap include Developers.

All ten public files match local source over HTTPS. Health is 200 and private
or unknown paths are 404. HTML nesting, headings, IDs, links, copy bindings and
exact command text pass static checks. Mobile Install and desktop Developers
standalone renders were inspected. Publication did not restart nginx.

## Evergreen copy — 2026-11-10

Published release:
`57f83f3939eb1439914c69f691b277f5fbf3a27f803b17ea368e80a4ed66ece6`.
Rollback: `0780845239168e3f334ee513025e9607a7762b422c09bb20504b61fccf83abbb`.
Product copy describes the player and firmware without owner anecdotes or
source-filename history. Measured chip identification stays in technical detail.
All four updated pages match local source over HTTPS; health remains 200.
IDs, link destinations and copy bindings are preserved.

## Public release wording — 2026-11-10

Published release:
`0780845239168e3f334ee513025e9607a7762b422c09bb20504b61fccf83abbb`.
Rollback: `e50c9feb9d6a9ca5250173eda0c5ff921be9c90ad2f84ed4b5c916bd4de8b556`.
The installation guide identifies Gnarlyr 1.0 as the first future public release,
using `GNARLYR.img`, and removes the development commit marker. The updated
page matches local source over HTTPS; health remains 200.

## Hardware and firmware guides — 2026-11-10

Published release:
`e50c9feb9d6a9ca5250173eda0c5ff921be9c90ad2f84ed4b5c916bd4de8b556`.
Rollback release:
`f8fbae026150e4fac7d2fee9ece4ee0179b25f2930e3fccf55a718e246d9c9c3`.

The home page links dedicated firmware, hardware and installation guides.
Content follows fresh-base through `c45f854`; the active firmware task owns
the user-selected `GNARLYR.img` identity change. The independent writing and
accuracy review led to corrections for listening statistics, mixtape editing,
language support, inherited fixes, first-install order and legacy updaters.

All nine public files match local source byte-for-byte over HTTPS. Health is
200; README, deployment configuration and unknown paths are 404. Static checks
pass for JavaScript syntax, headings, unique IDs, cross-page anchors, copy
bindings and exact commands. Standalone desktop/mobile renders were inspected.
The content pointer changed without restarting nginx. No firmware or device
operation was part of this publication.

## UX publication — 2026-11-10

Site source: `291b2fd`. Published release:
`f8fbae026150e4fac7d2fee9ece4ee0179b25f2930e3fccf55a718e246d9c9c3`.
The previous release remains available for rollback:
`517105ded8e9ae2845a039dfb281fb27c326f8511c679188effa1de412d295b8`.

The validation URL serves all four public files byte-for-byte against local
source. Health returns 200; README, deployment configuration and unknown paths
return 404. Direct-origin TLS verification passes, the service is healthy, and
live keyboard copying reports success. Publication changed only the content
pointer; nginx was not restarted. The [UX pass](gnarlyr-ux.md) records the
six-round design comparison.

## Initial release

Content release: `1684a6fa06bf98f4efb1acd6a01e3f46bf2f5a2a6693f542551a907106d4aa67`.
The validation URL serves the four public files, and the root matches the
local HTML byte-for-byte. Health is 200; private deployment files and unknown
paths are 404. Actual container limits and read-only mounts match the definition.
The validation hostname also passes direct-origin TLS and HTTP 200 at the Reserved
IP. The Service is healthy after all three domain routes were configured.

DNS access was unavailable during the initial release. The primary domain is
now verified below; the initial cutover blocker is historical.

## Primary domain verification — 2026-11-10

Use https://gnarlyr.com for public links. All ten current release files serve
through that domain, and each origin response matches local source exactly.
Public HTML differs only by Cloudflare's injected beacon. Verification used a
browser User-Agent. Origin TLS validates for `gnarlyr.com` at `159.89.222.183`.
`https://www.gnarlyr.com/install.html` redirects to the same path on the apex.
Canonical metadata and sitemap URLs already use the primary domain. This check
made no DNS or runtime changes and retained release
`6279229fb53f4d5f58936fa73b6125b4fb54fad3f03b178b66c1a7790aa2759b`.
