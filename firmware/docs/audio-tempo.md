# Pitch-preserving tempo kernel

`device/audio_tempo` is the fixed-memory integer time-stretching kernel in audio processing module 7. Playback and Settings do not call it yet; the player still plays at its original speed.

## Contract

The kernel accepts signed 24-bit stereo PCM at 8–48 kHz and integer speeds from 50 through 200 percent. A speed of 100 copies every sample unchanged. Invalid rates, speeds, overlapping spans, spans overlapping the control object and insufficient storage are rejected before the current state changes. Input outside the signed 24-bit range is rejected without consuming a frame. Zero-initialized state can be drained or finished safely.

The caller provides one or two disjoint byte spans. Together they must hold `2 * floor(rate / 16) + 2` frames, each six bytes: three little-endian bytes per channel. This is 6,012 bytes at 8 kHz and 36,012 bytes at 48 kHz, plus the control object. Either span can be empty. Ring wrapping and a split inside the window do not change PCM. The kernel allocates no memory and keeps no global mutable state.

`push` accepts one frame or reports backpressure. `pull` returns one frame when enough source audio is available, leaving output arguments unchanged otherwise. Drain before retrying a blocked push. `finish` ends input; continue pulling until `done`. The final output count is the nearest integer to `input_frames * 100 / speed`, with half frames rounded upward. After completion, pulls produce no more samples and pushes are refused. Restarting with a valid configuration clears the stream state.

## Processing

The method follows the pitch-period insertion/removal approach used by [Sonic](https://github.com/waywardgeek/sonic), by Bill Cox. Gnarlyr uses its own integer ratio accounting, packed 24-bit storage and two-span streaming interface.

A bounded average-magnitude-difference search considers periods corresponding to 16–400 Hz. Coarse and fine searches use phase-varied sampled points to avoid periodic decimation aliases; neighboring full-period scores estimate fractional lag. Fractional error accumulates across splices so rounding does not repeatedly shift a steady tone in the same direction. Reanalysis is due after ten milliseconds of consumed source audio; the current splice completes before a new analysis is applied. Stereo differences are summed separately, so opposite-phase channels cannot cancel the analysis signal.

Faster playback overlaps adjacent periods and consumes both; slower playback inserts an overlapped period. Direct copies between splices maintain the requested ratio, carrying fractional copy counts into later jobs. Crossfades use Q32 reciprocal weights and symmetric integer rounding. They stay within the input sample range; reciprocal approximation differs from exact rational interpolation by at most eight signed 24-bit sample units for the bounded periods. Each accepted source frame earns exact rational output credit; pulls need no wide duration division. At EOF, the short remaining tail is copied and the last retained period can repeat to complete the requested duration.

The ARM probe uses the last 32,000 bytes of the existing seven-block PCM ring for most of the workspace and the processing-bank tail for the remainder. Live integration must exclude those two blocks from DMA while stretching, retaining five blocks (10,000 stereo output frames, about 208 ms at 48 kHz). Normal-speed and USB output retain the full ring. The processor's non-clearing validation checks the header, running-code binding and body CRC while preserving caller workspace; a fresh load clears the module tail. The owner must reset the tempo stream before reusing workspace after an ownership change.

The kernel keeps source/output counts separate. Playback integration must preserve source-time seek/resume positions, handle queued boundaries and rate changes, and invalidate workspace before USB or rescan overwrites the processing slot. Higher input rates, live speed changes and actual player listening/throughput remain integration work.

## Checks

Native checks cover exact unity, all 151 integer speeds, rounded duration, signed extremes, opposite-phase stereo, chunk invariance, short and empty streams, invalid configuration, two-span boundaries, pitch changes, mixed-tone spectral peaks and backpressure. A 160-case steady-tone matrix including 20–40 Hz bass checks frequency independently of the implementation. The ARM image probe loads the checked module, places caller buffers in a partition of the PCM ring and the processing-bank tail, compares every output byte with the native tool and guards the stack and module code. Instruction counts from that probe are ISA observations; hardware cycles and listening quality need measurement on the player.
