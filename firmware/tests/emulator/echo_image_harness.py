import argparse
import importlib.util
import struct
import sys
import tempfile
import unittest
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "tools"))
sys.path.insert(0, str(PROJECT.parent / "tools"))

import echo_debug
import lang_pack
import make_image
from identity import IDENTITY

SKIP_EXIT_CODE = 77
DEFAULT_IMAGE = PROJECT / "build" / "device" / IDENTITY.update_file
DEFAULT_ELF = PROJECT / "build" / "device" / "platform" / "device" / "echo_device.elf"
DEFAULT_USB_OVERLAY_ELF = DEFAULT_ELF.with_name("echo_usb_overlay.elf")
CONFIG = {"image": DEFAULT_IMAGE, "elf": DEFAULT_ELF, "usb_overlay_elf": DEFAULT_USB_OVERLAY_ELF}

FIRST_FRAME_LIMIT_MS = 1000
DEEP_TRACK = 12
FIRST_AUDIO_LIMIT_MS = 1500
TRACK_GAP_LIMIT_MS = 1000
FADE_LIMIT_MS = 1500
SCAN_RECORD_POLL_MS = 100
FAT_READS_PER_SECOND_LIMIT = 40
SPIN_POLLS_PER_REST = 10
WAIT_REST_TOLERANCE_MS = 1
DATA_READS_PER_SECOND_MINIMUM = 250
WIDTH = 480
HEIGHT = 222
FIRST_ROW = 49
LAST_ROW = FIRST_ROW + HEIGHT - 1
SLOT_SECTORS = 0x2800
CHUNK_SECTORS = 16
BACKLIGHT_HZ = 25000
MINI_STOCK_SIZE = 0x2000004
LOADER_FLAG_VALUE = 0xFFFF18AF
LOADER_FLAG_REGISTER = 0x4C
LOADER_SELECT_REGISTER = 0xC4
LOADER_SELECT_VALUE = 0x01000000
PWM_PERIOD = 0x04
PWM_CONTROL = 0x0C
PWM_RUN = 0x10003
GPIO_DATA = 0x00
MADCTL_CHOICES = (0x28, 0x68)
MADCTL_COMMAND = 0x36
MADCTL_SWAP = 0x20
MADCTL_MIRROR_ROWS = 0x80
MADCTL_MIRROR_COLUMNS = 0x40
PORTRAIT_COLUMNS = (FIRST_ROW, FIRST_ROW + HEIGHT - 1)
PORTRAIT_LAST_ROW = WIDTH - 1
SETTINGS_VERSION = 10
SETTINGS_ORIENTATION_FIELD = 26
SETTINGS_CHECKSUM_FIELD = 28
REPORT_BYTES = 16384
REPORT_SLOTS = 3
COLD_REPORT_MS = 10000
POWER_HOLD_MS = 2600
DEBUG_FOLDER = IDENTITY.data_directory + "/debug"
RUN_PATH = "/" + DEBUG_FOLDER + "/run.txt"
RESTART_PATH = "/" + DEBUG_FOLDER + "/restart.req"
RESTART_REQUEST = b"RESTART AFTER INSTALL\n"
RESTART_ANSWER = b"CONNECT AFTER RESTART\n"
RECONNECT_WAIT_MS = 6000
CHARGER_MS = 10000
SCRIPT_LIMIT_MS = 40000
RECONNECT_OFF_MS = 1000
QUIET_AFTER_MS = 4000
BOOT_FLAG_CLEAR = 0xFFFF0000
REPORT_START_LIMIT_MS = 250
CABLE_STEADY_MS = 1000
CLOCK_TICK_MS = 1
GRF_WORDS = ("e8", "84", "b4", "b8", "98")
GRF_WORDS_POWERED = [2, 3, 2, 0, 4]
CABLE_NOTICED_LIMIT_MS = 100
CLOCK_SLACK_MS = 5
CODE_WINDOW_BYTES = 0x2666C
PROFILE_BUCKET_BYTES = 128
KEY_NEXT = 100
KEY_PLAY = 250
KEY_BACK = 500
KEY_HOLD_MS = 150
KEY_SETTLE_MS = 350
BACKLIGHT_PIN = 4
SOAK_MINUTE_MS = 60000
ABOUT_PROFILER_ROW = 13
USB_POWER_DOWN = [("grf", 0xB4, 0x00010001), ("grf", 0xB4, 0x000E0004), ("grf", 0xB8, 0x00010001),
                  ("grf", 0x98, 0x00040000), ("cru", 0xC0, 0x00020002), ("cru", 0xC0, 0x00040004),
                  ("cru", 0xC0, 0x00010001), ("cru", 0x80, 0x00800080), ("cru", 0x98, 0x04000400)]
