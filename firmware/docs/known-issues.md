# Gnarlyr known issues

Gnarlyr 1.0.0 is not released. The developer firmware runs on the owner's full-size Echo; the recorded checks below include Gnarlyr and its earlier foundation. It comes with no warranty: use it at your own risk. Read this before you install a build, and keep a way back ([`device-recovery.md`](device-recovery.md)).

## What has been checked

- **Software checks:** host tests, the simulator and the ARM emulator cover boot and updates, memory bounds, queue/session persistence, mixtapes, controls, Listening and lyrics. They do not reproduce analog output, Bluetooth radio operation or full USB host traffic.
- **On the owner's Echo (full size, 4 GB):** the owner confirmed display, menus, buttons, music playback, volume, internal/microSD USB storage and quiet audio on the earlier Podium 4.5.5-based build. The rebuilt Podium 4.5.9 foundation was then confirmed for file-copy installation, USB storage, playback, quiet audio and portrait mode. Later developer builds exercised reports, scripts, automatic update/restart/reconnect and frame/load measurements. The developer build `33757eb` and the demand-loaded-module build `6d027ed` were installed through the file-copy loop and returned on USB with their build reports confirmed. The unit was also restored to FiiO 2.0.0 and back through maskrom; the recovery steps in [`device-install.md`](device-install.md) and [`device-recovery.md`](device-recovery.md) are the ones used.
- **Gnarlyr on the owner's Echo:** file-copy installation and automatic restart/reconnect report the expected build. Home, Now Playing, Up Next, Mixtapes, Listening, Controls and the missing-lyrics view were checked on the display at medium text size in landscape. A named mixtape was saved before an update, survived the restart and loaded into playback and Up Next afterward. A 10-second WAV playback window had zero ring or output underruns.
- **Canonical Gnarlyr identity:** the owner's full-size ECHO completed both file-copy update hops to developer build `7075642`, with matching copy readbacks, automatic restart/reconnect and live build reports. The original data folder and an independent backup were preserved; copied settings and saved-mixtape files match their originals. Hardware captures show the saved queue order and paused position restored after testing. A ten-second active 24-bit, 44.1 kHz FLAC window decoded 107 blocks with zero ring or output underruns. This does not establish listening quality on both outputs.
- **Everything else** that Podium's and EchoPod's authors tested on their own players has not been repeated by this project. Where this document mentions such a result, it says whose it is.

## Safety

- **Volume:** the top volume is the DAC's full level, and sensitive earphones can be very loud. Turn the volume down before you put them in.
- **Battery:** install only with the battery charged. The code switches the player off when the battery runs low and refuses to start on an empty one ("Battery low: Charge the player to turn it on"); neither has been seen on a player with Gnarlyr.
- **Bricking:** a broken image can leave the player unable to start. The updater writes one firmware slot at a time and reads every chunk back, but a power cut during an install is still a risk. The only recovery for a player that doesn't start is the chip's maskrom mode ([`device-recovery.md`](device-recovery.md)), which needs a patched tool and care. Prepare it before the first install.

## Not proven on a player

These checks remain incomplete on the owner's unit; the observed developer flows above do not replace them.

