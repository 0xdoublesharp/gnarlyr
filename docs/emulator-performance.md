# Emulator performance investigation

The optimization target was twice the speed of the playback/module/settings journey, within two hours. Work stopped at the owner's consolidation checkpoint. No production emulator change is ready to integrate; the firmware and shared Python environment are unchanged.

Measurements use branch base `a7e168ba1d888f603a6ae24c501c018815d23873`, its own device image, native ARM64 Python 3.14 and Unicorn 2.1.4. The representative check is:

```sh
firmware/.venv/bin/python firmware/tests/emulator/test_echo_image.py EchoImageTest.test_modules_change_places_and_a_setting_is_saved_while_a_track_keeps_playing
```

## What the checks established

| Experiment | Complete journey | Result |
| --- | --- | --- |
| Unchanged emulator | Pass, repeated | 49.0 and 49.4 seconds elapsed |
| Native VOP pixel model | Pass, repeated | 42.6 and 46.8 seconds; insufficient gain for the added native build and bridge |
| Source-built pinned engine | Pass | 50.1 seconds |
| Upstream executable-write fix with data RAM marked read/write | Pass | 46.9 seconds in one paired trial; a modest lead, not the target |
| Virtual TLB alone | Pass | No useful gain established |
| Virtual TLB plus narrowed unmapped-access instrumentation | Fail | Invalid accesses in loaded module code; playback never became audible |
| Explicit identity TLB permissions | Pass after preserving the execution-end sentinel | 47.1 seconds; no useful gain established |
| Upstream Apple JIT fix and Release assertion experiment | Pass | No reproducible improvement established; later host load affected elapsed time |
| Tenfold synthetic clock polling advance | Fail | Playback never became audible; rejected |

Later runs overlapped other worktrees' firmware suites. Those elapsed times are unsuitable for a clean before/after comparison. A later Release-guard run used 52.5 CPU seconds and 60.4 elapsed seconds. Faster failing or incomplete journeys are not speedups.

Python profiling found about 26 million host callbacks, including 13.6 million pixel writes, and roughly 597,000 emulator starts. Native stack sampling also found instruction counting, exit checks, memory translation and JIT protection transitions. The host already runs ARM64 code; its processor cannot directly execute the firmware's Cortex-M3 Thumb instruction set.

## Relevant upstream work

- [Unicorn performance FAQ](https://github.com/unicorn-engine/unicorn/blob/master/docs/FAQ.md) recommends reducing instruction and memory instrumentation. A nonzero instruction count installs an instruction hook. It describes the virtual TLB as experimental.
- [Maintainer discussion of slow execution](https://github.com/unicorn-engine/unicorn/discussions/2278) identifies counting and hooks as causes of lost optimizations.
- [PR 2337: avoid dirty-page slow paths for non-executable memory](https://github.com/unicorn-engine/unicorn/pull/2337) is an applicable memory-write optimization. Its reported large gain is from another workload; our checked journey improved only modestly in one trial.
- [PR 2417: Apple Silicon JIT state across foreign callbacks](https://github.com/unicorn-engine/unicorn/pull/2417) addresses nested runtime correctness and reports a microbenchmark improvement. It did not establish a useful improvement in our checks.
- [Fast SystemC Processor Models with Unicorn](https://jngr.org/papers/juenger_rapido2019.pdf) demonstrates faster block-level instruction accounting. It permits timing overshoot, so applying the approach here needs explicit interrupt, event and module-observation parity checks.

A future optimization pass should start with clean paired measurements and instruction-count overhead. A QEMU or Renode replacement was not implemented or benchmarked. Retain the existing emulator until a candidate passes its observations and the full firmware suite.

## Consolidation checkpoint

The worktree is `/Users/justin/Workspaces/worktrees/SnowskyEcho/emulator-fast`. Unfinished source experiments are preserved there under ignored `.local/emulator-experiments/`; they are not production changes. Raw profiles, logs and disposable engine builds remain under `/Volumes/Scratch/SnowskyEcho/{reports,tmp,build}/emulator-fast`.

All task-started benchmarks and builds have exited. No full firmware suite was run for this checkpoint because there is no production source change. The final focused clock experiment failed and was not applied. Only this findings document is committed. Integration, worktree retirement and device installation remain with the owner.
