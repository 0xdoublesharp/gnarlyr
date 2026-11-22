import unittest
from types import SimpleNamespace
from unittest import mock

import echo_debug
import echo_language
import test_echo_language as base
from test_echo_language import PACK, pack, working_tree_show


class RecipeError(Exception):
    pass


class FakeRecipe:
    RecipeError = RecipeError
    lang_pack = SimpleNamespace(PackError=PACK.PackError)
    glyph_pack = SimpleNamespace(PackError=ValueError)

    def __init__(self, test, failure=None):
        self.test = test
        self.failure = failure
        self.prepared = []
        self.finished = []

    def Volumes(self, internal, music):
        return SimpleNamespace(internal=internal, music=music)

    def Extras(self, language, packs):
        return SimpleNamespace(language=language, packs=packs)

    def Setup(self, recipe, drawable):
        return SimpleNamespace(recipe=recipe, drawable=drawable)

    def check_recipe(self, recipe):
        return recipe

    def base_drawable(self, fonts, packs):
        return frozenset()

    def prepare(self, volumes, extras, setup):
        if self.failure is not None:
            raise self.failure
        self.prepared.append((volumes, extras, setup))
        return "prepared"

    def finish(self, volumes, prepared):
        self.finished.append(list(self.test.sent))
        return SimpleNamespace(as_dict=lambda: {"action": "written", "languages": ["fr"]})


class LanguageFonts(base.LanguageCommands):
    def recipe(self, failure=None):
        fake = FakeRecipe(self, failure)
        return fake, mock.patch.object(echo_language, "recipe_tools", return_value=fake)

    def test_install_builds_fonts_for_the_new_pack_and_writes_them_before_the_player_refreshes(self):
        tool, host = self.live_tool("fr")
        fake, patch = self.recipe()
        with patch:
            result = tool.language_install(self.pack, select=True)
        volumes, extras, setup = fake.prepared[0]
        self.assertEqual((self.root, self.card), (volumes.internal, volumes.music))
        self.assertEqual("fr", extras.language)
        self.assertEqual({"fr": pack()}, extras.packs)
        self.assertEqual(1, setup.recipe["version"])
        self.assertEqual([[]], fake.finished)
        self.assertEqual({"ok": True, "action": "written", "languages": ["fr"]}, result["fonts"])
        self.assertEqual("fr", result["selected"])

    def test_install_without_selecting_keeps_the_saved_language_for_the_style(self):
        tool, host = self.live_tool()
        fake, patch = self.recipe()
        with patch:
            tool.language_install(self.pack)
        self.assertIsNone(fake.prepared[0][1].language)

    def test_select_rebuilds_fonts_for_the_chosen_language_and_refuses_an_unknown_one_first(self):
        (self.lang / "fr.pack").write_bytes(pack())
        tool, host = self.live_tool("fr")
        fake, patch = self.recipe()
        with patch:
            result = tool.language_select("fr")
            self.assertEqual("fr", fake.prepared[0][1].language)
            self.assertEqual({}, fake.prepared[0][1].packs)
            self.assertTrue(result["fonts"]["ok"])
            with self.assertRaises(echo_debug.DebugError):
                tool.language_select("de")
        self.assertEqual(1, len(fake.prepared))

    def test_a_font_failure_is_reported_and_the_pack_still_installs(self):
        tool, host = self.live_tool("fr")
        fake, patch = self.recipe(RecipeError("offline"))
        with patch:
            result = tool.language_install(self.pack, select=True)
        self.assertEqual({"ok": False, "error": "fonts_failed", "message": "offline"}, result["fonts"])
        self.assertEqual(pack(), (self.lang / "fr.pack").read_bytes())
        self.assertEqual([], fake.finished)

    def test_without_the_font_tools_the_install_says_how_to_get_them(self):
        tool, host = self.live_tool("fr")
        with mock.patch.object(echo_language, "recipe_tools", side_effect=ImportError("No module named 'PIL'")):
            result = tool.language_install(self.pack, select=True)
        self.assertEqual("fonts_unavailable", result["fonts"]["error"])
        self.assertIn("requirements.txt", result["fonts"]["message"])

    def test_a_build_that_reads_no_extra_fonts_gets_none(self):
        def show(snapshot, name):
            if name.endswith("recipe.json"):
                raise ValueError("the installed build cannot be resolved in this repository")
            return working_tree_show(snapshot, name)
        tool, host = self.live_tool("fr")
        fake, patch = self.recipe()
        with patch, mock.patch.object(echo_language.Snapshot, "show", show):
            result = tool.language_install(self.pack, select=True)
        self.assertEqual("fonts_unsupported", result["fonts"]["error"])
        self.assertEqual([], fake.prepared)
        self.assertEqual("fr", result["selected"])


for inherited in [name for name in dir(base.LanguageCommands) if name.startswith("test_")]:
    setattr(LanguageFonts, inherited, None)


if __name__ == "__main__":
    unittest.main()