- **USB compatibility beyond the owner's Mac.** Storage enumeration and automatic reconnect with the overlay have worked on that Mac; broader host compatibility and sustained transfer checks remain.
- **USB DAC mode,** which now also runs from the overlay.
- **Lower-rate speech playback and the independent audio module.** Native tests cover conversion, duration, seeking and chapter boundaries; ARM scenarios cover WAV playback during menu changes, module corruption/fallback and actual AAC packet parsing/seeking through the module. On the player the earlier, unfolded kernel underran with 16 kHz AAC and 8 kHz WAV (core 0 73 to 91 % busy). The folded kernel with full clock during conversion has not yet been measured on the player; that, DAC clocks and both outputs still need player checks.
- **AAC edit-list trimming.** Native playback/seek/queue checks and an ARM decoder comparison cover the software path; listening checks on the player remain.
- **MP3 gapless transitions after seeking or resuming.** End trimming is covered by native and ARM decoder comparisons; transitions still need listening checks on the player.
- **Cold power cycles and resume.** Automatic update restarts preserve settings and saved mixtapes. Resume position and queue behavior after a true cold power cycle still need a complete owner check on the new build.
- **Translated interface layouts.** External language packs load without reinstalling firmware. Translated labels still need display-fit review at all three text sizes and both orientations before a translation is released.
- **Both portrait orientations and controls.** The owner confirmed portrait mode on the rebuilt foundation. Both portrait directions, rocker behavior and the new layouts/control mappings still need a complete device pass.
- **The ten-band equalizer's CPU load at 96 kHz.** By instruction count it takes about 55 % of the main core. It has not been heard or measured on a player.
- **DAC settings not yet checked against the fitted part.** The owner's player has two CS43198; earlier developer identification rows reported `431980 A1.06` for both. Some inherited code and register names assume the CS43131, a related part with a built-in headphone amplifier. The start-up sequence is stock's and audio works, but the gain, volume and filter registers have not been checked against the CS43198's documentation.
- **Bluetooth.**
- **Remaining developer-loop and power checks.** File-copy installs, saved reports, scripts, restart/reconnect and frame/load measurements have run on the player. Recorded 15-minute playback and idle battery soaks completed and returned reports; the cable bit remained 1 with the supply override active. These short voltage observations do not establish battery runtime or complete isolation from USB power. Low-battery abort/report behavior, charging from a low battery, charger-pin interpretation and profiler attribution to individual functions still need hardware validation. The experimental USB sense setting is not an established battery-saving mode.
- **The 4.4 mm output.**
- **Smart lists.** Covered by host tests and simulator captures only. How long the first library check and each list take with the owner's library and listening history, and the new 1.5 MB `track_keys.bin` being reserved on an existing card, still need a player check.

Also unchecked on a player by this project: `.m4a` and `.m4b` playback and seeking speed, `.m4b` chapters, CUE sheets by ear, wired headset buttons, the `MASKROM.REQ` request, going back to FiiO's firmware through the updater, the charge screen, auto off, and audiobook bookmarks.

## Known trade-offs

- **A failed install followed by music.** If an install fails partway through slot 1 and music is then played, the next plug-in shows "Restart the player to finish the update" and does not serve USB. The restart boots the complete slot 2.
- **Audio module damaged in both slots.** Lower-rate conversion and M4A demuxing require this module. If both copies fail validation, those tracks are refused; native-rate FLAC, MP3 and WAV remain available. A healthy copy in either slot is enough.
- **USB code damaged in both slots.** The USB overlay is stored in both firmware slots. If it were damaged in both while the main code stayed intact, USB storage would be unavailable ("USB code missing: reinstall the update") and recovery would be through maskrom only.
- **Language packs are bounded to 16 KB.** The glyph cache keeps 64 packed glyphs; built-in ASCII bypasses it. Additional script coverage still depends on the selected fonts.

- **Text right after an install.** Until the restart, every screen uses the built-in 16 px font, which is ASCII only: accented and Cyrillic letters show as `?`.

## Installing

The canonical `GNARLYR` identity has been installed through the two-hop file-copy transition on the owner's full-size ECHO. The installation and bounded persistence/playback checks are recorded above; the remaining device checks still apply.

- **File-copy updates between this project's developer builds are proven** ([`device-install.md`](device-install.md)). The debug loop has checked the copied image, ejected storage, installed the update and confirmed the build after automatic restart/reconnect. Maskrom recovery is also proven on the owner's unit.
- **Installing by file copy from FiiO's firmware is not verified.** FiiO's updater only reads files named `ECHOVnnn.IMG`, so `GNARLYR.IMG` would have to be renamed. Whether FiiO's 2.0.0 accepts it is not known. Podium's documentation reports that FiiO's updater installs Podium from a file copy.
- **The Echo Mini isn't supported.** The updater refuses its firmware.
- **Other custom firmware can't be installed from Gnarlyr.** Its updater takes only its own images and FiiO's Echo image; it refuses Podium and EchoPod images. To switch, go back to FiiO's firmware first.
- **Going back to FiiO's firmware through the updater** writes both slots. Keep a maskrom path ready ([`device-recovery.md`](device-recovery.md)).

## Portrait mode

- **The frame time overlay stays a landscape drawing.** In portrait the overlay (developer build, Settings → About → Frame time) is off screen.
- **Small text shows 13 rows in portrait**, not the 17 that fit, because the browse and Up Next row caches hold 13.

## Audiobook chapters (.m4b)

