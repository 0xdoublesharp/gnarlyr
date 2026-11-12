import base64
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = Path(os.environ.get("SITE_SOURCE_ROOT", ROOT)).resolve()
sys.path.insert(0, str(SOURCE_ROOT / "firmware" / "tools"))
import lang_pack

NODE = shutil.which("node")
NODE_SCRIPT = r"""
const fs = require("node:fs");
const vm = require("node:vm");
const input = JSON.parse(fs.readFileSync(0, "utf8"));
globalThis.LANGUAGE_CATALOGUE = input.catalogue;
vm.runInThisContext(fs.readFileSync(process.argv[1], "utf8"));
try {
  const api = globalThis.LanguagePacks;
  let result;
  if (input.operation === "build") {
    const bytes = api.buildPack(input.code, input.name, input.text);
    result = {pack: Buffer.from(bytes).toString("base64"), info: api.inspectPack(bytes)};
  } else if (input.operation === "inspect") {
    result = api.inspectPack(new Uint8Array(Buffer.from(input.pack, "base64")));
  } else if (input.operation === "template") {
    result = api.templateText();
  } else {
    result = api.parseTranslation(input.text);
  }
  process.stdout.write(JSON.stringify({ok: true, result}));
} catch (error) {
  process.stdout.write(JSON.stringify({ok: false, message: error.message}));
}
"""


