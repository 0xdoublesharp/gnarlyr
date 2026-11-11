# Builds and downloads

Public firmware and desktop installers are distributed as assets in [Gnarlyr GitHub Releases](https://github.com/0xdoublesharp/gnarlyr/releases). Keeping desktop source private does not require private download links. GitHub Actions artifacts are short-lived build outputs and require GitHub authentication; use release assets for website and installer downloads.

## Firmware

Every successful CI run uploads `GNARLYR.IMG`, `gnarlyr-source.tar.gz`, `firmware.json`, and `SHA256SUMS` as an Actions artifact. The image is a release-preset build for the full-size ECHO. CI does not write to a player or publish a release.

After the release checks in `AGENTS.md` pass on the player and the owner approves that exact release, tag its commit `vMAJOR.MINOR.PATCH`, matching `firmware/identity.json`. Dispatch **Firmware release** using that tag as the workflow ref, supply the same tag input, and confirm owner approval. The workflow validates the ref and version, runs both CI jobs, and publishes the checked bytes with matching source and hashes.

The first published firmware release makes these URLs available:

```text
https://github.com/0xdoublesharp/gnarlyr/releases/latest/download/GNARLYR.IMG
https://github.com/0xdoublesharp/gnarlyr/releases/latest/download/firmware.json
https://github.com/0xdoublesharp/gnarlyr/releases/latest/download/SHA256SUMS
```

For a pinned download, replace `latest/download` with `download/vMAJOR.MINOR.PATCH`. Installers should verify the image hash against the manifest, validate the image/model, and preserve the existing review and update checks. Hashes alone do not authenticate a publisher.

## Desktop

The private desktop repository builds installers only on explicit requests or version tags. Its publication tool downloads a successful release-build artifact and creates a draft release here with a `desktop-vVERSION` tag. Publishing from an authenticated maintainer's machine needs no cross-repository token stored in private CI. Desktop releases do not replace the latest firmware release.

Desktop asset names are `gnarlyr-desktop-macos-arm64.dmg`, `gnarlyr-desktop-macos-x64.dmg`, `gnarlyr-desktop-windows-x64.exe`, and `gnarlyr-desktop-linux-x64.AppImage`, according to the selected build targets. A versioned link has this form:

```text
https://github.com/0xdoublesharp/gnarlyr/releases/download/desktop-vVERSION/gnarlyr-desktop-macos-arm64.dmg
```

Only publish platforms that actually built and passed their package check. Signing and distribution terms must be settled before public desktop distribution. Website publication and firmware/device installation remain separate actions.
