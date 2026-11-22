"""Build static language variants from the English HTML and reviewed catalogues."""
import argparse
import hashlib
from html import escape, unescape
from html.parser import HTMLParser
import json
from pathlib import Path
import re
from urllib.parse import urlsplit, urlunsplit

from echo_debug import IDENTITY

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "site"
SITE_ORIGIN = f"https://{IDENTITY['name'].lower()}.com"
VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr"}
SKIP = {"svg", "script", "style", "code", "pre"}
BLOCKS = {"title", "h1", "h2", "h3", "p", "li", "dt", "dd", "figcaption", "a", "button", "label", "summary", "span", "strong"}
META = {"description", "og:title", "og:description", "og:image:alt", "twitter:title", "twitter:description", "twitter:image:alt"}


class Document(HTMLParser):
    """Keep source positions so rendering never reformats the original markup."""
    def __init__(self, source):
        super().__init__(convert_charrefs=False)
        self.source, self.nodes, self.stack = source, [], []
        self.lines = [0] + [match.end() for match in re.finditer("\n", source)]
        self.feed(source)
        if self.stack:
            raise ValueError("Unclosed HTML element: " + self.stack[-1]["tag"])

    def position(self):
        line, column = self.getpos()
        return self.lines[line - 1] + column

    def handle_starttag(self, tag, attrs):
        start = self.position()
        node = {"tag": tag, "attrs": dict(attrs), "start": start,
                "inside": start + len(self.get_starttag_text()), "parent": self.stack[-1] if self.stack else None}
        self.nodes.append(node)
        if tag in VOID:
            node.update(stop=node["inside"], end=node["inside"])
        else:
            self.stack.append(node)

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in VOID:
            self.stack.pop().update(stop=self.position() + len(self.get_starttag_text()),
                                    end=self.position() + len(self.get_starttag_text()))

    def handle_endtag(self, tag):
        if tag in VOID:
            return
        if not self.stack or self.stack[-1]["tag"] != tag:
            raise ValueError("Unbalanced HTML closing tag: " + tag)
        self.stack.pop().update(stop=self.position(), end=self.source.index(">", self.position()) + 1)


def patch(source, replacements):
    for start, end, value in sorted(replacements, reverse=True):
        source = source[:start] + value + source[end:]
    return source


def hidden(node):
    while node:
        attrs = node["attrs"]
        if node["tag"] in SKIP or attrs.get("aria-hidden") == "true":
            return True
        node = node["parent"]
    return False


def annotate(source, prefix):
    """Assign stable IDs once; subsequent edits keep the IDs in the source HTML."""
    doc, edits, selected = Document(source), [], set()
    existing = re.findall(r"\b" + re.escape(prefix) + r"\.([ma])([0-9]+)", source)
    counters = {kind: max([int(n) for k, n in existing if k == kind], default=0) for kind in "ma"}
    for node in doc.nodes:
        if hidden(node):
            continue
        attrs, additions = node["attrs"], []
        parent, covered = node["parent"], False
        while parent:
            covered |= parent["start"] in selected or "data-i18n" in parent["attrs"]
            parent = parent["parent"]
        text = unescape(re.sub(r"<[^>]*>", "", source[node["inside"]:node["stop"]]))
        brand = node
        branded = False
        while brand:
            branded |= bool({"brand", "wordmark"}.intersection(brand["attrs"].get("class", "").split()))
            brand = brand["parent"]
        if node["tag"] in BLOCKS and not covered and not branded and any(char.isalnum() for char in text):
            selected.add(node["start"])
            if "data-i18n" not in attrs:
                counters["m"] += 1
                additions.append(f'data-i18n="{prefix}.m{counters["m"]:03}"')
        if "data-i18n-attrs" not in attrs:
            fields = [name for name in ("alt", "aria-label", "title") if attrs.get(name)]
            if node["tag"] == "meta" and attrs.get("property", attrs.get("name")) in META:
                fields.append("content")
            entries = []
            for field in fields:
                counters["a"] += 1
                entries.append(f'{field}:{prefix}.a{counters["a"]:03}')
            if entries:
                additions.append('data-i18n-attrs="' + "|".join(entries) + '"')
        if additions:
            end = node["inside"] - (2 if source[node["inside"] - 2:node["inside"]] == "/>" else 1)
            edits.append((end, end, " " + " ".join(additions)))
    return patch(source, edits)


