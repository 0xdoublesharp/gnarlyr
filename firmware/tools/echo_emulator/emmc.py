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

READ_COMMAND_US = 250
READ_EXTRA_BLOCK_US = 25
WRITE_COMMAND_US = 900
WRITE_BLOCK_US = 70
STATUS_COMMAND_US = 15
POLL_JUMP_CYCLES = 240
STATUS_CARD_BUSY = 1 << 9
MMC_CLKENA = 0x10
CLKENA_CARD_CLOCK = 0x1

CTRL_RESET_BITS = 0x7
CTRL_FIFO_RESET = 0x2
FIFOTH_RESET_VALUE = 0x000F0000


class SparseDisk:
    def __init__(self, sector_count):
        self.sector_count = sector_count
        self.sectors = {}
        self.sources = []

    def add_source(self, first_lba, size, source):
        self.sources.append((first_lba, (size + SECTOR - 1) // SECTOR, source))

    def generated(self, lba):
        for first, count, source in self.sources:
            if first <= lba < first + count:
                return first, source
        return None

    def read_sector(self, lba):
        stored = self.sectors.get(lba)
        if stored is not None:
            return stored
        found = self.generated(lba)
        if found is None:
            return bytes(SECTOR)
        first, source = found
        return source.read((lba - first) * SECTOR, SECTOR)

    def read(self, lba, count):
        return b"".join(self.read_sector(lba + index) for index in range(count))

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
        self.read_log = []
        self.accesses = []
        self.commands = []
        self.pending_write = None
        self.failed_starts = 0
        self.starts = 0

    def ext_csd(self):
        data = bytearray(SECTOR)
        struct.pack_into("<I", data, EXT_CSD_SECTOR_COUNT, self.disk.sector_count)
        return bytes(data)

    def cid(self):
        return (0x11223344, 0x5566A1B2, 0x778899AA, 0xBBCCDDEE)

    def command(self, index, argument):
        self.commands.append((self.machine.cycles(), index, argument))
        if index == 0:
            self.starts += 1
            return ()
        if index == 1:
            return (0, 0, 0, 0) if self.starts <= self.failed_starts else (OCR_READY_SECTOR_MODE, 0, 0, 0)
        if index == 2 or index == 9:
            return self.cid()
        return (READY_TRANSFER_STATUS, 0, 0, 0)

    def read_blocks(self, index, argument, count):
        if index == 8:
            return self.ext_csd()
        self.reads.append((self.machine.cycles(), argument, count))
        self.read_log.append((self.machine.milliseconds(), argument, count))
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
        self.busy_until = 0
        self.resets = []
        self.regs[MMC_FIFOTH] = FIFOTH_RESET_VALUE

    def hard_reset(self):
        commands = len(self.card.commands) if self.card is not None else 0
        self.resets.append((self.machine.milliseconds(), commands))
        self.regs = {MMC_FIFOTH: FIFOTH_RESET_VALUE}
        self.read_queue.clear()
        self.write_words = []
        self.write_target = None
        self.busy_until = 0

    def busy(self):
        remaining = self.busy_until - self.machine.cycles()
        if remaining > 0:
            self.machine.poll_jump(min(remaining, POLL_JUMP_CYCLES))
        return self.busy_until > self.machine.cycles()

    def read(self, offset, size):
        if offset == MMC_FIFO:
            return self.read_queue.popleft() if self.read_queue and not self.busy() else 0
        return super().read(offset, size)

    def read_word(self, offset):
        if offset == MMC_FIFO:
            return self.read_queue.popleft() if self.read_queue and not self.busy() else 0
        if offset == MMC_STATUS:
            waiting = self.busy()
            count = 0 if waiting else len(self.read_queue)
            empty = STATUS_FIFO_EMPTY if count == 0 else 0
            return empty | (count << STATUS_FIFO_COUNT_SHIFT) | (STATUS_CARD_BUSY if waiting and not self.read_queue else 0)
        if offset == MMC_RINTSTS:
            value = super().read_word(offset)
            return value & ~INT_DATA_OVER if self.busy() else value
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
        if self.card is None or not self.regs.get(MMC_CLKENA, 0) & CLKENA_CARD_CLOCK:
            self.raise_status(INT_COMMAND_DONE | INT_RESPONSE_TIMEOUT)
            return
        response = self.card.command(index, argument)
        self.busy_until = self.machine.cycles() + self.latency_cycles(index, value)
        for position in range(4):
            self.regs[MMC_RESP0 + 4 * position] = response[position] if position < len(response) else 0
        self.raise_status(INT_COMMAND_DONE)
        if value & CMD_DATA:
            self.start_data(index, argument, value)

    def latency_cycles(self, index, value):
        count = max(1, self.regs.get(MMC_BYTCNT, SECTOR) // SECTOR)
        if value & CMD_DATA and value & CMD_WRITE:
            microseconds = WRITE_COMMAND_US + WRITE_BLOCK_US * count
        elif value & CMD_DATA:
            microseconds = READ_COMMAND_US + READ_EXTRA_BLOCK_US * (count - 1)
        else:
            microseconds = STATUS_COMMAND_US
        return microseconds * self.machine.core_hz // 1000000

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


class SdCard(EmmcCard):
    def command(self, index, argument):
        self.commands.append((self.machine.cycles(), index, argument))
        if index == 0:
            return ()
        if index == 8:
            return (0x1AA, 0, 0, 0)
        if index == 41:
            return (OCR_READY_SECTOR_MODE, 0, 0, 0)
        if index == 3:
            return (0x12340000, 0, 0, 0)
        if index == 9:
            csd = (1 << 126) | ((self.disk.sector_count // 1024 - 1) << 48) | (9 << 22)
            return tuple((csd >> (32 * word)) & 0xFFFFFFFF for word in range(4))
        return (READY_TRANSFER_STATUS, 0, 0, 0)

    def read_blocks(self, index, argument, count):
        if index == 13:
            status = bytearray(64)
            status[10] = 0x90
            return bytes(status)
        return super().read_blocks(index, argument, count)
