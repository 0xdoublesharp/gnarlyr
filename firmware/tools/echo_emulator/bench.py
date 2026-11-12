"""Scan and album-art timing runs on a device image, shared by `tools/scan_bench.py` and a scenario.

Every run starts from the generated volume or from the disk the previous run left, as the player
would after a restart. Times are emulated: instructions at one cycle each at the clock the
firmware set, plus the waits of the eMMC model.
"""
import time
from pathlib import Path

from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_SP)

import image_artifacts
import make_image
from identity import IDENTITY

from . import emmc
from .audio import AudioPath
from .scan_profile import Profiler
from .session import Session

KEYS = {"play": 250, "back": 500, "previous": 35, "next": 100}
KEY_HOLD_MS = 150
KEY_SETTLE_MS = 450
LONG_PRESS_MS = 1300
SETTLE_MS = 1500
SCAN_LIMIT_MS = 4 * 3600 * 1000
STEP_LIMIT_MS = 20000
PROGRESS_SECONDS = 30
MUSIC_ROWS_BEFORE_SONGS = 7
COVER_FLOW_ROW = 1
COVER_FLOW_STEPS = 5
TAP_COUNT = 12
TAP_GAP_MS = 160
HOLD_MS = 3000
HOME_CYCLE_WAIT_MS = 5600
RESCAN_ROW_GUESSES = (20, 21, 19, 22)
HOME_ROWS = 6
DATA = "/" + IDENTITY.data_directory
LIBRARY_PATH = DATA + "/library.db"
COVERS_PATH = DATA + "/covers.bin"
TIMES_PATH = DATA + "/rescan_times.bin"
TIME_KINDS = ("list", "open", "read", "write", "total")


class Artifacts:
    def __init__(self, build):
        self.build = Path(build)
        self.image_path = self.build / IDENTITY.update_file
        self.elf = self.build / "platform" / "device" / "echo_device.elf"
        self.usb_overlay_elf = self.elf.with_name("echo_usb_overlay.elf")
        self.rescan_overlay_elf = self.elf.with_name("echo_rescan_overlay.elf")
        missing = [str(path) for path in (self.image_path, self.elf, self.usb_overlay_elf,
                                          self.rescan_overlay_elf) if not path.is_file()]
        if missing:
            raise FileNotFoundError("missing build artifacts: " + ", ".join(missing))
        self.image = self.image_path.read_bytes()
        self.module_elfs = image_artifacts.module_elfs(self.build)

    def tag(self):
        return self.image[0x30:0x38].rstrip(b"\0").decode("ascii", "replace")

    def session(self, volume):
        return Session(self.image, self.elf, volume=volume, usb_overlay_elf=self.usb_overlay_elf,
                       module_elfs=self.module_elfs)


class Calls:
    """Every call of one function: start and end, arguments, result and the storage it used."""

    def __init__(self, session, name, address=None):
        self.session = session
        self.machine = session.machine
        self.name = name
        self.records = []
        self.open = []
        self.returns = set()
        address = address if address is not None else self.machine.symbols.get(name)
        self.present = address is not None
        if self.present:
            self.machine.add_address_hook(address, self.entered)

    def entered(self):
        machine = self.machine
        back = machine.reg(UC_ARM_REG_LR) & ~1
        card = self.session.card
        self.open.append({"back": back, "sp": machine.reg(UC_ARM_REG_SP),
                          "arguments": [machine.reg(UC_ARM_REG_R0), machine.reg(UC_ARM_REG_R1),
                                        machine.reg(UC_ARM_REG_R2)],
                          "start_us": machine.microseconds(), "reads": len(card.reads),
                          "instructions": machine.base_cycles})
        if back not in self.returns:
            self.returns.add(back)
            machine.add_address_hook(back, lambda: self.returned(back))

    def returned(self, back):
        machine = self.machine
        if not self.open or self.open[-1]["back"] != back or self.open[-1]["sp"] != machine.reg(UC_ARM_REG_SP):
            return
        call = self.open.pop()
        reads = self.session.card.reads[call.pop("reads"):]
        call.update(end_us=machine.microseconds(), result=machine.reg(UC_ARM_REG_R0),
                    read_commands=len(reads), read_sectors=sum(count for _, _, count in reads),
                    instructions=machine.base_cycles - call["instructions"])
        del call["back"], call["sp"]
        self.records.append(call)

    def since(self, start_us):
        return [call for call in self.records if call["start_us"] >= start_us]


