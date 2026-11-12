# Gnarlyr user guide

Gnarlyr is firmware for the full-size Snowsky (FiiO) Echo music player. It replaces FiiO's firmware and has large text, a Cover Flow album browser, a music library built on the player, favorites, playlists and an Up Next queue. Snowsky and FiiO are trademarks of their owners, and Echo is their product name. Gnarlyr is not made, endorsed or supported by FiiO.

Gnarlyr 1.0.0 is not released. Developer firmware runs on the owner's full-size Echo; [`known-issues.md`](known-issues.md) records the device checks and what still needs validation. This guide describes the implemented behavior.

**Use it at your own risk.** Gnarlyr comes with no warranty (see `LICENSE`). Replacing a player's firmware can leave it unable to start; the way back is maskrom recovery ([`device-recovery.md`](device-recovery.md)). Only install it if you're prepared to use that.

## Which players

- **Snowsky Echo (full size, 480 × 222 screen): the only player it is built for.** This project has the 4 GB model. Podium's author reports that Podium, which Gnarlyr is built on, also runs on the 8 GB model; this project has not tried one. FiiO's update file for this player is called `ECHOVnnn.IMG` (V2.0.0 is `ECHOV200.IMG`).
- **Echo Mini: not supported.** Gnarlyr refuses the Echo Mini's firmware. EchoPod is the firmware for the Echo Mini.

## Before you install

1. Copy anything you want to keep off the player's internal storage.
2. Charge the battery.
3. Prepare maskrom recovery and check that it reaches your player ([`device-recovery.md`](device-recovery.md)). It is the only way back if the player doesn't start.
4. Build the image (`README.md`, Build and test). There is no download yet.

## Install

This project's player has used maskrom recovery and file-copy updates between developer builds, including the transition to the canonical Gnarlyr identity. Follow [`device-install.md`](device-install.md) for the route matching the installed firmware.

Installing by copying a file under FiiO's firmware is not verified. FiiO's updater only reads files named `ECHOVnnn.IMG`, so `GNARLYR.IMG` would have to be renamed, and whether FiiO's 2.0.0 accepts it is not known.

On the first start with a microSD card in the player, Gnarlyr builds its library. You can browse and play as soon as it says "Library: N tracks". Album covers follow while the player is idle and not playing (or paused for a minute): they appear one album at a time, a press of any key or starting a song pauses them, and they carry on the next time the player sits idle, also after a restart. Albums without their cover yet show their initials. Wait for the library to finish before you plug the player into a computer.

## Home

Home starts with **Music**, **Mixtapes**, **Stats**, **USB Mode**, **Clock** and **Settings**, in that order. While a track is loaded, playing or paused, **Now playing** appears above Music; the row disappears when no track is loaded. The artwork caption shows the track title when space allows. Press Play there to return to Now Playing; Menu on Now Playing opens Up Next.

Audiobooks, Podcasts and Up Next are hidden on Home by default. Audiobooks and Podcasts are available at the end of Music when their shelves exist. Settings → Home screen can show them on Home too.

## Your music

