# Gnarlyr release notes

Gnarlyr builds on Podium 4.5.9 and EchoPod with their GPL credits preserved. The source provenance is recorded in [`../UPSTREAM.md`](../UPSTREAM.md). Version 1.0.0 is not released; hardware checks are recorded in [`known-issues.md`](known-issues.md).

## 1.0.0-beta.1 (public beta)

Gnarlyr is free, open-source firmware for the full-size Snowsky ECHO music player. It is built on Podium and EchoPod. This is a public beta for people who want to try it and tell us what breaks. It has been tested on the developer's player, but not yet on many others.

### Install

- **Coming from FiiO's firmware:** download `ECHOECHO.IMG` and check it against `SHA256SUMS`. Connect the player, copy the file to the top of its internal drive (not into a folder), eject and unplug. The player installs Gnarlyr and starts it.
- **Updating Gnarlyr:** copy `GNARLYR.IMG` to the top of the internal drive and eject. Confirm the new version with one more connection (step 4 of the update guide).
- **Going back to FiiO:** copy FiiO's `ECHOV200.IMG` to the internal drive and eject.
- Full steps, and what to do if the player won't start: <https://gnarlyr.com/install.html>.

### What's new

- **Faster USB copying.** The player connects at High Speed (480 Mbit/s) and copies about twice as fast as before: 8.7 MB/s writing to the microSD card and 9.8 MB/s reading from it.
- **New USB screens.** Connect to computer draws the last 10 seconds of reads and writes as a wireframe landscape. It shows the link speed as a USB symbol (with a lightning bolt at High Speed) and a row for each drive with its free space and activity. Press Menu for detailed stats. The USB DAC screen shows the level, the format, both volumes and the buffer, and tapping Back opens its details.
- **Safe updates.** A new version starts on trial and the previous one is kept as a backup. If the new one fails to start properly, the player puts the backup back and says "Update rolled back".
- **Card eject and hot-insert.** Settings → Eject card makes the microSD card safe to remove. Insert or remove a card while the player runs, and the library follows it.
- **Fast library scans with progress.** The player shows what it is doing while it checks for changes, sorts and saves. You can browse and play as soon as the tracks are ready, and album covers fill in later.
- **Recap.** See your listening time, plays, skips and top songs, albums and artists. Recap → Highlights shows a month, a year or all time as a short run of cards.
- **Wireframe start-up and shutdown.** Turn them off in Settings → Power → Start and stop animation.
- **Make it yours.** Choose from five themes or add `.gtheme` packs. Settings → Orientation offers portrait mode, and Settings → Home screen chooses and orders the items on Home.
- **Formats.** FLAC, MP3, WAV, AIFF, AAC and Apple Lossless (`.m4a`, `.m4b` audiobooks with chapters), Ogg Vorbis, Opus, APE, WMA, and DSD (DSF/DFF, played as PCM). Also CUE sheets and M3U playlists, at up to 192 kHz.
- **Parametric EQ and sound controls.** Use up to ten filters, or import AutoEQ files. Sound shaping offers seven simple controls. Also crossfeed, playback speed from 0.5× to 3× at the same pitch, fades between shuffled songs, and Skip silence and Speech boost for audiobooks and podcasts.
- **Bluetooth headphones,** with their own volume, as well as the 3.5 mm and 4.4 mm jacks.
- **ReplayGain** (Track, Album or Auto) and four **headphone profiles**, each with trim, balance and a volume limit.
- **Language packs.** English is built in. Make a pack for your language at <https://gnarlyr.com/languages.html> and copy it to the player.

### Known issues

- With the player connected, macOS System Information (`system_profiler`) can hang. Finder is not affected.
- Copy speed is still being improved.
- Battery life hasn't been measured yet.
- Two safety nets haven't been tried on a player since their latest fixes: recovery mode by holding Power and Vol+, and the automatic rollback of a broken update.
- Settings reset to their defaults if you come from a Gnarlyr development build.
- Ogg, Opus, APE, WMA and DSD playback, and Bluetooth, have had the least listening on a player. Reports on them are especially welcome.

