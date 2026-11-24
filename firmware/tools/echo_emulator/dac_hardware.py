import struct

from .devices import RegisterFile

DAC_LEFT_ADDRESS = 0x60
DAC_RIGHT_ADDRESS = 0x66
DAC_GAIN_REGISTER = 0x080000
DAC_FILTER_REGISTER = 0x090000
DAC_VOLUME_B_REGISTER = 0x090001
DAC_VOLUME_A_REGISTER = 0x090002
DAC_MUTE_REGISTER = 0x090003
DAC_GAIN_HIGH_VALUE = 0x31
DAC_GAIN_LOW_VALUE = 0x20
I2C_LATENCY_US = 200

DMAC_COMP_TYPE = 0x3F8
DMAC_CHANNEL_ENABLE = 0x3A0
PUSH_CFG_LOW = 0x40
PUSH_BIT = 0x01
PUSH_WRITE_ENABLE = 0x0100
AUDIO_BASE = 0x58 * 5
AUDIO_SAR = AUDIO_BASE + 0x00
AUDIO_LLP = AUDIO_BASE + 0x10
AUDIO_CFG_LOW = AUDIO_BASE + 0x40
AUDIO_BIT = 0x20
AUDIO_WRITE_ENABLE = 0x2000
CFG_SUSPEND = 0x100
CFG_FIFO_EMPTY = 0x200
ITEM_BYTES = 24
ITEM_NEXT = 8
ITEM_WORDS = 16


class RealI2sRegs(RegisterFile):
    def write_word(self, offset, value):
        if offset == 0x20:
            self.regs[offset] = 0
            return
        super().write_word(offset, value)