def catalogue(source):
    result = {}
    for node in Document(source).nodes:
        attrs = node["attrs"]
        entries = []
        if "data-i18n" in attrs:
            parent = node["parent"]
            while parent:
                if "data-i18n" in parent["attrs"]:
                    raise ValueError("Nested translation blocks are not supported")
                parent = parent["parent"]
            entries.append((attrs["data-i18n"], source[node["inside"]:node["stop"]], "html"))
        for field in attrs.get("data-i18n-attrs", "").split("|"):
            if field:
                name, key = field.split(":", 1)
                entries.append((key, attrs[name], "attribute"))
        for key, text, kind in entries:
            if not re.fullmatch(r"[a-z][a-z0-9_.-]+", key) or key in result:
                raise ValueError("Invalid or duplicate translation ID: " + key)
            result[key] = {"source": text, "source_sha256": hashlib.sha256(text.encode()).hexdigest(), "kind": kind}
    return result


class Structure(HTMLParser):
    def __init__(self, source):
        super().__init__(convert_charrefs=False)
        self.signature = []
        self.feed(source)

    def handle_starttag(self, tag, attrs):
        self.signature.append(("start", tag, sorted(attrs)))

    def handle_startendtag(self, tag, attrs):
        self.signature.append(("start", tag, sorted(attrs)))

    def handle_endtag(self, tag):
        self.signature.append(("end", tag))

    def handle_comment(self, data):
        self.signature.append(("comment", data))


def validate_messages(expected, messages, code):
    for key, entry in expected.items():
        translated = messages.get(key)
        if not translated:
            raise ValueError(f"{code}: missing translation {key}")
        if translated.get("source") != entry["source"] or translated.get("source_sha256") != entry["source_sha256"]:
            raise ValueError(f"{code}: stale translation {key}")
        text = translated.get("translation")
        if not isinstance(text, str) or not text.strip():
            raise ValueError(f"{code}: empty translation {key}")
        if entry["kind"] == "html" and Structure(text).signature != Structure(entry["source"]).signature:
            raise ValueError(f"{code}: changed protected markup in {key}")


def set_attribute(tag, name, value):
    pattern = r'(\s' + re.escape(name) + r'\s*=\s*)(["\']).*?\2'
    result, count = re.subn(pattern, lambda m: m[1] + '"' + escape(value, quote=True) + '"', tag, count=1, flags=re.S)
    if not count:
        raise ValueError("Missing HTML attribute: " + name)
    return result


def translate(source, messages):
    edits = [(n["inside"], n["stop"], messages[n["attrs"]["data-i18n"]]["translation"])
             for n in Document(source).nodes if "data-i18n" in n["attrs"]]
    source = patch(source, edits)
    edits = []
    for node in Document(source).nodes:
        if "data-i18n-attrs" not in node["attrs"]:
            continue
        tag = source[node["start"]:node["inside"]]
        for field in node["attrs"]["data-i18n-attrs"].split("|"):
            name, key = field.split(":", 1)
            tag = set_attribute(tag, name, messages[key]["translation"])
        edits.append((node["start"], node["inside"], tag))
    return patch(source, edits)


def configuration(site):
    config = json.loads((site / "i18n/locales.json").read_text())
    if config["version"] != 1 or config["origin"] != SITE_ORIGIN:
        raise ValueError("Unsupported site locale configuration")
    codes = [locale["code"] for locale in config["locales"]]
    if len(set(codes)) != len(codes) or codes[0] != "en" or any(not re.fullmatch("[a-z]{2}", code) for code in codes):
        raise ValueError("Locale codes must be unique two-letter codes, with English first")
    if len(set(config["pages"])) != len(config["pages"]) or "index.html" not in config["pages"]:
        raise ValueError("Configure unique localized pages, including index.html")
    for page in config["pages"]:
        if not re.fullmatch("[a-z]+(?:-[a-z]+)*\\.html", page):
            raise ValueError("Localized pages must be root HTML filenames")
    for locale in config["locales"]:
        for name in ("name", "og_locale", "language_label", "home_label", "guide_note"):
            if not isinstance(locale.get(name), str) or (name != "guide_note" and not locale[name]):
                raise ValueError("Missing locale field: " + name)
    return config


