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

Track, seek, output-switch and jack detection behavior is unchanged by this sequencing patch. This patch adds no new hardware validation claim.