def press(machine, key, hold_ms=KEY_HOLD_MS, settle_ms=KEY_SETTLE_MS):
    start = machine.milliseconds() + 20
    machine.press_key(KEYS[key], delay_ms=20, hold_ms=hold_ms)
    machine.run(until_ms=start + hold_ms + settle_ms)
    return start * 1000


def run_for(machine, milliseconds):
    machine.run(until_ms=machine.milliseconds() + milliseconds)


def settle_home(session):
    machine = session.machine
    machine.answer_waits_for_key(4)
    machine.run(until=lambda: len(machine.panel.lit_sweeps()) >= 2, until_ms=STEP_LIMIT_MS)
    run_for(machine, SETTLE_MS)


def read_volume_file(session, path):
    try:
        return session.fat().read(path)
    except (KeyError, ValueError, IndexError, TypeError):
        return None


def library_counts(session):
    library = read_volume_file(session, LIBRARY_PATH) or b""
    covers = read_volume_file(session, COVERS_PATH) or b""
    tracks = int.from_bytes(library[12:16], "little") if library[:4] == b"DSDB" else 0
    stored = int.from_bytes(covers[6:8], "little") if covers[:4] == b"DSCV" else 0
    return {"library_tracks": tracks, "stored_covers": stored}


def firmware_times(session):
    data = read_volume_file(session, TIMES_PATH)
    if not data or len(data) < 80:
        return None
    words = [int.from_bytes(data[index:index + 4], "little") for index in range(0, 80, 4)]
    passes = {}
    for number, stage in enumerate(("scan", "covers")):
        passes[stage] = {kind: {"ms": round(words[number * 5 + index] / 1000, 1),
                                "calls": words[10 + number * 5 + index]}
                         for index, kind in enumerate(TIME_KINDS)}
    return passes


