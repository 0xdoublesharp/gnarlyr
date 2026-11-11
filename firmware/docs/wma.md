# WMA Standard playback

WMA v1/v2 (`0x0160` and `0x0161`) decode in a separate core-1 program. The fixed-point sources are pinned to Rockbox revision `a587ee311c39f9a999a944a726f3406ffb726821`; provenance, GPL/LGPL/BSD notices and port changes are in `third_party/rockbox_wma`. Mono and stereo at 8–48 kHz are accepted within the transport bounds. WMA Pro, Lossless, Voice and encrypted streams are refused at open with a WMA Standard message.

## ASF and source positions

Audio module 13 contains the checked ASF object walk, packet parser, object reassembly and WMA source opener together with APE file operations and the common resampling entry. It has zero BSS and shares the existing 8 KiB audio bank with MP4, Ogg and APE. Its caller-owned state fits the existing decoder workspace. Resident code selects the program and dispatches open/read calls; it keeps no file-adapter function pointer into an evicted module.

The parser validates object sizes and 64-bit bounds before narrowing offsets, file/data packet counts, stream identity, subtype and configuration. It accepts fixed-size ASF packets up to 16 KiB, media objects up to 16 KiB, and codec packets up to 4 KiB. It handles multiple payloads, fragmented objects and compressed subpayloads; an incomplete object or storage failure is a sticky error. Other stream payloads are skipped. ASF metadata and `WM/Picture` covers continue through the existing library tag reader.

Seeking searches fixed-size packet send timestamps, starts at least 512 ms early with an additional packet of pre-roll, skips an initial incomplete object, and anchors the first complete object's presentation timestamp to the codec frame grid. Presentation timestamps include the ASF pre-roll offset; packet send times do not. Core 1 decodes pre-roll and the source trims output to the requested sample. Generated 44.1 kHz files match a full reference decode at the requested position. When a selected object has no presentation timestamp, seeking decodes from the beginning to preserve sample positions; this takes longer on long recordings.

ASF's duration is rounded to milliseconds. The probe rounds it to a codec-frame count and reports the decoded length after priming. The decoder discards two priming frames and drains the final overlap at clean end, matching FFmpeg's frame accounting. The source itself ends on the checked transport marker. Files without explicit encoder padding information retain their encoded padding; seamless joins of arbitrary WMA recordings cannot be inferred from the ASF duration alone. Queued fixture tracks add no extra engine frames or gap.

## Core-1 layout and protocol

The image reserves a 128 KiB `EC1M` bank at `0x300600`, after APE. Existing codec offsets stay fixed, and glyph/module allocation excludes both new banks. The program's static RAM limit is 40 KiB, below a 12 KiB stack reserve. Its 8 KiB input ring starts at `0x01040000`; fixed work arrays occupy the remaining 120 KiB through `0x01060000`, with a separate linker bound and size-report row. Mutable decoder state is initialized on every generation.

The stream begins with six little-endian words: codec ID, channels, rate, bitrate, block alignment and retained extra-data length; six extra-data bytes and two reserved bytes complete its 32-byte configuration. Each codec packet has a four-byte length prefix. A zero length ends the stream. The file adapter pads later fill reads with zeros while preserving storage errors.

Core 1 snapshots each packet privately before releasing ring space. Empty-output reservoir packets release input space too, so they cannot deadlock the producer. Shared stream generation checks suppress stale PCM. Output is planar signed 29-bit PCM in blocks of at most 2,048 frames; the common engine converts it to the output scale. The port uses fixed buffers and iterative VLC setup. It has no heap, recursive table construction or VLAs. CORDIC windows and noise data are read-only.

## Checks and instruction cost

Native and Cortex-M3 tests compare against independent ffmpeg decoding: exact lengths, at most 12 LSB16 difference for 16 kHz mono and 2 LSB16 for stereo. Native ASF packet bytes match ffmpeg's demux output, including codec objects split across 256-byte ASF packets and compressed subpayloads. Compressed-payload presentation timestamps have a seek-anchor regression. The host sanitizer exercises 256 deterministic packet mutations with storage canaries. Source tests cover ring starvation, seeks, storage errors and queued-track boundaries. A two-core player scenario opens a WMA file at a saved position through the real library, source module and ARM decoder.

| Fixture | Core-1 instructions per 20 ms |
| --- | ---: |
| WMA v1 mono, 16 kHz, 32 kbit/s | 199,167 |
| WMA v2 stereo, 48 kHz, 128 kbit/s | 464,403 |
| WMA v2 stereo, 44.1 kHz, 96 kbit/s | 306,523 |
| Same stereo fixture after seeking | 318,496 |

Core 1 runs at 100 MHz, with 2,000,000 cycles per 20 ms. Counts include setup, transport and copying. They measure instructions, not hardware cycles. Playback on both outputs, cycle margin, busy-card behavior and gapless listening still need checks on the owner’s player.
