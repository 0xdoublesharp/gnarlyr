import dataclasses
import json
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import identity


class IdentityReaderTest(unittest.TestCase):
    def test_the_checked_in_file_reads_as_the_fields_the_loaded_identity_carries(self):
        fields = identity.read()
        loaded = dataclasses.asdict(identity.load())
        self.assertEqual(set(fields), set(loaded))
        self.assertEqual({**fields, "former_ids": tuple(fields["former_ids"])}, loaded)

    def test_text_from_another_revision_parses_without_every_current_field(self):
        self.assertEqual({"name": "Other"}, identity.parse('{"name": "Other"}'))

    def test_text_that_is_not_an_object_is_refused(self):
        for text in ("[]", '"name"', "7"):
            with self.assertRaises(ValueError):
                identity.parse(text)

    def test_text_that_is_not_json_is_refused_as_a_value_error(self):
        with self.assertRaises(ValueError):
            identity.parse("{")

    def test_a_file_elsewhere_is_read_through_the_same_parser(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "identity.json"
            fields = identity.read()
            path.write_text(json.dumps(fields), encoding="utf-8")
            self.assertEqual(fields, identity.read(path))
            self.assertEqual(identity.load(), identity.load(path))

    def test_a_file_missing_a_field_cannot_be_loaded(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "identity.json"
            fields = identity.read()
            del fields["version"]
            path.write_text(json.dumps(fields), encoding="utf-8")
            with self.assertRaises(TypeError):
                identity.load(path)


if __name__ == "__main__":
    unittest.main()
