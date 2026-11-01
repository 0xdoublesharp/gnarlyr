# Credits

EchoPod is licensed under GPL-3.0-or-later (`LICENSE`). It builds on the work of others:

- **[ReChord](https://github.com/brnomt/ReChord)** (GPL-3.0): the community research into the Echo Mini's hardware. Its notes, drivers and debrick guide are where most of the hardware knowledge in `docs/hardware.md`, and values such as the panel and DAC set-up, come from.
- **The livetrack.club Echo Mini debrick guide**, on which the maskrom recovery steps build.
- **[libmad](https://codeberg.org/tenacityteam/libmad)** 0.16.4, by Underbit Technologies and the Tenacity team (GPL-2.0-or-later): the MP3 decoder on the player (`third_party/libmad`).
- **[FAAD2](https://github.com/knik0/faad2)** 2.11.4, by M. Bakker and Nero AG (GPL-2.0-or-later): the AAC-LC decoder on the player (`third_party/faad2`, fixed point, no SBR). "Code from FAAD2 is copyright (c) Nero AG, www.nero.com." The ALAC decoder (`src/codec/alac.c`) is Podium's own, written from the published format and checked against ffmpeg's output.
- **[dr_libs](https://github.com/mackron/dr_libs)** by David Reid (public domain or MIT-0): FLAC, MP3 and WAV decoding in the simulator (`third_party/dr_libs`).
- **[Inter](https://github.com/rsms/inter)** by Rasmus Andersson and the Inter project authors (SIL Open Font License 1.1): the UI font, pre-rendered into `src/fonts` (`assets/fonts/inter/OFL.txt`).
- **[rkdeveloptool](https://github.com/rockchip-linux/rkdeveloptool)** by Rockchip (GPL-2.0): used for maskrom recovery. It isn't included; `tools/patch_rkdeveloptool.py` patches a checkout for the Echo Mini.
- **[pid.codes](https://pid.codes)**: the USB vendor ID `1209`.

Snowsky and FiiO are trademarks of their owners. EchoPod isn't made, endorsed or supported by FiiO.
