import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    FNV_PRIME,
    FNV_START,
    IDENTITY,
    PROJECT,
    summary,
)

TRACK = (PROJECT / "tests" / "fixtures" / "mp3" / "cbr_192_48k_stereo_bare.mp3").read_bytes()
COVER = b"\x89PNG\r\n\x1a\n" + bytes(56)
SCAN_PATH = "/" + IDENTITY.data_directory + "/scan.bin"
COVERS_PATH = "/" + IDENTITY.data_directory + "/covers.bin"
STEPS_AFTER_START = 3
GUARD_OFFSET = 512
GUARD_RUNNING = GUARD_OFFSET + 4
GUARD_ATTEMPTS = GUARD_OFFSET + 5
GUARD_MARK = GUARD_OFFSET + 8
GUARD_SKIPS = GUARD_OFFSET + 16
SKIP_SLOTS = 4
PHASE_MASK = 7
PHASE_TAGS = 2
PHASE_COVER = 5
RETRIES = 3
BATCH = 8
STORAGE_EVENT = 6
FAULT_ADDRESS = 0x100
SECTOR = 512
TRACK_BUDGET_US = 500
COST_ALBUMS = 4
COST_TRACKS = 10


def guard_mark(path, size, phase):
    key = FNV_START
    for byte in path.encode("utf-8"):
        key = ((key ^ byte) * FNV_PRIME) & 0xFFFFFFFF
    key = ((key ^ size) * FNV_PRIME) & 0xFFFFFFFF
    return (key & ~PHASE_MASK & 0xFFFFFFFF) | phase


def word(data, offset):
    return int.from_bytes(data[offset:offset + 4], "little")


def guard_record(running, attempts):
    record = bytearray(4096)
    record[GUARD_OFFSET:GUARD_OFFSET + 4] = b"DSSG"
    record[GUARD_RUNNING] = running
    record[GUARD_ATTEMPTS] = attempts
    return bytes(record)


def music_volume(albums=1, tracks=3, root=(), scan=None):
    from echo_emulator.fat_volume import Fat32Volume
    volume = Fat32Volume()
    for album in range(1, albums + 1):
        folder = "ALBUM%d" % album
        volume.add_directory(folder)
        for number in range(1, tracks + 1):
            volume.add_file("%02d.MP3" % number, TRACK, None, directory=folder)
        volume.add_file("COVER.PNG", COVER, None, directory=folder)
    for name, data in root:
        volume.add_file(name, data, None)
    volume.add_directory(IDENTITY.data_directory)
    volume.add_directory("debug", IDENTITY.data_directory)
    if scan is not None:
        volume.add_file("scan.bin", scan, None, directory=IDENTITY.data_directory)
    return volume