class RealCs43198Bus(RegisterFile):
    CON = 0x00
    MRXADDR = 0x08
    MTXCNT = 0x10
    MRXCNT = 0x14
    IPD = 0x1C
    TXDATA = 0x100
    RXDATA = 0x200
    START = 0x08
    STOP = 0x10
    TX_DONE = 0x04
    RX_DONE = 0x08
    START_DONE = 0x10
    STOP_DONE = 0x20
    NAK = 0x40

    def __init__(self, machine, latency_us=I2C_LATENCY_US):
        super().__init__()
        self.machine = machine
        self.latency_us = latency_us
        self.chips = {
            DAC_LEFT_ADDRESS: {0x020000: 0xF6, 0x0F0000: 0x00},
            DAC_RIGHT_ADDRESS: {0x020000: 0xF6, 0x0F0000: 0x00},
        }
        self.pointer = {DAC_LEFT_ADDRESS: 0, DAC_RIGHT_ADDRESS: 0}
        self.pending_bits = 0
        self.ready_us = 0
        self.power_down_ready_us = {}
        self.transactions = 0
        self.writes = []
        self.fail_register = None
        self.fail_all = False

    def schedule(self, bits, delay_us):
        self.pending_bits |= bits
        self.ready_us = self.machine.microseconds() + delay_us

    def read_word(self, offset):
        if offset == self.IPD:
            if self.pending_bits and self.machine.microseconds() >= self.ready_us:
                self.regs[self.IPD] = self.regs.get(self.IPD, 0) | self.pending_bits
                self.pending_bits = 0
            return self.regs.get(self.IPD, 0)
        return super().read_word(offset)

    def sent_bytes(self, count):
        words = range((count + 3) // 4)
        return b"".join(self.regs.get(self.TXDATA + 4 * word, 0).to_bytes(4, "little")
                        for word in words)[:count]

    def transmit(self, count):
        frame = self.sent_bytes(count)
        self.transactions += 1
        address = frame[0] if frame else 0
        if self.fail_all or address not in self.chips or count < 5:
            self.schedule(self.NAK, self.latency_us)
            return
        reg_map = frame[1] << 16 | frame[2] << 8 | frame[3]
        if self.fail_register is not None and reg_map == self.fail_register:
            self.schedule(self.NAK, self.latency_us)
            return
        self.pointer[address] = reg_map
        if count >= 6:
            before = self.chips[address].get(reg_map, 0)
            self.chips[address][reg_map] = frame[5]
            self.writes.append((self.machine.microseconds(), address, reg_map, frame[5]))
            if reg_map == 0x020000 and (frame[5] & 0x10) and not (before & 0x10):
                self.power_down_ready_us[address] = self.machine.microseconds() + 10000
                self.chips[address][0x0F0000] = 0x00
        self.schedule(self.TX_DONE, self.latency_us)

    def receive(self, count):
        address = (self.regs.get(self.MRXADDR, 0) & 0xFF) - 1
        self.transactions += 1
        if self.fail_all or address not in self.chips:
            self.schedule(self.NAK, self.latency_us)
            return
        reg_map = self.pointer[address]
        value = self.chips[address].get(reg_map, 0)
        if reg_map == 0x0F0000 and address in self.power_down_ready_us:
            if self.machine.microseconds() >= self.power_down_ready_us[address]:
                value = 0x01
                self.chips[address][0x0F0000] = value
        self.regs[self.RXDATA] = value
        self.schedule(self.RX_DONE, self.latency_us)

    def write_word(self, offset, value):
        if offset == self.IPD:
            self.regs[self.IPD] = self.regs.get(self.IPD, 0) & ~value
            return
        super().write_word(offset, value)
        if offset == self.CON and value & self.START:
            self.schedule(self.START_DONE, 10)
        elif offset == self.CON and value & self.STOP:
            self.schedule(self.STOP_DONE, 10)
        elif offset == self.MTXCNT:
            self.transmit(value)
        elif offset == self.MRXCNT:
            self.receive(value)


class RealDmac(RegisterFile):
    def __init__(self, machine, rate_fn):
        super().__init__()
        self.machine = machine
        self.base = machine.dmac
        self.rate_fn = rate_fn
        self.running = False
        self.suspended = False
        self.started_us = 0
        self.paused_us = 0
        self.items = 0
        self.dma_ring = 0
        self.starts = 0
        self.stops = 0
        self.suspends = 0
        self.resumes = 0
        self.bad_disables = 0

    def attach(self):
        self.machine.uc.mem_unmap(0x60000000, 0x1000)
        self.machine.map_device(self, 0x60000000, 0x1000)

    def elapsed_us(self):
        now = self.paused_us if self.suspended else self.machine.microseconds()
        return now - self.started_us

    def ring_words(self):
        total = 0
        item = self.items
        for _ in range(64):
            words = struct.unpack_from("<I", self.machine.read_memory(item, ITEM_BYTES), ITEM_WORDS)[0]
            total += words
            item = struct.unpack_from("<I", self.machine.read_memory(item, ITEM_BYTES), ITEM_NEXT)[0]
            if item == self.items:
                break
        return total

    def current_sar(self):
        if not self.running or not self.items:
            return self.regs.get(AUDIO_SAR, 0)
        rate = self.rate_fn()
        words = self.elapsed_us() * rate * 2 // 1000000
        return self.dma_ring + (words % self.ring_words()) * 4

    def read_word(self, offset):
        if offset == DMAC_COMP_TYPE:
            return 0x44571110
        if offset == AUDIO_SAR:
            return self.current_sar()
        if offset == DMAC_CHANNEL_ENABLE:
            push = self.base.read_word(DMAC_CHANNEL_ENABLE) & PUSH_BIT
            return push | (AUDIO_BIT if self.running else 0)
        if offset < AUDIO_BASE:
            return self.base.read_word(offset)
        return super().read_word(offset)

    def write_word(self, offset, value):
        if offset == PUSH_CFG_LOW:
            if value & CFG_SUSPEND:
                value |= CFG_FIFO_EMPTY
            else:
                value &= ~CFG_FIFO_EMPTY
            self.base.write_word(offset, value)
            return
        if offset == AUDIO_CFG_LOW:
            if value & CFG_SUSPEND:
                if self.running and not self.suspended:
                    self.paused_us = self.machine.microseconds()
                    self.suspended = True
                    self.suspends += 1
                value |= CFG_FIFO_EMPTY
            else:
                if self.running and self.suspended:
                    self.started_us += self.machine.microseconds() - self.paused_us
                    self.suspended = False
                    self.resumes += 1
                value &= ~CFG_FIFO_EMPTY
            super().write_word(offset, value)
            return
        if offset == DMAC_CHANNEL_ENABLE:
            if value & PUSH_WRITE_ENABLE:
                if not (value & PUSH_BIT) and (self.base.read_word(DMAC_CHANNEL_ENABLE) & PUSH_BIT):
                    if not (self.base.read_word(PUSH_CFG_LOW) & CFG_SUSPEND):
                        self.bad_disables += 1
                self.base.write_word(offset, value)
            if value & AUDIO_WRITE_ENABLE:
                if value & AUDIO_BIT:
                    self.items = self.regs.get(AUDIO_LLP, 0)
                    self.dma_ring = struct.unpack("<I", self.machine.read_memory(self.items, 4))[0]
                    self.started_us = self.machine.microseconds()
                    self.running = True
                    self.suspended = False
                    self.starts += 1
                else:
                    if self.running:
                        if not self.suspended:
                            self.bad_disables += 1
                        self.running = False
                        self.suspended = False
                        self.stops += 1
            return
        if offset < AUDIO_BASE:
            self.base.write_word(offset, value)
            return
        super().write_word(offset, value)
