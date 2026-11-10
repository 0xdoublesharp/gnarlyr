# Tremor integer Vorbis decoder

Xiph.Org Foundation Tremor lowmem branch, pinned in `COMMIT`, from https://gitlab.xiph.org/xiph/tremor. Copyright 1994–2003 Xiph.Org Foundation; BSD-style license in `COPYING` and each source notice. The pinned commit includes the backported 2019 codebook bounds fix.

Only the codec is included. Gnarlyr supplies Ogg framing and file access. This private copy must be entered through the guarded Vorbis decoder wrapper; it is not a general libvorbisfile replacement.

Changes from upstream:

- Allocation uses a caller-owned bounded workspace. Temporary allocations replace `alloca` and restore their workspace cursor at each function exit. Exhaustion leaves the decoder through its guarded failure path before a null pointer can be returned to unchecked upstream code. Permanent storage resets between tracks; individual frees are no-ops.
- PCM output is planar 24-bit in signed 32-bit containers. The overlap-add retains eight more fractional bits than upstream's interleaved 16-bit output and clamps to the 24-bit range.
- Header reads stop at the first missing bit, and bit-read widths are checked before indexing the mask table. This prevents malformed setup fields from propagating through partially initialized state. Audio packets retain upstream end-of-packet handling.
- Setup arrays that cleanup visits are zero-initialized before parsing their entries, so a truncated setup cannot send uninitialized pointers into floor/mapping/residue cleanup.
- Codebook product bounds follow libvorbis; ordered lengths cannot exceed 32 bits. Lattice lookup size uses bounded integer arithmetic, including zero-dimension scalar books. Residue partition/vector books require a positive dimension before their arithmetic is used.
- Fixed-width types come from `stdint.h`, so PCM pointers use the same C type as the hardware interface even when the toolchain defines `int32_t` as `long`.
- Portable wide multiplies use explicit integer shifts instead of platform endian macros and union layout. The bit selection matches upstream, including `MULT31` precision.
- Scalar codebook failure explicitly converts the packed unsigned sentinel to a signed 32-bit value so native 64-bit-long tests retain the ARM result.

Build as portable fixed-point C for Cortex-M3, without ARM-state assembly, with wrapping signed arithmetic and unsigned plain char. The wrapper owns stream limits and caller buffers. Keep these changes documented when importing upstream fixes.

The library uses wrapping fixed-point arithmetic and intentional signed shifts; its translation units disable the shift sanitizer only. Other undefined-behavior checks and address instrumentation remain enabled in host test builds. The wrapper and workspace implementation use the project's full checks.
