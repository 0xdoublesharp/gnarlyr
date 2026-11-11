import struct
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import prove_upgrade
from echo_emulator.machine import Machine
from identity import IDENTITY


def transaction():
    accesses, writes = [], []
    image = struct.pack("<I", 42) + b"new image"
    for start in (40000, 0):
        for lba, word in ((start, 0xFFFFFFFF), (start + 16, 0), (start, 42)):
            writes.append((len(accesses), lba, 16))
            accesses.extend([("W", lba, 16, word), ("R", lba, 16)])
    return SimpleNamespace(layout={"slot_1": 0, "slot_2": 40000, "volume_start": 100000,
                                   "volume_sectors": 100},
                           card=SimpleNamespace(writes=writes, accesses=accesses),
                           changed_sectors=lambda: [0, 40000, 100001],
                           slot_image=lambda name: image), image


class TransactionTests(unittest.TestCase):
    def test_slot_two_then_one_with_header_last_and_readbacks_passes(self):
        session, image = transaction()
        self.assertEqual(prove_upgrade.verify_transaction(session, image), 6)

    def test_missing_readback_is_rejected(self):
        session, image = transaction()
        del session.card.accesses[1]
        with self.assertRaisesRegex(ValueError, "read back"):
            prove_upgrade.verify_transaction(session, image)

    def test_slot_one_first_is_rejected(self):
        session, image = transaction()
        session.card.writes = [(20 - stamp, lba, count) for stamp, lba, count in session.card.writes]
        with self.assertRaisesRegex(ValueError, "Slot 2"):
            prove_upgrade.verify_transaction(session, image)

    def test_early_header_commit_is_rejected(self):
        session, image = transaction()
        session.card.accesses[0] = ("W", 40000, 16, 42)
        with self.assertRaisesRegex(ValueError, "invalidated"):
            prove_upgrade.verify_transaction(session, image)

    def test_write_outside_slots_and_user_volume_is_rejected(self):
        session, image = transaction()
        session.card.writes.append((99, 80000, 16))
        with self.assertRaisesRegex(ValueError, "outside"):
            prove_upgrade.verify_transaction(session, image)

    def test_changed_sector_past_user_volume_is_rejected(self):
        session, image = transaction()
        session.changed_sectors = lambda: [100101]
        with self.assertRaisesRegex(ValueError, "outside"):
            prove_upgrade.verify_transaction(session, image)

    def test_wrong_installed_bytes_are_rejected(self):
        session, image = transaction()
        session.slot_image = lambda name: b"wrong"
        with self.assertRaisesRegex(ValueError, "exact new image"):
            prove_upgrade.verify_transaction(session, image)


class HookTests(unittest.TestCase):
    def test_missing_symbol_is_strict_by_default_and_optional_only_when_asked(self):
        machine = SimpleNamespace(symbols={}, overlays=[])
        with self.assertRaises(KeyError):
            Machine.hook_symbol(machine, "older_image", lambda: None)
        Machine.hook_symbol(machine, "older_image", lambda: None, optional=True)

    def test_optional_symbol_still_installs_a_hook_when_present(self):
        machine = SimpleNamespace(symbols={"present": 20}, overlays=[], add_address_hook=mock.Mock())
        callback = lambda: None
        Machine.hook_symbol(machine, "present", callback, optional=True)
        machine.add_address_hook.assert_called_once_with(20, callback)

    def test_candidates_get_their_own_symbols_and_only_old_hooks_are_optional(self):
        directory = mock.MagicMock()
        modules = [Path("platform/device/echo_module_views.elf")]
        with mock.patch("echo_emulator.session.Session") as session, \
                mock.patch.object(prove_upgrade.image_artifacts, "module_elfs", return_value=modules) as listed:
            with mock.patch.object(prove_upgrade.make_image, "validate_image", return_value=[]):
                with mock.patch.object(prove_upgrade.make_image, "firmware_from_elf") as parts:
                    parts.return_value = SimpleNamespace(code=b"", data=b"")
                    with mock.patch.object(prove_upgrade, "require"):
                        prove_upgrade.candidate_session(directory, historical=True)
                        self.assertEqual(session.call_args.kwargs["optional_hooks"], ("hal_system_set_usb_answer",))
                        prove_upgrade.candidate_session(directory)
                        self.assertEqual(session.call_args.kwargs["optional_hooks"], ())
        listed.assert_called_with(directory)
        self.assertEqual(session.call_args.kwargs["module_elfs"], modules)


class BuiltImageTests(unittest.TestCase):
    def test_exact_release_to_device_upgrade_uses_separate_symbol_sets(self):
        project = Path(__file__).resolve().parents[2]
        old, new = (project / "build" / preset for preset in ("release", "device"))
        if not all((directory / IDENTITY.update_file).is_file() for directory in (old, new)):
            self.skipTest("Build release and device images through firmware.py sizes first")
        result = prove_upgrade.prove(old, new)
        self.assertTrue(result["ok"])
        self.assertTrue(result["new_image_booted"])


if __name__ == "__main__":
    unittest.main()
