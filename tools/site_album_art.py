"""Fetch the pinned real album covers used by website screenshot fixtures."""
import hashlib
import json
from pathlib import Path
import urllib.request

CATALOG = Path(__file__).with_name("site_album_art.json")
CACHE = Path("/Volumes/Scratch/SnowskyEcho/tmp/site-refresh/art")


def albums():
    if not Path("/Volumes/Scratch").is_mount():
        raise ValueError("Mount Scratch before preparing screenshot artwork")
    CACHE.mkdir(parents=True, exist_ok=True)
    catalog = json.loads(CATALOG.read_text())
    result = []
    for album in catalog["albums"]:
        path = CACHE / (album["sha256"] + ".jpg")
        data = path.read_bytes() if path.is_file() else None
        if data is None:
            with urllib.request.urlopen(album["artwork"], timeout=30) as response:
                data = response.read(5 * 1024 * 1024 + 1)
        if len(data) > 5 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != album["sha256"]:
            raise ValueError("Screenshot artwork changed: " + album["title"])
        if not data.startswith(b"\xff\xd8"):
            raise ValueError("Expected JPEG album artwork")
        if not path.is_file():
            path.write_bytes(data)
        result.append({**album, "cover": str(path)})
    return result


if __name__ == "__main__":
    print(json.dumps(albums()))
