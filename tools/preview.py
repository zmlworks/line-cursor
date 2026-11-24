#!/usr/bin/env python3
"""Render the LineCursor glyph the same way as src/main.c, then write
an enlarged preview PNG and (optionally) app.ico."""
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# Colors: transparent / black / white, matching the AND/XOR cursor planes.
TRANS, BLACK, WHITE = 0, 1, 2


def scale_px(n: int, unit: int) -> int:
    return (n * unit + 16) // 32


def odd_at_least(n: int, lo: int) -> int:
    if n < lo:
        n = lo
    if n % 2 == 0:
        n += 1
    return n


def geometry(cx: int, cy: int) -> dict:
    """Mirror make_line_cursor() in src/main.c."""
    if cx < 16:
        cx = 32
    if cy < 16:
        cy = 32
    unit = cx if cx < cy else cy
    outline = scale_px(1, unit)
    if outline < 1:
        outline = 1
    core_h = odd_at_least(scale_px(7, unit), 3)
    bar_h = core_h + 2 * outline
    line_w = scale_px(32, unit)
    if line_w > cx - 2 and cx >= 18:
        line_w = cx - 2
    if line_w < 8:
        line_w = cx
    if line_w > cx:
        line_w = cx
    tick_core = odd_at_least(scale_px(3, unit), 1)
    tick_w = tick_core + 2 * outline
    tick_extra = scale_px(3, unit)
    if tick_extra < 2:
        tick_extra = 2
    tick_h = bar_h + 2 * tick_extra
    if tick_h > cy:
        tick_h = cy
    hole = odd_at_least(scale_px(3, unit), 1)
    if hole > core_h:
        hole = odd_at_least(core_h, 1)
    hx, hy = cx // 2, cy // 2
    return dict(
        cx=cx, cy=cy, outline=outline, core_h=core_h, bar_h=bar_h,
        line_w=line_w, tick_core=tick_core, tick_w=tick_w,
        tick_extra=tick_extra, tick_h=tick_h, hole=hole, hx=hx, hy=hy,
        bar_x0=hx - line_w // 2, bar_y0=hy - bar_h // 2,
        tick_x0=hx - tick_w // 2, tick_y0=hy - tick_h // 2,
        hole_x0=hx - hole // 2, hole_y0=hy - hole // 2,
    )


def render_planes(cx: int = 32, cy: int = 32) -> list[list[int]]:
    g = geometry(cx, cy)
    pix = [[TRANS for _ in range(cx)] for _ in range(cy)]

    def fill(x0, y0, x1, y1, color):
        for y in range(y0, y1):
            if y < 0 or y >= cy:
                continue
            for x in range(x0, x1):
                if 0 <= x < cx:
                    pix[y][x] = color

    fill(g["bar_x0"], g["bar_y0"], g["bar_x0"] + g["line_w"], g["bar_y0"] + g["bar_h"], WHITE)
    fill(g["tick_x0"], g["tick_y0"], g["tick_x0"] + g["tick_w"], g["tick_y0"] + g["tick_h"], WHITE)
    fill(g["bar_x0"] + g["outline"], g["bar_y0"] + g["outline"],
         g["bar_x0"] + g["line_w"] - g["outline"], g["bar_y0"] + g["bar_h"] - g["outline"], BLACK)
    fill(g["tick_x0"] + g["outline"], g["tick_y0"] + g["outline"],
         g["tick_x0"] + g["tick_w"] - g["outline"], g["tick_y0"] + g["tick_h"] - g["outline"], BLACK)
    fill(g["hole_x0"], g["hole_y0"], g["hole_x0"] + g["hole"], g["hole_y0"] + g["hole"], WHITE)
    return pix


def planes_to_rgba(pix: list[list[int]]) -> Image.Image:
    h, w = len(pix), len(pix[0])
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = im.load()
    for y in range(h):
        for x in range(w):
            c = pix[y][x]
            if c == BLACK:
                px[x, y] = (0, 0, 0, 255)
            elif c == WHITE:
                px[x, y] = (255, 255, 255, 255)
    return im


