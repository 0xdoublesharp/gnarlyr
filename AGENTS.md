# SnowskyEcho firmware development

- Follow the active goal in docs/goal.md. Work on copies of the standard ECHO firmware. Analysis tools must not flash the device or perform raw device writes; prepare concrete candidates and exact-model recovery before hardware deployment.
- Generated firmware packages, derived ELF files, Ghidra databases, logs, and pseudocode belong under /Volumes/Scratch/SnowskyEcho, never in Git. Preserve original device recovery backups as private source data under .local/device-backups on Data; analysis working copies stay on Scratch.
- Track firmware source under firmware/ with its GPL license and credits; read firmware/CLAUDE.md before changing that tree. Keep short findings under docs/. Use Python's standard library for root image/build tooling. Use tools/firmware.py for Scratch-routed builds; Python 3.14 reproduces the firmware Unicode tables.
- Validate container CRC, bounds, selected memory overlaps, and exported byte identity before analysis. Module execution addresses can overlap because code is overlaid at runtime.
- A derived ELF is an analysis representation, never a flashable image. Recovered names and decompiled types are hypotheses unless independently established.
- Run PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests after changing the parser/exporter.

## Website deployment

Gnarlyr.com is hosted on DoublePaaS. Use the `deploy-doublepaas` skill and read
[the deployment contract](docs/deployment.md) before changing hosting or publishing
the static site. Firmware builds and device installation remain separate from
website publication.
