import struct
import unittest
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from echo_image import Image, rockchip_crc


def fixture():
    b=bytearray(0x1000)
    b[0x10:0x18]=b'Rockchip'
    b[0x1f8:0x200]=b'RKnanoFW'
    struct.pack_into('<II',b,0x200,0x03050000,3)
    rows=[(0x03050200,0x03050000,0x100,0x03050400,0x03004000,8,0x03004008,8),
          (0x03050300,0x0307e8c0,0x100,0x03050408,0x03004800,8,0x03004808,8),
          (0x03050300,0x0307e8c0,0x100,0x03050408,0x03004800,8,0x03004808,8)]
    for i,row in enumerate(rows): struct.pack_into('<8I',b,0x208+i*32,*row)
    struct.pack_into('<II',b,0x400,0x03004000,0x03050041)
    b[0x440:0x444]=bytes.fromhex('00 20 70 47')
    struct.pack_into('<I',b,len(b)-4,rockchip_crc(b[:-4]))
    return b


class ContainerTests(unittest.TestCase):
    def test_crc_reference_bitwise(self):
        # Independent bit-at-a-time implementation exercises variant and initialization.
        value=0
        for byte in b'123456789':
            value ^= byte << 24
            for _ in range(8):
                value=((value<<1)^(0x04c10db7 if value&0x80000000 else 0))&0xffffffff
        self.assertEqual(rockchip_crc(b'123456789'),value)

    def test_corrupted_image_refused(self):
        b=fixture(); b[0x500]^=1
        with self.assertRaisesRegex(ValueError,'CRC'): Image(bytes(b))

    def test_out_of_file_module_refused(self):
        b=fixture(); struct.pack_into('<I',b,0x210,0x100000)
        struct.pack_into('<I',b,len(b)-4,rockchip_crc(b[:-4]))
        with self.assertRaisesRegex(ValueError,'exceeds image'): Image(bytes(b))

    def test_overlays_cannot_silently_overlap(self):
        image=Image(bytes(fixture()))
        with self.assertRaisesRegex(ValueError,'Overlapping'): image.analysis_elf([0,1,2])

    def test_elf_preserves_bytes_addresses_entry_and_bss(self):
        image=Image(bytes(fixture()))
        elf,mapping=image.analysis_elf([0,1])
        self.assertEqual(elf[:7],b'\x7fELF\x01\x01\x01')
        self.assertEqual(struct.unpack_from('<I',elf,24)[0],0x03050041)
        phnum=struct.unpack_from('<H',elf,44)[0]
        self.assertEqual(phnum,6)
        for i,region in enumerate(image.regions([0,1])):
            _,offset,address,_,size,memsize,_,_=struct.unpack_from('<8I',elf,52+i*32)
            self.assertEqual(address,region.address)
            self.assertEqual(memsize,region.size)
            expected=b'' if region.file_offset is None else image.data[region.file_offset:region.file_offset+region.size]
            self.assertEqual(elf[offset:offset+size],expected)
        self.assertEqual(mapping['source_sha256'],image.digest)

    def test_missing_vector_table_refused(self):
        b=fixture(); struct.pack_into('<I',b,0x210,0)
        struct.pack_into('<I',b,len(b)-4,rockchip_crc(b[:-4]))
        with self.assertRaisesRegex(ValueError,'vector table'): Image(bytes(b))

    def test_non_thumb_entry_refused(self):
        b=fixture(); struct.pack_into('<I',b,0x404,0x03050040)
        struct.pack_into('<I',b,len(b)-4,rockchip_crc(b[:-4]))
        with self.assertRaisesRegex(ValueError,'Thumb'): Image(bytes(b)).analysis_elf([0])

if __name__=='__main__': unittest.main()
