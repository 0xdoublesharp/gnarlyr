# Doublesharp Echo known issues

Doublesharp Echo 1.0.0 is not released and has not run on any player yet. It comes with no warranty: use it at your own risk. Read this before you install a build, and keep a way back ([`device-recovery.md`](device-recovery.md)).

## What has been checked

- **Doublesharp itself:** host tests, the simulator and the ARM emulator only (242 checks). The emulator has no USB traffic, no DAC, no SD card and no Bluetooth module, so those paths are covered by host tests of their logic and nothing more.
- **On the owner's Echo (full size, 4 GB):** an earlier build of this project, based on Podium 4.5.5, was installed through maskrom. The owner confirmed display, menus, buttons, music playback, volume, USB storage with internal storage and a microSD card, and quiet audio with the 12 ms DAC wait. The unit was also returned to FiiO's 2.0.0 through maskrom and back. The maskrom steps in [`device-install.md`](device-install.md) and [`device-recovery.md`](device-recovery.md) are the ones used.
- **Everything else** that Podium's and EchoPod's authors tested on their own players has not been repeated by this project. Where this document mentions such a result, it says whose it is.

## Safety

- **Volume:** the top volume is the DAC's full level, and sensitive earphones can be very loud. Turn the volume down before you put them in.
- **Battery:** install only with the battery charged. The code switches the player off when the battery runs low and refuses to start on an empty one ("Battery low: Charge the player to turn it on"); neither has been seen on a player with Doublesharp.
- **Bricking:** a broken image can leave the player unable to start. The updater writes one firmware slot at a time and reads every chunk back, but a power cut during an install is still a risk. The only recovery for a player that doesn't start is the chip's maskrom mode ([`device-recovery.md`](device-recovery.md)), which needs a patched tool and care. Prepare it before the first install.

## Not proven on a player

These are new or changed in Doublesharp, or were never checked on the owner's unit. Each needs a run on the player before it can be relied on.

- **USB enumeration timing.** The USB code is now loaded from storage into high RAM after the computer's bus reset. Whether a computer still enumerates the player reliably with that load in the path is not known.
- **USB DAC mode,** which now also runs from the overlay.
- **A real update install.** The updater borrows the mass-storage write buffer for its work; an install through the updater has only run in the emulator.
- **Playback after a USB session.** The overlay shares its memory with the decoder workspace.
- **Language packs.** The pack is read from the firmware slot into high RAM at start-up; that read, the Language choice and a pack file in `/.doublesharp/lang` have run in host tests, the simulator and the emulator only. No translation exists yet, so no language but English has been read by anyone.
- **Portrait mode.** The two portrait panel settings come from the landscape one by rule. Which way round each sits, and which one swaps the volume rocker, are untested.
- **The ten-band equalizer's CPU load at 96 kHz.** By instruction count it takes about 55 % of the main core. It has not been heard or measured on a player.
- **DAC settings not yet checked against the fitted part.** The owner's player has two CS43198 (Settings → About shows `431980 A1.06` for both). Some inherited code and register names assume the CS43131, a related part with a built-in headphone amplifier. The start-up sequence is stock's and audio works, but the gain, volume and filter registers have not been checked against the CS43198's documentation.
- **Bluetooth.**
- **The debug loop of developer builds** (`architecture.md`, Debug loop). The report is written to internal storage between the computer's bus reset and the first answer to it; in the emulator that adds about 40 ms the first time. A shutdown now loads the USB overlay and writes a report before the power is cut, also at the low-battery cut-off; none of that has run on a player. A cable plugged in while the firmware ran used to go unnoticed on the owner's player; the cause was the USB supply pins held high without a cable (`hardware.md`), and the pins now follow the cable. The USB sense row was built to test another explanation (the cable bit needs the USB hardware powered) and stays as an experiment; leaving the PHY powered costs battery, and how much is not known. The battery soak (`supply off`, `screen off`, `soak`) has not run on a player. Whether GPIO2.0 held low stops USB from powering the player with the cable in is not known, nor what the cable bit and the charger pin read then: the soak's report line records both at its start and end so that the first run answers it. If the cable bit drops to 0 with the supply held off, the player takes the cable for unplugged, and its own low-battery cut-off at 3,450 mV applies again besides the soak's 3,600 mV. Whether a computer still enumerates the player with a real eMMC's write times in that path is not known. The profiler's share of sleeping time depends on the address a real Cortex-M3 stacks when an interrupt ends a WFI. The report's power lines show the charger's status pin as the firmware reads it (GPIO2.3, low taken as charging); whether that pin and polarity are right on the Echo is what those lines are for.
- **The 4.4 mm output.**

Also unchecked on a player by this project: `.m4a` and `.m4b` playback and seeking speed, `.m4b` chapters, CUE sheets by ear, wired headset buttons, the `MASKROM.REQ` request, going back to FiiO's firmware through the updater, the charge screen, auto off, and audiobook bookmarks.

## Known trade-offs

- **A failed install followed by music.** If an install fails partway through slot 1 and music is then played, the next plug-in shows "Restart the player to finish the update" and does not serve USB. The restart boots the complete slot 2.
- **USB code damaged in both slots.** The USB overlay is stored in both firmware slots. If it were damaged in both while the main code stayed intact, USB storage would be unavailable ("USB code missing: reinstall the update") and recovery would be through maskrom only.
- **Language packs are limited to 8 KB, and Cyrillic is not usable yet (open).** English uses 4.6 KB of the 8 KB a pack may have. A Cyrillic translation needs about 1.6 times the English bytes, so it stops fitting once the English text passes about 5 KB, and the small 16 px font has no Cyrillic letters at all, so the pack builder refuses them today. Both are left as they are until a translation needs them (`architecture.md`, Languages).
- **Text right after an install.** Until the restart, every screen uses the built-in 16 px font, which is ASCII only: accented and Cyrillic letters show as `?`.

## Installing

- **The proven route is maskrom** ([`device-install.md`](device-install.md)): it is how the earlier build was installed on the owner's unit.
- **Installing by file copy from FiiO's firmware is not verified.** FiiO's updater only reads files named `ECHOVnnn.IMG`, so `DBLSHARP.IMG` would have to be renamed. Whether FiiO's 2.0.0 accepts it is not known. Podium's documentation reports that FiiO's updater installs Podium from a file copy.
- **The Echo Mini isn't supported.** The updater refuses its firmware.
- **Other custom firmware can't be installed from Doublesharp.** Its updater takes only its own images and FiiO's Echo image; it refuses Podium and EchoPod images. To switch, go back to FiiO's firmware first.
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

- No ReplayGain, lyrics or playback speed control.
- **The equalizer** leaves music above 96 kHz (176.4 and 192 kHz) unchanged, isn't applied in USB DAC mode or in the simulator, and a change is heard after the audio already buffered (about 0.3 s at 44.1 kHz).
- **Bluetooth and the jacks:** while music goes to Bluetooth the player can't read the headphone jack: pause, plug wired headphones in and press Play to switch.
- **Buttons on wired headphones** (3.5 mm) follow the readings of FiiO's firmware. Podium's author reports that on Apple EarPods the middle button works but + and − don't: Apple's remote signals them through a chip the player can't talk to ([`hardware.md`](hardware.md)). If buttons do nothing, Settings → About → Headset shows what the player reads while each button is pressed. The 4.4 mm jack has no headset buttons.
- **Settings → Output can't switch sound between the jacks:** both are driven by the same DACs, so it only picks the volume table (see [`user-guide.md`](user-guide.md)).