SOFT_DISCONNECT = ("usb", "soft disconnect", 0)
USB_PHY_ON = [("grf", 0xB8, 0x00010000), ("grf", 0xB4, 0x000E0002), ("grf", 0x98, 0x00040004),
              ("grf", 0xB4, 0x00010000)]
USB_PHY_TUNING = [("grf", 0x88, 0x0C000C00), ("grf", 0x84, 0xE01F0003), ("grf", 0x8C, 0x001C0014),
                  ("grf", 0x80, 0x00070004)]
USB_POWER_UP = [("cru", 0x80, 0x00800000), ("cru", 0x98, 0x04000000), ("cru", 0xC0, 0x00020000),
                ("cru", 0xC0, 0x00040000), ("cru", 0xC0, 0x00010000)]
PROFILED_MS = 1500
LANGUAGE_RAM = 0x0309B000
MISSING_TEXT = "Text is missing: reinstall the update"
MODULE_LOAD_LIMIT_MS = 60
MODULE_REUSE_LIMIT_US = 100
MODULE_PREFIX = "echo_module_"
MUSIC_ROWS_BETWEEN_SONGS_AND_COVER_FLOW = 6
SETTINGS_PATH = "/" + IDENTITY.data_directory + "/settings.bin"
SETTINGS_REPEAT_FIELD = 12
REPEAT_ALL = 2
SETTINGS_REPEAT_ROW = 2
HOME_UP_NEXT_AFTER_SETTINGS = 2
STEP_GAP_WITH_A_MODULE_MS = 250
STEPS_AFTER_NOTICE = 20
FNV_START = 2166136261
FNV_PRIME = 16777619

GAMMA_POSITIVE = (0xF0, 0x0B, 0x12, 0x09, 0x0A, 0x26, 0x39, 0x54, 0x4E, 0x38, 0x13, 0x13, 0x2E,
                  0x34)
GAMMA_NEGATIVE = (0xF0, 0x10, 0x15, 0x0D, 0x0C, 0x07, 0x38, 0x43, 0x4D, 0x3A, 0x16, 0x15, 0x30,
                  0x35)


def expected_init(madctl):
    return [
        (0x11, ()), (0xF0, (0xC3,)), (0xF0, (0x96,)), (0x36, (madctl,)), (0x3A, (0x55,)),
        (0xB4, (0x01,)), (0xB6, (0x20, 0x02, 0x3B)), (0xB7, (0xC6,)), (0xB9, (0x02, 0xE0)),
        (0xC0, (0xC0, 0x64)), (0xC1, (0x1D,)), (0xC2, (0xA7,)), (0xC5, (0x28,)),
        (0xE8, (0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33)), (0xE0, GAMMA_POSITIVE),
        (0xE1, GAMMA_NEGATIVE), (0xF0, (0x3C,)), (0xF0, (0x69,)), (0x35, (0x00,)), (0x29, ()),
        (0x21, ()), (0x2A, (0x00, 0x31, 0x01, 0x0E)), (0x2B, (0x00, 0x00, 0x01, 0xDF)),
        (0x2C, ()),
    ]


def english(name):
    return dict(lang_pack.read_catalogue())[name]


def settings_file(orientation):
    data = bytearray(SETTINGS_CHECKSUM_FIELD + 4)
    data[0:4] = b"DSST"
    data[4] = SETTINGS_VERSION
    data[6:9] = bytes((7, 2, 0))
    data[13] = 13
    data[14] = 2
    data[16] = 35
    data[19:24] = bytes((10,) * 5)
    data[24] = 1
    data[SETTINGS_ORIENTATION_FIELD] = orientation
    checksum = FNV_START
    for byte in data[:SETTINGS_CHECKSUM_FIELD]:
        checksum = (checksum ^ byte) * FNV_PRIME & 0xFFFFFFFF
    data[SETTINGS_CHECKSUM_FIELD:] = checksum.to_bytes(4, "little")
    return bytes(data)


