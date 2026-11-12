# Battery measurements

The parent runs this kit through the existing developer debug loop after separately approving and installing a reviewed image. Building the kit and analysing files are offline operations. Keep each run's files on Scratch. No battery-life improvement has been measured on a player for these changes.

## Prepare a run

1. Charge the player fully, then record its build tag, battery voltage on battery power, firmware settings, card model, track, gain, volume, headphone load, output and Bluetooth peer. Use the same load and settings for comparisons. Disable EQ and transitions for the initial comparison. Prepare a queue longer than the test or select repeat; a stopped queue would measure standby for the rest of the run.
2. For each playback run, start the chosen recording before sending the script: 44.1 kHz/16-bit FLAC, 320 kbit/s MP3, then mono 16 kHz speech. Repeat each through 3.5 mm, 4.4 mm and Bluetooth. For Bluetooth, unplug both jacks and confirm the peer is receiving audio. Keep the screen off throughout the timed window. Verify underrun counters in the final report.
3. Generate the files, replacing `abcdef0` with the installed build tag. Use `--mode standby` for a paused, screen-off 24-hour test; start playback first, since that script presses Play once to pause and waits 35 s for pause standby. Use `--mode playback` for uninterrupted playback. The script is bound to the supplied build and uses the ordinary `soak` line.

   ```sh
   python3 firmware/tools/battery.py prepare --build abcdef0 --mode playback \
     --out /Volumes/Scratch/SnowskyEcho/reports/battery-run
   ```

4. With the parent's approved USB session open, copy the generated zero-filled `battery.bin` into the internal drive's `/.gnarlyr/debug/` directory. It is 49,152 bytes and must exist before the script starts. Preserve any previous trace first; a new supply-off run starts at record one. The parent sends `run.txt` through `tools/echo_debug.py run`, using the same debug loop and eject procedure as other scripts. That command may wait up to 24 hours plus its normal allowance. Leave the cable in, the host awake and its automount behavior unchanged. Do not send other scripts or press keys during the run.

   ```sh
   python3 tools/echo_debug.py run \
     /Volumes/Scratch/SnowskyEcho/reports/battery-run/run.txt \
     > /Volumes/Scratch/SnowskyEcho/reports/battery-run/run-result.json
   ```

`supply off` requests both supply pins low. A cable bit of one does not prove the battery is supplying the player. Compare the trace's initial voltage and discharge with the unplugged reading, and use an external current meter when available. The battery ADC reads the system rail; a USB-powered voltage is not a cell measurement. Firmware cannot establish electrical isolation of the supply pins.

## End and collect

The script waits up to 1,440 minutes. With the supply held off, the battery HAL reports battery operation so the normal below-3,450 mV-for-two-seconds shutdown remains active. The soak's slower fallback is below that cutoff, at 3,440 mV. A normal duration end or a key restores the supply and reconnects as before. At the battery cutoff, aborting the script restores the supply and the existing USB shutdown path enters charge mode, retaining a `low_battery` report. It may end without reconnecting, so the host command can time out. After that endpoint the parent reconnects the player and copies `battery.bin` and every retained `report*.txt` before starting another run. The next cold report preserves the prior shutdown report. Keep the last minute's trace as the final completed sample; the shutdown can fall between samples.

Analyse the copied trace, optionally supplying the battery's independently known usable capacity. Do not infer capacity from a voltage trace.

```sh
python3 firmware/tools/battery.py analyse \
  /Volumes/Scratch/SnowskyEcho/reports/battery-run/battery.bin \
  --capacity-mah 500 \
  > /Volumes/Scratch/SnowskyEcho/reports/battery-run/analysis.json
```

Here `500` is an example input, not a measured capacity for the Echo. The tool fits percent against elapsed time, reports elapsed hours, remaining hours at the fitted slope and full-discharge hours, and scales that slope by the supplied capacity for mA-equivalent current. Battery percent comes from the voltage gauge, so load sag, recovery and the discharge curve limit these estimates. A flat, increasing or USB-controller-powered trace has no discharge estimate. The actual time from playback start to the normal low-battery cutoff is the primary runtime result. On a connected cable this endpoint enters charge mode after restoring supply power. Use the final report's `uptime_ms`, `report.reason`, `audio.underruns`, output-underrun count and `script.soak` end state to establish the endpoint and software health.

