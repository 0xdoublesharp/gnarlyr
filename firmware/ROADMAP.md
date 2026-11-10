# Gnarlyr feature status

Gnarlyr 1.0.0 is not released. Developer builds run on the owner’s full-size ECHO; the exact hardware checks and remaining limits are recorded in `docs/known-issues.md`.

Status values:

- **emulator and simulator:** in the code and passing its host tests, the simulator or the ARM emulator. Not run on a player.
- **not started:** not in this tree.

| Feature | Status | Notes |
|---|---|---|
| FLAC, MP3, WAV, AAC-LC and ALAC playback, 44.1 to 192 kHz | emulator and simulator | |
| Gapless playback and fades | emulator and simulator | |
| Resume after power-off | emulator and simulator | |
| Library and album covers built on the player | emulator and simulator | |
| Library built on a computer (`tools/echo_sync.py`) | emulator and simulator | Host tests only |
| CUE sheets | emulator and simulator | |
| Browse lists, Cover Flow, Now Playing | emulator and simulator | |
| Up Next, shuffle, repeat | emulator and simulator | |
| M3U playlists and the Favorite Tracks playlist | emulator and simulator | |
| Album favorites | emulator and simulator | |
| Audiobooks and podcasts with bookmarks, `.m4b` chapters | emulator and simulator | |
| Ten-band equalizer, saved curves, curve files | emulator and simulator | Host tests only; the simulator does not apply it |
| Text sizes and highlight colours | emulator and simulator | |
| Portrait mode | emulator and simulator | |
| Button lock and skip gestures | emulator and simulator | |
| Wired headset buttons | emulator and simulator | Host tests only |
| Bluetooth headphones | emulator and simulator | Host tests and a simulated module |
| Battery level, charge screen, low-battery shutdown, auto off | emulator and simulator | |
| USB mass storage | emulator and simulator | The emulator has no USB traffic; host tests cover the protocol |
| USB DAC mode | emulator and simulator | Host tests only |
| Updater: `GNARLYR.IMG`, and FiiO's image for the way back | emulator and simulator | |
| Link-time optimisation, USB overlay, size report | emulator and simulator | |
| 12 ms DAC output power-up wait | emulator and simulator | An earlier build with this wait was quiet on the owner's unit |
| Own name, identifier, version, data folder and update file | emulator and simulator | |
| Full queue editing with a session journal | not started | In the earlier Podium 4.5.5-based build; to be ported |
| Named mixtapes | not started | In the earlier build; to be ported |
| Session restore bound to library content | not started | In the earlier build; to be ported |
| Configurable buttons | not started | In the earlier build; to be merged with the skip gestures and the lock |
| Colour themes, vector icons, cassette art | not started | In the earlier build; to be ported |
| New settings file driven by an option table | not started | |
| Hardware discovery report | not started | |
| 96 MHz clock for screen-off playback, battery percent smoothing, hold to power on | not started | From EchoPod |
| Lyrics | not started | From EchoPod |
| Listen history with a clock | not started | From EchoPod |
| Opus playback | not started | From EchoPod |
| Chinese, Japanese, Korean and other scripts | not started | Glyph packs, from EchoPod |
| Values re-derived from FiiO 2.0.0: volume curve, Bluetooth level, jack pulls, DAC filters, power-down order | not started | |
| Browser flasher | not started | |

Each deployment needs a prepared candidate, the exact-model recovery route and the applicable owner approval. [`docs/known-issues.md`](docs/known-issues.md) lists what that install has to prove.
