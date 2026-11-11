"""Regenerate branded link-preview PNGs and social metadata for the static site."""
import hashlib
from html import escape
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "site"
SCRATCH = Path("/Volumes/Scratch/SnowskyEcho/tmp/site-social")
IDENTITY = json.loads((ROOT / "firmware/identity.json").read_text())
MANAGED = {
    "og:site_name", "og:locale", "og:image", "og:image:secure_url", "og:image:type",
    "og:image:width", "og:image:height", "og:image:alt", "twitter:card",
    "twitter:title", "twitter:description", "twitter:image", "twitter:image:alt",
}


class Metadata(HTMLParser):
    def __init__(self):
        super().__init__()
        self.values = {}

    def handle_starttag(self, tag, attributes):
        attributes = dict(attributes)
        if tag == "meta":
            key = attributes.get("property", attributes.get("name"))
            if key:
                self.values[key] = attributes.get("content", "")


def card_svg(card, artwork, domain):
    title = "".join(f'<text x="64" y="{260 + i * 78}" font-size="66" font-weight="900" letter-spacing="-2">{escape(line)}</text>'
                    for i, line in enumerate(card["title"]))
    description = "".join(f'<text x="66" y="{418 + i * 34}" font-size="24" fill="#b9acc8">{escape(line)}</text>'
                          for i, line in enumerate(card["description"]))
    illustration = artwork.replace('<svg ', '<svg x="625" y="64" width="560" height="467" ', 1)
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="630" viewBox="0 0 1200 630">
<defs><radialGradient id="backdrop"><stop stop-color="#34203f"/><stop offset="1" stop-color="#150f25"/></radialGradient></defs>
<rect width="1200" height="630" fill="#150f25"/>
<ellipse cx="940" cy="300" rx="500" ry="420" fill="url(#backdrop)"/>
<rect x="20.5" y="20.5" width="1159" height="589" rx="2" fill="none" stroke="#45334f"/>
<g fill="#f4edde" font-family="Avenir Next, Arial, sans-serif">
<text x="64" y="100" font-size="36" font-weight="900" letter-spacing="1">{escape(IDENTITY["name"].upper())}<tspan fill="#ff88bd">.</tspan></text>
<text x="66" y="161" font-family="monospace" font-size="16" fill="#a1edda" letter-spacing="1.5">{escape(card["label"].upper())}</text>
{title}{description}
</g>{illustration}
<path d="M64 540H1136" stroke="#45334f"/>
<text x="64" y="586" font-family="monospace" font-size="22" fill="#f4edde" letter-spacing="1">{escape(domain)}</text>
<text x="1136" y="586" text-anchor="end" font-family="monospace" font-size="14" fill="#b9acc8" letter-spacing="2">LESS SCROLLING. MORE LISTENING.</text>
</svg>'''.encode()


def metadata_tags(source, card, url):
    metadata = Metadata()
    metadata.feed(source)
    values = metadata.values
    for key in ("og:title", "og:description", "og:type", "og:url"):
        if not values.get(key):
            raise ValueError(f'{card["page"]} needs {key}')
    alt = f'{IDENTITY["name"]} cassette artwork beside the words “{" ".join(card["title"])}”'
    properties = {
        "og:site_name": IDENTITY["name"], "og:locale": "en_US", "og:image": url,
        "og:image:secure_url": url, "og:image:type": "image/png",
        "og:image:width": "1200", "og:image:height": "630", "og:image:alt": alt,
    }
    names = {
        "twitter:card": "summary_large_image", "twitter:title": values["og:title"],
        "twitter:description": values["og:description"], "twitter:image": url,
        "twitter:image:alt": alt,
    }
    return "\n".join(f'<meta {attribute}="{key}" content="{escape(value, quote=True)}">'
                     for attribute, fields in (("property", properties), ("name", names))
                     for key, value in fields.items())


def update_page(source, tags):
    head, separator, body = source.partition("</head>")
    if not separator:
        raise ValueError("Page has no closing head tag")
    def remove_managed(match):
        metadata = Metadata()
        metadata.feed(match[0])
        return "" if MANAGED.intersection(metadata.values) else match[0]
    head = re.sub(r"<meta\b[^>]*>\n?", remove_managed, head)
    source = head.rstrip() + "\n" + tags + "\n</head>" + body
    if 'prefix="og: https://ogp.me/ns#"' not in source:
        source = source.replace('<html lang="en">', '<html lang="en" prefix="og: https://ogp.me/ns#">', 1)
    return source


def main():
    if not Path("/Volumes/Scratch").is_mount():
        raise SystemExit("Mount /Volumes/Scratch before rendering social images")
    renderer = shutil.which("rsvg-convert")
    if not renderer:
        raise SystemExit("Install librsvg to provide rsvg-convert")
    config = (ROOT / "tools/site_social.json").read_text().replace("{firmware_name}", json.dumps(IDENTITY["name"])[1:-1])
    cards = json.loads(config)["cards"]
    if len({card["page"] for card in cards}) != len(cards) or {card["page"] for card in cards} != {path.name for path in SITE.glob("*.html")}:
        raise ValueError("Configure exactly one card per public HTML page")
    if len({card["name"] for card in cards}) != len(cards):
        raise ValueError("Card names must be unique")
    home = (SITE / "index.html").read_text()
    metadata = Metadata()
    metadata.feed(home)
    base_url = metadata.values["og:url"]
    domain = urlsplit(base_url).netloc
    artwork = re.search(r'<div class="art"[^>]*>\s*(<svg\b.*?</svg>)', home, re.S)[1]
    output = SITE / "assets/social"
    SCRATCH.mkdir(parents=True, exist_ok=True)
    rendered = []
    with tempfile.TemporaryDirectory(prefix="render-", dir=SCRATCH) as work:
        for card in cards:
            if not re.fullmatch(r"[a-z]+(?:-[a-z]+)*", card["name"]):
                raise ValueError("Card names must be lowercase words separated by hyphens")
            png = subprocess.run([renderer, "--format", "png"], input=card_svg(card, artwork, domain),
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True, timeout=30,
                                 env={**os.environ, "TMPDIR": work}).stdout
            if png[:8] != b"\x89PNG\r\n\x1a\n" or struct.unpack_from(">II", png, 16) != (1200, 630):
                raise ValueError("Expected a 1200 by 630 PNG")
            name = card["name"] + "-" + hashlib.sha256(png).hexdigest()[:12] + ".png"
            relative = "assets/social/" + name
            source = (SITE / card["page"]).read_text()
            page = update_page(source, metadata_tags(source, card, base_url + relative))
            rendered.append((card, name, png, page))
    output.mkdir(parents=True, exist_ok=True)
    for card, name, png, page in rendered:
        (output / name).write_bytes(png)
        (SITE / card["page"]).write_text(page)
        for prior in output.glob(card["name"] + "-*.png"):
            if prior.name != name and re.fullmatch(re.escape(card["name"]) + r"-[0-9a-f]{12}\.png", prior.name):
                prior.unlink()
    print(json.dumps({"images": ["assets/social/" + name for _, name, _, _ in rendered], "size": [1200, 630]}))


if __name__ == "__main__":
    main()
