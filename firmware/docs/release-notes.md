# Podium release notes

Podium is a fork of EchoPod 0.6.0 for the full-size Snowsky Echo. Entries from 0.6.0 down are EchoPod's.

Each Podium version starts with `### Highlights`: at most five one-line bullets for users, in plain words. The website's What's new page shows only those; the sections below are the detailed record.

## Podium 4.5.9 (2026-11-08)

### Highlights

- Ten-band equalizer in 0.5 dB steps, with saved curves 1 to 8 and AutoEQ files from an `EQ` folder.
- Button lock: hold Play and press Power for a second to lock or unlock the buttons.
- Hold Play on a song → Add to Favorite Playlist collects songs in `Favorite Tracks.m3u`.
- Hold a volume button right after a volume step to keep changing the volume.
- Shuffle shows in the top bar: a note or a disc with crossed arrows.
- Longer battery life: the player sleeps between frames, the decoding core rests when ahead, and the panel sleeps with the screen off.
- Favorite Tracks shows up at once, no rescan; a rescan no longer drops Bluetooth headphones.
- Diagnostics moved to Settings → About.

### New

- **Ten-band Custom EQ.** Settings → Custom EQ now has ten bands: 31, 62, 125, 250 and 500 Hz, and 1, 2, 4, 8 and 16 kHz. Each band moves in steps of 0.5 dB between -10 and +10 dB and shows its gain with one decimal (+1.5 dB). The list scrolls; it works in landscape and in portrait. Bands at or above a quarter of the music's sample rate are skipped (the 16 kHz band is idle at 44.1 and 48 kHz). The equalizer still leaves music above 96 kHz unchanged.

- **Saved curves.** The last row of Custom EQ, "Save to slot", saves the current curve into slot 1 to 8 (▶ to pick, vol+ and vol- for the slot, ▶ to save, ↩ to cancel; a slot already in use reads "used" and is overwritten). Saved slots appear in Settings → Equalizer after Custom, named Slot 1 to Slot 8. Choosing one with ▶ copies its curve into Custom and opens Custom EQ on it, with "Save to slot" set to that slot. While the curve still matches the slot, Settings shows the slot's name in place of Custom. The slots live in `/.echo/eq_slots.bin` on the player's internal storage.

- **Curves from files.** Put text files in a folder named `EQ` at the root of the card (or of the internal storage when no card is inserted). Each file shows in Settings → Equalizer under its file name, after the saved slots (up to 16 files, names up to 27 characters, names starting with a dot are ignored). Choosing a file copies its curve into Custom. Two formats are read: Equalizer APO / AutoEQ `GraphicEQ: 20 -1.5; 21 -1.2; ...` lines, and one `frequency gain` pair per line (`31 1.5`). Frequencies between the bands are interpolated by octave distance, gains are rounded to 0.5 dB and limited to ±10 dB.

- **Add to Favorite Playlist.** The song menu (hold Play on a song in any list) has a third item after Add to Up Next. It appends the song to `Favorite Tracks.m3u` in the top folder of the music storage, the card when one is in use. The file is created on first use with an `#EXTM3U` header and one relative path per line, and a song already listed is not added twice ("Added to Favorites", "Already in Favorites", "Couldn't add to Favorites"). A track of a CUE album is written as a `#PODIUM-START:<ms>` line above the album's audio file path, so computer players still open the file while the player and echo-sync resolve the line to that exact track (a plain path to a CUE-split file still means its last track); each track is added once. It holds about 300 songs. Favorite Tracks shows under Music → Playlists with the new song straight away, also after a restart; no rescan is needed.

- **Button lock.** Hold ▶, then press Power while still holding it, and keep both down for about a second to lock the player's buttons; do the same to unlock them, so nothing is pressed by accident in a pocket. While locked, every button except Power is ignored (Power still turns the screen on and off, and holding it still turns the player off); a padlock shows in the top bar; a press shows "Hold Play+Power", and locking or unlocking lights the screen and shows "Buttons locked" or "Buttons unlocked", also when the screen was off. Wired headset buttons keep working. The lock ends when the player restarts.

- **Hold to keep changing the volume.** With Settings → Skip songs set to Hold vol± or Double vol±, holding Vol+ or Vol− right after a volume step, while the volume bar is still showing on Now Playing (it now stays 2.5 s after a step) or within 2.5 s with the screen off, keeps raising or lowering the volume instead of skipping the song or seeking. Holding without a volume step first skips or seeks as before, and pressing any other button ends it. Double/triple Play is unchanged: there Vol± always change the volume.

### Fixed