- Chapters are checked in tests on synthetic files and one ffmpeg-made `.m4b`, not with Libation or Audible files. Gapless continuation from one chapter into the next is checked as one decoder stream, not by ear.
- **Only `.m4b` files are split.** Chapters in `.m4a` and other files are ignored.
- **M4B books appear on Audiobooks wherever they are stored.** Named audiobook roots keep their author/title hierarchy; books elsewhere appear directly on the shelf. Chaptered files appear once and whole-file books retain independent bookmarks. Rescan or rerun echo-sync to update an existing library. Selecting a folder containing book chapters resumes that book at any depth; holding ▶ opens the chapters. Separate chapter files and chaptered `.m4b` files use the same action. Native button journeys cover nested paths; this flow still needs a check on the player.
- **A chaptered book remembers one place.** Choosing another chapter moves the bookmark to it.
- **At most 255 chapters** are read; a longer chapter list ends with the 255th chapter running to the file's end. UTF-16 chapter titles of a text track show as "Chapter n". The desktop indexer now preserves the same chapter layout as the device scanner; the synthetic and ffmpeg-derived fixture libraries match in native comparisons.
- **Titles are cut at 63 bytes.**

## CUE sheets

- Gapless continuation between CUE tracks of one file is checked in tests (one decoder stream, no reopen), not by ear.
- **Quotes inside a title** can't be escaped in a sheet, so a `\"` ends the title there. The player cuts texts from a sheet at 255 bytes; echo-sync doesn't.
- **Two sheets for the same audio file** both make tracks, so the album appears twice. Sheets in a folder too big for the library scan's listing memory (thousands of entries in one folder) are matched with the audio files of the same batch only.
- **A playlist entry that names a CUE image** (the FLAC) resolves to one of its tracks, not necessarily the first.
- **Text from sheets isn't recomposed to NFC on the player**, as with tags; echo-sync does recompose it. Sheets in decomposed form make the player's and the computer's libraries differ in those strings only.

## USB

- **Eject both drives before you unplug.** Unplugging first can lose the last changes, and the player warns you ("Unplugged too soon").
- **Some file managers only unmount.** The player finishes a USB session when the computer ejects the drive. Finder and `diskutil eject` send that; a file manager that only unmounts leaves the player showing "Eject the drive to finish". Eject in Finder or run `diskutil eject` on the disk.
- **macOS leaves `._` files** beside everything it copies. The library scan ignores them, but they take space. `dot_clean` on the drive removes them.
- **USB DAC mode limits:** full speed only (44.1 or 48 kHz, 16 or 24-bit; 88.2 and 96 kHz don't fit this chip's USB buffer); about 20 ms of delay; the player stays in DAC mode until you unplug it or hold Back; Power doesn't turn the screen off during DAC mode. If the computer stops sending audio and starts again, the first 20 ms are silent.
- **The USB product IDs are test IDs** (`1209:0001` for storage, `1209:0002` for DAC mode, from pid.codes). An assigned ID is still to be requested.
- **Reports in Podium's documentation, not repeated here:** the first connection to a new Windows computer can fail with "Device Descriptor Request Failed" and works on the second try; a computer twice reported the card's exFAT volume as not cleanly closed, with nothing lost; on Windows 11 copying ran at about 3.7 MB/s to the SD card and 5 MB/s to internal storage; USB DAC mode played from Windows 11 and an Android phone.

## Music and library

