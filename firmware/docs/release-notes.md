# Gnarlyr release notes

Gnarlyr builds on Podium 4.5.9 and EchoPod with their GPL credits preserved. The source provenance is recorded in [`../UPSTREAM.md`](../UPSTREAM.md). Version 1.0.0 is not released; hardware checks are recorded in [`known-issues.md`](known-issues.md).

## Identity and files

The canonical firmware name is **Gnarlyr**, made by **doublesharp**. The image identifier and USB storage vendor are `GNARLYR`; the update file is `GNARLYR.IMG`, the data directory is `/.gnarlyr`, and exported CUE playlist entries use `#GNARLYR-START:`. `identity.json` supplies the build, simulator and host tools.

The binary library, settings, saved-queue, mixtape and listening formats keep their maker-specific `DS` markers. Moving an installed developer build to the canonical identifiers requires the prepared update/data transition so its existing state is preserved.

## Library and navigation

- Search metadata and paths, combine sorting and filters, inspect track information and assign personal mood/activity labels.
- Scoped Play All and selected-result playback preserve the displayed order.
- Waking the player keeps the screen on even when a fresh wake timestamp is newer than the current app step. Screen timeout and auto off also remain correct across clock wraparound.
- Navigation returns to existing checkpoints. Mixtape actions settle at List, modal Back returns to its parent, and hold Back cancels work and reaches Home.
- Full queue editing, journaled resume, named mixtapes, listening history/export and timed sidecar lyrics.

## Interface and sound controls

- Settings → Clock format selects 12-hour time with AM/PM or 24-hour time for Clock and the status bar, including Calendar. The choice survives restarts.

- Five themes with live preview, configurable playback tap/hold actions, shared icons and cassette artwork.
- Flat retro-future headers and focus accents, clearer Listen and charging layouts, a larger battery readout and an honest distinction between USB power, charging and full.
- Theme and EQ browsing preview without saving. Custom EQ has a response curve and staged audible editing; leaving a preview restores the saved choice.
- Existing slides use eased motion. The Home perspective grid stays inside its artwork panel and clear of captions.
- Obsolete Connect USB, USB sense and raw DAC About rows are removed. Hardware identification remains available to the developer HAL.

## Playback fixes

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
