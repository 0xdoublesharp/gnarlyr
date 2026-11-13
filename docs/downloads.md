# Builds and downloads

Public firmware and desktop installers are distributed as assets in [Gnarlyr GitHub Releases](https://github.com/0xdoublesharp/gnarlyr/releases). Keeping desktop source private does not require private download links. GitHub Actions artifacts are short-lived build outputs and require GitHub authentication; use release assets for website and installer downloads.

## Firmware

Every successful CI run packages its image with `tools/release_package.py` and uploads `GNARLYR.IMG`, `ECHOECHO.IMG` (the same bytes under the name FiiO's updater installs), `gnarlyr-source.tar.gz`, `firmware.json` and `SHA256SUMS` as an Actions artifact. CI does not write to a player or publish a release.

A tag is `v` plus the version in `firmware/identity.json` (`v1.0.0`), or that plus a pre-release label of up to eight characters (`v1.0.0-beta.1`). When CI runs on a pre-release tag, the label replaces the commit in the image's 8-byte build field, so Settings → About → Firmware shows `1.0.0-beta.1` and debug reports give `firmware.build: beta.1`; a local build gets the same with `ECHO_BUILD_TAG=beta.1`. A pre-release ships the `device` preset, the build installed on the owner's player during development, which writes debug reports to `/.gnarlyr/debug` for testers to send. A release without a label ships the `release` preset, keeps the commit as its build and must be on `main`; a pre-release may be cut from an integration branch.

After the release checks in `AGENTS.md` pass on the player and the owner approves that exact build:

1. Add a `## <version>` section (`## 1.0.0-beta.1`) to `firmware/docs/release-notes.md`; it becomes the release body. `python3 tools/release_package.py notes <tag>` prints it.
2. Tag the commit and push the tag.
3. Run **CI** with the tag as its ref. Its preflight sets the label, builds, and packages the image.
4. Dispatch **Firmware release** with the same tag as the workflow ref, the tag, that CI run's ID and owner approval. It checks the tag, the CI run and every hash with `tools/release_package.py verify`, then publishes that run's exact files. It does not rebuild the image. A pre-release is published as a GitHub pre-release and does not become `latest`.

CI artifacts expire after seven days, so finish the device checks and publication within that time.

The first published firmware release makes these URLs available:

```text
https://github.com/0xdoublesharp/gnarlyr/releases/latest/download/GNARLYR.IMG
https://github.com/0xdoublesharp/gnarlyr/releases/latest/download/firmware.json
https://github.com/0xdoublesharp/gnarlyr/releases/latest/download/SHA256SUMS
```

For a pinned download, replace `latest/download` with `download/<tag>`. A pre-release is reached only by its pinned URL, for example `https://github.com/0xdoublesharp/gnarlyr/releases/download/v1.0.0-beta.1/ECHOECHO.IMG`. Installers should verify the image hash against the manifest, validate the image/model, and preserve the existing review and update checks. Hashes alone do not authenticate a publisher.

## Desktop

The private desktop repository builds installers only on explicit requests or version tags. Its publication tool downloads a successful release-build artifact and creates a draft release here with a `desktop-vVERSION` tag. Publishing from an authenticated maintainer's machine needs no cross-repository token stored in private CI. Desktop releases do not replace the latest firmware release.

Desktop asset names are `gnarlyr-desktop-macos-arm64.dmg`, `gnarlyr-desktop-macos-x64.dmg`, `gnarlyr-desktop-windows-x64.exe`, and `gnarlyr-desktop-linux-x86_64.AppImage`, according to the selected build targets. A versioned link has this form:

```text
https://github.com/0xdoublesharp/gnarlyr/releases/download/desktop-vVERSION/gnarlyr-desktop-macos-arm64.dmg
```

Only publish platforms that actually built and passed their package check. Signing and distribution terms must be settled before public desktop distribution. Website publication and firmware/device installation remain separate actions.
