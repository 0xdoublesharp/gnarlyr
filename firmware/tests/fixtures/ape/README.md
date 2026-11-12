# APE reference fixtures

`tools/ape_fixture.py` produces deterministic integer ramps in WAV, encodes them with the Monkey's Audio 3.99 Unix port at revision `5fe956c7e67c13daa80518a4cc7001e9fa185297`, then requires ffmpeg's independent APE decoder to reproduce every input byte. The encoder is a test-only Scratch tool and is not distributed with the firmware. Its source is <https://github.com/fernandotcl/monkeys-audio>.

The fixture generator defines every PCM sample, so native and ARM tests compare against the exact input without retaining PCM dumps. Small streams cover every compression level, mono/stereo, 16/24-bit and 48/96/192 kHz. `seek16` crosses multiple frame boundaries at 8 kHz and permits independent seek-table starts.

Regenerate in Scratch, then copy the small `.ape` fixtures into this directory:

```sh
python3 firmware/tools/ape_fixture.py --encoder /Volumes/Scratch/SnowskyEcho/build/ape-wma-mac/mac --output /Volumes/Scratch/SnowskyEcho/data/ape-wma/fixtures --scratch /Volumes/Scratch/SnowskyEcho/tmp/ape-wma/fixtures
```
