import struct

import make_image

from .emmc import EmmcCard, synthetic_emmc
from .fat_volume import Fat32Volume, short_name
from .machine import Machine

LOADER_REQUEST_NAME = "MASKROM.REQ"
LOADER_REQUEST_TEXT = b"MASKROM REQ"
IMAGE_DATE_OFFSET = 0
BRAND_OFFSET = 0x10
BRAND_SIZE = 8
STOCK_SIZE = 0x2000004
FRAME_LIMIT_MS = 20000
SLOT_BYTES = make_image.fw_end(make_image.FW_SIZE)


class Clock:
    def cycles(self):
        return 0


def repack(image, header_edit):
    data = bytearray(image)
    header_edit(data, 0)
    header_edit(data, make_image.FW_SIZE)
    end = make_image.fw_end(make_image.FW_SIZE)
    struct.pack_into("<I", data, end, make_image.crc32(data[:end]))
    return bytes(data)


def with_date(image, date):
    value = make_image.bcd_date(date)

    def edit(data, base):
        struct.pack_into("<I", data, base + IMAGE_DATE_OFFSET, value)

    return repack(image, edit)


def with_brand(image, brand):
    text = make_image.text_field(brand, "brand")

    def edit(data, base):
        data[base + BRAND_OFFSET:base + BRAND_OFFSET + BRAND_SIZE] = text

    return repack(image, edit)


class Session:
    def __init__(self, image, elf_path, files=(), vbus_after_first_frame=False, volume=None,
                 vbus_at_start=False, reuse=None, usb_overlay_elf=None):
        self.image = image
        self.elf_path = elf_path
        self.usb_overlay_elf = usb_overlay_elf
        if reuse is not None:
            volume = reuse.volume
            card, self.layout = EmmcCard(reuse.disk, Clock()), reuse.layout
        else:
            if volume is None:
                volume = Fat32Volume()
                for name, data, size in files:
                    volume.add_file(name, data, size)
            card, self.layout = synthetic_emmc(Clock(), self.image, volume)
        self.card = card
        self.disk = card.disk
        self.machine = Machine(self.image, elf_path, card, usb_overlay_elf)
        card.machine = self.machine
        self.volume = volume
        self.before = self.disk.snapshot()
        self.vbus_after_first_frame = vbus_after_first_frame
        self.machine.vbus = vbus_at_start

    def reboot(self, image=None, vbus_after_first_frame=False):
        return Session(image or self.image, self.elf_path, reuse=self,
                       vbus_after_first_frame=vbus_after_first_frame,
                       usb_overlay_elf=self.usb_overlay_elf)

    def write_slots(self, image):
        self.disk.write(self.layout["slot_1"], image)
        self.disk.write(self.layout["slot_2"], image)

    def damage_usb_overlay(self, slot):
        lba = self.layout[slot] + make_image.USB_OVERLAY_OFFSET // 512 + 1
        sector = bytearray(self.disk.read(lba, 1))
        sector[0] ^= 0xFF
        self.disk.write(lba, bytes(sector))
        self.before = self.disk.snapshot()

    def usb_overlay_reads(self, slot):
        first = self.layout[slot] + make_image.USB_OVERLAY_OFFSET // 512
        last = first + make_image.USB_OVERLAY_CAPACITY // 512
        return [(lba, count) for _, lba, count in self.card.reads if first <= lba < last]

    def root_entry_start(self, name):
        raw = short_name(name)
        cluster = self.layout["volume_start"] + self.volume.cluster_lba(2)
        data = self.disk.read(cluster, self.volume.sectors_per_cluster)
        for offset in range(0, len(data), 32):
            if data[offset:offset + 11] == raw:
                return data[offset:offset + 32]
        return None

    def file_exists(self, name):
        return self.root_entry_start(name) is not None

    def fat_entry(self, cluster):
        lba = self.layout["volume_start"] + 32 + cluster // 128
        return struct.unpack_from("<I", self.disk.read(lba, 1), cluster % 128 * 4)[0] & 0x0FFFFFFF

    @property
    def panel(self):
        return self.machine.panel

    def run_to_first_frame(self):
        machine = self.machine
        reason = machine.run(until=lambda: len(self.panel.lit_sweeps()) >= 1,
                             until_ms=FRAME_LIMIT_MS)
        if self.vbus_after_first_frame and reason == "condition met":
            machine.vbus = True
        return reason

    def run_to_home_screen(self):
        return self.machine.run(until=lambda: len(self.panel.lit_sweeps()) >= 2,
                                until_ms=FRAME_LIMIT_MS)

    def run_until_reset(self):
        return self.machine.run(until_ms=FRAME_LIMIT_MS)

    def run_until_waiting_for_key(self, presses=0, limit_ms=FRAME_LIMIT_MS):
        self.machine.answer_waits_for_key(presses)
        return self.machine.run(until_ms=limit_ms)

    def run_through_update_check(self, limit_ms=FRAME_LIMIT_MS):
        self.machine.answer_waits_for_key(1)
        self.machine.stop_at("usb_session_storage_changed")
        return self.machine.run(until_ms=limit_ms)

    def written_ranges(self):
        return [(lba, count) for _, lba, count in self.card.writes]

    def file_lba(self, name):
        cluster = self.volume.first_cluster(name)
        return self.layout["volume_start"] + self.volume.cluster_lba(cluster)

    def read_file(self, name, length):
        return self.disk.read(self.file_lba(name), (length + 511) // 512)[:length]

    def slot_image(self, slot):
        return self.disk.read(self.layout[slot], SLOT_BYTES // 512)

    def changed_sectors(self):
        after = self.disk.sectors
        keys = set(after) | set(self.before)
        return sorted(lba for lba in keys if after.get(lba) != self.before.get(lba))
