# SnowskyEcho firmware development

- Follow the active goal in docs/goal.md. Work on copies of the standard ECHO firmware. Analysis tools must not flash the device or perform raw device writes; prepare concrete candidates and exact-model recovery before hardware deployment.
- Firmware, derived ELF files, Ghidra databases, logs, and generated pseudocode belong under /Volumes/Scratch/SnowskyEcho, never in Git.
- Keep scripts and short human-written findings here. Use Python's standard library for image tooling.
- Validate container CRC, bounds, selected memory overlaps, and exported byte identity before analysis. Module execution addresses can overlap because code is overlaid at runtime.
- A derived ELF is an analysis representation, never a flashable image. Recovered names and decompiled types are hypotheses unless independently established.
- Run PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests after changing the parser/exporter.
