GPIO_DATA = 0x00
GPIO_DIRECTION = 0x04
GPIO_INPUT_LEVEL = 0x50

CRU_PLL_CON1 = 0x04
CRU_MODE = 0x10
CRU_CLOCK_SELECT = 0x14
CORE_DIVIDER_MASK = 0x0007
CRU_PCLK_SELECT = 0x18
PLL_LOCKED = 0x400

GRF_VBUS = 0xE8
GRF_VBUS_PRESENT = 1 << 1

PWM_PERIOD = 0x04
PWM_DUTY = 0x08
PWM_CONTROL = 0x0C

ADC_RESULT = 0x00
ADC_CONTROL = 0x08

VOP_CONTROL = 0x00
VOP_STATUS = 0x24
VOP_COMMAND = 0x28
VOP_DATA = 0x2C
VOP_CONTROL_PIXELS = 0x2

MMC_CTRL = 0x00
MMC_CMDARG = 0x28
MMC_CMD = 0x2C
MMC_RESP0 = 0x30
MMC_RINTSTS = 0x44
MMC_STATUS = 0x48
MMC_FIFOTH = 0x4C
MMC_BYTCNT = 0x20
MMC_FIFO = 0x200

CMD_START = 1 << 31
CMD_UPDATE_CLOCK_ONLY = 1 << 21
CMD_DATA = 1 << 9
CMD_WRITE = 1 << 10

INT_COMMAND_DONE = 1 << 2
INT_DATA_OVER = 1 << 3
INT_RESPONSE_TIMEOUT = 1 << 8

STATUS_FIFO_EMPTY = 1 << 2
STATUS_FIFO_COUNT_SHIFT = 17

GINTSTS = 0x14
GRSTCTL = 0x10
GINT_USB_RESET = 1 << 12
USB_PHY_OFF = (0xB4, 0x00010001)
GRSTCTL_SOFT_RESET = 1
GRSTCTL_FLUSH_BITS = (1 << 4) | (1 << 5)
GRSTCTL_AHB_IDLE = 1 << 31
DCTL = 0x804
DCTL_SOFT_DISCONNECT = 1 << 1

SYSTICK_CTRL = 0x10
SYSTICK_RELOAD = 0x14
SYSTICK_CURRENT = 0x18
SCB_ICSR = 0xD04
SCB_VTOR = 0xD08
SCB_AIRCR = 0xD0C
ICSR_PENDSTSET = 1 << 26
ICSR_PENDSTCLR = 1 << 25
AIRCR_VECTKEY = 0x05FA0000
AIRCR_SYSRESETREQ = 0x4
SYSTICK_TICKINT = 0x2

PMU_BOOT_FLAG = 0x4C

SYSTICK_POLL_JUMP_CYCLES = 240


class RegisterFile:
    def __init__(self, masked=False):
        self.regs = {}
        self.masked = masked

    def read(self, offset, size):
        word = self.read_word(offset & ~3)
        shift = (offset & 3) * 8
        return (word >> shift) & ((1 << (size * 8)) - 1)

    def write(self, offset, size, value):
        base = offset & ~3
        if size == 4:
            self.write_word(base, value & 0xFFFFFFFF)
            return
        shift = (offset & 3) * 8
        mask = ((1 << (size * 8)) - 1) << shift
        merged = (self.regs.get(base, 0) & ~mask) | ((value << shift) & mask)
        self.write_word(base, merged)

    def read_word(self, offset):
        return self.regs.get(offset, 0)

    def write_word(self, offset, value):
        if self.masked:
            mask = value >> 16
            old = self.regs.get(offset, 0)
            value = (old & ~mask & 0xFFFF) | (value & mask & 0xFFFF)
        self.regs[offset] = value


class GenericPage(RegisterFile):
    def __init__(self, machine, base):
        super().__init__()
        self.machine = machine
        self.base = base

    def read_word(self, offset):
        self.machine.unknown_mmio.add(self.base + offset)
        return super().read_word(offset)

    def write_word(self, offset, value):
        self.machine.unknown_mmio.add(self.base + offset)
        super().write_word(offset, value)


