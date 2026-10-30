#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generates the test images and their reference decodes.

Each <image>.ref holds the expected frames as RGBA:
    FBVREF <width> <height> <frames>\n
    <delay of each frame in ms, 0 when the animation stops>\n
    <frames * width * height * 4 bytes>
PNG, JPEG and GIF references come from Pillow; BMP files are written here
from known pixels. Run it from anywhere: python3 tests/gen_data.py
"""

import os
import struct
import sys
import zlib

from PIL import Image, ImageDraw, ImageSequence

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")


def write_ref(name, size, frames, delays):
    w, h = size
    with open(os.path.join(OUT, name + ".ref"), "wb") as f:
        f.write(f"FBVREF {w} {h} {len(frames)}\n".encode())
        f.write((" ".join(str(d) for d in delays) + "\n").encode())
        for rgba in frames:
            assert len(rgba) == w * h * 4
            f.write(bytes(rgba))


def pattern(w, h):
    """RGB pixels with gradients, so every channel and position matters."""
    return [((x * 255) // max(w - 1, 1), (y * 255) // max(h - 1, 1), ((x + y) * 37) & 0xFF)
            for y in range(h) for x in range(w)]


def rgba_of(im):
    return im.convert("RGBA").tobytes()


# PNG ----------------------------------------------------------------------

def gen_png():
    w, h = 23, 17
    rgb = Image.new("RGB", (w, h))
    rgb.putdata(pattern(w, h))
    rgb.save(os.path.join(OUT, "rgb.png"))
    write_ref("rgb.png", rgb.size, [rgba_of(rgb)], [0])

    rgba = rgb.copy()
    rgba.putalpha(Image.linear_gradient("L").resize((w, h)))
    rgba.save(os.path.join(OUT, "rgba.png"))
    write_ref("rgba.png", rgba.size, [rgba_of(rgba)], [0])

    gray = rgb.convert("L")
    gray.save(os.path.join(OUT, "gray.png"))
    write_ref("gray.png", gray.size, [rgba_of(gray)], [0])

    pal = rgb.convert("P", palette=Image.ADAPTIVE, colors=16)
    pal.info["transparency"] = 3
    pal.save(os.path.join(OUT, "palette_trns.png"), transparency=3)
    write_ref("palette_trns.png", pal.size, [rgba_of(Image.open(os.path.join(OUT, "palette_trns.png")))], [0])

    # Written by hand: Pillow cannot save 16-bit color or interlaced PNGs.
    g16 = [[(x * 65535) // (w - 1) ^ (y * 911) for x in range(w)] for y in range(h)]
    write_png("gray16.png", w, h, 0, 16, [[(v,) for v in row] for row in g16])
    write_ref("gray16.png", (w, h), [bytes(c for row in g16 for v in row
                                           for c in (to8(v),) * 3 + (255,))], [0])

    rgba16 = [[(x * 2849, y * 3855, (x * y * 97) & 0xFFFF, (x + y) * 1638) for x in range(w)]
              for y in range(h)]
    write_png("rgba16.png", w, h, 6, 16, rgba16)
    write_ref("rgba16.png", (w, h), [bytes(to8(c) for row in rgba16 for px in row for c in px)], [0])

    px = pattern(w, h)
    rows = [[px[y * w + x] for x in range(w)] for y in range(h)]
    write_png("interlaced.png", w, h, 2, 8, rows, interlace=True)
    write_ref("interlaced.png", (w, h), [bytes(c for r in rows for p in r for c in p + (255,))], [0])


def to8(v):
    return (v * 255 + 32895) >> 16


def write_png(name, w, h, color_type, depth, rows, interlace=False):
    """rows: h lists of w tuples of samples."""
    fmt = ">" + ("H" if depth == 16 else "B") * len(rows[0][0])

    def scanlines(coords):
        out = b""
        for y, xs in coords:
            out += b"\0" + b"".join(struct.pack(fmt, *rows[y][x]) for x in xs)
        return out

    if interlace:
        passes = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
                  (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
        raw = b""
        for x0, y0, dx, dy in passes:
            xs = list(range(x0, w, dx))
            if xs:
                raw += scanlines((y, xs) for y in range(y0, h, dy))
    else:
        raw = scanlines((y, range(w)) for y in range(h))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    ihdr = struct.pack(">IIBBBBB", w, h, depth, color_type, 0, 0, 1 if interlace else 0)
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
                chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


# JPEG ---------------------------------------------------------------------

def gen_jpeg():
    w, h = 32, 24
    rgb = Image.new("RGB", (w, h))
    rgb.putdata(pattern(w, h))
    for name, kw in (("baseline.jpg", {}), ("progressive.jpg", {"progressive": True})):
        rgb.save(os.path.join(OUT, name), quality=90, **kw)
        write_ref(name, (w, h), [rgba_of(Image.open(os.path.join(OUT, name)))], [0])

    rgb.convert("L").save(os.path.join(OUT, "gray.jpg"), quality=90)
    write_ref("gray.jpg", (w, h), [rgba_of(Image.open(os.path.join(OUT, "gray.jpg")))], [0])

    rgb.convert("CMYK").save(os.path.join(OUT, "cmyk.jpg"), quality=90)
    cmyk = Image.open(os.path.join(OUT, "cmyk.jpg"))
    out = bytearray()
    for c, m, y, k in cmyk.getdata():
        out += bytes(((255 - c) * (255 - k) // 255, (255 - m) * (255 - k) // 255,
                      (255 - y) * (255 - k) // 255, 255))
    write_ref("cmyk.jpg", (w, h), [out], [0])


# BMP ----------------------------------------------------------------------

def bmp(name, w, h, bpp, rows, palette=(), header=40, compression=0, masks=(),
        top_down=False, palette_entry=4):
    """rows: bottom-up unless top_down, each already packed and padded."""
    pal = b"".join(bytes((b, g, r)) + (b"\0" if palette_entry == 4 else b"") for r, g, b in palette)
    if header == 12:
        info = struct.pack("<IHHHH", 12, w, h, 1, bpp)
    else:
        info = struct.pack("<IiiHHIIiiII", header, w, -h if top_down else h, 1, bpp,
                           compression, 0, 2835, 2835, len(palette), 0)
        info += b"".join(struct.pack("<I", m) for m in masks)
        info += b"\0" * (header - len(info)) if header > len(info) else b""
    offset = 14 + len(info) + len(pal)
    data = b"".join(rows)
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(b"BM" + struct.pack("<IHHI", offset + len(data), 0, 0, offset) + info + pal + data)


def pad(row):
    return row + b"\0" * (-len(row) % 4)


def gen_bmp():
    w, h = 13, 7
    px = pattern(w, h)

    def ref(name, pixels, alpha=None):
        out = bytearray()
        for i, (r, g, b) in enumerate(pixels):
            out += bytes((r, g, b, 255 if alpha is None else alpha[i]))
        write_ref(name, (w, h), [out], [0])

    def bottom_up(pack):
        return [pad(pack(y)) for y in reversed(range(h))]

    # 24 bpp, odd width for row padding; bottom-up and top-down
    pack24 = lambda y: b"".join(bytes((b, g, r)) for r, g, b in px[y * w:(y + 1) * w])
    bmp("rgb24.bmp", w, h, 24, bottom_up(pack24))
    ref("rgb24.bmp", px)
    bmp("rgb24_topdown.bmp", w, h, 24, [pad(pack24(y)) for y in range(h)], top_down=True)
    ref("rgb24_topdown.bmp", px)

    # palettes
    palette = [(i * 17, 255 - i * 17, (i * 91) & 0xFF) for i in range(16)]
    idx = [(x * 3 + y * 5) % 16 for y in range(h) for x in range(w)]
    pal_px = [palette[i] for i in idx]

    def pack_bits(y, bits):
        v, n, out = 0, 0, bytearray()
        for x in range(w):
            v = (v << bits) | (idx[y * w + x] & ((1 << bits) - 1))
            n += bits
            if n == 8:
                out.append(v)
                v, n = 0, 0
        if n:
            out.append(v << (8 - n))
        return bytes(out)

    bmp("pal8.bmp", w, h, 8, bottom_up(lambda y: pack_bits(y, 8)), palette)
    ref("pal8.bmp", pal_px)
    bmp("pal4.bmp", w, h, 4, bottom_up(lambda y: pack_bits(y, 4)), palette)
    ref("pal4.bmp", pal_px)
    bmp("os2_pal8.bmp", w, h, 8, bottom_up(lambda y: pack_bits(y, 8)), palette, header=12, palette_entry=3)
    ref("os2_pal8.bmp", pal_px)

    mono = [(200, 10, 30), (5, 90, 250)]
    idx1 = [(x + y) % 2 for y in range(h) for x in range(w)]
    saved = idx
    idx = idx1
    bmp("mono.bmp", w, h, 1, bottom_up(lambda y: pack_bits(y, 1)), mono)
    idx = saved
    ref("mono.bmp", [mono[i] for i in idx1])

    # 16 bpp: default 555 and 565 bitfields
    def scale(v, bits):
        m = (1 << bits) - 1
        return ((v >> (8 - bits)) * 255 + m // 2) // m

    p555 = lambda y: b"".join(struct.pack("<H", (r >> 3) << 10 | (g >> 3) << 5 | b >> 3)
                              for r, g, b in px[y * w:(y + 1) * w])
    bmp("rgb555.bmp", w, h, 16, bottom_up(p555))
    ref("rgb555.bmp", [(scale(r, 5), scale(g, 5), scale(b, 5)) for r, g, b in px])

    p565 = lambda y: b"".join(struct.pack("<H", (r >> 3) << 11 | (g >> 2) << 5 | b >> 3)
                              for r, g, b in px[y * w:(y + 1) * w])
    bmp("rgb565.bmp", w, h, 16, bottom_up(p565), compression=3, masks=(0xF800, 0x07E0, 0x001F))
    ref("rgb565.bmp", [(scale(r, 5), scale(g, 6), scale(b, 5)) for r, g, b in px])

    # 32 bpp: X8R8G8B8 and a V4 header with alpha
    alpha = [(x * 255) // (w - 1) for y in range(h) for x in range(w)]
    p32 = lambda y: b"".join(bytes((b, g, r, 0)) for r, g, b in px[y * w:(y + 1) * w])
    bmp("xrgb32.bmp", w, h, 32, bottom_up(p32))
    ref("xrgb32.bmp", px)

    p32a = lambda y: b"".join(bytes((b, g, r, alpha[y * w + x]))
                              for x, (r, g, b) in enumerate(px[y * w:(y + 1) * w]))
    bmp("argb32_v4.bmp", w, h, 32, bottom_up(p32a), header=108, compression=3,
        masks=(0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000))
    ref("argb32_v4.bmp", px, alpha)


# GIF ----------------------------------------------------------------------

GIF_PAL = [0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0, 255, 255, 255] + [0] * (250 * 3)


def gif_frame(w, h, fill):
    im = Image.new("P", (w, h), fill)
    im.putpalette(GIF_PAL)
    return im


def gif_ref(name):
    im = Image.open(os.path.join(OUT, name))
    loop = im.info.get("loop")
    frames, delays = [], []
    for f in ImageSequence.Iterator(im):
        frames.append(rgba_of(f))
        d = f.info.get("duration", 0) // 10
        delays.append((d if d >= 2 else 10) * 10)
    if len(frames) < 2 or loop is None:
        delays[-1] = 0
    if len(frames) < 2:
        delays = [0]
    write_ref(name, im.size, frames, delays)


def gen_gif():
    w, h = 32, 24
    fr = []
    for k in range(4):
        im = gif_frame(w, h, 0)
        ImageDraw.Draw(im).rectangle([k * 5, 3, k * 5 + 8, 15], fill=1 + k)
        fr.append(im)
    fr[0].save(os.path.join(OUT, "anim_full.gif"), save_all=True, append_images=fr[1:],
               duration=50, loop=0, disposal=1)
    gif_ref("anim_full.gif")

    for disp in (1, 2, 3):
        base = gif_frame(w, h, 5)
        ImageDraw.Draw(base).rectangle([0, 0, 10, 10], fill=3)
        fr = [base]
        for k in range(5):
            im = gif_frame(w, h, 0)
            ImageDraw.Draw(im).rectangle([2 + k * 5, 10, 7 + k * 5, 15], fill=1 + (k % 4))
            fr.append(im)
        name = f"anim_disposal{disp}.gif"
        fr[0].save(os.path.join(OUT, name), save_all=True, append_images=fr[1:],
                   duration=[80] + [0] * 5, loop=0, disposal=disp, transparency=0, optimize=False)
        gif_ref(name)

    fr = []
    for k in range(3):
        im = gif_frame(w, h, 5)
        ImageDraw.Draw(im).ellipse([k * 6, k * 4, k * 6 + 15, k * 4 + 15], fill=1 + k)
        fr.append(im)
    fr[0].save(os.path.join(OUT, "anim_interlaced.gif"), save_all=True, append_images=fr[1:],
               duration=[30, 40, 50], loop=2, interlace=True)
    gif_ref("anim_interlaced.gif")
    fr[0].save(os.path.join(OUT, "anim_once.gif"), save_all=True, append_images=fr[1:],
               duration=[30, 40, 50], interlace=False)
    gif_ref("anim_once.gif")

    still = gif_frame(w, h, 2)
    ImageDraw.Draw(still).line([0, 0, w, h], fill=4)
    still.save(os.path.join(OUT, "still.gif"))
    gif_ref("still.gif")


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        os.remove(os.path.join(OUT, f))
    gen_png()
    gen_jpeg()
    gen_bmp()
    gen_gif()
    return 0


if __name__ == "__main__":
    sys.exit(main())
