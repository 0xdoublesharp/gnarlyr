import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
from urllib.parse import unquote, urlsplit
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

SPEC = importlib.util.spec_from_file_location("publish_site", Path(__file__).parents[1] / "tools/publish_site.py")
publisher = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(publisher)
LOCALE_SPEC = importlib.util.spec_from_file_location("site_locales", Path(__file__).parents[1] / "tools/site_locales.py")
locales = importlib.util.module_from_spec(LOCALE_SPEC)
LOCALE_SPEC.loader.exec_module(locales)
SCRATCH = Path("/Volumes/Scratch/SnowskyEcho/tmp")


class LocalizedPublicationTests(unittest.TestCase):
    def test_localized_html_has_its_own_canonical_and_versioned_shared_assets(self):
        with tempfile.TemporaryDirectory(prefix="site-locales-test-", dir=SCRATCH) as folder:
            site = Path(folder)
            page = ('<!doctype html><html lang="en"><head><title data-i18n="home.title">Music</title>'
                    f'<link rel="canonical" href="{locales.SITE_ORIGIN}/">'
                    f'<meta property="og:url" content="{locales.SITE_ORIGIN}/">'
                    '<meta property="og:locale" content="en_US">'
                    '<link rel="stylesheet" href="styles.css"></head><body>'
                    '<header><nav><a href="docs.html" data-i18n="home.docs">Docs</a></nav></header>'
                    '<h1 data-i18n="home.heading">Your music.</h1></body></html>')
            for name in publisher.PUBLIC_FILES:
                (site / name).write_text(page if name.endswith(".html") else "asset")
            (site / "i18n").mkdir()
            (site / "i18n/locales.json").write_text(json.dumps({
                "version": 1, "origin": locales.SITE_ORIGIN, "pages": ["index.html"],
                "locales": [
                    {"code": "en", "name": "English", "og_locale": "en_US", "language_label": "Site language", "home_label": "home", "guide_note": ""},
                    {"code": "es", "name": "Español", "og_locale": "es_ES", "language_label": "Idioma", "home_label": "inicio", "guide_note": "Guías en inglés."},
                ],
            }))
            messages = {}
            for key, source, translation in [("home.title", "Music", "Música"), ("home.heading", "Your music.", "Tu música."), ("home.docs", "Docs", "Documentos")]:
                messages[key] = {"source": source, "source_sha256": hashlib.sha256(source.encode()).hexdigest(), "translation": translation}
            (site / "i18n/es.json").write_text(json.dumps({"version": 1, "locale": "es", "messages": messages}))
            with mock.patch.object(publisher, "SITE", site):
                files = publisher.public_files()
            self.assertIn("es/index.html", files)
            html = files["es/index.html"].decode()
            self.assertIn('<html lang="es">', html)
            self.assertIn(f'rel="canonical" href="{locales.SITE_ORIGIN}/es/"', html)
            self.assertIn("Tu música.", html)
            self.assertIn('href="/docs.html"', html)
            version = hashlib.sha256(files["styles.css"]).hexdigest()[:12]
            self.assertIn(f'href="/styles.css?v={version}"', html)
            self.assertNotIn("i18n/es.json", files)


class CatalogueSafetyTests(unittest.TestCase):
    def test_social_metadata_refresh_preserves_translation_ids(self):
        tool = Path(__file__).parents[1] / "tools/site_social.py"
        spec = importlib.util.spec_from_file_location("site_social", tool)
        social = importlib.util.module_from_spec(spec)
        with mock.patch.object(sys, "path", [str(tool.parent), *sys.path]):
            spec.loader.exec_module(social)
        source = (publisher.SITE / "index.html").read_text()
        card = next(card for card in json.loads((tool.parent / "site_social.json").read_text())["cards"] if card["page"] == "index.html")
        refreshed = social.update_page(source, social.metadata_tags(source, card, f"{locales.SITE_ORIGIN}/assets/social/example.png"))
        self.assertEqual(locales.catalogue(refreshed), locales.catalogue(source))

    def test_annotations_keep_inline_context_and_stable_ids(self):
        source = '<html><head><title>Music</title></head><body><p>Read <a href="docs.html">the guide</a>.</p></body></html>'
        annotated = locales.annotate(source, "home")
        self.assertEqual(locales.annotate(annotated, "home"), annotated)
        entries = locales.catalogue(annotated)
        self.assertIn('Read <a href="docs.html">the guide</a>.', [entry["source"] for entry in entries.values()])

    def test_missing_stale_empty_and_changed_markup_translations_are_rejected(self):
        expected = locales.catalogue('<p data-i18n="home.text">Read <a href="docs.html">the guide</a>.</p>')
        source = expected["home.text"]
        good = {"source": source["source"], "source_sha256": source["source_sha256"],
                "translation": 'Lee <a href="docs.html">la guía</a>.'}
        locales.validate_messages(expected, {"home.text": good}, "es")
        for messages in ({}, {"home.text": {**good, "source_sha256": "old"}},
                         {"home.text": {**good, "translation": ""}},
                         {"home.text": {**good, "translation": '<a href="https://other.example">Click</a>'}}):
            with self.subTest(messages=messages), self.assertRaises(ValueError):
                locales.validate_messages(expected, messages, "es")

    def test_attribute_text_is_escaped_and_nested_blocks_are_rejected(self):
        source = '<img alt="English" data-i18n-attrs="alt:home.alt">'
        result = locales.translate(source, {"home.alt": {"translation": '\"><script>alert(1)</script>&'}})
        doc = locales.Document(result)
        self.assertEqual(len(doc.nodes), 1)
        self.assertEqual(doc.nodes[0]["attrs"]["alt"], '\"><script>alert(1)</script>&')
        with self.assertRaisesRegex(ValueError, "Nested"):
            locales.catalogue('<p data-i18n="home.p"><span data-i18n="home.span">Text</span></p>')


class PublishedLanguageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.files = publisher.public_files()
        cls.config = locales.configuration(publisher.SITE)
        cls.pages = {path: locales.Document(data.decode()) for path, data in cls.files.items() if path.endswith(".html")}

    def test_only_complete_language_pages_are_published(self):
        self.assertEqual({locale["code"] for locale in self.config["locales"]}, {"en", "es", "de", "ja", "fr", "pt"})
        self.assertEqual(len(self.pages), 15)
        self.assertTrue(all("data-i18n" not in doc.source for doc in self.pages.values()))
        self.assertFalse(any(path.startswith("i18n/") for path in self.files))
        self.assertNotIn("es/docs.html", self.files)
        english = locales.catalogue((publisher.SITE / "index.html").read_text())
        for code in ["es", "de", "ja", "fr", "pt"]:
            messages = json.loads((publisher.SITE / f"i18n/{code}.json").read_text())["messages"]
            locales.validate_messages(english, messages, code)
            html = self.files[f"{code}/index.html"].decode()
            self.assertIn(messages["home.m010"]["translation"], html)
            self.assertNotIn(english["home.m010"]["source"], html)

    def test_canonicals_hreflang_and_social_locales_are_reciprocal(self):
        expected = {locale["code"]: self.config["origin"] + locales.page_path("index.html", locale["code"])
                    for locale in self.config["locales"]}
        expected["x-default"] = self.config["origin"] + "/"
        for locale in self.config["locales"]:
            code = locale["code"]
            doc = self.pages["index.html" if code == "en" else f"{code}/index.html"]
            head = [node["attrs"] for node in doc.nodes if node["tag"] in ("link", "meta")]
            self.assertEqual({a["hreflang"]: a["href"] for a in head if a.get("rel") == "alternate"}, expected)
            self.assertEqual([a["href"] for a in head if a.get("rel") == "canonical"], [expected[code]])
            self.assertEqual([a["content"] for a in head if a.get("property") == "og:url"], [expected[code]])
            self.assertEqual([a["content"] for a in head if a.get("property") == "og:locale"], [locale["og_locale"]])
            self.assertEqual([n["attrs"]["lang"] for n in doc.nodes if n["tag"] == "html"], [code])
            structured = [json.loads(doc.source[n["inside"]:n["stop"]]) for n in doc.nodes if n["tag"] == "script" and n["attrs"].get("type") == "application/ld+json"]
            self.assertEqual(structured[0]["url"], expected[code])
            self.assertEqual(structured[0]["inLanguage"], code)
        self.assertFalse(any(n["attrs"].get("hreflang") for n in self.pages["docs.html"].nodes if n["tag"] == "link"))

    def test_sitemap_contains_canonical_pages_without_english_duplicates(self):
        root = ET.fromstring(self.files["sitemap.xml"])
        urls = [node.text for node in root.iter("{http://www.sitemaps.org/schemas/sitemap/0.9}loc")]
        self.assertEqual(len(urls), 15)
        self.assertEqual(len(set(urls)), len(urls))
        self.assertIn(f"{self.config['origin']}/ja/", urls)
        self.assertNotIn(f"{self.config['origin']}/index.html", urls)
        self.assertFalse(any(url.endswith("/docs.html") and "/es/" in url for url in urls))

    def test_generated_navigation_assets_and_fragments_resolve(self):
        ids = {path: {n["attrs"]["id"] for n in doc.nodes if "id" in n["attrs"]} for path, doc in self.pages.items()}
        for name, doc in self.pages.items():
            page_ids = [n["attrs"]["id"] for n in doc.nodes if "id" in n["attrs"]]
            self.assertEqual(len(page_ids), len(set(page_ids)), name)
            for node in doc.nodes:
                for field in ("href", "src"):
                    value = node["attrs"].get(field, "")
                    url = urlsplit(value)
                    if not value or url.scheme or url.netloc:
                        continue
                    if not url.path:
                        path = name
                    else:
                        path = unquote(url.path.lstrip("/"))
                        if url.path.endswith("/"):
                            path += "index.html"
                    self.assertIn(path, self.files, (name, value))
                    if url.fragment and path in ids:
                        self.assertIn(unquote(url.fragment), ids[path], (name, value))

    def test_source_edits_require_translation_updates(self):
        source = {name: (publisher.SITE / name).read_bytes() for name in publisher.PUBLIC_FILES}
        source["index.html"] = source["index.html"].replace(b"Rediscover the music you own.", b"Enjoy your music again.")
        with self.assertRaisesRegex(ValueError, "stale translation"):
            locales.build(source, publisher.SITE)
        source["index.html"] = (publisher.SITE / "index.html").read_bytes().replace(b"</main>", b"<p>A new feature.</p></main>")
        with self.assertRaisesRegex(ValueError, "unmarked translation"):
            locales.build(source, publisher.SITE)

    def test_each_page_tracks_only_explicit_public_routes(self):
        expected = {"/", *["/" + name for name in publisher.PUBLIC_FILES if name.endswith(".html")]}
        expected |= {f"/{code}/{suffix}" for code in ("es", "de", "ja", "fr", "pt") for suffix in ("", "index.html")}
        for path, doc in self.pages.items():
            scripts = [node["attrs"] for node in doc.nodes if node["tag"] == "script" and node["attrs"].get("src", "").startswith("/analytics.js?")]
            self.assertEqual(len(scripts), 1, path)
            self.assertEqual(set(scripts[0]["data-paths"].split(",")), expected, path)


if __name__ == "__main__":
    unittest.main()
