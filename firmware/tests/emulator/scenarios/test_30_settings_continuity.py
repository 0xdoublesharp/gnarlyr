import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    IDENTITY,
    TRIAL_RUNNING,
    fnv1a,
    make_image,
    seal_settings,
    struct,
    summary,
)

FOLDER = IDENTITY.data_directory
TAG_BYTES = make_image.TEXT_FIELD_SIZE
MARK_BYTES = 24
BUILD_BYTES = 16
TRIAL_CHECK = 0x5A


def build_of(image):
    tag = image[make_image.BUILD_TAG_OFFSET:make_image.BUILD_TAG_OFFSET + TAG_BYTES]
    printable = bytes(b for b in tag.split(b"\0")[0] if 0x20 <= b < 0x7F).decode("ascii")
    return f"{IDENTITY.version}-{printable}" if printable else IDENTITY.version


def with_tag(image, tag):
    from echo_emulator.session import repack
    field = make_image.text_field(tag, "build tag")

    def edit(data, base):
        at = base + make_image.BUILD_TAG_OFFSET
        data[at:at + TAG_BYTES] = field

    return repack(image, edit)


def mark(build, request=0):
    name = build.encode("ascii")[:BUILD_BYTES]
    return struct.pack("<I", fnv1a(name)) + bytes((1, request, 0, 0)) + name.ljust(BUILD_BYTES, b"\0")


def backup_list(builds, newest):
    body = b"DSBU" + bytes((1, newest, 0, 0)) + b"".join(
        build.encode("ascii").ljust(BUILD_BYTES, b"\0") for build in builds)
    return body + struct.pack("<I", fnv1a(body))


def running_trial():
    record = bytearray(512)
    record[:8] = b"DSUT" + bytes((TRIAL_RUNNING, 0, TRIAL_RUNNING ^ TRIAL_CHECK, 1))
    return bytes(record)


class EchoImageTest(EchoImageHarness):
    def player_volume(self, files, folders=()):
        from echo_emulator.fat_volume import Fat32Volume
        volume = Fat32Volume()
        volume.add_directory(FOLDER)
        for folder in folders:
            parent, _, name = folder.rpartition("/")
            volume.add_directory(name, directory="/".join(part for part in (FOLDER, parent) if part))
        for path, data in files:
            parent, _, name = path.rpartition("/")
            volume.add_file(name, data, directory="/".join(part for part in (FOLDER, parent) if part))
        return volume

    def read(self, session, path):
        return session.fat().read(f"{FOLDER}/{path}")

    def custom_settings(self, version=1):
        settings = bytearray(self.saved_settings(0, 15, 1))
        settings[4] = version
        return seal_settings(settings)

    def assert_list(self, session, builds, newest):
        data = self.read(session, "backup/list.bin")
        self.assertEqual(backup_list(builds, newest), data[:len(backup_list(builds, newest))])

    def test_an_update_backs_up_what_the_old_build_left_before_the_new_build_reads_it(self):
        old = build_of(self.image)
        newer = with_tag(self.newer_image(), "c0ffee01")
        settings = self.custom_settings()
        volume = self.player_volume([("settings.bin", settings), ("continuity.bin", mark(old))])
        volume.add_file(IDENTITY.update_file, newer)
        session = self.session(volume=volume, vbus=True)
        self.assertEqual(self.boot_until_waiting_for_key(session), "reached board_wait_for_power_hold")
        self.assertEqual(session.machine.reached["trial_module_install"], 1)
        self.assertIsNone(self.read(session, "backup/list.bin"))
        trial = session.reboot(image=newer)
        trial.machine.note("user_state_module_settle")
        self.assertEqual(trial.run_to_first_frame(), "condition met")
        self.assert_healthy(trial)
        self.assertEqual(trial.machine.reached["user_state_module_settle"], 1)
        self.assertEqual(self.trial_record(trial), (TRIAL_RUNNING, 1))
        self.assertEqual(self.read(trial, "backup/1/settings.bin"), settings)
        self.assertEqual(self.read(trial, "settings.bin"), settings)
        self.assert_list(trial, [old, ""], 0)
        self.assertEqual(self.read(trial, "continuity.bin"), mark(build_of(newer)))
        again = trial.reboot(image=newer)
        again.machine.note("user_state_module_settle")
        self.assertEqual(again.run_to_first_frame(), "condition met")
        self.assertEqual(again.machine.reached["user_state_module_settle"], 1)
        self.assert_list(again, [old, ""], 0)
        summary("backup on update", trial)

    def test_an_older_build_sets_a_newer_settings_file_aside_and_starts_on_defaults(self):
        newer_settings = self.custom_settings(version=2)
        volume = self.player_volume([("settings.bin", newer_settings),
                                     ("continuity.bin", mark("9.9.9-future01")),
                                     ("trial.bin", running_trial())])
        session = self.session(volume=volume)
        session.machine.note("user_state_module_settle")
        self.assertEqual(session.run_to_first_frame(), "condition met")
        self.assert_healthy(session)
        self.assertEqual(session.machine.reached["user_state_module_settle"], 1)
        self.assertEqual(self.read(session, "newer/settings.bin"), newer_settings)
        self.assertEqual(self.read(session, "backup/1/settings.bin"), newer_settings)
        self.assert_list(session, ["9.9.9-future01", ""], 0)
        current = self.read(session, "settings.bin")
        self.assertTrue(current is None or current[4] == 1)
        summary("newer settings set aside", session)

    def test_a_restore_request_puts_the_backup_back_before_the_settings_load(self):
        saved = self.custom_settings()
        volume = self.player_volume(
            [("settings.bin", self.saved_settings(0, 20, 0)), ("backup/1/settings.bin", saved),
             ("backup/list.bin", backup_list(["1.0.0-aaaa1111", ""], 0)),
             ("continuity.bin", mark(build_of(self.image), 1)), ("trial.bin", running_trial())],
            folders=["backup", "backup/1"])
        session = self.session(volume=volume)
        session.machine.note("user_state_module_settle")
        self.assertEqual(session.run_to_first_frame(), "condition met")
        self.assert_healthy(session)
        self.assertEqual(self.read(session, "settings.bin"), saved)
        self.assertEqual(self.read(session, "continuity.bin"), mark(build_of(self.image)))
        self.assertIsNone(self.read(session, "backup/2/settings.bin"))
        summary("restore request", session)


if __name__ == "__main__":
    main(EchoImageTest)