To report a problem, open an issue at <https://github.com/0xdoublesharp/gnarlyr/issues>. Include the version from Settings → About → Firmware, what you did and what happened. Attach the report files from the hidden `.gnarlyr/debug` folder on the player's internal drive. The full list is in [known issues](https://github.com/0xdoublesharp/gnarlyr/blob/v1.0.0-beta.1/firmware/docs/known-issues.md#beta-testers).

## Unreleased: safe updates and recovery

- An update installs on trial into slot 1; slot 2 keeps the build that was running. The trial ends, and the new build is copied into slot 2 ("Saving this version as the backup"), after a USB session in which the computer wrote to internal storage and then ejected the drives. Copy any small file before ejecting: a session whose drives opened read-only, or that wrote nothing, leaves the trial running.
- A trial build that starts without its language pack or its extension module restarts by itself, and the next start puts the slot 2 build back ("Update rolled back"). Three unconfirmed starts or USB sessions do the same; a start after which the player runs for 20 s doesn't count. Holding Power for 10 s restarts a stuck trial build.
- With the cable in a computer, holding Power and vol+ for 3 s restarts the player into maskrom at any time, including on the dark charge screen. A `MASKROM.REQ` file on internal storage does the same at the next start.
- Both MMC controllers are reset once before the first eMMC command and never afterwards. A reset after start-up had left a trial build without its text and unable to roll back.
- The USB FIFOs are sized from the USB core's FIFO RAM, 308 words on the player. Before, the player refused the USB audio (DAC) configuration.
- On the player, trial installs ran during development and the paperclip maskrom route put a known-good build back after two broken ones. The automatic rollback of an unhealthy build, confirmation by host writes and the always-on loader keys have emulator checks only.

## Unreleased: settings continuity

- The first start of a new version copies the settings and saved state the previous version left into a backup named after it; the last two are kept. Settings → Backups puts one back.
- A version that finds a file written by a newer version keeps that file aside instead of resetting it, and the newer version takes it back on return. Every file format has a conversion path from version 1, so the first layout change after the beta converts files instead of resetting them.
- These are checked by unit tests and emulator scenarios; they have not run on the player yet.

## Unreleased: cards, power and controls

- The microSD card can be inserted and removed while the player runs; the library follows the card. Settings → Eject card makes it safe to remove, Settings → Mount card uses an ejected card again, and Play on the USB screen ejects the card while the computer has the drives.
- Settings → Power → Standby when switched off makes a tap of Power, and auto off, pause and power down the output and Bluetooth for an instant return; Power off after (30 min, 2 h or 8 h) then switches the player off.
- Settings → Status bar → Status bar offers Compact: smaller icons, the percentage inside the battery and more list rows.
- Holding Power and pressing Vol+ or Vol− skips tracks, also while the buttons are locked. Settings → Controls → Hold Menu chooses what holding Menu does.
- Holding Play on a song offers Hide track and Delete file.
- Recap → Highlights shows a month, a year or all time as a short run of cards.
- Bluetooth has its own volume, separate from the wired one, starting at 50 of 60.
- The charge screen estimates the charging rate and time to full and says whether the power comes from a computer or a charger.
- "Checking for changes" shows its progress with a turning disc.
- Checked with host tests and emulator scenarios; player checks are listed in [known issues](known-issues.md).

## Unreleased: formats start at version 1

- Every Gnarlyr file and pack format is version 1, using its current layout: settings, language packs, theme packs, the library, the stores and the small files ([formats](../../docs/formats.md)). Files written by earlier development builds are refused and replaced by defaults or rebuilt; settings reset once.
- Language packs are segmented: text used by one screen group loads with that group, so a pack needs only its shared text and its largest group in the player's 18 KiB of language memory.
- A pack file with a damaged part is not offered in Settings → Language. If the card holding the chosen pack is removed, screens opened afterwards show English until the card is back.
- Checked in host tests, the tool and browser tests and the emulator; not yet seen on a player.

## Status bar

- Settings → Status bar adds live player volume, repeat mode, queue position and percentage switches, and an optional five-bar battery. Existing files keep the solid battery and clock defaults. Controls stays last in Settings.
- Now Playing uses the header palette for its top band, keeping status icons and numbers readable in Classic.
- Queue position is centred in portrait and appears in landscape when the title is hidden. Items fit by priority at every text size; the battery and screen title keep their places.
- Native checks cover fitting, battery levels and thresholds, settings migration and layout; the focused ARM scenario follows volume and queue changes. Simulator captures cover Neon Noir, Graphite and Classic in both orientations and all text sizes. Player acceptance remains with the integration owner.
## Unreleased: DSD PCM conversion

