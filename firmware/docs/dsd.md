# DSF and DFF PCM playback

The dedicated core-1 DSD program converts uncompressed DSF and DSDIFF to 88.2 kHz
signed 24-bit PCM. DoP is not implemented. The file adapter accepts mono/stereo
DSD64/DSD128 and mono DSD256 for playback. Stereo DSD256, other rates, more than
two channels, DST compression and files beyond the 32-bit file interface are
refused. Container recognition and the host/reference decoder also handle
stereo DSD256 so its cost can be measured without admitting it for playback.

These are software results. No DSD or AIFF image from this change was installed
on a player. Listening, physical CPU margin, storage contention and power use
remain unchecked. Instruction counts do not establish physical real-time playback.

## Containers and metadata

The checked reader follows [Sony's DSF specification](https://dsd-guide.com/sites/default/files/white-papers/DSFFileFormatSpec_E.pdf)
and [Philips' DSDIFF 1.5 specification](https://www.sonicstudio.com/pdf/dsd/DSDIFF_1.5_Spec.pdf).
DSF stores 4,096 bytes per channel per block, with either LSB-first (`bits=1`) or
MSB-first (`bits=8`) samples. Its declared bit count excludes the final block's
padding. DFF stores MSB-first channel bytes interleaved in its uncompressed DSD
chunk. Header/chunk lengths, channel counts, compression, sample counts and data
bounds are checked before a generation is published. The parser does not
interpret DST or multichannel layouts.

DSF uses the ID3 pointer from its header. DFF uses ID3 and, for missing artist or
title fields, DIIN's DIAR/DITI text. An explicit ID3 field wins regardless of
chunk order. ID3 APIC artwork follows the existing cover pipeline. echo-sync's
indexer has the same DIIN fallback; cover-export's Mutagen path reads APIC in
both containers. Generated native and Python fixtures cover the tag/art paths.

## Filter and level

The first stage is a fifth-order CIC FIR, the convolution of five unit-gain
boxes of length R, where R is 16, 32 or 64 for DSD64, DSD128 or DSD256. It
converts to 176.4 kHz. Its lengths are 76, 156 and 316 DSD samples. Byte lookup
tables evaluate that FIR directly; there are no wrapping integrator states.
Coefficients are exact integers with DC sums R^5 (2^20, 2^25 and 2^30). The
result is represented at Q28 for the following stage.

A symmetric 64-tap FIR compensates CIC passband droop and decimates by two to
88.2 kHz. Each rate has its own Q30 coefficients and exact unity DC sum. The
coefficient generator integrates the inverse CIC response up to 0.17 cycles per
176.4 kHz input sample (29,988 Hz), then applies a Kaiser window with beta 8.6.
The folded kernel uses 32 signed 64-bit multiply-accumulates per output frame
and channel. `python3 tools/firmware.py regen --jobs 2` regenerates its tables.

Numeric checks require less than 0.01 dB combined passband error through 20 kHz,
every first-stage alias into 0–20 kHz below -85 dB, and the second stage's
44.1–88.2 kHz stopband below -85 dB. These checks include quantized FIR
coefficients. The native double-precision reference independently convolves
five boxes and integrates the compensation response; sine, sweep, alternating
and 0x69 silence, overload and partial-byte fixtures agree within 32 units at
24-bit scale. ARM output matches native output exactly on generated fixtures.

DSD's conventional reference peak is -6.02 dB relative to the full +/-1 bit
stream. A factor of two restores that reference peak to PCM full scale. All
arithmetic retains headroom through filtering; the final conversion rounds and
saturates to signed 24 bits. Normal in-band signals at or below the DSD reference
level receive that gain. Above-reference peaks or pathological bit patterns can
saturate; they never wrap. Saturation is not an assertion that arbitrary DSD
material can receive +6.02 dB without clipping.

Sixteen initial PCM frames are discarded and mathematical zero padding flushes
the tail. The output has exactly ceil(bit_count / decimation) frames, including
partial bytes; DSF's stored padding never becomes negative full-scale audio.
The compensated delay leaves a fixed advance of 1.5 DSD clocks (at most 0.532
microseconds), shared by full-file and seeked output.

## Transport, seeking and queueing

Audio module 10 owns container probing, opening and the virtual stream reader.
Resident code has only dispatch/wrappers and caller-owned state; the extension,
DSP and Ogg modules gain no DSD code. The virtual stream is a 32-byte little-endian
header (`DSDP`, DSD rate, channels, layout, 64-bit remaining bit count, first PCM
frame and DSF block size), followed by the original channel data. Layout 0 is
DFF, 1 is LSB-first DSF and 2 is MSB-first DSF.

Core 1 snapshots complete channel blocks before releasing their ring bytes and
checks generations after reads. A starved read retains its staged output;
reopening resets history and rejects old generations. Storage failure remains
an error, not a clean end. The 32 KiB input ring and 50,304-byte lookup/history/
packet workspace fit the existing 128 KiB ring area. Static state is separate
and the linker retains a 16 KiB stack reserve.

Core 1 publishes 4,096-frame PCM blocks (46 ms at 88.2 kHz) into the two shared
output slots. Core 0 takes at most one decoded block per audio service, so the
block length bounds how fast it can drain core 1. With the earlier 512-frame
blocks, the owner's player running build `405d960` took 312 blocks in a 3 s
window of a stereo DSD64 sine (about 104 per second, 60% of real time), with
an empty output buffer, 590 engine underruns and core 1 busy 57.5% of the time:
core 1 rested with both slots full while core 0 starved. At 4,096 frames a
service takes 46 ms of audio, beyond the 20 ms core 1 rests while its slots
are full.

`device_image_boot_test_dsd64_stereo_keeps_up_with_the_players_audio_service_cadence`
models that hand-off. It lets the image's audio service run at most once per
10 ms, gives core 1 a time-based budget of 150 instructions per microsecond
(more than this stream needs, so decoding cost is not the limit) and analyses
the frames the output DMA plays. With 512-frame blocks the 1 kHz level falls
from 0.35 to 0.25 within 0.25 s; with 4,096-frame blocks every window stays at
the reference. The emulator does not model core 1's SysTick rests or real
cycle costs.

A seek starts at least 40 PCM frames before its target, rounded down to a whole
DSF channel-block pair or a DFF PCM-frame boundary. Normal decoding rebuilds all
history, and the source discards frames before the requested global PCM
position. Seeks and end positions match the native reference. Queued accepted
tracks share the 88.2 kHz output clock, have no inserted output frames and use
one DMA start in the ARM DSF-to-DFF scenario. Separate files zero-pad their own
filter endpoints; continuity of one filter across files is not implemented.

The core-1 bank begins at `0x2E0400`, immediately after Opus, with the normal
512-byte EC1M length/CRC header and a 128 KiB payload capacity. Glyphs, languages
and modules are allocated after that bank. Other core-1 offsets are unchanged.

## Instruction limit

Core 1 starts at 100 MHz and requests 200 MHz for all accepted DSD except mono
DSD64. The [clock reference](core1-clock.md) records the hardware/stock basis,
independent calculation divider and idle clock reduction. A 20 ms interval has
two million cycles at 100 MHz or four million at 200 MHz.

`core1_dsd_decodes_in_emulator` compares generated DSF/DFF PCM with the native
reference and measures all three DSD rates. The following are the larger DSF/DFF
steady means and normalized full 4,096-frame block peaks, excluding setup and the
partial final block:

| Stream | MHz | Mean instructions / 20 ms | Peak / 20 ms | Peak ideal budget used | Playback |
|---|---:|---:|---:|---:|---|
| DSD64 mono | 100 | 1,298,244 | 1,304,051 | 65.2% | Accepted |
| DSD64 stereo | 200 | 1,928,655 | 1,938,850 | 48.5% | Accepted |
| DSD128 mono | 200 | 1,686,086 | 1,692,079 | 42.3% | Accepted |
| DSD128 stereo | 200 | 2,528,170 | 2,538,765 | 63.5% | Accepted |
| DSD256 mono | 200 | 2,496,697 | 2,503,881 | 62.6% | Accepted |
| DSD256 stereo | 200 | 3,758,287 | 3,770,464 | 94.3% | Refused |

Admission requires both mean and full-block peak below 80% of the selected
ideal instruction budget. Stereo DSD256 is refused before transport startup.
The shared PCM slots buffer channel-block snapshot bursts. Loads, branches,
multiplies, memory waits and storage increase actual cycle costs. On the
player, the 512-frame build decoded the stereo DSD64 sine at about 1.05 times
real time while core 1 was busy. If core 1 ran at the requested 200 MHz, that
is roughly two cycles per counted instruction; the margin at that rate is small
and needs a player measurement with the 4,096-frame blocks.