class ScanRun:
    """One rescan, from the rescan code's entry to the end of its cover pass.

    Builds that run the cover pass in the background, after the library is published, end the
    scan pass at the first return of the scratch memory; the run then waits for `covers_run` to
    finish. Older builds end at the first return."""

    def __init__(self, session, artifacts, log):
        self.session = session
        self.machine = session.machine
        self.log = log
        self.profiler = Profiler(session, artifacts.rescan_overlay_elf)
        self.started = None
        self.ended = None
        self.clock = []
        self.progress = [0, 0]
        self.toasts = []
        self.toast_us = []
        self.armed = True
        self.covers_started = None
        self.covers_ended = None
        symbols = self.profiler.overlay.symbols
        self.background = "covers_run" in symbols
        if self.background:
            self.machine.add_address_hook(symbols["covers_run"], self.covers_entered)
        self.walks = Calls(session, "walk_music")
        self.machine.add_address_hook(self.profiler.overlay.symbols["rescan_run"], self.entered)
        self.machine.hook_symbol("hal_system_return_scratch", self.left)
        self.machine.hook_symbol("show_rescan_progress", self.counted, optional=True)
        self.machine.hook_symbol("toast_show", self.toasted)

    def entered(self):
        if self.armed and self.profiler.active and self.started is None:
            self.started = self.mark()
            self.profiler.start()

    def covers_entered(self):
        if self.ended is not None and self.covers_started is None:
            self.covers_started = self.mark()

    def left(self):
        if self.started is not None and self.ended is None:
            self.ended = self.mark()
            if not self.background:
                self.profiler.stop()
        elif self.covers_started is not None and self.covers_ended is None:
            self.covers_ended = self.mark()
            self.profiler.stop()

    def finished(self):
        return self.covers_ended if self.background else self.ended

    def mark(self):
        machine = self.machine
        self.clock.append(machine.core_hz)
        return {"us": machine.microseconds(), "instructions": machine.base_cycles,
                "waits": machine.jump_cycles, "wall": time.time(), "hz": machine.core_hz}

    def counted(self):
        self.progress = [self.machine.reg(UC_ARM_REG_R0), self.machine.reg(UC_ARM_REG_R1)]

    def toasted(self):
        self.toasts.append(self.machine.read_cstring(self.machine.reg(UC_ARM_REG_R1)).decode("utf-8", "replace"))
        self.toast_us.append(self.machine.microseconds())

    def run(self, limit_ms=SCAN_LIMIT_MS):
        machine = self.machine
        shown = time.time()
        end = machine.milliseconds() + limit_ms
        while self.finished() is None and machine.milliseconds() < end:
            reason = machine.run(until=lambda: self.finished() is not None,
                                 until_ms=min(end, machine.milliseconds() + 2000))
            if reason not in ("time limit", "condition met"):
                raise RuntimeError("the emulator stopped: %s" % reason)
            if time.time() - shown > PROGRESS_SECONDS:
                shown = time.time()
                self.log("  %d s emulated, %s files and tracks, scan %s" % (
                    machine.milliseconds() // 1000, self.progress,
                    "running" if self.started else "not started"))
        if self.finished() is None:
            raise RuntimeError("the rescan did not finish within %d ms" % limit_ms)
        machine.run(until=lambda: self.walk() is not None, until_ms=machine.milliseconds() + STEP_LIMIT_MS)
        run_for(machine, SETTLE_MS)

    def walk(self):
        return next((call for call in self.walks.records if call["start_us"] >= self.ended["us"]), None)

    def browsable_us(self):
        later = [moment for moment in self.toast_us if moment >= self.ended["us"]]
        return later[0] if later else self.ended["us"]

    def result(self):
        report = self.profiler.report()
        decode = report["functions"].get("cover_image_decode", {})
        calls = decode.get("calls", 0)
        last = self.finished()
        return {
            "emulated_ms": round((last["us"] - self.started["us"]) / 1000, 1),
            "scan_pass_ms": round((self.ended["us"] - self.started["us"]) / 1000, 1),
            "browsable_ms": round((self.browsable_us() - self.started["us"]) / 1000, 1),
            "cover_pass_ms": (round((self.covers_ended["us"] - self.covers_started["us"]) / 1000, 1)
                              if self.covers_ended else None),
            "instructions": last["instructions"] - self.started["instructions"],
            "emmc_wait_cycles": last["waits"] - self.started["waits"],
            "core_hz": sorted(set(self.clock)),
            "wall_seconds": round(last["wall"] - self.started["wall"], 1),
            "toasts": self.toasts,
            "counts": library_counts(self.session),
            "firmware_times": firmware_times(self.session),
            "decoded_covers": calls,
            "instructions_per_decode": decode.get("instructions", 0) // calls if calls else 0,
            "ms_per_decode": round(decode.get("ms", 0) / calls, 2) if calls else 0,
            "fingerprint_walk_after_scan": walk_summary(self.walk()),
            "profile": report,
        }


def walk_summary(walk):
    if walk is None:
        return None
    return {"ms": round((walk["end_us"] - walk["start_us"]) / 1000, 1), "instructions": walk["instructions"],
            "read_commands": walk["read_commands"], "read_sectors": walk["read_sectors"]}


def first_scan(artifacts, volume, log):
    session = artifacts.session(volume)
    run = ScanRun(session, artifacts, log)
    run.machine.answer_waits_for_key(4)
    run.run()
    return session, run


def module_loaded(machine, name):
    return any(overlay.name == "echo_module_" + name and overlay.loaded(machine)
               for overlay in machine.modules)


def open_settings(machine):
    """Home's rows depend on the library (an Audiobooks shelf comes before Settings), so each
    row down from the top is opened until the settings module is the one loaded."""
    for _ in range(HOME_ROWS):
        press(machine, "next")
        press(machine, "play", settle_ms=800)
        if module_loaded(machine, "settings"):
            return True
        press(machine, "back", settle_ms=600)
    return False


class Shortcut:
    """A requested rescan that found nothing changed and rebuilt nothing."""

    def __init__(self, run, pressed_us):
        self.run = run
        self.pressed_us = pressed_us

    def result(self):
        run = self.run
        return {"shortcut": True, "toasts": run.toasts,
                "emulated_ms": round((run.toast_us[0] - self.pressed_us) / 1000, 1),
                "fingerprint_walk": walk_summary(run.walks.records[-1] if run.walks.records else None)}


