# ReplayGain metadata

The portable tag reader returns track and album gain, with a peak-limited value for each, in `TrackTags.replay_gain`. The device scanner and desktop indexer store these values in an optional library extension. Playback does not apply normalization yet.

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

## Library cache

The version 6 library header, its ten sections and its 32-byte track records stay unchanged. An optional extension begins immediately after the greatest section end, without extra alignment. Its data is excluded from the library content fingerprint, so adding or changing gains preserves queue and personal-label identities when the base library is otherwise identical. The existing device and desktop writers have different string-deduplication layouts; an extension binds to the fingerprint of the base that its writer produced.

The extension header is 20 bytes, little-endian:

| Offset | Type | Value |
|---|---|---|
| 0 | four bytes | `DSRG` |
| 4 | `u16` | extension version 1 |
| 6 | `u16` | record size 20 |
| 8 | `u32` | base library track count |
| 12 | `u32` | base library content fingerprint |
| 16 | `u32` | FNV-1a checksum of the preceding 16 bytes |

One 20-byte record follows per track in final library order: four `u32` amplitudes (`track`, `album`, `track_safe`, `album_safe`) and a `u32` checksum. The checksum is FNV-1a over the base fingerprint and track index, both little-endian `u32`, followed by the 16 amplitude bytes. The seed is 2,166,136,261 and the multiplier is 16,777,619, with arithmetic modulo 2^32. A safe amplitude must be positive and no greater than its raw amplitude when a gain exists; absent gains have both fields zero.

`library_read_replay_gain` validates the header's version, record size, track count, identity and checksum, the complete table's bounds, and the selected record's checksum and amplitude constraints. It requests at most 44 bytes from its source per lookup. It does not read the entire gain table when opening the library. Legacy files and files without the extension return zero fields. A recognized but invalid extension or a failed read returns false with zero fields, so callers can use their missing-gain policy without accepting unvalidated amplification.

Writers omit the table when no track has gain metadata. When the device builder writes an untagged database into a reserved file with at least four trailing bytes available, it writes a zero extension marker before publishing the base magic. This prevents old gains from surviving when metadata is removed but the base library stays identical. The returned length includes this marker. Pass the actual reserved file capacity to the builder; an unbounded sink is intended for a fresh or truncated file. Capacity checks include the extension, and the base magic is published only after its payload has been written successfully.

CUE parts and file chapters inherit their audio file's gain. Per-track CUE `REM` ReplayGain values are not read. Rescan on the player or rebuild with the desktop indexer to populate an older library's gain data.

## Verification and generation

`test_replay_gain` compares all 19,201 supported centidecibel inputs with independent floating-point exponentiation and checks parsing, duplicate handling and conservative peak limits. `replay_gain_metadata_formats` writes tags into copies of real container fixtures using Mutagen, then reads them through the C tag reader. It also checks ID3 encoding variants, MP3 tag precedence, malformed values and unknown MP4 namespaces.

Regenerate the table with Python 3.14 from the firmware directory:

```sh
python tools/replay_gain_table.py --output src/library/replay_gain_table.h --style .clang-format
```

`replay_gain_table_up_to_date` checks that the committed table matches the generator. Numeric parsing and conversion are part of the library scan target, outside the playback loop. Peak conversion uses all supplied decimal digits; tests cover exact Q24 boundaries and values immediately above them. `test_library_replay_gain` checks legacy files, identity preservation, sorted gain association, corrupt or truncated data, failed writes and removal of a previous reserved-file extension. `replay_gain_cache_matches_desktop` compares independently written extension bytes and reads both database variants through the C reader, then compares real tagged-file and CUE scans between the device and desktop indexers.
