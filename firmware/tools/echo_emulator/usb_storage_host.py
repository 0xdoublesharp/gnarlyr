import struct
from collections import deque

GINTSTS = 0x014
GRXSTSR = 0x01C
GRXSTSP = 0x020
DSTS = 0x808
DAINT = 0x818
GINT_RX_LEVEL = 1 << 4
GINT_ENUMERATION_DONE = 1 << 13
GINT_IN_ENDPOINT = 1 << 18
GINT_OUT_ENDPOINT = 1 << 19
EP_ENABLE = 1 << 31
EP_DISABLE = 1 << 30
EP_INT_TRANSFER_DONE = 1 << 0
EP_INT_SETUP = 1 << 3
PACKET_OUT_DATA = 2
PACKET_OUT_DONE = 3
PACKET_SETUP_DONE = 4
PACKET_SETUP_DATA = 6
SIZE_MASK = 0x7FFFF
COUNT_SHIFT = 19
COUNT_MASK = 0x3FF
BULK = 1
SECTOR = 512
CBW_SIGNATURE = b"USBC"
CSW_LENGTH = 13
CBW_LENGTH = 31
SENSE_LENGTH = 18
TRANSFER_BYTE_LIMIT = 2047
TRANSFER_PACKET_LIMIT = 15
DSTS_FULL_SPEED = 1 << 1
SET_CONFIGURATION = bytes([0x00, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00])


def in_control(endpoint):
    return 0x900 + 0x20 * endpoint


def out_control(endpoint):
    return 0xB00 + 0x20 * endpoint


class Endpoint:
    def __init__(self):
        self.control = 0
        self.size = 0
        self.events = 0
        self.packets = 0
        self.bytes = 0
        self.words = []