def requested_rescan(previous, artifacts, log):
    """Settings → Rescan library, pressed as a user would; the row is found by trying the
    likely positions on fresh starts until the settings module's `request_rescan` runs."""
    for presses in RESCAN_ROW_GUESSES:
        session = previous.reboot()
        machine = session.machine
        settle_home(session)
        run = ScanRun(session, artifacts, log)
        machine.note("request_rescan")
        if not open_settings(machine):
            raise RuntimeError("Settings did not open from Home")
        for _ in range(presses):
            press(machine, "next", settle_ms=120)
        pressed_us = machine.microseconds()
        press(machine, "play", settle_ms=50)
        machine.run(until=lambda: run.started is not None or run.toasts,
                    until_ms=machine.milliseconds() + STEP_LIMIT_MS)
        if not machine.reached.get("request_rescan"):
            continue
        if run.started is None and run.toasts:
            return session, Shortcut(run, pressed_us), presses
        if run.started is None:
            continue
        run.run()
        return session, run, presses
    raise RuntimeError("Settings → Rescan library was not found in rows %s" % (RESCAN_ROW_GUESSES,))


def usb_music_check(previous, artifacts, log):
    session = previous.reboot()
    machine = session.machine
    settle_home(session)
    walks = Calls(session, "walk_music")
    run = ScanRun(session, artifacts, log)
    machine.stub_symbol("usb_session_storage_changed", lambda machine: 1)
    machine.note("usb_storage_session")
    notices = machine.collect_notices()
    start_us = machine.microseconds()
    machine.vbus = True
    machine.run(until=lambda: walks.records or run.started is not None,
                until_ms=machine.milliseconds() + STEP_LIMIT_MS)
    run_for(machine, SETTLE_MS)
    walk = walks.records[0] if walks.records else None
    return {
        "session_ran": machine.reached.get("usb_storage_session", 0) > 0,
        "walk_found": walks.present,
        "walk": walk_summary(walk),
        "from_cable_ms": round((walk["end_us"] - start_us) / 1000, 1) if walk else None,
        "rescan_started": run.started is not None,
        "notices": [title + ": " + message for title, message in notices],
    }


class ArtWatch:
    """Cover loads, frames and the storage behind them while the screens run."""

    def __init__(self, session):
        self.session = session
        self.machine = session.machine
        self.half = Calls(session, "cover_cache_load_half")
        self.full = Calls(session, "cover_cache_load_full")
        self.reads = Calls(session, "covers_read")
        self.frames = Calls(session, "render")
        self.home = Calls(session, "home_art_update")
        self.plays = Calls(session, "hal_audio_play")

    def loads(self, start_us, end_us=None):
        found = []
        for kind, calls in (("half", self.half), ("full", self.full)):
            for call in calls.since(start_us):
                if end_us is not None and call["start_us"] > end_us:
                    continue
                read = [item for item in self.reads.records
                        if call["start_us"] <= item["start_us"] <= call["end_us"]]
                found.append({"kind": kind, "album": call["arguments"][2 if kind == "half" else 1],
                              "position": call["arguments"][1] if kind == "half" else None,
                              "start_us": call["start_us"], "end_us": call["end_us"],
                              "attempted": call["result"] != 0, "found": bool(read),
                              "read_commands": call["read_commands"], "read_sectors": call["read_sectors"],
                              "record_commands": sum(item["read_commands"] for item in read),
                              "instructions": call["instructions"]})
        return sorted(found, key=lambda item: item["start_us"])

    def frame_after(self, moment_us):
        for frame in self.frames.records:
            if frame["start_us"] >= moment_us:
                return frame
        return None

    def drawn_us(self, load):
        frame = self.frame_after(load["end_us"])
        return frame["end_us"] if frame else None

    def first_frame_after(self, moment_us):
        frame = self.frame_after(moment_us)
        return frame["end_us"] if frame else None


def milliseconds(later, earlier):
    return None if later is None or earlier is None else round((later - earlier) / 1000, 1)


def load_summary(watch, loads):
    read = [load for load in loads if load["attempted"]]
    return {"loads": len(loads), "read_loads": len(read),
            "found": sum(1 for load in read if load["found"]),
            "read_commands": sum(load["read_commands"] for load in read),
            "read_sectors": sum(load["read_sectors"] for load in read),
            "load_ms": round(sum(load["end_us"] - load["start_us"] for load in read) / 1000, 1),
            "commands_per_found": round(sum(load["read_commands"] for load in read if load["found"])
                                        / max(1, sum(1 for load in read if load["found"])), 2),
            "index_commands_per_found": round(
                sum(load["read_commands"] - load["record_commands"] for load in read if load["found"])
                / max(1, sum(1 for load in read if load["found"])), 2)}


