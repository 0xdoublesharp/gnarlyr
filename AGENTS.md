# Gnarlyr development

- The firmware source is under firmware/ with its GPL license and credits; read firmware/CLAUDE.md before changing that tree. Use tools/firmware.py for builds and tests; it routes output to Scratch. Python 3.14 reproduces the firmware Unicode tables.
- Never write to the player, start a maskrom session or read it raw without the owner's approval for that action; an earlier approval does not carry over. The one exception is a standing approval the owner has granted in writing: its scope and every install made under it are recorded in notes/approvals.md. If that file is absent or does not cover the action, ask. Before any install, have a concrete, reviewable image and a recovery route for this exact model. Never use Echo Mini loaders or flash offsets.
- Install order is slot 2 first, every write read back and compared, then slot 1. Writes go only to the two firmware slots and the user volume. Never overwrite the verified stock image or the private backup of the owner's unit under .local/device-backups.
- Simulator, emulator and packaging results establish software behaviour only. Describe a feature as working after it has been seen on the player. A release needs, on the player: power cycles, playback on both outputs, state kept across restarts, charging and normal file transfers.
- Generated firmware images, derived ELF files, Ghidra databases, logs and pseudocode belong under /Volumes/Scratch/SnowskyEcho, never in Git.
- Commit reference documentation only. Plans, status logs, investigation narratives and session handoffs go in notes/, which Git ignores.
- Stock firmware analysis: validate container CRC, bounds, selected memory overlaps and exported byte identity before analysis. Module execution addresses can overlap because code is overlaid at runtime. A derived ELF is an analysis representation, never a flashable image. Recovered names and decompiled types are hypotheses unless independently established. Use Python's standard library for the root tools, and run PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests after changing the parser or exporter.

## Website deployment

Gnarlyr.com is hosted on DoublePaaS. Use the `deploy-doublepaas` skill and read
[the deployment contract](docs/deployment.md) before changing hosting or publishing
the static site. Firmware builds and device installation remain separate from
website publication.
