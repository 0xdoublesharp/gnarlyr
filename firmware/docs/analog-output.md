# Analog output sequencing

`device/dac_sequence.h` exposes the shared CS43198 sequences through `DacBus`:

- `dac_sequence_power_down` mutes PCM, waits 150 ms, clears old status, enables PDN_DONE, asserts PDN_HP on both chips, waits for both completions, then asserts PDN_ASP. Read failure or a 150 ms completion timeout returns false before shutting down the serial port.
- `dac_sequence_power_up` applies the pop-free settings before releasing ASP and HP, waits 12 ms, then restores the temporary settings. The caller keeps the external mute asserted until ready.
- `dac_sequence_gain` powers down, writes Low `0x20` or High `0x31`, powers up and releases PCM mute. Failure leaves the board caller muted. It requires clocks to remain running throughout.

Cold start uses the power-up helper. Power-off and pause standby use the power-down helper through `dac_output_off`. Runtime gain fades out before the helper and fades back only after success. Output enable keeps external mute asserted through its 10 ms settling delay.

The register values and ordering follow CS43198 DS1156F2 sections 5.5–5.7 and the OUT_FS field. Low uses the valid `10` OUT_FS code; High retains `11` and +1dB_EN. The repository's stock comparison does not yet establish stock gain values. Physical transient amplitude and listening behavior require the owner's player.

## Owner listening check

On each jack, start at low volume and compare silence and a quiet continuous track:

1. Cold start and start playback; listen for a DC thump.
2. Switch Low to High and back during playback, then while paused.
3. Pause briefly and resume; repeat after at least 35 seconds for standby.
4. Change tracks and seek forward/backward, including between sample rates.
5. Switch outputs and plug/unplug; check for clicks and stale audio bursts.
6. Power off while playing, then cold start again.

## Level evidence

[CS43198 DS1156F2](https://statics.cirrus.com/pubs/proDatasheet/CS43198_DS1156F2.pdf), section 7.5.1, defines OUT_FS `00` and `01` as reserved, `10` as 1.41 Vrms and `11` as 1.73 Vrms. Thus `0x20` is the lowest documented valid OUT_FS code. `0x31` selects `11` plus +1dB_EN, which gives 2 Vrms with HV_EN set. The configured Class H value `0x1f` at `0x0b0000` sets HV_EN (bit 1). The expected High/Low voltage ratio is `2 / 1.41 = 1.418`, approximately `20 log10(2 / 1.41) = 3.04 dB`, before downstream amplification and identical digital attenuation. This is a datasheet expectation, not a measurement at either jack.

The [stock record](../../docs/stock-firmware.md) establishes the register bus, start-up sequence and 12 ms settling delay, but explicitly lists gain values as not compared. It does not prove that stock uses `0x20` for Low. This patch chooses `0x20` from the fitted part's documented valid codes; a stock gain trace remains needed to claim equivalence.

## Transition checks and diagnostics

Track restarts and seeks fade out before DMA stops; DMA descriptors start again from the ring origin after prefill, before fade-in. Resetting engine positions now clears the entire ring so a short prefill cannot leave earlier track samples in unused space. Seamless same-rate queue transitions retain their existing contiguous PCM behavior. Output selection fades to mute before applying its volume curve and fades back afterward.

At the first sampled raw jack change, the DAC guard asserts external mute and clears its applied-volume state. It holds across the seven-sample debounce and releases after the stable output curve has been applied, or after contact bounce returns to the original connected state. With neither jack detected, the guard remains asserted until a jack settles connected. Logical pause mute remains independent. Detection is polled, so contact motion before the first sample cannot be protected by software.

Debug reports include `dac.up`, `dac.power_left`, `dac.power_right`, `dac.start_us`, `dac.stop_us` and `dac.gain_us`. Power values are the last successful reads of 0x020000 for each chip, not live reads after reset or rail removal. A failed receive marks that chip unknown; earlier bus failures leave the last cached read intact. Start duration is the last successful cold start; stop and gain durations cover their latest attempts, in microseconds. These timings exclude the board gain fade around the register sequence.

Native checks cover ring clearing after seek, register order/timeouts and report fields. The ARM scenario checks the sample at seek restart, unused ring space, output-switch ramps and the mute guard. Physical transient levels and jack timing still require the owner checks above.
