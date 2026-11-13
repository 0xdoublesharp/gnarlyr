import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main, IDENTITY
from echo_emulator.emmc import SdCard, SparseDisk
from echo_emulator.fat_volume import Fat32Volume
from echo_emulator.playback import Rig
from echo_emulator.bluetooth import Headphones
from echo_emulator.power import DecoderLoad, PowerMeter


class EchoImageTest(EchoImageHarness):
    def test_a_soak_saves_minute_power_samples_in_the_preallocated_trace(self):
        text = "build %s\nsupply off\nscreen off\nsoak 3\nreconnect\n" % self.build_tag()
        volume = self.debug_volume(run_txt=text.encode("ascii"), battery_bin=bytes(49152))
        session, machine = self.ejecting_session(volume)
        PowerMeter(machine)
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.run_until_supply(machine, (0, 0), 5000), "condition met")
        machine.run(until_ms=machine.milliseconds() + 125000)
        self.assertFalse(machine.faults, (machine.faults, machine.symbol_at(machine.current_pc())))
        import battery
        data = session.fat().read("/" + IDENTITY.data_directory + "/debug/battery.bin")
        result = battery.analyse(data)
        self.assertGreaterEqual(len(result["samples"]), 2, result)
        self.assertEqual(24, result["samples"][0]["core0_clock_mhz"])
        self.assertFalse(result["samples"][0]["usb_powered"])
        self.assert_healthy(session)

    def test_a_supply_off_soak_keeps_normal_low_battery_shutdown(self):
        session, machine = self.soaking_session(["supply off", "screen off", "soak 180"])
        PowerMeter(machine)
        machine.note("debug_report_write")
        machine.adc_battery_value = 700
        reason = machine.run(until=lambda: machine.reached.get("debug_report_write", 0) > 0,
                             until_ms=machine.milliseconds() + 20000)
        self.assertEqual("condition met", reason)
        machine.run(until_ms=machine.milliseconds() + 2000)
        self.assertEqual("low_battery", self.read_report(session)["values"]["report.reason"])
        self.assertEqual((1, 1), self.usb_supply_pins(machine))
        self.assert_healthy(session)

    def test_screen_off_sleep_reduces_core_0_timer_wakeups(self):
        rig = self.speech_rig()
        meter = PowerMeter(rig.machine)
        rig.open_songs()
        rig.play_song(1)
        rig.machine.power_key_released_at_ms = rig.machine.milliseconds() + 150
        rig.run_for(1000)
        result = meter.measure(rig, rig.session.card, 1500)
        self.assertLessEqual(result["core0_wfi_wakes_per_s"], 210, result)
        self.assertEqual(0, result["underruns"], result)
        self.assert_healthy(rig.session)

    def battery_case(self, codec, output):
        volume = Fat32Volume()
        rate, channels = (16000, 1) if codec == "speech" else (44100, 2)
        extension = "wav" if codec == "speech" else codec
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / ("Load." + extension)
            command = ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                       "anoisesrc=color=pink:seed=1:sample_rate=" + str(rate),
                       "-t", "30", "-ac", str(channels)]
            if codec == "mp3":
                command += ["-b:a", "320k"]
            elif codec == "flac":
                command += ["-sample_fmt", "s16"]
            elif codec == "ogg":
                command += ["-c:a", "vorbis", "-strict", "-2"]
            subprocess.run(command + [str(path)], check=True)
            volume.add_file(path.name, path.read_bytes())
        if output == 0:
            volume.add_directory(IDENTITY.data_directory)
            record = b"BT\x01\x01\x01" + bytes(range(1, 7)) + b"Radio" + bytes(27) + bytes(38 * 5 + 1)
            volume.add_file("bluetooth.bin", record, directory=IDENTITY.data_directory)
        inserted, meters, radios = [], [], []

        def prepare(machine):
            disk = SparseDisk(volume.sector_count)
            volume.write_to(disk, 0)
            card = SdCard(disk, machine)
            inserted.append(card)
            machine.sd_controller.card = card
            machine.card_present = True
            meters.append(PowerMeter(machine))
            if output == 0:
                machine.bluetooth.headphones = Headphones(bytes(range(1, 7)), "Radio")
                radios.append(machine.bluetooth)
            original = machine.external_pin_levels
            def pins(port):
                value = original(port)
                if port == 0 and output == 1:
                    value |= 1 << 13
                if port == 2 and output == 2:
                    value |= 1 << 8
                return value
            machine.external_pin_levels = pins

        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=Fat32Volume(), module_elfs=self.module_elfs, prepare=prepare)
        if codec != "speech":
            DecoderLoad(rig.audio, self.elf.parent)
        rig.open_songs()
        started = rig.play_song(1)
        rig.run_for(500)
        self.assertIsNotNone(rig.first_audio_after(started),
                             (codec, output, rig.audio.plays, rig.machine.faults,
                              rig.machine.panics, getattr(rig.audio.core1, "instructions", 0)))
        rig.machine.power_key_released_at_ms = rig.machine.milliseconds() + 150
        rig.run_for(1000)
        result = meters[0].measure(rig, inserted[0], 4000)
        result.update(codec=codec, output=output)
        if radios:
            result["bluetooth_commands"] = [item.hex() for _, item in radios[0].commands]
            self.assertTrue(radios[0].streaming, result)
        self.assertEqual(0, result["underruns"], result)
        self.assertEqual(0, result["output_underruns"], result)
        self.assertTrue(result["panel_asleep"], result)
        self.assertFalse(result["vop_clock_gated"], result)
        self.assertFalse(result["backlight_on"], result)
        self.assertEqual(0, result["panel_on_ms"], result)
        self.assertEqual(0, result["backlight_on_ms"], result)
        self.assert_healthy(rig.session)
        return result

    def test_battery_playback_matrix_has_no_underruns(self):
        figures = []
        for codec in os.environ.get("ECHO_BATTERY_CODECS", "flac,mp3,speech").split(","):
            for output in (1, 2, 0):
                figures.append(self.battery_case(codec, output))
                destination = os.environ.get("ECHO_BATTERY_REPORT")
                if destination:
                    Path(destination).write_text(json.dumps(figures, indent=2) + "\n")
        print(json.dumps(figures, indent=2))
        destination = os.environ.get("ECHO_BATTERY_REPORT")
        if destination:
            Path(destination).write_text(json.dumps(figures, indent=2) + "\n")


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