def page_path(page, code):
    prefix = "/" if code == "en" else f"/{code}/"
    return prefix if page == "index.html" else prefix + page


def selector(config, locale, page):
    links = []
    for target in config["locales"]:
        translated = target["code"] == "en" or page in config["pages"]
        path = page_path(page if translated else "index.html", target["code"])
        label = escape(target["name"])
        if not translated:
            label += ' <span class="language-home">· ' + escape(locale["home_label"]) + '</span>'
        current = ' aria-current="page"' if target["code"] == locale["code"] else ""
        links.append(f'<a href="{path}" lang="{target["code"]}" hreflang="{target["code"]}"{current}>{label}</a>')
    note = '<p>' + escape(locale["guide_note"]) + '</p>' if locale["guide_note"] else ""
    label = escape(locale["language_label"], quote=True)
    return (f'<div class="site-languages wrap">{note}<details class="language-picker">'
            f'<summary aria-label="{label}: {escape(locale["name"], quote=True)}">{escape(locale["name"])} <span aria-hidden="true">⌄</span></summary>'
            f'<nav aria-label="{label}">{"".join(links)}</nav></details></div>')


def render(source, config, locale, page):
    code, edits = locale["code"], []
    url = config["origin"] + page_path(page, code)
    for node in Document(source).nodes:
        tag, attrs = source[node["start"]:node["inside"]], node["attrs"]
        if node["tag"] == "html":
            tag = set_attribute(tag, "lang", code)
        if node["tag"] == "link" and attrs.get("rel") == "canonical":
            tag = set_attribute(tag, "href", url)
        if node["tag"] == "meta" and attrs.get("property") in ("og:url", "og:locale"):
            tag = set_attribute(tag, "content", url if attrs["property"] == "og:url" else locale["og_locale"])
        # Root-relative assets and navigation work from every language directory.
        for field in ("href", "src"):
            value = attrs.get(field, "")
            parts = urlsplit(value)
            if not value or value.startswith("#") or parts.scheme or parts.netloc:
                continue
            path = parts.path.lstrip("/")
            if parts.path == "/" and field == "href":
                path = "index.html"
            if path in config["pages"]:
                path = page_path(path, code)
            else:
                path = "/" + path
            tag = set_attribute(tag, field, urlunsplit(("", "", path, parts.query, parts.fragment)))
        if tag != source[node["start"]:node["inside"]]:
            edits.append((node["start"], node["inside"], tag))
    source = patch(source, edits)
    source = re.sub(r'\sdata-i18n(?:-attrs)?="[^"]*"', "", source)
    if page in config["pages"]:
        alternates = [f'<link rel="alternate" hreflang="{target["code"]}" href="{config["origin"]}{page_path(page, target["code"])}">' for target in config["locales"]]
        alternates.append(f'<link rel="alternate" hreflang="x-default" href="{config["origin"]}{page_path(page, "en")}">')
        alternates += [f'<meta property="og:locale:alternate" content="{target["og_locale"]}">' for target in config["locales"] if target["code"] != code]
        source = source.replace("</head>", "\n" + "\n".join(alternates) + "\n</head>", 1)
    if page == "index.html":
        metadata = next((node["attrs"]["content"] for node in Document(source).nodes
                         if node["tag"] == "meta" and node["attrs"].get("name") == "description"), "")
        structured = json.dumps({"@context": "https://schema.org", "@type": "WebSite", "name": IDENTITY["name"],
                                 "url": url, "inLanguage": code, "description": metadata}, ensure_ascii=False).replace("<", "\\u003c")
        source = source.replace("</head>", '<script type="application/ld+json">' + structured + '</script>\n</head>', 1)
    source = source.replace("</header>", "</header>" + selector(config, locale, page), 1)
    return source


