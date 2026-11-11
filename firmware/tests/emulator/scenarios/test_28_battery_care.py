import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main, struct, GPIO_DATA, IDENTITY


class EchoImageTest(EchoImageHarness):
    def test_usb_only_keeps_usb_supply_on_and_restores_charging_for_low_voltage(self):
        from echo_emulator.fat_volume import Fat32Volume
        settings = bytearray(self.saved_settings(4, 5, shuffle=0))
        settings[52] = 16
        checksum = 2166136261
        for value in settings[:54]:
            checksum = (checksum ^ value) * 16777619 & 0xFFFFFFFF
        struct.pack_into("<I", settings, 54, checksum)
        volume = Fat32Volume()
        volume.add_directory(IDENTITY.data_directory)
        volume.add_file("settings.bin", bytes(settings), directory=IDENTITY.data_directory)
        session = self.session(volume=volume)
        machine = session.machine
        session.run_to_home_screen()

        def pins():
            return (bool(machine.gpio[2].regs[GPIO_DATA] & 1),
                    bool(machine.gpio[0].regs[GPIO_DATA] & (1 << 14)))

        from unicorn.arm_const import UC_ARM_REG_R0
        millivolts = [3999]

        def battery_gauge(machine):
            reading = [millivolts[0]] * 8 + [0] * 56 + [8, 8]
            machine.uc.mem_write(machine.reg(UC_ARM_REG_R0), struct.pack("<66I", *reading))

        machine.stub_symbol("board_battery_gauge", battery_gauge)
        machine.vbus = True
        machine.run(until=lambda: pins() == (True, False),
                    until_ms=machine.milliseconds() + 3000)
        self.assertEqual(pins(), (True, False))
        millivolts[0] = 3500
        machine.run(until=lambda: pins() == (True, True),
                    until_ms=machine.milliseconds() + 3000)
        self.assertEqual(pins(), (True, True))
        millivolts[0] = 3999
        machine.run(until=lambda: pins() == (True, False),
                    until_ms=machine.milliseconds() + 3000)
        self.assertEqual(pins(), (True, False))
        machine.vbus = False
        machine.run(until=lambda: pins() == (False, False),
                    until_ms=machine.milliseconds() + 3000)
        self.assertEqual(pins(), (False, False))
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
