# Gnarlyr known issues

Gnarlyr 1.0.0 is not released. Recorded device checks below are from earlier builds of this project, including the FreshBase foundation. It comes with no warranty: use it at your own risk. Read this before you install a build, and keep a way back ([`device-recovery.md`](device-recovery.md)).

## What has been checked

- **Software checks:** host tests, the simulator and the ARM emulator cover boot and updates, memory bounds, queue/session persistence, mixtapes, controls, Listening and lyrics. They do not reproduce analog output, Bluetooth radio operation or full USB host traffic.
- **On the owner's Echo (full size, 4 GB):** the owner confirmed display, menus, buttons, music playback, volume, internal/microSD USB storage and quiet audio on the earlier Podium 4.5.5-based build. The rebuilt Podium 4.5.9 foundation was then confirmed for file-copy installation, USB storage, playback, quiet audio and portrait mode. Later developer builds exercised reports, scripts, automatic update/restart/reconnect and frame/load measurements. FreshBase `33757eb` and the demand-loaded-module build `6d027ed` were installed through the file-copy loop and returned on USB with their build reports confirmed. The unit was also restored to FiiO 2.0.0 and back through maskrom; the recovery steps in [`device-install.md`](device-install.md) and [`device-recovery.md`](device-recovery.md) are the ones used.
- **Everything else** that Podium's and EchoPod's authors tested on their own players has not been repeated by this project. Where this document mentions such a result, it says whose it is.

## Safety

- **Volume:** the top volume is the DAC's full level, and sensitive earphones can be very loud. Turn the volume down before you put them in.
- **Battery:** install only with the battery charged. The code switches the player off when the battery runs low and refuses to start on an empty one ("Battery low: Charge the player to turn it on"); neither has been seen on a player with Gnarlyr.
- **Bricking:** a broken image can leave the player unable to start. The updater writes one firmware slot at a time and reads every chunk back, but a power cut during an install is still a risk. The only recovery for a player that doesn't start is the chip's maskrom mode ([`device-recovery.md`](device-recovery.md)), which needs a patched tool and care. Prepare it before the first install.

## Not proven on a player

These checks remain incomplete on the owner's unit. Earlier hardware results do not establish the new Gnarlyr interface, queue, mixtape, Listening or lyrics behavior on the player.

- **USB compatibility beyond the owner's Mac.** Storage enumeration and automatic reconnect with the overlay have worked on that Mac; broader host compatibility and sustained transfer checks remain.
- **USB DAC mode,** which now also runs from the overlay.
- **Playback across the new feature-module paths.** Playback and USB storage worked on the rebuilt foundation. Switching demand-loaded screens while playing, then returning from USB and resuming with the new queue/session state, still needs a device check.
- **Additional language packs.** Earlier installed builds booted with their English text read from the firmware language pack. Selecting an additional translation or a separate pack file in `/.freshbase/lang` has not been validated on the owner's player.
- **Both portrait orientations and controls.** The owner confirmed portrait mode on the rebuilt foundation. Both portrait directions, rocker behavior and the new layouts/control mappings still need a complete device pass.
- **The ten-band equalizer's CPU load at 96 kHz.** By instruction count it takes about 55 % of the main core. It has not been heard or measured on a player.
- **DAC settings not yet checked against the fitted part.** The owner's player has two CS43198 (Settings → About shows `431980 A1.06` for both). Some inherited code and register names assume the CS43131, a related part with a built-in headphone amplifier. The start-up sequence is stock's and audio works, but the gain, volume and filter registers have not been checked against the CS43198's documentation.
- **Bluetooth.**
- **Remaining developer-loop and power checks.** File-copy installs, saved reports, scripts, restart/reconnect and frame/load measurements have run on the player. Recorded 15-minute playback and idle battery soaks completed and returned reports; the cable bit remained 1 with the supply override active. These short voltage observations do not establish battery runtime or complete isolation from USB power. Low-battery abort/report behavior, charging from a low battery, charger-pin interpretation and profiler attribution to individual functions still need hardware validation. The experimental USB sense setting is not an established battery-saving mode.
- **The 4.4 mm output.**

Also unchecked on a player by this project: `.m4a` and `.m4b` playback and seeking speed, `.m4b` chapters, CUE sheets by ear, wired headset buttons, the `MASKROM.REQ` request, going back to FiiO's firmware through the updater, the charge screen, auto off, and audiobook bookmarks.

## Known trade-offs

- **A failed install followed by music.** If an install fails partway through slot 1 and music is then played, the next plug-in shows "Restart the player to finish the update" and does not serve USB. The restart boots the complete slot 2.
- **USB code damaged in both slots.** The USB overlay is stored in both firmware slots. If it were damaged in both while the main code stayed intact, USB storage would be unavailable ("USB code missing: reinstall the update") and recovery would be through maskrom only.
- **Language packs are bounded to 16 KB.** The glyph cache keeps 64 packed glyphs; built-in ASCII bypasses it. Additional script coverage still depends on the selected fonts.

- **Text right after an install.** Until the restart, every screen uses the built-in 16 px font, which is ASCII only: accented and Cyrillic letters show as `?`.

## Installing

