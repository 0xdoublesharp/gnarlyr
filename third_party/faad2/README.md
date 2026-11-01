# FAAD2

Freeware Advanced Audio Decoder 2.11.4 (`VERSION`), Copyright (c) Nero AG, www.nero.com, licensed under GPL-2.0-or-later (`COPYING`; every source file carries the notice). Code from FAAD2 is copyright (c) Nero AG, www.nero.com.

Only the files an AAC-LC decoder needs are vendored: `bits`, `cfft`, `common`, `decoder`, `drc`, `error`, `filtbank`, `huffman`, `is`, `mdct`, `mp4`, `ms`, `output`, `pns`, `pulse`, `specrec`, `syntax` and `tns`, their headers, the Huffman codebooks and `neaacdec.h`. SBR, PS, LTP, SSR, Main-profile prediction, error resilience, DRM and LD are not included.

Build settings (`platform/device/logic/CMakeLists.txt`, target `echo_faad`): `FIXED_POINT`, `LC_ONLY_DECODER`, `DISABLE_SBR`, `FAAD_NO_ARM_ASM`.

## Changes to the upstream files

- `fixed.h`: the ARM inline assembly (ARM state, not Thumb) is skipped when `FAAD_NO_ARM_ASM` is defined, so the portable 64-bit multiplies are used.
- `syntax.c`: the two 24 KB element scratch areas of `single_lfe_channel_element` and `channel_pair_element` live in one static `element_scratch` instead of on the stack, so the decoder fits the 24 KB stack of the second core. The decoder is not reentrant.
