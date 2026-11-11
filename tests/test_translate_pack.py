import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location('translate_pack', pathlib.Path(__file__).parents[1] / 'tools/translate_pack.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class TranslationTests(unittest.TestCase):
    def test_validate_placeholders_and_breaks(self):
        self.assertEqual(m.validate('Volume {0}', 'Volumen {0}'), [])
        self.assertTrue(m.validate('Volume {0}', 'Volumen'))
        self.assertTrue(m.validate('Play', ''))
        self.assertTrue(m.validate('One\\nTwo', 'Uno Dos'))

    def test_preserves_keys(self):
        pack = m.build_pack([('STR_PLAY', 'Play')], 'Spanish', lambda text, language: 'Reproducir')
        self.assertEqual(pack, [('STR_PLAY', 'Reproducir')])

    def test_prompt_includes_player_context(self):
        prompt = m.make_prompt('Playing', 'Spanish', 'STR_PLAYING', 'Shared words')
        self.assertIn('portable music player', prompt)
        self.assertIn('STR_PLAYING', prompt)
        self.assertIn('Shared words', prompt)
        self.assertTrue(prompt.endswith('\\n\\n\\nPlaying'.replace('\\n', '\n')))