class EchoImageTest(EchoImageHarness):
    def fault_while_reading(self, machine, path):
        from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_R1
        state = {"open": False}

        def opened():
            state["open"] = machine.read_cstring(machine.reg(UC_ARM_REG_R1)).decode() == path

        def read():
            if state["open"]:
                machine.uc.reg_write(UC_ARM_REG_PC, FAULT_ADDRESS | 1)

        machine.hook_symbol("hal_file_open", opened)
        machine.hook_symbol("hal_file_close", lambda: state.update(open=False))
        machine.hook_symbol("hal_file_read", read)

    def fault_at(self, machine, name):
        from unicorn.arm_const import UC_ARM_REG_PC
        machine.hook_symbol(name, lambda: machine.uc.reg_write(UC_ARM_REG_PC, FAULT_ADDRESS | 1))

    def paths_opened_while_scanning(self, machine):
        from unicorn.arm_const import UC_ARM_REG_R1
        paths = []
        state = {"scanning": False}

        def opened():
            if state["scanning"]:
                path = machine.read_cstring(machine.reg(UC_ARM_REG_R1))
                paths.append(path.decode("utf-8", "replace"))

        machine.hook_symbol("hal_system_borrow_scratch", lambda: state.update(scanning=True))
        machine.hook_symbol("hal_file_open", opened)
        return paths

    def storage_events(self, machine):
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1
        values = []

        def recorded():
            if machine.reg(UC_ARM_REG_R0) == STORAGE_EVENT:
                values.append(machine.reg(UC_ARM_REG_R1))

        machine.hook_symbol("debug_loop_event", recorded)
        return values

    def boot_until_fault(self, session):
        reason = session.machine.run(until_ms=self.session_module.FRAME_LIMIT_MS)
        self.assertTrue(reason.startswith("fault"), reason)
        self.assertTrue(session.machine.faults)

    def scan_to_the_end(self, session):
        session.machine.stop_at("hal_system_return_scratch")
        reason = session.machine.run(until_ms=self.session_module.FRAME_LIMIT_MS)
        self.assertEqual(reason, "reached hal_system_return_scratch")

    def covers_to_the_end(self, session):
        machine = session.machine
        machine.continue_past_stop()
        machine.stop_at("hal_system_return_scratch")
        reason = machine.run(until_ms=machine.milliseconds() + self.session_module.FRAME_LIMIT_MS)
        self.assertEqual(reason, "reached hal_system_return_scratch")

    def guard(self, session):
        return session.fat().read(SCAN_PATH)

    def skips(self, session):
        data = self.guard(session)
        return [word(data, GUARD_SKIPS + 4 * slot) for slot in range(SKIP_SLOTS)]

    def report_after_usb(self, session):
        machine = session.machine
        machine.host_ejects()
        machine.answer_waits_for_key(4)
        machine.stop_at("usb_session_storage_changed")
        machine.vbus = True
        reason = machine.run(until_ms=machine.milliseconds() + self.session_module.FRAME_LIMIT_MS)
        self.assertEqual(reason, "reached usb_session_storage_changed")
        return self.read_report(session)

    def assert_no_scan_at_start(self, session):
        machine = session.machine
        machine.note("hal_system_borrow_scratch")
        steps = []

        def stepped():
            steps.append(machine.milliseconds())
            if len(steps) > STEPS_AFTER_START:
                machine.stop("started")

        self.assertEqual(session.run_to_first_frame(), "condition met")
        machine.hook_symbol("app_step", stepped)
        self.assertEqual(machine.run(until_ms=self.session_module.FRAME_LIMIT_MS), "started")
        self.assertNotIn("hal_system_borrow_scratch", machine.reached)

    def test_a_cover_that_faults_is_skipped_and_reported_on_the_next_start(self):
        cover = "/ALBUM1/COVER.PNG"
        first = self.session(volume=music_volume())
        self.fault_while_reading(first.machine, cover)
        self.boot_until_fault(first)
        mark = guard_mark(cover, len(COVER), PHASE_COVER)
        self.assertEqual(self.guard(first)[GUARD_RUNNING], 1)
        self.assertEqual(word(self.guard(first), GUARD_MARK), mark)
        second = first.reboot()
        events = self.storage_events(second.machine)
        self.scan_to_the_end(second)
        self.assertEqual(self.built_library_tracks(second), 3)
        self.assertIn(mark, self.skips(second))
        self.covers_to_the_end(second)
        self.assertEqual(self.guard(second)[GUARD_RUNNING], 0)
        self.assertIn(mark, events)
        second.machine.continue_past_stop()
        report = self.report_after_usb(second)
        details = [event["kind"] + " " + event["detail"] for event in report["events"]]
        self.assertIn("storage %d" % mark, details)
        self.assert_healthy(second)
        summary("cover fault recovered", second)

    def test_a_track_that_faults_is_narrowed_down_then_skipped(self):
        track = "/ALBUM1/02.MP3"
        first = self.session(volume=music_volume())
        self.fault_while_reading(first.machine, track)
        self.boot_until_fault(first)
        self.assertEqual(word(self.guard(first), GUARD_MARK) & PHASE_MASK, 1)
        second = first.reboot()
        self.fault_while_reading(second.machine, track)
        self.boot_until_fault(second)
        mark = guard_mark(track, len(TRACK), PHASE_TAGS)
        self.assertEqual(word(self.guard(second), GUARD_MARK), mark)
        third = second.reboot()
        self.fault_while_reading(third.machine, track)
        events = self.storage_events(third.machine)
        self.scan_to_the_end(third)
        self.assertEqual(self.built_library_tracks(third), 2)
        self.assertIn(mark, self.skips(third))
        self.assertIn(mark, events)
        self.assert_healthy(third)

    def test_three_stopped_scans_fall_back_to_no_covers_and_a_fourth_stops_retries(self):
        session = self.session(volume=music_volume())
        for attempt in range(1, RETRIES + 1):
            self.fault_at(session.machine, "hal_system_borrow_listing")
            self.boot_until_fault(session)
            self.assertEqual(self.guard(session)[GUARD_ATTEMPTS], attempt)
            session = session.reboot()
        paths = self.paths_opened_while_scanning(session.machine)
        self.scan_to_the_end(session)
        self.assertEqual(self.built_library_tracks(session), 3)
        self.assertNotIn(COVERS_PATH, paths)
        self.assertEqual(self.guard(session)[GUARD_RUNNING], 0)
        self.assert_healthy(session)
        sectors = session.file_sectors(SCAN_PATH)
        session.disk.write(sectors.start + 1, guard_record(1, RETRIES + 1)[SECTOR:2 * SECTOR])
        stopped = session.reboot()
        self.assert_no_scan_at_start(stopped)
        self.assert_healthy(stopped)

    def test_a_waiting_update_comes_before_an_interrupted_scan(self):
        newer = self.newer_image()
        interrupted = guard_record(1, 1)
        resumed = self.session(volume=music_volume(scan=interrupted))
        resumed.machine.stop_at("hal_system_borrow_scratch")
        self.assertEqual(resumed.machine.run(until_ms=self.session_module.FRAME_LIMIT_MS),
                         "reached hal_system_borrow_scratch")
        volume = music_volume(root=[(IDENTITY.update_file, newer)], scan=interrupted)
        waiting = self.session(volume=volume, vbus=True)
        waiting.machine.note("hal_system_borrow_scratch")
        self.assertEqual(self.boot_until_waiting_for_key(waiting), "reached board_wait_for_power_hold")
        self.assertEqual(waiting.machine.reached["updater_install"], 1)
        self.assertNotIn("hal_system_borrow_scratch", waiting.machine.reached)
        self.assertEqual(waiting.slot_image("slot_1"), newer[:self.session_module.SLOT_BYTES])

    def scan_time_us(self, stub_guard):
        from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2
        from echo_emulator.machine import STUB_RETURN
        session = self.session(volume=music_volume(COST_ALBUMS, COST_TRACKS))
        machine = session.machine
        writes = []

        def written():
            if machine.reg(UC_ARM_REG_R2) != GUARD_OFFSET or machine.reg(UC_ARM_REG_R1) != SECTOR * 8:
                return
            writes.append(machine.microseconds())
            if stub_guard:
                machine.uc.reg_write(UC_ARM_REG_R0, 1)
                machine.uc.reg_write(UC_ARM_REG_PC, STUB_RETURN | 1)

        marks = {}
        self.mark(machine, marks, "hal_system_borrow_scratch")
        machine.hook_symbol("hal_file_write_open", written)
        self.scan_to_the_end(session)
        self.assertEqual(self.built_library_tracks(session), COST_ALBUMS * COST_TRACKS)
        return machine.microseconds() - marks["hal_system_borrow_scratch"][1], len(writes)

    def test_marking_files_costs_under_half_a_millisecond_per_track(self):
        guarded_us, writes = self.scan_time_us(False)
        bare_us, _ = self.scan_time_us(True)
        tracks = COST_ALBUMS * COST_TRACKS
        batches = (tracks + BATCH - 1) // BATCH
        self.assertGreaterEqual(writes, 2 + batches)
        self.assertLessEqual(writes, 2 + batches + COST_ALBUMS)
        self.assertLessEqual(guarded_us - bare_us, TRACK_BUDGET_US * tracks)
        print("\n[scan guard] %d tracks, %d guard writes, %.1f ms added to a %.1f ms scan, "
              "%.2f ms per write" % (tracks, writes, (guarded_us - bare_us) / 1000, bare_us / 1000,
                                     (guarded_us - bare_us) / 1000 / max(writes, 1)))


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
