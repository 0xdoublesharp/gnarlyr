import struct

INDEXED = 1
LOSSLESS = 2
OPAQUE = 15
PALETTE_MAX = 256
ALPHA_ENTRIES_MAX = 255
ROW_MAX_BYTES = 1024
RUN_MAX = 128
LOSSLESS_PACKET_MAX = 64
SKIP_MAX = 32
MATRIX = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))
SPREAD_MASK = 0x07E0F81F
SPREAD_ROUNDING = 7 << 21 | 7 << 11 | 7


def rgb565(red, green, blue):
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def alpha4(alpha):
    return (alpha * OPAQUE + 127) // 255


def fifteenth(value):
    return value * 0x8889 >> 19


def blend(under, over, alpha):
    weight = min(alpha, OPAQUE)
    spread = lambda color: (color | color << 16) & SPREAD_MASK
    mixed = spread(under) * (OPAQUE - weight) + spread(over) * weight + SPREAD_ROUNDING
    blue = fifteenth(mixed & 0x7FF)
    red = fifteenth(mixed >> 11 & 0x3FF)
    green = fifteenth(mixed >> 21 & 0x7FF)
    return (red << 11 | green << 5 | blue) & 0xFFFF


def put(under, color, alpha):
    if alpha >= OPAQUE:
        return color
    return under if alpha == 0 else blend(under, color, alpha)


def encode_indexed_row(indices):
    out = bytearray()
    literal = []

    def flush():
        while literal:
            chunk = literal[:RUN_MAX]
            del literal[:RUN_MAX]
            out.append(len(chunk) - 1)
            out.extend(chunk)

    at = 0
    while at < len(indices):
        run = 1
        while at + run < len(indices) and indices[at + run] == indices[at] and run < RUN_MAX:
            run += 1
        if run >= 3:
            flush()
            out += bytes((0x80 | (run - 1), indices[at]))
            at += run
        else:
            literal.append(indices[at])
            at += 1
    flush()
    return bytes(out)


def decode_indexed_row(data, width, colors, alphas, under):
    pixels = list(under)
    at = x = 0
    while x < width:
        if at >= len(data):
            raise ValueError("indexed row ends early")
        packet = data[at]
        at += 1
        count = (packet & 0x7F) + 1
        size = 1 if packet & 0x80 else count
        if count > width - x or len(data) - at < size:
            raise ValueError("indexed packet overruns its row")
        for offset in range(count):
            index = data[at] if packet & 0x80 else data[at + offset]
            if index >= len(colors):
                raise ValueError("palette index out of range")
            pixels[x + offset] = put(pixels[x + offset], colors[index], alphas[index])
        at += size
        x += count
    return pixels, at


def split(color):
    return color >> 11, (color >> 5) & 63, color & 31


def wrapped(delta, bits):
    size = 1 << bits
    delta %= size
    return delta - size if delta >= size // 2 else delta


def delta_byte(previous, value):
    pr, pg, pb = split(previous)
    r, g, b = split(value)
    dr, dg, db = wrapped(r - pr, 5), wrapped(g - pg, 6), wrapped(b - pb, 5)
    half = dg >> 1
    er, eb = dr - half, db - half
    if -8 <= dg <= 7 and -2 <= er <= 1 and -2 <= eb <= 1:
        return (dg + 8) << 4 | (er + 2) << 2 | (eb + 2)
    return None


def add_delta(previous, delta):
    half = delta >> 5
    pr, pg, pb = split(previous)
    red = (pr + half + ((delta >> 2) & 3) - 6) & 31
    green = (pg + (delta >> 4) - 8) & 63
    blue = (pb + half + (delta & 3) - 6) & 31
    return red << 11 | green << 5 | blue


KIND_COST = {"run": 0, "delta": 1, "literal": 2, "skip": 0}
KIND_BASE = {"run": 0x00, "delta": 0x40, "literal": 0x80, "skip": 0xC0}


def pixel_kinds(previous, color, alpha):
    if alpha == 0:
        return ("skip",)
    kinds = ["literal"]
    if color == previous:
        kinds.append("run")
    if delta_byte(previous, color) is not None:
        kinds.append("delta")
    return tuple(kinds)


def header_cost(kind, alpha):
    return 1 if kind == "skip" or alpha == OPAQUE else 2


