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

On the first start with a microSD card in the player, Gnarlyr builds its library and album covers. Wait for it to finish before you plug the player into a computer.

## Your music

- Put music on a microSD card formatted FAT32 or exFAT, in any folder layout. With no card in the player (or one it can't read), it uses internal storage instead. Settings → About → Music shows which one is in use. Settings are kept on internal storage either way, so they stay the same when you insert or remove the card.
- Gnarlyr plays FLAC, MP3 and WAV, up to 192 kHz, and `.m4a` and `.m4b` files with AAC-LC or Apple Lossless audio. Speech recordings at 8, 11.025, 12, 16, 22.05 and 24 kHz are converted to 32, 44.1 or 48 kHz for playback; 32 kHz plays at its original rate. Speed and pitch stay the same. It lists the other formats FiiO lists, and says when it can't play one.
- Ogg Vorbis (`.ogg`) accepts mono or stereo at 8–192 kHz within the decoder limits; Ogg Opus (`.opus`) accepts mono or stereo with channel mapping family 0. Chained Vorbis files play the first stream only. Large picture comments and separate interleaved streams are skipped during decoding. Both formats play in the desktop simulator; player listening and high-rate playback still need checking. See [format limits](ogg-vorbis.md).
- Music → Folders shows the card as it is: folders, then each audio file by its full name with its extension (`01 Intro.mp3`). The other lists show titles from the tags. Hold ▶ on a folder there to play everything under it, subfolders included, in the order Folders shows it (a CUE album plays once, as its songs); the queue follows the shuffle setting and has no length limit beyond the library's 65,535 tracks.
- A `.cue` file next to a single FLAC, WAV or MP3 album image splits it into one track per CUE track (see "CUE sheets" below). Album covers come from art inside each album's first track, or from a `cover`, `folder`, `front` or `albumart` JPEG or PNG next to the tracks.
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

Cards through 32 GB use FAT32; larger cards use exFAT. The volume is named ECHO. Internal storage keeps its files. This is a quick format: old file contents can remain recoverable until new files overwrite them.

An interrupted format reports that the card needs formatting. Reinsert the card and choose Format card again before copying music. Physical card formatting still needs the owner checks listed in [known issues](known-issues.md).

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

- The sheet must name one file. The player looks for that name in the sheet's folder (capitals don't matter), and when there is none, for an audio file with the same name as the sheet (`Violator.cue` and `Violator.flac`), which is what many rips need. Sheets that name an APE, M4A or another format the player can't play, sheets that list several files (one per track) and sheets whose file is missing are ignored; their audio files list as before.
- Track titles come from `TITLE`, artists from `PERFORMER` (the track's, otherwise the one at the top of the sheet, which is also the album artist), the album from the top `TITLE`, the genre from `REM GENRE` and the year from `REM DATE`. What the sheet doesn't say comes from the audio file's tags, then from its folder name. A track without a title is called "Track 3".
- A track starts at its `INDEX 01` and ends where the next one starts, so silence marked `INDEX 00` before a track belongs to the track before it. Tracks play without a gap.
- The sheet is read as UTF-8 (a byte order mark is fine) or, when it isn't valid UTF-8, as Windows-1252.
- In Folders the album folder shows the sheet (`Violator.cue`) and the audio file (`Violator.flac`), as they are on the card. The audio file shows the length of the whole album. The sheet opens its songs by title with their own lengths, and choosing one plays from there; choosing the audio file plays the album from its first song.
- Time, progress, seeking, resume, Up Next, shuffle, favorites and bookmarks all work on the CUE track, not on the whole file. Covers come from the audio file's art or a `cover.jpg` next to it, as for other albums.
- Rescan the library (Settings → Rescan library) or run echo-sync again after adding or changing a sheet.

## Copying music over USB

The USB screen shows the connection mode and charging state. While connecting, or if charging artwork is unavailable, a plain text screen keeps the state and instructions visible.

1. Connect the player and choose **File transfer** when it asks (see [When you plug in](#when-you-plug-in)). Two drives appear: internal storage and the SD card.
2. Copy music to the SD card, or to internal storage when no card is in the player.
3. **Eject both drives before you unplug.** If you unplug first, recent changes may be lost, and the player warns you ("Unplugged too soon").
4. When music changed, the player rebuilds its library by itself. Copying other files doesn't trigger a rebuild. Settings → Rescan library rebuilds it on demand.

## When you plug in

The player charges whenever a cable is in. Plugged into a computer, it doesn't connect by itself: it shows **USB connected** with **Charge only**, **File transfer** and **USB DAC**, and nothing appears on the computer until you choose. Charge only is highlighted, and its row counts down from 15 seconds; pressing any button starts the count again. Back, the end of the count, or the screen going dark all mean Charge only: music and menus carry on, and the player stays charging. The player asks once per plug-in; unplug and plug in again to be asked again, or open **USB mode** from Home to connect later without unplugging. A wall charger never brings up the question.

On the charge screen (the player switched off on a cable) a computer wakes the screen with the same question. Charge only, Back or the countdown return to the dark charge screen; File transfer and USB DAC connect, and the charge screen comes back when you're done.

Settings → When plugged in changes this: **Ask** (the default), **File transfer** (connect at once, as earlier builds did) or **Charge only** (never connect; the player only charges).

## Switching USB modes

Open **USB mode** from Home or Settings, or press Menu on the charging screen. Choose **Charge only**, **File transfer** or **USB DAC**. The player keeps charging while you use it, and you can change modes with the cable connected. Charge only ends a connection and leaves the player to charge; from the charging screen, leaving the selector in any way returns to the charging screen.

Before leaving File transfer, eject both drives on the computer and wait for the mode selector. The player keeps the track, position and queue for your return. From USB DAC, hold Back for a second and release it to return to the selector.

## Using the player as a USB DAC

**Not run on a player by this project** (see [`known-issues.md`](known-issues.md)). In this mode the player is a sound card for your computer or phone: their audio plays through the player's headphone outputs.

1. Plug in the USB cable and choose **USB DAC** when the player asks (or, with the cable in, Home → USB mode → USB DAC).
2. The player shows "USB DAC". It is a standard USB Audio Class 1.0 device, which Windows 10 and later, macOS, Linux, Android and iOS support without a driver (phones need a USB-C or Lightning adapter that passes data).
3. On the computer, choose Gnarlyr as the sound output (Windows: the speaker icon in the taskbar; macOS: System Settings → Sound → Output). The player shows the format and its volume, for example "48 kHz 24-bit · Volume 35".
4. Use the computer's volume, the player's volume keys, or both. DAC mode has its own volume, separate from music: it starts at 35 of 60, the screen shows it ("48 kHz 24-bit · Volume 35"), holding Vol± changes it quickly, and the player remembers it for next time. Start low.
5. Hold Back for a second and let go to open the mode selector, then choose Charge only or File transfer. The player keeps charging.

The player accepts 16 or 24-bit at 44.1 and 48 kHz; the computer picks one (on Windows: Sound settings → Gnarlyr → Properties → Advanced). Music on the player stops while it's a DAC. Choose File transfer from the mode selector to copy music without removing the cable. A charger works the same in both modes.

## Build the library on a computer

Rebuilding on the player takes a while, most of it decoding album covers. A computer can build the same library and album covers instead, straight onto the card, and tell the player that the card has already been scanned.

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

The computer picks each album's cover the way the player does: the picture inside the album's first track (FLAC, MP3, WAV, DSF/DFF, `.m4a`, `.ape` and `.wma`; a front cover wins), otherwise a `cover`, `folder`, `front` or `albumart` JPEG or PNG next to the tracks. Ogg pictures are skipped, as on the player. The covers can look a little different from the player's own, because the computer resizes them with a different filter. When the player rescans later (for example after you copy a new album to it), it keeps the computer's covers and only decodes art that's new or changed; on a card that isn't FAT32 or exFAT, albums whose cover is an image file are decoded again.

## Buttons

| Button | In lists | On Now Playing |
|---|---|---|
| Volume up / down | Next / previous item; hold to scroll faster | Volume; hold Vol+ for the next track, Vol− for the previous one (in audiobooks and podcasts: 30 s forward, 15 s back) |
| Play | Select; a song plays the list from that song. Hold on an album for Favorites, hold on a song for Play Next / Add to Up Next / Add to Favorite Playlist, hold on a folder in Folders to play everything under it, subfolders included | Play / pause; hold to seek |
| Menu | | Opens Up Next (also on Home, below Now Playing) |
| Back | Back; hold to go home | Back; hold to go home |
| Power | Screen off or on; hold 2 s to turn off | Same |

The Now Playing column above is for Settings → Skip songs = Hold vol±; the other two choices move skipping to a double-click of Vol± or a double/triple click of ▶ (see Skip songs).

**Wired headphones with buttons** (3.5 mm jack, from any screen, screen on or off): the middle button pauses and resumes, press it twice for the next track and three times for the previous one; + and − change the volume, hold + for the next track and − for the previous one. Not tried on a player by this project: see [`known-issues.md`](known-issues.md).

With the screen off, ▶‖ still pauses and resumes, a tap of vol± changes the volume and holding vol± skips tracks (or jumps 30 s / 15 s in audiobooks and podcasts).

After 30 seconds paused, the player switches its sound output off to save battery. Play then takes a moment longer to start and fades in from where you paused; Bluetooth headphones stay connected.

In Cover Flow (Music → Cover Flow), the tile before the first album, with a shuffle mark for a cover, is **Random album**: press ▶ on it to play a random album from its first track. Hold a volume button to scroll faster; keep holding to jump between artist initials. A temporary letter badge shows the current section. Press ▶ to open the selected album, or hold ▶ to favorite it. An album without artwork gets a cover made from its initials, or its artist's when the title has none, in colours from the current theme.

## Now Playing style

Settings → Now Playing style offers **Cover** and **Cassette**. Cover is the default. Cassette puts the title, artist and album on a tape label in your theme's colours. Long labels end with an ellipsis. The left reel empties and the right reel fills as the track advances; the hubs turn during playback and stop when paused or the screen is off. The choice stays after a restart.

Both styles use the same time, progress bar and playback buttons. Hold Play to seek, use the volume buttons to move the target, then press Play to keep it or Back to cancel. Menu opens Up Next and holding Menu opens Lyrics, with the default button assignments.

## Charging

The player charges whenever it's plugged in, on or off. Turning it off while it's plugged in (or plugging a charger into a switched-off player) shows the charge screen: a large battery in your theme's colours with the percentage beneath or beside it. While the player charges, the empty part of the battery gently pulses. "Charging" means the charger is active; "Charged" means a known full battery. "USB power" means the cable supplies power while charging is inactive. Before a battery reading is available, the screen shows "Reading battery". The screen goes dark after 5 seconds; press any button to see it again. Hold Power for 2 seconds to turn the player on, or unplug it to switch it off completely. Press Menu to open the USB mode selector from the charge screen. Plugging into a computer wakes the screen and asks what the connection is for (see [When you plug in](#when-you-plug-in)).

## Button lock

Hold ▶, then press Power while still holding it, and keep both down for about a second to lock the buttons, for example before the player goes into a pocket; do the same to unlock. While locked, a padlock shows in the top bar and every button except Power is ignored; Power still turns the screen on and off. Pressing another button shows "Hold Play+Power" if the screen is on. Locking or unlocking lights the screen to confirm. Wired headset buttons keep working. Restarting the player unlocks it.

The top bar shows, from the left: play or pause, Bluetooth, the padlock, and the shuffle mode (a note with crossed arrows for Shuffle Songs, a disc with crossed arrows for Shuffle Albums); the battery sits on the right. The battery's fill shows how much charge is left. At 15% or less the fill turns red, and at 5% or less the outline turns red too. While the cable supplies power, the fill turns green and a bolt appears inside the battery: a light bolt with a dark edge while charging, a plain dark bolt once it's charged. To see the level as a number too, turn on Settings → Battery percent: the percentage then shows to the left of the battery, in red when low.

## Skip songs

Settings → Skip songs picks how the buttons change the song on Now Playing and with the screen off. Hold vol± (the default): hold Vol+ for the next song and Vol− for the previous one. Double vol±: double-click Vol+ for the next song and Vol− for the previous one; a single click changes the volume after a short pause, and holding Vol+ fast-forwards and holding Vol− rewinds inside the song (5 s per step, 15 s per step after about a second), also with the screen off. Double/triple Play: double-click ▶ for the next song and triple-click for the previous one; a single click pauses or resumes after a short pause, and Vol± keep changing the volume at once. In lists and menus the buttons always react at once. With Hold vol± or Double vol±, holding Vol+ or Vol− right after a volume step, while the volume bar is still showing (about 2.5 seconds after the step, with the screen on or off), keeps changing the volume instead of skipping or seeking; pressing any other button ends that.

## Rewind on resume

After a pause, ▶ picks up a little before where you stopped, so you don't lose the thread. The longer the pause, the further back: nothing for a pause of 10 seconds or less, 3 seconds after a minute, 10 seconds after ten minutes and 20 seconds after an hour or more. Audiobooks and podcasts go back half as far again (4.5 s, 15 s and 30 s). After the player was switched off, the first ▶ goes back 20 seconds (30 seconds for audiobooks and podcasts), since the player has no clock to tell how long it was off. It never goes back past the start of the song, chapter or CUE track. If you move the position yourself while paused, ▶ plays from exactly there. The time on Now Playing shows the jump. Settings → Rewind on resume turns this off.

## Headphone profiles

Settings → Headphones keeps four profiles. **Active profile** chooses one, or **Off** to use neutral sound. Open a profile to edit it, then choose **Use profile** to make it active.

- **Name:** choose In-ear, Over-ear, On-ear, Earbuds, Studio or Travel.
- **Trim:** reduce the level from 0 to −12 dB in 0.5 dB steps.
- **Balance:** move left or right in 1% steps. Balance reduces the opposite channel and never boosts either one.
- **Volume ceiling:** limit the volume to a step from 0 to 60; 0 mutes it.

Press ▶ to save a value; Back cancels the choice. The selected profile, names and values are kept on internal storage across restarts and card changes. Trim, balance and the ceiling apply to music and USB DAC output. Music and USB DAC keep their separate volume levels.

Switching to a louder profile, including Off, lowers those volume levels first so neither channel rises unexpectedly. Restoring a channel that was completely muted can lower the volume to 0. Turn it up again as needed, within the chosen ceiling. Music already buffered finishes before new trim and balance values reach it.

## Safe start volume

When you turn the player on, plug headphones into either jack or connect Bluetooth headphones, a volume above the safe level is lowered to it, and a short "Volume lowered to 24" message says so. A quieter volume is never raised. Turn it up again as usual; it stays where you put it until the next time you turn the player on or plug headphones in. Settings → Safe start volume picks the level (12, 18, 24, 30 or 36, on the same scale Now Playing shows; 24 is the default) or Off. The USB DAC keeps its own volume and isn't affected.

## Portrait mode

Not tried on a player yet ([`known-issues.md`](known-issues.md)). Settings → Orientation has Landscape (the default), Portrait and Portrait, flipped. The screen turns as soon as you pick, and the player starts the same way next time. Pick whichever portrait way round puts the buttons where you like them.

In portrait, Home shows the Gnarlyr title below the status row and Now Playing keeps the compact status row; Home shows the album art under the menu, Now Playing puts the cover above the track, Cover Flow stacks the albums from top to bottom, and the status icons sit in a strip above the title. Lists show up to 13 rows. The volume rocker turns with the screen: in Portrait the buttons swap roles, so the one that is Vol− in landscape moves down through lists and the one that is Vol+ moves up; in Portrait, flipped they keep their usual roles. On Now Playing the button nearer the top of the screen turns the volume up, so in Portrait, flipped it's the one labelled Vol−. With the screen off, Vol+ is louder in every orientation.

## Charging display

The charging screen shows a large battery graphic and percentage. Its energy animation runs while the battery is charging and stops when the screen goes dark. An unknown reading keeps the battery graphic hollow. Press Menu to choose Player, USB storage or USB DAC; hold Power to return to the player.

## Languages

The player includes English. Add other languages as external packs without reinstalling the firmware.

- When the player has more than one language, Settings → Language appears after Orientation and lists them by their own names. Pick one and every screen changes at once; the player starts in it next time.
- To add a language without reinstalling, copy its pack to the folder `.gnarlyr/lang` on the SD card (or on internal storage if you use no card). The file is named after the language's code, for example `de.pack`. The folder is hidden on most computers; create `lang` inside `.gnarlyr` if it isn't there. Eject, and the language is in Settings → Language.
- A pack is made for one firmware version. After an update, an older pack may be refused; the player then stays in its built-in language and Settings → Language says "Couldn't load that language" when you pick it. Get the pack made for the new version.
- If you take out the card that holds the pack, the player uses its built-in language until the card is back.
- Today's fonts draw English and the accented letters of western European languages. Cyrillic is drawn in lists and titles but not yet in the small text, so it can't be used for a pack; Chinese, Japanese and Korean are not drawn.
- To make a pack, open the [browser builder](../../site/languages.html), download its English template, and translate the entries. Choose a language code and its native name, then build and download the pack. Missing translations use English. The builder checks the text and characters supported by this firmware. See [language-pack tools](../../docs/language-packs.md) for checked USB installation and language selection from a computer.

## Text size

Accent Color, Brightness, Text size, Orientation, Gain, Output, ReplayGain and Prevent clipping preview the highlighted value as you scroll. Press ▶ to save it. Back, leaving the picker or switching off restores the saved value. Accent Color changes the Graphite theme's highlights and decorative details.

Settings → Text size picks Small, Medium (the default) or Large for list rows, Now Playing, Cover Flow, toasts and messages. Small shows eight rows per screen, Medium six and Large five (in portrait: 13, 13 and 11). The title bar, the battery and the small details on Now Playing keep their size. The change shows at once; no restart is needed.

## Equalizer

Settings → Equalizer previews a sound: Off, Retro, Bass, Heavy, Pop, Jazz, Unique (the same curves as FiiO's firmware), Custom, then your saved slots (Slot 1 to Slot 8) and the files from the `EQ` folder. Move with vol+ and vol− to hear the highlighted curve and see its frequency response. Browsing leaves saved settings unchanged. Press ▶ to keep the sound; Back cancels. Leaving for Now Playing, Home or another screen, or switching off, restores the kept sound. Choosing a slot or a file copies its curve into Custom. Choosing a slot or Custom also opens Custom EQ, so the curve can be changed right away; for a slot, "Save to slot" starts on that slot. While the curve still matches a slot, Settings → Equalizer shows the slot's name in place of Custom.

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

Lines starting with `#` are comments. The points can be in any order and at any frequencies: each of the ten bands gets the gain interpolated between its two nearest points (by octave distance), or the nearest point's gain beyond the ends. Gains are rounded to 0.5 dB and limited to ±10 dB. A file without a usable frequency and gain says "Could not read the EQ file" and changes nothing.

The graph is a calculated 44.1 kHz filter response with a labeled decibel scale, not an audio spectrum or a measurement of the output. It shows the tone-shaping response before the separately displayed computed preamp value. That value comes from the same linear gain used by the equalizer. Custom calculates preamp compensation from the combined filter response, including overlapping boosts and cuts. A change is heard after about a third of a second. The equalizer works on music up to 96 kHz (higher rates play unchanged) and not in USB DAC mode. Bands at or above a quarter of the music's sample rate do nothing (the 16 kHz band is idle at 44.1 and 48 kHz).

## ReplayGain

Settings → ReplayGain offers Off, Track and Album. Track uses each song's prepared gain; Album uses the album gain to keep the level relationships within an album. If one kind of gain is missing, the player uses the other. Songs without gain tags keep their normal level. Opus files also use their R128 track or album gain tags, converted to the ReplayGain reference; standard ReplayGain gain tags take precedence when both are present. Off is the default.

Leave Settings → Prevent clipping on to limit normalization using the file's peak tags. Without peak tags, it retains attenuation and limits boosts to unity. Turning it off allows the full tagged gain. Changes apply to newly buffered audio while the current and next songs keep playing.

The player reads existing ReplayGain tags. Rescan the library, or rebuild it with the desktop indexer, to add gain data to an older library. Your music files are unchanged.

## Transitions

Settings → Transitions offers Off and Fade. With Fade, while shuffle is on, the end of each song fades out and the next one fades in. Settings → Transition length, shown while Transitions is on, sets each fade from 0.5 to 6 seconds; 1.5 seconds is the default, and a fade never takes more than a third of either song. Songs that follow each other on an album, in album order, still play gapless, and so do CUE tracks and chapters of one file, audiobooks and podcasts, and songs shorter than 2 seconds. Skipping, choosing a song or seeking starts it straight away, without a long fade. Without shuffle, songs play as before. Transitions don't apply to USB DAC mode.

Fade plays one song after the other. A crossfade, where the next song starts while the current one fades out, would need two decoders running at once; that doesn't fit in the player's memory.

## Bluetooth headphones

Settings → Bluetooth shows whether Bluetooth is on and which headphones are connected. Open it to turn Bluetooth on or off, connect, switch or pair headphones. The screen lists your headphones (the player remembers up to six, most recent first) with their state, a **Pair new device** row, and a hint for the selected row at the bottom.

**Pair new headphones:**

1. Put the headphones in pairing mode.
2. Select **Pair new device** and press ▶. The player searches and lists what it finds below that row.
3. Select your headphones and press ▶. The row says "Connecting…", then the player shows "Connected to …".

**Every day:** with Bluetooth on, the player connects to your most recent headphones by itself when it starts and whenever they come back in range. If a connection stalls, it resets and tries again within about 15 seconds. To switch to other remembered headphones, select them and press ▶. To disconnect, select the connected headphones and press ▶: they stay disconnected until you press ▶ on them again or restart the player. To forget headphones, hold ▶ on them.

Music plays through the headphones whenever nothing is plugged into either jack. To switch to wired headphones, pause, plug them in and press play: while music is going to Bluetooth the player can't see the jack. After you unplug, the next play goes back to Bluetooth.

The headphones' controls work: pause, play and next/previous track, and taking them out of your ears pauses if they have in-ear detection. If they disconnect, playback pauses. With nothing connected for 5 minutes, Bluetooth switches itself off to save battery until you open its screen again or restart the player. A small Bluetooth mark next to the play icon at the top shows Bluetooth is on (dim) or connected (bright).

## Audiobooks and podcasts

Put audiobooks in a folder called `Audiobooks` and podcasts in a folder called `Podcasts`, at the top of the card (any capitalisation; `Audiobook` and `Podcast` work too). Home then shows **Audiobooks** and **Podcasts**; they stay hidden while those folders don't exist. Books and episodes stay out of Music (Cover Flow, Albums, Artists, Genres, Songs and shuffle), so they never turn up between your albums.

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
- Playback speed can't be changed yet.
- **HE-AAC doesn't play.** An `.m4b` or `.m4a` that isn't AAC-LC or Apple Lossless (HE-AAC with SBR or PS, xHE-AAC) is refused with "Only AAC-LC and ALAC M4A play". Libation can download AAC-LC: in Libation's settings, leave "Request xHE-AAC Codec" off (Libation's FAQ says that makes it download AAC-LC); it keeps whatever codec Audible delivers and doesn't re-encode, so the files may still be HE-AAC for some titles. Libation names every non-MP3 file `.m4b`, whatever its codec.

## Updating Gnarlyr

An install through the updater has only run in the emulator so far ([`known-issues.md`](known-issues.md)). Charge the battery first.

1. Connect the player and copy the new `GNARLYR.IMG` to the top level of internal storage, not into a folder.
2. Eject both drives. The player shows "Checking GNARLYR.IMG", then "Installing update" with a percentage. Don't unplug or turn it off.
3. At "Update installed", unplug USB and hold Power. The player turns off.
4. Hold Power to start the new version.

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
- **"USB code missing: reinstall the update" or "Restart the player to finish the update":** see Known trade-offs in [`known-issues.md`](known-issues.md).
- **The player doesn't start at all:** the chip's maskrom mode writes FiiO's firmware back from a computer over USB. It needs a patched `rkdeveloptool` and some care; the steps are in [`device-recovery.md`](device-recovery.md). If the player still runs and you expect trouble, create the file `MASKROM.REQ` on internal storage beforehand (same document).
- **Noting a problem:** write down what you did, the FiiO version the player had before, and the Firmware line from Settings → About.

## Themes and controls

Settings → Theme offers Neon Noir, Graphite, Sunset, Sea Glass and Classic. Theme changes keep the text size and orientation you selected. Settings → Controls assigns tap and hold actions independently for Volume+, Volume−, Play and Menu during playback. Default keeps the normal skip gesture; Off ignores that action. Reset buttons restores the normal controls. List navigation stays consistent.

## Waiting

When something takes more than a moment, its screen says what is happening and a small wireframe turns under the message: a cassette while saving (Export plays, mixtapes, tags), a disc while working through your music (library scan, finding covers, search, preparing a queue, listening history) and a solid while loading tags. Where the player knows how far it has got, the cassette's tape winds from the left reel to the right, the disc's rim fills in, or the solid's edges light up. Only the wireframe is redrawn, and it stops as soon as the work ends or the screen turns off.
## Customising Home

Settings → Home screen lists every Home item in Home order, each marked Shown or Hidden; Settings is marked Always shown and stays on Home. Press Play on an item for **Hide from Home** (or **Show on Home**) and **Move**. Move picks the item up, and so does holding Play on it: the title reads Move: vol ±, the volume buttons move the item up and down the list, Play places it and Back puts it back where it was. **Reset to default** restores the original order with every item shown. Each change is saved at once and Home follows it straight away, keeping your place on the item you had selected. Hiding only ever removes an item: Now Playing and Up Next still appear only while something is loaded, and Audiobooks and Podcasts only when their folders exist.

## Editing a queue

Open Up Next and hold Play on a song for its actions. Move it earlier or later, play it next, or remove it. The playing song offers Edit full queue and Keep current. Clear added removes manually queued songs while keeping the underlying list. Back cancels unfinished queue preparation. The exact edited order and playback position are saved together.

## Mixtapes

Home → Mixtapes saves a queue as a named list. Save queue captures the current sequence; New mixtape starts an empty one. While naming, the volume buttons choose a character, Play adds it, Menu erases it and holding Play saves the name. A mixtape's actions play it, load it into Up Next for editing, replace its contents, rename it or delete it. Songs are matched by their paths and cue boundaries after a rescan.

## Clock and calendar

Gnarlyr sets its clock from your Mac. Every companion command that ejects the player (`python3 tools/echo_debug.py set-time` does only that) leaves the Mac's time on the drive, and the player sets its clock as the drive is ejected. The clock then keeps going while the player is on. When the player has been switched off or restarted, the time is unknown until the next sync.

While the time is set it also shows in the status bar, left of the battery; in landscape it steps aside for a long screen title. Settings → Clock format chooses 12-hour time with AM/PM or 24-hour time (the default). The status bar follows the same choice, including in Calendar. Home → Clock shows the time, the weekday and the date. Press ▶ for Calendar: the current month from Monday with today marked. The volume buttons move a month at a time, ▶ goes back to the current month and Back returns to Clock. Without a set time both say so and how to set it.

## Listening and lyrics

Home → Listening shows total listening time, plays, skips, and top songs, albums and artists. It records time while music is playing. Totals and ranked lists work without a calendar clock.

Export plays saves your plays to `/.scrobbler.log` at the top of the player's drive, in the Rockbox scrobbler format that import tools read; your history stays as it is. It starts as soon as you choose it and shows how far it has got; Back cancels it. When it finishes it shows how many plays it saved. Each export replaces the previous file. With nothing to export it says so and leaves the file alone. Plays of songs no longer in the library are left out. Times are saved in UTC. A play gets its time only while the player's clock is set (see Clock and calendar); a play made while the clock was not set is saved with the time 0.

Place a timed `.lrc` file beside a song, with the same filename stem. Hold Menu on Now Playing to open Lyrics; tap Menu still opens Up Next. Lyrics follow playback, and the volume buttons move through the lines. Back returns to Now Playing.
