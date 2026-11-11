"""Function-level timing in the emulator: entry and exit hooks, phases, storage commands, samples.

Emulated time is instructions (one cycle each, counted in 4,000-instruction slices) plus the
cycles the eMMC model makes the firmware wait. A hooked function's time is read at the slice
it starts and ends in, so one call is exact to a slice; sums over many calls are unbiased.
"""
import bisect
import struct
from pathlib import Path

from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_SP

import make_image

from .machine import SLICE_INSTRUCTIONS, Overlay

SCAN_PASS = "scan"
COVER_PASS = "covers"
OTHER_PASS = "other"
PASS_FUNCTIONS = {"library_scan": SCAN_PASS, "cover_build": COVER_PASS}
PHASES = {
    "track_tags_read": "tags",
    "scan_mark_cue_targets": "cue",
    "scan_add_cue": "cue",
    "cue_sheet_usable": "cue",
    "scan_add_chapters": "chapters",
    "mp4_chapters_open": "chapters",
    "track_fields_from_tags": "builder add",
    "library_builder_add_track": "builder add",
    "library_builder_begin_playlist": "playlists",
    "library_builder_playlist_entry": "playlists",
    "library_builder_prepare": "builder prepare",
    "library_builder_finish": "builder finish",
    "builder_model": "builder model",
    "builder_guess_album_artists": "builder album artists",
    "builder_index_paths": "builder paths",
    "sort_records": "builder sort",
    "builder_rank": "builder rank",
    "builder_encode": "builder encode",
    "cover_image_decode": "decode",
}
INPUT_OUTPUT = {
    "open_dir": "list",
    "next_entry": "list",
    "close_dir": "list",
    "open_file": "open",
    "close_file": "open",
    "read_handle": "read",
    "write_library": "write",
    "write_scratch": "write",
    "write_covers": "write",
    "timed_write": "write",
    "hal_system_keep_alive": "keep alive",
}
DEFAULT_PHASE = {SCAN_PASS: "walk", COVER_PASS: "find art", OTHER_PASS: "setup"}
MAIN_FUNCTIONS = ("hal_file_read", "hal_file_open", "hal_file_write_at", "hal_dir_open",
                  "hal_dir_next", "hal_system_keep_alive")
COUNTERS = ("instructions", "waits", "us", "read_commands", "read_sectors", "write_commands",
            "write_sectors")


