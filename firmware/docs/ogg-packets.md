# Ogg packet input

`codec/ogg_packet.h` reads one sequential logical Ogg stream through a bounded `FlacInput` callback. It reconstructs packet boundaries across pages into caller-owned storage. The caller chooses the packet capacity; the reader uses no allocation or recursion. It follows [Xiph Ogg framing](https://www.xiph.org/ogg/doc/framing.html).

Initialize `OggPackets` with `ogg_packets_init`, then call `ogg_packets_next`. A ready packet reports its byte count, first/last flags and a 64-bit granule position. Only the last packet completed on a page receives that page's granule; other packets report `UINT64_MAX`. Empty packets are valid. A packet whose length is a multiple of 255 still consumes its terminal zero lace.

Every new page is checked in full before any of its packets is returned: capture pattern, version, flags, bounded header/body lengths, serial number, sequence, continuation and CRC-32 with polynomial `0x04C11DB7`, initial value zero, no reflection or final XOR. The image-container CRC uses a different polynomial and cannot substitute for this check. A hole, changed serial, invalid checksum, truncated page or unfinished final packet is refused. Chained and multiplexed logical streams require a separate caller policy; this reader does not switch streams implicitly or search past corruption.

`OGG_PACKET_WAIT` means a bounded callback read failed. `OGG_PACKET_TOO_LARGE` means the supplied packet buffer is too small. Neither changes the reader or packet metadata, so the caller can retry. Partial bytes in the caller's buffer are unspecified until `OGG_PACKET_READY`. `OGG_PACKET_END` requires an end-of-stream page. `OGG_PACKET_INVALID` reports malformed input. The input must remain unchanged during a call.

A core-1 ring adapter must retain every byte the reader can revisit until a call commits. Page CRC verification can read a full 65,307-byte page before it returns a packet; packet capacity alone therefore does not bound the ring retention requirement. Direct file callbacks have no such ring restriction.

The native packet tests cover a real Vorbis fixture, maximum-size pages, zero-length packets, continued packets, final granules, truncated/corrupt input and exact retry after a failed read at every byte of a multi-page packet. `ogg_packet_tool INPUT OUTPUT` exports checked packets for decoder comparisons as little-endian length (4 bytes), first/last flags (4 bytes), granule (8 bytes), then payload. It is a host test utility.

This packet reader does not decode Vorbis audio. Player playback still needs the decoder, bounded setup/scratch storage, seeking, trimming and format selection.