class Gpio(RegisterFile):
    def __init__(self, machine, port):
        super().__init__()
        self.machine = machine
        self.port = port

    def read_word(self, offset):
        if offset == GPIO_INPUT_LEVEL:
            data = self.regs.get(GPIO_DATA, 0)
            direction = self.regs.get(GPIO_DIRECTION, 0)
            external = self.machine.external_pin_levels(self.port)
            return (data & direction) | (external & ~direction & 0xFFFFFFFF)
        return super().read_word(offset)

    def write_word(self, offset, value):
        before = self.regs.get(offset, 0)
        super().write_word(offset, value)
        if offset in (GPIO_DATA, GPIO_DIRECTION) and before != value:
            self.machine.gpio_log.append(
                (self.machine.cycles(), self.port, offset, value,
                 self.regs.get(GPIO_DATA, 0), self.regs.get(GPIO_DIRECTION, 0),
                 len(self.machine.panel.groups)))


class Cru(RegisterFile):
    def __init__(self, machine):
        super().__init__(masked=True)
        self.machine = machine

    def read_word(self, offset):
        value = super().read_word(offset)
        if offset == CRU_PLL_CON1:
            value |= PLL_LOCKED
        return value

    def write_word(self, offset, value):
        super().write_word(offset, value)
        if offset == CRU_CLOCK_SELECT and value >> 16 == CORE_DIVIDER_MASK:
            self.machine.coast_log.append((self.machine.milliseconds(), (value & 7) + 1))
            self.machine.clock_changed()
            return
        self.machine.register_log.append(("cru", offset, value))
        if offset in (CRU_MODE, CRU_PCLK_SELECT, CRU_CLOCK_SELECT, 0x00, CRU_PLL_CON1):
            self.machine.clock_changed()


class Grf(RegisterFile):
    def __init__(self, machine):
        super().__init__(masked=True)
        self.machine = machine

    def read_word(self, offset):
        if offset == GRF_VBUS:
            return GRF_VBUS_PRESENT if self.machine.cable_bit() else 0
        return super().read_word(offset)

    def write_word(self, offset, value):
        super().write_word(offset, value)
        self.machine.grf_log.append((self.machine.stamp(), offset, value))
        self.machine.register_log.append(("grf", offset, value))
        if (offset, value) == USB_PHY_OFF:
            self.machine.usb_powered_down()