def cheapest_kinds(colors, alphas):
    previous, best, steps = 0, {}, []
    for at, (color, alpha) in enumerate(zip(colors, alphas)):
        choices, back = {}, {}
        for kind in pixel_kinds(previous, color, alpha):
            options = [(cost + (0 if kind == last and alphas[at - 1] == alpha else
                                header_cost(kind, alpha)), last) for last, cost in best.items()]
            cost, last = min(options) if options else (header_cost(kind, alpha), None)
            choices[kind], back[kind] = cost + KIND_COST[kind], last
        steps.append(back)
        best = choices
        if alpha:
            previous = color
    kind = min(best, key=best.get)
    kinds = []
    for back in reversed(steps):
        kinds.append(kind)
        kind = back[kind]
    return kinds[::-1]


def encode_lossless_row(colors, alphas=None):
    alphas = alphas or [OPAQUE] * len(colors)
    out = bytearray()
    kinds = cheapest_kinds(colors, alphas)
    at = previous = 0
    while at < len(colors):
        kind, alpha = kinds[at], alphas[at]
        limit = SKIP_MAX if kind == "skip" else LOSSLESS_PACKET_MAX
        end = at + 1
        while (end < len(colors) and end - at < limit and kinds[end] == kind
               and alphas[end] == alpha):
            end += 1
        if kind != "skip" and alpha < OPAQUE:
            out.append(0xE0 + alpha)
        out.append(KIND_BASE[kind] | (end - at - 1))
        for color in colors[at:end]:
            if kind == "delta":
                out.append(delta_byte(previous, color))
            elif kind == "literal":
                out += struct.pack("<H", color)
            if kind != "skip":
                previous = color
        at = end
    return bytes(out)


def decode_lossless_row(data, width, under, alpha_allowed):
    pixels = list(under)
    at = x = previous = 0
    alpha = OPAQUE
    while x < width:
        if at >= len(data):
            raise ValueError("lossless row ends early")
        packet = data[at]
        at += 1
        if packet >= 0xE0:
            if not alpha_allowed or packet == 0xE0 or packet > 0xEE or alpha != OPAQUE:
                raise ValueError("misplaced or reserved alpha prefix")
            alpha = packet - 0xE0
            continue
        count = (packet & (0x3F if packet < 0xC0 else 0x1F)) + 1
        size = (0, 1, 2, 0)[packet >> 6] * count
        if packet >= 0xC0 and (not alpha_allowed or alpha != OPAQUE):
            raise ValueError("transparency where it is not allowed")
        if count > width - x or len(data) - at < size:
            raise ValueError("lossless packet overruns its row")
        for offset in range(count):
            if packet < 0x40:
                pixels[x + offset] = put(pixels[x + offset], previous, alpha)
            elif packet < 0x80:
                previous = add_delta(previous, data[at + offset])
                pixels[x + offset] = put(pixels[x + offset], previous, alpha)
            elif packet < 0xC0:
                previous = data[at + 2 * offset] | data[at + 2 * offset + 1] << 8
                pixels[x + offset] = put(pixels[x + offset], previous, alpha)
        alpha = OPAQUE
        at += size
        x += count
    return pixels, at


