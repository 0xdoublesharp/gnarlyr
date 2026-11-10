# Credits

Gnarlyr is made by doublesharp and licensed under GPL-3.0-or-later (`LICENSE`). It builds on the work of others:

- **[Podium](https://podium-echo.online)** 4.5.9 (GPL-3.0-or-later): the firmware Gnarlyr starts from (`UPSTREAM.md`). Podium's author ported EchoPod to the full-size Echo and wrote the board support for this player, the updater's support for this player's images and FiiO's Echo image, the interface around larger text with three text sizes, portrait mode, the equalizer with its ten bands, saved curves and curve files, M4A and `.m4b` chapter support, CUE sheets, the wired headset buttons, the button lock, the Favorite Tracks playlist, the `MASKROM.REQ` loader request, and the ALAC decoder (`src/codec/alac.c`), written from the published format and checked against ffmpeg's output.
- **[EchoPod](https://github.com/BasilSkyWalk/echopod)** by BasilSkyWalk (GPL-3.0-or-later): the Echo Mini firmware Podium is forked from, at 0.6.0. The interface, the library, playback, the updater and most of the code are EchoPod's. Gnarlyr also ports later EchoPod work directly: the size report, link-time optimisation, the overlay code check and the USB overlay with its shared loader (`UPSTREAM.md` lists the commits).
- **[ReChord](https://github.com/brnomt/ReChord)** (GPL-3.0): the community research into the Echo Mini's hardware. Its notes, drivers and debrick guide are where most of the hardware knowledge in `docs/hardware.md`, and values such as the panel and DAC set-up, come from.
- **The livetrack.club Echo Mini debrick guide**, on which the maskrom recovery steps build.
- **[libmad](https://codeberg.org/tenacityteam/libmad)** 0.16.4, by Underbit Technologies and the Tenacity team (GPL-2.0-or-later): the MP3 decoder on the player (`third_party/libmad`).
- **[FAAD2](https://github.com/knik0/faad2)** 2.11.4, by M. Bakker and Nero AG (GPL-2.0-or-later): the AAC-LC decoder on the player (`third_party/faad2`, fixed point, no SBR). "Code from FAAD2 is copyright (c) Nero AG, www.nero.com."
- **[dr_libs](https://github.com/mackron/dr_libs)** by David Reid (public domain or MIT-0): FLAC, MP3 and WAV decoding in the simulator (`third_party/dr_libs`).
- **[Inter](https://github.com/rsms/inter)** by Rasmus Andersson and the Inter project authors (SIL Open Font License 1.1): the UI font, pre-rendered into `src/fonts` (`assets/fonts/inter/OFL.txt`).
- **[rkdeveloptool](https://github.com/rockchip-linux/rkdeveloptool)** by Rockchip (GPL-2.0): used for maskrom installs and recovery. It isn't included; `tools/patch_rkdeveloptool.py` patches a checkout for the Echo.
- **[Unicorn](https://www.unicorn-engine.org)** (GPL-2.0), **[Pillow](https://python-pillow.org)** (MIT-CMU), **[mutagen](https://github.com/quodlibet/mutagen)** (GPL-2.0-or-later) and **[fontTools](https://github.com/fonttools/fonttools)** (MIT): the firmware emulator tests and the cover, tag and font tools (`tools/requirements.txt`). They aren't part of the firmware.
- **[Ghidra](https://ghidra-sre.org)** (Apache-2.0): used to study FiiO's Echo firmware. Nothing of FiiO's is included.
- **[pid.codes](https://pid.codes)**: the USB vendor ID `1209`.
- Built with **[Claude Code](https://claude.com/claude-code)** by Anthropic.

Snowsky and FiiO are trademarks of their owners, and Echo is their product name. Gnarlyr is not made, endorsed or supported by FiiO.
