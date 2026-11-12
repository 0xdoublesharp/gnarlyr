# WMA Standard reference fixtures

`tools/wma_fixture.py` generates short chirps with ffmpeg's WMA v1/v2 encoders. The small ASF files cover 16 kHz mono, 48 kHz stereo and a two-second 44.1 kHz stereo seek source. `v2fragment` uses 256-byte ASF packets to split codec objects across packets; `v1compressed` packages the independent encoder output as compressed subpayloads. The reference test independently demuxes and decodes them with ffmpeg at run time; PCM stays in memory or on Scratch.

Native and ARM tests require byte-identical ASF payloads, exact decoded lengths, and PCM differences at 16-bit scale no greater than 12 LSB for the low-rate mono stream or 2 LSB for stereo. Seek comparisons use the full reference decode at 700, 1,300 and 1,900 ms, with 1,300 ms repeated on ARM. The tests also check the integer IMDCT against an independent cosine basis.

```sh
python3 firmware/tools/wma_fixture.py --output /Volumes/Scratch/SnowskyEcho/data/ape-wma/wma-fixtures
```

Copy the resulting small `.wma` files into this directory when deliberately updating fixtures. The generator uses ffmpeg's bitexact flags and records its signal and encoder settings in source.