def ordered(rows):
    result = []
    for y, row in enumerate(rows):
        line = []
        for x, (red, green, blue, alpha) in enumerate(row):
            adjust = MATRIX[y % 4][x % 4] - 8
            line.append((max(0, min(255, red + adjust // 2)), max(0, min(255, green + adjust // 4)),
                         max(0, min(255, blue + adjust // 2)), alpha))
        result.append(line)
    return result


def reduced(rows):
    return [[(rgb565(red, green, blue) if alpha4(alpha) else 0, alpha4(alpha))
             for red, green, blue, alpha in row] for row in rows]


def ordered_palette(pairs):
    translucent = sorted(pair for pair in pairs if pair[1] < OPAQUE)
    opaque = sorted(pair for pair in pairs if pair[1] == OPAQUE)
    if len(translucent) > ALPHA_ENTRIES_MAX:
        raise ValueError("more than 255 translucent palette entries")
    entries = translucent + opaque
    return entries, {pair: index for index, pair in enumerate(entries)}, len(translucent)


def quantised(image, dither):
    from PIL import Image
    has_alpha = image.mode == "RGBA" and image.getextrema()[3][0] < 255
    method = Image.Quantize.FASTOCTREE if has_alpha else Image.Quantize.MEDIANCUT
    mode = Image.Dither.FLOYDSTEINBERG if dither == "fs" else Image.Dither.NONE
    source = image if has_alpha else image.convert("RGB")
    result = source.quantize(colors=PALETTE_MAX - (1 if has_alpha else 0), method=method,
                             dither=mode)
    raw = result.getpalette(rawmode="RGBA" if has_alpha else "RGB")
    step = 4 if has_alpha else 3
    entries = [tuple(raw[at:at + step]) + ((255,) if not has_alpha else ())
               for at in range(0, len(raw), step)]
    width = image.width
    indices = list(result.getdata())
    return [[reduced([[entries[index]]])[0][0] for index in indices[y * width:(y + 1) * width]]
            for y in range(image.height)]


def palettize(rows):
    pairs = {pair for row in rows for pair in row}
    if len(pairs) > PALETTE_MAX:
        raise ValueError("more than 256 colour and alpha pairs")
    entries, lookup, translucent = ordered_palette(pairs)
    return entries, translucent, [[lookup[pair] for pair in row] for row in rows]


def image_rows(image):
    rgba = image.convert("RGBA")
    data = list(rgba.getdata())
    return [data[y * image.width:(y + 1) * image.width] for y in range(image.height)]


def prepared(image, codec, dither):
    if dither not in ("none", "ordered", "fs") or (codec == LOSSLESS and dither == "fs"):
        raise ValueError("dither must be none, ordered or fs (indexed only)")
    rows = image_rows(image)
    if dither == "ordered":
        rows = ordered(rows)
    pixels = reduced(rows)
    if codec == INDEXED and len({pair for row in pixels for pair in row}) > PALETTE_MAX:
        from PIL import Image
        flat = Image.new("RGBA", image.size)
        flat.putdata([pixel for row in rows for pixel in row])
        pixels = quantised(flat, dither)
    return pixels


def payload(pixels, codec, frames=1):
    height, width = len(pixels), len(pixels[0])
    alpha_present = any(alpha < OPAQUE for row in pixels for _, alpha in row)
    if codec == INDEXED:
        entries, translucent, indices = palettize(pixels)
        head = bytes((INDEXED, len(entries) - 1, translucent, frames))
        nibbles = bytearray((translucent + 1) // 2)
        for index in range(translucent):
            nibbles[index >> 1] |= entries[index][1] << (4 * (index & 1))
        prefix = head + b"".join(struct.pack("<H", color) for color, _ in entries) + bytes(nibbles)
        rows = [encode_indexed_row(row) for row in indices]
    elif codec == LOSSLESS:
        prefix = bytes((LOSSLESS, 0, int(alpha_present), frames))
        rows = [encode_lossless_row([color for color, _ in row], [alpha for _, alpha in row])
                for row in pixels]
    else:
        raise ValueError("unknown codec")
    if max(len(row) for row in rows) > ROW_MAX_BYTES:
        raise ValueError("an encoded row exceeds 1,024 bytes; choose the indexed codec")
    offset = len(prefix) + 4 * (height + 1)
    table = []
    for row in rows:
        table.append(offset)
        offset += len(row)
    table.append(offset)
    return prefix + struct.pack("<%dI" % len(table), *table) + b"".join(rows)


def parse(data, width, height):
    codec, entries_less_one, alpha, frames = data[:4]
    if codec == INDEXED:
        entries = entries_less_one + 1
        colors = list(struct.unpack_from("<%dH" % entries, data, 4))
        nibbles = data[4 + 2 * entries:4 + 2 * entries + (alpha + 1) // 2]
        alphas = [nibbles[index >> 1] >> (4 * (index & 1)) & 15 if index < alpha else OPAQUE
                  for index in range(entries)]
        table_at = 4 + 2 * entries + (alpha + 1) // 2
    elif codec == LOSSLESS:
        colors, alphas, table_at = None, None, 4
    else:
        raise ValueError("unknown codec")
    table = struct.unpack_from("<%dI" % (height + 1), data, table_at)
    return {"codec": codec, "alpha": alpha, "frames": frames, "colors": colors, "alphas": alphas,
            "table": table, "width": width, "height": height}


def decode(data, width, height, under):
    info = parse(data, width, height)
    table = info["table"]
    result = []
    for y in range(height):
        row = data[table[y]:table[y + 1]]
        if info["codec"] == INDEXED:
            pixels, used = decode_indexed_row(row, width, info["colors"], info["alphas"], under[y])
        else:
            pixels, used = decode_lossless_row(row, width, under[y], bool(info["alpha"]))
        if used != len(row):
            raise ValueError("trailing bytes in a row")
        result.append(pixels)
    return result
