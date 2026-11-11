# DSF and DFF PCM playback

The dedicated core-1 DSD program converts uncompressed DSF and DSDIFF to 88.2 kHz
signed 24-bit PCM. DoP is not implemented. The file adapter accepts mono/stereo
DSD64 and mono DSD128 for playback. Stereo DSD128, DSD256, other rates, more than
two channels, DST compression and files beyond the 32-bit file interface are
refused. Container recognition and the host/reference decoder also handle
DSD256 so its cost can be measured without enabling it on the player.

These are software results. No DSD or AIFF image from this change was installed
on a player. Listening, physical CPU margin, storage contention and power use
remain unchecked. Stereo DSD64 has very little ideal instruction margin; the
instruction counts do not establish that it keeps real time on the hardware.

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

Audio module 9 owns container probing, opening and the virtual stream reader.
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

The actual loader's `core1_divider_for` caps core 1 at 100 MHz: a 200 MHz PLL
selects divider 2. A 20 ms interval therefore has two million cycles. Older
Vorbis/Opus documentation's 200 MHz core-1 assumption is not used here.

`core1_dsd_decodes_in_emulator` generates DSF and DFF, compares all ARM PCM with
the native decoder, and records steady instruction counts for mono and stereo
at all three rates. Stereo DSD128 and both DSD256 channel counts exceed even
the one-cycle-per-instruction floor and are refused before transport startup.
Mono/stereo DSD64 and mono DSD128 fit the mean instruction floor; memory,
multiply, branch and storage cycle costs still need measurement on the player.
The isolated 512-frame block peaks include channel-block snapshot bursts; the
shared PCM slots buffer those bursts. This policy establishes an instruction
screen, not measured physical real-time support.
