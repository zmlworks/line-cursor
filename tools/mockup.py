#!/usr/bin/env python3
"""Mockup of LineCursor 0.3: two full-width lines just below the arrow,
plus a small vertical example. Also writes app.ico."""
from __future__ import annotations

import io
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

CORE = 9
GAP = 9
LINE_H = CORE  # solid black, no outline
COUNT = 2


def draw_windows_arrow(scale: int = 1) -> tuple[Image.Image, tuple[int, int], int, int]:
    cell = 32
    outline_pts = [
        (1, 1), (1, 21), (6, 16), (9, 24), (13, 22), (9, 14), (18, 14),
    ]
    fill_pts = [
        (2, 3), (2, 18), (6, 14), (9, 21), (11, 20), (8, 13), (16, 13),
    ]
    img = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
    shimg = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shimg)
    sd.polygon([(x + 1, y + 1) for x, y in outline_pts], fill=(0, 0, 0, 100))
    shimg = shimg.filter(ImageFilter.GaussianBlur(radius=0.6))
    img = Image.alpha_composite(img, shimg)
    d = ImageDraw.Draw(img)
    d.polygon(outline_pts, fill=(0, 0, 0, 255))
    d.polygon(fill_pts, fill=(255, 255, 255, 255))
    px = img.load()
    bottom, right = 0, 0
    for y in range(cell):
        for x in range(cell):
            if px[x, y][3] > 64:
                if y > bottom:
                    bottom = y
                if x > right:
                    right = x
    hot = (1, 1)
    if scale != 1:
        img = img.resize((cell * scale, cell * scale), Image.Resampling.NEAREST)
    return img, hot, bottom, right


def draw_stripes(length: int, vertical: bool, core=CORE, gap=GAP, count=COUNT) -> Image.Image:
    extent = count * core + (count - 1) * gap
    if vertical:
        im = Image.new("RGBA", (extent, length), (0, 0, 0, 0))
    else:
        im = Image.new("RGBA", (length, extent), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for n in range(count):
        o = n * (core + gap)
        if vertical:
            d.rectangle([o, 0, o + core - 1, length - 1], fill=(0, 0, 0, 255))
        else:
            d.rectangle([0, o, length - 1, o + core - 1], fill=(0, 0, 0, 255))
    return im


def fake_window(size, title, body) -> Image.Image:
    w, h = size
    im = Image.new("RGB", (w, h), body)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, w - 1, 28], fill=(255, 255, 255))
    d.rectangle([0, 0, w - 1, h - 1], outline=(200, 200, 200))
    d.line([0, 28, w, 28], fill=(230, 230, 230))
    font = ImageFont.load_default()
    d.text((12, 8), title, fill=(40, 40, 40), font=font)
    for i, ch in enumerate(("_", "x")):
        d.text((w - 42 + i * 18, 8), ch, fill=(80, 80, 80), font=font)
    return im