class RtcBus(RegisterFile):
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
    ADDRESS = 0xA2
    REGISTERS = 16

    def __init__(self, machine):
        super().__init__()
        self.machine = machine
        self.present = True
        self.registers = bytearray([0x00, 0x00, 0x05, 0x00, 0x12, 0x03, 0x06, 0x01, 0x26,
                                    0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00])
        self.pointer = 0
        self.frames = []

    def pend(self, bits):
        self.regs[self.IPD] = self.regs.get(self.IPD, 0) | bits

    def sent_bytes(self, count):
        words = range((count + 3) // 4)
        return b"".join(self.regs.get(self.TXDATA + 4 * word, 0).to_bytes(4, "little")
                        for word in words)[:count]

    def transmit(self, count):
        frame = self.sent_bytes(count)
        self.frames.append(("write", frame))
        if not self.present or frame[:1] != bytes([self.ADDRESS]):
            self.pend(self.NAK)
            return
        if count > 1:
            self.pointer = frame[1] % self.REGISTERS
        for offset, byte in enumerate(frame[2:]):
            self.registers[(self.pointer + offset) % self.REGISTERS] = byte
        self.pend(self.TX_DONE)

    def receive(self, count):
        address = self.regs.get(self.MRXADDR, 0) & 0xFF
        self.frames.append(("read", address, count))
        if not self.present or address != self.ADDRESS + 1:
            self.pend(self.NAK)
            return
        data = bytes(self.registers[(self.pointer + index) % self.REGISTERS] for index in range(count))
        for word in range((count + 3) // 4):
            self.regs[self.RXDATA + 4 * word] = int.from_bytes(data[4 * word:4 * word + 4], "little")
        self.pend(self.RX_DONE)

    def write_word(self, offset, value):
        if offset == self.IPD:
            self.regs[self.IPD] = self.regs.get(self.IPD, 0) & ~value
            return
        super().write_word(offset, value)
        if offset == self.CON and value & self.START:
            self.pend(self.START_DONE)
        elif offset == self.CON and value & self.STOP:
            self.pend(self.STOP_DONE)
        elif offset == self.MTXCNT:
            self.transmit(value)
        elif offset == self.MRXCNT:
            self.receive(value)


class Pmu(RegisterFile):
    def __init__(self, machine):
        super().__init__(masked=True)
        self.machine = machine

    def write_word(self, offset, value):
        super().write_word(offset, value)
        if offset == PMU_BOOT_FLAG:
            self.machine.pmu_flag_writes.append((self.machine.stamp(), value))


class Pwm(RegisterFile):
    def __init__(self, machine):
        super().__init__()
        self.machine = machine

    def write_word(self, offset, value):
        super().write_word(offset, value)
        self.machine.pwm_log.append((self.machine.cycles(), offset, value, self.machine.pclk_hz(),
                                     len(self.machine.panel.groups)))


class SarAdc(RegisterFile):
    KEY_CHANNEL = 1
    BATTERY_CHANNEL = 0

    def __init__(self, machine):
        super().__init__()
        self.machine = machine

    def read_word(self, offset):
        if offset == ADC_RESULT:
            channel = self.regs.get(ADC_CONTROL, 0) & 7
            if channel == self.KEY_CHANNEL:
                return self.machine.adc_key_value
            return self.machine.adc_battery_value
        return super().read_word(offset)


class Watchdog(RegisterFile):
    def __init__(self, machine):
        super().__init__()
        self.machine = machine
        self.top = 0
        self.last_kick = 0

    def timeout_cycles(self):
        clocks = 1 << (16 + self.top)
        return clocks * self.machine.core_hz // self.machine.pclk_hz()

    def expired(self):
        if not self.machine.watchdog_running:
            return False
        return self.machine.cycles() - self.last_kick > self.timeout_cycles()

    def write_word(self, offset, value):
        super().write_word(offset, value)
        if offset == 0x0C and value == 0x76:
            self.machine.watchdog_kicks += 1
            self.last_kick = self.machine.cycles()
        elif offset == 0x04:
            self.top = value & 0xF
        elif offset == 0x00:
            self.machine.watchdog_running = bool(value & 1)


class Vop(RegisterFile):
    def __init__(self, machine):
        super().__init__()
        self.machine = machine
        self.pixel_mode = False

    def read_word(self, offset):
        if offset == VOP_STATUS:
            return 0
        return super().read_word(offset)

    def write(self, offset, size, value):
        if offset == VOP_DATA and self.pixel_mode:
            self.machine.panel.pixel(value & 0xFFFF)
            return
        super().write(offset, size, value)

    def write_word(self, offset, value):
        super().write_word(offset, value)
        if offset == VOP_CONTROL:
            self.pixel_mode = value == VOP_CONTROL_PIXELS
        elif offset == VOP_COMMAND:
            self.machine.panel.command(value & 0xFF)
        elif offset == VOP_DATA:
            self.machine.panel.parameter(value & 0xFF)


class Dmac(RegisterFile):
    COMP_TYPE = 0x3F8

    def read_word(self, offset):
        if offset == self.COMP_TYPE:
            return 0x44571110
        return super().read_word(offset)


class Systick(RegisterFile):
    def __init__(self, machine):
        super().__init__()
        self.machine = machine
        self.base_cycle = 0
        self.enabled = False
        self.tick_interrupt = False
        self.reload = 0
        self.delivered_wraps = 0
        self.vtor = 0

    def elapsed(self):
        return self.machine.cycles() - self.base_cycle

    def wraps(self):
        if not self.enabled or self.reload == 0:
            return 0
        return self.elapsed() // (self.reload + 1)

    def pending(self):
        return self.wraps() > self.delivered_wraps

    def current(self):
        if not self.enabled:
            return 0
        return self.reload - self.elapsed() % (self.reload + 1)

    def acknowledge(self):
        self.delivered_wraps = self.wraps()

    def read_word(self, offset):
        if offset == SYSTICK_CURRENT:
            self.machine.poll_jump(SYSTICK_POLL_JUMP_CYCLES)
            return self.current()
        if offset == SCB_ICSR:
            self.machine.poll_jump(SYSTICK_POLL_JUMP_CYCLES)
            return ICSR_PENDSTSET if self.pending() else 0
        if offset == SYSTICK_CTRL:
            return (1 if self.enabled else 0) | (2 if self.tick_interrupt else 0)
        if offset == SCB_VTOR:
            return self.vtor
        return super().read_word(offset)

    def restart(self):
        self.base_cycle = self.machine.cycles()
        self.delivered_wraps = 0

    def write_word(self, offset, value):
        if offset == SYSTICK_CTRL:
            was = self.enabled
            self.enabled = bool(value & 1)
            self.tick_interrupt = bool(value & SYSTICK_TICKINT)
            if self.enabled and not was:
                self.restart()
        elif offset == SYSTICK_RELOAD:
            self.reload = value & 0xFFFFFF
            self.restart()
        elif offset == SYSTICK_CURRENT:
            self.restart()
        elif offset == SCB_ICSR:
            if value & ICSR_PENDSTCLR:
                self.acknowledge()
        elif offset == SCB_VTOR:
            self.vtor = value
        elif offset == SCB_AIRCR:
            if (value & 0xFFFF0000) == AIRCR_VECTKEY and value & AIRCR_SYSRESETREQ:
                self.machine.system_reset_requested()
        else:
            super().write_word(offset, value)


class UsbCore(RegisterFile):
    GOTGCTL = 0x000
    GOTGCTL_B_DEVICE = 1 << 16
    GOTGCTL_B_SESSION_VALID = 1 << 19
    GSNPSID = 0x040
    GHWCFG2 = 0x048
    GHWCFG3 = 0x04C
    GHWCFG4 = 0x050

    def __init__(self, machine):
        super().__init__()
        self.machine = machine
        self.interrupts = 0

    def read_word(self, offset):
        if offset == self.GOTGCTL:
            valid = self.GOTGCTL_B_SESSION_VALID if self.machine.vbus else 0
            return self.GOTGCTL_B_DEVICE | valid
        if offset == self.GSNPSID:
            return 0x4F54294A
        if offset == self.GHWCFG2:
            return 0x228DDD50
        if offset == self.GHWCFG3:
            return 0x04000000
        if offset == self.GHWCFG4:
            return 0x02000000 | (1 << 25)
        if offset == GINTSTS:
            return self.interrupts
        if offset == GRSTCTL:
            return super().read_word(offset) | GRSTCTL_AHB_IDLE
        return super().read_word(offset)

    def write_word(self, offset, value):
        if offset == GINTSTS:
            cleared = self.interrupts & value
            self.interrupts &= ~value
            if cleared & GINT_USB_RESET:
                self.machine.usb_reset_acknowledged()
            return
        if offset == DCTL:
            was_disconnected = self.regs.get(DCTL, 0) & DCTL_SOFT_DISCONNECT
            super().write_word(offset, value)
            if was_disconnected and not value & DCTL_SOFT_DISCONNECT:
                self.machine.usb_connected()
            elif not was_disconnected and value & DCTL_SOFT_DISCONNECT:
                self.machine.usb_disconnected()
            return
        if offset == GRSTCTL:
            self.regs[offset] = value & ~(GRSTCTL_SOFT_RESET | GRSTCTL_FLUSH_BITS)
            return
        super().write_word(offset, value)

    def raise_reset(self):
        self.interrupts |= GINT_USB_RESET