- Put music on a microSD card formatted FAT32 or exFAT, in any folder layout. With no card in the player (or one it can't read), it uses internal storage instead. Settings → About → Music shows which one is in use. Settings are kept on internal storage either way, so they stay the same when you insert or remove the card.
- Gnarlyr supports FLAC, MP3, WAV and 16/24-bit AIFF, up to 192 kHz, and `.m4a` and `.m4b` files with AAC-LC or Apple Lossless audio. Speech recordings at 8, 11.025, 12, 16, 22.05 and 24 kHz are converted to 32, 44.1 or 48 kHz for playback; 32 kHz plays at its original rate. Speed and pitch stay the same. DSF/DFF convert to 88.2 kHz PCM: DSD64 and DSD128 accept mono/stereo, and DSD256 accepts mono; stereo DSD256 is refused. It lists the other formats FiiO lists, and says when it can't play one.
- APE accepts 16/24-bit mono or stereo: Fast through 192 kHz, Normal through 192 kHz mono or 96 kHz stereo, and High through 96 kHz mono or 48 kHz stereo. WMA Standard v1/v2 accepts mono or stereo through 48 kHz; Pro, Lossless, Voice and encrypted WMA are refused. Tags and embedded covers appear in the library.
- Ogg Vorbis (`.ogg`) accepts mono or stereo at 8–192 kHz within the decoder limits; Ogg Opus (`.opus`) accepts mono or stereo with channel mapping family 0. Chained Vorbis files play the first stream only. Large picture comments and separate interleaved streams are skipped during decoding. Both formats play in the desktop simulator; player listening and high-rate playback still need checking. See [format limits](ogg-vorbis.md).
- Music → Folders shows the card as it is: folders, then each audio file by its full name with its extension (`01 Intro.mp3`). The other lists show titles from the tags. Hold ▶ on a folder there to play everything under it, subfolders included, in the order Folders shows it (a CUE album plays once, as its songs); the queue follows the shuffle setting and has no length limit beyond the library's 65,535 tracks.
- A `.cue` file next to a single FLAC, WAV or MP3 album image splits it into one track per CUE track (see "CUE sheets" below). Album covers come from art inside each album's first track (any of the formats above, including Ogg and Opus), or from a `cover`, `folder`, `front` or `albumart` JPEG or PNG next to the tracks when there is no usable art inside. JPEG (baseline or progressive, colour, greyscale or CMYK, upright as its EXIF orientation says) and PNG covers of any size are shown; lossless, arithmetic-coded or 12-bit JPEGs and other image formats are not.
- `.m3u` and `.m3u8` playlists anywhere on the card appear under Music → Playlists.
- **Favorite Playlist.** Hold Play on a song in any list and choose Add to Favorite Playlist. The song goes into `Favorite Tracks.m3u` in the top folder of the music storage (the card, or the player's own storage without a card). The file is made on the first use, holds one path per line relative to that folder, and can be read and edited on a computer. A song that is one track of a CUE album gets the line `#GNARLYR-START:` and the track's start in milliseconds above its path (the path is the album's audio file, so computer players open the whole album); the player and echo-sync use the start to pick that exact track, and a line without it stands for the album's last track. Each track of a CUE album is added and checked on its own. A song already in it shows "Already in Favorites". The file has room for a few hundred songs; when it is full, or a computer edit left it without spare room, the player shows "Couldn't add to Favorites". Music → Playlists shows Favorite Tracks with the new song straight away, with no rescan, also after a restart. Lines added to the file on a computer show after the next library scan.
- The library holds up to 65,535 tracks; [`known-issues.md`](known-issues.md) says what has been tested.
- Gnarlyr keeps its own files in a hidden `/.gnarlyr` folder on the card, about 110 MB of them. Don't delete it unless you want the library rebuilt.

## Format a card

Back up everything you want to keep on the microSD card. Formatting erases its music, library, saved queue, resume positions and mixtapes.

1. Pause playback from the card and finish any USB session. Charge the battery above 30%, or connect a charger.
2. Open Settings → Format card. Check the card size and the warning that everything on it will be erased.
3. Press Play to continue. The second confirmation starts on Cancel; choose Erase card with a volume key, then press Play. Back cancels either confirmation.
4. Keep the card inserted until formatting finishes. The library starts empty; copy music over USB to rebuild it.

Cards through 32 GB use FAT32; larger cards use exFAT. The volume is named GnarlySD. Internal storage keeps its files. This is a quick format: old file contents can remain recoverable until new files overwrite them.

An interrupted format reports that the card needs formatting. Reinsert the card and choose Format card again before copying music. Physical card formatting still needs the owner checks listed in [known issues](known-issues.md).

## Hide or delete a song

Hold Play on a song in a Music list or a smart list for its actions.

- **Hide track** takes the song out of every list, Play all, shuffle and the smart lists at once, and keeps it out after rescans. Settings → Hidden tracks (shown while something is hidden) lists hidden songs by file name; press Play on one to show it again. A song hidden before the last rescan comes back at the next rescan. Up to 16 songs can be hidden.
- **Delete file** asks first, naming the file; Cancel is selected. Deleting removes the file from the card, skips to the next song if it was playing, and takes it out of the lists, the queue and Up Next without a rescan. Mixtapes skip it when they load. A song that is one part of a CUE sheet or an audiobook's chapter can't be deleted on its own.
- Up to 12 songs can be hidden or deleted between rescans; after that the player asks for a rescan first.

## Search, sorting and personal tags

Music → Search finds songs across their metadata and paths. View options combine sorting, filters and personal tags. See [Search, sorting and personal tags](library-discovery.md) for controls, scopes, limits and saved-view behavior.

## Smart lists

Music → Smart Lists has five lists that build themselves from what you play and add. Each holds up to 100 songs and works like the other song lists: ▶ plays the list from the selected song (Shuffle applies as usual), and holding ▶ offers Play Next, Add to Up Next and the other song actions. The player has no clock, so these lists count plays and library updates instead of days:

- **Recently Added**: songs that are new to the library, newest first. The first time you open a smart list, the player notes the songs already there; songs that appear after a later rescan or library build show up here, those from the latest change first.
- **Recently Played**: the songs you played last, each once, newest first.
- **Most Played**: the songs you listened to most. A play counts when at least half of the song was heard.
- **Never Played**: songs you haven't played, the newest additions first, then in album order.
- **Old Favorites**: songs from your Favorite Tracks playlist and favorite albums that you haven't played in your last 500 plays, the longest unplayed first, then favorites you have never played.

The history behind them is your last 4,096 plays. The first list you open after the library changed checks the library first and says so; a list is then remembered until you play something, restart the player or change your favorites, so opening it again is instant.

## Audiobook chapters

M4B files appear on Audiobooks wherever you keep them. Named `Audiobooks` and `Audiobook` folders share the shelf and retain their folder hierarchy; M4B books in other folders appear directly. Whole-file books remember their own position and play as one book. A chaptered `.m4b` appears as a book folder containing its chapter titles and lengths. The player scan and desktop echo-sync both preserve these chapters. Author and title folders stay in their original hierarchy. Select a book's chapter folder to continue listening, or hold Play to choose a chapter; this works inside nested folders too. Selecting a chapter starts at its position in the book; the final chapter continues to the file's end. Books without usable chapter information remain playable as one track. Rescan or run echo-sync again after changing chapter information or to apply this recognition to an existing library.

## CUE sheets

An album ripped as one big FLAC, WAV or MP3 with a `.cue` file shows up as a normal album with one track per CUE track:

```
Depeche Mode/Violator/
  Violator.flac           the whole album in one file
  Violator.cue            hidden as a track, its tracks take its place
```

- The sheet must name one file. The player looks for that name in the sheet's folder (capitals don't matter), and when there is none, for an audio file with the same name as the sheet (`Violator.cue` and `Violator.flac`), which is what many rips need. CUE images use FLAC, WAV or MP3. Sheets with another audio extension, several files (one per track) or a missing file are ignored; their audio files list as before.
- Track titles come from `TITLE`, artists from `PERFORMER` (the track's, otherwise the one at the top of the sheet, which is also the album artist), the album from the top `TITLE`, the genre from `REM GENRE` and the year from `REM DATE`. What the sheet doesn't say comes from the audio file's tags, then from its folder name. A track without a title is called "Track 3".
- A track starts at its `INDEX 01` and ends where the next one starts, so silence marked `INDEX 00` before a track belongs to the track before it. Tracks play without a gap.
- The sheet is read as UTF-8 (a byte order mark is fine) or, when it isn't valid UTF-8, as Windows-1252.
- In Folders the album folder shows the sheet (`Violator.cue`) and the audio file (`Violator.flac`), as they are on the card. The audio file shows the length of the whole album. The sheet opens its songs by title with their own lengths, and choosing one plays from there; choosing the audio file plays the album from its first song.
- Time, progress, seeking, resume, Up Next, shuffle, favorites and bookmarks all work on the CUE track, not on the whole file. Covers come from the audio file's art or a `cover.jpg` next to it, as for other albums.
- Rescan the library (Settings → Rescan library) or run echo-sync again after adding or changing a sheet.

## Copying music over USB

1. Connect the player and choose **Connect to computer** when it asks (see [When you plug in](#when-you-plug-in)). Two drives appear: internal storage and the SD card.
2. Copy music to the SD card, or to internal storage when no card is in the player.
3. Disconnect in one of two ways:
   - **Eject on the computer.** Eject both drives (Finder's eject button, or `diskutil eject`). The player shows **USB mode**.
   - **Hold Back on the player** for a second. If the computer hasn't written anything for 3 seconds, the player tells it the drives are gone, shows "Disconnecting..." for 2 seconds and then **USB mode**, just as after an eject. If the computer is still writing, the player shows "Still writing..." and disconnects as soon as it stops. To disconnect at once anyway, keep holding Back for 3 more seconds; recent changes may be lost, and the player says so ("Disconnected too soon").
4. When music changed, the player rebuilds its library by itself, then finds the new covers while it is idle. Copying other files doesn't trigger a rebuild. Settings → Rescan library checks the card and rebuilds the library when something changed, or after a firmware update; otherwise it says "Library is up to date". If the player restarts during a rebuild, it tries again at the next start and leaves out the file it stopped on; after three stops in a row it builds the library without covers.

If you unplug the cable before either, recent changes may be lost, and the player warns you ("Unplugged too soon").

After a disconnect from the player, the computer sees the drives disappear rather than being ejected. A Mac may then show its "Disk Not Ejected Properly" notice; when the player had waited for the computer to finish writing, your files are already on the player.

**The USB screen** opens on an overview: the current transfer rate in MB/s with an arrow (down while the computer mostly copies to the player, up while it mostly reads), a graph of the last 10 seconds, how much was read and written, and a gauge for each drive (internal storage and microSD card) with its free space, a light and a ↓ write and ↑ read arrow that glow while that drive is used and fade over about a third of a second, and "Ejected" or "No card" where that applies. Activity is shown only by these lights, the number and the graph, so no words flash while files copy; the arrow's direction and "Idle", shown when nothing has moved for two seconds and with a dimmed graph, each stay for at least a second. Tap Back for the details: per drive, the data read and written, the read and write commands, the average size of a command and the free space when the computer connected; then the peak and average rate, how busy the drives were, errors with the last SCSI sense code, the link (480 Mbit/s high speed or 12 Mbit/s full speed, with the packet size) and the session time. Each tap moves on a page and the last returns to the overview; the player keeps the page you chose for the next connection until it restarts. Free space is read once, when the computer connects, and isn't updated while the computer has the drives. While the player waits for writes to finish or disconnects, the screen shows the turning cassette instead. It works in both orientations and every text size.

## When you plug in

Plugged into a computer, the player doesn't connect by itself. It shows **Computer found** and these choices, each with a short name, a picture and a line under it that says what it does:

| Choice | Picture | What it does |
|---|---|---|
| **Connect to computer** | laptop | "Music drives, firmware updates, companion app." The computer sees the player's drives: copy music, install a firmware update or use the Gnarlyr companion app on your computer. Music stops until you disconnect. The player charges. |
| **Charge & play** | battery with a bolt | "Charges the battery while you keep listening." Nothing appears on the computer. |
| **Run from USB power** | plug | "Powers the player from USB to save the battery." Keep listening without charging: the cable powers the player and the battery rests. When the battery is low, the line says "Battery low: charges first" and the player charges until it is safe (see [Run from USB power](#run-from-usb-power)). |
| **USB audio (DAC)** | speaker | "Play your computer's sound on the player." Your computer's audio plays through the player's headphone outputs (see [Using the player as a USB DAC](#using-the-player-as-a-usb-dac)). Music on the player stops. The player charges. |

**Connect to computer** is the first row and is highlighted, so one press of Play connects. Doing nothing means **Charge & play**: the screen counts down "Charging in 15 s", and any button starts the count again. Back, the end of the count or the screen going dark all mean Charge & play, and music and menus carry on. To answer the same way every time, hold Play on a choice: the player confirms with "Always: …" and saves it as Settings → When plugged in.

The player asks once per plug-in. Unplug and plug in again to be asked again, or use **USB mode** to change without unplugging (below). A wall charger never asks: the player charges, or plays on USB power when that is your saved choice and then says "Run from USB power".

On the charge screen (the player switched off on a cable) a computer wakes the screen with the same question. Charge & play, Back or the countdown return to the dark charge screen; Connect to computer and USB audio (DAC) connect, and the charge screen comes back when you're done.

**Settings → Power on** chooses how to leave the charge screen: Hold (the default) requires Power for two seconds; Press starts the player with a tap. Press is convenient when the player is switched off on a cable, but a pocket press can start it and use battery. Connecting USB still opens the charge screen and keeps the same computer-connection choices.

From completely off, keep Power held until the player starts. Firmware latches power immediately when it gains control. This setting cannot shorten any hold required by the boot ROM or power circuitry before that point.

**Settings → When plugged in** answers for every plug-in: **Ask** (the default), **Connect to computer**, **Charge & play**, **Run from USB power** or **USB audio (DAC)**, with the same pictures and lines. Run from USB power also applies on a wall charger; with any other choice a wall charger charges.

## Switching USB modes

The same chooser, titled **USB mode**, opens from Home → USB mode, Settings → USB mode, Menu on the charge screen, and after you leave Connect to computer or USB audio (DAC). It lists Connect to computer, Charge & play, Run from USB power (while a cable is in) and USB audio (DAC), each with its line; the mode in use has a check mark. A choice made here lasts until you unplug; hold Play to keep it for every plug-in. From the charge screen, leaving the chooser in any way returns to the charge screen.

To leave Connect to computer, eject both drives on the computer or hold Back on the player (see [Copying music over USB](#copying-music-over-usb)), then wait for USB mode. The player keeps the track, position and queue for your return. From USB audio (DAC), hold Back for a second and release it to return to USB mode, or use the DAC menu.

While the player runs from USB power, the battery in the status bar and on the charge screen shows a plug instead of the bolt, and the charge screen says "Charging off".

## Using the player as a USB DAC

**Not run on a player by this project** (see [`known-issues.md`](known-issues.md)). In this mode the player is a sound card for your computer or phone: their audio plays through the player's headphone outputs.

1. Plug in the USB cable and choose **USB audio (DAC)** when the player asks (or, with the cable in, Home → USB mode → USB audio (DAC)).
2. The player shows "USB audio (DAC)". It offers USB Audio Class 2.0 over a high-speed connection and Class 1.0 over full speed. Select Gnarlyr in the host’s audio settings; phones need an adapter that passes USB data. Host compatibility still needs the checks listed in known issues.
3. On the computer, choose Gnarlyr as the sound output (Windows: the speaker icon in the taskbar; macOS: System Settings → Sound → Output). The player shows the format and its volume, for example "48 kHz 24-bit · Volume 35".
4. Use the computer's volume, the player's volume keys, or both. DAC mode has its own volume, separate from music: it starts at 35 of 60, the screen shows it ("48 kHz 24-bit · Volume 35"), tap Vol± to change it on release, and the player remembers it for next time. Start low. Press ▶ to send play/pause to the computer. Hold Vol+ for 400 ms to send next track, or Vol− for previous; each hold sends one command. The computer’s media app decides how to handle these keys.
5. Hold Back for a second and let go to open USB mode, then choose another mode. The player keeps charging.

Press Menu in DAC mode to adjust Volume, Gain, Output jack and Headphones, or choose Connect to computer or Charge & play. Volume and headphone changes apply to the live stream. The DAC screen shows the negotiated format in a frame, the stereo levels after headphone trim, the output jack, the computer's volume, the DAC volume and a bar for the buffer. Tap Back for the details: the exact sample rate and bits, the USB interface and channels, the buffer (average with its low and high), the clock correction in ppm and the feedback value, underruns and overruns, the output, the gain with its DAC register value, the computer's volume in tenths of a dB, the data rate and the session time. Tap Back again to return; holding Back still leaves.

The computer picks the format (macOS: Audio MIDI Setup; Windows: Sound settings → Gnarlyr → Properties → Advanced).

| Connection | Stereo PCM depth | Rates in kHz |
| --- | --- | --- |
| High speed, UAC2 | 16, 24 or 32-bit | 44.1, 48, 88.2, 96, 176.4, 192 |
| Full speed, UAC1 | 16 or 24-bit | 44.1, 48 |

High-speed mode uses asynchronous feedback to follow the player’s audio clock, with a 5 ms device buffer target. Full-speed mode retains a 20 ms target and sample correction. Host buffering adds delay. To preserve PCM sample words, turn headphone profiles Off and use an exclusive/direct host output at the file’s own format with host processing disabled. Player and host volume still affect the analog result. Music on the player stops while it's a DAC. Choose Connect to computer in USB mode to copy music without removing the cable. A charger works the same in both modes.

**The DAC menu** (press Menu) changes, without leaving DAC mode: **Volume** (press Play, then Vol±), **Gain**, **Output** (which jack's volume levels to use), **Headphones** (Off or one of your profiles) and switches to **Connect to computer** or **Charge & play**. Vol± moves, Play changes the highlighted row, Back or Menu closes the menu. Your choices are kept for next time. The equalizer and ReplayGain don't apply in DAC mode; the sound reaches the DAC as the computer sends it, with only volume, gain and the headphone profile's trim, balance and ceiling.

## Build the library on a computer

Rebuilding on the player takes a while, and its covers fill in afterwards while the player is idle. A computer can build the same library and album covers instead, straight onto the card, and tell the player that the card has already been scanned.

The tool is **echo-sync**, `tools/echo_sync.py`. It runs from the source; there are no packaged downloads. Install Python 3, then the tools' packages once with `python3 -m pip install -r tools/requirements.txt`.

1. Copy your music to the card, in the player over USB or in a card reader.
2. Run `python3 tools/echo_sync.py /Volumes/CARD` with the card's mount point (on Windows, `tools\echo_sync.bat E:` finds Python for you). Without an argument it looks for a card the player has used and asks.
3. When it says "Done", eject the card (in the player: both drives).

This project has run the tool's tests, not the tool on a real card.

It reads the whole card, as the player does, and writes `library.db`, `covers.bin` and `scan.bin` into the card's `/.gnarlyr` folder. If the player has never used the card, it asks before creating `/.gnarlyr` (`--yes` creates it without asking). Files the player made are overwritten in place and keep their size; missing ones are made at the player's sizes (4 MB, 80 MB and 4 KB). It then prints what it found, for example "1,402 tracks, 120 albums, 3 playlists" and "115 covers, 5 without art".

**Why the player doesn't rescan.** After a USB session the player checks for changes: it compares the names, sizes and modification times of the music, playlists and cover images on the card with a fingerprint it keeps in `scan.bin`. The computer writes the same fingerprint, so the check finds nothing new and the player keeps the computer's library. With a card reader there's no USB session, so the player uses the new library straight away.

**When the player rescans anyway.** If the fingerprints differ, the player rescans as it would without the computer, and nothing is lost. That happens when:

- the music changed after the computer ran;
- the card is exFAT and a file's time was saved without a time-zone tag (modern Windows, macOS and Linux always add one; some cameras and old devices don't);
- the card is FAT32 and the computer reads its file times differently from the player (not checked on any system by this project);
- the card has folders more than 31 levels deep or paths longer than 1,023 bytes (the player can't fingerprint those);
- a name was saved decomposed (for example by Linux) and read on a Mac, or only as an old DOS short name with accented letters.

To see whether it works on your card, let the player scan it once, then run echo-sync with `--check` without changing anything (`python3 tools/echo_sync.py /Volumes/CARD --check`). "They match" means the player won't rescan after the next sync.

**After a firmware update** the player checks the music once when it first starts ("Checking for changes"), and rescans only if the music changed, or if it can't read the library on the card (as after an update that changes the library format). `scan.bin` remembers the firmware version; the computer keeps it. A `scan.bin` the computer had to create has no version yet, so the player checks once on its next start too.

| Option | Effect |
|---|---|
| `--dry-run` | Read everything and say what would be written, but write nothing |
| `--no-covers` | Build the library only; `covers.bin` stays as it is, so new albums show without covers until the next rescan on the player |
| `--covers-only` | Rebuild `covers.bin` from the card's `library.db` |
| `--no-fingerprint` | Leave `scan.bin` alone, so the player compares the music with its own last scan |
| `--check` | Only compare the computer's fingerprint with the player's |
| `--yes` | Create `/.gnarlyr` without asking |
| `--verbose` | List the albums without art |

The computer picks each album's cover the way the player does: the picture inside the album's first track (FLAC, MP3, WAV, DSF/DFF, Ogg, Opus, `.m4a`, `.ape` and `.wma`; a front cover wins), otherwise, or when that picture can't be shown, a `cover`, `folder`, `front` or `albumart` JPEG or PNG next to the tracks. The covers can look a little different from the player's own, because the computer resizes them with a different filter. When the player rescans later (for example after you copy a new album to it), it keeps the computer's covers and only decodes art that's new or changed; on a card that isn't FAT32 or exFAT, albums whose cover is an image file are decoded again.
The computer picks each album's cover the way the player does: the picture inside the album's first track (FLAC, MP3, WAV, AIFF, DSF/DFF, `.m4a`, `.ape` and `.wma`; a front cover wins), otherwise a `cover`, `folder`, `front` or `albumart` JPEG or PNG next to the tracks. Ogg pictures are skipped, as on the player. The covers can look a little different from the player's own, because the computer resizes them with a different filter. When the player rescans later (for example after you copy a new album to it), it keeps the computer's covers and only decodes art that's new or changed; on a card that isn't FAT32 or exFAT, albums whose cover is an image file are decoded again.

## Buttons

| Button | In lists | On Now Playing |
|---|---|---|
| Volume up / down | Next / previous item; hold to scroll faster | Volume; hold Vol+ for the next track, Vol− for the previous one (in audiobooks and podcasts: 30 s forward, 15 s back) |
| Play | Select; a song plays the list from that song. Hold on an album for Favorites, hold on a song for Play Next / Add to Up Next / Add to Favorite Playlist, hold on a folder in Folders to play everything under it, subfolders included | Play / pause; hold to seek |
| Menu | Opens Now Playing while a track is loaded | Opens Quick Settings; hold for Lyrics |
| Volume up / down | Next / previous item; hold to accelerate and jump by letter in long name-sorted lists | Volume; hold Vol+ for the next track, Vol− for the previous one (in audiobooks and podcasts: 30 s forward, 15 s back) |
| Play | Select; a song plays the list from that song. Hold on an artist or album artist for Play all / Shuffle albums, hold on an album for Favorites, hold on a song for Play Next / Add to Up Next / Add to Favorite Playlist, hold on a folder in Folders to play everything under it, subfolders included | Play / pause; hold to seek |
| Menu | | Opens Up Next (also on Home, below Now Playing) |
| Back | Back; hold to go home | Back; hold to go home |
| Power | Screen off or on; hold 2 s to turn off | Same |

The Now Playing column above is for Settings → Skip songs = Hold vol±; the other two choices move skipping to a double-click of Vol± or a double/triple click of ▶ (see Skip songs).

**Wired headphones with buttons** (3.5 mm jack, from any screen, screen on or off): the middle button pauses and resumes, press it twice for the next track and three times for the previous one; + and − change the volume, hold + for the next track and − for the previous one. Not tried on a player by this project: see [`known-issues.md`](known-issues.md).

With the screen off, ▶‖ still pauses and resumes, a tap of vol± changes the volume and holding vol± skips tracks (or jumps 30 s / 15 s in audiobooks and podcasts).

After 30 seconds paused, the player switches its sound output off to save battery. Play then takes a moment longer to start and fades in from where you paused; Bluetooth headphones stay connected.

Opening an artist in Music → Artists or Album Artists shows their albums as covers, oldest first, with each album's title and year under the centre cover. Move with the volume buttons, press ▶ to open the album, hold ▶ to play or shuffle all of the artist's albums, and press Back to return to the list. An artist with one album opens it directly.

In Cover Flow (Music → Cover Flow), the tile before the first album, with a shuffle mark for a cover, is **Random album**: press ▶ on it to play a random album from its first track. Hold a volume button to scroll faster; keep holding to jump between artist initials. A temporary letter badge shows the current section. Press ▶ to open the selected album, or hold ▶ to favorite it. An album without artwork gets a cover made from its initials, or its artist's when the title has none, in colours from the current theme.

## Quick Settings

Tap Menu on Now Playing for **Quick Settings**. Vol+ moves to the next row and Vol− to the previous row; ▶ changes the selected value immediately. Back closes the panel and hold Back goes Home. Menu returns to Now Playing.

The rows are Shuffle (Off, Songs, Albums), Repeat (Off, One, All), Equalizer (presets, Custom and saved slots), Headphone profile (Off and four profiles), Playback speed, Sleep timer, Transitions (Off or Fade), ReplayGain (Off, Track, Album), Lyrics and Up Next. Each setting shows its current value. Lyrics and Up Next open their screens; Back returns to the selected panel row. Up Next is also on Home. Hold Menu on Now Playing still opens Lyrics directly.

Changes use the same saving and sound controls as Settings. Sleep timer runs for this session, just like Settings → Sleep timer. Transition length, custom EQ curves and headphone profile editing remain in Settings. Play on Playback speed steps to the next speed (after 3× it returns to 0.5×), applies it at once and saves it.
## Long lists and whole artists

In Artists, Album artists, Albums, Songs, Genres, Playlists and Favorites, hold Vol+ or Vol− to move faster, then jump to the start of each letter section. The same gesture works in name-sorted search results and filter choices, genre albums, and the name-sorted folders and files on Folders, Audiobooks and Podcasts. The badge shows the current letter and disappears shortly after release. Accents and a leading “The” follow the library's sorting; digits and symbols show **#**, at their place in the sort order. Small sections are kept, and holding stops at the ends. Lists that fit on one screen keep their ordinary scrolling.

Folders stay before files. Files keep disc and track-number order, with letter jumps inside each group of files having the same disc and track number. Chapters, playlist contents and artist albums keep their playback order. Audiobook files outside the named audiobook folders retain their existing album order.

Hold ▶ on an artist or album artist to open **Play all** and **Shuffle albums**. Play all starts with the earliest album, then follows year and album title, with each album's tracks in disc and track order. Shuffle albums changes the album sequence and keeps each album's tracks together in order. Both include whole albums, including guest artists and CUE tracks or chapters, and replace Up Next. Back cancels preparation. The exact queue and your position are kept across restarts; the queue can contain up to 65,535 tracks.

## Now Playing style

Settings → Now Playing style offers **Cover**, **Cassette**, **VU meters** and **Spectrum**. Cover is the default, and your choice stays after a restart.

Cassette puts the title, artist and album inside a neon wireframe shell above the theme's perspective floor. The left reel empties and the right reel fills as the track advances; the spokes turn during playback and stop when paused.

VU meters shows separate left and right neon dials. The bright needles follow the music with a gentle lag and a trailing echo; a small peak light marks a loud signal. Spectrum shows sixteen coloured frequency bars rising from the floor, with caps that briefly hold each peak. Both keep the title, artist, time and progress visible. Long labels end with an ellipsis.

The artwork uses your theme's colours in either orientation and at every text size. Pausing lets the needles and bars settle. Turning the screen off stops their updates.

Both styles use the same time, progress bar and playback buttons. Hold Play to seek, use the volume buttons to move the target, then press Play to keep it or Back to cancel. Menu opens Quick Settings and holding Menu opens Lyrics, with the default button assignments.

## Charging

The player normally charges whenever it's plugged in, on or off. Turning it off while it's plugged in (or plugging a charger into a switched-off player) shows the charge screen: a large battery in your theme's colours with the percentage beneath or beside it. While the player charges, the empty part of the battery gently pulses. "Charging" means the charger is active; "Charged" means a known full battery. "USB power" means the cable supplies power while charging is inactive. Before a battery reading is available, the screen shows "Reading battery". The screen goes dark after 5 seconds; press any button to see it again. Hold Power for 2 seconds to turn the player on (or tap it with Power on set to Press), or unplug it to switch it off completely. Press Menu to open USB mode from the charge screen. A plug instead of the bolt means the player is playing on USB power and not charging. Plugging into a computer wakes the screen and asks what the connection is for (see [When you plug in](#when-you-plug-in)).

## Button lock

Hold ▶, then press Power while still holding it, and keep both down for about a second to lock the buttons, for example before the player goes into a pocket; do the same to unlock. While locked, a padlock shows in the top bar and every button except Power is ignored; Power still turns the screen on and off. Pressing another button shows "Hold Play+Power" if the screen is on. Locking or unlocking lights the screen to confirm. Wired headset buttons keep working. Restarting the player unlocks it.

The top bar shows, from the left: play or pause, Bluetooth, the padlock, and the shuffle mode (a note with crossed arrows for Shuffle Songs, a disc with crossed arrows for Shuffle Albums); the battery sits on the right. The battery's fill shows how much charge is left. At 15% or less the fill turns red, and at 5% or less the outline turns red too. While the cable supplies power, the fill turns green and a bolt appears inside the battery: a light bolt with a dark edge while charging, a plain dark bolt once it's charged. To see the level as a number too, turn on Settings → Status bar → Battery percent: the percentage then shows to the left of the battery, in red when low.

## Skip songs

Settings → Skip songs picks how the buttons change the song on Now Playing and with the screen off. Hold vol± (the default): hold Vol+ for the next song and Vol− for the previous one. Double vol±: double-click Vol+ for the next song and Vol− for the previous one; a single click changes the volume after a short pause, and holding Vol+ fast-forwards and holding Vol− rewinds inside the song (5 s per step, 15 s per step after about a second), also with the screen off. Double/triple Play: double-click ▶ for the next song and triple-click for the previous one; a single click pauses or resumes after a short pause, and Vol± keep changing the volume at once. In lists and menus the buttons always react at once. With Hold vol± or Double vol±, holding Vol+ or Vol− right after a volume step, while the volume bar is still showing (about 2.5 seconds after the step, with the screen on or off), keeps changing the volume instead of skipping or seeking; pressing any other button ends that.

## Rewind on resume

After a pause, ▶ picks up a little before where you stopped, so you don't lose the thread. The longer the pause, the further back: nothing for a pause of 10 seconds or less, 3 seconds after a minute, 10 seconds after ten minutes and 20 seconds after an hour or more. Audiobooks and podcasts go back half as far again (4.5 s, 15 s and 30 s). After the player was switched off, the first ▶ goes back 20 seconds (30 seconds for audiobooks and podcasts), since the player has no clock to tell how long it was off. It never goes back past the start of the song, chapter or CUE track. If you move the position yourself while paused, ▶ plays from exactly there. The time on Now Playing shows the jump. Settings → Rewind on resume turns this off.

## Gain and DAC filter

Settings → Gain picks Low (the default) or High for both jacks. High gives harder-to-drive headphones more level. Choosing High lowers the music and USB DAC volumes by 6 steps first, so the sound never gets suddenly louder; turn it up again if you need to, within your headphone profile's ceiling. Going back to Low leaves the volume where it is.

Settings → DAC filter chooses how the DAC smooths the sound: Fast, low latency (the default, as on FiiO's firmware), Fast, phase compensated, Slow, low latency, Slow, phase compensated, or Non-oversampling. Fast filters keep the treble flat almost to the top of the range and cut sharply above it; slow ones begin to roll off a little earlier and more gently. Low latency filters delay the sound less; phase compensated ones keep every frequency in step. Non-oversampling skips the smoothing and softens the highest treble a little at 44.1 and 48 kHz.

Gain and DAC filter apply when you press ▶, not while you scroll. During playback the sound mutes for a moment while the player changes them. Both are kept across restarts.

## Headphone profiles

Settings → Headphones keeps four profiles. **Active profile** chooses one, or **Off** to use neutral sound. Open a profile to edit it, then choose **Use profile** to make it active.

- **Name:** choose In-ear, Over-ear, On-ear, Earbuds, Studio or Travel.
- **Trim:** reduce the level from 0 to −12 dB in 0.5 dB steps.
- **Balance:** move left or right in 1% steps. Balance reduces the opposite channel and never boosts either one.
- **Volume ceiling:** limit the volume to a step from 0 to 60; 0 mutes it.
- **Crossfeed:** Off, Low, Medium or High (see Crossfeed below). A new profile starts with the Settings → Crossfeed choice.

Press ▶ to save a value; Back cancels the choice. The selected profile, names and values are kept on internal storage across restarts and card changes. Trim, balance and the ceiling apply to music and USB DAC output. Music and USB DAC keep their separate volume levels.

Switching to a louder profile, including Off, lowers those volume levels first so neither channel rises unexpectedly. Restoring a channel that was completely muted can lower the volume to 0. Turn it up again as needed, within the chosen ceiling. Music already buffered finishes before new trim and balance values reach it.

## Crossfeed

On headphones each ear normally hears only its own channel, which can make stereo recordings sound as if they sit inside your head. Crossfeed blends a little of each channel into the other ear, softened and slightly delayed as it would be from speakers, so the sound moves out in front of you.

Settings → Crossfeed chooses Off, Low, Medium or High for when no headphone profile is active. Each headphone profile has its own Crossfeed setting, which applies while that profile is active. Low blends the least, High the most. Crossfeed lowers the level by about 2 dB so loud passages never clip. It acts on music and spoken word in stereo. Mono recordings play unchanged, and it doesn't apply in USB DAC mode.

## Safe start volume

When you turn the player on, plug headphones into either jack or connect Bluetooth headphones, a volume above the safe level is lowered to it, and a short "Volume lowered to 24" message says so. The safe level you pick is for wired headphones. Connecting Bluetooth headphones lowers only the Bluetooth volume, and only from above 50 down to 50, the Bluetooth level FiiO's firmware starts at. A quieter volume is never raised. Turn it up again as usual; it stays where you put it until the next time you turn the player on or plug headphones in. Settings → Safe start volume picks the level (12, 18, 24, 30 or 36, on the same scale Now Playing shows; 24 is the default) or Off. The USB DAC keeps its own volume and isn't affected.

## Portrait mode

Not tried on a player yet ([`known-issues.md`](known-issues.md)). Settings → Orientation has Landscape (the default), Portrait and Portrait, flipped. The screen turns as soon as you pick, and the player starts the same way next time. Pick whichever portrait way round puts the buttons where you like them.

In portrait, Home shows the Gnarlyr title below the status row and Now Playing keeps the compact status row; Home shows the album art under the menu, Now Playing puts the cover above the track, Cover Flow stacks the albums from top to bottom, and the status icons sit in a strip above the title. Lists show up to 13 rows. The volume rocker turns with the screen: in Portrait the buttons swap roles, so the one that is Vol− in landscape moves down through lists and the one that is Vol+ moves up; in Portrait, flipped they keep their usual roles. On Now Playing the button nearer the top of the screen turns the volume up, so in Portrait, flipped it's the one labelled Vol−. With the screen off, Vol+ is louder in every orientation.

## Charging display

The charging screen shows a large battery graphic and percentage. Its energy animation runs while the battery is charging and stops when the screen goes dark. An unknown reading keeps the battery graphic hollow. Press Menu to open USB mode; hold Power to turn the player on.

## Languages

The player includes English. Add other languages as external packs without reinstalling the firmware.

- When the player has more than one language, Settings → Language appears after Orientation and lists them by their own names. Pick one and every screen changes at once; the player starts in it next time.
- To add a language without reinstalling, copy its pack to the folder `.gnarlyr/lang` on the SD card (or on internal storage if you use no card). The file is named after the language's code, for example `de.pack`. The folder is hidden on most computers; create `lang` inside `.gnarlyr` if it isn't there. Eject, and the language is in Settings → Language.
- A pack is made for one firmware version. After an update, an older pack may be refused; the player then stays in its built-in language and Settings → Language says "Couldn't load language" when you pick it. Get the pack made for the new version.
- If you take out the card that holds the pack, the player uses its built-in language until the card is back.
- Today's fonts draw English and the accented letters of western European languages. Cyrillic is drawn in lists and titles but not yet in the small text, so it can't be used for a pack; Chinese, Japanese and Korean are not drawn.
- To make a pack, open the [browser builder](../../site/languages.html), download its English template, and translate the entries. Choose a language code and its native name, then build and download the pack. Missing translations use English. The builder checks the text and characters supported by this firmware. See [language-pack tools](../../docs/language-packs.md) for checked USB installation and language selection from a computer.

## Text size

Accent Color, Brightness, Text size, Orientation, Output, ReplayGain and Prevent clipping preview the highlighted value as you scroll. Press ▶ to save it. Back, leaving the picker or switching off restores the saved value. Accent Color changes the Graphite theme's highlights and decorative details.

Settings → Text size picks Small, Medium (the default) or Large for list rows, Now Playing, Cover Flow, toasts and messages. Small shows eight rows per screen, Medium six and Large five (in portrait: 13, 13 and 11). The title bar, the battery and the small details on Now Playing keep their size. The change shows at once; no restart is needed.

## Equalizer

Settings → Equalizer previews a sound: Off, Retro, Bass, Heavy, Pop, Jazz, Unique (the same curves as FiiO's firmware), Custom for the graphic sound, Custom (parametric), then your saved slots (Slot 1 to Slot 8) and the files from the `EQ` folder, followed by Sound shaping. Move with vol+ and vol− to hear the highlighted curve and see its frequency response. Browsing leaves saved settings unchanged. Press ▶ to keep the sound; Back cancels. Leaving for Now Playing, Home or another screen, or switching off, restores the kept sound. Choosing a slot or a file copies its curve into the corresponding Custom sound. Choosing a slot or either Custom also opens its editor, so the curve can be changed right away; for a slot, "Save to slot" starts on that slot. While the curve still matches a slot, Settings → Equalizer shows the slot's name in place of Custom.

Settings → Custom EQ edits ten bands: 31, 62, 125, 250 and 500 Hz, and 1, 2, 4, 8 and 16 kHz. Volume keys choose a band; ▶ enters adjustment. The selected frequency and gain remain visible beside the curve. Adjust with vol+ and vol− in steps of 0.5 dB, from -10.0 to +10.0 dB. Changes are heard while adjusting, but saved settings stay unchanged until ▶ keeps the adjustment. Back cancels the current adjustment. Leaving the editor also cancels an unkept adjustment. Keeping a changed band selects Custom.

**Saving a curve.** After the last band, Custom EQ offers "Save to slot". Select it with ▶, pick slot 1 to 8 with vol+ and vol− (a slot that already holds a curve shows "used"), and press ▶ to save or ↩ to cancel. The saved curve shows up in Settings → Equalizer as Slot 1 to Slot 8.

**Curves from files.** Make a folder named `EQ` at the root of the memory card (or of the player's storage when no card is inserted) and copy text files into it. Each file shows in Settings → Equalizer by its file name (up to 16 files, names up to 27 characters; names starting with a dot are ignored). Two formats work:

- Equalizer APO / AutoEQ: a line `GraphicEQ: 20 -1.5; 21 -1.2; 22 0; ...`. Other lines, such as `Preamp: -6 dB`, are ignored.
- One band per line: a frequency in Hz and a gain in dB, separated by a space, tab, comma or semicolon:

  ```
  31 1.5
  62 -2
  125 0
  1000 3
  16000 -4.5
  ```

Lines starting with `#` are comments. The points can be in any order and at any frequencies: each of the ten bands gets the gain interpolated between its two nearest points (by octave distance), or the nearest point's gain beyond the ends. Gains are rounded to 0.5 dB and limited to ±10 dB. A file without a usable frequency and gain says "Couldn't read EQ file" and changes nothing.

The graph is a calculated 44.1 kHz filter response with a labeled decibel scale, not an audio spectrum or a measurement of the output. It shows the tone-shaping response before the separately displayed computed preamp value. The preamp caption is calculated from the displayed reference response. Custom calculates preamp compensation from the combined filter response, including overlapping boosts and cuts. A change is heard after about a third of a second. The equalizer works on music up to 96 kHz (higher rates play unchanged) and not in USB DAC mode. Graphic bands at or above a quarter of the music's sample rate do nothing (the 16 kHz band is idle at 44.1 and 48 kHz).

### Parametric EQ

Settings → Parametric EQ keeps a separate parametric Custom sound. The graphic Custom EQ editor stays available. A parametric sound has up to ten enabled or disabled filters: Peak, Low shelf or High shelf. Select a filter with the volume keys and press ▶ to see Enabled, Type, Frequency, Gain, Q and Remove filter. Select a field with ▶, change it with the volume keys, and press ▶ to keep it; Back cancels that adjustment. Back from the fields returns to the filter list. Add filter creates an enabled, flat 1 kHz Peak filter. Remove filter removes the selected filter.

Frequency covers 20 Hz to 20 kHz in roughly 1/24-octave steps, rounded to whole Hz. Gain covers −12.0 to +12.0 dB in 0.1 dB steps, so imported correction values keep their precision. Q covers 0.30 to 10.00 in 0.01 steps. The selected filter's frequency, gain and Q appear above the response graph in landscape; its fields show those values in portrait. Enabled turns a filter off without removing its settings.

Requested preamp adjusts the global level from −60.0 to +12.0 dB. The separately displayed computed preamp includes automatic headroom and can be lower than the requested value. The graph and its preamp caption use the labeled 44.1 kHz reference; playback recomputes protection for the actual output rate. Headroom covers overlapping boosts, resonant shelves and transient overshoot; it can reduce the level substantially for complex curves. A positive requested value cannot override that protection. Extremely demanding curves can require more attenuation than the fixed-point preamp can represent and become silent. Lower the boosts or disable filters to recover level.

Save to slot stores the parametric sound in any of the same eight slots. A slot holds either a graphic or a parametric sound; saving replaces its previous kind. Settings → Equalizer offers Custom for the graphic sound, Custom (parametric), saved slots, and files. Choosing a parametric slot opens its editor. Music above 96 kHz and USB DAC output remain unchanged. Parametric filters at or above half the actual output rate are inactive; converted speech uses its converted output rate.

### Sound shaping

Open **Settings → Equalizer → Sound shaping** to change the sound with words. Use the volume keys to browse Warmth, Brightness, Bass, Punch, Vocals, Air and Clarity. Press Play to adjust a control, move between −5 and +5 with the volume keys, and press Play again to keep it. Back cancels the current adjustment. The slider, response curve and automatic preamp update as you move. Zero leaves that control neutral.

| Control | Negative end | Positive end | Filter | Frequency | Q | Change per step toward the positive end |
| --- | --- | --- | --- | --- | --- | --- |
| Warmth | Cool | Warm | Peak | 300 Hz | 0.70 | +1 dB: fuller low mids |
| Brightness | Dark | Bright | High shelf | 6 kHz | 0.71 | +1 dB: more treble |
| Bass | Light | Deep | Low shelf | 80 Hz | 0.71 | +1 dB: more low bass |
| Punch | Soft | Punchy | Peak | 90 Hz | 1.40 | +1 dB: a tighter bass emphasis |
| Vocals | Distant | Close | Peak | 2 kHz | 0.70 | +1 dB: more vocal presence |
| Air | Closed | Airy | High shelf | 12 kHz | 0.71 | +1 dB: more upper treble |
| Clarity | Muddy | Clean | Peak | 350 Hz | 0.90 | −1 dB: less low-mid weight |

Negative steps reverse the filter change. These are tone adjustments; Punch changes bass emphasis without changing timing or compression. Headroom protection lowers the overall level as needed for the combined curve, so boosted settings can sound quieter. The graph shows the filter response before that attenuation; its preamp caption shows the reduction.

**Reset shaping** returns all seven controls to zero and keeps the correction underneath. **Save to slot** stores both the sound and the control positions in one of the eight EQ slots. Choosing that slot opens Sound shaping again. The Equalizer row describes the first two nonzero controls in control order, for example **Warm · Bright**; a fully neutral shape reads **Neutral**. Opening Settings → Parametric EQ exposes the resulting filters. Keeping a manual filter edit turns that Custom curve into an ordinary parametric sound; a saved shaping slot keeps its positions.

**On top of** adds shaping to a saved parametric correction. Import an AutoEQ file through Equalizer, save it to a slot, then open Sound shaping and select that slot under On top of. Each nonzero shaping control takes one filter. The correction and shaping together must fit ten filters: a seven-filter correction leaves room for three nonzero controls. A change that would exceed ten is refused with a message and leaves the previous sound intact. Empty and graphic slots are refused. Select No correction for shaping alone.

The saved sound contains a copy of the correction, so changing the source slot later does not change it. On reopening, a copied correction is labelled Parametric EQ. Reset removes only the shaping filters. Leaving an adjustment for Home, another module or USB restores the kept sound. The existing music-rate and USB DAC limits apply.

### AutoEQ parametric files

1. Export **ParametricEQ.txt** from AutoEQ, using at most ten Peak or shelf filters when possible. Copy it into `/EQ` on the active music volume. File names must fit the existing 27-byte limit.
2. Open Settings → Equalizer and highlight the file to preview it. Press ▶ to keep it and open Parametric EQ.
3. Review the filters and computed preamp, then use Save to slot to keep a named slot number. The imported Custom sound is also saved across restarts.

Equalizer APO syntax works directly:

```text
Preamp: -6.2 dB
Filter 1: ON PK Fc 105 Hz Gain -2.3 dB Q 0.70
Filter 2: ON LSC Fc 100 Hz Gain 3.4 dB Q 0.71
Filter 3: ON HS Fc 9000 Hz Gain -1.1 dB
Filter 4: OFF PK Fc 400 Hz Gain 2 dB Q 1.00
```

PK and PEQ mean Peak; LS/LSC and HS/HSC mean Low shelf and High shelf. Names are case insensitive, and CRLF or LF line endings work. Peak needs Q; a shelf without Q uses 0.71, approximately the cookbook's default unity slope. OFF filters are ignored. Exported Squiglink files using this syntax work too; Wavelet GraphicEQ exports continue to use the existing graphic fitter.

If more than ten ON filters are supplied, the ten largest absolute gains are kept and the player reports that reduction. Malformed or out-of-range filter lines reject the whole import, leaving the kept sound unchanged. Files over 16 KiB are refused. GraphicEQ files still fit to the graphic bands; their Preamp lines remain ignored. Parametric imports retain their requested preamp, subject to automatic headroom.

## ReplayGain

Settings → ReplayGain offers Off, Track, Album and Auto. Auto uses album gain for consecutive songs in album order, including album shuffle, and track gain for song shuffle or isolated songs. Track uses each song's prepared gain; Album uses the album gain to keep the level relationships within an album. If one kind of gain is missing, the player uses the other. Songs without gain tags keep their normal level. Opus files also use their R128 track or album gain tags, converted to the ReplayGain reference; standard ReplayGain gain tags take precedence when both are present. Off is the default.

Leave Settings → Prevent clipping on to limit normalization using the file's peak tags. Without peak tags, it retains attenuation and limits boosts to unity. Turning it off allows the full tagged gain. Changes apply to newly buffered audio while the current and next songs keep playing.

The player reads existing ReplayGain tags. Rescan the library, or rebuild it with the desktop indexer, to add gain data to an older library. Your music files are unchanged.

## Transitions

Settings → Transitions offers Off and Fade. With Fade, while shuffle is on, the end of each song fades out and the next one fades in. Settings → Transition length, shown while Transitions is on, sets each fade from 0.5 to 6 seconds; 1.5 seconds is the default, and a fade never takes more than a third of either song. Songs that follow each other on an album, in album order, still play gapless, and so do CUE tracks and chapters of one file, audiobooks and podcasts, and songs shorter than 2 seconds. Skipping, choosing a song or seeking starts it straight away, without a long fade. Without shuffle, songs play as before. Transitions don't apply to USB DAC mode.

Fade plays one song after the other. A crossfade, where the next song starts while the current one fades out, would need two decoders running at once; that doesn't fit in the player's memory.

## Playback speed

Playback speed plays faster or slower without changing the pitch: 0.5×, 0.75×, Normal (1×), 1.25×, 1.5×, 1.75×, 2×, 2.5× or 3×. On Now Playing, tap Menu to open Quick Settings and press Play on Playback speed to step through the speeds; it is also in Settings, below Status bar and above Rewind on resume. The speed applies at once and is saved for the next start. While it isn't Normal, Now Playing shows it under the track. Track times, seeking, bookmarks and resume positions keep using the recording's own time. Listening history counts the time you actually listened.

Recordings above 48 kHz always play at normal speed.

## Bluetooth headphones

Settings → Bluetooth shows whether Bluetooth is on and which headphones are connected. Open it to turn Bluetooth on or off, connect, switch or pair headphones. The screen lists your headphones (the player remembers up to six, most recent first) with their state, a **Pair new device** row, and a hint for the selected row at the bottom.

**Pair new headphones:**

1. Put the headphones in pairing mode.
2. Select **Pair new device** and press ▶. The player searches and lists what it finds below that row.
3. Select your headphones and press ▶. The row says "Connecting…", then the player shows "Connected to …".

**Every day:** with Bluetooth on, the player connects to your most recent headphones by itself when it starts and whenever they come back in range. If a connection stalls, it resets and tries again within about 15 seconds. To switch to other remembered headphones, select them and press ▶. To disconnect, select the connected headphones and press ▶: they stay disconnected until you press ▶ on them again or restart the player. To forget headphones, hold ▶ on them.

Music plays through the headphones whenever nothing is plugged into either jack.

Bluetooth has its own volume, separate from the wired one. While Bluetooth headphones are connected and nothing is plugged into a jack, the volume keys and the volume on screen are the Bluetooth volume; otherwise they're the wired volume. Each stays where you left it, also after a restart. The Bluetooth volume starts at 50, and its top step, 60, is as loud as FiiO's firmware goes over Bluetooth; headphone profiles don't limit or trim it. To switch to wired headphones, pause, plug them in and press play: while music is going to Bluetooth the player can't see the jack. After you unplug, the next play goes back to Bluetooth.

The headphones' controls work: pause, play and next/previous track, and taking them out of your ears pauses if they have in-ear detection. If they disconnect, for example when you walk out of range, playback pauses. When they come back, the player reconnects but stays paused: press ▶ to carry on.

With nothing connected for 5 minutes, Bluetooth switches itself off to save battery. You don't need to turn it back on: if music last played through Bluetooth, pressing ▶ wakes Bluetooth and shows "Connecting to …" with your most recent headphones' name; music starts as soon as they connect. The same happens when you turn the player on and press ▶ before your headphones have reconnected. If they don't connect within 10 seconds, the player stays paused and says so; press ▶ to try again, or plug headphones into a jack and the music plays there. Nothing plays out loud while it waits. The Bluetooth screen shows "Off to save battery · Play: reconnect" while Bluetooth is switched off this way; press ▶ there to reconnect.

If a speaker or headphones stay silent when they connect while music is playing, the player starts their stream again once by itself; you shouldn't need to press play on the speaker. A small Bluetooth mark next to the play icon at the top shows Bluetooth is on (dim) or connected (bright).

## Audiobooks and podcasts

Put audiobooks in a folder called `Audiobooks` and podcasts in a folder called `Podcasts`, at the top of the card (any capitalisation; `Audiobook` and `Podcast` work too). Music then shows **Audiobooks** and **Podcasts** after the library views; they stay hidden while those folders don't exist. You can also show them on Home through Settings → Home screen. Books and episodes stay out of Music (Cover Flow, Albums, Artists, Genres, Songs and shuffle), so they never turn up between your albums.

```
Audiobooks/
  The Long Road/          one folder per book
    01 - Departure.mp3    chapters play in file-name order
    02 - The River.mp3
  Short Story.mp3         a book in one file works too
  Dune.m4b                a book in one chaptered .m4b works too
Podcasts/
  Tech Talk/              one folder per show
    2026-10-01 - Episode 41.mp3
```

- **Audiobooks** lists your books, each with "3 chapters", "Chapter 2 of 12" or "Finished". ▶ on a book carries on where you stopped (or starts a finished book again). Hold ▶ on a book to see its chapters.
- **`.m4b` audiobooks** are indexed like `.m4a` files (AAC-LC or Apple Lossless). When the file carries chapters, it shows up as a book of its own named after the file (`Dune`), with one chapter per marker and the chapter's title as the song title; "Chapter 3 of 41", the chapter list, ▶ to resume, bookmarks and the 30 s / 15 s jumps all work on the chapter, and playing on from one chapter into the next is gapless. Both chapter kinds are read: the Nero `chpl` list and the QuickTime chapter track (what Libation, Audible conversions and ffmpeg write). A file with no chapters, one chapter, or chapter data that can't be read is one track. A book has at most 255 chapters; the rest are part of the last one. Put the `.m4b` straight into `Audiobooks/` (not inside a folder of its own), because a folder is a book by itself. Outside `Audiobooks/` a chaptered `.m4b` is an album of its chapters, like a CUE album, and its row in Folders is a folder named after the file.
- **Podcasts** lists your shows; open one to see its episodes, each with "23:10 left" or "Played".
- A chaptered `.m4b` remembers one place for the whole file, keyed by the file's path: the chapter and the position inside it. ▶ on the book resumes that chapter; choosing another chapter from the list starts it at its top, and the place moves with whatever is played. Renaming or moving the file forgets the place.
- Every book and every episode remembers its own place, even when you play music in between or turn the player off. The places are saved in `/.gnarlyr/bookmarks.bin` (the last 128 books and episodes).
- Books and shows always play in order: shuffle doesn't apply to them, and your shuffle setting for music stays as it was.
- On Now Playing, holding Vol+ jumps 30 s forward and holding Vol− jumps 15 s back. Jumping past the end of a chapter goes to the next one. To pick another chapter, use the book's chapter list.
- An episode or book counts as finished once it played to its last tenth (at most its last 30 s).
- These folders still appear under Music (Albums, Artists, Folders and so on), as all tracks on the card do. Give them a genre such as "Audiobook" if you want to find them there.
- **Skip silence** (Settings → Skip silence) shortens long pauses in books and episodes. A pause that lasts more than a quarter of a second is cut to a quarter of a second, with a short fade, so speech carries on sooner. Pauses shorter than that play as recorded. Music isn't affected. Positions, bookmarks, chapters and the time shown on Now Playing stay in the recording's own time, so a book resumes exactly where it was even after skipped pauses.
- **Speech boost** (Settings → Speech boost) evens out the volume of books and episodes: quiet voices are raised by up to 12 dB and loud passages are brought down, so you don't have to keep changing the volume. Room noise below the voices isn't raised, and the output never goes above full scale. Music isn't affected.
- Change the speed for spoken word on Now Playing: tap Menu, then press Play on Playback speed in Quick Settings (see Playback speed). Positions, bookmarks and chapters still refer to the original recording.
- **HE-AAC doesn't play.** An `.m4b` or `.m4a` that isn't AAC-LC or Apple Lossless (HE-AAC with SBR or PS, xHE-AAC) is refused with "Only AAC-LC and ALAC M4A play". Libation can download AAC-LC: in Libation's settings, leave "Request xHE-AAC Codec" off (Libation's FAQ says that makes it download AAC-LC); it keeps whatever codec Audible delivers and doesn't re-encode, so the files may still be HE-AAC for some titles. Libation names every non-MP3 file `.m4b`, whatever its codec.

## Updating Gnarlyr

An install through the updater has only run in the emulator so far ([`known-issues.md`](known-issues.md)). Charge the battery first.

1. Plug the player into the computer and choose **Connect to computer** (or Home → USB mode → Connect to computer). Copy the new `GNARLYR.IMG` to the top level of internal storage, not into a folder.
2. Eject both drives. The player shows "Checking GNARLYR.IMG", then "Installing update" with a percentage. Don't unplug or turn it off.
3. At "Update installed", unplug USB and hold Power. The player turns off.
4. Hold Power to start the new version.
5. Connect the player to the computer once more, copy any small file onto internal storage (or delete one), and eject both drives. The player shows "Saving this version as the backup" while it copies; from then on it keeps the new version. It only does this when the computer could write to internal storage during that connection, so a version whose drives open read-only stays on trial.

The new version starts on trial: the previous version stays on the player as a backup until step 5. If the new version starts without its text, or doesn't start properly three times in a row (a start counts unless the player then runs for 20 seconds), or its USB connection fails, the player puts the previous version back and says "Update rolled back". If the player hangs while it is connected, hold Power for 10 seconds to restart it.

The updater deletes the file after a successful install, when it is not a valid image, and when that version is already installed.

## Going back to FiiO's firmware

There are two ways. This project has done the second on its player; the first has not run on a player with Gnarlyr.

**Through the updater**

1. Download FiiO's firmware for the Echo from FiiO's website and unzip it. The image is `ECHOV200.IMG` for V2.0.0.
2. Delete any `GNARLYR.IMG` from internal storage if it's still there.
3. Copy FiiO's `ECHOV200.IMG` to the top level of internal storage, unchanged. It is a 39,845,892-byte file.
4. Eject both drives. The player checks the file (this takes a while) and shows "Installing FiiO firmware", then "FiiO firmware installed".
5. Unplug, take the microSD card out, hold Power to turn the player off, and switch it on. Podium's documentation says FiiO's firmware then installs the same file once more at its first start.

**Through maskrom**

Follow [`device-recovery.md`](device-recovery.md). It writes FiiO's image into both firmware slots from a computer.

## If something goes wrong

- **"isn't a valid image for this player":** for an update, rebuild the image and copy it again. For going back to FiiO, check that the file is FiiO's Echo image (`ECHOV200.IMG`), unchanged (its hash matches the one in FiiO's zip). Gnarlyr only installs its own images and FiiO's; to switch to another custom firmware, go back to FiiO's first.
- **"Update failed":** copy the file again and retry before turning the player off.
- **"USB missing: reinstall update" or "Restart to finish the update":** see Known trade-offs in [`known-issues.md`](known-issues.md).
- **"Update rolled back":** the new version didn't start properly, and the player went back to the version it had before. Report it with the Firmware line from Settings → About.
- **The player doesn't start at all:** the chip's maskrom mode writes FiiO's firmware back from a computer over USB. It needs a patched `rkdeveloptool` and some care; the steps are in [`device-recovery.md`](device-recovery.md). If the player still runs and you expect trouble, create the file `MASKROM.REQ` on internal storage beforehand, or, with the cable plugged into the computer, hold Power and vol+ together for 5 seconds (same document).
- **Noting a problem:** write down what you did, the FiiO version the player had before, and the Firmware line from Settings → About.

## Themes and controls

Settings → Theme offers Neon Noir, Graphite, Sunset, Sea Glass and Classic, followed by installed theme packs. Move with the volume buttons to preview a theme's colours and background. Press Play to keep it; Back restores the saved theme. Theme changes keep your text size, orientation and navigation.

To install a pack, create `.gnarlyr/themes` on the SD card, copy its `.gtheme` file there and eject normally. Without a card, use the same folder on internal storage. The list shows the pack's name and author. Damaged packs stay visible but cannot be applied. A low-contrast warning means the palette may make text hard to read. Verified, when present, identifies a signature from a trusted gallery key; unsigned packs work normally too.

Theme selection survives a restart and firmware updates that support the same pack format. An absent or damaged active pack uses the built-in default, so you can always choose another theme. To update a pack, replace its file with the newer version using the same pack ID. You can return to a built-in at any time, then remove unwanted pack files on a computer.

To make your own, start with the original designs in [`examples/themes`](../../examples/themes), edit the manifest and PNGs, and build with `python3 tools/theme_pack.py FOLDER --output FILE.gtheme`. Check a download with `python3 tools/theme_pack.py --check FILE.gtheme`. The [community format guide](../../docs/theme-format.md) explains dimensions, palette roles, licences and optional signing. Packs contain only data and never require a firmware reflash.

 Settings → Controls assigns tap and hold actions independently for Volume+, Volume−, Play and Menu during playback. Default keeps the normal skip gesture; Off ignores that action. Reset buttons restores the normal controls. List navigation stays consistent.

## Waiting

When something takes more than a moment, its screen says what is happening and a small wireframe turns under the message: a cassette while saving (Export plays, mixtapes, tags), a disc while working through your music (library scan, finding covers, search, preparing a queue, listening history) and a solid while loading tags. Where the player knows how far it has got, the cassette's tape winds from the left reel to the right, the disc's rim fills in, or the solid's edges light up. Only the wireframe is redrawn, and it stops as soon as the work ends or the screen turns off.
## Customising Home

Settings → Home screen lists the nine editable Home items in their saved order, each marked Shown or Hidden; Settings is marked Always shown and stays on Home. Now playing appears automatically at the top when a track is loaded and has no editor row. Press Play on an item for **Hide from Home** (or **Show on Home**) and **Move**. Move picks the item up, and so does holding Play on it: the title reads Move: vol ±, the volume buttons move the item up and down the list, Play places it and Back puts it back where it was. **Reset to default** restores Music, Mixtapes, Stats, USB Mode, Clock and Settings, with Audiobooks, Podcasts and Up Next hidden. Each change is saved at once and Home follows it straight away, keeping your place on the item you had selected. A shown Up Next needs a loaded track, and shown Audiobooks and Podcasts need their shelves.

An existing settings file adopts this default if its saved Home order and hidden mask still match the old built-in default: items in their original order with nothing hidden. A customised order or hidden mask is kept; Now playing uses the automatic top row in either case.

## Editing a queue

Open Up Next and hold Play on a song for its actions. Move it earlier or later, play it next, or remove it. The playing song offers Edit full queue and Keep current. Clear added removes manually queued songs while keeping the underlying list. Back cancels unfinished queue preparation. The exact edited order and playback position are saved together.

## Mixtapes

Home → Mixtapes saves a queue as a named list. Save queue captures the current sequence; New mixtape starts an empty one. While naming, the volume buttons choose a character, Play adds it, Menu erases it and holding Play saves the name. A mixtape's actions play it, load it into Up Next for editing, replace its contents, rename it or delete it. Songs are matched by their paths and cue boundaries after a rescan.

## Side A/B tapes

Home → Mixtapes → New two-sided tape makes a C60 or C90 cassette: 30 or 45 minutes per side, with up to 64 songs on each side. Choose a length and name it with the usual naming controls.

Hold Play on a song in a Music list, choose **Add to tape**, choose your cassette and then **Side A** or **Side B**. Songs join the end of that side. Add at least one song to each side before playing. A side that is full refuses another song and keeps what you already saved.

Open the tape for **Resume tape**, **Play Side A** or **Play Side B**. It plays in the saved order. Side A stops at its end and shows a cassette and offers to flip to Side B; press Play to flip, or Back to stay stopped. Side B stops at its end too; Play rewinds the tape to Side A. Repeat does not cross a side boundary. The player remembers the side, song and exact position across restarts, including a side waiting to be flipped.

Rename and Delete work as for other mixtapes. To play a different queue, select another song or mixtape. Ordinary single-sided mixtapes keep their existing actions.

## Status bar

Settings → Status bar chooses **Show volume**, **Show repeat**, **Queue position**, **Battery percent** and **Battery style**. Volume, repeat, position and percent start off; the clock appears when the time is known. Solid is the default battery style. Choices are kept across restarts.

Volume shows a speaker and the player's own 0–60 level, changing as you adjust it. Repeat shows the repeat arrows for All, with a small song badge for One, and is hidden for Off. Queue position shows the current place and total, such as **3/12**, while a queue is loaded. It is centred in portrait; landscape shows it when the screen title is hidden.

Segmented battery uses five green bars, one for each started fifth of the charge. At 15% or less its bars turn red; at 5% or less its outline turns red too. Both styles show the same charging bolt on USB power.

When space is tight, the battery, existing playback/link/lock/shuffle icons and the screen title keep their places. The remaining items get space in this order: volume, queue position, clock, repeat, battery percent. A smaller item may fit when a wider one cannot. Text size keeps the status icons and small numbers at the same size.

## Clock and calendar

Settings → Date & time, beside Clock format, shows the current date and time, or Not set. Open it to edit the year (2024–2099), month, day, hour, minute and UTC offset. Vol+ and Vol− change the focused value; hold to repeat. Menu moves to the next field, holding Menu moves to the previous one, ▶ saves and Back cancels. The day follows the month length, including leap years. The hour follows Clock format, with AM/PM in 12-hour time. Saving sets seconds to zero and updates the status bar, Clock and Calendar at once. If saving fails, the editor stays open and the running clock keeps its previous time.

Enter your local date and time. UTC offset is the difference between your local time and UTC, for example −08:00 for Pacific standard time and −07:00 for Pacific daylight time. It changes in 15-minute steps, from −12:00 to +14:00. The offset from your last Settings save or Mac sync is kept across restarts; a player with no saved offset starts at +00:00. Change the offset yourself when daylight saving time changes. The clock chip and listening timestamps use UTC.

Your Mac can also set the clock. Every companion command that ejects the player (`python3 tools/echo_debug.py set-time` does only that) leaves the Mac's time and offset on the drive, and the player applies them as the drive is ejected. A later Mac sync replaces a time set in Settings; a later successful Settings save replaces the last sync. The clock then keeps going while the player is on. Saving on the player keeps UTC in the clock chip and the offset in Settings. After switching off or restarting, Gnarlyr reads the chip once, a couple of seconds after startup, and restores local time with the saved offset. If audio is already active, recovery waits until it stops. If the chip cannot supply a valid, trusted date and time, the clock stays Not set; set it in Settings or sync with your Mac.

The set time also shows in the status bar, left of the battery; in landscape it steps aside for a long screen title. Settings → Clock format chooses 12-hour time with AM/PM or 24-hour time (the default). The status bar follows the same choice, including in Calendar. Home → Clock shows the time, the weekday and the date. Press ▶ for Calendar: the current month from Monday with today marked. The volume buttons move a month at a time, ▶ goes back to the current month and Back returns to Clock. Without a set time both say so and how to set it.

## Saved moments

Open Home → Stats → Moments while listening and choose **Save moment**. Each row shows the song title and saved time, newest first. Select one to play from that exact place. Hold Play on a saved row and choose Delete to remove it, or Keep moment to cancel. You can keep 64 moments; delete one when the list is full.

Moments survive restarts and library rescans. They follow the song's path and exact CUE or chapter boundaries. If a file is moved or its chapter boundaries change, its moment stays in the list and says the song or chapter is unavailable until that identity returns.

## Listening and lyrics

Home → Stats shows total listening time, plays, skips, and top songs, albums and artists. It records time while music is playing. Totals and ranked lists work without a calendar clock.

Stats → Recap sums up a period as a short run of cards: choose **This month**, **Last month**, **This year** or **All time**, then turn the cards with the volume buttons (Play also goes forward, Back returns to the periods). The cards show the time listened with plays, skips and artists; your top five artists, albums (with their covers) and songs; your favorite day of the week and hour, your longest run of days with music and how many artists were new in the period; the split between wired, Bluetooth and USB DAC listening and between lossy, lossless and hi-res files; and a closing card of highlights. Months and years follow the clock and its UTC offset, and each play counts on the date it was heard. Without a set clock only All time opens, without days and hours. A period without plays says so. The first opening after new plays works through the history while a disc turns; the result is kept in `/.gnarlyr/recap.bin`, so the same period opens again without that wait until something new is played.

Export plays saves your plays to `/.scrobbler.log` at the top of the player's drive, in the Rockbox scrobbler format that import tools read; your history stays as it is. It starts as soon as you choose it and shows how far it has got; Back cancels it. When it finishes it shows how many plays it saved. Each export replaces the previous file. With nothing to export it says so and leaves the file alone. Plays of songs no longer in the library are left out. Times are saved in UTC. A new play gets its time from the current clock, whether set in Settings or by your Mac (see Clock and calendar). Changing the clock leaves earlier plays and the start time of a play already in progress unchanged; a play made while the clock was not set is saved with the time 0.

To start fresh, hold ▶ on Listening and choose **Clear history**. The confirmation lists what resets: listening totals, top songs, albums and artists, and the history used by Smart Lists and Export plays. **Moments and Favorites are kept.** Choose **Export plays first** to save the current plays to `/.scrobbler.log` before clearing, **Clear** to clear immediately, or **Cancel** to keep the history. Cancel is selected first. Back also leaves the confirmation without clearing.

Clearing resets Most Played and Recently Played; Never Played and Old Favorites rebuild from the empty history. Recently Added still uses library updates. An existing exported file stays on the drive; Export plays first replaces it in the usual way. A failed or cancelled export keeps the history. A scan finishes before clearing becomes available. The reset is saved so an interrupted write recovers either the previous history or the empty one. Music played after the reset starts a new history.

Place a timed `.lrc` file beside a song, with the same filename stem. Hold Menu on Now Playing to open Lyrics; tap Menu opens Quick Settings, which also has Lyrics and Up Next. Lyrics follow playback, and the volume buttons move through the lines. Back returns to the screen you opened Lyrics from.

### Power shortcuts and Menu hold

Hold Power first, then press Up for the next track or Down for the previous track, including while the keys are locked. Release both keys before another shortcut. These shortcuts consume the Power gesture; holding Power alone retains its usual power-off action. Hold Play and Power to toggle the key lock. Developer builds also capture the screen to the debug folder with Power+Menu (`shot0.565`, replacing the previous capture).

Settings → Controls → Hold Menu chooses Default, Quick Settings, Up Next, Add to tape, Shuffle, or Sleep timer. Default keeps the existing screen and playback button mappings. Shuffle toggles between off and songs; Sleep timer opens its duration choices. Text entry keeps its own gestures. Choose Default to restore each screen's Menu hold.

### AIFF files

AIFF (`.aiff`, `.aif`) and uncompressed AIFF-C (`.aifc`, `NONE` or `sowt`) accept
mono or stereo 16/24-bit music at standard rates from 8 to 192 kHz. Seeking and
queued transitions follow the WAV rules. ID3 tags and embedded album art appear
in the library. Compressed AIFF-C and floating point files cannot play.

### DSD files

DSF and uncompressed DFF play as 88.2 kHz PCM. Mono and stereo DSD64/DSD128 and mono
DSD256 are accepted. Stereo DSD256, multichannel DSD and DST-compressed
DFF are refused. DoP output is unavailable. ID3 tags and artwork appear in the
library; DFF's DIIN artist/title fill missing ID3 fields. The output uses the
usual seeking and queue controls.
### Compact status bar

Settings → Status bar → Status bar selects Standard or Compact. Compact uses smaller icons,
a combined battery percentage and USB bolt, and tighter list spacing. Lists use the reclaimed
space; the number of extra rows depends on text size, orientation and row details. Battery
segments still follow Battery style. Compact always includes the percentage in the battery.
Playback, Bluetooth, lock, shuffle and battery keep their places. Optional items fit in this
priority order: volume, queue position, clock, repeat. A visible title takes priority over queue
position. Standard retains its separate optional percentage.

The charging screen uses the theme's battery wireframe, percentage and power state. Its pulse
advances once a second while charging and stops for other power states. A gauge trend estimate
appears after at least five minutes and several consistent windows: percent per hour, then an
approximate time to full. Both update at most once a minute. Chevrons indicate a slower, medium or faster estimated gain. A flat, noisy,
interrupted or newly connected session shows Measuring… instead. The estimate uses a sliding
window of about ten minutes, not a current sensor; tapering near full can change the time.
Computer means the existing USB connection code saw a host response; Charger means USB power
is present without one. This does not initiate USB enumeration. The app continues sampling
with the display dark; switching away from the charging module can restart measurement.
No mA value is shown because the gauge does not measure current.

### Monkey's Audio files

APE files accept 16/24-bit mono or stereo. Fast accepts up to 192 kHz. Normal accepts up to 192 kHz mono or 96 kHz stereo. High accepts up to 96 kHz mono or 48 kHz stereo. Extra High and Insane compression are refused. APEv2 tags and embedded covers appear in the library.

### WMA files

Use WMA Standard v1/v2, mono or stereo, at 8–48 kHz. ASF tags and `WM/Picture` covers are read with the library. WMA Pro, Lossless, Voice and encrypted files are refused. WMA recordings without encoder padding information can retain a gap between songs.

## Sleep timer

Settings → Sleep timer turns the player off after 15–120 minutes, at End of track, or at End of chapter/episode. Chapter/episode uses the current audiobook chapter, podcast episode or CUE track; on ordinary music it ends at the current song. The final minute fades smoothly to silence. If an end timer is armed with less time remaining, its fade starts at the current level and uses that shorter interval. A paused end timer waits for playback to finish; a timed timer continues while paused. Off cancels the timer and restores the normal listening level. A new start leaves the timer off.

## A–B repeat

On Now Playing, hold Play to open seeking, then press Menu to mark A at the sound currently playing. Repeat those two presses later to mark B: the player repeats A through the sample before B. Do it a third time to clear the loop. “A set, B next”, “A-B repeat” and “A-B off” show the result. The seek cursor also shows the Menu hint. Moving the seek cursor does not move an A/B mark; marks use the playing position when Menu is released.

Marks use source samples, including inside CUE tracks and audiobook chapters. A B mark must follow A. A pause keeps the loop and its output ready; seeking outside a loop returns to A. Changing tracks, handing audio to USB or restarting clears it. End-of-track/chapter sleep clears a loop so the track can finish. Timed sleep still fades and switches off during a loop. MP3 seeks retain the decoder’s existing approximate positioning.

## Run from USB power

Choose **Run from USB power** when you plug in, in USB mode, or as Settings → When plugged in. The cable keeps the player running while battery charging is off, so a player that lives on a cable doesn't sit at full charge. It lasts until you unplug, unless it is your saved choice. Unknown readings or a voltage below 3.6 V turn charging back on, and it stays on until the reading reaches 3.8 V; meanwhile the chooser says "Battery low: charges first" and the charge screen says "Charging". While charging is off, the status bar and the charge screen show a plug and the charge screen says "Charging off". Unplugging turns both USB supply controls off.

There is no 80% limit: on USB power the ADC measures the supply rail rather than the cell alone, so that reading cannot reliably enforce a cell percentage. The percentage shown on USB power can therefore differ from the battery level. Run from USB power needs a charger capable of powering the player; verify it with the intended charger before relying on it for long listening sessions.
