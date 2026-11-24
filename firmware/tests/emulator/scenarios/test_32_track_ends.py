import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    PROJECT,
    EchoImageHarness,
    main,
    struct,
)

PATTERN = (0, 8485, 12000, 8485, 0, -8485, -12000, -8485)
LOUD_TENTHS = 5
PLAY_LIMIT_MS = 16000
QUIET_CHECK_MS = 500
OUTPUT_STARTS = 6
PLAYED_FRAMES = 176000
OPENING_FRAMES = 2048
MP3_PEAK = 3000


def wav_pcm(rate, channels):
    frames = rate * LOUD_TENTHS // 10
    samples = []
    for frame in range(frames):
        value = PATTERN[frame % len(PATTERN)]
        samples.extend((value, -value)[:channels])
    return samples


def wav_file(rate, channels):
    pcm = struct.pack("<%dh" % (rate * LOUD_TENTHS // 10 * channels), *wav_pcm(rate, channels))
    return struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ", 16, 1,
                       channels, rate, rate * 2 * channels, 2 * channels, 16, b"data",
                       len(pcm)) + pcm


def aiff_file(rate):
    samples = wav_pcm(rate, 2)
    pcm = struct.pack(">%dh" % len(samples), *samples)
    exponent = 16383 + rate.bit_length() - 1
    mantissa = rate << (16414 - exponent)
    common = struct.pack(">hIhHIi", 2, len(samples) // 2, 16, exponent, mantissa, 0)
    sound = struct.pack(">II", 0, 0) + pcm
    body = (b"AIFF" + b"COMM" + struct.pack(">I", len(common)) + common
            + b"SSND" + struct.pack(">I", len(sound)) + sound)
    return b"FORM" + struct.pack(">I", len(body)) + body


class PlayedOutput:
    def __init__(self):
        from echo_emulator.audio import RING_BYTES
        self.slots = RING_BYTES // 8
        self.audio = None
        self.last = 0
        self.played = 0
        self.stale = []
        self.written = {}
        self.starts = 0
        self.openings = []

    def start(self, machine):
        self.flush(machine)
        self.last = 0
        self.starts += 1
        self.openings.append([])

    def attach(self, rig):
        from echo_emulator.audio import PRODUCED_OFFSET
        from unicorn import UC_HOOK_MEM_WRITE
        self.audio = rig.audio
        machine, ring, engine = rig.machine, rig.audio.ring, rig.audio.engine

        def write(uc, access, address, size, value, user):
            self.flush(machine)
            produced, consumed = struct.unpack("<II", machine.read_memory(
                engine + PRODUCED_OFFSET, 8))
            slot = (address - ring) // 8
            ahead = (slot - produced) % self.slots
            free = self.slots - (produced - consumed)
            self.written[slot] = produced + ahead if ahead < free else produced - (
                self.slots - ahead)

        machine.uc.hook_add(UC_HOOK_MEM_WRITE, write, None, ring, ring + self.slots * 8 - 1)
        machine.hook_symbol("audio_engine_pump", lambda: self.flush(machine))
        machine.hook_symbol("dmac_start_loop", lambda: self.start(machine))

    def flush(self, machine):
        audio = self.audio
        if audio is None or not audio.running or audio.suspend_depth:
            return
        now = audio.elapsed_us(machine) * audio.rate // 1000000
        if now <= self.last:
            return
        ring = machine.read_memory(audio.dma_ring, self.slots * 8)
        for frame in range(max(self.last, now - self.slots), now):
            slot = frame % self.slots
            if any(ring[slot * 8:slot * 8 + 8]) and self.written.get(slot) != frame:
                self.stale.append((machine.milliseconds(), audio.rate, frame))
            if frame < OPENING_FRAMES and self.openings:
                self.openings[-1].append(struct.unpack_from("<ii", ring, slot * 8))
            self.played += 1
        self.last = now


class EchoImageTest(EchoImageHarness):
    def assert_opening(self, played, expected):
        self.assertEqual(OPENING_FRAMES, len(played))
        got = [(left >> 8, right >> 8) for left, right in played]
        self.assertEqual(list(expected[:OPENING_FRAMES]), got)

    def test_each_format_starts_with_its_own_first_samples_and_ends_in_silence(self):
        from echo_emulator.core1_bridge import (CORE1_M4A, CORE1_MP3, CORE1_VORBIS,
                                                Core1Bridge)
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        fixtures = PROJECT / "tests/fixtures"
        volume = Fat32Volume()
        volume.add_file("01 AIFF 44k.aiff", aiff_file(44100))
        volume.add_file("02 WAV 8k.wav", wav_file(8000, 1))
        volume.add_file("03 MP3 48k.mp3",
                        (fixtures / "mp3/cbr_192_48k_stereo_bare.mp3").read_bytes())
        volume.add_file("04 AIFF 44k.aiff", aiff_file(44100))
        volume.add_file("05 Vorbis 44k.ogg", (fixtures / "vorbis/stereo44.ogg").read_bytes())
        volume.add_file("06 Vorbis 48k.ogg", (fixtures / "vorbis/stereo48.ogg").read_bytes())
        volume.add_file("07 AAC 44k.m4a",
                        (fixtures / "m4a_music/Aac Artist/Aac Album/02 Tail.m4a").read_bytes())
        output = PlayedOutput()
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        programs = {program: self.elf.with_name("core1_%s.bin" % name).read_bytes()
                    for program, name in ((CORE1_MP3, "mp3"), (CORE1_M4A, "m4a"),
                                          (CORE1_VORBIS, "vorbis"))}
        bridge = Core1Bridge(rig.audio, programs)
        output.attach(rig)
        rig.open_songs()
        rig.play_song(1)
        machine = rig.machine
        limit = machine.milliseconds() + PLAY_LIMIT_MS
        quiet = 0
        while quiet < 2 and machine.milliseconds() < limit:
            rig.run_for(QUIET_CHECK_MS)
            quiet = quiet + 1 if not rig.audio.running else 0
        print("\n[track ends] %d frames played over %d output starts, core 1 programs %s"
              % (output.played, output.starts, bridge.started))
        self.assertEqual([], output.stale[:20])
        self.assertGreaterEqual(output.starts, OUTPUT_STARTS)
        self.assertEqual([CORE1_MP3, CORE1_M4A, CORE1_VORBIS], sorted(set(bridge.started)))
        self.assertGreater(output.played, PLAYED_FRAMES)
        aiff = [(value << 8, -value << 8) for value in PATTERN] * (OPENING_FRAMES // len(PATTERN))
        self.assert_opening(output.openings[0], aiff)
        self.assert_opening(output.openings[3], aiff)
        speech = [value << 8 for value in wav_pcm(8000, 1)]
        self.assert_opening(output.openings[1],
                            self.converted_reference((speech, speech), 4, OPENING_FRAMES))
        mp3 = output.openings[2]
        self.assertEqual(OPENING_FRAMES, len(mp3))
        self.assertLessEqual(max(abs(value) >> 16 for pair in mp3 for value in pair), MP3_PEAK)
        self.assertGreater(max(abs(value) >> 16 for pair in mp3 for value in pair), MP3_PEAK // 2)
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
