# Credits

Podium is licensed under GPL-3.0-or-later (`LICENSE`). It builds on the work of others:

- **[EchoPod](https://github.com/BasilSkyWalk/echopod)** 0.6.0 by BasilSkyWalk (GPL-3.0-or-later): the firmware Podium is forked from. The interface, the library, playback, the updater and most of the code are EchoPod's; Podium ports it to the full-size Echo and changes the interface around iPod-sized text.
- **[ReChord](https://github.com/brnomt/ReChord)** (GPL-3.0): the community research into the Echo Mini's hardware. Its notes, drivers and debrick guide are where most of the hardware knowledge in `docs/hardware.md`, and values such as the panel and DAC set-up, come from.
- **The livetrack.club Echo Mini debrick guide**, on which the maskrom recovery steps build.
- **[libmad](https://codeberg.org/tenacityteam/libmad)** 0.16.4, by Underbit Technologies and the Tenacity team (GPL-2.0-or-later): the MP3 decoder on the player (`third_party/libmad`).
- **[FAAD2](https://github.com/knik0/faad2)** 2.11.4, by M. Bakker and Nero AG (GPL-2.0-or-later): the AAC-LC decoder on the player (`third_party/faad2`, fixed point, no SBR). "Code from FAAD2 is copyright (c) Nero AG, www.nero.com." The ALAC decoder (`src/codec/alac.c`) is Podium's own, written from the published format and checked against ffmpeg's output.
- **[dr_libs](https://github.com/mackron/dr_libs)** by David Reid (public domain or MIT-0): FLAC, MP3 and WAV decoding in the simulator (`third_party/dr_libs`).
- **[Inter](https://github.com/rsms/inter)** by Rasmus Andersson and the Inter project authors (SIL Open Font License 1.1): the UI font, pre-rendered into `src/fonts` (`assets/fonts/inter/OFL.txt`).
- **[rkdeveloptool](https://github.com/rockchip-linux/rkdeveloptool)** by Rockchip (GPL-2.0): used for maskrom installs and recovery. It isn't included; `tools/patch_rkdeveloptool.py` patches a checkout for the Echo.
- **[Unicorn](https://www.unicorn-engine.org)** (GPL-2.0), **[Pillow](https://python-pillow.org)** (MIT-CMU), **[mutagen](https://github.com/quodlibet/mutagen)** (GPL-2.0-or-later) and **[fontTools](https://github.com/fonttools/fonttools)** (MIT): the firmware emulator tests and the cover, tag and font tools (`tools/requirements.txt`). They aren't part of the firmware.
- **[Ghidra](https://ghidra-sre.org)** (Apache-2.0): used to study FiiO's Echo firmware for the port. Nothing of FiiO's is included.
- **[pid.codes](https://pid.codes)**: the USB vendor ID `1209`.
- Built with **[Claude Code](https://claude.com/claude-code)** by Anthropic.

Snowsky and FiiO are trademarks of their owners. Podium isn't made, endorsed or supported by FiiO.
