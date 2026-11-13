# libdemac

Source: [Rockbox](https://github.com/Rockbox/rockbox/tree/a587ee311c39f9a999a944a726f3406ffb726821/lib/rbcodec/codecs/demac/libdemac), revision recorded in `COMMIT`.

Dave Chapman's fixed-point Monkey's Audio decoder is GPL-2.0-or-later; `COPYING` preserves the license. The range decoder also credits Michael Schindler (1997–2000), under the same terms. Original copyright and license notices remain in the source.

Retained: the portable decoder, entropy coder, predictor, five filter instantiations, generic vector math, CRC and declarations. Every build selects the generic vector implementation, including x86 hosts; the imported architecture-specific vector headers are absent. The file parser and architecture-specific assembly are not built. Gnarlyr supplies a checked file parser, bounded record transport and core-1 entry point.

Local changes add a checked input boundary, a failure checkpoint, bounded range-symbol search, Rice-shift bounds and zero-divisor refusal. Arithmetic uses wrapping signed operations; upstream signed shifts are compiled without shift sanitization, matching the other fixed-point vendors. No heap is used. Decoder globals belong to one active core-1 stream; native callers must serialize decoder use.