- Added a dedicated DSF/DFF decoder, 88.2 kHz PCM conversion, seeking, queued
  transitions, ID3 artwork and DFF DIIN artist/title fallback in both scanners.
- Core 1 requests 200 MHz for stereo DSD64, DSD128 and mono DSD256, and for
  Vorbis above 96 kHz. Idle work and other streams retain 100 MHz. Stereo
  DSD256 is refused with a 20% instruction reserve; DST and multichannel DSD
  remain unsupported. Stock/hardware clock limits and software transitions
  are documented in [the clock reference](core1-clock.md).
- Generated native reference tests, filter-response checks and ARM decoder
  comparisons cover software behavior. Physical playback and real-time margin
  have not been checked on the player.

## Unreleased: AIFF playback

- Added AIFF and AIFF-C `NONE`/`sowt` through the WAV PCM path, with seeking,
  library ID3 tags and embedded artwork. Mono/stereo 16/24-bit files accept the
  standard 8–192 kHz rates. Compressed and float AIFF-C remain unsupported.
- Generated native fixtures cover playback, seeks, endpoints, tags and artwork.
  This addition has not been checked on the player.


## USB screens

- Connect to computer shows the live rate in MB/s beside fixed read and write arrows, a 10-second graph, what was read and written, and a gauge per drive with free space at connect and a light with write and read arrows that fade over 300 ms. Nothing in the rate row moves or changes size: an arrow lights while its direction is in use and stays lit 0.4 s after the last transfer, both dim with a dimmed 0.0 when nothing moves, and the rate is averaged over about a second.
- Press Menu on the Connect to computer screen, or tap Back on the USB DAC screen, for details: per-drive data, commands, command size, peak and average rate, drive busy time, errors with the last sense code, link and session time; for the DAC, the exact format, interface, buffer range, clock correction and feedback, underruns and overruns, gain register, host volume and data rate. The chosen page is kept until restart.
- During high-speed USB DAC streaming the screen now draws each row in 160-pixel pieces, keeping USB audio polling inside the 125 µs frame (148 → 114 µs worst gap), so redraws no longer cost audio packets.
- The DAC ring now shares the idle mass-storage buffer, which leaves 9 KB of the USB overlay free in developer builds.
- Native rendering, rate, sparkline, free-space and counter tests and ARM scenarios cover software behaviour. Player checks are listed in [known issues](known-issues.md).

## USB DAC

