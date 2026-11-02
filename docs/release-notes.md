# Podium release notes

Podium is a fork of EchoPod 0.6.0 for the full-size Snowsky Echo. Entries from 0.6.0 down are EchoPod's.

## Podium 4.5.4 (2026-11-01)

### New

- **M4A playback.** `.m4a` files play: AAC-LC (what Apple Music and iTunes purchases use, mono or stereo, 44.1 and 48 kHz) and Apple Lossless (ALAC, 16 and 24 bit, mono or stereo). Seeking, resume after power-off, Up Next, shuffle and bookmarks work as for the other formats, and consecutive tracks play without a gap, trimmed by the encoder delay and padding in the file's `iTunSMPB` tag. Now Playing shows `AAC` or `ALAC`. The library already listed `.m4a` files with their tags, length and cover art; nothing needs rescanning. HE-AAC (SBR and PS), AAC at rates the player's output doesn't offer (such as 22.05 and 32 kHz), `.m4b` audiobooks and AAC in other containers don't play (see `known-issues.md`). Not yet heard on the player.

### Fixed

- **Text is readable after an update.** After installing an update, every screen showed `?` instead of letters until the player was turned off and on again. Screens now use the small built-in font until the restart, so the Update installed message, the Goodbye screen and the menus stay legible.

### Changed

- **Named Podium.** The splash screen, the Home title, the USB drive and the USB DAC device say Podium instead of EchoPod.
- **Firmware version in Diagnostics.** Firmware shows the version and the build, such as `4.5.4-b97f4e9`, instead of the build alone.
- **Echo only.** The Echo Mini board is gone from the source.

## Podium 4.5.3 (2026-11-01)

### Fixed

- **The player starts again.** 4.5.1 and 4.5.2 never started on the Echo: black screen, nothing on USB. The boot ROM clears the firmware's memory while it still runs from the top of that memory, and those builds had grown into it. The start-up screen-clear buffer is smaller now, and a build that would reach that area is refused.
- **Long albums no longer stutter.** Plain playback of a large album image started to stutter about 13 minutes (90 MB) in: every read walked the file from its start again. Reading now costs the same anywhere in a file.
- **CUE tracks start quickly.** Starting any track but the first of a large album image (hundreds of megabytes, one FLAC with a CUE sheet) took seconds, and the player stayed sluggish afterwards. Reading deep into a big file on internal storage walked the file's whole cluster chain again and again; each open file now keeps a map of where its data lies, so jumping anywhere in it costs one lookup.
- **Smoother UI while switching tracks.** Skipping between the tracks of an album image no longer freezes the screen, no longer makes music stutter when going back to a list, and no longer ends in the "STALL" screen.
- **The start of a CUE track is not faded in slowly.** The fade-in at the start of a track no longer stretches to a second or more while the player is busy reading.

## Podium 4.5.2 (not released)

### New

- **CUE sheets.** A `.cue` file next to an album's single FLAC, WAV or MP3 image now makes one track per CUE track, with its title, artist, album, number, genre and year from the sheet. The image itself no longer shows up as one long track. Tracks play through without a gap, and the progress bar, seeking, resume after power-off, Up Next, shuffle and bookmarks all work per track. Rescan the library (Settings → Rescan library) or run echo-sync again to see it. The library format changed (version 4), so the first start after updating rebuilds the library.

### Fixed

- **The player starts on large internal storage.** 4.5.1 created its files before lighting the screen, scanning the whole FAT one sector at a time, so a large internal volume left the screen black and USB silent for a minute or more. The files are now made after the screen shows "Preparing storage", the scan starts where the last file ended and internal storage is read several sectors per command.
- **An update file is removed after it is used.** `ECHOVnnn.IMG` is deleted after a successful install, when it is not a valid image, and when it is already installed, so a restored older build is not overwritten again on the next USB eject. A failed install leaves the file for another try.

## Podium 4.5.1 (not released)

### New

- **Music on internal storage.** With no card in the player, or one it can't read, the player uses its internal storage for music. Diagnostics → Music shows which one is in use.

## Podium 4.5.0 (2026-11-01)

### New

- **Snowsky Echo board.** The first build for the full-size Echo (480×222): the iPod-style interface with iPod-sized text (22 px, six rows), the Echo's display through its LCD controller, its pins for power, sound and jacks, and the update file `ECHOVnnn.IMG` named after the version, so 4.5.0 is `ECHOV450.IMG`. Podium's updater also installs FiiO's own Echo firmware for the way back.
- **MASKROM.REQ.** A file named `MASKROM.REQ` holding the text `MASKROM REQ` at the top of internal storage reboots the player into the chip's USB recovery mode.