- **Holding a button could count as a tap.** The ▶‖, ≡, ↩ and volume buttons share one resistor ladder; a reading that wandered between two buttons' ranges during a hold looked like letting go, so holding Play on a song sometimes played it instead of opening its menu. A held button now stays held until the reading leaves every button's range.

- **A CUE track started a moment late.** Changing songs muted the DAC with its hardware mute pin and faded the volume up from silence over about 60 ms, so the first moment of a song that starts right at its CUE mark was lost. Changing songs and seeking now keep the output open and fade up from 40 dB below the set volume (about 30 ms). The decoded audio was already sample-exact: checked on Playing The Angel, every track starts within 30 samples (0.7 ms) of its CUE mark.
- **A library rescan dropped the Bluetooth headphones.** Finishing a scan restarted the Bluetooth module, which cut the connection and often made reconnecting fail. A connected device now stays connected through a rescan (and through a USB session that changed the storage).
- **In portrait, volume went the wrong way.** On Now Playing the button nearer the top of the screen now turns the volume up and holding it skips forward, in both portraits; in Portrait, flipped that's the one labelled Vol−. Lists keep their direction. With the screen off, Vol+ is louder, as labelled.
- **Long messages ran off a portrait screen.** A message too wide for the screen, such as the lock hint in Large text, now uses the smaller font.

### Changed

- **Portrait Home and Now Playing drop the title line.** Only the status row stays at the top, so Home's menu and Now Playing's cover move up; other screens keep their titles.

- **Shuffle shows in the top bar.** A note with crossed arrows means Shuffle Songs, a disc with crossed arrows means Shuffle Albums, on every screen. Now Playing no longer spells it out in its details line; Repeat still shows there.

- **Longer battery life.** With the screen on, the player now draws a new frame only when something changes (about 60 times a second while something moves, 10 times a second when nothing does) and sleeps between steps instead of spinning. The decoding core sleeps whenever it is ahead of playback. With the screen off, the display panel is put to sleep, not just its backlight; waking it takes about 0.1 s longer. How much longer the battery lasts hasn't been measured yet.

- **Settings from 4.5.8 are converted.** The five old Custom bands are spread over the new ten by octave distance and kept at their gains in 0.5 dB steps; the 1 kHz band is unchanged, the others differ slightly. Settings written by 4.5.9 are not read by older versions, which start from the defaults.

- **Headroom follows the deepest cut as well as the highest boost.** Custom turns the music down by its largest boost, or, with only cuts, by the deepest dip of the combined curve, so neighbouring cuts at the low bands don't clip.

- **Diagnostics is now Settings → About.** Home no longer has a Diagnostics row; the same rows (Firmware, Battery and the rest) open from About, the first row in Settings.

- **About lost Last start, Last step, Last battery and Last USB.** They rarely showed anything useful. About now has Firmware, Battery, Jack 3.5, Jack 4.4, Music, Headset and CPU clock.

- **Diagnostics → Key test is gone.** The button lock needed the room, and the screen was a landscape-only picture. In the developer build, the USB register, Test tone, Tone 192k, Tone 176k, Loop cycles, Core 1, LPC accel, High RAM, Boot clocks, Boot PLL and Boot hold rows went too, to make room for the equalizer, DMA tone went to make room for the Favorite Tracks list, and Stopwatch went to make room as well. A Core 0 busy row shows how much of the last second the main core spent awake.

### When you update

- Custom EQ keeps its curve (converted to ten bands). Saved slots start empty. Settings → About replaces Home → Diagnostics.

## Podium 4.5.8 (2026-11-07)

### Highlights

- Portrait mode: Settings → Orientation turns the screen upright.
- `.m4b` audiobooks with chapters, as Libation and Audible conversions make them.
- One album with a bad cover no longer blanks every cover in Cover Flow.
- The library rebuilds once after the update.

### New

- **Portrait mode.** Settings → Orientation → Portrait or Portrait, flipped turns the screen upright (222 × 480) straight away, and the player starts that way next time. Home shows the album art under the menu, Now Playing puts the cover above the track, Cover Flow stacks the albums from top to bottom, and lists show up to 13 rows with the status icons in a strip above the title. The volume rocker turns with the screen: in Portrait (not flipped) the button that was Vol− now moves down through lists. List values use the small font and never take the room of a name. It is saved with the other settings; settings from earlier versions load with Landscape.

- **`.m4b` audiobooks with chapters.** `.m4b` files are indexed like `.m4a` (AAC-LC and Apple Lossless). Chapters are read from the Nero `chpl` atom and from the QuickTime chapter track (what Libation, Audible conversions and ffmpeg write); a chaptered book in `Audiobooks/` is a book of its own with "Chapter n of N", the chapter titles, bookmarks that resume the right chapter and position, and the 30 s / 15 s jumps within a chapter. Files without chapters, or with chapter data that can't be read, are one track. A file that isn't AAC-LC or ALAC (HE-AAC, xHE-AAC) now says "Only AAC-LC and ALAC M4A play". In Libation, leave "Request xHE-AAC Codec" off to get AAC-LC.

