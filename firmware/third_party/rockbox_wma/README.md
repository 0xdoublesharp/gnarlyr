# Fixed-point WMA Standard decoder

Source: [Rockbox codec libraries](https://github.com/Rockbox/rockbox/tree/a587ee311c39f9a999a944a726f3406ffb726821/lib/rbcodec/codecs), revision recorded in `COMMIT`.

The retained WMA decoder, fixed-point FFT/IMDCT, lookup tables and bit-reader declarations come from `libwma` and `lib`. The ASF declaration comes from `libasf`; Gnarlyr supplies its own checked demuxer.

Licenses are preserved in each source file and in `COPYING.GPL-2`, `COPYING.LGPL-2.1` and `COPYING.BSD`. The WMA/transform lineage is LGPL-2.0-or-later; the bit-reader files are LGPL-2.1-or-later; Michael Giacomelli's fixed-point helpers are GPL-2.0-or-later; Xiph's math/lookup material has BSD terms. These terms permit inclusion in Gnarlyr under GPL-3.0-or-later. Copyright holders include the FFmpeg Project, Michael Niedermayer, Michael Giacomelli, Dave Hooper, Mohamed Tarek and the Xiph.Org Foundation.

Local changes:

- Portable integer math is used on Cortex-M3 and the host. CORDIC phase and accumulator types have explicit 32-bit widths.
- Window tables are generated through `tools/firmware.py regen` from the pinned CORDIC constants and kept read-only. The noise table is read-only too; LSP mode scales samples at use.
- Work arrays are assigned to a bounded core-1 workspace. The temporary spectral coefficients share the IMDCT scratch storage after coefficient conversion completes. Decoder globals support one active stream; native callers serialize access.
- The high-band boundary calculation uses a 64-bit intermediate, and its output band count is checked. A generated 48 kHz fixture reproduced the original overflow under the host sanitizer.
- Bit reads and skips are bounded. Reservoir reads use actual packet lengths, with explicit padding; malformed input exits through a checkpoint.
- Gnarlyr builds VLC tables iteratively in fixed buffers, replacing the heap/recursive builder. VLC lookup honors the full declared depth.
- The stream wrapper applies the FFT transform's output normalization, discards two priming frames and drains the final overlap, matching independent FFmpeg decoding.

The runtime uses no heap. `port/` contains the small compatibility headers and generated windows. Source formatting and original notices are retained.
