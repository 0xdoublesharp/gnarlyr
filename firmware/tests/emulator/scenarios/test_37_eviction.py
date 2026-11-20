import os
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, IDENTITY, main, settings_file

STRESS_SEED = 20261119
SHORT_ACTIONS = 20
MIN_SWAPS = 3
LONG_ACTIONS = 300
LONG_PRESS_FIELD = "b59"
SETTINGS = 2
UP_NEXT_SHORTCUT = 2
HOLD_MS = 1200
USB_MS = (1200, 3000)
WAIT_MS = (40, 1600)
HOLD_MENU_ISSUE = "hold-menu-stale-update"
SHORTCUTS = (1, 2, 3, 4, 5)
TAPS = ("play", "next", "previous", "back", "menu")


def known_issue_open(name):
    closed = os.environ.get("ECHO_KNOWN_ISSUES", "").split(",")
    return name not in closed and "all" not in closed


def stress_runs():
    try:
        return int(os.environ.get("ECHO_EVICTION_STRESS_RUNS", "0"))
    except ValueError:
        return 0


class EchoImageTest(EchoImageHarness):
    def eviction_rig(self, long_press=0):
        from echo_emulator.modules import SlotOwnership
        from echo_emulator.playback import Rig
        from echo_emulator.real_volume import real_volume
        volume = real_volume(playable=True)
        volume.add_directory(IDENTITY.data_directory)
        volume.add_file("settings.bin", settings_file(0, 4096, **{LONG_PRESS_FIELD: long_press}),
                        directory=IDENTITY.data_directory)
        holder = {}
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf, volume=volume,
                  module_elfs=self.module_elfs,
                  prepare=lambda machine: holder.setdefault("watch", SlotOwnership(machine)))
        return rig, holder["watch"]

    def test_slot_ownership_faults_on_a_planted_call_into_an_evicted_module(self):
        from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R3
        rig, watch = self.eviction_rig()
        blx_r3 = self.resident_blx(rig.machine, 3)
        machine = rig.machine
        rig.press("previous")
        rig.press("play")
        self.assertIn(SETTINGS, [number for number, (functions, _) in watch.layouts.items()])
        self.assertIsNotNone(watch.owner)
        self.assertEqual([], watch.faults)
        owner = watch.owner
        others = [number for number in watch.layouts if number != owner]
        target = min(address for address in watch.taken
                     if address not in watch.layouts[owner][0]
                     and any(address in watch.layouts[number][0] for number in others))
        planted = []

        def plant():
            if not planted and watch.owner == owner:
                planted.append(machine.milliseconds())
                machine.uc.reg_write(UC_ARM_REG_R3, target | 1)
                machine.uc.reg_write(UC_ARM_REG_LR, blx_r3 + 2 | 1)
                machine.uc.reg_write(UC_ARM_REG_PC, target | 1)

        machine.hook_symbol("app_step", plant)
        machine.run(until=lambda: watch.faults, until_ms=machine.milliseconds() + 500)
        self.assertEqual(1, len(planted))
        self.assertEqual(1, len(watch.faults))
        self.assertIn("evicted", watch.faults[0])
        self.assertIn("pc %#x" % target, watch.faults[0])

    def resident_blx(self, machine, register):
        import make_image
        code = machine.read_memory(make_image.CODE_BASE, 0x20000)
        pattern = (0x4780 | register << 3).to_bytes(2, "little")
        offset = code.find(pattern)
        while offset % 2:
            offset = code.find(pattern, offset + 1)
        self.assertGreaterEqual(offset, 0)
        return make_image.CODE_BASE + offset

    def test_a_hold_menu_shortcut_from_now_playing_calls_no_evicted_module_code(self):
        rig, watch = self.eviction_rig(long_press=UP_NEXT_SHORTCUT)
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(600)
        self.assertEqual([], watch.faults)
        rig.press("menu", hold=HOLD_MS)
        rig.run_for(600)
        if known_issue_open(HOLD_MENU_ISSUE):
            print("known issue %s still reproduces:\n%s" % (HOLD_MENU_ISSUE,
                                                           "\n".join(watch.faults)))
            return
        self.assertEqual([], watch.faults, "\n".join(watch.faults))

    def stress_action(self, rig, watch, rng):
        machine = rig.machine
        choice = rng.randrange(10)
        if choice < 4:
            rig.press(rng.choice(TAPS))
        elif choice == 4:
            if not known_issue_open(HOLD_MENU_ISSUE) or watch.owner in (None, SETTINGS):
                rig.press("menu", hold=HOLD_MS)
        elif choice == 5:
            rig.press("back", hold=HOLD_MS)
        elif choice == 6:
            rig.skip(rng.choice(("next", "previous")))
        elif choice == 7:
            machine.vbus = not machine.vbus
            rig.run_for(rng.randrange(*USB_MS))
        else:
            machine.press_key(rig_key(rng), delay_ms=10, hold_ms=rng.randrange(40, 200))
            rig.run_for(rng.randrange(*WAIT_MS))

    def stress(self, seed, actions):
        rng = random.Random(seed)
        rig, watch = self.eviction_rig(long_press=rng.choice(SHORTCUTS))
        rig.open_songs()
        rig.play_song(1 + rng.randrange(3))
        done = 0
        for done in range(actions):
            self.stress_action(rig, watch, rng)
            if watch.faults or rig.machine.faults:
                break
        rig.machine.vbus = False
        rig.run_for(800)
        print("eviction stress seed %d: %d actions, %s" % (seed, done + 1, watch.counts), flush=True)
        detail = "\n".join(watch.faults + [str(fault) for fault in rig.machine.faults])
        self.assertEqual([], watch.faults + rig.machine.faults,
                         "eviction stress seed %d failed at action %d:\n%s" % (seed, done, detail))
        return watch

    def test_eviction_stress_with_a_fixed_seed_touches_no_evicted_module(self):
        watch = self.stress(STRESS_SEED, SHORT_ACTIONS)
        self.assertGreaterEqual(watch.counts["loads"], MIN_SWAPS)
        self.assertGreaterEqual(watch.counts["overlays"], 1)

    def test_eviction_stress_with_new_seeds_touches_no_evicted_module(self):
        runs = stress_runs()
        if runs <= 0:
            self.skipTest("set ECHO_EVICTION_STRESS_RUNS to run the long randomised stress")
        base = int(os.environ.get("ECHO_EVICTION_STRESS_SEED", str(random.SystemRandom().randrange(1 << 30))))
        for run in range(runs):
            seed = base + run
            print("eviction stress seed %d" % seed, flush=True)
            self.stress(seed, LONG_ACTIONS)


def rig_key(rng):
    from echo_emulator.playback import KEYS
    return KEYS[rng.choice(TAPS)]


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
