import struct
from collections import deque


class UsbAudioHost:
    def __init__(self, machine, rate=48000, width=4, high=True):
        self.machine = machine
        self.core = machine.usb
        self.original_read = self.core.read_word
        self.original_write = self.core.write_word
        self.core.read_word = self.read_word
        self.core.write_word = self.write_word
        self.high = high
        self.rate = rate
        self.width = width
        self.rx = deque()
        self.words = deque()
        self.requests = deque()
        self.pending = None
        self.control = bytearray()
        self.responses = []
        self.tx = {}
        self.hid = []
        self.feedback = []
        self.feedback_value = (rate << 16) // 8000
        self.accumulator = 0
        self.sequence = 0
        self.expected = []
        self.missed = 0
        self.enabled = False
        self.next_packet = None
        self.next_request = 0
        self.registered = False
        self.pause_until = 0
        self.bus_started = False
        self.in_due = {}
        self.vary_levels = False

    def setup(self, kind, request, value=0, index=0, length=0, payload=b""):
        self.requests.append((struct.pack("<BBHHH", kind, request, value, index, length), payload))

    def enumerate(self):
        self.setup(0x80, 6, 0x0100, length=18)
        self.setup(0x80, 6, 0x0200, length=1024)
        self.setup(0x80, 6, 0x0600, length=10)
        self.setup(0x80, 6, 0x0700, length=1024)
        self.setup(0, 9, 1)
        if self.high:
            self.setup(0xA1, 2, 0x0100, 0x0400, 74)
        self.format(self.rate, self.width)
        self.setup(0x81, 6, 0x2200, 2, 255)

    def format(self, rate, width):
        self.setup(0x01, 11, 0, 1)
        self.rate, self.width = rate, width
        if self.high:
            self.setup(0x21, 1, 0x0100, 0x0400, 4, struct.pack("<I", rate))
        else:
            self.setup(0x22, 1, 0x0100, 1, 3, struct.pack("<I", rate)[:3])
        self.setup(0x01, 11, width - 1, 1)

    def push(self, endpoint, payload, packet_type=2):
        self.rx.append((endpoint, payload, packet_type))
        control = 0xB00 + endpoint * 0x20
        self.core.regs[control] = self.core.regs.get(control, 0) & ~(1 << 31)
        event = 8 if packet_type == 6 else 1
        self.core.regs[control + 8] = self.core.regs.get(control + 8, 0) | event

    def complete_request(self):
        if self.pending is None:
            return
        if self.pending[:2] == (0x01, 11):
            self.enabled = self.pending[2] != 0
            self.next_packet = None
        self.responses.append((self.pending, bytes(self.control)))
        self.pending = None
        self.control.clear()
        self.next_request = self.machine.microseconds() + 250

    def transactions(self):
        now = self.machine.microseconds()
        for endpoint, due in list(self.in_due.items()):
            if due <= now:
                self.core.regs[0x900 + endpoint * 0x20] &= ~(1 << 31)
                self.core.regs[0x908 + endpoint * 0x20] = 1
                del self.in_due[endpoint]
        if self.registered and now >= self.next_request and self.pending is None and not self.rx:
            if self.requests and self.core.regs.get(0xB00, 0) & (1 << 31):
                raw, payload = self.requests.popleft()
                self.pending = struct.unpack("<BBHHH", raw)
                self.control.clear()
                self.push(0, raw, 6)
                if payload:
                    self.rx.append((0, payload, 2))
            elif not self.requests and self.enabled and self.next_packet is None:
                self.next_packet = now + (125 if self.high else 1000)
        period = 125 if self.high else 1000
        if self.next_packet is not None and self.enabled:
            while self.next_packet <= now:
                if self.next_packet >= self.pause_until:
                    self.stream_packet()
                self.next_packet += period

    def stream_packet(self):
        self.accumulator += self.feedback_value if self.high else (self.rate << 16) // 1000
        count, self.accumulator = divmod(self.accumulator, 1 << 16)
        if not self.core.regs.get(0xB20, 0) & (1 << 31):
            self.missed += 1
            return
        data = bytearray()
        expected = []
        for _ in range(count):
            left = (0x12345678 + self.sequence) & 0xFFFFFFFF
            right = (0xFEDCBA98 - self.sequence) & 0xFFFFFFFF
            if self.vary_levels:
                shift = (self.sequence // (self.rate // 8)) % 5
                left >>= shift
                right = (0x654321FE - self.sequence) >> (4 - shift)
            self.sequence += 1
            for word in (left, right):
                sample = word.to_bytes(4, "little")[4 - self.width:]
                data.extend(sample)
                expected.append(int.from_bytes(sample, "little") << (8 * (4 - self.width)))
        maximum = self.core.regs[0xB20] & 0x7FF
        if len(data) > maximum:
            raise AssertionError(("packet exceeds MPS", len(data), maximum))
        self.push(1, data)
        self.expected.extend(expected)

    def read_word(self, offset):
        if offset == 0x014:
            self.transactions()
            events = self.original_read(offset)
            if self.rx:
                events |= 1 << 4
            if any(self.core.regs.get(0xB08 + ep * 0x20, 0) for ep in range(2)):
                events |= 1 << 19
            if any(self.core.regs.get(0x908 + ep * 0x20, 0) for ep in range(3)):
                events |= 1 << 18
            return events
        if offset == 0x808:
            return ((self.machine.microseconds() // (125 if self.high else 1000)) & 0x3FFF) << 8 | (0 if self.high else 6)
        if offset == 0x818:
            out = sum((1 << (16 + ep)) for ep in range(2) if self.core.regs.get(0xB08 + ep * 0x20, 0))
            incoming = sum((1 << ep) for ep in range(3) if self.core.regs.get(0x908 + ep * 0x20, 0))
            return incoming | out
        if offset == 0x020:
            endpoint, payload, packet_type = self.rx.popleft()
            self.words.extend(struct.unpack("<%dI" % ((len(payload) + 3) // 4), payload + bytes(-len(payload) % 4)))
            return endpoint | len(payload) << 4 | packet_type << 17
        if offset == 0x1000:
            return self.words.popleft()
        if offset in (0x918, 0x938, 0x958):
            return 256
        return self.original_read(offset)

    def write_word(self, offset, value):
        if offset == 0x014 and value & (1 << 12) and self.bus_started:
            self.registered = True
            self.core.interrupts |= 1 << 13
        if offset == 0x804:
            self.bus_started = not value & 2
        if offset in (0x908, 0x928, 0x948, 0xB08, 0xB28):
            self.core.regs[offset] = self.core.regs.get(offset, 0) & ~value
            return
        if offset in (0x900, 0x920, 0x940, 0xB00, 0xB20):
            if value & (1 << 30):
                value &= ~((1 << 31) | (1 << 30))
            if value & (1 << 29):
                value |= 1 << 16
            elif value & (1 << 28):
                value &= ~(1 << 16)
            if offset == 0x900 and value & (1 << 31) and self.core.regs.get(0x910, 0) & 0x7FFFF == 0:
                value &= ~(1 << 31)
                self.complete_request()
            self.core.regs[offset] = value & ~((1 << 28) | (1 << 29))
            return
        if offset in (0x910, 0x930, 0x950):
            self.tx[(offset - 0x910) // 0x20] = bytearray()
            self.core.regs[offset] = value
            return
        if offset in (0x1000, 0x2000, 0x3000):
            endpoint = offset // 0x1000 - 1
            buffer = self.tx.setdefault(endpoint, bytearray())
            buffer.extend(value.to_bytes(4, "little"))
            length = self.core.regs.get(0x910 + endpoint * 0x20, 0) & 0x7FFFF
            if len(buffer) >= length:
                payload = bytes(buffer[:length])
                self.core.regs[0x900 + endpoint * 0x20] &= ~(1 << 31)
                self.core.regs[0x908 + endpoint * 0x20] = 1
                if endpoint == 0:
                    self.control.extend(payload)
                    if len(self.control) >= self.pending[4] or length < 64:
                        if self.pending[:2] == (0x01, 11):
                            self.enabled = self.pending[2] != 0
                            self.next_packet = None
                        self.complete_request()
                elif endpoint == 1:
                    self.feedback_value = int.from_bytes(payload, "little")
                    self.feedback.append(self.feedback_value)
                else:
                    self.hid.append(int.from_bytes(payload, "little"))
                if endpoint > 0:
                    self.core.regs[0x900 + endpoint * 0x20] |= 1 << 31
                    self.core.regs[0x908 + endpoint * 0x20] = 0
                    self.in_due[endpoint] = self.machine.microseconds() + (125 if endpoint == 1 else 8000)
            return
        self.original_write(offset, value)
