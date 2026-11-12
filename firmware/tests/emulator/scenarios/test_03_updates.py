import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    BOTH_SLOTS_REQUEST,
    EchoImageHarness,
    main,
    CHUNK_SECTORS,
    IDENTITY,
    SLOT_SECTORS,
    SPIN_POLLS_PER_REST,
    WAIT_REST_TOLERANCE_MS,
    struct,
    summary,
)


class EchoImageTest(EchoImageHarness):
    def test_with_the_developer_request_a_newer_image_fills_slot_2_first_then_slot_1(self):
        newer = self.newer_image()
        session = self.session(files=[(IDENTITY.update_file, newer, None),
                                      (BOTH_SLOTS_REQUEST, b"BOTH SLOTS\n", None)], vbus=True)
        session.machine.note("updater_install")
        session.machine.note("updater_install_trial")
        self.assertEqual(session.run_to_first_frame(), "condition met")
        session.machine.stop_at("board_wait_for_power_hold")
        self.assertEqual(session.run_until_waiting_for_key(presses=1),
                         "reached board_wait_for_power_hold")
        machine = session.machine
        self.assert_healthy(session)
        self.assertEqual(machine.reached["updater_install"], 1)
        self.assertNotIn("updater_install_trial", machine.reached)
        self.assertGreaterEqual(machine.usb_connect_count, 1)
        self.assertFalse(session.file_exists(BOTH_SLOTS_REQUEST))
        self.assertIsNone(self.trial_record(session))
        slots = {name: session.layout[name] for name in ("slot_1", "slot_2")}
        inside = {name: range(start, start + SLOT_SECTORS) for name, start in slots.items()}
        volume_start = session.layout["volume_start"]
        writes = [item for item in session.card.writes if item[1] < volume_start]
        for _, lba, count in writes:
            self.assertEqual(count, CHUNK_SECTORS)
            self.assertTrue(any(lba in span and lba + count <= span.stop
                                for span in inside.values()), hex(lba))
        for lba in session.changed_sectors():
            self.assertTrue(lba >= volume_start or any(lba in span for span in inside.values()),
                            hex(lba))
        by_slot = {name: [item for item in writes if item[1] in span]
                   for name, span in inside.items()}
        self.assertEqual(len(writes), len(by_slot["slot_1"]) + len(by_slot["slot_2"]))
        self.assertLess(max(stamp for stamp, _, _ in by_slot["slot_2"]),
                        min(stamp for stamp, _, _ in by_slot["slot_1"]))
        header_word = struct.unpack_from("<I", newer)[0]
        writes_in_order = [(lba, count, word) for kind, lba, count, *rest in session.card.accesses
                           if kind == "W" and lba < volume_start for word in rest]
        for name, start in slots.items():
            sequence = [item for item in writes_in_order if item[0] in inside[name]]
            self.assertEqual(sequence[0], (start, CHUNK_SECTORS, 0xFFFFFFFF))
            self.assertEqual(sequence[-1], (start, CHUNK_SECTORS, header_word))
            self.assertNotIn(start, [lba for lba, _, _ in sequence[1:-1]])
        self.assert_every_write_read_back(
            [item for item in session.card.accesses if item[0] != "W" or item[1] < volume_start])
        self.assertEqual(session.slot_image("slot_1"), newer[:self.session_module.SLOT_BYTES])
        self.assertEqual(session.slot_image("slot_2"), newer[:self.session_module.SLOT_BYTES])
        summary("update", session)
        installed = self.session(image=session.slot_image("slot_1") + newer[-4:])
        self.assertEqual(installed.run_to_first_frame(), "condition met")
        self.assertEqual(len(installed.panel.lit_sweeps()), 1)
        self.assert_healthy(installed)

    def test_the_running_image_offered_again_is_not_written(self):
        session = self.session(files=[(IDENTITY.update_file, self.image, None)], vbus=True)
        session.machine.note("updater_install_trial")
        session.machine.note("updater_check")
        session.run_to_first_frame()
        self.assertEqual(session.run_through_update_check(),
                         "reached usb_session_storage_changed")
        self.assertEqual(session.machine.reached["updater_check"], 1)
        self.assertNotIn("updater_install_trial", session.machine.reached)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_an_echo_pod_image_is_refused_and_nothing_is_written(self):
        mini = self.session_module.with_brand(self.image, "EchoPod")
        self.assert_update_refused(("ECHOV497.IMG", mini, None))

    def test_a_podium_image_under_our_file_name_is_refused_and_nothing_is_written(self):
        podium = self.session_module.with_brand(self.image, "PODIUM")
        self.assert_update_refused((IDENTITY.update_file, podium, None))

    def test_an_image_with_a_former_brand_under_our_file_name_is_refused_and_nothing_is_written(self):
        for brand in IDENTITY.former_ids:
            former = self.session_module.with_brand(self.image, brand)
            self.assert_update_refused((IDENTITY.update_file, former, None))

    def test_a_mini_stock_size_image_is_refused_and_nothing_is_written(self):
        self.assert_update_refused(("ECHOV496.IMG", None, self.session_module.STOCK_SIZE))

    def test_a_key_wait_that_spins_ends_like_one_that_rests(self):
        rested, rested_polls = self.refuse_an_update(True)
        spun, spun_polls = self.refuse_an_update(False)
        self.assertGreater(len(rested_polls), 0)
        self.assertGreater(len(spun_polls), SPIN_POLLS_PER_REST * len(rested_polls))
        self.assertEqual(rested.card.accesses, spun.card.accesses)
        self.assertEqual(rested.machine.reached, spun.machine.reached)
        self.assertEqual(rested.changed_sectors(), spun.changed_sectors())
        self.assertEqual(rested.machine.gpio_log[-1][1:6], spun.machine.gpio_log[-1][1:6])
        self.assertLessEqual(abs(rested.machine.milliseconds() - spun.machine.milliseconds()),
                             WAIT_REST_TOLERANCE_MS)
        summary("spinning key wait", spun)

    def test_an_update_file_is_installed_once_and_is_gone_afterwards(self):
        newer = self.newer_image()
        first = self.session(files=[(IDENTITY.update_file, newer, None)], vbus=True)
        cluster = first.volume.first_cluster(IDENTITY.update_file)
        self.assertEqual(self.boot_until_waiting_for_key(first),
                         "reached board_wait_for_power_hold")
        self.assertEqual(first.machine.reached["updater_install_trial"], 1)
        self.assertFalse(first.file_exists(IDENTITY.update_file))
        self.assertEqual(first.fat_entry(cluster), 0)
        self.assertEqual(first.slot_image("slot_1"), newer[:self.session_module.SLOT_BYTES])
        second = first.reboot(image=first.slot_image("slot_1") + newer[-4:],
                              vbus_after_first_frame=True)
        self.assertEqual(self.boot_until_waiting_for_key(second),
                         "reached usb_session_storage_changed")
        self.assertNotIn("updater_install_trial", second.machine.reached)
        self.assertNotIn("updater_check", second.machine.reached)
        self.assertEqual([w for w in second.card.writes if w[1] < second.layout["volume_start"]], [])
        self.assert_healthy(second)

    def test_restoring_an_older_build_does_not_reinstall_the_removed_update(self):
        newer = self.newer_image()
        first = self.session(files=[(IDENTITY.update_file, newer, None)], vbus=True)
        self.boot_until_waiting_for_key(first)
        self.assertEqual(first.machine.reached["updater_install_trial"], 1)
        first.write_slots(self.image)
        again = first.reboot(image=self.image, vbus_after_first_frame=True)
        self.boot_until_waiting_for_key(again)
        self.assertNotIn("updater_install_trial", again.machine.reached)
        self.assertEqual(again.slot_image("slot_1"), self.image[:self.session_module.SLOT_BYTES])
        self.assert_healthy(again)

    def test_an_update_file_that_is_refused_or_already_installed_is_removed(self):
        mini = self.session_module.with_brand(self.image, "EchoPod")
        for name, data in (("ECHOV497.IMG", mini), (IDENTITY.update_file, self.image)):
            session = self.session(files=[(name, data, None)], vbus=True)
            self.boot_until_waiting_for_key(session)
            self.assertNotIn("updater_install_trial", session.machine.reached)
            self.assertFalse(session.file_exists(name), name)
            self.assert_only_the_user_volume_changed(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