def make_mockup(out: Path) -> None:
    W, H = 960, 540
    desk = Image.new("RGB", (W, H), (70, 110, 150))
    d = ImageDraw.Draw(desk)
    for y in range(H):
        t = y / max(H - 1, 1)
        d.line([(0, y), (W, y)], fill=(int(70 + 20 * t), int(110 + 15 * t), int(150 + 10 * t)))

    desk.paste(fake_window((420, 260), "Document", (248, 248, 246)), (80, 70))
    desk.paste(fake_window((300, 180), "Folder", (242, 242, 240)), (560, 160))
    d = ImageDraw.Draw(desk)
    d.rectangle([0, H - 36, W, H], fill=(32, 32, 36))
    d.rectangle([10, H - 28, 34, H - 8], fill=(0, 120, 215))

    arrow, hot, bottom, right = draw_windows_arrow(1)
    hot_x, hot_y = 340, 230
    paste_x = hot_x - hot[0]
    paste_y = hot_y - hot[1]
    # Horizontal pair: top of first line just below arrow base
    line_top = paste_y + bottom + 1
    stripes = draw_stripes(W, vertical=False)
    desk.paste(stripes, (0, line_top), stripes)
    desk.paste(arrow, (paste_x, paste_y), arrow)

    # 8x inset around the two lines and the arrow base
    zoom = 8
    cx0, cy0, cw, ch = paste_x - 4, paste_y + bottom - 14, 52, 14 + stripes.size[1] + 8
    crop = desk.crop((cx0, cy0, cx0 + cw, cy0 + ch)).resize(
        (cw * zoom, ch * zoom), Image.Resampling.NEAREST
    )
    inset_w, inset_h = crop.size
    # If inset is huge, cap width by using a slightly tighter zoom later if needed
    box_x = W - inset_w - 20
    box_y = 24
    # Keep inset on the desktop: if too tall, place it but don't overflow taskbar
    if box_y + inset_h + 30 > H - 36:
        # shrink zoom if needed — shouldn't with ch ~ 11*2+9+14+8 = 53, *8 = 424, plus 24+30 = 478, H-36=504. tight but ok
        pass
    d = ImageDraw.Draw(desk)
    d.rectangle(
        [box_x - 6, box_y - 6, box_x + inset_w + 6, box_y + inset_h + 22],
        fill=(250, 250, 248),
        outline=(40, 40, 40),
    )
    desk.paste(crop, (box_x, box_y))
    font = ImageFont.load_default()
    d.text((box_x, box_y + inset_h + 6),
           "8x  two solid black lines, gap 9px, top = arrow base + 1",
           fill=(40, 40, 40), font=font)
    d.rectangle([cx0, cy0, cx0 + cw, cy0 + ch], outline=(255, 220, 80))

    d.rectangle([0, 0, W, 22], fill=(250, 250, 248))
    d.text(
        (10, 5),
        f"LineCursor 0.5 mockup  ·  2 solid black lines  ·  thickness {CORE}px  ·  "
        f"gap {GAP}px  ·  horizontal   (not a live screenshot)",
        fill=(40, 40, 40),
        font=font,
    )
    desk.save(out)
    print(f"wrote {out} ({W}x{H}) line_top={line_top} extent={stripes.size[1]}")


def make_ico(out: Path) -> None:
    sizes = (16, 32, 48, 64)
    pngs = []
    for s in sizes:
        im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        core = max(2, (9 * s + 16) // 32)
        gap = max(1, (9 * s + 16) // 32)
        lh = core
        # two solid black lines in the lower half
        total = lh * 2 + gap
        ly = s - total - max(1, s // 16)
        if ly < s // 3:
            ly = s // 3
        for n in range(2):
            y = ly + n * (lh + gap)
            if y + lh > s:
                break
            d.rectangle([0, y, s - 1, y + lh - 1], fill=(0, 0, 0, 255))
        sc = s / 32.0
        ax, ay = int(6 * sc), int(1 * sc)
        outer = [(ax, ay), (ax, ay + int(9 * sc)), (ax + int(3 * sc), ay + int(6 * sc)),
                 (ax + int(5 * sc), ay + int(11 * sc)), (ax + int(7 * sc), ay + int(10 * sc)),
                 (ax + int(4 * sc), ay + int(5 * sc)), (ax + int(9 * sc), ay + int(5 * sc))]
        if s >= 16:
            d.polygon(outer, fill=(0, 0, 0, 255))
            if s >= 24:
                inner = [(ax + 1, ay + 2), (ax + 1, ay + int(7 * sc)),
                         (ax + int(3 * sc), ay + int(5 * sc)),
                         (ax + int(5 * sc), ay + int(9 * sc)), (ax + int(6 * sc), ay + int(8 * sc)),
                         (ax + int(4 * sc), ay + int(4 * sc)), (ax + int(8 * sc), ay + int(4 * sc))]
                d.polygon(inner, fill=(255, 255, 255, 255))
        buf = io.BytesIO()
        im.save(buf, format="PNG")
        pngs.append(buf.getvalue())
    header = struct.pack("<HHH", 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    entries = b""
    body = b""
    for s, data in zip(sizes, pngs):
        entries += struct.pack("<BBBBHHII", s, s, 0, 0, 1, 32, len(data), offset)
        body += data
        offset += len(data)
    out.write_bytes(header + entries + body)
    print(f"wrote {out} sizes={sizes} ({out.stat().st_size} bytes)")


def main() -> None:
    here = Path(__file__).resolve().parent
    root = here.parent
    build = root / "build"
    build.mkdir(parents=True, exist_ok=True)
    make_mockup(build / "preview.png")
    make_ico(root / "src" / "app.ico")


if __name__ == "__main__":
    main()
