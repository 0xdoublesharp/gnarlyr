# Podium known issues

Podium is early firmware made by one person and tested on a dozen players. It comes with no warranty: use it at your own risk. Read this before you install, and keep a way back (see "If something goes wrong" in `docs/user-guide.md`).

## Safety

- **Volume:** Podium's top volume is close to FiiO's maximum, and sensitive earphones can be very loud. Turn the volume down before you put them in.
- **Battery:** Podium switches off when the battery runs low, but doesn't yet refuse to start on an almost empty battery. If it switches off by itself, charge it before using it again.
- **Bricking:** development builds have left the Echo unable to start (all were recovered through maskrom). Podium's updater writes one firmware slot at a time and reads every chunk back, but a power cut during an install is still a risk: install with the battery charged.

## Installing

- **Tested on a dozen players:** an Echo 4 GB (hardware 1.1.7, installed from macOS) and users' Echos, both the 4 GB and the 8 GB model. FiiO's updater installs Podium from a file copy (confirmed by a user). On this Echo it refused once (it shows "Updating" and starts FiiO's firmware again, nothing written), probably because the file was stored in pieces right after many deletions; that install went through maskrom (`docs/device-install.md`).
- **The Echo Mini isn't supported.** Podium refuses its firmware, and its firmware ignores Podium's file.
- **Going back to FiiO's firmware** goes through Podium's own updater, which writes both slots, so keep the `MASKROM.REQ` escape hatch and a maskrom path ready (`docs/device-recovery.md`). Another custom firmware can't be installed from Podium: go back to FiiO's first.
- **If the player doesn't start at all,** the only recovery known is the chip's maskrom mode (`docs/device-recovery.md`). It works on the full-size Echo, but it needs a patched tool and care.

## Echo values from the decompile

- The Echo build comes from decompiled stock code and is checked on the Mac only. Values taken from the decompile and not seen working: the panel's memory access byte (`0x28`; `-DECHO_PANEL_MADCTL=0x68` mirrors the columns), one 16-bit write per pixel in the LCD controller, the 49-row window offset, the 25 kHz backlight level table, the power pair, the jack polarity, and the stock Echo image's CRC range (the first 38 MiB).
- **Text on the Echo comes from glyph faces** in the image (22 px, 128 cached glyphs). After an install, every screen uses the built-in 16 px font (ASCII only, accented letters show as `?`) because glyph reads stop until the restart.

## Portrait mode

- **Not tested on a player.** The panel's memory access bytes for the two portraits (`0x88` for Portrait, `0x48` for Portrait, flipped, with the 49-line offset on the columns) come from the landscape byte `0x28` by the same rule EchoPod uses on the Mini; which way round each one sits, and which one swaps the volume rocker, are untested guesses.
- **The frame time overlay stays a landscape drawing.** In portrait the overlay (Settings → About → Frame time) is off screen.
- **Small text shows 13 rows in portrait**, not the 17 that fit, because the browse and Up Next row caches hold 13.

## Audiobook chapters (.m4b)

- **Not tested on a player.** Chapters were checked in tests on synthetic files and one ffmpeg-made `.m4b`, not with Libation or Audible files, and gapless continuation from one chapter into the next only as one decoder stream.
- **Only `.m4b` files are split.** Chapters in `.m4a` and other files are ignored.
- **A book is the folder at the top of `Audiobooks/`.** An `.m4b` inside a book folder, or deeper (Libation's `Author/Title/Title.m4b`), is listed under that folder's book, not as a book of its own, and its chapters sit one folder further down (hold ▶ on the folder, then open the file's folder). Put the files straight into `Audiobooks/`.
- **A chaptered book remembers one place.** Choosing another chapter moves the bookmark to it. A CUE album in an audiobook folder is now bookmarked under its audio file's path instead of its folder's.
- **At most 255 chapters** are read; a longer chapter list ends with the 255th chapter running to the file's end. UTF-16 chapter titles of a text track show as "Chapter n". The PC indexer lists an `.m4b` as one track and doesn't read chapters.
- **Titles are cut at 63 bytes.**

## CUE sheets

- **Not tested on a player.** Gapless continuation between CUE tracks of one file is checked in tests (one decoder stream, no reopen), not by ear.
- **Quotes inside a title** can't be escaped in a sheet, so a `\"` ends the title there. The player cuts texts from a sheet at 255 bytes; echo-sync doesn't.
- **Two sheets for the same audio file** both make tracks, so the album appears twice. Sheets in a folder too big for the library scan's listing memory (thousands of entries in one folder) are matched with the audio files of the same batch only.
- **A playlist entry that names a CUE image** (the FLAC) resolves to one of its tracks, not necessarily the first.
- **Text from sheets isn't recomposed to NFC on the player**, as with tags; echo-sync does recompose it. Sheets in decomposed form make the player's and the computer's libraries differ in those strings only.

## USB

- **The first connection to a new computer can fail.** Windows may report "Device Descriptor Request Failed", or the drives may drop off. Unplug and plug in again; the second try has always worked.
- **Internal storage as the music drive (no card):** the player reads and writes the internal volume one 512-byte block at a time, which is slower than the SD card path, and a first library scan of a big collection takes correspondingly longer. Not yet measured on a player.
- **Eject both drives before you unplug.** Unplugging first can lose the last changes, and the player warns you ("Unplugged before ejecting"). Once, a file deleted from Windows and ejected properly was still on internal storage afterwards; it hasn't happened again.
- **Some file managers only unmount.** The player finishes a USB session when the computer ejects the drive. Finder, `diskutil eject` and Windows' Safely Remove send that; some file managers (Marta on macOS) only unmount, and the player keeps showing "Eject the drive to finish". Eject in Finder or run `diskutil eject` on the disk.
- **The SD card can be marked as needing repair.** Twice a computer reported the card's exFAT volume as not cleanly closed. Nothing was lost. If Windows offers to scan and fix the card, let it (or run `chkdsk /f` on its drive letter; on a Mac, Disk Utility's First Aid).
- **Speed on a Mac hasn't been measured.** On Windows 11, copying runs at about 3.7 MB/s to the SD card and 5 MB/s to internal storage, and reading from the SD card at about 3.9 MB/s.
- **USB DAC mode is tested on one player with Windows 11** (44.1 and 48 kHz, 16 and 24-bit) **and an Android phone.** Linux and a Mac haven't been tried. If it doesn't appear as a sound card, crackles, or plays at the wrong speed, set Settings → USB mode back to Storage and report what you saw. Known limits: full speed only (44.1 or 48 kHz, 16 or 24-bit; 88.2 and 96 kHz don't fit this chip's USB buffer); about 20 ms of delay; the player stays in DAC mode until you unplug it or hold Back; Power doesn't turn the screen off during DAC mode. If the computer stops sending audio and starts again, the first 20 ms are silent.
- **macOS leaves `._` files** beside everything it copies. Podium ignores them, but they take space. `dot_clean` on the drive removes them.

## Music and library

- **Gapless MP3** needs the LAME tag that LAME and ffmpeg write. MP3s without one (some iTunes and older encoders) keep a short gap between tracks. After a seek, or when playback resumes part-way through a track, that track's silent end isn't trimmed, so the next track starts a few tens of milliseconds late.
- **Characters:** song, artist and album names show Latin letters with accents and Cyrillic (Russian, Ukrainian, Belarusian, Serbian, Bulgarian, Macedonian). Chinese, Japanese and Korean text, emoji and other scripts (Arabic, Hebrew, Thai, Greek…) show as `?`: Podium has no glyphs for them. Chinese, Japanese and Korean can be added in an image you build yourself ([`cjk.md`](cjk.md)). The first time a screen shows an accented or Cyrillic letter, it's read from internal storage. The small font used right after an update is ASCII only.
- **Formats:** FLAC, MP3, WAV, AAC-LC and ALAC (`.m4a`) play, at 44.1, 48, 88.2, 96, 176.4 and 192 kHz. APE and the other formats FiiO lists appear in the library but don't play; Podium says so.
- **M4A:** HE-AAC (AAC with SBR or PS, as some streaming rips use) is refused, because the decoder has no SBR; AAC at 32 kHz and below is refused with the sample-rate message, as MP3 is. Only mono and stereo play. `.m4b` files are indexed like `.m4a` (chapters below). xHE-AAC, which Audible's "High" quality can deliver, is refused like HE-AAC, with "Only AAC-LC and ALAC M4A play"; the message also shows for a damaged M4A. The start trim of a file without an `iTunSMPB` tag is zero, so an encoder's priming silence (about 23 ms) is audible at the start; an edit list (`elst`) is not read. Files over 4 GB and 64-bit box sizes are refused. The player's AAC and ALAC decoding has been run on the second core in an emulator and on a computer, not on a player: whether it keeps up at 44.1 kHz and how long a seek takes are still to be checked.
- **Building the library takes time.** On the first start, after music changes over USB, and once after updating to 0.2.0 (the library format changed). A 1,563-track, 144-album card takes about 1 minute 45 seconds: 20 s for the tracks and 1.4 minutes for the covers, where decoding each album's picture takes about half a second. Later rescans keep the covers whose art didn't change and decode only new or changed albums; the first rescan after updating still decodes them all, and so does one after an echo-sync from before this change. Covers from a cover file on a card that isn't FAT32 or exFAT are always decoded again. The player is busy until it finishes: let it finish before you plug it in. echo-sync can build the library on a computer instead.
- **Library size:** tested on the player with 6,558 tracks, and with over 1,600 albums; on a computer with 20,000 tracks. Larger libraries may stop with an error when the library file (4 MB) is full.
- **Album art:** the covers file has room for 2,673 albums. Past that, the remaining albums show without art.
- **Cover Flow** orders albums by album artist, then year, not by album title like the Albums list.
- **Spoken word is decided by the top folder.** Only tracks inside an `Audiobooks`, `Audiobook`, `Podcasts` or `Podcast` folder at the top of the card count as audiobooks or podcasts; the same folder names deeper down are music. An author with music too appears under Artists with the music only.

## echo-sync

- **Tested on Windows 11 only.** The macOS (Apple silicon) and Linux files pass an automatic build check but haven't been run on a real card yet. On a Mac, the downloaded file needs `chmod +x` and its quarantine flag removed before it runs (see `docs/user-guide.md`).
- **FAT32 and exFAT only.** On other file systems the file times may not match the player's, and the player then rescans anyway.
- **Copies that never finished** (a file whose size on the card is larger than what was written) are read as the player sees them; recopy such files from the computer.

## Not there yet

- No ReplayGain or lyrics.
- **The equalizer hasn't been heard on a player yet.** It's checked against the filter formula in tests only. It leaves music above 96 kHz (176.4 and 192 kHz) unchanged, isn't applied in USB DAC mode or in the simulator, and a change is heard after the audio already buffered (about 0.3 s at 44.1 kHz).
- Bluetooth has been tested with one pair of headphones (OnePlus Buds 3). While music goes to Bluetooth the player can't read the headphone jack: pause, plug wired headphones in and press Play to switch.
- Buttons on wired headphones (3.5 mm) follow stock's readings. On Apple EarPods the middle button works but + and − don't: Apple's remote signals them through a chip the player can't talk to (`docs/hardware.md`); on the player both read the same, four 10 ms dips to about 920, so they can't be told apart. If buttons do nothing, Settings → About → Headset shows what the player reads while each button is pressed (lowest reading, how long, how many dips); that line helps fix it. The 4.4 mm jack has no headset buttons.
- The 4.4 mm balanced output works (reported by a tester). Settings → Output can't switch sound between the jacks: both are driven by the same DACs, so it only picks the volume table (see `docs/user-guide.md`).
- Audiobooks and podcasts: the shelves, covers and keeping them out of Music are checked on a player; bookmarks, in-order play and the 30 s / 15 s jumps so far only in the simulator. There's no playback speed control.
- The brightness, Favorites stutter, sound-after-USB and battery-while-charging fixes since 0.1.0 still need a tester's confirmation on a player.
