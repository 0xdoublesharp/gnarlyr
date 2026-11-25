# Real-hardware validation playbooks

Use these playbooks to test a full-size SNOWSKY ECHO from macOS over USB storage and USB Audio 2.0. The playbooks exercise the connected player. Follow the [device approval rules](../AGENTS.md) before running them on the owner's unit.

The checks use temporary files on internal storage and leave microSD music intact. Add `--save` to write JSON reports to `/Volumes/Scratch/SnowskyEcho/reports/hw-playbooks/`. [Playbook source](../tools/echo_hw_check.py).

## Quick commands

Run the standard set (`storage`, `ui`, `playback`, `rescan`, and `dac`). It uses 32-bit audio for the DAC check by default.

```sh
python3 tools/echo_hw_check.py playbook all --silent --save
```

Run `certify` to check `storage`, `codecs`, `settings`, `ui`, `rescan`, and `dac`. The DAC sweep tests 16-, 24-, and 32-bit formats.

```sh
python3 tools/echo_hw_check.py playbook certify --silent --save
```

Run an individual playbook:

```sh
python3 tools/echo_hw_check.py playbook storage
python3 tools/echo_hw_check.py playbook codecs
python3 tools/echo_hw_check.py playbook settings
python3 tools/echo_hw_check.py playbook ui
python3 tools/echo_hw_check.py playbook playback
python3 tools/echo_hw_check.py playbook rescan
python3 tools/echo_hw_check.py playbook dac --rates 44100,48000,96000,192000 --bits-list 16,24,32 --silent
```

Add `--build <sha7>` to pin and verify the installed commit, or `--json` for machine-readable output.

## Playbook matrix

| Playbook | What it drives on real silicon | Pass criteria & captured metrics |
|---|---|---|
| `storage` | Uncached (`F_NOCACHE`) 8 MiB sequential + 4 × 256 KiB burst write/read/SHA-256 on internal `/.gnarlyr/debug/`, then full temp + AppleDouble cleanup | 100% SHA-256 match, write/read MB/s, firmware `usb.timing` (`receive_ms`, `card_ms`, `ui_ms`, `fast`, `fallbacks`) |
| `codecs` | Active playback matrix across SD `Batch 4 Checks`, `Codec Checks`, and `Beta Checks` (WAV 8/44/96k, FLAC 44/96k small blocks/192k 24-bit, MP3 CBR/VBR, AAC-LC 44/16k, ALAC 44k/24, Vorbis 44/48k, Opus/Ogg Opus, AIFF, WMA v2, APE, DSD64, 44.1→48k transition, screen-off audio, damaged-FLAC recovery) | Every playable track `audio=1`, `blocks>0`, 0 underruns/output_underruns, 0 faults, Core 0 / Core 1 %, `buffered_ms`, `read_max_us`, `handoff_max_us` |
| `settings` | Active 96 kHz / 24-bit FLAC playback while sweeping DSP (`Equalizer`, `Crossfeed`, `ReplayGain`, `Prevent clipping`, `Skip silence`, `Speech boost`), DAC hardware (`Gain`, `DAC filter`, `Playback speed`), Now Playing styles (`Cover`, `Cassette`, `VU meters`, `Spectrum`), layout (`Portrait`, `Text size`), worst-case full stack, and `screen off`, then restores the owner's original `settings.bin` values | Every `set` line `ok`, `audio=1`, 0 underruns, 0 faults, per-setting CPU/frame/buffer impact table, exact owner settings restored |
| `ui` | Home → RTC read → Cover Flow (`down × 3`, `up × 1`) → screenshots (`home`, `coverflow`) with 1 kHz PC profiler and two `measure 2000` windows | All script lines `ok`, `script.state=finished`, `rtc=ok`, 0 faults, `frame_avg_us`/`frame_worst_us`, VOP `send_avg_us`, `core0_busy_%`, top CPU functions |
| `playback` | Now Playing screen-on (`measure 3000`) → `screen off` (`measure 3000`) → `screen on` → restore pause + Home with 1 kHz PC profiler | All script lines `ok`, 0 underruns, 0 faults, Core 0 / Core 1 busy %, `buffered_ms`, `read_max_us`, `handoff_max_us`, screen-on vs screen-off comparison, top CPU functions |
| `rescan` | `rescan check` with 1 kHz PC profiler on the player's real internal + SD library | All script lines `ok`, 0 faults, exact `scan` event duration (`start` → `up_to_date` ms), top CPU functions |
| `dac` | Automated `reconnect dac` + 12-key in-DAC torture sequence streamed via CoreAudio at 44.1 / 48 / 96 / 192 kHz across 16/24/32-bit (`--silent`) | Exact `usb.dac.rate` & `usb.dac.bits`, 0 underruns, 0 overflows, 0 realigns, `max_gap_us <= 200`, symbolized `usb.dac.max_gap_pc` |

## Physical release checks

Run `python3 tools/echo_hw_check.py checklist` for the manual checklist. Before release, check both headphone outputs by ear and verify power cycles, saved state across restarts, charging, and normal file transfers on the player. [Release requirements](../AGENTS.md).
