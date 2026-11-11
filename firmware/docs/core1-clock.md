# Calculation-core clock

Core 1 starts at 100 MHz while audio holds the PLL at 200 MHz. Its programs may
select PLL / 1 for 200 MHz decode work. DSD64 mono uses 100 MHz; DSD64 stereo,
DSD128 and mono DSD256 use 200 MHz. Vorbis above 96 kHz uses 200 MHz. Other
programs retain 100 MHz. These clocks provide two or four million cycles per
20 ms, respectively. Instructions are not cycles: the emulator does not model
Cortex-M3 instruction timing, SRAM waits, storage contention or power draw.

## Hardware and stock reference

Rockchip's [2019 roadmap](https://www.cnx-software.com/pdf/Rockchip-Roadmap-2019-.11_mts.pdf),
RKNanoD page, specifies a 500 MHz calculation core and a 250 MHz system core.
The [RKNanoD TRM V1.0](https://rockchip.fr/Rockchip%20RKNanoD%20TRM%20V1.0.pdf),
CRU_CLKSEL0_CON, defines the calculation source in bit 15 and its divider in
bits 14:12 as source / (field + 1). The system divider occupies separate bits
2:0. GRF_INTER_CON0 bits 15:14 enable calculation data/instruction SRAM
multicycle responses. The [datasheet V1.8](https://rockchip.fr/RKNanoD%20datasheet%20V1.8.pdf)
specifies a nominal 1.2 V logic supply, with recommended limits 1.08–1.32 V;
its PLL range alone does not establish the CPU limit.

Static analysis uses the CRC-checked ECHO 2.0.0 image identified in
[the stock reference](../../docs/stock-firmware.md). The selected main/overlay
ELF, loaded extents and Ghidra byte hashes were checked against that image.
Addresses below describe recovered behavior, not vendor function names.

| Address | Checked behavior |
|---|---|
| `0x0307B500` | 36 clock profiles, seven words each; calculation clock at row offset `0x14` |
| `0x030685DC` | Combines active profiles, bounds them and calls the clock solver |
| `0x03068316` | Solves PLL and separate system/calculation dividers |
| `0x03051C90` | Programs PLL/dividers; enables calculation SRAM multicycle responses above 300 MHz |
| `0x03051E74` | Direct calculation-divider setter with a 500 MHz limit |
| `0x030687C0` | Activates a clock profile |
| `0x03066E32` | Requests profile 31, whose calculation target is 400 MHz |

Profiles 5–13 request 300 or 450 MHz calculation clocks, 14–16 request 400 MHz,
and 17–22 request 480 MHz. Profile 35 bounds the calculation target at 480 MHz;
stock callers also explicitly activate that profile. Stock DSD programs occupy
modules 81 and 82. The exact DSD-rate-to-profile mapping has not been recovered;
the profile values must not be presented as measurements of stock DSD playback.

The apparent voltage-related calls passing `0x100`/`0x120` in the clock setter
land at `0x030617CE`, which only loads and returns a stored word. They do not
write a regulator. No voltage adjustment or PLL retune is introduced here.
200 MHz is below both the documented maximum and stock's active calculation
clock targets. Higher steps would require additional PLL/system-clock work and
are outside this implementation.

## Requests and transitions

The audio HAL sets the PLL to 200 MHz before starting a decoder and keeps it
there until audio/core 1 shut down. `core1_divider_for` still selects /2 for
startup. Only the DSD and Vorbis programs install `StreamRegion.request_clock`;
the stream resets that request to 100 MHz on adoption or cancellation. DSD
requests after validating its transport header; Vorbis requests after validating
its setup. Requests never depend on untrusted filename extensions.

The core-1 startup helper writes `0x70000000 | divider_field` to CRU `0x40180014`,
changing only calculation divider bits 14:12 with the hardware write mask.
Barriers complete the transition before decoding resumes. The existing SRAM
response setting remains unchanged: 200 MHz is below stock's 300 MHz threshold.
The 1 ms idle SysTick reload follows the selected calculation clock. While
waiting for input or output slots, `core1_idle` lowers the clock to 100 MHz and
restores the requested clock after waking. Pause therefore does not hold a
continuously running 200 MHz decoder. Stop gates/powers down core 1 as before.

Core 0's coasting writes only system-divider bits 2:0. Its
`tick_clock_set_rate` accounts for system time across those changes; calculation
clock requests neither call it nor change core 0's PLL, divider or timebase.
No request handshake or dispatch is added to resident core-0 code.

## Admission and checks

DSD admits a rate/channel combination only when both measured steady mean and
normalized full-block peak are below 80% of the selected ideal instruction
budget. This reserves at least 20% for costs absent from instruction counting;
it does not establish physical real-time support. Stereo DSD256 peaks at about
3.77 million instructions per 20 ms and is refused by the file opener before
publishing a stream generation. DSD and Vorbis measurements are recorded in
[DSD](dsd.md) and [Vorbis](ogg-vorbis.md); Opus retains its normal clock and its
[budget](opus.md) is two million cycles per 20 ms. This base has no APE or WMA
playback program, so no APE compression level or WMA profile is enabled here.

Native tests cover accepted/refused DSD opens, reference PCM, seeks and endpoints.
ARM tests observe the actual masked clock writes for rate/channel changes,
high-rate Vorbis, full output slots and cancellation. Integrated scenarios play
stereo DSD128 across DSF/DFF with one DMA start, seek mono DSD256, and refuse
stereo DSD256. No player access was used; hardware timing, listening, voltage and
power measurements remain separate checks.