def checker(size: tuple[int, int], cell: int, a=(200, 200, 200), b=(230, 230, 230)) -> Image.Image:
    im = Image.new("RGB", size, a)
    px = im.load()
    w, h = size
    for y in range(h):
        for x in range(w):
            if ((x // cell) + (y // cell)) % 2:
                px[x, y] = b
    return im


def blit(dst: Image.Image, src: Image.Image, xy: tuple[int, int]) -> None:
    dst.paste(src, xy, src)


def label(draw: ImageDraw.ImageDraw, xy, text, fill=(40, 40, 40)) -> None:
    font = ImageFont.load_default()
    draw.text(xy, text, fill=fill, font=font)


def make_preview(pix32: list[list[int]], out: Path, zoom: int = 16) -> None:
    glyph = planes_to_rgba(pix32)
    g = geometry(32, 32)
    zw, zh = glyph.size[0] * zoom, glyph.size[1] * zoom
    big = glyph.resize((zw, zh), Image.Resampling.NEAREST)

    pad = 28
    gap = 24
    footer = 36
    header = 28
    # Three panels: light, dark, checkerboard (hole / outline clear on all)
    panels = [
        ("light", Image.new("RGB", (zw, zh), (245, 245, 242))),
        ("dark", Image.new("RGB", (zw, zh), (24, 24, 26))),
        ("checker", checker((zw, zh), zoom, (186, 186, 186), (228, 228, 228))),
    ]
    # Also a 1x strip under a modest scale (4x) for context
    small_zoom = 4
    small = glyph.resize((glyph.size[0] * small_zoom, glyph.size[1] * small_zoom), Image.Resampling.NEAREST)
    sw, sh = small.size

    width = pad * 2 + zw * 3 + gap * 2
    height = header + pad + zh + gap + sh + footer + 8
    canvas = Image.new("RGB", (width, height), (250, 250, 248))
    draw = ImageDraw.Draw(canvas)

    titles = ["light background", "dark background", "checker (transparency)"]
    for i, ((name, bg), title) in enumerate(zip(panels, titles)):
        x = pad + i * (zw + gap)
        y = header
        composed = bg.copy()
        blit(composed, big, (0, 0))
        # mark hotspot with a thin red hairline that stops short of the glyph
        hx = g["hx"] * zoom
        hy = g["hy"] * zoom
        # small red ticks outside the bar, pointing at the hole
        mark = ImageDraw.Draw(composed)
        # Don't cover the cursor: draw 2px red brackets just outside
        # Skip overlay on the cursor itself; a 1px ring around the hole at 1x scale
        # is too small. At zoom, draw a faint red plus only in the hole? Better:
        # label below and a tiny red corner marks far from the line.
        canvas.paste(composed, (x, y))
        # frame
        draw.rectangle([x - 1, y - 1, x + zw, y + zh], outline=(200, 200, 198))
        fill = (40, 40, 40)
        label(draw, (x, y - 14), f"{title}   {zoom}x")

        # Crosshair guides in the margin around the panel, pointing at hotspot
        hx_c = x + hx
        hy_c = y + hy
        # short ticks on the frame
        draw.line([(hx_c, y - 5), (hx_c, y - 1)], fill=(200, 40, 40), width=1)
        draw.line([(hx_c, y + zh + 1), (hx_c, y + zh + 5)], fill=(200, 40, 40), width=1)

    # 1x/4x row
    x0 = pad
    y0 = header + zh + gap
    for bg_color, name in [((245, 245, 242), "light"), ((24, 24, 26), "dark")]:
        bg = Image.new("RGB", (sw, sh), bg_color)
        blit(bg, small, (0, 0))
        canvas.paste(bg, (x0, y0))
        draw.rectangle([x0 - 1, y0 - 1, x0 + sw, y0 + sh], outline=(200, 200, 198))
        label(draw, (x0, y0 + sh + 4), f"{name}  {small_zoom}x  (actual 32px cursor, {small_zoom}x nearest)")
        x0 += sw + gap

    # Caption describing the hotspot
    cap = (f"hotspot = center ({g['hx']},{g['hy']})  ·  "
           f"bar {g['line_w']}x{g['bar_h']}px (core {g['core_h']}px + {g['outline']}px white outline)  ·  "
           f"center tick + {g['hole']}px white sighting dot")
    label(draw, (pad, height - 22), cap)

    canvas.save(out)
    print(f"wrote {out} ({canvas.size[0]}x{canvas.size[1]})")
    print("geometry @32:", {k: g[k] for k in ('line_w','bar_h','core_h','outline','tick_w','tick_h','hole','hx','hy')})


def make_ico(out: Path) -> None:
    import io, struct
    sizes = (16, 32, 48, 64)
    pngs = []
    for s in sizes:
        buf = io.BytesIO()
        planes_to_rgba(render_planes(s, s)).save(buf, format="PNG")
        pngs.append(buf.getvalue())
    header = struct.pack("<HHH", 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    entries = b""
    body = b""
    for s, data in zip(sizes, pngs):
        w = 0 if s >= 256 else s
        h = 0 if s >= 256 else s
        entries += struct.pack("<BBBBHHII", w, h, 0, 0, 1, 32, len(data), offset)
        body += data
        offset += len(data)
    out.write_bytes(header + entries + body)
    print(f"wrote {out} sizes={sizes} ({out.stat().st_size} bytes)")


def main() -> None:
    here = Path(__file__).resolve().parent
    root = here.parent
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", type=Path, default=root / "build" / "preview.png")
    ap.add_argument("--ico", type=Path, default=root / "src" / "app.ico")
    ap.add_argument("--zoom", type=int, default=16)
    args = ap.parse_args()
    args.preview.parent.mkdir(parents=True, exist_ok=True)
    pix = render_planes(32, 32)
    make_preview(pix, args.preview, zoom=args.zoom)
    if args.ico:
        make_ico(args.ico)


if __name__ == "__main__":
    main()