Existing reports can also be analysed with `analyse REPORT.txt --report`. This uses their `power.sample` rows, retains the `script.soak` lines and has only the report's retained history. Minute trace files provide the full run without growing resident RAM. `script.soak` retains its existing duration, voltage slope, busy share and start/end state fields. Its `clock_mhz` is the configured clock; the binary trace records the effective divided core 0 clock and the active core 1 clock.

For switched-off drain, the parent records an unplugged, rested voltage and percent, switches the player off manually for 24 hours, then starts it and takes the same readings before playback. Keep temperature and rest time comparable. The firmware cannot log while off. Compare this separately with the paused-screen-off result, including whether Bluetooth was disabled or connected during standby.

## Trace format

The developer build writes one 24-byte little-endian record per minute while `supply off` is active, into the preallocated internal file. There are at most 2,048 records. Each contains six 32-bit words: uptime milliseconds; core 0 idle microseconds since the previous record; effective core 0 MHz in the low half and active core 1 MHz in the high half; battery mV in the low half, percent in bits 16–23 and the existing power-sample flags in bits 24–31; one-based sequence; and the XOR of the first five words with `0x44534254`. The first record establishes the time origin. Subsequent idle deltas remain valid across the microsecond counter's wrap. The analyser unwraps uptime, checks sequence and checksum, rejects invalid intervals, stops at unused zero records and ignores an incomplete final record. A corrupt complete record is an error. Core 1 busy time is not measured by this trace; only the emulator's instruction model estimates it.

The recorder adds one small eMMC write per minute. Its cost is included in a kit run; compare builds using the same kit. A missing or short file prevents recording and never allocates or grows a file during playback. Preserve the raw trace and reports on Scratch, including errors.

## Emulator comparison

Run only this matrix, with a unique Scratch destination for each revision:

```sh
ECHO_BATTERY_REPORT=/Volumes/Scratch/SnowskyEcho/reports/battery-matrix.json \
  python3 tools/firmware.py test --jobs 2 \
  --regex '^device_image_boot_test_battery_playback_matrix_has_no_underruns$'
```

The JSON includes busy shares, clock residence, WFI wakes, read sizes, maximum read gap, transfer time and two assumed card-tail active times per minute. Card timestamps have millisecond resolution; transfer time sums each command's configured latency so same-millisecond commands cannot overlap in that sum. It also checks zero ring/output underruns and the screen-off state. Four seconds is a scheduling comparison window after playback settles; it is not a long playback or hardware power test.

## Playback work audit

The playback source selector in `board/audio.c` chooses one format from the file header and starts one core 1 program. FLAC uses that program or its core 0 fallback when core 1 cannot start; it does not run both decoders. There is no MQA decoder in this tree. The following existing paths already avoid unnecessary sample processing:

| Work | Existing bypass |
| --- | --- |
| Flat equalizer | `equalizer_design.c` omits zero-gain sections; no sections means `engine->equalizing` is false and `equalize` returns without processing samples. |
| Unity ReplayGain and headphone gains | `convert_pending` selects direct 16/24-bit conversion when both gains are unity. The general multiply path remains necessary for actual attenuation, balance or other bit depths. |
| Matching sample rates | `copy_until_limit` calls interpolation only when `resample.factor > 1`; native-rate playback copies directly. |
| Inactive fades | `shape_block` calls the sample ramps only for active fade-in or eligible fade-out. It still does a few block-level checks and constructs a descriptor; no inactive per-sample fade runs. |
| Screen-off motion and meters | Rendering and animation are gated on `display_on`. There is no independent audio level-meter sample pass in this path. Now Playing refreshes its state and checks its cached cover during ordinary screen updates; it does not decode an extra audio format. |

The substantial waste found here was the pending-render flag keeping the dark player at the busy loop rate. The scheduler fix removes that cause. Status and clock change detection also belongs behind the display-on guard: a dark screen needs no status hash, and waking computes the current state before drawing. Keep nonvisual work, key handling, player transitions, low-battery protection and Bluetooth service active. Do not skip every screen's `update` callback indiscriminately: some screens own background jobs and completion behavior.

