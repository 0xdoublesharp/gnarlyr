# Monkey's Audio playback

APE decoding uses a separate core-1 program built from the GPL-2.0-or-later Rockbox libdemac revision `a587ee311c39f9a999a944a726f3406ffb726821`. Notices and local changes are recorded in `third_party/demac`. It uses portable fixed-point arithmetic, fixed buffers and no heap.

The checked file reader accepts APE versions 3.97–3.99, 16/24-bit mono/stereo and 8–192 kHz header rates. It reads seek-table entries from the file, validates their bounds and increasing order, and starts decoding at the frame containing a requested sample. The existing audio source trims decoded output to that exact sample. APE has an exact sample count and no encoder delay or padding; the source exposes that count for gapless queueing. Device listening remains untested.

Audio module 13 contains the APE probe, seek and file adapter together with ASF/WMA file operations and the shared resampling entry. It occupies the same 8 KiB audio bank as MP4 and Ogg, with zero BSS and caller-owned state in the existing decoder workspace. Resident code only selects the program and dispatches the module's open/read calls. Audio-bank resampler preparation preserves a loaded APE module, and read dispatch checks its identity before calling into it.

The stream begins with six little-endian words: version, level, bits, channels, rate and reserved zero. Each frame has four little-endian words: compressed byte count, sample count, leading alignment bytes to skip and first sample. The compressed bytes include the preceding partial word and one look-ahead word. The final frame can use zero-filled look-ahead beyond EOF. A zero byte count ends the stream; fill requests beyond that marker receive zeros. Failed storage reads are sticky errors, never clean end markers.

Core 1 copies input from its 64 KiB ring into a private 16 KiB snapshot in the upper ring area, then decodes at most 256 samples per block. It refills only when fewer than 8 KiB remain. The entropy reader refuses to cross the snapshot, bounds range symbols and shift counts, and exits through a failure checkpoint on malformed input. Frame CRCs are checked against decoded PCM. Track-generation checks in the shared stream discard output when a track changes during decode. The linker reserves 16 KiB of stack and bounds static state to 36 KiB.

The core-1 image occupies a checked `EC1M` bank at `0x300600`, immediately after the DSD bank. Its maximum code size is 128 KiB; glyphs, language packs and modules are allocated outside the reserved bank. Existing codec offsets and firmware-slot write ranges are unchanged.

Native tests compare every decoded sample with deterministic input independently verified by ffmpeg, exercise all compression levels and depths, seek-table restarts, fragmented file transport, starvation and corrupt data. Source tests exercise the real shared ring, exact sample lengths, start trimming and file closure. The Cortex-M3 test repeats the reference comparisons and reports instructions per 20 ms. A two-core player scenario opens through the library at a saved position and checks program/module selection and resampling.

## Instruction budget and admitted streams

Core 1 starts every program at 100 MHz (2,000,000 cycles per 20 ms) and may request 200 MHz (4,000,000) as described in [Calculation-core clock](core1-clock.md). A stream is admitted only when its measured cost stays below 80% of the ideal budget at the clock it uses, the same reserve DSD and Vorbis keep. `ape_track_clock` gives that clock, or zero for a refused stream; the opener in module 13 refuses a stream before publishing a generation when the clock is zero or above the PLL that the audio HAL holds while decoding (`ape_track_realtime`). The core-1 program applies the same table to the validated stream header and requests 200 MHz only for the boosted rows; it returns to 100 MHz while idle and on every new stream. Both 16-bit and 24-bit files use these bounds.

| Compression | Mono | Stereo at 100 MHz | Stereo at 200 MHz |
| --- | ---: | ---: | ---: |
| Fast | 192 kHz | 96 kHz | 192 kHz |
| Normal | 192 kHz | 48 kHz | 96 kHz |
| High | 96 kHz | 48 kHz | 48 kHz |
| Extra High / Insane | Refused | Refused | Refused |

Mono streams never need the boost. Fast stereo above 96 kHz (2.31 million instructions per 20 ms at 192 kHz) and Normal stereo above 48 kHz (1.84 million at 96 kHz) request 200 MHz. Normal stereo at 192 kHz (3.67 million) and High stereo at 96 kHz (3.22 million) exceed the 3.2 million reserve screen even at 200 MHz and are refused, as are High mono above 96 kHz, which has no measurement. Extra High and Insane remain refused until explicitly admitted by a measured profile. The decoder harness can still exercise refused rates and levels for measurement.

| Fixture | Core-1 instructions per 20 ms |
| --- | ---: |
| Fast, 16-bit stereo, 48 kHz | 462,969 |
| Normal, 16-bit stereo, 48 kHz | 732,031 |
| High, 16-bit stereo, 48 kHz | 1,180,938 |
| Extra High, 16-bit stereo, 48 kHz | 4,128,281 |
| Insane, 16-bit stereo, 48 kHz | 22,449,062 |
| Fast, 24-bit stereo, 192 kHz | 2,307,500 |
| Normal, 24-bit stereo, 192 kHz | 3,671,875 |
| High, 24-bit stereo, 96 kHz | 3,218,750 |
| Normal, 24-bit mono, 192 kHz | 1,530,625 |
| Fast, 24-bit stereo, 96 kHz | 1,153,750 |
| Normal, 24-bit stereo, 96 kHz | 1,835,938 |
| High, 24-bit mono, 96 kHz | 1,406,250 |

Counts include CRC, copying and transport. They are measured on deterministic fixtures, not a bound for every recording. Extra High and Insane exceed even an ideal one-cycle-per-instruction budget at 48 kHz. Normal 24-bit stereo at 192 kHz and High stereo at 96 kHz exceed the reserve at 200 MHz and are refused. Admitted high-rate streams still need player cycle/load and listening checks: loads, multiplies, branches, memory waits and storage contention consume additional cycles. Player testing is outside this change.
