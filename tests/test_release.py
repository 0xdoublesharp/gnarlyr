import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import release
import release_package as package

TAG = f"v{package.IDENTITY['version']}-beta.2"
COMMIT = "a" * 40


class VersionTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(dir=release.scratch_tmp())
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.site = self.root / "site"
        self.site.mkdir()
        old_tag = f"v{package.IDENTITY['version']}-beta.1"
        (self.root / "CHANGELOG.md").write_text(
            f"## [{TAG[1:]}] - Unreleased\n\nCurrent notes.\n\n"
            f"## [{old_tag[1:]}] - 2026-11-26\n\nPrevious notes.\n")
        heading = f'<th data-player="{release.PROJECT}">Firmware<small>ECHO · {TAG[1:]}</small></th>'
        (self.site / "comparison.html").write_text(
            f'<p class="intro">The <a href="{release.RELEASES}/tag/{TAG}">{TAG[1:]} release</a>.</p>\n'
            f"{heading}\n{heading}\n")
        tag_link = f'<a href="{release.RELEASES}/tag/{old_tag}">Release</a>\n'
        direct_links = [f'<a href="{release.RELEASES}/download/{old_tag}/{name}">Download</a>\n'
                        for name in (package.STOCK_IMAGE, package.SUMS)]
        (self.site / "install.html").write_text(
            f"<h3>Download Firmware {old_tag} directly</h3>\n" + "".join(direct_links) + tag_link)
        (self.site / "languages.html").write_text(f"<p>Ready to install · {old_tag}</p>\n" + tag_link)
        (self.site / "themes-catalog.json").write_text(json.dumps({"themes": [
            {"id": Path(name).stem, "downloadUrl": f"{release.RELEASES}/download/{TAG}/{name}",
             "sha256": "a" * 64} for name in release.THEMES]}))

    def test_dry_run_prints_the_assets_and_never_runs_commands(self):
        for verb in ("prepare", "stage", "smoke", "promote"):
            with (self.subTest(verb=verb), patch.object(release, "ROOT", self.root),
                  patch.object(release, "run") as run):
                output = io.StringIO()
                args = [verb, TAG, "--dry-run"]
                if verb == "promote":
                    args += ["--channel", "stable", "--deploy-site"]
                with contextlib.redirect_stdout(output):
                    self.assertEqual(0, release.main(args))
                plan = json.loads(output.getvalue())
                self.assertEqual(verb, plan["action"])
                self.assertEqual(TAG, plan["version"])
                self.assertEqual("stable" if verb == "promote" else "nightly", plan["channel"])
                self.assertEqual(verb == "promote", plan["deploy_site"])
                self.assertEqual(6, sum(name.endswith(".pack") for name in plan["assets"]))
                self.assertEqual(3, sum(name.endswith(".gtheme") for name in plan["assets"]))
                run.assert_not_called()

    def test_wrong_identity_version_stops_before_any_command(self):
        stderr = io.StringIO()
        with patch.object(release, "run") as run, contextlib.redirect_stderr(stderr):
            self.assertEqual(1, release.main(["prepare", "v65535.0.1", "--dry-run"]))
        self.assertIn("identity.json", stderr.getvalue())
        run.assert_not_called()

    def test_changelog_requires_an_exact_version_entry(self):
        with self.assertRaisesRegex(ValueError, "missing changelog entry"):
            release.changelog(f"v{package.IDENTITY['version']}-beta.9", self.root / "CHANGELOG.md")

    def test_a_stale_comparison_caption_is_rejected(self):
        comparison = self.site / "comparison.html"
        content = comparison.read_text()
        comparison.write_text(content.replace(f"ECHO · {TAG[1:]}", "ECHO · 0.0.1", 1))
        with self.assertRaisesRegex(ValueError, "comparison.html"):
            release.check_site(TAG, self.site, set())

    def test_an_unknown_download_pin_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "install.html"):
            release.check_site(TAG, self.site, set())

    def test_manual_install_accepts_the_stock_image_and_checksums(self):
        old_tag = f"v{package.IDENTITY['version']}-beta.1"
        self.assertEqual(dict.fromkeys(release.THEMES, "a" * 64),
                         release.check_site(TAG, self.site, {old_tag}))

    def test_manual_install_rejects_the_redundant_update_image_link(self):
        old_tag = f"v{package.IDENTITY['version']}-beta.1"
        install = self.site / "install.html"
        install.write_text(install.read_text()
                           + f'<a href="{release.RELEASES}/download/{old_tag}/{package.IMAGE}">Update</a>\n')
        with self.assertRaisesRegex(ValueError, "install.html"):
            release.check_site(TAG, self.site, {old_tag})


class PackageTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(dir=release.scratch_tmp())
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.asset_dir = self.root / "assets"
        image = self.root / "update.img"
        data = bytearray(128)
        data[package.BUILD_FIELD] = package.label(TAG).encode().ljust(package.FIELD, b"\0")
        image.write_bytes(data)
        source = self.root / "source.tar.gz"
        source.write_bytes(b"source archive")
        themes = tuple(self.root / name for name in release.THEMES)
        for theme in themes:
            theme.write_bytes(theme.name.encode())
        self.manifest = package.package(image, source, self.asset_dir, COMMIT, TAG, extra_assets=themes)
        self.draft = {"id": 42, "draft": True, "tag_name": TAG, "target_commitish": COMMIT, "prerelease": True}
        self.assets = {index: path for index, path in enumerate(sorted(self.asset_dir.iterdir()), 1)}
        self.remote = [{"id": index, "name": path.name, "state": "uploaded", "size": path.stat().st_size}
                       for index, path in self.assets.items()]
        self.theme_hashes = {name: self.manifest["files"][name]["sha256"] for name in release.THEMES}

    def test_manifest_and_checksum_cover_each_pack_and_stock_alias(self):
        with patch.object(release, "check_theme") as check_theme:
            manifest = release.verify_assets(self.asset_dir, TAG, COMMIT, self.theme_hashes)
        self.assertEqual(self.manifest, manifest)
        self.assertEqual(release.asset_names() - {package.SUMS, package.MANIFEST}, set(manifest["files"]))
        self.assertEqual(manifest["files"][package.IMAGE], manifest["files"][package.STOCK_IMAGE])
        self.assertEqual(len(release.THEMES), check_theme.call_count)

    def test_altered_theme_with_original_checksum_is_rejected(self):
        name = release.THEMES[0]
        (self.asset_dir / name).write_bytes(b"modified theme")
        with self.assertRaisesRegex(ValueError, f"{name}: SHA-256"):
            release.verify_assets(self.asset_dir, TAG, COMMIT, self.theme_hashes)

    def test_built_theme_must_match_site_catalog(self):
        with patch.object(release, "check_theme"), self.assertRaisesRegex(ValueError, "themes-catalog"):
            release.verify_assets(self.asset_dir, TAG, COMMIT, dict.fromkeys(release.THEMES, "0" * 64))

    def fake_download(self, *args, stdout):
        self.assertEqual(("gh", "api", "--hostname", "github.com"), args[:4])
        self.assertEqual(("-H", "Accept: application/octet-stream"), args[-2:])
        index = int(args[4].split("/")[-1])
        stdout.write(self.assets[index].read_bytes())

    def test_smoke_downloads_each_expected_asset_and_validates_the_packs(self):
        with (patch.object(release, "gh_json", side_effect=[[], [self.remote]]),
              patch.object(release, "run", side_effect=self.fake_download) as download,
              patch.object(release, "check_theme") as check,
              contextlib.redirect_stdout(io.StringIO())):
            release.smoke(TAG, self.draft, self.theme_hashes)
        self.assertEqual(len(self.assets), download.call_count)
        self.assertEqual(len(release.THEMES), check.call_count)

    def test_smoke_rejects_missing_assets_before_downloading(self):
        with (patch.object(release, "gh_json", side_effect=[[], [self.remote[:-1]]]),
              patch.object(release, "run") as run,
              self.assertRaisesRegex(ValueError, "missing")):
            release.smoke(TAG, self.draft, self.theme_hashes)
        run.assert_not_called()

    def test_stage_creates_a_prerelease_draft(self):
        with (patch.object(release, "clean_head", return_value=COMMIT),
              patch.object(release, "check_theme"),
              patch.object(release, "find_release", return_value=None),
              patch.object(release, "check_remote_tag"),
              patch.object(release, "gh_release") as github):
            release.stage(TAG, True, self.root, "notes", self.theme_hashes)
        args = github.call_args.args
        self.assertEqual(("create", TAG), args[:2])
        self.assertIn("--draft", args)
        self.assertIn("--prerelease", args)
        self.assertIn(COMMIT, args)
        self.assertIn(self.asset_dir / package.SUMS, args)

    def test_stage_updates_a_draft_but_refuses_a_published_release(self):
        with (patch.object(release, "clean_head", return_value=COMMIT),
              patch.object(release, "check_theme"),
              patch.object(release, "find_release", side_effect=[self.draft, {**self.draft, "draft": False}]),
              patch.object(release, "check_remote_tag"),
              patch.object(release, "gh_release") as github):
            release.stage(TAG, False, self.root, "notes", self.theme_hashes)
            with self.assertRaisesRegex(ValueError, "already published"):
                release.stage(TAG, False, self.root, "notes", self.theme_hashes)
        edit, upload = [call.args for call in github.call_args_list]
        self.assertEqual(("edit", TAG), edit[:2])
        self.assertIn("--prerelease=false", edit)
        self.assertEqual(("upload", TAG, "--clobber"), upload[:3])

    def test_promote_smokes_before_flipping_draft_to_published(self):
        operations = []
        with (patch.object(release, "smoke", side_effect=lambda *_: operations.append("smoke")),
              patch.object(release, "gh_release", side_effect=lambda *args: operations.append(args))):
            release.promote(TAG, self.draft, True, False, self.theme_hashes)
        self.assertEqual(["smoke", ("edit", TAG, "--draft=false", "--prerelease", "--latest=false")], operations)

    def test_stable_promotion_publishes_the_site_after_firmware(self):
        operations = []
        with (patch.object(release, "clean_head", return_value=COMMIT),
              patch.object(release, "run", side_effect=lambda *args, **kwargs: operations.append(args)),
              patch.object(release, "smoke", side_effect=lambda *_: operations.append("smoke")),
              patch.object(release, "gh_release", side_effect=lambda *args: operations.append(args))):
            release.promote(TAG, self.draft, False, True, self.theme_hashes)
        publisher = (sys.executable, release.ROOT / "tools/publish_site.py")
        self.assertEqual([(*publisher, "--check"), "smoke",
                          ("edit", TAG, "--draft=false", "--prerelease=false", "--latest"), publisher], operations)


if __name__ == "__main__":
    unittest.main()