### Fixed

- **Echo screen timing** follows the clock speed; with the fixed stock timing the first picture was noise.
- **Echo brightness** stays visible at the lowest level (20 % to 100 %).

## 0.6.0

### New

- **Albums stay together when some tracks feature another artist.** When files have no album-artist tag, a track by "The Weeknd & Justice" used to split off from the rest of "Hurry Up Tomorrow" by The Weeknd. Tracks without that tag that share a folder and album name are now one album, under the artist most of them share. Rescan the library (Settings → Rescan library) or run echo-sync again to see it.
- **Random album.** Cover Flow starts with a Random album tile (a shuffle mark as its cover), just before the first album. Press Play on it to hear a random album from its first track, in order.

### Fixed

- **Bluetooth connects within seconds.** "Connecting…" could last 20–30 seconds after the headphones had already announced the connection. The player now asks the Bluetooth module for its status every second while connecting, as stock does, so "Connected" shows within about a second of the link coming up, whether the player or the headphones started it.
- **Headphones put in pairing mode are found.** Headphones the player already knew didn't show up when put in pairing mode while it was searching, until Bluetooth was turned off and on. Like stock, a search that follows a connection attempt now restarts the module first (half a second, still shown as Searching…). Remembered headphones a search finds connect straight away; after Pair new device or a disconnect, their row shows "Nearby" instead. Devices found stay listed between searches.
- **Play no longer pauses again on Bluetooth.** A late "paused" report from the headphones could pause playback right after Play was pressed. Reports are now ignored for 3 seconds after Play or Pause on the player, and a pause the headphones cause shows "Paused by <name>".
- **Diagnostics → Bluetooth** keeps the last 12 module messages over two lines, with the commands the player sent and the seconds between them, and keeps them when Bluetooth is turned off and on.

## 0.5.0

### New

- **Bluetooth headphones.** Settings → Bluetooth turns Bluetooth on, pairs headphones (Pair new device) and lists up to six you've used, most recent first. With Bluetooth on, the player connects to your last headphones by itself at start-up and when they come back, and resets and retries a connection that stalls. Press Play on the connected headphones to disconnect them, on others to switch. Music goes to the headphones whenever nothing is in either jack; their pause, play, next, previous and in-ear detection work, and a dropped connection pauses playback. A Bluetooth mark next to the play icon shows Bluetooth on (dim) or connected (bright). With nothing connected for 5 minutes, Bluetooth switches itself off until you open its screen or restart. Tested on the player with OnePlus Buds 3.
- **Charge screen.** Switching off with a cable plugged in, or plugging a charger into a switched-off player, now shows the battery level and "Charging" or "Charged" (the screen goes dark after 5 seconds; any button lights it). Hold Power to turn the player on, or unplug to switch it off.

### Fixed

