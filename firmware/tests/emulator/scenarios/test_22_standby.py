import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    FNV_PRIME,
    FNV_START,
    IDENTITY,
    STARTUP_WORK_MS,
    main,
    struct,
)


class EchoImageTest(EchoImageHarness):
    def test_standby_keeps_time_and_the_watchdog_alive_and_power_wakes(self):
        from unicorn.arm_const import UC_ARM_REG_R0

        settings = bytearray(self.saved_settings(4, 15, shuffle=0))
        settings[4], settings[53], settings[58] = 16, 16, 6
        for first, end, target in ((0, 54, 54), (58, 74, 74)):
            checksum = FNV_START
            for value in settings[first:end]:
                checksum = (checksum ^ value) * FNV_PRIME & 0xFFFFFFFF
            settings[target:target + 4] = struct.pack("<I", checksum)
        volume = self.debug_volume()
        volume.add_file("settings.bin", bytes(settings), directory=IDENTITY.data_directory)
        session = self.session(volume=volume)
        machine = session.machine
        states = []

        def following():
            address = machine.reg(UC_ARM_REG_R0)
            states.append(machine.read_memory(address, 1)[0])

        machine.hook_symbol("extension_standby_follow", following)
        session.run_to_home_screen()
        machine.run(until_ms=machine.milliseconds() + STARTUP_WORK_MS)
        machine.power_key_released_at_ms = machine.milliseconds() + 150
        self.assertEqual(machine.run(until=lambda: states and states[-1] == 1 and
                                     machine.core_hz == 24000000,
                                     until_ms=machine.milliseconds() + 2000), "condition met")
        self.assertEqual(machine.core_hz, 24000000)
        kicks = machine.watchdog_kicks
        start = machine.milliseconds()
        machine.run(until_ms=start + 3000)
        self.assertGreaterEqual(machine.milliseconds(), start + 3000)
        self.assertGreater(machine.watchdog_kicks, kicks)
        machine.power_key_released_at_ms = machine.milliseconds() + 150
        self.assertEqual(machine.run(until=lambda: states[-1] == 0 and machine.core_hz > 24000000,
                                     until_ms=machine.milliseconds() + 2000), "condition met")
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_healthy(session)
        self.assert_only_the_user_volume_changed(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
