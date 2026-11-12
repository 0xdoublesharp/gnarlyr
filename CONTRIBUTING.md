# Contributing to Gnarlyr

Help improve firmware, fix a bug, translate the interface, or make the guides clearer. Start with an issue for a substantial feature so its behavior and device budget can be agreed before implementation.

## Choose the right repository

This repository contains firmware, its simulator and emulator, the website, and public host tools. The desktop app is developed separately in the private [gnarlyr-desktop repository](https://github.com/0xdoublesharp/gnarlyr-desktop).

Read [AGENTS.md](AGENTS.md) and, before editing firmware, [firmware/CLAUDE.md](firmware/CLAUDE.md). The [development workflow](docs/development.md) explains file ownership, focused checks, and integration.

## Make a focused change

1. Branch from current `main`. For concurrent work, use a separate worktree and agree on shared files before editing.
2. Keep a commit to one complete behavior: implementation, relevant tests, necessary generated output, and reference documentation.
3. Regenerate assets with `python3 tools/firmware.py regen --jobs 2`; do not edit generated files by hand.
4. Run focused tests and the smoke group. For firmware behavior, also run the size report for both image presets.
5. Open a pull request describing the behavior, checks, and remaining device checks.

```sh
python3 tools/firmware.py test --jobs 2 --regex '^test_player$'
python3 tools/firmware.py test --group smoke --jobs 2
python3 tools/firmware.py regen --check --jobs 2
python3 tools/firmware.py sizes --jobs 2
```

Use a short imperative commit subject. Scopes help identify the component: `fix(firmware): preserve shuffle after restart`, `feat(tools): validate language packs`, or `docs(site): explain USB updates`. Preserve the original message when cherry-picking upstream work and credit hand ports.

## Check the result

The integration owner runs one complete suite on the combined candidate. Feature workers run focused tests; do not start competing full-suite runs. CI runs portable checks on Linux. Native macOS FAT/exFAT mount and read/write checks run with the required system permissions on trusted main/release refs; pull requests use Linux. Fast tool/generated-data checks run before six deterministic Linux shards. Together the shards and preflight cover every CTest test; CTest retains each shard's required fixtures. Documentation-only changes skip the expensive workflow.

Root parser, exporter, and host-tool changes use:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests
```

UI changes need checks at all affected text sizes and orientations. Keep release/device resident-code reserves of at least 4 KiB/1 KiB and stay within every slot and RAM budget. Source and simulator checks describe software behavior; report player behavior only after checking it on the player.

## Translate and document

The English catalog is `firmware/assets/lang/en.txt`. Preserve placeholders and line breaks, validate packs, and review language and display fit. See the [language-pack guide](docs/language-packs.md). Keep docs factual and credit upstream results explicitly.

## Protect devices and data

Device access requires the owner's approval for the exact action. Preserve recovery images, private backups, music, and unrelated work. Put disposable builds and captures under Scratch; keep worktrees on backed-up storage. Commit source and reference docs, with plans and session notes in ignored `notes/`. Never commit credentials, personal data, dependencies, build output, or run evidence.

Report security issues through [private vulnerability reporting](https://github.com/0xdoublesharp/gnarlyr/security/advisories/new). Follow the [code of conduct](CODE_OF_CONDUCT.md).
