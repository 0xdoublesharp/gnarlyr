# Device timing

Measuring library scan and cover times on the player, in the same terms as the emulator's scan bench: library browsable, covers done and a rescan with nothing changed. It also measures Cover Flow cover latency, track start to the Now Playing cover, and the first screen after a cold start.

The player records timestamps in its debug event log (Debug loop, Event log, in `architecture.md`). `tools/scan_timing.py` writes the debug scripts and turns the reports into one table. Only `device` builds record these events; in `release` builds the calls compile to nothing.

## The test library

`tools/test_library.py` builds a card layout from the Free Music Archive's `fma_small` set. The set's clips are under Creative Commons licences, each with its own attribution. The layout is `Artist/Album/NN Title.ext`, with tags taken from `fma_metadata`. The mix is 50% MP3 as published, 20% FLAC, 15% M4A (AAC), 10% Ogg Vorbis and 5% Opus. The art is generated: embedded art and folder images, including progressive JPEG, CMYK JPEG, EXIF-rotated JPEG and PNG, and a few albums have no art. A few albums are joined into one file with a CUE sheet, and a few into an M4B with chapters. The builder writes `CREDITS.txt` (title, artist, licence and source for every clip) into the library and `<library>-manifest.json` beside it.

The music and the libraries stay on Scratch. They are for private testing, and audio is never committed.

```sh
firmware/.venv/bin/python firmware/tools/test_library.py --tracks 4000
firmware/.venv/bin/python firmware/tools/test_library.py --tracks 8000
```

Each command writes `/Volumes/Scratch/SnowskyEcho/data/test-library/fma-<tracks>`. It needs `fma_small.zip` and `fma_metadata.zip` in `/Volumes/Scratch/SnowskyEcho/data/fma/`, plus `ffmpeg` on PATH. The archives use bzip2 compression, which macOS `unzip` cannot read, so the builder reads them with Python's `zipfile`. The ffmpeg build here has no libvorbis, so the builder uses ffmpeg's own Vorbis encoder.

A few `fma_small` MP3s are damaged: neither mutagen nor ffmpeg finds their audio frames. The builder skips such a track, logs it and lists it under `skipped` in the manifest. A CUE or M4B album with a damaged part is skipped whole. When skips leave the library short, the next albums in the same seeded order make up the count. Albums are taken whole, so a library can hold a few more tracks than asked for. A rerun keeps every finished file: a track counts as finished once its tags are written, which is the last step. Unfinished files are written again.

## Script lines and events

| Script | Lines | Measures |
|---|---|---|
| `rescan` | `rescan check`, `wait 8000` | the music check Settings → Rescan library runs, from the line to `scan up_to_date` |
| `coverflow` | `open coverflow`, then three `down` and one `up`, 1.5 s apart | open to the last cover loaded, and each step to its last cover loaded |
| `nowplaying` | `open home`, Music → Albums, the third album, play its first track | track start to the first full-size cover loaded |

`open nowplaying` and `rescan full` are also accepted on a script line, but no script here uses them. A script reconnects USB 2 s after its last line, which would interrupt a full rescan, so the first scan in step 2 is the full-scan measurement.

The `nowplaying` script follows the default Music menu, where Albums is the fifth entry. Hiding an entry in Settings moves it.

The scan figures come from the `scan` events: `start`, `library` once the library can be browsed, and `covers`/`covers_done`/`covers_stopped` around each background cover pass. Cover loads are `cover full|half <album> found|missing`; a load counts whether or not the album has art. The event log keeps the last 48 events. The table names a report that has lost the start of a scan or a script, and that report's figures are then missing.

## Steps on the player

These steps need the owner's approval for the session (`../AGENTS.md`). `<tag>` is the running build's tag; `python3 tools/echo_debug.py status` prints it. There are three report slots, so copy the reports after each step.

1. Install a `device` build that contains this kit (`device-install.md`).
2. **First scan.** Copy the library's folders onto the card, then eject. The scan starts once the USB session ends; leave the player alone until the covers are done. At 4,000 tracks the emulator took about 2.5 minutes. Plug the cable back in and copy the reports:
   `mkdir -p /Volumes/Scratch/SnowskyEcho/reports/device-timing/scan && cp /Volumes/Gnarlyr/.gnarlyr/debug/report*.txt "$_"`
3. **Rescan with nothing changed.**
   `firmware/.venv/bin/python firmware/tools/scan_timing.py script rescan --build <tag> > /Volumes/Scratch/SnowskyEcho/tmp/rescan.txt`
   `python3 tools/echo_debug.py run /Volumes/Scratch/SnowskyEcho/tmp/rescan.txt`
   `echo_debug run` ejects the card, waits for the player to reconnect and prints the script results. Copy the reports to `.../device-timing/rescan`.
4. **Cover Flow** and **Now Playing**: the same as step 3, with the `coverflow` and `nowplaying` scripts, each into its own folder.
5. **Cold start.** Switch the player off. With the cable plugged in, switch it on. The report written within 10 s of power-on is a cold-start report. Copy it to `.../device-timing/boot`.
6. Make the table:
   `firmware/.venv/bin/python firmware/tools/scan_timing.py report /Volumes/Scratch/SnowskyEcho/reports/device-timing/*`
   `--json` prints the same figures as JSON, including each cover pass.