- High-speed UAC2 adds stereo 16/24/32-bit PCM at 44.1, 48, 88.2, 96, 176.4 and 192 kHz. Full-speed UAC1 retains 16/24-bit at 44.1/48 kHz.
- Explicit feedback follows the player’s clock without dropping or repeating samples in UAC2. The USB overlay ring targets 5 ms of device buffering (17,600 bytes in device, 24,000 in release); headphone trim/balance and DAC volume remain available. An empty USB stream is cleared and muted before it reprimes.
- ▶ sends host play/pause; 400 ms Vol+ and Vol− holds send next and previous. Volume taps change the local DAC volume.
- Native descriptor, clock-drift and PCM tests and ARM streaming scenarios cover software behavior. Physical host compatibility, clock accuracy, both outputs, remote keys and hardware CPU margin remain open in the [host acceptance checks](known-issues.md#usb-dac-host-acceptance).

## Identity and files

The canonical firmware name is **Gnarlyr**, made by **doublesharp**. The image identifier and USB storage vendor are `GNARLYR`; the update file is `GNARLYR.IMG`, the data directory is `/.gnarlyr`, and exported CUE playlist entries use `#GNARLYR-START:`. `identity.json` supplies the build, simulator and host tools.

The binary library, settings, saved-queue, mixtape and listening formats keep their maker-specific `DS` markers. Moving an installed developer build to the canonical identifiers requires the prepared update/data transition so its existing state is preserved.

## Library and navigation

- Home defaults to Music, Mixtapes, Recap, Clock, USB Mode and Settings. A loaded track adds Now playing at the top, including while paused; the artwork caption shows the track title when space allows. Audiobooks and Podcasts are reachable from Music when their shelves exist; they and Up Next can be shown through Settings → Home screen. The editor has nine items; Now playing is automatic. Existing untouched Home defaults migrate on load, while customised orders and hidden masks are kept.
- The library opens as soon as its tracks are sorted; album covers fill in afterwards while the player is idle and not playing, pause for a key press, playback or a USB cable, and resume where they stopped, also after a restart. In the emulator, a generated 4,003-track card is browsable after 29.6 s and has all 303 covers after 135.8 s; the same card took 160.9 s, all of it before the library opened, at the start of this work. Not yet timed on a player.
- Settings → Rescan library says "Library is up to date" instead of rebuilding when the music, the firmware and the last scan are unchanged (1.5 s on 4,003 tracks in the emulator; it rebuilt for 34.7 s before).
- Album art: Ogg and Opus pictures, CMYK and YCCK JPEGs and JPEG EXIF orientation are shown; ID3 tags with unsynchronisation are read correctly; embedded art that can't be decoded gives way to the folder image. echo-sync picks the same picture as the player for every case of a 55-album test corpus.
- Cover decoding is about a fifth faster, and long file reads and writes take one card command per 16 KB.
- A scan that stops the player (a fault, a stall, power loss) skips the file it was reading at the next start and lists it in the debug report; after three stops in a row the library is built without covers.
- Long name-sorted lists share Cover Flow's accelerated holds and letter sections, with sort-key folding for accents and “The”, a theme-coloured badge, and clamped section steps that preserve small sections. Short lists keep their ordinary scrolling. Repeated volume input is coalesced and released holds discard stale repeats.
- Home avoids walking every album when the cover database is unavailable.
- Holding Play on an artist or album artist offers Play all and Shuffle albums. Albums follow year/title order or a shuffled album sequence; each album's tracks stay in order. The persistent queue preserves CUE parts, chapters and restart position, up to 65,535 entries.

- Settings → Format card adds a quick FAT32/exFAT format with capacity and erase warnings, a separate confirmation with Cancel selected, playback/USB/battery guards, progress and removal recovery. Formatting clears the card's library and saved playback state. Native sparse images pass macOS fsck and read/write checks; ARM scenarios cover copied WAV playback and removal/retry. Physical card acceptance remains open.

- Search metadata and paths, combine sorting and filters, inspect track information and assign personal mood/activity labels.
- Scoped Play All and selected-result playback preserve the displayed order.
- Waking the player keeps the screen on even when a fresh wake timestamp is newer than the current app step. Screen timeout and auto off also remain correct across clock wraparound.
- Navigation returns to existing checkpoints. Mixtape actions settle at List, modal Back returns to its parent, and hold Back cancels work and reaches Home.
- Full queue editing, journaled resume, named mixtapes, listening history/export and timed sidecar lyrics.
- C60/C90 two-sided tapes have per-side time and song limits, song-list additions, a cassette flip prompt and exact side/entry/position resume. Single-sided tapes retain their existing actions; the catalogue and session formats stay at version 1.
- Listening → Clear history (hold ▶ for options) adds a confirmation with Export plays first, Clear and Cancel. It resets totals, rankings and history-based Smart Lists while keeping saved moments, favorites and exported files. The empty history uses a verified spare counter block so interrupted writes recover the previous or empty history. Player validation remains pending.
- Listening → Moments saves up to 64 labelled positions by path and chapter boundaries, replays them after a restart or rescan, and supports confirmed deletion. Spare-bank writes preserve the published list through interrupted saves.

## Interface and sound controls

- Equalizer → Sound shaping adds seven word-based controls, an eleven-step slider, live response and automatic headroom. Reset keeps any underlying correction; saved EQ slots retain the control positions. Shaping can sit on a saved parametric correction within the ten-filter limit, with an explicit refusal when a change would exceed it. Headroom follows the actual cascade and its transients, avoiding excessive attenuation from multiplying section bounds and adding another margin. Native checks cover mappings, clipping, persistence and layouts; the ARM playback scenario records zero underruns while all seven sliders change. Player listening checks remain pending.

- Parametric EQ adds up to ten Peak, Low shelf or High shelf filters, an adjustable preamp, live response plots and button editing. AutoEQ/Equalizer APO parametric files import alongside GraphicEQ files, and saved slots can hold either kind. Alternating-bank storage keeps the previous parametric copy through interrupted saves. Coefficients use the actual output rate through 96 kHz; USB DAC output remains unchanged. Player listening and restart checks are pending.

- Settings → Now Playing style adds Cassette, VU meters and Spectrum beside Cover. The cassette uses neon wireframe artwork with turning spokes; VU has separate left/right needles and peak lights; Spectrum has sixteen theme-coloured bars and peak caps over the perspective floor. The saved choice survives a restart. Native, simulator and emulator checks cover the interface, analysis and playback load; player acceptance remains open.
- Settings → Date & time edits the local date, hour, minute and UTC offset, writes the clock chip through the extension module and updates the running wall clock immediately after successful readback. Back cancels; a failed save keeps the running clock unchanged. Date and time set on the player now survive restart and power-off: one deferred RTC read restores trusted UTC with the saved offset, with no read during USB or active audio. Later Mac time syncs still replace the running clock and save their offset. Listening timestamps remain UTC. The simulator supports the same setting path; on-player validation remains pending.
- Menu on Now Playing opens Quick Settings: immediate Shuffle, Repeat, EQ preset or saved slot, Headphone profile, Playback speed, Sleep timer, Transitions and ReplayGain changes, plus Lyrics and Up Next. Vol± moves, ▶ changes or opens, and Back closes. Hold Menu still opens Lyrics. Saved changes use the Settings setters; the sleep timer lasts for the running session.
- Playback speed plays 0.5× to 3× without changing the pitch, from Quick Settings or Settings → Playback speed, and is saved in the settings file (extension byte E3). Now Playing shows the speed while it isn't Normal. Positions, seeks, bookmarks and resume stay in the recording's own time; recordings above 48 kHz play at normal speed. Checked with host tests and emulator scenarios only; listening quality, core load and both outputs still need checks on a player ([Playback speed](audio-tempo.md)).
- Startup draws its first frame before loading optional extension artwork, reserving files or checking for music changes. Bluetooth reconnect follows that frame. The developer report keeps boot-phase timestamps for cold-start measurements.
- Settings → Power on adds Press for leaving the charge screen with a tap; Hold remains the default. Completely powered-off startup still requires holding Power until the boot ROM hands control to firmware. These paths have software checks; player timing remains to be measured.
- Settings → DAC filter offers the CS43198 filters FiiO's firmware offers: fast or slow roll-off, low latency or phase compensated, and non-oversampling. Settings → Gain keeps Low and High: High uses FiiO's register value, and Low the data sheet's documented `0x20` instead of FiiO's reserved code. Both now change during playback by muting and, for Gain, powering the outputs down around the change as the DAC's data sheet requires, and choosing High lowers the volume by 6 dB first. Not yet checked on the player.
- Install data-only `.gtheme` packs from the card without reflashing. The Theme picker previews palettes/backgrounds, keeps the selected pack across restarts and falls back to a built-in when it is missing or damaged. RGB565 art streams through the existing strip canvas with a small tile cache. Original Pocket Scope and Desktop 98 source packs, a builder/checker, stable update IDs and optional signature metadata are included. The trusted-key list starts empty; unsigned packs work normally. Software checks cover native validation, streaming and persistence; player acceptance is still pending.
- Plugging in offers the same four modes everywhere, with one name, one picture and one line each: **Connect to computer** (music drives, firmware updates and the companion app; music stops), **Charge & play**, **Run from USB power** (keep listening without charging; a low battery charges first) and **USB audio (DAC)**. Each shows its name with a one-line description under it in the prompt and the menus. The plug-in prompt ("Computer found"), USB mode from Home, Settings or the charge screen, Settings → When plugged in and the DAC menu all use them. Connect to computer is first and highlighted; doing nothing still means Charge & play, with "Charging in 15 s". Holding Play on a choice keeps it for every plug-in. Run from USB power replaces the separate Battery care setting, lasts until unplugging unless saved, and shows a plug instead of the bolt in the status bar and on the charge screen; a saved Run from USB power is announced on any cable. When plugged in can now also start USB audio (DAC) at once. Physical charger/load acceptance of Run from USB power remains open.

- Settings → Clock format selects 12-hour time with AM/PM or 24-hour time for Clock and the status bar, including Calendar. The choice survives restarts.
- Four named headphone profiles apply attenuation-only trim, left/right balance and a volume ceiling to music and USB DAC output. Settings → Headphones selects and edits them; verified alternating-bank saves keep profiles on internal storage apart from the settings file. Switching to less attenuation lowers the saved mode volumes first. Analog Gain is unchanged; physical output checks remain pending.
- The USB screens are redesigned in the theme's style: a wireframe landscape of the last 10 seconds of reads and writes, the transfer rate, the amount read and written, a gauge for each drive with its free space or ejected state, the USB speed and the battery. A turning cassette shows while the player waits for writes to finish or disconnects. The DAC screen adds a level meter, the format, both volumes, the jack, buffer, clock correction, underruns and time connected.
- Connect to computer can be ended from the player: hold Back for a second. The player waits until the computer has stopped writing for 3 seconds, tells it the drives are gone and finishes as after an eject; holding on for 3 more seconds disconnects at once with a warning.
- The DAC menu (Menu in DAC mode) changes volume, gain, output and the headphone profile, or switches to Connect to computer or Charge & play, without leaving DAC mode.
- Crossfeed (Off, Low, Medium, High) blends each channel into the other ear for headphones, as a Settings default and per headphone profile. Stereo music and spoken word only; USB DAC output is unchanged.
- Skip silence shortens pauses in audiobooks and podcasts to a quarter second, and Speech boost evens out their volume without going above full scale. Positions, bookmarks and chapters stay exact. Native tests and emulator scenarios cover them; nothing has been heard on the player yet.
- Five themes with live preview, configurable playback tap/hold actions, shared icons and cassette artwork.
- Flat retro-future headers and focus accents, clearer Listen and charging layouts, a larger battery readout and an honest distinction between USB power, charging and full.
- Theme and EQ browsing preview without saving. Custom EQ has a response curve and staged audible editing; leaving a preview restores the saved choice.
- Existing slides use eased motion. The Home perspective grid stays inside its artwork panel and clear of captions.
- Obsolete Connect USB, USB sense and raw DAC About rows are removed. Hardware identification remains available to the developer HAL.

## Bluetooth

- When Bluetooth switched itself off after 5 idle minutes, or the player has just been turned on, ▶ wakes it and reconnects the last headphones if music last played through Bluetooth: "Connecting to …", then music starts once they connect. After 10 s without them the player stays paused and says so; wired headphones in either jack play at once. Nothing plays to an empty jack while it waits.
- A speaker or headphones that pause the stream as it starts get it started again once, so a connection while playing isn't left silent until play is pressed on the speaker.
- Headphones that come back in range after a loss reconnect without resuming playback.
- The Bluetooth screen says "Off to save battery · Play: reconnect" while Bluetooth is switched off to save battery, and ▶ there reconnects instead of the screen waking Bluetooth on opening.
- Checked with host tests and emulator scenarios only; not yet checked on a player.

## Playback fixes

- Screen-off redraw requests no longer keep the CPU at its busy polling rate. Compressed reads refill the existing ring in bursts, idle timer wakes are less frequent, and paused screen-off standby can coast at 24 MHz. The developer loop can retain minute battery samples for a discharge run or a 24-hour standby comparison. Battery-life gains and changed-cadence playback have not been measured on the player.
- APE: dedicated core-1 decoding, 16/24-bit mono/stereo, Fast through 192 kHz, Normal through 192 kHz mono or 96 kHz stereo, and High through 96 kHz mono or 48 kHz stereo, exact seek-table starts and sample lengths. Fast stereo above 96 kHz and Normal stereo above 48 kHz run core 1 at 200 MHz. Extra High/Insane are refused.
- WMA Standard v1/v2: fixed-point core-1 decoding with checked ASF demuxing, tags/covers, timestamp-guided seeks, priming trim and final-overlap drain. Pro, Lossless, Voice and encrypted files are refused. Encoder padding remains when it is not described by the file.
- Both codecs have native/reference and ARM decoder/player checks. Physical playback and cycle margins remain untested.

- A–B repeat marks source-sample positions from the Now Playing seek cursor’s Menu action and repeats the selected range, including CUE tracks and chapters. Marks clear on track changes and USB handoff. Player listening checks remain pending.

- ReplayGain Auto selects album gain for album-order neighbours and track gain for song shuffle or isolated tracks. The companion supports the existing mode field’s fourth value.

- Sleep timer fades over its final minute and can stop at the end of the current track, chapter or episode. Physical listening and shutdown checks remain pending.

- Opus R128 track and album gain tags now feed ReplayGain normalization with the +5 dB reference conversion. Standard ReplayGain gain tags take precedence, and the decoder keeps applying header gain once.

- Shuffle can fade between songs (Settings → Transitions, 0.5 to 6 s). Album-order neighbours, parts of one file and spoken word stay gapless. Not yet heard on a player.

- Ogg Vorbis compatibility is checked against independently built Xiph and aoTuV encoders and ffmpeg-native output, across VBR qualities −1 to 10, speech rates, 192 kHz stereo and managed 500 kbit/s. Large artwork comments are skipped during decode. Chained files stop at their first link with its correct duration; multiplexed files ignore other checked serials when Vorbis is first.

- The desktop simulator plays Vorbis and Opus with the firmware decoders, including seeking, mono output, Opus gain and end trimming. Core-1 emulation measures about 0.81/1.34/2.31 million instructions per 20 ms for q10 stereo at 48/96/192 kHz. These are software checks; neither codec has been listened to on the player, and actual real-time margin remains unmeasured.

- Output-underrun diagnostics exclude intentional muted DMA pauses, clear stale start/resume flags and retain pending playback faults. Unmuted interruptions remain visible.

- Standard speech rates from 8 to 24 kHz are converted with a fixed-point interpolation filter: 8 and 16 kHz to 32 kHz, 11.025 and 22.05 kHz to 44.1 kHz, 12 and 24 kHz to 48 kHz; 32 kHz uses native output. Seeking and chapter positions stay on the recording clock, with no extra output frames. The filter does about half the multiply work per channel, filters mono once, and core 0 keeps its full clock while converting.

- M4B files at any depth appear on Audiobooks and stay out of Music lists. Named roots merge on the shelf; external chaptered files appear once. Whole-file books retain independent positions and play as one book.
- Audiobook folder selection resumes the saved chapter and position at every folder depth, including author/title layouts and books made of separate files. Hold Play still opens chapters; selecting a paused loaded book continues its stream.

- Desktop indexing and echo-sync preserve `.m4b` chapter tracks and book folders from Nero and QuickTime chapter metadata, matching the player scanner. Malformed chapter boxes retain valid text-track or whole-book fallbacks.

- MP3 encoder end padding is retained until actual end of stream and removed after approximate seeking or part-way resume as well as a normal start.
- AAC/ALAC presentation trim reads a selected sound track's single unity-rate MP4 edit. Valid `iTunSMPB` data takes precedence; malformed tags retain the edit fallback.
- Independent movie/media clocks and 64-bit edit entries are converted to output samples with checked arithmetic. Codec configuration supplies packet boundaries.
- The DAC power-up wait is 12 ms, matching the observed stock sequence. Earlier owner listening checks reported quiet playback; no analog noise specification is claimed.

## Architecture and validation

- USB status drawing now loads with the verified USB overlay. Connecting and charging without the settings artwork keep a resident text fallback. The renderer is cleared before the overlay memory is reused.

Demand-loaded screen modules, an independent audio module for interpolation and M4A demuxing, a shared overlay loader, USB storage/DAC/update code in an on-demand overlay, and per-image size checks keep the device memory limits explicit. The developer loop provides reports, scripts, screenshots, profiling and file-copy updates.

Software checks include native playback comparisons, the simulator, exact ARM decoder comparisons, boot/update scenarios and persistence regressions. See [`known-issues.md`](known-issues.md) for the separate device-validation scope and remaining codec, audio and hardware limits.
