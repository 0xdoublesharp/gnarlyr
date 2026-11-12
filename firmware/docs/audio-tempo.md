# Playback speed

Playback speed changes how fast a recording plays without changing its pitch. The choices are 0.5×, 0.75×, 1×, 1.25×, 1.5×, 1.75×, 2×, 2.5× and 3×. Settings → Playback speed and the Playback speed row of Quick Settings (tap Menu on Now Playing; Play steps to the next speed) set it through the same `settings_action_*` functions; the choice is saved and applied at start.

## Kernel

`device/audio_tempo` is a fixed-memory integer time-stretching kernel. It accepts signed 24-bit stereo PCM at 8–48 kHz and integer speeds from 50 through 300 percent. A speed of 100 copies every sample unchanged. Invalid rates, speeds, overlapping spans, spans overlapping the control object and insufficient storage are rejected before the current state changes. Input outside the signed 24-bit range is rejected without consuming a frame. Zero-initialized state can be drained or finished safely.

The caller provides one or two disjoint byte spans. Together they must hold `2 * floor(rate / 16) + 2` frames, each six bytes: three little-endian bytes per channel. This is 6,012 bytes at 8 kHz and 36,012 bytes at 48 kHz, plus the control object. Either span can be empty. Ring wrapping and a split inside the window do not change PCM. The kernel allocates no memory and keeps no global mutable state.

`push` accepts one frame or reports backpressure. `pull` returns one frame when enough source audio is available. Drain before retrying a blocked push. `finish` ends input; continue pulling until `done`. The final output count is the nearest integer to `input_frames * 100 / speed`, with half frames rounded upward.

The method follows the pitch-period insertion/removal approach used by [Sonic](https://github.com/waywardgeek/sonic), by Bill Cox. Gnarlyr uses its own integer ratio accounting, packed 24-bit storage and two-span streaming interface. A bounded average-magnitude-difference search considers periods corresponding to 16–400 Hz, with phase-varied sampled points, a fractional-lag estimate carried across splices and reanalysis after ten milliseconds of consumed source audio. Stereo differences are summed separately, so opposite-phase channels cannot cancel the analysis signal.

Slower playback inserts an overlapped period. Up to 2×, faster playback overlaps two adjacent periods and consumes both, with direct copies between splices carrying fractional counts. Above 2×, as in Sonic, the overlap is shortened to `period × 100 / (speed − 100)` frames and consumes a period plus that length, so 3× blends half a period and skips one and a half. Crossfades use Q32 reciprocal weights and symmetric integer rounding and stay within the input sample range. Each accepted source frame earns exact rational output credit. At the end of a stream the remaining tail is copied and the last period can repeat to complete the requested duration.

## Block stage and engine integration

`device/audio_tempo_stage` wraps one kernel stream for the engine with block calls: start, push a block of interleaved frames, pull a block (optionally shifted into 24-bit ring words), finish, and `pending(input)`, the number of output frames still to come once `input` more source frames arrive. The stage's control object and a 4,014-byte spare span live in processing module 7's BSS; the rest of the workspace is the last two blocks of the seven-block PCM ring. A fresh module load clears the BSS, and the validation of an already loaded copy leaves it in place.

The engine stretches decoded PCM at its own rate, after volume, ReplayGain and the headphone profile and before the 32/44.1/48 kHz interpolator, so the interpolator never computes frames that faster playback would drop. `device/audio_engine_stage` (also in module 7) drains the stage into the ring directly at native rates or through the interpolator otherwise, then pushes the next converted block. While stretching, `config.ring_words` is five blocks (10,000 frames, about 208 ms at 48 kHz) and DMA loops over those five; the reserved two blocks are never written by the output side. Normal speed and the USB DAC use all seven. Rates above 48 kHz play at normal speed with the full ring; any change into or out of such a rate also changes the output clock, so DMA restarts and the ring size never changes under a running transfer.

Positions, seeks, ranges, bookmarks and the resume file stay in source time: each segment records the speed it was stretched at and converts consumed output frames back. A seek, a speed change and a new track restart the stage. Queued files at the same input rate and adjacent chapters continue in the same stage; the next segment starts after the stage's and interpolator's pending output. A change of input rate on the same output clock drains the stage and the interpolator, then restarts both. The end-of-track prediction for fades counts audio inside the stage at the current speed (see Track transitions in [architecture](architecture.md)). Stretching keeps core 0 at the full clock.

The simulator runs the same kernel between its decoder and the SDL stream; positions there are the decoder's, already in source time. The listening log counts wall time while the play moves forward and converts the remaining source time to wall time when a track ends.

## Checks

Native kernel checks cover exact unity, every integer speed from 50 to 300 with its rounded duration, signed extremes, opposite-phase stereo, chunk invariance, short and empty streams, invalid configuration, two-span boundaries, pitch changes, mixed-tone spectral peaks and backpressure; a 224-case steady-tone matrix (8–48 kHz, 20 Hz–1 kHz, 0.5× to 3×) measured at most 0.16 % frequency error. Stage checks compare block push/pull with the frame kernel at every rate and speed and check `pending`. Engine checks compare stretched native and converted output with an independent kernel-plus-interpolator rendering, durations, source-time positions, seeks, queued boundaries, input-rate changes, the reserved ring tail and rates above 48 kHz. The ARM probe drives the stage in the checked module image and compares every output byte with the native kernel.

Emulator scenarios measure core 0 with the image's own pump while playing (one cycle per emulated instruction, no output underruns at any listed speed). FLAC frames are decoded by the core-1 program; the cooperative bridge runs it for 128 slices of 1,000 instructions per fill in these scenarios so that core 1, whose throughput the emulator does not model, never limits the measurement of core 0:

| Source | 1× | 1.5× | 2× | 3× |
|---|---|---|---|---|
| 44.1 kHz stereo FLAC | 20.3 % | 27.3 % | 37.6 % | 56.3 % |
| 16 kHz mono speech, 32 kHz output | 27.9 % | 26.7 % | 37.0 % | 46.2 % |

Speech at 1× coasts at half clock, which is why its share is higher than at 1.5×. The emulator does not reproduce the player's cycles per instruction; core load, listening quality and both outputs need checks on the player.
