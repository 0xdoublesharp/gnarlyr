from .devices import GPIO_DATA, GPIO_DIRECTION, RegisterFile

UART_BASE = 0x400B0000
UART_DATA = 0x00
UART_LINE_CONTROL = 0x0C
UART_LINE_STATUS = 0x14
UART_STATUS = 0x7C
UART_RX_LEVEL = 0x84
UART_PARAMETERS = 0xF4
DIVISOR_LATCH = 0x80
LINE_DATA_READY = 0x01
STATUS_TX_NOT_FULL = 0x02
FIFO_PARAMETERS = 0x00020000
POWER_PORT = 2
POWER_PIN = 7
COMMAND_SIZE = 5
ADDRESS_COMMAND_SIZE = 10
READY_MS = 300
LINK_MS = 1500
REFUSE_MS = 1000
STREAM_ON = 0x1E
PLAY_NOTICE = 0x86
PAUSE_NOTICE = 0x87
SINK_PAUSE_MS = 200


class Headphones:
    def __init__(self, address, name):
        self.address = bytes(address)
        self.name = name.encode("utf-8")

    def status(self):
        payload = self.address + self.name
        return b"CQO%02d" % len(payload) + payload


class BluetoothModule(RegisterFile):
    def __init__(self, machine):
        super().__init__()
        self.machine = machine
        self.rx = bytearray()
        self.tx = bytearray()
        self.powered = False
        self.power_ups = 0
        self.power_downs = []
        self.generation = 0
        self.headphones = None
        self.in_range = True
        self.link_ms = LINK_MS
        self.connected = False
        self.pauses_new_streams = False
        self.paused_this_link = False
        self.commands = []
        self.streaming = False
        gpio = machine.gpio[POWER_PORT]
        original = gpio.write_word

        def write_word(offset, value):
            original(offset, value)
            self.follow_power()

        gpio.write_word = write_word

    def power_pin_high(self):
        regs = self.machine.gpio[POWER_PORT].regs
        return bool(regs.get(GPIO_DATA, 0) >> POWER_PIN & 1 and
                    regs.get(GPIO_DIRECTION, 0) >> POWER_PIN & 1)

    def follow_power(self):
        on = self.power_pin_high()
        if on == self.powered:
            return
        self.powered = on
        self.generation += 1
        self.connected = False
        self.streaming = False
        self.rx.clear()
        self.tx.clear()
        if on:
            self.power_ups += 1
            self.later(READY_MS, b"CV15")
        else:
            self.power_downs.append(self.machine.milliseconds())

    def later(self, delay_ms, answer, action=None):
        generation = self.generation

        def deliver():
            if generation != self.generation:
                return
            if action is not None and not action():
                return
            self.rx.extend(answer)

        self.machine.at_ms(self.machine.milliseconds() + delay_ms, deliver)

    def read_word(self, offset):
        if offset == UART_LINE_STATUS:
            return LINE_DATA_READY if self.rx else 0
        if offset == UART_DATA and not self.regs.get(UART_LINE_CONTROL, 0) & DIVISOR_LATCH:
            return self.rx.pop(0) if self.rx else 0
        if offset == UART_STATUS:
            return STATUS_TX_NOT_FULL
        if offset == UART_RX_LEVEL:
            return len(self.rx)
        if offset == UART_PARAMETERS:
            return FIFO_PARAMETERS
        return super().read_word(offset)

    def write_word(self, offset, value):
        if offset == UART_DATA and not self.regs.get(UART_LINE_CONTROL, 0) & DIVISOR_LATCH:
            self.receive(value & 0xFF)
            return
        super().write_word(offset, value)

    def receive(self, byte):
        if not self.powered:
            return
        self.tx.append(byte)
        if self.tx[0] != ord("C"):
            self.tx.pop(0)
            return
        size = ADDRESS_COMMAND_SIZE if self.tx[1:2] in (b"L", b"H") else COMMAND_SIZE
        if len(self.tx) >= size:
            command = bytes(self.tx[:size])
            del self.tx[:size]
            self.commands.append((self.machine.milliseconds(), command))
            self.answer(command)

    def answer(self, command):
        letter = command[1]
        if letter == ord("Q"):
            self.rx.extend(self.headphones.status() if self.connected else b"CQI")
        elif letter in (ord("L"), ord("H")):
            self.link(command[1:2], command[4:10])
        elif letter == ord("T"):
            self.connected = False
            self.streaming = False
            self.rx.extend(b"CTOK")
        elif letter == ord("S"):
            self.later(REFUSE_MS, b"CSEN")
        elif letter == ord("Y"):
            self.follow_stream(command[3] == STREAM_ON)

    def link(self, letter, address):
        reachable = self.headphones is not None and self.in_range
        if not reachable or address != self.headphones.address:
            self.later(REFUSE_MS, b"C" + letter + b"ER")
            return

        def arrive():
            if not self.in_range:
                return False
            self.connected = True
            self.paused_this_link = False
            return True

        self.later(self.link_ms, b"C" + letter + b"OK", arrive)

    def follow_stream(self, on):
        self.streaming = on and self.connected
        if self.streaming and self.pauses_new_streams and not self.paused_this_link:
            self.paused_this_link = True

            def suspend():
                self.streaming = False
                return True

            self.later(SINK_PAUSE_MS, b"CP", suspend)

    def walk_away(self):
        self.in_range = False
        self.connected = False
        self.streaming = False

    def come_back(self, reconnect=False):
        self.in_range = True
        if reconnect and self.headphones is not None:
            self.connected = True
            self.paused_this_link = False

    def sent(self, since_ms=0):
        return [command for stamp, command in self.commands if stamp >= since_ms]

    def stream_frames(self, since_ms=0):
        frames = []
        for command in self.sent(since_ms):
            if command[1] == ord("Y"):
                frames.append("on" if command[3] == STREAM_ON else "off")
            elif command[1] == PLAY_NOTICE:
                frames.append("play notice")
            elif command[1] == PAUSE_NOTICE:
                frames.append("pause notice")
        return frames