- **File-copy updates between this project's developer builds are proven** ([`device-install.md`](device-install.md)). The debug loop has checked the copied image, ejected storage, installed the update and confirmed the build after automatic restart/reconnect. Maskrom recovery is also proven on the owner's unit.
- **Installing by file copy from FiiO's firmware is not verified.** FiiO's updater only reads files named `ECHOVnnn.IMG`, so `FRSHBS.IMG` would have to be renamed. Whether FiiO's 2.0.0 accepts it is not known. Podium's documentation reports that FiiO's updater installs Podium from a file copy.
- **The Echo Mini isn't supported.** The updater refuses its firmware.
- **Other custom firmware can't be installed from Gnarlyr.** Its updater takes only its own images and FiiO's Echo image; it refuses Podium and EchoPod images. To switch, go back to FiiO's firmware first.
- **Going back to FiiO's firmware through the updater** writes both slots. Keep a maskrom path ready ([`device-recovery.md`](device-recovery.md)).

## Portrait mode

- **The frame time overlay stays a landscape drawing.** In portrait the overlay (developer build, Settings → About → Frame time) is off screen.
- **Small text shows 13 rows in portrait**, not the 17 that fit, because the browse and Up Next row caches hold 13.

## Audiobook chapters (.m4b)

- Chapters are checked in tests on synthetic files and one ffmpeg-made `.m4b`, not with Libation or Audible files. Gapless continuation from one chapter into the next is checked as one decoder stream, not by ear.
- **Only `.m4b` files are split.** Chapters in `.m4a` and other files are ignored.
- **A book is the folder at the top of `Audiobooks/`.** An `.m4b` inside a book folder, or deeper (Libation's `Author/Title/Title.m4b`), is listed under that folder's book, not as a book of its own, and its chapters sit one folder further down (hold ▶ on the folder, then open the file's folder). Put the files straight into `Audiobooks/`.
- **A chaptered book remembers one place.** Choosing another chapter moves the bookmark to it.
- **At most 255 chapters** are read; a longer chapter list ends with the 255th chapter running to the file's end. UTF-16 chapter titles of a text track show as "Chapter n". The PC indexer lists an `.m4b` as one track and doesn't read chapters.
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
- **Formats:** FLAC, MP3, WAV, AAC-LC and ALAC (`.m4a`, `.m4b`) play, at 44.1, 48, 88.2, 96, 176.4 and 192 kHz. APE and the other formats FiiO lists appear in the library but don't play; the player says so.
- **Gapless MP3** needs the LAME tag that LAME and ffmpeg write. MP3s without one keep a short gap between tracks. After a seek, or when playback resumes part-way through a track, that track's silent end isn't trimmed, so the next track starts a few tens of milliseconds late.
- **M4A:** HE-AAC (AAC with SBR or PS) and xHE-AAC are refused with "Only AAC-LC and ALAC M4A play", because the decoder has no SBR; the message also shows for a damaged M4A. AAC at 32 kHz and below is refused with the sample-rate message, as MP3 is. Only mono and stereo play. The start trim of a file without an `iTunSMPB` tag is zero, so an encoder's priming silence (about 23 ms) is audible at the start; an edit list (`elst`) is not read. Files over 4 GB and 64-bit box sizes are refused.
- **Building the library takes time,** on the first start and after music changes over USB. Decoding album covers is the slow part; later rescans keep the covers whose art didn't change. The player is busy until it finishes. `tools/echo_sync.py` can build the library on a computer instead. Podium's documentation reports about 1 minute 45 seconds for 1,563 tracks and 144 albums on a card.
- **Library size:** the library file is 4 MB and the format holds 65,535 tracks. The builder is tested on a computer with 20,000 tracks. A larger library may stop with an error when the file is full. Podium's documentation reports 6,558 tracks and over 1,600 albums on a player.
- **Album art:** the covers file has room for 2,673 albums. Past that, the remaining albums show without art.
- **Cover Flow** orders albums by album artist, then year, not by album title like the Albums list.
- **Spoken word is decided by the top folder.** Only tracks inside an `Audiobooks`, `Audiobook`, `Podcasts` or `Podcast` folder at the top of the card count as audiobooks or podcasts; the same folder names deeper down are music. An author with music too appears under Artists with the music only.

## echo-sync

- **Run from the source only** (`python3 tools/echo_sync.py`); there are no packaged downloads. This project has run its tests, not the tool on a real card.
- **FAT32 and exFAT only.** On other file systems the file times may not match the player's, and the player then rescans anyway.
- **Copies that never finished** (a file whose size on the card is larger than what was written) are read as the player sees them; recopy such files from the computer.

## Not there yet

- No ReplayGain or playback speed control.
- **Timed sidecar lyrics are implemented.** Their timing and display with the new interface still need a device check.
- **The equalizer** leaves music above 96 kHz (176.4 and 192 kHz) unchanged, isn't applied in USB DAC mode or in the simulator, and a change is heard after the audio already buffered (about 0.3 s at 44.1 kHz).
- **Bluetooth and the jacks:** while music goes to Bluetooth the player can't read the headphone jack: pause, plug wired headphones in and press Play to switch.
- **Buttons on wired headphones** (3.5 mm) follow the readings of FiiO's firmware. Podium's author reports that on Apple EarPods the middle button works but + and − don't: Apple's remote signals them through a chip the player can't talk to ([`hardware.md`](hardware.md)). If buttons do nothing, Settings → About → Headset shows what the player reads while each button is pressed. The 4.4 mm jack has no headset buttons.
- **Settings → Output can't switch sound between the jacks:** both are driven by the same DACs, so it only picks the volume table (see [`user-guide.md`](user-guide.md)).
