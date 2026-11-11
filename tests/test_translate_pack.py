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

    def test_retries_invalid_placeholders(self):
        calls = []
        def translate(text, language, key, feedback):
            calls.append(feedback)
            return 'Conectar {0}' if len(calls) == 1 else 'Conectar'
        self.assertEqual(m.translate_entry('Connect', 'Spanish', 'STR_CONNECT', translate), 'Conectar')
        self.assertIn('placeholders differ', calls[1])

    def test_fails_after_invalid_retries(self):
        with self.assertRaises(ValueError):
            m.translate_entry('Connect', 'Spanish', 'STR_CONNECT', lambda *args: 'Conectar {0}')

    def test_checkpoint_binds_source_and_language(self):
        source = [('STR_PLAY', 'Play')]
        token = m.checkpoint_token(source, 'Spanish', 'translategemma:27b')
        self.assertNotEqual(token, m.checkpoint_token([('STR_PLAY', 'Playing')], 'Spanish', 'translategemma:27b'))
        self.assertNotEqual(token, m.checkpoint_token(source, 'French', 'translategemma:27b'))
