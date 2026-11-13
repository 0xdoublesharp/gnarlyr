import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main

STEPS = ["measure 1500", "play", "measure 700", "play", "measure 700", "back", "measure 700",
         "screenshot home", "reconnect"]
PARTS = ("covers", "reflections", "text", "backdrop", "hash", "audio")


class EchoImageTest(EchoImageHarness):
    def test_builtin_ascii_glyph_lookup_has_a_bounded_instruction_cost(self):
        from unicorn import Uc, UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB, UC_HOOK_CODE
        from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1,
                                       UC_ARM_REG_PC, UC_ARM_REG_SP, UC_CPU_ARM_CORTEX_M3)
        from echo_emulator.machine import load_symbols
        import make_image
        symbols = load_symbols(self.elf)
        core = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        core.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M3)
        core.mem_map(0x03000000, 0x100000)
        core.mem_write(make_image.CODE_BASE,
                       make_image.elf_sections(Path(self.elf).read_bytes())[".code"].content)
        instructions = [0]

        def counted(uc, address, size, context):
            instructions[0] += 1

        core.hook_add(UC_HOOK_CODE, counted)
        returned = 0x0300F000
        for codepoint in range(32, 127):
            core.reg_write(UC_ARM_REG_SP, 0x030FE000)
            core.reg_write(UC_ARM_REG_R0, symbols["FONT_INTER_REGULAR_16"])
            core.reg_write(UC_ARM_REG_R1, codepoint)
            core.reg_write(UC_ARM_REG_LR, returned | 1)
            core.emu_start(symbols["font_find_glyph"] | 1, returned, count=10000)
            self.assertEqual(core.reg_read(UC_ARM_REG_PC), returned)
            glyph = core.reg_read(UC_ARM_REG_R0)
            self.assertEqual(int.from_bytes(core.mem_read(glyph, 2), "little"), codepoint)
        print("%d instructions for 95 resident ASCII glyph lookups" % instructions[0])
        self.assertLessEqual(instructions[0], 95 * 40)

    def trace_fills(self, machine):
        import struct
        from unicorn.arm_const import (UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1,
                                       UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP)
        fills = {}

        def entered():
            if machine.reached.get("debug_script_take", 0) < 1:
                return
            canvas = machine.reg(UC_ARM_REG_R0)
            clip = struct.unpack("<4i", machine.read_memory(canvas + 20, 16))
            x, y, w = (machine.reg(UC_ARM_REG_R1), machine.reg(UC_ARM_REG_R2),
                       machine.reg(UC_ARM_REG_R3))
            x, y, w = [value - (1 << 32) if value >= 1 << 31 else value for value in (x, y, w)]
            h = struct.unpack("<i", machine.read_memory(machine.reg(UC_ARM_REG_SP), 4))[0]
            left, top = max(x, clip[0]), max(y, clip[1])
            right, bottom = min(x + w, clip[0] + clip[2]), min(y + h, clip[1] + clip[3])
            area = max(0, right - left) * max(0, bottom - top)
            key = "%08x" % (machine.reg(UC_ARM_REG_LR) & ~1)
            calls, pixels = fills.get(key, (0, 0))
            fills[key] = (calls + 1, pixels + area)

        machine.hook_symbol("canvas_fill_rect", entered)
        return fills

    def sample_functions(self, machine):
        import bisect
        import make_image
        from echo_emulator.scan_profile import function_names
        ordered = sorted(function_names(self.elf, make_image.CODE_BASE, 0x04000000).items())
        addresses, names = [item[0] for item in ordered], [item[1] for item in ordered]
        samples, original = {}, machine.step_slice
        started = machine.reached

        def step():
            original()
            if started.get("debug_script_take", 0) < 1:
                return
            pc = machine.current_pc()
            index = bisect.bisect_right(addresses, pc) - 1
            name = names[index] if index >= 0 and pc < 0x030766C0 else "(module or data)"
            samples[name] = samples.get(name, 0) + 1
            if name in watched:
                from unicorn.arm_const import UC_ARM_REG_LR
                back = machine.reg(UC_ARM_REG_LR) & ~1
                caller = bisect.bisect_right(addresses, back) - 1
                key = name + " <- " + (names[caller] if caller >= 0 and back < 0x030766C0
                                       else "%08x" % back)
                samples[key] = samples.get(key, 0) + 1

        watched = set(filter(None, os.environ.get("ECHO_FRAME_CALLERS", "").split(",")))
        machine.step_slice = step
        return samples

    def test_frame_cost_of_a_resting_home_a_slide_and_a_moving_selection(self):
        from echo_emulator.playback import ALBUM_COUNT, Rig
        from echo_emulator.real_volume import real_volume
        script = "\n".join(["build " + self.build_tag(), "back:long", "wait 1500"] + STEPS + [""])
        volume = self.debug_volume(real_volume(playable=True, album_count=ALBUM_COUNT),
                                   run_txt=script.encode("ascii"))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf, volume=volume,
                  module_elfs=self.module_elfs)
        session, machine = rig.session, rig.machine
        for name in ("usb_session_storage_changed", "debug_script_take", "hal_system_usb_reconnect"):
            machine.note(name)
        samples = self.sample_functions(machine) if os.environ.get("ECHO_FRAME_PROFILE") else None
        fills = self.trace_fills(machine) if os.environ.get("ECHO_FILL_TRACE") else None
        machine.host_ejects()
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        report = self.read_report(session)
        ran = report["script"]
        self.assertEqual(report["values"]["script.state"], "finished")
        figures = {
            "measures": [{key: measure[key] for key in ("line", "ms", "frames", "frame_avg_us",
                                                        "draw_avg_us", "send_avg_us",
                                                        "update_avg_us")}
                         for measure in ran["measures"]],
            "parts_us": {part: report["values"].get("frame.part.%s_us" % part)
                         for part in PARTS},
            "dma_pixels": getattr(getattr(machine, "dmac", None), "pushed_pixels", None),
            "fills": sorted(fills.items(), key=lambda item: -item[1][1])[:30]
            if fills is not None else None,
            "top_functions": sorted(samples.items(), key=lambda item: -item[1])[:80]
            if samples is not None else None,
        }
        print(json.dumps(figures, indent=2))
        destination = os.environ.get("ECHO_FRAME_REPORT")
        if destination:
            Path(destination).write_text(json.dumps(figures, indent=2) + "\n")
        self.assertEqual(len(figures["measures"]), 4)
        for measure in figures["measures"][1:]:
            self.assertGreater(measure["frames"], 0)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