def build(files, site=SITE):
    config = configuration(site)
    result = dict(files)
    originals = {name: data.decode() for name, data in files.items() if name.endswith(".html")}
    if not set(config["pages"]).issubset(originals):
        raise ValueError("Unknown localized page")
    expected = {}
    for page in config["pages"]:
        prefix = "home" if page == "index.html" else Path(page).stem
        if annotate(originals[page], prefix) != originals[page]:
            raise ValueError(f"{page}: unmarked translation text; run site_locales.py annotate --page {page}")
        entries = catalogue(originals[page])
        if not entries or expected.keys() & entries.keys():
            raise ValueError("Localized pages need distinct translation IDs")
        expected.update(entries)
    routes = ["/", *["/" + page for page in originals]]
    for locale in config["locales"][1:]:
        for page in config["pages"]:
            routes.append(page_path(page, locale["code"]))
            if page == "index.html":
                routes.append(f'/{locale["code"]}/index.html')
        catalog = json.loads((site / f'i18n/{locale["code"]}.json').read_text())
        if catalog.get("version") != 1 or catalog.get("locale") != locale["code"]:
            raise ValueError("Mismatched translation catalogue")
        validate_messages(expected, catalog["messages"], locale["code"])
        for page in config["pages"]:
            path = locale["code"] + "/" + page
            result[path] = render(translate(originals[page], catalog["messages"]), config, locale, page).encode()
    for page, source in originals.items():
        result[page] = render(source, config, config["locales"][0], page).encode()
    for page in [name for name in result if name.endswith(".html")]:
        result[page] = re.sub(rb'\bdata-paths="[^"]*"', lambda _: b'data-paths="' + ",".join(routes).encode() + b'"', result[page])
    urls = [config["origin"] + page_path(page, "en") for page in originals]
    urls += [config["origin"] + page_path(page, locale["code"]) for locale in config["locales"][1:] for page in config["pages"]]
    result["sitemap.xml"] = ('<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n' + "".join(f'  <url><loc>{escape(url)}</loc></url>\n' for url in urls) + '</urlset>\n').encode()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("annotate", "catalogue", "status", "render"))
    parser.add_argument("--page", default="index.html")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        if args.command in ("annotate", "catalogue"):
            if not re.fullmatch(r"[a-z]+(?:-[a-z]+)*\.html", args.page):
                raise ValueError("Choose a root HTML filename")
            path = SITE / args.page
            if args.command == "annotate":
                path.write_text(annotate(path.read_text(), "home" if args.page == "index.html" else path.stem))
                print(json.dumps({"page": args.page, "messages": len(catalogue(path.read_text()))}))
            else:
                print(json.dumps(catalogue(path.read_text()), ensure_ascii=False, indent=2))
        elif args.command == "status":
            config = configuration(SITE)
            expected = {key: value for page in config["pages"] for key, value in catalogue((SITE / page).read_text()).items()}
            problems = []
            for locale in config["locales"][1:]:
                path = SITE / f'i18n/{locale["code"]}.json'
                messages = json.loads(path.read_text())["messages"] if path.exists() else {}
                for key, entry in expected.items():
                    old = messages.get(key)
                    try:
                        validate_messages({key: entry}, messages, locale["code"])
                    except ValueError as error:
                        status = "missing" if not old else "stale" if old.get("source_sha256") != entry["source_sha256"] or old.get("source") != entry["source"] else "invalid"
                        problems.append({"locale": locale["code"], "key": key, "status": status, "source": entry["source"], "source_sha256": entry["source_sha256"], "kind": entry["kind"], "previous_source": old.get("source") if old else None, "detail": str(error)})
            print(json.dumps({"messages_per_language": len(expected), "changes": problems}, ensure_ascii=False, indent=2))
            return int(bool(problems))
        else:
            import publish_site
            output = args.output.resolve() if args.output else None
            scratch = Path("/Volumes/Scratch/SnowskyEcho")
            if not scratch.parent.is_mount() or output is None or not output.is_relative_to(scratch):
                raise ValueError("Render output must be under /Volumes/Scratch/SnowskyEcho")
            for name, data in publish_site.public_files().items():
                target = output / name
                target.parent.mkdir(parents=True, exist_ok=True)
                if target.is_symlink():
                    raise ValueError("Refusing a symlink output")
                target.write_bytes(data)
            print(json.dumps({"output": str(output)}))
        return 0
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