@unittest.skipUnless(NODE, "Node is required for browser/Python language-pack parity")
class LanguageBrowserTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalogue = lang_pack.read_catalogue()
        cls.drawable = lang_pack.drawable_characters()
        cls.segments = lang_pack.string_segments(cls.catalogue) if hasattr(lang_pack, "string_segments") else None
        cls.metadata = lang_pack.catalogue_info(cls.catalogue, cls.drawable, *([cls.segments] if cls.segments else []))

    def browser(self, operation, **values):
        payload = {"operation": operation, "catalogue": self.metadata, **values}
        result = subprocess.run([NODE, "-e", NODE_SCRIPT, str(ROOT / "site/language-packs.js")],
                                input=json.dumps(payload), text=True, capture_output=True,
                                check=True)
        return json.loads(result.stdout)

    def inspect(self, data):
        return self.browser("inspect", pack=base64.b64encode(data).decode("ascii"))

    def pack(self, **translations):
        return lang_pack.build_pack("es", self.catalogue,
                                    {"STR_LANGUAGE_NAME": "Español", **translations}, *([self.segments] if self.segments else []))

    def validate(self, data):
        return lang_pack.validate_pack(data, self.catalogue, self.drawable, *([self.segments] if self.segments else []))

    def seal(self, data):
        if not self.segments:
            struct.pack_into("<I", data, 16, lang_pack.fnv(data[20:], lang_pack.fnv(data[:16])))
            return data
        total = struct.unpack_from("<H", data, 56)[0]
        for index in range(min(total, len(self.segments))):
            row = 60 + 16 * index
            start, size = struct.unpack_from("<II", data, row + 4)
            struct.pack_into("<I", data, row + 12, lang_pack.fnv(data[start:start + size]))
        end = lang_pack.index_size(total)
        struct.pack_into("<I", data, 16, lang_pack.fnv(data[20:end], lang_pack.fnv(data[:16])))
        return data

    def test_builder_matches_python_byte_for_byte_and_fills_missing_english(self):
        text = "STR_LANGUAGE_NAME = Español\nSTR_SETTINGS = Ajustes\nSTR_PLACE = {1} de {0}\n"
        result = self.browser("build", code="es", name="Español", text=text)
        self.assertTrue(result["ok"], result)
        expected = self.pack(STR_SETTINGS="Ajustes", STR_PLACE="{1} de {0}")
        self.assertEqual(base64.b64decode(result["result"]["pack"]), expected)
        self.assertEqual(result["result"]["info"],
                         self.validate(expected))

    def test_native_name_is_added_when_the_translation_omits_it(self):
        result = self.browser("build", code="es", name="Español", text="STR_SETTINGS = Ajustes")
        self.assertTrue(result["ok"], result)
        self.assertEqual(base64.b64decode(result["result"]["pack"]),
                         self.pack(STR_SETTINGS="Ajustes"))

    def test_placeholder_reordering_and_repetition_match_python_semantics(self):
        result = self.browser("build", code="es", name="Español",
                              text="STR_PLACE = {1} / {0} / {0}")
        self.assertTrue(result["ok"], result)
        pack = base64.b64decode(result["result"]["pack"])
        self.assertEqual(self.validate(pack)["texts"]
                         ["STR_PLACE"], "{1} / {0} / {0}")

    def test_translation_errors_are_rejected_before_a_pack_is_created(self):
        cases = [
            {"text": "STR_SETTINGS = A\nSTR_SETTINGS = B"},
            {"text": "STR_UNKNOWN_TRANSLATION = A"},
            {"text": "this is not a catalogue entry"},
            {"text": "STR_PLACE = {0}"},
            {"text": "STR_SETTINGS = Music 😀"},
            {"text": "STR_LANGUAGE_NAME = Different"},
            {"code": "longer"},
            {"code": "a/b"},
            {"code": "es\n"},
            {"code": ""},
            {"name": ""},
            {"name": "é" * 14},
            {"name": "bad\0name"},
            {"name": "first\nsecond"},
            {"name": "a\u00a0b"},
            {"text": "STR_SETTINGS = " + "x" * self.metadata["pack_limit"]},
        ]
        for values in cases:
            with self.subTest(values=values):
                data = {"code": "es", "name": "Español", "text": "", **values}
                self.assertFalse(self.browser("build", **data)["ok"])

    def test_python_packs_are_inspected_with_exactly_the_same_strings(self):
        pack = self.pack(STR_SETTINGS="Ajustes")
        result = self.inspect(pack)
        self.assertTrue(result["ok"], result)
        self.assertEqual(result["result"],
                         self.validate(pack))

    def test_shared_string_offsets_remain_compatible(self):
        pack = bytearray(self.pack())
        names = self.segments[0][1] if self.segments else [name for name, _ in self.catalogue]
        settings = names.index("STR_SETTINGS")
        other = settings + 1
        table = lang_pack.index_size(len(self.segments)) if self.segments else 56
        pack[table + 2 * other:table + 2 + 2 * other] = pack[table + 2 * settings:table + 2 + 2 * settings]
        self.seal(pack)
        expected = self.validate(pack)
        self.assertEqual(self.inspect(pack), {"ok": True, "result": expected})

    def test_corrupt_incompatible_and_semantically_invalid_packs_match_python_rejection(self):
        valid = self.pack()
        cases = [valid[:10], valid[:-1], valid + b"x"]
        for offset in (0, 4, 6, 8, 12, 16, 55, 57, 60, 64, len(valid) - 2):
            changed = bytearray(valid)
            changed[offset] ^= 1
            cases.append(bytes(changed))
        body = lang_pack.index_size(len(self.segments)) + 2 * len(self.segments[0][1]) if self.segments else 56 + 2 * len(self.catalogue)
        fields = ((56, 1), (56, len(self.segments) + 1), (58, 1), (60, 3), (62, 1), (76, 9)) if self.segments else ((56, 1), (56, len(valid)))
        for field, value in fields:
            changed = bytearray(valid)
            struct.pack_into("<H", changed, field, value)
            cases.append(bytes(self.seal(changed)))
        if self.segments:
            legacy = lang_pack.build_legacy_pack("es", self.catalogue, {"STR_LANGUAGE_NAME": "Español"})
            cases.append(legacy)
        for offset, value in ((body, 255), (len(valid) - 1, 65), (20, 47), (23, 65)):
            changed = bytearray(valid)
            changed[offset] = value
            cases.append(bytes(self.seal(changed)))
        cases.extend([self.pack(STR_SETTINGS="😀"), self.pack(STR_PLACE="{0}")])
        changed = bytearray(valid)
        changed[28:56] = b"Other" + bytes(23)
        cases.append(bytes(self.seal(changed)))
        for index, data in enumerate(cases):
            with self.subTest(case=index):
                with self.assertRaises(lang_pack.PackError):
                    self.validate(data)
                self.assertFalse(self.inspect(data)["ok"])

    def test_downloadable_template_preserves_the_exact_catalogue_order_and_text(self):
        result = self.browser("template")
        self.assertTrue(result["ok"], result)
        self.assertEqual(lang_pack.read_catalogue_text(result["result"]), self.catalogue)


if __name__ == "__main__":
    unittest.main()