def pin_high_index(machine, port, pin):
    for index, entry in enumerate(machine.gpio_log):
        _, entry_port, _, _, data, direction, _ = entry
        if entry_port == port and data >> pin & 1 and direction >> pin & 1:
            return index
    return None


def pin_ever_output(machine, port, pin):
    return any(entry[1] == port and entry[5] >> pin & 1 for entry in machine.gpio_log)


def summary(name, session):
    machine = session.machine
    panel = machine.panel
    print("\n[%s] %d ms emulated, %d VOP commands, %d windows, %d pixels, %d sweeps, "
          "%d eMMC writes, %d unmodelled registers" % (
              name, machine.milliseconds(), len(panel.groups), len(panel.windows),
              panel.pixel_total, len(panel.sweeps), len(session.card.writes),
              len(machine.unknown_mmio)))


class EchoImageHarness(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from echo_emulator import session
        cls.session_module = session
        cls.image = Path(CONFIG["image"]).read_bytes()
        cls.elf = CONFIG["elf"]
        cls.usb_overlay_elf = CONFIG["usb_overlay_elf"]
        cls.module_elfs = sorted(Path(cls.elf).parent.glob("echo_module_*.elf"))

    def session(self, files=(), image=None, vbus=False, volume=None, rest_in_waits=True):
        return self.session_module.Session(image or self.image, self.elf, files=files,
                                           vbus_after_first_frame=vbus, volume=volume,
                                           usb_overlay_elf=self.usb_overlay_elf,
                                           rest_in_waits=rest_in_waits,
                                           module_elfs=self.module_elfs)

    def assert_healthy(self, session):
        machine = session.machine
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.panics, [])

    def assert_only_the_user_volume_changed(self, session):
        layout = session.layout
        first = layout["volume_start"]
        last = first + layout["volume_sectors"]
        for _, lba, count in session.card.writes:
            self.assertTrue(first <= lba and lba + count <= last, (lba, count))
        for lba in session.changed_sectors():
            self.assertTrue(first <= lba < last, lba)

    def assert_windows_inside_panel(self, panel):
        drawn = [window for window in panel.windows if window.pixels > 0]
        self.assertTrue(drawn)
        for window in drawn:
            self.assertTrue(0 <= window.columns[0] <= window.columns[1] < WIDTH, window.columns)
            self.assertTrue(FIRST_ROW <= window.rows[0] <= window.rows[1] <= LAST_ROW,
                            window.rows)
            self.assertEqual(window.pixels, window.width * window.height)
        self.assertEqual(panel.out_of_range, 0)


    def portrait_windows(self, panel):
        return [window for window in panel.windows if window.columns == PORTRAIT_COLUMNS and window.pixels > 0]

    def boot_saved_orientation(self, orientation):
        from echo_emulator.fat_volume import Fat32Volume
        volume = Fat32Volume()
        volume.add_directory(IDENTITY.data_directory)
        volume.add_file("settings.bin", settings_file(orientation), directory=IDENTITY.data_directory)
        session = self.session(volume=volume)
        panel = session.panel
        reason = session.machine.run(
            until=lambda: sum(w.pixels for w in self.portrait_windows(panel)) >= WIDTH * HEIGHT,
            until_ms=self.session_module.FRAME_LIMIT_MS)
        self.assertEqual(reason, "condition met")
        return session

    def assert_turned_before_the_first_portrait_frame(self, session, madctl_change):
        panel = session.panel
        init = panel.init_commands()
        landscape = dict(init)[MADCTL_COMMAND][0]
        self.assertEqual(init, expected_init(landscape))
        turns = [params for command, params in panel.groups if command == MADCTL_COMMAND][1:]
        self.assertEqual(turns, [[madctl_change(landscape & ~MADCTL_SWAP)]])
        first = panel.windows.index(self.portrait_windows(panel)[0])
        for window in panel.windows[first:]:
            self.assertEqual(window.columns, PORTRAIT_COLUMNS)
            self.assertTrue(0 <= window.rows[0] <= window.rows[1] <= PORTRAIT_LAST_ROW)
        self.assertEqual(max(window.rows[1] for window in panel.windows[first:]),
                         PORTRAIT_LAST_ROW)
        self.assert_healthy(session)


    @staticmethod
    def usb_supply_pins(machine):
        return (machine.gpio[0].regs[GPIO_DATA] >> 14 & 1, machine.gpio[2].regs[GPIO_DATA] & 1)


    def built_library_tracks(self, session):
        for data in self.volume_sectors(session):
            if data[:4] == b"DSDB":
                return struct.unpack_from("<I", data, 12)[0]
        return 0


    def volume_sectors(self, session):
        start = session.layout["volume_start"]
        return [data for lba, data in session.disk.sectors.items() if lba >= start]

    def recorded_version(self, session):
        for data in self.volume_sectors(session):
            if data[:4] == b"DSSC":
                return bytes(data[16:32]).rstrip(b"\0").decode("ascii")
        return None

    def recorded_entries(self, session):
        for data in self.volume_sectors(session):
            if data[:4] == b"DSSC":
                return int.from_bytes(data[12:16], "little")
        return None


    def assert_every_write_read_back(self, accesses):
        for index, access in enumerate(accesses):
            if access[0] != "W":
                continue
            lba, count = access[1], access[2]
            covered = []
            for later in accesses[index + 1:]:
                if later[0] != "R" or len(covered) >= count:
                    break
                covered.extend(range(later[1], later[1] + later[2]))
            self.assertEqual(covered[:count], list(range(lba, lba + count)))

    def newer_image(self):
        return self.session_module.with_date(self.image, (2099, 1, 1))


    def assert_update_refused(self, update):
        session = self.session(files=[update], vbus=True)
        session.machine.note("updater_install")
        session.machine.note("updater_check")
        session.run_to_first_frame()
        self.assertEqual(session.run_until_waiting_for_key(presses=1),
                         "reached board_wait_for_key")
        self.assertEqual(session.machine.reached["updater_check"], 1)
        self.assertNotIn("updater_install", session.machine.reached)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        summary("refused " + update[0], session)

    def refuse_an_update(self, rest_in_waits):
        mini = self.session_module.with_brand(self.image, "EchoPod")
        session = self.session(files=[("ECHOV497.IMG", mini, None)], vbus=True,
                               rest_in_waits=rest_in_waits)
        session.machine.note("updater_check")
        polls = session.machine.trace("board_input_take_key_press")
        session.run_to_first_frame()
        self.assertEqual(session.run_until_waiting_for_key(presses=1),
                         "reached board_wait_for_key")
        self.assert_healthy(session)
        return session, polls


    def boot_until_waiting_for_key(self, session):
        session.machine.note("updater_install")
        session.machine.note("updater_check")
        session.run_to_first_frame()
        session.machine.answer_waits_for_key(2)
        session.machine.stop_at("usb_session_storage_changed")
        session.machine.stop_at("board_wait_for_power_hold")
        return session.machine.run(until_ms=self.session_module.FRAME_LIMIT_MS)


    def reports(self, session):
        """(path, parsed report) of every report file on the drive, newest first."""
        reader = session.fat()
        found = []
        for slot in range(REPORT_SLOTS):
            path = "/%s/report%d.txt" % (DEBUG_FOLDER, slot)
            data = reader.read(path)
            if data is not None:
                self.assertEqual(len(data), REPORT_BYTES)
                found.append((path, echo_debug.parse_report(data.decode("ascii"))))
        return sorted(found, key=lambda item: -item[1]["values"]["report.sequence"])

    def report_path(self, session):
        return self.reports(session)[0][0]

    def read_report(self, session):
        return self.reports(session)[0][1]

    def mark(self, machine, marks, name, after=None):
        def reached():
            if after is None or after in marks:
                marks.setdefault(name, (machine.stamp(), machine.microseconds()))

        machine.hook_symbol(name, reached)


    def press(self, machine, key):
        start = machine.milliseconds()
        machine.press_key(key, delay_ms=20, hold_ms=KEY_HOLD_MS)
        machine.run(until_ms=start + 20 + KEY_HOLD_MS + KEY_SETTLE_MS)

    def switch_profiler_on(self, machine):
        for key in [KEY_NEXT, KEY_PLAY, KEY_PLAY] + [KEY_NEXT] * ABOUT_PROFILER_ROW + [KEY_PLAY]:
            self.press(machine, key)

    def index_of(self, log, sequence, start=0):
        for index in range(start, len(log) - len(sequence) + 1):
            if log[index:index + len(sequence)] == sequence:
                return index
        return None


    def writes_after_last_disconnect(self, machine, since=0):
        log = machine.register_log[since:]
        last = len(log) - 1 - log[::-1].index(SOFT_DISCONNECT)
        return log[last + 1:]


    def build_tag(self):
        return self.image[0x30:0x38].rstrip(b"\0").decode("ascii")

    def debug_volume(self, volume=None, root=(), **debug_files):
        from echo_emulator.fat_volume import Fat32Volume
        volume = volume or Fat32Volume()
        for name, data in root:
            volume.add_file(name, data, None)
        volume.add_directory(IDENTITY.data_directory)
        volume.add_directory("debug", IDENTITY.data_directory)
        for name, data in debug_files.items():
            volume.add_file(name.replace("_", "."), data, None, DEBUG_FOLDER)
        return volume

    def ejecting_session(self, volume, notes=()):
        session = self.session(volume=volume)
        machine = session.machine
        for name in ("usb_session_storage_changed", "debug_script_take", "hal_system_usb_reconnect",
                     "debug_restart_after_install", "board_restart", "updater_install",
                     "board_wait_for_power_hold", *notes):
            machine.note(name)
        machine.host_ejects()
        machine.answer_waits_for_key(4)
        session.run_to_first_frame()
        machine.vbus = True
        return session, machine

    def run_until_reconnect_request(self, machine):
        reached = machine.reached
        return machine.run(until=lambda: reached.get("hal_system_usb_reconnect", 0) >= 1,
                           until_ms=machine.milliseconds() + SCRIPT_LIMIT_MS)

    def log_time(self, machine, text, occurrence=0):
        return [stamp for stamp, event in machine.usb_log if event.startswith(text)][occurrence]

    def print_usb_log(self, title, machine, extra=()):
        events = sorted(list(machine.usb_log) + list(extra))
        print("\n[%s] USB events: %s" % (title, "; ".join("%d ms %s" % item for item in events)))

    def converted_screenshots(self, session, directory):
        volume = Path(directory) / "volume"
        session.fat().extract("/" + DEBUG_FOLDER, volume / IDENTITY.data_directory / "debug")
        return echo_debug.Tool(volume=volume).screenshots(Path(directory) / "png")["screenshots"]


    def run_probe_script(self, lines):
        script = ("build %s\n%s\nreconnect\n" % (self.build_tag(), "\n".join(lines))).encode("ascii")
        session, machine = self.ejecting_session(self.debug_volume(run_txt=script))
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        return session, machine, self.read_report(session)

    def soaking_session(self, lines):
        """A player whose script has just taken the USB supply away for a soak."""
        script = ("build %s\n%s\n" % (self.build_tag(), "\n".join(lines))).encode("ascii")
        session, machine = self.ejecting_session(self.debug_volume(run_txt=script))
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.usb_supply_pins(machine), (1, 1))
        self.assertEqual(self.run_until_supply(machine, (0, 0), SCRIPT_LIMIT_MS), "condition met")
        return session, machine

    def run_until_supply(self, machine, pins, limit_ms):
        return machine.run(until=lambda: self.usb_supply_pins(machine) == pins,
                           until_ms=machine.milliseconds() + limit_ms)

    def soak_of(self, report):
        self.assertEqual(len(report["script"]["soaks"]), 1)
        return report["script"]["soaks"][0]


    def hold_power(self, machine):
        machine.power_key_released_at_ms = machine.milliseconds() + POWER_HOLD_MS


    def check_usb_charge_shutdown(self, screen_off):
        session, machine = self.ejecting_session(self.debug_volume())
        machine.note("debug_report_write")
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        machine.run(until_ms=COLD_REPORT_MS + 500)
        _, settings = self.module(machine, "settings")
        self.assertTrue(settings.loaded(machine))
        if screen_off:
            machine.power_key_released_at_ms = machine.milliseconds() + KEY_HOLD_MS
            machine.run(until_ms=machine.milliseconds() + KEY_SETTLE_MS)
            self.assertEqual(machine.gpio[2].regs[GPIO_DATA] & 1 << BACKLIGHT_PIN, 0)
        self.hold_power(machine)
        machine.run(until=lambda: machine.reached.get("debug_report_write", 0) >= 2,
                    until_ms=machine.milliseconds() + 8000)
        machine.run(until_ms=machine.milliseconds() + 1000)
        newest, older = self.reports(session)
        self.assertEqual((newest[1]["values"]["report.sequence"], newest[1]["values"]["report.reason"],
                          newest[1]["values"]["report.cold_start"]), (2, "shutdown", 0))
        self.assertEqual((older[1]["values"]["report.reason"], older[1]["values"]["report.cold_start"]),
                         ("usb_session", 1))
        self.assertNotEqual(newest[0], older[0])
        self.assertEqual(newest[1]["values"]["power.vbus"], 1)
        self.assertEqual(newest[1]["values"]["usb.last_end"], "ejected")
        self.assertEqual(machine.panics, [])
        self.assertIsNone(machine.reset_requested)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        machine.note("hal_system_request_usb_mode")
        from echo_emulator.playback import KEYS
        self.press(machine, KEYS["menu"])
        self.assertTrue(settings.loaded(machine))
        self.press(machine, KEY_PLAY)
        self.assertEqual(machine.reached.get("hal_system_request_usb_mode", 0), 1)
        self.assertEqual(machine.usb_connect_count, 1)
        self.assert_healthy(session)

    def cold_session_on(self, earlier):
        session = earlier.reboot()
        machine = session.machine
        machine.note("usb_session_storage_changed")
        machine.answer_waits_for_key(4)
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        return session


    def run_usb_sessions(self, session, count):
        reached = session.machine.reached
        return session.machine.run(
            until=lambda: reached.get("usb_session_storage_changed", 0) >= count,
            until_ms=session.machine.milliseconds() + self.session_module.FRAME_LIMIT_MS)


    def loaded_language_pack(self, session):
        pack = make_image.language_packs_in(self.image)[0]
        return session.machine.read_memory(LANGUAGE_RAM, len(pack)) == pack


    def realistic_boot(self, sectors_per_cluster, next_free, usb_ms, gap_ms):
        from echo_emulator.real_volume import real_volume
        volume = real_volume(sectors_per_cluster=sectors_per_cluster, next_free=next_free)
        session = self.session(volume=volume)
        machine = session.machine
        machine.vbus = True
        listens = machine.trace("usb_session_listen")
        steps = machine.trace("app_step")
        machine.answer_waits_for_key(5)
        self.assertEqual(session.run_to_first_frame(), "condition met")
        self.assertLess(machine.milliseconds(), FIRST_FRAME_LIMIT_MS)
        self.assertEqual(session.run_to_home_screen(), "condition met")
        self.assertTrue(listens)
        self.assertGreaterEqual(listens[0], CABLE_STEADY_MS)
        self.assertLess(listens[0], usb_ms)
        self.assertLess(machine.milliseconds(), 20000)
        gaps = [later - earlier for earlier, later in zip(steps, steps[1:])]
        self.assertLess(max(gaps), gap_ms)
        self.assertEqual(machine.watchdog_resets, 0)
        self.assertGreater(machine.watchdog_kicks, 0)
        self.assert_healthy(session)
        summary("realistic %d sectors per cluster" % sectors_per_cluster, session)


    def playback_rig(self):
        from echo_emulator.playback import Rig
        return Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                   module_elfs=self.module_elfs)


    def speech_rig(self, rate=8000):
        from echo_emulator.playback import Rig
        from echo_emulator.fat_volume import Fat32Volume
        pattern = struct.pack("<8h", 0, 2828, 4000, 2828, 0, -2828, -4000, -2828)
        pcm = pattern * (rate * 30 // 8)
        wav = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ",
                          16, 1, 1, rate, rate * 2, 2, 16, b"data", len(pcm)) + pcm
        volume = Fat32Volume()
        volume.add_file("Speech.wav", wav)
        return Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                   volume=volume, module_elfs=self.module_elfs)

    def replay_gain_rig(self, mode, clipping=True, equalizer=0):
        from echo_emulator.playback import Rig
        from echo_emulator.fat_volume import Fat32Volume
        pattern = (0, 14142, 20000, 14142, 0, -14142, -20000, -14142)
        pcm = struct.pack("<8h", *pattern) * (44100 * 30 // 8)
        frames = bytearray()
        values = {"REPLAYGAIN_TRACK_GAIN": "+6.00 dB", "REPLAYGAIN_ALBUM_GAIN": "-12.00 dB",
                  "REPLAYGAIN_TRACK_PEAK": "1", "REPLAYGAIN_ALBUM_PEAK": "1"}
        for key, value in values.items():
            payload = b"\x00" + key.encode() + b"\x00" + value.encode()
            frames += b"TXXX" + struct.pack(">I", len(payload)) + b"\x00\x00" + payload
        size = len(frames)
        tag = b"ID3\x03\x00\x00" + bytes((size >> shift & 127 for shift in (21, 14, 7, 0))) + frames
        body = (b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, 44100, 88200, 2, 16)
                + b"data" + struct.pack("<I", len(pcm)) + pcm
                + b"id3 " + struct.pack("<I", len(tag)) + tag + bytes(len(tag) % 2))
        volume = Fat32Volume()
        volume.add_file("Speech.wav", b"RIFF" + struct.pack("<I", len(body)) + body)
        volume.add_directory(IDENTITY.data_directory)
        settings = bytearray(42)
        settings[:6] = b"DSST" + bytes((14, mode | (4 if clipping else 0)))
        settings[6:9] = bytes((7, 2, 0))
        settings[13], settings[14], settings[16] = 60, 2, 35
        settings[18] = equalizer
        settings[24], settings[27] = 1, 1
        settings[28:38] = bytes((20,) * 10)
        checksum = FNV_START
        for value in settings[:38]:
            checksum = (checksum ^ value) * FNV_PRIME & 0xFFFFFFFF
        settings[38:] = struct.pack("<I", checksum)
        volume.add_file("settings.bin", bytes(settings), directory=IDENTITY.data_directory)
        return Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                   volume=volume, module_elfs=self.module_elfs), pattern

    def assert_normalized_pcm(self, rig, pattern, gain):
        from echo_emulator.audio import PRODUCED_OFFSET, RING_BYTES
        produced = struct.unpack("<I", rig.machine.read_memory(
            rig.audio.engine + PRODUCED_OFFSET, 4))[0]
        self.assertGreater(produced, 128)
        for frame in range(produced - 128, produced):
            value = (pattern[frame % len(pattern)] * 256 * gain + 32768) >> 16
            value = max(-8388608, min(8388607, value))
            expected = value << 8 & 0xFFFFFFFF
            offset = rig.audio.ring + frame % (RING_BYTES // 8) * 8
            self.assertEqual((expected, expected), struct.unpack(
                "<II", rig.machine.read_memory(offset, 8)), frame)


    def vorbis_rig(self, mixed=False):
        from echo_emulator.core1_bridge import CORE1_VORBIS, Core1Bridge
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        volume = Fat32Volume()
        if mixed:
            rate = 8000
            pcm = struct.pack("<8h", 0, 2828, 4000, 2828, 0, -2828, -4000, -2828) * (rate * 30 // 8)
            wav = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ",
                              16, 1, 1, rate, rate * 2, 2, 16, b"data", len(pcm)) + pcm
            volume.add_file("A.wav", wav)
        volume.add_file("B.ogg", (PROJECT / "tests/fixtures/vorbis/seek44.ogg").read_bytes())
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        program = self.elf.with_name("core1_vorbis.bin").read_bytes()
        return rig, Core1Bridge(rig.audio, {CORE1_VORBIS: program})

    def opus_rig(self):
        from echo_emulator.core1_bridge import CORE1_OPUS, CORE1_VORBIS, Core1Bridge
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        volume = Fat32Volume()
        volume.add_file("B.ogg", (PROJECT / "tests/fixtures/vorbis/seek44.ogg").read_bytes())
        volume.add_file("C.opus", (PROJECT / "tests/fixtures/opus/stereo48.opus").read_bytes())
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        programs = {CORE1_VORBIS: self.elf.with_name("core1_vorbis.bin").read_bytes(),
                    CORE1_OPUS: self.elf.with_name("core1_opus.bin").read_bytes()}
        return rig, Core1Bridge(rig.audio, programs)


    def converted_reference(self, channels, factor, count):
        import audio_resample_table
        rows = [audio_resample_table.coefficients(12 * phase // factor) for phase in range(factor)]
        frames = []
        for frame in range(count):
            cursor, phase = divmod(frame, factor)
            first = cursor - audio_resample_table.LEFT
            pair = []
            for samples in channels:
                total = sum(coefficient * samples[first + tap]
                            for tap, coefficient in enumerate(rows[phase])
                            if 0 <= first + tap < len(samples))
                pair.append(max(-8388608, min(8388607, (total + (1 << 29)) >> 30)))
            frames.append(tuple(pair))
        return frames


    def module(self, machine, name):
        for number, overlay in enumerate(machine.modules, 1):
            if overlay.name == MODULE_PREFIX + name:
                return number, overlay
        self.fail("the image has no module named " + name)

    def collect_toasts(self, machine):
        from unicorn.arm_const import UC_ARM_REG_R1
        toasts = []
        machine.hook_symbol("toast_show", lambda: toasts.append(
            machine.read_cstring(machine.reg(UC_ARM_REG_R1)).decode("utf-8", "replace")))
        return toasts

    def assert_module_code_never_written(self, watch):
        self.assertEqual(watch.code_writes, [])

    def started_session(self, prepare=None, **options):
        """A player that has finished its first start (files made, library scanned) and rests."""
        session = self.session(**options)
        machine = session.machine
        if prepare is not None:
            prepare(session)
        machine.note("hal_system_return_scratch")
        machine.answer_waits_for_key(4)
        machine.run(until=lambda: "hal_system_return_scratch" in machine.reached,
                    until_ms=self.session_module.FRAME_LIMIT_MS)
        machine.run(until_ms=machine.milliseconds() + 1500)
        return session, machine

    def release_to_first_draw_ms(self, machine, key):
        """Tap a key and return how long after its release the panel got its next pixels."""
        released = machine.milliseconds() + 20 + KEY_HOLD_MS
        machine.press_key(key, delay_ms=20, hold_ms=KEY_HOLD_MS)
        machine.run(until_ms=released)
        windows = len(machine.panel.windows)
        machine.run(until=lambda: len(machine.panel.windows) > windows, until_ms=released + 1000)
        drawn = machine.milliseconds()
        machine.run(until_ms=released + KEY_SETTLE_MS)
        return drawn - released

    def open_settings(self, machine):
        self.press(machine, KEY_NEXT)
        self.press(machine, KEY_PLAY)

    def assert_module_screen_refused(self, session, watch, toasts, number):
        machine = session.machine
        self.assertEqual(toasts, [english("STR_SCREEN_MISSING")])
        self.assertEqual([(call["module"], call["loaded"]) for call in watch.loads()],
                         [(number, False)])
        self.assertEqual(watch.executed, 0)
        self.assertFalse(any(machine.read_memory(make_image.MODULE_BASE,
                                                 make_image.MODULE_CAPACITY)))
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)


def prerequisites_missing():
    if importlib.util.find_spec("unicorn") is None:
        return "unicorn is not installed"
    for path in (CONFIG["image"], CONFIG["elf"]):
        if not Path(path).exists():
            return "%s is missing" % path
    return None


def parse_arguments(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, default=DEFAULT_IMAGE)
    parser.add_argument("--elf", type=Path, default=DEFAULT_ELF)
    parser.add_argument("--usb-overlay-elf", type=Path, default=DEFAULT_USB_OVERLAY_ELF)
    parser.add_argument("tests", nargs="*")
    return parser.parse_args(argv)


def scenario_cases(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from scenario_cases(item)
        else:
            yield item


def main(test_case=None, argv=None):
    arguments = parse_arguments(argv)
    CONFIG["image"] = arguments.image
    CONFIG["elf"] = arguments.elf
    CONFIG["usb_overlay_elf"] = arguments.usb_overlay_elf
    reason = prerequisites_missing()
    if reason:
        print("SKIP: " + reason)
        return SKIP_EXIT_CODE
    if test_case is None:
        suite = unittest.defaultTestLoader.discover(str(Path(__file__).with_name("scenarios")))
        if arguments.tests:
            wanted = {name.rsplit(".", 1)[-1] for name in arguments.tests}
            cases = list(scenario_cases(suite))
            missing = wanted - {case._testMethodName for case in cases}
            if missing:
                raise SystemExit("Unknown scenarios: " + ", ".join(sorted(missing)))
            suite = unittest.TestSuite(case for case in cases if case._testMethodName in wanted)
    else:
        suite = unittest.defaultTestLoader.loadTestsFromNames(
            arguments.tests, sys.modules[test_case.__module__]) if arguments.tests else (
            unittest.defaultTestLoader.loadTestsFromTestCase(test_case))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1
