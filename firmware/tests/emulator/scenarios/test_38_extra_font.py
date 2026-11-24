import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    IDENTITY,
    EchoImageHarness,
    lang_pack,
    main,
    make_image,
)

import font_build
import glyph_pack

HANGUL = 0xAC00
HANGUL_GLYPHS = 64
MP3_FRAME = bytes.fromhex("fffb9064") + bytes(413)
MISSING_FILE_BYTES = 16384
IMAGE_FACES = 21
SLOT_BYTES = 368
INDEX_BYTES_PER_SLOT = 4
HOLD_NONE = 0
HOLD_FREEZE = 2
SETTINGS_LANGUAGE = slice(19, 24)
SETTINGS_TEXT_SIZE = 24
TEXT_SIZE_LARGE = 2
PACKS = Path(__file__).resolve().parents[3] / "src" / "fonts" / "packs"
CHINESE_FACES = ["inter_regular_16_latin1_sc", "inter_regular_22_sc", "inter_semibold_22_sc",
                 "inter_regular_18_sc", "inter_semibold_18_sc", "inter_regular_26_sc",
                 "inter_semibold_26_sc"]


def hangul_face(level):
    glyphs = [font_build.GlyphImage(HANGUL + index, 10, 12, 1, 11, 12,
                                    [(level + index) % 16] * 120)
              for index in range(HANGUL_GLYPHS)]
    return glyph_pack.pack_glyphs(glyphs)


def hangul_font(level=7):
    return glyph_pack.pack_extra_font([hangul_face(level)] * glyph_pack.EXTRA_FACES, ["ko"])


def chinese_font():
    faces = [(PACKS / f"{name}.bin").read_bytes() for name in CHINESE_FACES]
    return glyph_pack.pack_extra_font(faces, ["zh-CN"])


def hangul_word(match):
    return chr(HANGUL + sum(map(ord, match.group(0))) % HANGUL_GLYPHS)


def korean_translations(catalogue):
    translations = {}
    for name, text in catalogue:
        pieces = [piece if lang_pack.PLACEHOLDER.fullmatch(piece)
                  else re.sub(r"[A-Za-z]+", hangul_word, piece)
                  for piece in lang_pack.PIECE.findall(text)]
        translations[name] = "".join(pieces)
    return translations


