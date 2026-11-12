import decimal
import re
import struct
from functools import cache

from fnv import fnv1a

KEYS = ("replaygain_track_gain", "replaygain_album_gain",
        "replaygain_track_peak", "replaygain_album_peak")
SPACE = " \t\n\r\v\f"
NUMBER = re.compile(r"([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE]([+-]?[0-9]+))?)(?:[ \t\n\r\v\f]*([dD][bB]))?")
UNITY = 65536
PEAK_SCALE = 1 << 24
MAXIMUM = (1 << 32) - 1


@cache
def gain_powers():
    with decimal.localcontext() as context:
        context.prec = 60
        return tuple(tuple(int((decimal.Decimal(10) **
                                (decimal.Decimal(sign * (1 << bit)) / 2000) * (1 << 32))
                               .to_integral_value(rounding=decimal.ROUND_HALF_EVEN))
                           for bit in range(14)) for sign in (1, -1))


def amplitude(centi_db):
    if not -9600 <= centi_db <= 9600:
        return 0
    gain = UNITY
    amount = abs(centi_db)
    for bit, factor in enumerate(gain_powers()[centi_db < 0]):
        if amount & (1 << bit):
            gain = (gain * factor + (1 << 31)) >> 32
    return max(gain, 1)


def parse_number(text, gain):
    if not isinstance(text, str) or not text.isascii() or not 0 < len(text) <= 64:
        return None
    match = NUMBER.fullmatch(text.strip(SPACE))
    if match is None or (match[3] is not None and not gain) or abs(int(match[2] or 0)) > 99:
        return None
    with decimal.localcontext() as context:
        context.prec = 80
        value = decimal.Decimal(match[1])
        if not gain and value < 0:
            return None
        mode = decimal.ROUND_HALF_UP if gain else decimal.ROUND_CEILING
        fixed = int((value * (100 if gain else PEAK_SCALE)).to_integral_value(rounding=mode))
    limit = 9600 if gain else MAXIMUM
    return fixed if -limit <= fixed <= limit else None


def from_tags(tags):
    parsed = []
    for index, key in enumerate(KEYS):
        values = (parse_number(value, index < 2) for value in tags.get(key, []))
        parsed.append(next((value for value in values if value is not None), None))
    gains = [amplitude(value) if value is not None else 0 for value in parsed[:2]]
    safe = []
    for index, gain in enumerate(gains):
        peak = parsed[2 + index]
        peak = parsed[3 - index] if peak is None else peak
        limit = UNITY if peak is None else MAXIMUM if peak == 0 else (1 << 40) // peak
        safe.append(min(gain, limit))
    return tuple(gains + safe)


def parse_r128(text):
    if not isinstance(text, str) or re.fullmatch(r"[+-]?[0-9]{1,6}", text) is None or len(text) > 6:
        return None
    value = int(text)
    if not -32768 <= value <= 32767:
        return None
    scaled = (value + 5 * 256) * 100
    fixed = (abs(scaled) + 128) // 256 * (-1 if scaled < 0 else 1)
    return fixed if -9600 <= fixed <= 9600 else None


def encode_extension(gains, fingerprint):
    if not any(track or album for track, album, _, _ in gains):
        return b""
    header = struct.pack("<4sHHII", b"DSRG", 1, 20, len(gains), fingerprint)
    result = bytearray(header + struct.pack("<I", fnv1a(header)))
    for index, gain in enumerate(gains):
        record = struct.pack("<IIII", *gain)
        seed = fnv1a(struct.pack("<II", fingerprint, index))
        result += record + struct.pack("<I", fnv1a(record, seed))
    return bytes(result)


def tag_values(tags):
    from mutagen.apev2 import APEv2
    from mutagen.asf import ASFTags
    from mutagen.id3 import ID3
    from mutagen.mp4 import MP4Tags
    from mutagen.oggopus import OggOpusVComment
    values = {}
    if isinstance(tags, ID3):
        items = ((frame.desc, list(frame.text[:1])) for frame in tags.getall("TXXX"))
    elif isinstance(tags, MP4Tags):
        prefixes = ("----:com.apple.iTunes:", "----:org.hydrogenaudio.replaygain:")
        items = ((key[len(prefix):], [bytes(value).decode("ascii", errors="replace")
                                     for value in data if getattr(value, "dataformat", 1) == 1])
                 for key, data in tags.items() for prefix in prefixes if key.startswith(prefix))
    elif isinstance(tags, APEv2):
        items = ((key, list(value)) for key, value in tags.items()
                 if value.kind == 0 and len(list(value)) == 1)
    elif isinstance(tags, ASFTags):
        items = ((key, [value.value for value in data if isinstance(value.value, str)])
                 for key, data in tags.items())
    else:
        items = tags.items()
    for key, data in items:
        if key.isascii() and key.lower() in KEYS:
            values.setdefault(key.lower(), []).extend(data)
    if isinstance(tags, OggOpusVComment):
        for index, key in enumerate(("r128_track_gain", "r128_album_gain")):
            fixed = next((gain for value in tags.get(key, [])
                          if (gain := parse_r128(value)) is not None), None)
            if fixed is not None:
                text = f"{'-' if fixed < 0 else ''}{abs(fixed) // 100}.{abs(fixed) % 100:02d}"
                values.setdefault(KEYS[index], []).append(text)
    return values
