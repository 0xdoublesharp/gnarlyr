# Gnarlyr release notes

Gnarlyr builds on Podium 4.5.9 and EchoPod with their GPL credits preserved. The source provenance is recorded in [`../UPSTREAM.md`](../UPSTREAM.md). Version 1.0.0 is not released; hardware checks are recorded in [`known-issues.md`](known-issues.md).

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


## USB DAC

- High-speed UAC2 adds stereo 16/24/32-bit PCM at 44.1, 48, 88.2, 96, 176.4 and 192 kHz. Full-speed UAC1 retains 16/24-bit at 44.1/48 kHz.
- Explicit feedback follows the player’s clock without dropping or repeating samples in UAC2. The USB overlay ring targets 5 ms of device buffering (17,600 bytes in device, 24,000 in release); headphone trim/balance and DAC volume remain available. An empty USB stream is cleared and muted before it reprimes.
- ▶ sends host play/pause; 400 ms Vol+ and Vol− holds send next and previous. Volume taps change the local DAC volume.
- Native descriptor, clock-drift and PCM tests and ARM streaming scenarios cover software behavior. Physical host compatibility, clock accuracy, both outputs, remote keys and hardware CPU margin remain open in the [host acceptance checks](known-issues.md#usb-dac-host-acceptance).

## Identity and files

The canonical firmware name is **Gnarlyr**, made by **doublesharp**. The image identifier and USB storage vendor are `GNARLYR`; the update file is `GNARLYR.IMG`, the data directory is `/.gnarlyr`, and exported CUE playlist entries use `#GNARLYR-START:`. `identity.json` supplies the build, simulator and host tools.

The binary library, settings, saved-queue, mixtape and listening formats keep their maker-specific `DS` markers. Moving an installed developer build to the canonical identifiers requires the prepared update/data transition so its existing state is preserved.

## Library and navigation

- Home defaults to Music, Mixtapes, Stats, USB Mode, Clock and Settings. A loaded track adds Now playing at the top, including while paused; the artwork caption shows the track title when space allows. Audiobooks and Podcasts are reachable from Music when their shelves exist; they and Up Next can be shown through Settings → Home screen. The editor has nine items; Now playing is automatic. Existing untouched Home defaults migrate on load, while customised orders and hidden masks are kept. The settings v15 layout is unchanged.
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
- Settings → Date & time edits the local date, hour, minute and UTC offset, writes the clock chip through the extension module and updates the running wall clock immediately after successful readback. Back cancels; a failed save keeps the running clock unchanged. Date and time set on the player now survive restart and power-off: one deferred RTC read restores trusted UTC with the saved offset, with no read during USB or active audio. Later Mac time syncs still replace the running clock and save their offset. Listening timestamps remain UTC. The simulator supports the same setting path; on-player validation remains pending.
- Menu on Now Playing opens Quick Settings: immediate Shuffle, Repeat, EQ preset or saved slot, Headphone profile, Sleep timer, Transitions and ReplayGain changes, plus Lyrics and Up Next. Vol± moves, ▶ changes or opens, and Back closes. Hold Menu still opens Lyrics. Saved changes use the Settings setters; the sleep timer lasts for the running session.
- Startup draws its first frame before loading optional extension artwork, reserving files or checking for music changes. Bluetooth reconnect follows that frame. The developer report keeps boot-phase timestamps for cold-start measurements.
- Settings → Power on adds Press for leaving the charge screen with a tap; Hold remains the default. Completely powered-off startup still requires holding Power until the boot ROM hands control to firmware. These paths have software checks; player timing remains to be measured.
- Settings → DAC filter offers the CS43198 filters FiiO's firmware offers: fast or slow roll-off, low latency or phase compensated, and non-oversampling. Settings → Gain keeps Low and High with FiiO's register values. Both now change during playback by muting and, for Gain, powering the outputs down around the change as the DAC's data sheet requires, and choosing High lowers the volume by 6 dB first. Not yet checked on the player.
- Plugging in offers the same four modes everywhere, with one name, one picture and one line each: **Connect to computer** (drives for copying music, updates and the Gnarlyr app; music stops), **Charge**, **Play on USB power** (keep listening without charging; a low battery charges first) and **USB DAC**. The plug-in prompt ("Computer found"), USB mode from Home, Settings or the charge screen, Settings → When plugged in and the DAC menu all use them. Connect to computer is first and highlighted; doing nothing still means Charge, with "Charging in 15 s". Holding Play on a choice keeps it for every plug-in. Play on USB power replaces the separate Battery care setting, lasts until unplugging unless saved, and shows a plug instead of the bolt in the status bar and on the charge screen; a saved Play on USB power is announced on any cable. When plugged in can now also start the USB DAC at once. Physical charger/load acceptance of Play on USB power remains open.

- Settings → Clock format selects 12-hour time with AM/PM or 24-hour time for Clock and the status bar, including Calendar. The choice survives restarts.
- Four named headphone profiles apply attenuation-only trim, left/right balance and a volume ceiling to music and USB DAC output. Settings → Headphones selects and edits them; verified alternating-bank saves keep profiles on internal storage without changing settings v15. Switching to less attenuation lowers the saved mode volumes first. Analog Gain is unchanged; physical output checks remain pending.
- The USB screens are redesigned in the theme's style: a wireframe cassette whose reels turn while the computer reads or writes, a state light, the amount copied, each drive's size or ejected state, the USB speed and the battery. The DAC screen adds a level meter, the format, both volumes, the jack, buffer, clock correction, underruns and time connected.
- Connect to computer can be ended from the player: hold Back for a second. The player waits until the computer has stopped writing for 3 seconds, tells it the drives are gone and finishes as after an eject; holding on for 3 more seconds disconnects at once with a warning.
- The DAC menu (Menu in DAC mode) changes volume, gain, output and the headphone profile, or switches to Connect to computer or Charge, without leaving DAC mode.
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
