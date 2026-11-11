# Standalone Opus decoder

The full fixed-point decoder fits the existing exclusive core 1 code bank.
The standalone component supports SILK, CELT and hybrid, mono/stereo, the five
standard output rates from 8 to 48 kHz, PLC, FEC and packets up to 120 ms. Player
integration, image storage placement and physical playback checks remain.

## Source and configuration

`third_party/opus` contains the unchanged source subset from the official
[libopus 1.6.1 archive](https://www.opus-codec.org/downloads/). VERSION records the
retained paths and archive SHA256
`6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1`.
COPYING preserves the upstream BSD-style redistribution terms.

Compile the source lists `OPUS_SOURCES`, `CELT_SOURCES`, `SILK_SOURCES` and
`SILK_SOURCES_FIXED` from the three upstream `*_sources.mk` files, with function
and data sections plus linker garbage collection. Define `OPUS_BUILD`,
`FIXED_POINT`, `DISABLE_FLOAT_API`, `ENABLE_HARDENING` and `PACKAGE_VERSION`.
Force-include `port/opus_support.h` for the vendored translation units. Optional
neural enhancements, custom modes, float APIs, SIMD dispatch and 96 kHz extensions
are disabled. All standard Opus coding modes remain available.

The port replaces upstream stack-allocation macros with checked caller-owned
scratch. Every allocation checks multiplication, alignment and remaining space.
There is no heap or VLA. Exhaustion returns `OPUS_DECODE_EXHAUSTED` through a
checkpoint, releases the active arena and requires decoder reinitialization.
The component supports one active decode call at a time.

## API and memory

`opus_decode_state_bytes` reports caller-owned state size. `opus_decode_init`
accepts separate eight-byte-aligned state and scratch buffers. The recommended
scratch capacity is `OPUS_DECODE_SCRATCH_BYTES` (32,768 bytes); smaller arenas fail
safely if exhausted. Initialization rejects overlapping buffers.

`opus_decode_packet` accepts a bounded packet and caller-owned interleaved
16-bit PCM. Plain packet input uses `frames=0`; PLC and FEC require an explicit
requested duration. Output reports frames, rate, channels and packet coding mode.
FEC may conceal loss when usable redundancy is absent. Container pre-skip,
header gain, end trimming and seek pre-roll belong to the stream layer.

| Measured ARM resource | Bytes |
|---|---:|
| Text, including checked component and freestanding helpers | 55,372 |
| Constants and codec tables | 28,884 |
| Code and constants combined | 84,256 |
| Remaining in the 131,072-byte code bank | 46,816 |
| Mono decoder state | 18,340 |
| Stereo decoder state | 27,060 |
| Context plus active-context pointer | 60 |
| Observed scratch high water | 13,924 |
| Observed checked ARM stack high water | 2,492 |

Measurements use Arm GNU 15.3.1, Cortex-M3 Thumb and `-Oz`. Stack observations
cover the exercised paths; the proposed reservation remains 16 KiB.

The existing 49,152-byte local data/stack bank can hold stereo state, the
60-byte context and a 16,384-byte guarded stack, leaving 5,648 bytes for the
stream wrapper and alignment. The 131,072-byte input/workspace bank can hold:

| Proposed simultaneous reservation | Bytes |
|---|---:|
| Input ring | 8,192 |
| Complete packet | 61,440 |
| Checked scratch | 32,768 |
| 120 ms stereo interleaved PCM | 23,040 |
| Remaining | 5,632 |

The packet limit accommodates 48 maximum-length frames plus framing; excessive
padding is rejected. The core 0 Ogg reader must stream fragments without another
complete packet copy. A 120 ms packet can produce 5,760 frames, exceeding the
current 4,608-frame planar output slot. Publish the staged PCM in smaller blocks.
The existing two output slots keep their current allocation.

## Checks and integration gates

Eight focused behaviors pass under UndefinedBehaviorSanitizer, including
32 KiB arena canaries, alias rejection, insufficient-output state preservation,
recoverable exhaustion and a 12,800-case malformed-packet/FEC matrix across all
TOCs, five rates and both output channel counts.

All twelve official [RFC8251 test streams](https://opus-codec.org/testvectors/)
pass in mono and stereo at 48 kHz: 24 cases, 40,150 packets and 30,780,960 frames.
Every packet's entropy final range matches; upstream `opus_compare` quality
checks pass. Scratch high water is 13,924 bytes. Existing eight-second Opus
fixtures decode to 384,000 frames each after trimming. Encoded SILK/hybrid/CELT
references differ by at most two 16-bit units from ffmpeg's libopus backend and
one unit across ARM and host fixed-point builds. ARM also passes tiny-arena
exhaustion/reinitialization and explicit PLC/FEC duration checks.

A long random-packet sequence produces larger ARM/host concealment differences.
The first isolated difference follows eight random packets and occurs on a
two-byte SILK packet, whose one-byte payload invokes upstream PLC. This stress
check establishes bounded execution; official conformance quality and final
ranges establish decoder acceptance. The reproduction is retained on Scratch.

The new component is not registered in the current firmware build. Integration
must measure its cold source module and main firmware reserve (at least 8,192
bytes), allocate image storage after checking existing program/table ranges,
and test Ogg packet streaming, gain/trimming/seeking and USB transitions.
Physical checks must establish real-time decoding and input starvation behavior.
Unused runtime code capacity does not establish an unused image storage offset.

Probe sources and scripts are under `/Volumes/Scratch/SnowskyEcho/tmp/opus-fit`;
objects and stack records are under `build/opus-fit`, and logs/PCM under
`reports/opus-fit` within the same Scratch project directory.
