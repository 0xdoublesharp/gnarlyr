# Snowsky ECHO firmware research

The static Gnarlyr.com firmware site lives in [`site/`](site/README.md). Open `site/index.html` to preview it; it needs no build step or dependencies.

Offline investigation of the **standard, full-size ECHO**, using the official 2.0.0 image. The device owner reported hardware 1.1.7 and successfully installed stock 2.0.0 before this investigation.

**Result:** REA maps stock Thumb code and several hardware interfaces. The tracked `firmware/` tree provides an open-source full-size ECHO foundation with an SDL simulator and a source-built Cortex-M3 image. This is not recovered original vendor source, and the custom image has not been tested on the owner's device.

Latest [REA hardware tracing and source-foundation evaluation](docs/hardware-interfaces.md). The custom source includes cassette menus, configurable playback buttons, full queue editing and [named saved mixtapes](firmware/docs/mixtapes.md).

The active delivery target and milestones are in [the working firmware goal](docs/goal.md). Current [recovery preparation](docs/recovery-preparation.md) records the built tool, connected-device observations and the pending physical connection check.

Read [the investigation findings](docs/initial-investigation.md) for verified addresses, limits, recovery status, and the next milestone.

## Build the source foundation

Focused checks build only selected dependencies. Use `python3 tools/firmware.py test --group host`, `test --group smoke`, or `test --regex EXPRESSION`; add `--list` to inspect the selection. Build one executable with `build --target echo_sim`. `--build-jobs` and `--test-jobs` set parallelism separately. See [development workflow](docs/development.md).

See [build prerequisites and commands](docs/building-firmware.md). From the project root, run `python3 tools/firmware.py setup`, then `python3 tools/firmware.py build --preset device` or `python3 tools/firmware.py test`. The wrapper keeps generated output on Scratch and does not access the player.

## Files

- `tools/echo_image.py`: validates the Rockchip CRC and module bounds; exports selected non-overlapping modules as a byte-preserving analysis ELF.
- `tools/run_analysis.py`: creates a fresh Scratch run, imports the ELF with the Cortex processor, and exports selected pseudocode/instructions.
- `tools/check_analysis.py`: compares Ghidra's imported identity, memory addresses, loaded-byte hashes, BSS state, and selected Thumb functions against the export map.
- `ghidra/PrepareEcho.java`: defines the architectural vector table and selected overlay entry before analysis.
- `ghidra/ExportEcho.java`: exports function observations and loaded-memory hashes.

All firmware, ELF files, Ghidra databases, raw reports, and generated pseudocode remain under `/Volumes/Scratch/SnowskyEcho`. They are not Git source. REA source is at `~/Workspaces/rea`; its isolated CLI runtime is on Scratch. Agent/MCP configuration was not changed.

## Repeat the checks

From this directory:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -v
PYTHONDONTWRITEBYTECODE=1 python3 tools/run_analysis.py
```

The runner uses a new timestamped run name; select a new explicit `--run-name NAME` to label a run. It refuses existing run directories. Defaults reference the installed Ghidra 12.1.4, existing Homebrew Java 21, and verified stock image on this Mac; `--help` lists overrides. No command accesses the player.

Latest verified run:

- Ghidra project: `/Volumes/Scratch/SnowskyEcho/build/verified-12-1-4/projects/echo-v200.gpr`
- Pseudocode and instructions: `/Volumes/Scratch/SnowskyEcho/reports/verified-12-1-4/ghidra/`
- Original image copy: `/Volumes/Scratch/SnowskyEcho/data/stock/ECHOV200.IMG`

The `.elf` files are analysis representations. **Never copy them to the player as firmware.**

## Repeat the REA decompilation

The actual tested CLI invocation uses only this process's configuration:

```sh
env GHIDRA_INSTALL_DIR=/Volumes/Scratch/SnowskyEcho/tools/ghidra_12.1.4_PUBLIC \
  JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  REA_GHIDRA_LANGUAGE_ID=ARM:LE:32:Cortex \
  REA_GHIDRA_SEED_FILE=/Volumes/Scratch/SnowskyEcho/build/initial-investigation/rea-seeds.tsv \
  TMPDIR=/Volumes/Scratch/rea/t \
  JAVA_TOOL_OPTIONS='-Duser.home=/Volumes/Scratch/SnowskyEcho/tmp/initial-investigation/java-home -Djava.io.tmpdir=/Volumes/Scratch/rea/t -XX:ActiveProcessorCount=4' \
  node /Volumes/Scratch/rea/build/snowsky-echo/runtime/node_modules/rea-agents/scripts/rea.mjs \
  decompile /Volumes/Scratch/SnowskyEcho/build/initial-investigation/echo-main-overlay001.elf \
  --procedure 0x0307e8c0 --provider ghidra --json \
  > /Volumes/Scratch/rea/reports/snowsky-echo/overlay001-decompile.json
```

The seed names an entry inferred from the selected module's start. It does not assert that this was the original developer's function name. REA's Cortex setting is scoped to these ARM ELF inputs and must not be applied to unrelated targets.

## Persistent REA session

Run `node tools/rea_session.mjs /Volumes/Scratch/SnowskyEcho/reports/NEW-RUN` and send newline-delimited JSON requests such as `{"name":"open_binary","arguments":{"path":"/Volumes/Scratch/SnowskyEcho/build/initial-investigation/echo-main-overlay001.elf","provider_id":"ghidra"}}`, then `{"name":"procedure_pseudo_code","arguments":{"procedure":"0x03061bee"}}`. Send `{"name":"close_binary","arguments":{}}` followed by `{"close":true}` to release the provider and exit. `{"schema":"TOOL_NAME"}` prints the actual server-advertised input schema.

For a different selected overlay, set `REA_GHIDRA_SEED_FILE` to its Scratch seed file before launching the persistent client. The display investigation uses `/Volumes/Scratch/SnowskyEcho/build/panel-map/rea-seeds.tsv` with the main+4 ELF; seed names describe inferred roles, not recovered vendor symbols.