## Retained standby option

The current pause standby is the safe measurement starting point: after 30 seconds it releases the DAC, I2S/DMA and core 1, retains the app and queue, and can resume without a full boot. With the screen off, Gnarlyr runs core 0 from the 24 MHz crystal with the PLL powered down and wakes on the 5 ms timer (1 ms while the Bluetooth module is powered, which drains its UART). Core 1 is powered down, which is preferable to retaining an idle decoder solely for its WFI loop. Auto-off eventually drops the power hold and requires a cold start. The standalone standby kit holds auto-off off only while the script runs.

A new alternative to auto-off could retain core 0 RAM and wake only from the digital Power pin. Enter it after saving settings and playback position, closing outstanding writes, releasing audio, powering Bluetooth off, sleeping the panel and stopping its PWM. Keep the power hold asserted. Gate unused controllers, suspend the stall watch, put the watchdog into a documented sleep-safe state, arm a verified Power GPIO interrupt, and mask periodic work before WFI. Check the pin again after arming it so a press between the last check and WFI cannot be lost. On wake restore clocks, interrupts, watchdog and driver state, reconcile elapsed wall/sleep time, clear stale key transitions and draw the retained screen before lighting it. Starting playback then follows the existing output-open path.

This is a design, not an implemented mode. The current input driver polls an ADC ladder and Power GPIO; it has no proven key interrupt route. Turning SysTick off also removes ADC scans, watchdog kicks and the stall heartbeat, and stops the firmware's elapsed-time accounting. The retained RTC has been observed keeping time while off, but release firmware does not yet use it to account for a stopped SysTick. Connected Bluetooth also needs its polled UART; a key-only state must power it off and give up earbud wake, or separately establish a supported radio wake interrupt. Those hardware and lifecycle changes exceed a safe small edit within the resident reserve.

Firmware controls the dynamic work, clock gates, output supplies, radio power, backlight and whether RAM stays powered. A useful drain model is:

`standby current = retained-SoC/RAM leakage + retained clocks + regulator losses + RTC + card idle + wake work`

The existing paused state retains a running PLL/peripheral domains and 200 timer interrupts per second. A verified key-only state could remove timer/ADC work and more clocks. Its current cannot be inferred in mA from instruction counts: the static domains, card and regulators must be measured. Switched-off current still includes the RTC/always-on supply and leakage; measure it separately instead of assuming zero.

The parent's measurement sequence is:

1. Run the 24-hour paused-screen-off kit with Bluetooth disabled, then with a connected peer, retaining the minute trace and cutoff report if it ends early. This establishes the current implemented state.
2. Measure switched-off current and a 24-hour rested voltage change manually under the same conditions. Use an external meter with enough resolution for sub-mA current; a one-percent voltage gauge cannot establish a small standby drain in a single day.
3. Before implementing retained standby, establish Power's GPIO/NVIC wake route, RAM retention, wake clock source and watchdog behavior from documented hardware or the exact model's independently checked stock path. Test wake without SD writes first. Preserve the existing auto-off default until this passes.
4. For a prototype, target a visible retained screen within 250 ms and resumed audio within one second across at least 30 sleep/wake cycles, both jacks, low battery and USB insertion. Separately test a press at entry, a held Power key, reset, cable attach/removal and persistence after a real power loss. Measure actual latency, including the DAC's waits and I2C setup.
5. Compare retained-standby current to off current. A proposed acceptance target is an additional one percent of usable capacity per 24 hours or less: `extra_mA <= capacity_mAh * 0.01 / 24`. That is 0.208 mA for an illustrative 500 mAh capacity. The parent chooses the actual capacity and acceptable loss.

The energy break-even interval is `(cold_boot_mAh - retained_resume_mAh) / (standby_mA - off_mA)` hours when both differences are positive. Measure boot and resume charge with a current logger. Below that interval retained standby may save energy as well as latency; above it auto-off saves charge. Report latency and overnight drain together, and retain auto-off as a choice.