- **Characters:** song, artist and album names show Latin letters with accents and Cyrillic. Chinese, Japanese and Korean names show as `?`; glyph packs for them are planned. Emoji and other scripts (Arabic, Hebrew, Thai, Greek…) show as `?` too.
- **Paused-output diagnostics:** installed build `7075642` can count expected I2S starvation after a muted DMA pause. The source correction clears these flags without counting them, retains genuine faults and clears stale flags before start/resume; ARM regressions cover it. The correction still needs a player check. The total is not a count of audible dropouts.
- **Formats:** FLAC, MP3, WAV, AAC-LC and ALAC (`.m4a`, `.m4b`) accept 8, 11.025, 12, 16, 22.05, 24, 32, 44.1, 48, 88.2, 96, 176.4 and 192 kHz. Rates below 32 kHz are converted to 44.1 or 48 kHz; the others use their original rate. Converted tracks have no extra output frames. Queued files at the same decoded rate retain interpolation history, as do adjacent chapters. Different input rates drain and restart the filter. Seamless transitions between converted files still need listening checks. Ogg Vorbis playback is connected in source and covered by native/reference and two-core ARM tests. It accepts mono/stereo setup within the 64 KiB decoder workspace and a 32 KiB compressed-packet limit; larger setups are refused. Physical playback and decode headroom remain to be checked. Ogg Opus (`.opus`, mono or stereo, channel mapping family 0) is connected the same way: header gain, pre-skip and end trimming, nonzero start granules and seeking with 80 ms pre-roll have native tests against an independent libopus decode, and the two-core ARM emulator plays, seeks and switches between the Vorbis and Opus programs. Opus has not been heard on a player; its real-time margin on core 1 is unmeasured (about 0.83 to 0.99 million emulated instructions per 20 ms of stereo at 64–256 kbit/s, against 0.28 million for Vorbis and 0.54 million for AAC). Multichannel Opus (mapping families 1 and 255) and packets above 32 KiB are refused. Opus `R128_TRACK_GAIN` and `R128_ALBUM_GAIN` tags are not read; `REPLAYGAIN_*` comments in an Opus file are read like Vorbis comments. The simulator does not play Opus. APE and the other formats FiiO lists appear in the library but don't play; the player says so.
- **Gapless MP3** needs the LAME tag that LAME and ffmpeg write. MP3s without one keep a short gap between tracks. End padding is trimmed after a seek or part-way resume too. Seek positions remain approximate because they use the Xing table or a proportional byte offset.
- **M4A:** HE-AAC (AAC with SBR or PS) and xHE-AAC are refused with "Only AAC-LC and ALAC M4A play", because the decoder has no SBR; the message also shows for a damaged M4A. Only mono and stereo play. Priming trim uses a valid `iTunSMPB` tag or a single unity-rate edit list (`elst`, version 0 or 1) for the sound track. Files with neither retain their priming silence. Empty, multiple or non-unit-rate edits are not applied; timing tables longer than 64 runs are refused. Files over 4 GB and 64-bit box sizes are refused.
- **Building the library takes time,** on the first start and after music changes over USB. Decoding album covers is the slow part; later rescans keep the covers whose art didn't change. The player is busy until it finishes. `tools/echo_sync.py` can build the library on a computer instead. Podium's documentation reports about 1 minute 45 seconds for 1,563 tracks and 144 albums on a card.
- **Library size:** the library file is 4 MB and the format holds 65,535 tracks. The builder is tested on a computer with 20,000 tracks. A larger library may stop with an error when the file is full. Podium's documentation reports 6,558 tracks and over 1,600 albums on a player.
- **Album art:** the covers file has room for 2,673 albums. Past that, the remaining albums show without art.
- **Cover Flow** orders albums by album artist, then year, not by album title like the Albums list.
- **Spoken-word classification:** top-level `Audiobooks`, `Audiobook`, `Podcasts` and `Podcast` folders classify their contents; `.m4b` files elsewhere are audiobooks too. Other files under similarly named nested folders remain music. An author with music too appears under Artists with the music only. Genre tags alone do not classify a file as spoken word. Music search covers the music partition; shelf-wide spoken-word search is not yet available.

## echo-sync

- **Run from the source only** (`python3 tools/echo_sync.py`); there are no packaged downloads. This project has run its tests, not the tool on a real card.
- **FAT32 and exFAT only.** On other file systems the file times may not match the player's, and the player then rescans anyway.
- **Copies that never finished** (a file whose size on the card is larger than what was written) are read as the player sees them; recopy such files from the computer.

## Not there yet

- Playback speed control remains unavailable. ReplayGain reads prepared track/album metadata from a rescanned or rebuilt library; physical listening and headroom checks remain open.
- **Timed sidecar lyrics are implemented.** The missing-sidecar notice was checked on the player; timing and text from a real sidecar still need a device check.
- **The equalizer** leaves music above 96 kHz (176.4 and 192 kHz) unchanged, isn't applied in USB DAC mode or in the simulator, and a change is heard after the audio already buffered (about 0.3 s at 44.1 kHz).
- **Bluetooth and the jacks:** while music goes to Bluetooth the player can't read the headphone jack: pause, plug wired headphones in and press Play to switch.
- **Buttons on wired headphones** (3.5 mm) follow the readings of FiiO's firmware. Podium's author reports that on Apple EarPods the middle button works but + and − don't: Apple's remote signals them through a chip the player can't talk to ([`hardware.md`](hardware.md)). If buttons do nothing, Settings → About → Headset shows what the player reads while each button is pressed. The 4.4 mm jack has no headset buttons.
- **Settings → Output can't switch sound between the jacks:** both are driven by the same DACs, so it only picks the volume table (see [`user-guide.md`](user-guide.md)).
