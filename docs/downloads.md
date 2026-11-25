# Builds and downloads

Use [Gnarlyr GitHub Releases](https://github.com/0xdoublesharp/gnarlyr/releases) for public firmware and desktop installer links. GitHub Actions artifacts expire and require authentication.

## Firmware

CI preflight packages the image with `tools/release_package.py` and uploads `GNARLYR.IMG`, `ECHOECHO.img` (the same bytes under the name FiiO's updater accepts), `gnarlyr-source.tar.gz`, `firmware.json`, and `SHA256SUMS` as an Actions artifact. Publication requires a successful run of the entire CI workflow.

Point first-time users directly to `ECHOECHO.img`: download, copy to internal storage, eject both drives, unplug and restart. Keep this filename so no rename is needed. After Gnarlyr starts, the companion app can prepare later updates. If FiiO briefly shows Updating and returns to stock, use [first-install troubleshooting](../firmware/docs/device-install.md#briefly-shows-updating-then-returns-to-fiio); the macOS helper handles contiguous allocation and readback as an optional fallback.

A tag is `v` plus the version in `firmware/identity.json` (`v1.0.0`), or that plus a pre-release label of up to eight characters (`v1.0.0-beta.1`). When CI runs on a pre-release tag, the label replaces the commit in the image's 8-byte build field, so Settings → About → Firmware shows `1.0.0-beta.1` and debug reports give `firmware.build: beta.1`; a local build gets the same with `ECHO_BUILD_TAG=beta.1`. A pre-release ships the `device` preset, the build installed on the owner's player during development, which writes debug reports to `/.gnarlyr/debug` for testers to send. A release without a label ships the `release` preset, keeps the commit as its build and must be on `main`; a pre-release may be cut from an integration branch.

### Publish firmware

1. Add a `## <version>` section (for example, `## 1.0.0-beta.1`) to [the release notes](../firmware/docs/release-notes.md). Check the release body with `python3 tools/release_package.py notes <tag>`.
2. Tag the commit and push the tag.
3. Run **CI** with the tag as its ref. Wait for the workflow to pass and download its firmware artifact. Preflight embeds the pre-release label when the tag has one.
4. Verify the image checksum and get owner approval before installing it on the owner's player. Run the release checks in [AGENTS.md](../AGENTS.md) on that exact image.
5. After the checks pass and the owner approves publication of that build, dispatch **Firmware release** on the same tag. Supply the tag, the CI run ID, and owner approval. The workflow checks the tag, run, and hashes, then publishes the files from that run. It marks a pre-release separately from `latest`.

CI artifacts expire after seven days, so finish the device checks and publication within that time.

The first published stable firmware release makes these URLs available:

```text
https://github.com/0xdoublesharp/gnarlyr/releases/latest/download/GNARLYR.IMG
https://github.com/0xdoublesharp/gnarlyr/releases/latest/download/ECHOECHO.img
https://github.com/0xdoublesharp/gnarlyr/releases/latest/download/firmware.json
https://github.com/0xdoublesharp/gnarlyr/releases/latest/download/SHA256SUMS
```

For a pinned download, replace `latest/download` with `download/<tag>`, as in `https://github.com/0xdoublesharp/gnarlyr/releases/download/v1.0.0-beta.1/ECHOECHO.img`. Use a pinned URL for pre-releases. Installers should verify the image hash against the manifest, validate the image and model, and preserve the existing review and update checks. A matching hash confirms integrity. Use the project's release page to establish the source of the download.

## Desktop

The private desktop repository builds installers only on explicit requests or version tags. Its publication tool downloads a successful release-build artifact and creates a draft release here with a `desktop-vVERSION` tag. Publishing from an authenticated maintainer's machine needs no cross-repository token stored in private CI. Desktop releases do not replace the latest firmware release.

Desktop asset names are `gnarlyr-desktop-macos-arm64.dmg`, `gnarlyr-desktop-macos-x64.dmg`, `gnarlyr-desktop-windows-x64.exe`, and `gnarlyr-desktop-linux-x86_64.AppImage`, according to the selected build targets. A versioned link has this form:

```text
https://github.com/0xdoublesharp/gnarlyr/releases/download/desktop-vVERSION/gnarlyr-desktop-macos-arm64.dmg
```

Only publish platforms that actually built and passed their package check. Signing and distribution terms must be settled before public desktop distribution. Website publication and firmware/device installation remain separate actions.
