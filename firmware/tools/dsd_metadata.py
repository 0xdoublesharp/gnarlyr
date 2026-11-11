import struct


def read_at(handle, offset, count):
    handle.seek(offset)
    return handle.read(count)


def chunks(handle, start, end):
    while end - start >= 12:
        header = read_at(handle, start, 12)
        if len(header) != 12:
            return
        tag, count = struct.unpack(">4sQ", header)
        body = start + 12
        if count > end - body or (count & 1) > end - body - count:
            return
        yield tag, body, body + count
        start = body + count + (count & 1)


def diin_tags(handle):
    handle.seek(0, 2)
    size = handle.tell()
    header = read_at(handle, 0, 16)
    if len(header) != 16 or header[:4] != b"FRM8" or header[12:] != b"DSD ":
        return {}
    end = struct.unpack_from(">Q", header, 4)[0] + 12
    if end > size:
        return {}
    values = {}
    for tag, body, limit in chunks(handle, 16, end):
        if tag != b"DIIN":
            continue
        for field, text, stop in chunks(handle, body, limit):
            if field not in (b"DIAR", b"DITI") or stop - text < 4:
                continue
            length_bytes = read_at(handle, text, 4)
            if len(length_bytes) != 4:
                continue
            count = struct.unpack(">I", length_bytes)[0]
            if count <= stop - text - 4:
                value = read_at(handle, text + 4, min(count, 256)).decode("latin-1").strip("\x00 \t\r\n")
                values.setdefault("artist" if field == b"DIAR" else "title", value)
    return values