def keyed(watch, key, hold_ms=KEY_HOLD_MS, settle_ms=KEY_SETTLE_MS, wanted=None):
    machine = watch.machine
    pressed = press(machine, key, hold_ms, settle_ms)
    loads = watch.loads(pressed)
    first = watch.first_frame_after(pressed)
    found = [load for load in loads if load["found"] and (wanted is None or load["kind"] == wanted)]
    last_drawn = watch.drawn_us(found[-1]) if found else None
    return {"key": key, "to_first_frame_ms": milliseconds(first, pressed),
            "to_last_cover_drawn_ms": milliseconds(last_drawn, pressed),
            "last_load_to_drawn_ms": milliseconds(last_drawn, found[-1]["start_us"]) if found else None,
            "summary": load_summary(watch, loads), "loads": loads}


def home_art(session, watch):
    machine = session.machine
    boot_loads = [load for load in watch.loads(0) if load["kind"] == "full" and load["attempted"]]
    found = [load for load in boot_loads if load["found"]]
    cold = None
    if found:
        first = found[0]
        cold = {"load_ms": milliseconds(first["end_us"], first["start_us"]),
                "load_to_drawn_ms": milliseconds(watch.drawn_us(first), first["start_us"]),
                "read_commands": first["read_commands"], "albums_tried": len(boot_loads),
                "tries_read_commands": sum(load["read_commands"] for load in boot_loads)}
    start = machine.microseconds()
    run_for(machine, HOME_CYCLE_WAIT_MS)
    cycled = [load for load in watch.loads(start) if load["attempted"]]
    cycled_found = [load for load in cycled if load["found"]]
    cycle = None
    if cycled:
        cycle = {"albums_tried": len(cycled), "load_ms": round(sum(load["end_us"] - load["start_us"]
                                                                   for load in cycled) / 1000, 1),
                 "read_commands": sum(load["read_commands"] for load in cycled),
                 "first_try_to_drawn_ms": milliseconds(watch.drawn_us(cycled_found[-1]), cycled[0]["start_us"])
                 if cycled_found else None}
    press(machine, "play")
    warm = keyed(watch, "back")
    return {"cold": cold, "cycle": cycle, "warm_return": warm}


def move_rows(machine, rows):
    for _ in range(abs(rows)):
        press(machine, "next" if rows > 0 else "previous")


def cover_flow(session, watch, music_row):
    """Opens Cover Flow from Home; the Music menu keeps the row it was left on."""
    machine = session.machine
    press(machine, "play")
    move_rows(machine, COVER_FLOW_ROW - music_row)
    entry = keyed(watch, "play", settle_ms=1500)
    steps = [keyed(watch, "next", settle_ms=800) for _ in range(COVER_FLOW_STEPS)]
    back_steps = [keyed(watch, "previous", settle_ms=800) for _ in range(COVER_FLOW_STEPS)]
    taps = tap_burst(watch, "next")
    hold = held(watch, "next")
    press(machine, "back")
    press(machine, "back")
    return {"entry": entry, "steps": steps, "steps_back": back_steps, "taps": taps, "hold": hold}


def tap_burst(watch, key):
    machine = watch.machine
    presses = []
    start = machine.microseconds()
    for _ in range(TAP_COUNT):
        presses.append(machine.microseconds() + 10000)
        machine.press_key(KEYS[key], delay_ms=10, hold_ms=100)
        run_for(machine, TAP_GAP_MS)
    run_for(machine, 1500)
    latencies = [milliseconds(watch.first_frame_after(moment), moment) for moment in presses]
    return {"taps": TAP_COUNT, "gap_ms": TAP_GAP_MS, "key_to_frame_ms": latencies,
            "worst_key_to_frame_ms": max(value for value in latencies if value is not None) if any(latencies) else None,
            "summary": load_summary(watch, watch.loads(start)), "frames": frame_stats(watch, start)}


