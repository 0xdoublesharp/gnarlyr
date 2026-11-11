# ReplayGain metadata

The portable tag reader returns track and album gain, with a peak-limited value for each, in `TrackTags.replay_gain`. These values are computed while reading metadata. Playback and the library database do not consume them yet.

## Tag mapping

The four names are `REPLAYGAIN_TRACK_GAIN`, `REPLAYGAIN_ALBUM_GAIN`, `REPLAYGAIN_TRACK_PEAK` and `REPLAYGAIN_ALBUM_PEAK`. Matching ignores ASCII case.

| Container | Metadata |
|---|---|
| FLAC and Ogg Vorbis | Vorbis comments |
| MP3 | ID3 TXXX/TXX; APEv2 fills missing fields |
| WAV and DSF | Embedded ID3 TXXX/TXX |
| AAC/ALAC in MP4 | Freeform atoms with `com.apple.iTunes` or `org.hydrogenaudio.replaygain` as their namespace |
| Monkey's Audio | APEv2 text items |
| WMA | ASF string attributes |

Each field keeps its first valid value. Invalid values leave the field available for a later valid value. In MP3, valid ID3 fields take precedence over APEv2. R128 and SoundCheck fields are not interpreted.

ID3 values support Latin-1, UTF-8 and both UTF-16 byte orders, with BOM handling. Numeric text must contain ASCII characters. Unsupported encodings, malformed UTF-16 and non-ASCII numeric bytes are rejected. ASF values with embedded NULs followed by nonzero data are rejected.

## Numeric representation

Gain text accepts a sign, decimal point, optional scientific exponent, surrounding ASCII whitespace and an optional case-insensitive `dB` suffix. Peak text uses the same numeric syntax without a unit. Each numeric value is limited to 64 bytes and an explicit exponent between -99 and 99.

Gains round to the nearest hundredth of a decibel, with ties away from zero, within -96 to +96 dB. The parser uses bounded integer arithmetic. A generated fixed-point table converts the result to Q16 amplitude; unity is 65,536. Zero in a returned gain field means missing metadata, so an explicit 0 dB is distinguishable from an absent tag. Very small amplitudes remain at least one Q16 unit.

Peaks are nonnegative Q24 values, with unity 16,777,216. They round upward so quantization cannot understate a declared peak. The representable maximum is `UINT32_MAX / 16777216`, just below 256. A positive value smaller than one Q24 unit rounds to one. An explicit zero peak represents silence.

`ReplayGain` contains four Q16 amplitudes: `track`, `album`, `track_safe` and `album_safe`. A safe value never exceeds its corresponding raw gain. It uses the matching peak, falling back to the other peak when the matching one is absent. The upper bound is `floor(2^40 / peak_q24)`. A zero peak imposes no additional limit. When neither peak exists, attenuation is retained and boosts are capped at unity. A peak alone does not create a missing gain.

These limits cover the declared source peak. Later EQ or other processing needs its own headroom policy.

## Verification and generation

`test_replay_gain` compares all 19,201 supported centidecibel inputs with independent floating-point exponentiation and checks parsing, duplicate handling and conservative peak limits. `replay_gain_metadata_formats` writes tags into copies of real container fixtures using Mutagen, then reads them through the C tag reader. It also checks ID3 encoding variants, MP3 tag precedence, malformed values and unknown MP4 namespaces.

Regenerate the table with Python 3.14 from the firmware directory:

```sh
python tools/replay_gain_table.py --output src/library/replay_gain_table.h --style .clang-format
```

`replay_gain_table_up_to_date` checks that the committed table matches the generator. Numeric parsing and conversion are part of the library scan target, outside the playback loop.