def chinese_translations(catalogue):
    face = glyph_pack.read_face((PACKS / f"{CHINESE_FACES[0]}.bin").read_bytes())
    han = [codepoint for codepoint in sorted(face) if 0x4E00 <= codepoint <= 0x9FFF]
    han = [han[index * len(han) // 160] for index in range(160)]

    def word(match):
        value = lang_pack.fnv(match.group(0).encode("ascii"))
        return chr(han[value % len(han)]) + chr(han[(value >> 12) % len(han)])

    return {name: "".join(piece if lang_pack.PLACEHOLDER.fullmatch(piece)
                          else re.sub(r"[A-Za-z]+", word, piece)
                          for piece in lang_pack.PIECE.findall(text))
            for name, text in catalogue}


class EchoImageTest(EchoImageHarness):
    def test_an_invalid_slot_1_glyph_table_uses_slot_2_glyphs(self):
        expected = self.render_image_face(None)
        for damage in ("table magic", "table count"):
            with self.subTest(damage=damage):
                self.assertEqual(expected, self.render_image_face(damage))

    def test_a_damaged_slot_1_face_renders_the_slot_2_glyphs(self):
        expected = self.render_image_face(None)
        for damage in ("row", "header"):
            with self.subTest(damage=damage):
                self.assertEqual(expected, self.render_image_face(damage))

    def render_image_face(self, damage):
        face = 2
        row = 8 + face * 8
        at = make_image.GLYPH_TABLE_OFFSET
        offset, size = struct.unpack_from("<II", self.image, at + row)
        glyphs = hangul_face(9)
        self.assertLessEqual(len(glyphs), size)

        def prepare(session):
            for slot in ("slot_1", "slot_2"):
                base = session.layout[slot]
                first = base + offset // 512
                payload = bytearray(glyphs + bytes(-len(glyphs) % 512))
                table_lba = base + at // 512
                table = bytearray(session.disk.read(table_lba, 1))
                struct.pack_into("<I", table, row + 4, len(glyphs))
                if slot == "slot_1":
                    if damage == "header":
                        payload[glyph_pack.HEADER_CHECK] ^= 0xFF
                    elif damage == "row":
                        struct.pack_into("<I", table, row + 4, 1)
                    elif damage == "table magic":
                        table[0] ^= 0xFF
                    elif damage == "table count":
                        struct.pack_into("<I", table, 4, make_image.GLYPH_FACE_LIMIT + 1)
                session.disk.write(first, bytes(payload))
                session.disk.write(table_lba, bytes(table))

        session, machine = self.started_session(prepare=prepare, volume=self.korean_volume(None))
        self.assertTrue(any(index == face and present
                            for (index, _), present in self.hangul_lookups(machine).items()))
        if damage is not None:
            backup = session.layout["slot_2"] + offset // 512
            self.assertTrue(any(lba == backup for _, lba, _ in session.card.reads))
        self.assert_healthy(session)
        return machine.panel.frame

    def player_volume(self, code, translations, font, text_size=None):
        from echo_emulator.fat_volume import Fat32Volume
        catalogue = lang_pack.read_catalogue()
        pack = lang_pack.build_pack(code, catalogue, translations, lang_pack.read_segments(catalogue))
        volume = Fat32Volume()
        folder = IDENTITY.data_directory
        volume.add_directory(folder)
        volume.add_directory("lang", directory=folder)
        volume.add_directory("fonts", directory=folder)
        volume.add_file(f"{code}.pack", pack, directory=folder + "/lang")
        settings = bytearray(self.saved_settings(0, 15, 0))
        settings[SETTINGS_LANGUAGE] = code.encode("ascii").ljust(5, b"\0")
        if text_size is not None:
            settings[SETTINGS_TEXT_SIZE] = text_size
        struct.pack_into("<I", settings, 54, lang_pack.fnv(settings[:54]))
        volume.add_file("settings.bin", bytes(settings), directory=folder)
        if font is not None:
            volume.add_file("extra.font", font, directory=folder + "/fonts")
        return volume

    def korean_volume(self, font):
        return self.player_volume("ko", korean_translations(lang_pack.read_catalogue()), font)

    def font_sectors(self, session, volume):
        node = next(child for child in volume.directory(IDENTITY.data_directory + "/fonts").children
                    if child.name == "extra.font")
        first = session.layout["volume_start"] + volume.cluster_lba(node.first)
        return range(first, first + (node.size + 511) // 512)

    def word(self, machine, name):
        return struct.unpack("<I", machine.read_memory(machine.symbols[name], 4))[0]

    def attached(self, machine):
        return self.word(machine, "extra_source") != 0

    def cached(self, machine):
        slots, count = self.word(machine, "slots"), self.word(machine, "slot_count")
        memory = machine.read_memory(slots, count * SLOT_BYTES)
        entries = {}
        for slot in range(count):
            key, present = struct.unpack_from("<IB", memory, slot * SLOT_BYTES)
            if key:
                entries[(key >> 16) - 1, key & 0xFFFF] = bool(present)
        misses = slots + count * (SLOT_BYTES + INDEX_BYTES_PER_SLOT)
        for key, in struct.iter_unpack("<I", machine.read_memory(misses, count * 4)):
            if key:
                entries[(key >> 16) - 1, key & 0xFFFF] = False
        return entries

    def hangul_lookups(self, machine):
        return {(face, codepoint): present for (face, codepoint), present in
                self.cached(machine).items() if HANGUL <= codepoint < HANGUL + HANGUL_GLYPHS}

    def assert_hangul_falls_back(self, session):
        machine = session.machine
        self.assertFalse(self.attached(machine))
        self.assertEqual([], [key for key, present in self.hangul_lookups(machine).items()
                              if present])
        self.assert_healthy(session)

    def test_an_extra_font_on_internal_storage_draws_korean_text(self):
        session, machine = self.started_session(volume=self.korean_volume(hangul_font()))
        self.assertTrue(self.attached(machine))
        lookups = self.hangul_lookups(machine)
        print("\n[extra font] Hangul lookups after start: %d" % len(lookups))
        self.assertTrue(lookups)
        self.assertTrue(all(face >= IMAGE_FACES for face, _ in lookups))
        self.assertTrue(all(lookups.values()))
        self.assert_healthy(session)

    def test_without_an_extra_font_korean_text_falls_back(self):
        session, _ = self.started_session(volume=self.korean_volume(None))
        self.assert_hangul_falls_back(session)

    def test_a_damaged_extra_font_header_is_refused(self):
        font = bytearray(hangul_font())
        font[40] ^= 0xFF
        session, _ = self.started_session(volume=self.korean_volume(bytes(font)))
        self.assert_hangul_falls_back(session)

    def test_a_truncated_extra_font_is_refused(self):
        font = hangul_font()
        session, _ = self.started_session(volume=self.korean_volume(font[:len(font) - 700]))
        self.assert_hangul_falls_back(session)

    def test_a_damaged_extra_face_sector_loses_only_its_glyphs(self):
        font = bytearray(hangul_font())
        start, size = struct.unpack_from("<II", font, 12 + 8 * 2)
        font[start + 2 * 512 + 3] ^= 0xFF
        session, machine = self.started_session(volume=self.korean_volume(bytes(font)))
        self.assertTrue(self.attached(machine))
        lookups = self.hangul_lookups(machine)
        self.assertTrue(any(not present for (face, _), present in lookups.items()
                            if face == IMAGE_FACES + 2))
        self.assertTrue(all(present for (face, _), present in lookups.items()
                            if face >= IMAGE_FACES and face != IMAGE_FACES + 2))
        self.assert_healthy(session)

    def song_volume(self, font):
        from echo_emulator.fat_volume import Fat32Volume
        volume = Fat32Volume()
        folder = IDENTITY.data_directory
        volume.add_directory(folder)
        volume.add_directory("fonts", directory=folder)
        if font is not None:
            volume.add_file("extra.font", font, directory=folder + "/fonts")
        volume.add_file(chr(HANGUL) + chr(HANGUL + 1) + " Song.mp3", MP3_FRAME * 40, None,
                        long_name=True)
        return volume

    def test_a_scan_records_the_characters_the_fonts_cannot_draw(self):
        session, _ = self.started_session(volume=self.song_volume(None))
        reader = session.fat()
        text = reader.read("/" + IDENTITY.data_directory + "/fonts/missing.txt")
        count = reader.read("/" + IDENTITY.data_directory + "/fonts/missing.count")
        self.assertIsNotNone(text)
        self.assertEqual(MISSING_FILE_BYTES, len(text))
        self.assertEqual((chr(HANGUL) + chr(HANGUL + 1)).encode("utf-8"), text.rstrip(b"\0"))
        self.assertGreaterEqual(struct.unpack_from("<I", count)[0], 1)
        self.assert_healthy(session)

    def test_a_scan_records_nothing_the_extra_font_draws(self):
        session, _ = self.started_session(volume=self.song_volume(hangul_font()))
        self.assertIsNone(session.fat().read("/" + IDENTITY.data_directory + "/fonts/missing.txt"))
        self.assert_healthy(session)

    def test_with_a_card_as_the_music_volume_the_extra_font_comes_from_internal_storage(self):
        card = self.with_card()
        session, machine = self.started_session(prepare=lambda session: card.insert(session.machine),
                                                volume=self.korean_volume(hangul_font()))
        self.assertTrue(self.attached(machine))
        self.assert_healthy(session)

    def usb_session(self, volume, changed):
        session = self.session(volume=volume, vbus=True)
        machine = session.machine
        machine.host_ejects()
        for name in ("usb_wait_for_host", "usb_session_storage_changed"):
            machine.note(name)
        if changed:
            machine.stub_symbol("usb_session_storage_changed", lambda machine: 1)
        attaches = machine.trace("font_pack_attach_extra")
        machine.answer_waits_for_key(4)
        self.assertEqual(session.run_to_first_frame(), "condition met")
        self.assertEqual(machine.run(until=lambda: "usb_wait_for_host" in machine.reached,
                                     until_ms=self.session_module.FRAME_LIMIT_MS), "condition met")
        return session, machine, attaches

    def chinese_volume(self):
        translations = chinese_translations(lang_pack.read_catalogue())
        return self.player_volume("zh-CN", translations, chinese_font(), TEXT_SIZE_LARGE)

    def held_glyphs(self, machine):
        base, held = self.word(machine, "slots"), self.word(machine, "held_bytes")
        memory = machine.read_memory(base, held)
        glyphs, at = {}, 0
        while at < held:
            key, present = struct.unpack_from("<IB", memory, at)
            width, height = memory[at + 10], memory[at + 11]
            glyphs[(key >> 16) - 1, key & 0xFFFF] = bool(present)
            at += (16 + (width * height + 1) // 2 + 3) & ~3
        return glyphs

    def test_a_storage_session_with_a_chinese_menu_reads_no_extra_font_and_draws_held_glyphs(self):
        volume = self.chinese_volume()
        session, machine, _ = self.usb_session(volume, changed=False)
        self.assertEqual(HOLD_FREEZE, self.word(machine, "hold"))
        held = self.word(machine, "held_bytes")
        glyphs = self.held_glyphs(machine)
        chinese = [codepoint for (face, codepoint), present in glyphs.items()
                   if present and face >= IMAGE_FACES and codepoint >= 0x4E00]
        sectors = self.font_sectors(session, volume)
        reads = len(session.card.read_log)
        frozen = []
        machine.hook_symbol("usb_session_storage_changed", lambda: frozen.append(
            (self.word(machine, "hold"), self.word(machine, "held_bytes"),
             self.held_glyphs(machine))))
        self.assertEqual(machine.run(until=lambda: "usb_session_storage_changed" in machine.reached,
                                     until_ms=machine.milliseconds() + self.session_module.FRAME_LIMIT_MS),
                         "condition met")
        during = [(lba, count) for _, lba, count in session.card.read_log[reads:]
                  if any(lba <= sector < lba + count for sector in sectors)]
        print("\n[extra font] held %d bytes, %d Chinese glyphs; extra.font reads while shared: %d"
              % (held, len(chinese), len(during)))
        self.assertGreater(len(chinese), 10)
        self.assertEqual([], during)
        added = {key: present for key, present in frozen[0][2].items() if key not in glyphs}
        print("[extra font] looked up while frozen: %s" % sorted(
            ("%d U+%04X %s" % (face, codepoint, chr(codepoint)), present)
            for (face, codepoint), present in added.items()))
        self.assertEqual(HOLD_FREEZE, frozen[0][0])
        self.assertEqual({}, added)
        machine.run(until_ms=machine.milliseconds() + 1500)
        self.assertEqual(HOLD_NONE, self.word(machine, "hold"))
        self.assertTrue(self.attached(machine))
        self.assert_healthy(session)

    def test_an_extra_font_replaced_while_shared_is_read_afresh_after_the_session(self):
        volume = self.korean_volume(hangul_font(7))
        session, machine, attaches = self.usb_session(volume, changed=True)
        before = len(attaches)
        replaced = hangul_font(3)
        sectors = self.font_sectors(session, volume)
        session.disk.write(sectors[0], replaced + bytes(-len(replaced) % 512))
        machine.host_leaves()
        self.assertEqual(machine.run(until=lambda: "usb_session_storage_changed" in machine.reached,
                                     until_ms=machine.milliseconds() + self.session_module.FRAME_LIMIT_MS),
                         "condition met")
        machine.run(until_ms=machine.milliseconds() + 1500)
        self.assertGreater(len(attaches), before)
        self.assertTrue(self.attached(machine))
        self.assertEqual(HOLD_NONE, self.word(machine, "hold"))
        slots, count = self.word(machine, "slots"), self.word(machine, "slot_count")
        memory = machine.read_memory(slots, count * SLOT_BYTES)
        levels = {}
        for slot in range(count):
            key, present = struct.unpack_from("<IB", memory, slot * SLOT_BYTES)
            codepoint = key & 0xFFFF
            if present and (key >> 16) - 1 >= IMAGE_FACES and HANGUL <= codepoint < HANGUL + HANGUL_GLYPHS:
                levels[codepoint] = memory[slot * SLOT_BYTES + 16] >> 4
        print("\n[extra font] Hangul glyphs drawn after the session: %d" % len(levels))
        self.assertTrue(levels)
        self.assertEqual({codepoint: (3 + codepoint - HANGUL) % 16 for codepoint in levels}, levels)
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
