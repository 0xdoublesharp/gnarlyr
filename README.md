# Snowsky ECHO firmware research

Offline investigation of the **standard, full-size ECHO**, using the official 2.0.0 image. The device owner reported hardware 1.1.7 and successfully installed stock 2.0.0 before this investigation.

**Result:** the container can be mapped and its Thumb code decompiled. Ghidra and REA both produced usable pseudocode. This is a foundation for writing firmware; it is not recovered original source or a replacement firmware release.

The active delivery target and milestones are in [the working firmware goal](docs/goal.md).

Read [the investigation findings](docs/initial-investigation.md) for verified addresses, limits, recovery status, and the next milestone.

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
