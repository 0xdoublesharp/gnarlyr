# Gnarlyr website deployment

The app session owns this static website’s manual publication and DoublePaaS
runtime. Website deployment does not build, distribute, or flash firmware.
There is no public Gnarlyr source or firmware release URL yet. Add firmware
screenshots under `site/assets/` when they are ready, then publish the updated page.

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
| Validation URL | https://gnarlyr.apps.doublesharp.com |
| Final domain | https://gnarlyr.com; www redirects to the apex |
| Origin address | `159.89.222.183` |

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

The publisher includes only `index.html`, `favicon.svg`, `robots.txt`,
`sitemap.xml`, and regular public image files under `site/assets/`, up to
64 MiB total. Firmware, README files, deployment files and hidden files are
excluded. SHA-256 of the ordered paths and file hashes identifies a release.
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

Require HTTP 200 for `/`, `/favicon.svg`, `/robots.txt`, `/sitemap.xml` and
`/healthz`; compare `/` byte-for-byte against `site/index.html`. Require 404 for
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

Gnarlyr.com currently uses Cloudflare. The connected account has no DNS
integration, and the documented local DNS credential and backup are absent.
The custom-domain cutover still requires DNS access: point the apex A record to
`159.89.222.183`, point www to the apex (or the same origin), reconcile conflicting
AAAA records, then verify origin and public TLS, page bytes and the www redirect.
Do not replace mail or unrelated records.