def held(watch, key):
    machine = watch.machine
    start = machine.microseconds() + 20000
    machine.press_key(KEYS[key], delay_ms=20, hold_ms=HOLD_MS)
    run_for(machine, 20 + HOLD_MS)
    released = machine.microseconds()
    run_for(machine, 2000)
    loads = watch.loads(start)
    after = [load for load in loads if load["start_us"] >= released and load["found"]]
    return {"hold_ms": HOLD_MS, "during": load_summary(watch, watch.loads(start, released)),
            "frames": frame_stats(watch, start, released),
            "positions_moved": len({load["position"] for load in loads if load["kind"] == "half"}),
            "release_to_last_cover_drawn_ms": milliseconds(watch.drawn_us(after[-1]), released) if after else None}


def frame_stats(watch, start_us, end_us=None):
    frames = [frame for frame in watch.frames.records
              if frame["start_us"] >= start_us and (end_us is None or frame["start_us"] <= end_us)]
    if len(frames) < 2:
        return {"frames": len(frames)}
    gaps = [later["start_us"] - earlier["start_us"] for earlier, later in zip(frames, frames[1:])]
    return {"frames": len(frames), "mean_gap_ms": round(sum(gaps) / len(gaps) / 1000, 1),
            "worst_gap_ms": round(max(gaps) / 1000, 1),
            "mean_draw_ms": round(sum(frame["end_us"] - frame["start_us"] for frame in frames) / len(frames) / 1000, 1)}


def now_playing(session, watch):
    """All Songs from Home, its first song, a skip to the next song (another album), a skip
    back, then Back to the list, to Music and to Home while the track plays."""
    machine = session.machine
    press(machine, "play")
    move_rows(machine, MUSIC_ROWS_BEFORE_SONGS)
    press(machine, "play")
    opened = keyed(watch, "play", settle_ms=1500)
    plays = len(watch.plays.records)
    skip = keyed(watch, "next", hold_ms=LONG_PRESS_MS, settle_ms=1500, wanted="full")
    track_started = watch.plays.records[plays]["start_us"] if len(watch.plays.records) > plays else None
    found = [load for load in skip["loads"] if load["found"]]
    skip["track_start_to_cover_drawn_ms"] = milliseconds(watch.drawn_us(found[-1]), track_started) if found and track_started else None
    back = keyed(watch, "previous", hold_ms=LONG_PRESS_MS, settle_ms=1500, wanted="full")
    to_list = keyed(watch, "back")
    press(machine, "back")
    home = keyed(watch, "back")
    return {"open": opened, "skip_to_next_album": skip, "skip_back": back, "back_to_list": to_list,
            "home_while_playing": home}


def strip_loads(result):
    if isinstance(result, dict):
        return {key: strip_loads(value) for key, value in result.items() if key != "loads"}
    if isinstance(result, list):
        return [strip_loads(value) for value in result]
    return result


def display(previous, artifacts, log, covers=True):
    session = previous.reboot()
    if not covers:
        disable_covers(session)
    AudioPath(session.machine)
    watch = ArtWatch(session)
    settle_home(session)
    result = {"covers_file": covers, "missing_hooks": [calls.name for calls in
                                                       (watch.half, watch.full, watch.reads, watch.frames)
                                                       if not calls.present]}
    music_row = 0
    if covers:
        result["home"] = home_art(session, watch)
        log("  now playing")
        result["now_playing"] = now_playing(session, watch)
        music_row = MUSIC_ROWS_BEFORE_SONGS
    log("  cover flow")
    result["cover_flow"] = cover_flow(session, watch, music_row)
    return session, strip_loads(result)


def disable_covers(session):
    reader = session.fat()
    found = reader.find(COVERS_PATH)
    if found is None:
        return
    _, first, _ = found
    lba = reader.data_start + (first - 2) * reader.sectors_per_cluster
    session.disk.write(lba, bytes(emmc.SECTOR))


def assumptions(volume, first):
    manifest = volume.manifest
    return {
        "instruction_cost": "1 cycle per instruction (no wait states, no cache misses modelled)",
        "emmc": "read %d us + %d us per extra block; write %d us + %d us per block" % (
            emmc.READ_COMMAND_US, emmc.READ_EXTRA_BLOCK_US, emmc.WRITE_COMMAND_US, emmc.WRITE_BLOCK_US),
        "core_hz_during_scan": first["core_hz"] if first else None,
        "file_system": "%s, %d KiB clusters, %d GB card, internal eMMC user volume" % (
            manifest["file_system"], manifest["sectors_per_cluster"] // 2, manifest["card_gigabytes"]),
        "image_window": "%#x" % make_image.OVERLAY_BASE,
    }