### Fixed

- **One album could leave Cover Flow without any covers.** If building one album's cover failed or the rescan stopped partway, every album lost its cover, and rescanning didn't help. Now a bad cover only costs its own album, and the covers you had stay until a rescan finishes. An album spread over many folders, or many albums in one folder, no longer slow the cover step down.

### Changed

- **The developer Diagnostics menu lost its Fonts and Long names rows.** The code window is full, and portrait mode needed the room. The shipped (`release`) Diagnostics menu is unchanged.

### When you update

- Your library is rebuilt once, automatically, because its format changed (it adds chapter folders); your `.m4b` files then show up. Settings, equalizer and Bluetooth headphones stay as they are. Orientation starts on Landscape. Bookmarks stay valid.

## Podium 4.5.7 (2026-11-05)

### Highlights

- Up Next shows on Home while music plays.
- Hold Play on a folder to play everything in it.
- Volume has 60 steps of 1 dB, up to the DAC's full level.
- New Settings → Skip songs: hold or double-click volume, or double-click Play.
- Fixes: no crackle at power-off, steadier battery percent, Custom EQ no longer clips.

### New

- **Up Next on Home.** Home shows Up Next below Now Playing once something is playing, so the queue no longer needs a trip through Now Playing and Menu.
- **Play a whole folder.** Hold ▶ on a folder in Music → Folders to play every song under it, subfolders included, in the order Folders shows them; a CUE album plays once, as its songs. With Shuffle set to Songs it starts from a random song; otherwise from the first. The queue holds up to 65,535 tracks. A folder without songs shows "No songs in this folder". Audiobooks and Podcasts keep opening the folder on a hold.
- **Volume in 60 steps of 1 dB, up to the DAC's full level.** The volume has 60 levels of exactly 1 dB (it had 50 of 1.2 dB), and the top level is the DAC's full level (0 dB) on the 3.5 mm jack: 1 dB louder at the top than before. The 4.4 mm jack keeps stock's 5 dB lower scale. A saved volume (music and USB DAC mode) converts to the nearest new level, so on 3.5 mm the loudness doesn't change; on 4.4 mm it is about 3.5 dB quieter, so turn it up a few steps once; the default is level 13 for music and 35 for DAC mode. The Now Playing bar fills over the 60 levels.
- **Skip songs.** Settings → Skip songs picks how the buttons change the song on Now Playing and with the screen off: Hold vol± (as before: hold Vol+ for the next song, Vol− for the previous one), Double vol± (double-click Vol+ for the next song, Vol− for the previous one; a single click changes the volume after a short pause, and holding Vol+ fast-forwards and holding Vol− rewinds inside the song, faster the longer it is held, on Now Playing and with the screen off) or Double/triple Play (double-click ▶ for the next song, triple-click for the previous one, like the wired headset remote; a single click pauses after a short pause, Vol± stay instant). Lists and menus are not affected. It is saved with the other settings; settings from earlier versions load with Hold vol±.

### Fixed

- **Custom equalizer headroom.** The Custom EQ's attenuation now follows the real peak of the curve instead of a fixed table sized for five equal boosts. A boost still lowers the whole curve by about the boost, so a raised band never clips; the bands left at 0 sound quieter by that amount, not inverted. Every non-flat curve, cuts included, keeps 2 to 5.5 dB of headroom (more for deeper cuts), so cutting 100 Hz and 315 Hz no longer clips drums and loud masters ("crunchy"). Changing a band's gain with vol± while playing no longer clicks. Off stays untouched.
- **No crackle at power-off.** Turning off while music plays, after a pause, by the sleep timer, auto-off or a low battery now stops the music and switches the DAC and amplifier off before the power goes, with no crackle or pop. Turning off takes about a second longer after the screen goes dark.
- **One hold to finish an update.** The "Update installed" screen no longer closes on any key. Unplug USB, hold Power once to turn the player off, then hold it again to start the new firmware. Short presses there are ignored.
- **Battery percent no longer sticks low after load.** The shown percent used to fall after a load (backlight, playing) but needed a 10-point rise to climb back, so it looked like a high drain. On battery it now moves both ways after a 3-point change.
- **A paused player saves battery.** After 30 seconds of pause the player powers down its audio (DAC, amplifier, second core) and drops to the idle clock. Play continues from the same position, with a short delay while the output starts again.

## Podium 4.5.6 (2026-11-04)

### Highlights