class StorageHost:
    def __init__(self, machine, packet=SECTOR, megabytes_per_second=40):
        self.machine = machine
        self.packet = packet
        self.faults = []
        self.in_sizes = []
        self.events = []
        self.packet_us = packet / megabytes_per_second
        self.out_queue = deque()
        self.rx = deque()
        self.reading = deque()
        self.in_bytes = bytearray()
        self.inputs = {endpoint: Endpoint() for endpoint in range(16)}
        self.outputs = {endpoint: Endpoint() for endpoint in range(16)}
        self.next_due_us = 0
        self.enumerated = False
        self.armed_packets = []

    def claims(self, offset):
        return offset in (GRXSTSR, GRXSTSP, DAINT, DSTS) or 0x900 <= offset < 0xC00 or \
            0x1000 <= offset < 0x5000

    def interrupts(self):
        self.deliver()
        bits = GINT_ENUMERATION_DONE if self.enumerated == "pending" else 0
        if self.rx:
            bits |= GINT_RX_LEVEL
        if any(endpoint.events for endpoint in self.outputs.values()):
            bits |= GINT_OUT_ENDPOINT
        if any(endpoint.events for endpoint in self.inputs.values()):
            bits |= GINT_IN_ENDPOINT
        return bits

    def clear(self, value):
        if value & GINT_ENUMERATION_DONE and self.enumerated == "pending":
            self.enumerated = True

    def enumerate(self):
        self.enumerated = "pending"
        self.rx.append((0, PACKET_SETUP_DATA, SET_CONFIGURATION))
        self.rx.append((0, PACKET_SETUP_DONE, b""))

    def send(self, data):
        for start in range(0, len(data), self.packet):
            self.out_queue.append(bytes(data[start:start + self.packet]))
        if len(data) % self.packet == 0 and not data:
            self.out_queue.append(b"")

    def deliver(self):
        endpoint = self.outputs[BULK]
        now = self.machine.microseconds()
        while self.out_queue and endpoint.control & EP_ENABLE and endpoint.packets > 0 and \
                not any(entry[0] == BULK for entry in self.rx) and now >= self.next_due_us:
            data = self.out_queue.popleft()
            self.next_due_us = max(now, self.next_due_us) + self.packet_us
            self.rx.append((BULK, PACKET_OUT_DATA, data))
            endpoint.packets -= 1
            if endpoint.packets == 0 or len(data) < self.packet:
                self.rx.append((BULK, PACKET_OUT_DONE, b""))

    def status_word(self, entry):
        endpoint, kind, data = entry
        return endpoint | len(data) << 4 | kind << 17

    def pop(self):
        if not self.rx:
            return 0
        entry = self.rx.popleft()
        endpoint, kind, data = entry
        padded = data + bytes(-len(data) % 4)
        self.reading = deque(struct.unpack("<%dI" % (len(padded) // 4), padded))
        if kind == PACKET_OUT_DATA and endpoint == BULK:
            self.events.append((self.machine.microseconds(), "out", len(data)))
        if kind == PACKET_OUT_DONE:
            self.outputs[endpoint].events |= EP_INT_TRANSFER_DONE
            self.outputs[endpoint].control &= ~EP_ENABLE
        if kind == PACKET_SETUP_DONE:
            self.outputs[0].events |= EP_INT_SETUP
        return self.status_word(entry)

    def read_word(self, offset):
        if offset == GRXSTSR:
            self.deliver()
            return self.status_word(self.rx[0]) if self.rx else 0
        if offset == GRXSTSP:
            return self.pop()
        if offset == DSTS:
            return 0 if self.packet == SECTOR else DSTS_FULL_SPEED
        if offset == DAINT:
            outs = sum(1 << (16 + n) for n, e in self.outputs.items() if e.events)
            ins = sum(1 << n for n, e in self.inputs.items() if e.events)
            return outs | ins
        if 0x1000 <= offset < 0x5000:
            return self.reading.popleft() if self.reading else 0
        return self.endpoint_register(offset)

    def endpoint_register(self, offset):
        table, base = (self.inputs, 0x900) if offset < 0xB00 else (self.outputs, 0xB00)
        endpoint = table[(offset - base) // 0x20]
        field = (offset - base) % 0x20
        if field == 0x00:
            return endpoint.control
        if field == 0x08:
            return endpoint.events
        if field == 0x10:
            return endpoint.size
        return 0

    def write_word(self, offset, value):
        if offset in (GRXSTSR, GRXSTSP, DAINT, DSTS):
            return
        if 0x1000 <= offset < 0x5000:
            self.fifo_write((offset - 0x1000) // 0x1000, value)
            return
        table, base = (self.inputs, 0x900) if offset < 0xB00 else (self.outputs, 0xB00)
        number = (offset - base) // 0x20
        endpoint = table[number]
        field = (offset - base) % 0x20
        if field == 0x08:
            endpoint.events &= ~value
        elif field == 0x10:
            endpoint.size = value
        elif field == 0x00:
            self.control_write(table is self.inputs, number, endpoint, value)

    def control_write(self, is_in, number, endpoint, value):
        if value & EP_DISABLE:
            endpoint.control = value & ~(EP_ENABLE | EP_DISABLE)
            return
        starting = value & EP_ENABLE and not endpoint.control & EP_ENABLE
        endpoint.control = value
        if not starting:
            return
        endpoint.packets = (endpoint.size >> COUNT_SHIFT) & COUNT_MASK
        endpoint.bytes = endpoint.size & SIZE_MASK
        endpoint.words = []
        if number == BULK:
            self.check_transfer(is_in, endpoint)
        if not is_in and number == BULK:
            self.armed_packets.append(endpoint.packets)
        if is_in and endpoint.bytes == 0:
            self.finish_in(number, endpoint)

    def check_transfer(self, is_in, endpoint):
        whole = -(-endpoint.bytes // self.packet) if endpoint.bytes else 1
        packets_match = endpoint.packets == whole if is_in else \
            endpoint.bytes == endpoint.packets * self.packet
        if endpoint.packets > TRANSFER_PACKET_LIMIT or endpoint.bytes > TRANSFER_BYTE_LIMIT or \
                not packets_match:
            self.faults.append(("in" if is_in else "out", endpoint.packets, endpoint.bytes))
        if is_in:
            self.in_sizes.append(endpoint.bytes)

    def fifo_write(self, number, value):
        endpoint = self.inputs[number]
        endpoint.words.append(value)
        if len(endpoint.words) * 4 >= endpoint.bytes:
            self.finish_in(number, endpoint)

    def finish_in(self, number, endpoint):
        data = struct.pack("<%dI" % len(endpoint.words), *endpoint.words)[:endpoint.bytes]
        if number == BULK:
            self.in_bytes += data
            self.events.append((self.machine.microseconds(), "in", len(data)))
        endpoint.words = []
        endpoint.control &= ~EP_ENABLE
        endpoint.events |= EP_INT_TRANSFER_DONE

    def configured(self):
        return bool(self.armed_packets)

    def take_in(self, length):
        if len(self.in_bytes) < length:
            return None
        data = bytes(self.in_bytes[:length])
        del self.in_bytes[:length]
        return data


def command_block(tag, length, inbound, lun, block):
    return struct.pack("<4sIIBBB", CBW_SIGNATURE, tag, length, 0x80 if inbound else 0, lun,
                       len(block)) + bytes(block) + bytes(16 - len(block))


def rw10(opcode, lba, count):
    return struct.pack(">BBIBHB", opcode, 0, lba, 0, count, 0)


class ScsiHost:
    def __init__(self, machine, host):
        self.machine = machine
        self.host = host
        self.tag = 0

    def wait(self, ready, limit_ms):
        until = self.machine.milliseconds() + limit_ms
        result = self.machine.run(until=ready, until_ms=until)
        if result != "condition met":
            raise AssertionError("the device stopped answering: %s" % result)

    def status(self):
        box = {}

        def arrived():
            box["csw"] = self.host.take_in(CSW_LENGTH)
            return box["csw"] is not None

        self.wait(arrived, 5000)
        signature, tag, residue, status = struct.unpack("<4sIIB", box["csw"])
        if signature != b"USBS" or tag != self.tag:
            raise AssertionError("bad status %r" % box["csw"])
        return status, residue

    def write(self, lun, lba, data):
        self.tag += 1
        count = len(data) // SECTOR
        self.host.send(command_block(self.tag, len(data), False, lun, rw10(0x2A, lba, count)))
        self.host.send(data)
        return self.status()

    def test_unit_ready(self, lun):
        self.tag += 1
        self.host.send(command_block(self.tag, 0, False, lun, bytes(6)))
        return self.status()

    def until_ready(self, lun, tries=4):
        for _ in range(tries):
            if self.test_unit_ready(lun)[0] == 0:
                return True
            self.request_sense(lun)
        return False

    def read(self, lun, lba, count):
        return self.receive(lun, rw10(0x28, lba, count), count * SECTOR)

    def request_sense(self, lun):
        return self.receive(lun, bytes([0x03, 0, 0, 0, SENSE_LENGTH, 0]), SENSE_LENGTH)

    def receive(self, lun, block, length):
        self.tag += 1
        self.host.send(command_block(self.tag, length, True, lun, block))
        box = {}

        def arrived():
            box["data"] = self.host.take_in(length)
            return box["data"] is not None

        self.wait(arrived, 5000)
        return box["data"], self.status()


def command_phases(events):
    phases = []
    current = None
    for when, kind, length in events:
        if kind == "out" and length == CBW_LENGTH:
            if current is not None and "status" in current:
                phases.append(current)
            current = {"command": when, "data": [], "status": None}
        elif current is None:
            continue
        elif kind == "in" and length == CSW_LENGTH:
            current["status"] = when
        else:
            current["data"].append(when)
    if current is not None and current.get("status"):
        phases.append(current)
    return phases


def phase_summary(events):
    phases = [phase for phase in command_phases(events) if phase["data"] and phase["status"]]
    if not phases:
        return {}
    count = len(phases)
    first = sum(phase["data"][0] - phase["command"] for phase in phases) / count
    data = sum(phase["data"][-1] - phase["data"][0] for phase in phases) / count
    status = sum(phase["status"] - phase["data"][-1] for phase in phases) / count
    gaps = [later["command"] - earlier["status"] for earlier, later in zip(phases, phases[1:])]
    gap = sum(gaps) / len(gaps) if gaps else 0
    steps = [later - earlier for phase in phases
             for earlier, later in zip(phase["data"], phase["data"][1:])]
    return {"commands": count, "to_data_us": first, "data_us": data, "to_status_us": status,
            "next_command_us": gap, "longest_gap_us": max(gaps) if gaps else 0,
            "longest_packet_gap_us": max(steps) if steps else 0}
