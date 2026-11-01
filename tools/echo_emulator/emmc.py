import struct
from collections import deque

from .devices import (
    CMD_DATA, CMD_START, CMD_UPDATE_CLOCK_ONLY, CMD_WRITE, INT_COMMAND_DONE,
    INT_DATA_OVER, INT_RESPONSE_TIMEOUT, MMC_BYTCNT, MMC_CMD, MMC_CMDARG, MMC_CTRL,
    MMC_FIFO, MMC_FIFOTH, MMC_RESP0, MMC_RINTSTS, MMC_STATUS, STATUS_FIFO_COUNT_SHIFT,
    STATUS_FIFO_EMPTY, RegisterFile)

SECTOR = 512
IDB_MAGIC = 0xFCDC8C3B
IDB_FIRST = 0x40
IDB_STRIDE = 0x400
IDB_COPIES = 5
IDB_SYSTEM_MIB = 0x202
IDB_TAG = 0x20A
FIRMWARE_START = 0x2000
SECTORS_PER_MIB = 0x800
SLOT_2_OFFSET = 0x2800
EXT_CSD_SECTOR_COUNT = 212
READY_TRANSFER_STATUS = 0x900
OCR_READY_SECTOR_MODE = 0xC0FF8080

CTRL_RESET_BITS = 0x7
CTRL_FIFO_RESET = 0x2
FIFOTH_RESET_VALUE = 0x000F0000


class SparseDisk:
    def __init__(self, sector_count):
        self.sector_count = sector_count
        self.sectors = {}

    def read(self, lba, count):
        return b"".join(self.sectors.get(lba + index, bytes(SECTOR)) for index in range(count))

    def write(self, lba, data):
        for index in range(len(data) // SECTOR):
            chunk = data[index * SECTOR:(index + 1) * SECTOR]
            if any(chunk):
                self.sectors[lba + index] = bytes(chunk)
            else:
                self.sectors.pop(lba + index, None)

    def snapshot(self):
        return dict(self.sectors)


class EmmcCard:
    def __init__(self, disk, machine):
        self.disk = disk
        self.machine = machine
        self.writes = []
        self.reads = []
        self.accesses = []
        self.commands = []
        self.pending_write = None

    def ext_csd(self):
        data = bytearray(SECTOR)
        struct.pack_into("<I", data, EXT_CSD_SECTOR_COUNT, self.disk.sector_count)
        return bytes(data)

    def cid(self):
        return (0x11223344, 0x5566A1B2, 0x778899AA, 0xBBCCDDEE)

    def command(self, index, argument):
        self.commands.append((self.machine.cycles(), index, argument))
        if index == 0:
            return ()
        if index == 1:
            return (OCR_READY_SECTOR_MODE, 0, 0, 0)
        if index == 2 or index == 9:
            return self.cid()
        return (READY_TRANSFER_STATUS, 0, 0, 0)

    def read_blocks(self, index, argument, count):
        if index == 8:
            return self.ext_csd()
        self.reads.append((self.machine.cycles(), argument, count))
        self.accesses.append(("R", argument, count))
        return self.disk.read(argument, count)

    def write_blocks(self, argument, data):
        count = len(data) // SECTOR
        self.writes.append((self.machine.stamp(), argument, count))
        self.accesses.append(("W", argument, count, struct.unpack_from("<I", data)[0]))
        self.disk.write(argument, data)


class DwMmc(RegisterFile):
    def __init__(self, machine, card):
        super().__init__()
        self.machine = machine
        self.card = card
        self.read_queue = deque()
        self.write_words = []
        self.write_target = None
        self.regs[MMC_FIFOTH] = FIFOTH_RESET_VALUE

    def read(self, offset, size):
        if offset == MMC_FIFO:
            return self.read_queue.popleft() if self.read_queue else 0
        return super().read(offset, size)

    def read_word(self, offset):
        if offset == MMC_FIFO:
            return self.read_queue.popleft() if self.read_queue else 0
        if offset == MMC_STATUS:
            count = len(self.read_queue)
            empty = STATUS_FIFO_EMPTY if count == 0 else 0
            return empty | (count << STATUS_FIFO_COUNT_SHIFT)
        return super().read_word(offset)

    def write_word(self, offset, value):
        if offset == MMC_RINTSTS:
            self.regs[offset] = self.regs.get(offset, 0) & ~value
        elif offset == MMC_CTRL:
            if value & CTRL_FIFO_RESET or value & CTRL_RESET_BITS == CTRL_RESET_BITS:
                self.read_queue.clear()
                self.write_words = []
            self.regs[offset] = value & ~CTRL_RESET_BITS
        elif offset == MMC_FIFO:
            self.accept_word(value)
        elif offset == MMC_CMD:
            self.regs[offset] = value & ~CMD_START
            if value & CMD_START:
                self.execute(value)
        else:
            super().write_word(offset, value)

    def raise_status(self, bits):
        self.regs[MMC_RINTSTS] = self.regs.get(MMC_RINTSTS, 0) | bits

    def execute(self, value):
        if value & CMD_UPDATE_CLOCK_ONLY:
            return
        index = value & 0x3F
        argument = self.regs.get(MMC_CMDARG, 0)
        if self.card is None:
            self.raise_status(INT_COMMAND_DONE | INT_RESPONSE_TIMEOUT)
            return
        response = self.card.command(index, argument)
        for position in range(4):
            self.regs[MMC_RESP0 + 4 * position] = response[position] if position < len(response) else 0
        self.raise_status(INT_COMMAND_DONE)
        if value & CMD_DATA:
            self.start_data(index, argument, value)

    def start_data(self, index, argument, value):
        count = max(1, self.regs.get(MMC_BYTCNT, SECTOR) // SECTOR)
        if value & CMD_WRITE:
            self.write_target = (argument, count)
            self.write_words = []
            return
        data = self.card.read_blocks(index, argument, count)
        self.read_queue = deque(struct.unpack("<%dI" % (len(data) // 4), data))
        self.raise_status(INT_DATA_OVER)

    def accept_word(self, value):
        if self.write_target is None:
            return
        self.write_words.append(value)
        address, count = self.write_target
        if len(self.write_words) == count * SECTOR // 4:
            data = struct.pack("<%dI" % len(self.write_words), *self.write_words)
            self.write_target = None
            self.write_words = []
            self.card.write_blocks(address, data)
            self.raise_status(INT_DATA_OVER)


def build_idb(system_mib):
    block = bytearray(2 * SECTOR)
    struct.pack_into("<I", block, 0, IDB_MAGIC)
    struct.pack_into("<H", block, IDB_SYSTEM_MIB, system_mib)
    block[IDB_TAG:IDB_TAG + 4] = b"NANO"
    return bytes(block)


def synthetic_emmc(machine, image, volume, system_mib=96):
    firmware_end = FIRMWARE_START + system_mib * SECTORS_PER_MIB
    disk = SparseDisk(firmware_end + volume.sector_count)
    for copy in range(IDB_COPIES):
        disk.write(IDB_FIRST + copy * IDB_STRIDE, build_idb(system_mib))
    disk.write(FIRMWARE_START, image)
    disk.write(FIRMWARE_START + SLOT_2_OFFSET, image)
    volume.write_to(disk, firmware_end)
    layout = {
        "slot_1": FIRMWARE_START,
        "slot_2": FIRMWARE_START + SLOT_2_OFFSET,
        "volume_start": firmware_end,
        "volume_sectors": volume.sector_count,
    }
    return EmmcCard(disk, machine), layout