- Pick the highlight color (eight choices).
- Pick the text size: Small, Medium or Large.
- Equalizer with six presets and a five-band Custom EQ.
- Folders show files as they are on the card, CUE albums included.
- Wired headset buttons pause, skip and go back.

### New

- **Selection color.** Settings → Color picks the highlight of the selected row: Blue (as before), Red, Orange, Green, Teal, Purple, Pink or Graphite. The choice is saved with the other settings.
- **Text size.** Settings → Text size picks Small (eight rows per screen), Medium (as before, six rows) or Large (five rows) for lists, Now Playing, Cover Flow, toasts and messages, with every accented and Cyrillic letter at each size. It applies at once and is saved with the other settings; settings from earlier versions load with Medium.
- **Equalizer.** Settings → Equalizer picks Off, Retro, Bass, Heavy, Pop, Jazz, Unique or Custom; Custom EQ sets five bands (100 Hz, 315 Hz, 1 kHz, 3.15 kHz, 10 kHz) by ±10 dB with vol±. It works for 16- and 24-bit files at 44.1, 48 and 96 kHz; off, the sound is unchanged. Not applied in USB DAC mode or above 96 kHz.
- **Folders show files as on disk.** A FLAC with a CUE sheet is listed once, under its file name, and plays the album from the first track; the `.cue` file is listed next to it and opens the album's songs by title. The library rebuilds on the first start.
- **Wired headset buttons.** One, two or three clicks of the middle button on a 3.5 mm headset remote pause, skip or go back. On Android-style remotes + and − change the volume and holding them skips to the next or previous track. Apple EarPods: only the middle button works (see `known-issues.md`). Headphones without a mic are ignored. Diagnostics → Headset shows what the player reads.

## Podium 4.5.5 (2026-11-03)

### Highlights

- Russian, Ukrainian and other Cyrillic names show their letters.
- Settings stay when you swap or remove the microSD card.

### New

- **Cyrillic.** Song, artist and album names in Russian, Ukrainian, Belarusian, Serbian, Bulgarian and Macedonian show their letters instead of `?` (U+0400–U+045F, Ґ and ґ, regular and bold). The small font shown right after an update is still ASCII only.

### Fixed

- **Settings survive a change of storage.** Settings are the player's, not the music's: they now live on internal storage even when the music comes from a microSD card, so taking the card out or putting one in keeps them. Settings saved by an earlier Podium on the card or internal storage are picked up once. Favorites and resume positions stay with each library.

## Podium 4.5.4 (2026-11-02)

### Highlights

- M4A files play: AAC-LC and Apple Lossless.
- Text is readable right after an update.
- The splash screen, USB drive and USB DAC say Podium.

### New

- **M4A playback.** `.m4a` files play: AAC-LC (what Apple Music and iTunes purchases use, mono or stereo, 44.1 and 48 kHz) and Apple Lossless (ALAC, 16 and 24 bit, mono or stereo). Seeking, resume after power-off, Up Next, shuffle and bookmarks work as for the other formats, and consecutive tracks play without a gap, trimmed by the encoder delay and padding in the file's `iTunSMPB` tag. Now Playing shows `AAC` or `ALAC`. The library already listed `.m4a` files with their tags, length and cover art; nothing needs rescanning. HE-AAC (SBR and PS), AAC at rates the player's output doesn't offer (such as 22.05 and 32 kHz), `.m4b` audiobooks and AAC in other containers don't play (see `known-issues.md`). Played on the Echo: Daft Punk, Massive Attack and St. Vincent albums.

### Fixed

- **Text is readable after an update.** After installing an update, every screen showed `?` instead of letters until the player was turned off and on again. Screens now use the small built-in font until the restart, so the Update installed message, the Goodbye screen and the menus stay legible.

### Changed

- **Named Podium.** The splash screen, the Home title, the USB drive and the USB DAC device say Podium instead of EchoPod.
- **Firmware version in Diagnostics.** Firmware shows the version and the build, such as `4.5.4-b97f4e9`, instead of the build alone.
- **Echo only.** The Echo Mini board is gone from the source.

## Podium 4.5.3 (2026-11-01)

### Highlights

- CUE sheets: one track per CUE track, gapless.
- Music plays from internal storage when there is no card.
- Long albums play without stutter and CUE tracks start fast.
- The player starts on large internal storage.
- An update file is deleted after it is used.

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

### Highlights

- First Podium for the full-size Echo: iPod-style menus and 22 px text.
- Installs from a file copy; the same updater brings FiiO's firmware back.

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
- **Play no longer pauses again on Bluetooth.** A late "paused" report from the headphones could pause playback right after Play was pressed. Reports are now ignored for 3 seconds after Play or Pause on the player, and a pause the headphones cause shows "Paused by \<name>".
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