def symbol_table(elf_path):
    elf = Path(elf_path).read_bytes()
    header = make_image.ELF_HEADER.unpack_from(elf)
    table, count = header[6], header[12]
    sections = [make_image.ELF_SECTION.unpack_from(elf, table + index * make_image.ELF_SECTION.size)
                for index in range(count)]
    symbols = []
    for section in sections:
        if section[1] != 2:
            continue
        names = sections[section[6]]
        for position in range(section[5] // 16):
            name, value, size, info, other, shndx = struct.unpack_from(
                "<IIIBBH", elf, section[4] + position * 16)
            start = names[4] + name
            label = elf[start:elf.index(b"\x00", start)].decode("ascii")
            if label:
                symbols.append((label, value & ~1, size, info & 0xF))
    return symbols


def function_names(elf_path, first, last):
    return {address: name for name, address, size, kind in symbol_table(elf_path)
            if kind == 2 and first <= address < last}


def zero_counts():
    return dict.fromkeys(COUNTERS, 0)


class Mark:
    def __init__(self, machine, card):
        self.instructions = machine.base_cycles
        self.waits = machine.jump_cycles
        self.us = machine.microseconds()
        self.reads = len(card.reads)
        self.writes = len(card.writes)


class Frame:
    def __init__(self, name, back, sp, mark):
        self.name = name
        self.back = back
        self.sp = sp
        self.mark = mark


def difference(machine, card, since):
    reads = card.reads[since.reads:]
    writes = card.writes[since.writes:]
    return {
        "instructions": machine.base_cycles - since.instructions,
        "waits": machine.jump_cycles - since.waits,
        "us": machine.microseconds() - since.us,
        "read_commands": len(reads),
        "read_sectors": sum(count for _, _, count in reads),
        "write_commands": len(writes),
        "write_sectors": sum(count for _, _, count in writes),
    }


def add(total, counts):
    for key in COUNTERS:
        total[key] += counts[key]


class Profiler:
    """Watches named functions in the main program and the rescan overlay while it is loaded."""

    def __init__(self, session, rescan_overlay_elf, sample=True):
        self.session = session
        self.machine = session.machine
        self.card = session.card
        self.overlay = Overlay(session.image, rescan_overlay_elf, make_image.OVERLAY_OFFSET)
        self.overlay_elf = rescan_overlay_elf
        self.active = False
        self.enabled = False
        self.stack = []
        self.returns = set()
        self.buckets = {}
        self.functions = {}
        self.missing = []
        self.last = None
        self.samples = {}
        self.sample_names = self.sample_table()
        self.sampling = sample
        self.watch_overlay()
        for name in MAIN_FUNCTIONS:
            self.watch(name, self.machine.symbols.get(name), overlay=False)
        if sample:
            self.wrap_slices()

    def watch_overlay(self):
        names = set(PHASES) | set(INPUT_OUTPUT) | set(PASS_FUNCTIONS) | {"rescan_run"}
        for name in sorted(names):
            if name in self.overlay.symbols:
                self.watch(name, self.overlay.symbols[name], overlay=True)
            elif name not in MAIN_FUNCTIONS:
                self.missing.append(name)
        entry = self.overlay.symbols.get("rescan_run")
        if entry is not None:
            self.machine.add_address_hook(entry, self.overlay_entered)
        self.machine.hook_symbol("hal_system_return_scratch", self.overlay_left)

    def overlay_entered(self):
        self.active = self.overlay.loaded(self.machine)

    def overlay_left(self):
        self.active = False

    def watch(self, name, address, overlay):
        if address is None:
            self.missing.append(name)
            return

        def entered():
            if not self.enabled or (overlay and not self.active):
                return
            self.enter(name)

        self.machine.add_address_hook(address, entered)

    def start(self):
        self.enabled = True
        self.last = Mark(self.machine, self.card)

    def stop(self):
        self.charge()
        self.enabled = False

    def current_key(self):
        stage = OTHER_PASS
        phase = None
        kind = "cpu"
        for frame in self.stack:
            stage = PASS_FUNCTIONS.get(frame.name, stage)
            if frame.name in PHASES:
                phase = PHASES[frame.name]
                kind = "cpu"
            if frame.name in INPUT_OUTPUT:
                kind = INPUT_OUTPUT[frame.name]
        if phase is None:
            phase = DEFAULT_PHASE[stage]
        if stage == COVER_PASS and phase == "tags":
            phase = "find art: tags"
        return stage, phase, kind

    def charge(self):
        if self.last is None:
            return
        counts = difference(self.machine, self.card, self.last)
        key = self.current_key()
        add(self.buckets.setdefault(key, zero_counts()), counts)
        self.last = Mark(self.machine, self.card)

    def enter(self, name):
        machine = self.machine
        self.charge()
        back = machine.reg(UC_ARM_REG_LR) & ~1
        sp = machine.reg(UC_ARM_REG_SP)
        self.stack.append(Frame(name, back, sp, Mark(machine, self.card)))
        if back not in self.returns:
            self.returns.add(back)
            machine.add_address_hook(back, lambda: self.returned(back))

    def returned(self, back):
        if not self.stack:
            return
        sp = self.machine.reg(UC_ARM_REG_SP)
        if self.stack[-1].back != back or self.stack[-1].sp != sp:
            return
        self.charge()
        while self.stack and self.stack[-1].back == back and self.stack[-1].sp == sp:
            frame = self.stack.pop()
            stats = self.functions.setdefault(frame.name, dict(zero_counts(), calls=0))
            stats["calls"] += 1
            add(stats, difference(self.machine, self.card, frame.mark))

    def sample_table(self):
        overlay_end = self.overlay.base + make_image.OVERLAY_CAPACITY
        ranges = [(self.overlay_elf, self.overlay.base, overlay_end),
                  (self.machine.elf_path, make_image.CODE_BASE, self.overlay.base)]
        tables = []
        for elf_path, base, end in ranges:
            ordered = sorted(function_names(elf_path, base, end).items())
            tables.append((base, end, [item[0] for item in ordered], [item[1] for item in ordered]))
        return tables

    def wrap_slices(self):
        machine = self.machine
        original = machine.step_slice

        def step():
            original()
            if self.enabled:
                self.sample(machine.current_pc())

        machine.step_slice = step

    def sample(self, pc):
        for base, end, addresses, names in self.sample_names:
            if base <= pc < end:
                if base == self.overlay.base and not self.active:
                    continue
                index = bisect.bisect_right(addresses, pc) - 1
                name = names[index] if index >= 0 else "?"
                self.samples[name] = self.samples.get(name, 0) + 1
                return
        self.samples["(other)"] = self.samples.get("(other)", 0) + 1

    def report(self):
        def timed(counts):
            return dict(counts, ms=round(counts["us"] / 1000, 1))

        phases = [dict(timed(counts), stage=stage, phase=phase, kind=kind)
                  for (stage, phase, kind), counts in sorted(self.buckets.items())]
        functions = {name: timed(stats) for name, stats in sorted(self.functions.items())}
        total = sum(self.samples.values()) or 1
        top = sorted(self.samples.items(), key=lambda item: -item[1])[:25]
        return {
            "phases": phases,
            "functions": functions,
            "top_functions": [{"name": name, "samples": count, "share": round(count / total, 4)}
                              for name, count in top],
            "sample_instructions": SLICE_INSTRUCTIONS,
            "missing_hooks": sorted(set(self.missing)),
        }

