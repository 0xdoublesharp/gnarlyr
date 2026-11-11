# Opus decoder test audio

`tools/opus_fixture.py` generates these Ogg Opus files with ffmpeg's libopus encoder and decodes the `.pcm` references with ffmpeg's libopus decoder (Homebrew libopus 1.6.1, a floating-point build). The player's decoder is the fixed-point build of the same release, so the references are independent of its arithmetic.

The signals are chirps at 48 kHz starting at amplitude 12,000: the left (or only) channel starts at 997 Hz and rises 43 Hz per second, the right channel starts at 2,111 Hz and rises 73 Hz per second. The `.pcm` files are little-endian signed 16-bit interleaved output after pre-skip and end trimming, exactly as long as the source.

| File | Content |
|---|---|
| `stereo48` | 2 s stereo, 128 kbit/s, 20 ms CELT packets, 200 ms pages |
| `speech16` | 1 s mono, 16 kbit/s VoIP application, 20 ms SILK packets |
| `long120` | 1 s stereo, 96 kbit/s, 120 ms packets |
| `gain120` | `long120` with an identification-header output gain of +3 dB (768 in Q7.8); its reference is decoded with that gain applied |
| `origin48` | `stereo48` with every known granule moved 4,800 samples later; it decodes to the `stereo48` reference |

Against these references the fixed-point decoder differs by at most 8 units at 128 kbit/s CELT (RMS 1.7), at most 1 unit for SILK, and at most 13 units (RMS 4.0) with the +3 dB gain; the tests allow 16 units and RMS 4.5. A one-sample position error on these signals produces differences in the thousands.

To regenerate (ffmpeg with libopus on the path):

```
python3 tools/opus_fixture.py
```

Normal tests use the checked-in files. libopus's BSD license is in `third_party/opus/COPYING`.