- **Switching off on a cable.** Holding Power with a charger or computer plugged in left the screen black and the player unresponsive until the reset button: the cable kept it powered. It now shows the charge screen.
- **Battery percent** ([#4](https://github.com/BasilSkyWalk/echopod/issues/4)). The level dropped a few points whenever playback or the screen briefly loaded the battery, and could show 98 % on the charger when the battery held less, which then looked like drain while off. It now averages about a minute of readings, never shows more than 99 % while charging, and settles to the true level within a minute of unplugging.
- **Start-up.** The screen stays dark until EchoPod's first frame, so the panel's noise no longer flashes in the corner.
- **Dark screen during USB.** A message shown with the screen off (such as "Computer found") could stay almost black for the whole USB session; it now lights fully.

### Known limits

- To switch from Bluetooth to wired headphones, pause, plug them in and press Play: while music goes to Bluetooth the player can't read the jack.

## 0.4.0

### New

- **Chinese, Japanese and Korean names.** Song, artist and album names in Korean, Simplified Chinese and Japanese no longer show as `?`: the player now has the 2,350 common Hangul syllables, the 6,763 hanzi of GB 2312, the kanji of JIS X 0208, kana and CJK punctuation, at both text sizes (Noto Sans CJK SC). The characters are stored in the firmware image and read as a screen needs them. Traditional-only Chinese characters, emoji and other scripts still show as `?`.

### Changed

- **Accented letters** (é, ü, ñ…) are now read the same way instead of being built into the firmware's code. They look exactly as before, and the space they free leaves room for future features.

## 0.3.0

### Fixed

- **Going back to FiiO's firmware 4.0.0.** EchoPod refused FiiO's newest 8 GB firmware (`HIFIEC40.IMG`) as "isn't a valid image for this player": it told the 8 GB and 512 MB models apart by a size field that FiiO changed in 4.0.0. It now looks for the update file name each FiiO image is built to look for (`HIFIECnn.IMG` on the 8 GB model, `MINIVnnn.IMG` on the 512 MB one), checked against FiiO's 3.7.0 to 4.0.0 images, and still refuses the 512 MB model's firmware. Tested on the player: 0.3.0 installed FiiO 4.0.0, and FiiO 4.0.0 installed 0.3.0 again. On 0.2.0, install FiiO 3.9.0 first and update to 4.0.0 from there.
- **USB DAC plays on more computers and phones.** Android always picked 96 kHz, and some computers pick 88.2 or 96 kHz too, but this chip can't take packets that large, so those hosts showed the format and played nothing. DAC mode now offers 44.1 and 48 kHz (16 or 24-bit), and no longer depends on the order in which the computer sets the format and the rate. All four formats played on the player from Windows 11, and it played from an Android phone; Linux hasn't been tried yet. If you set your computer to 88.2 or 96 kHz before, pick 48 kHz.
- **Gapless MP3.** MP3s made with LAME or ffmpeg now play without a gap between tracks: the player reads the encoder delay and padding from the file's LAME tag and drops the silence the encoder added at each end.

### Faster

- **Rescans keep the covers.** A rescan no longer decodes every album's picture again: covers whose art is unchanged are kept, and only new albums and changed art are decoded (about half a second each). The first rescan after updating still decodes them all, once. The covers file now has room for 2,673 albums instead of 2,674.
- **echo-sync's covers are kept too.** echo-sync now writes `covers.bin` in the player's layout with the same art stamps, so when the player does rescan (say after you add an album), it keeps the covers the computer made and decodes only what's new. Download echo-sync again to get this.
- JPEG covers decode slightly faster (about 5–8 % on a PC).

## 0.2.0

### New

- **Audiobooks and podcasts.** Put books in an `Audiobooks` folder and shows in a `Podcasts` folder at the top of the card. Home gets an Audiobooks and a Podcasts shelf. Each book and episode remembers where it stopped, books always play in order (even with shuffle on), and holding vol± jumps 30 s forward or 15 s back instead of skipping. Books and episodes stay out of Music: they never show up in Cover Flow, Albums, Artists, Genres, Songs or shuffle.
- **USB DAC mode.** Settings → USB mode → DAC turns the player into a USB sound card (USB Audio Class 1.0, 16 or 24-bit up to 96 kHz). It has its own volume, remembered between sessions (vol± changes it, hold to repeat), and the screen shows the format and volume.
- **echo-sync: build the library on a computer.** One file for Windows, macOS (Apple silicon) or Linux, nothing to install. It reads the music on the player's card (in Storage mode or a card reader) and writes the library, covers and scan fingerprint, so the player starts without scanning. 1,563 tracks take about 2 minutes. See "Build the library on a computer" in `docs/user-guide.md`.

### Faster

- **USB storage reads:** about 3.9 MB/s instead of 1.3 MB/s on Windows (one multi-block card read per request).
- **Library rebuild on the player:** writes of whole sectors go to the card in one command without reading them first, folder listings are read 4 KB at a time, and opening the tracks of one album no longer walks the card from the top for each one. A 1,563-track, 144-album card now rebuilds in about 1 minute 45 seconds, covers included; the faster file lookups alone took its track scan from 61 s to 19 s.
- **echo-sync** skips embedded pictures and padding when it reads tags: 2 minutes instead of 22 for the same card.

### Fixed

- Settings → Brightness: every level now changes the backlight, from dimmest at 1 to full at 10.
- Settings → Output explains what it does: both jacks are driven by the same DACs, so it picks the volume table, and Auto shows the plugged jack.
- Favorites no longer makes the music repeat a beat while the list opens.
- Sound comes back after using USB, without a restart.
- The battery level no longer drops by about 10 % when the charger is plugged in, and doesn't jump when it's unplugged.
- After echo-sync, the player no longer rescans exFAT cards: both sides now compare file times in UTC.

### Good to know when updating

- **The library format changed (version 3).** After updating, the player rebuilds its library once, even if the music didn't change. echo-sync files from before 0.2.0 write the old format: download echo-sync again.
- **Rescan timings.** Each rebuild saves where its time went to `/.echo/rescan_times.bin` on the card. If a rebuild seems slow, send that file with your report.
- Everything else that's missing or unproven is in `docs/known-issues.md`.

## 0.1.0

The first public build: FLAC, MP3 and WAV playback, library and covers built on the player, Cover Flow, browsing, playlists, favorites, resume, USB storage, and updates (and the way back to FiiO's firmware) from a file copy.
