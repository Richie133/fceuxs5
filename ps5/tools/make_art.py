#!/usr/bin/env python3
# fceuxs5: draws the app's home-screen art -- icon0.png (512x512) and the 3840x2160 background that becomes
# pic0.dds / pic1.dds (BC7_UNORM, DX10 header, no mipmaps; compressed with etcpak) -- in the intro's style: the chrome
# "FCEUXS5" logo on plasma and stars. All drawn by code with the Bungee font (SIL OFL 1.1).
#
# usage: make_art.py <Bungee.ttf> <PressStart2P.ttf> <out sce_sys dir>
#
# SPDX-License-Identifier: GPL-3.0-or-later
import math
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

bungee, pixel_font, out = sys.argv[1:4]
rng = np.random.default_rng(5)


def chrome(h):
    t = np.arange(h, dtype=np.float32) / max(1, h - 1)
    keys = np.array([0.0, 0.42, 0.5, 0.56, 0.8, 1.0])
    cols = np.array([[40, 110, 255], [190, 235, 255], [255, 255, 255], [40, 30, 70], [255, 120, 30], [255, 230, 120]],
                    np.float32)
    return np.stack([np.interp(t, keys, cols[:, k]) for k in range(3)], axis=1)


def plasma(w, h, t, bright):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    s = 960.0 / w
    xx, yy = xx * s, yy * s
    v = (np.sin(xx * 0.018 + t) + np.sin(yy * 0.023 - t * 1.3) + np.sin((xx + yy) * 0.012 + t * 0.7) +
         np.sin(np.sqrt((xx - 480) ** 2 + (yy - 270) ** 2) * 0.03 - t * 2))
    rgb = np.stack([0.5 + 0.5 * np.sin(v * math.pi + p) for p in (0, 2.1, 4.2)], -1) * bright
    # vignette
    nx, ny = (np.mgrid[0:h, 0:w][1] - w / 2) / h, (np.mgrid[0:h, 0:w][0] - h / 2) / h
    return rgb * (1 - 0.5 * (nx ** 2 + ny ** 2))[..., None]


def stars(img, n, scale):
    h, w = img.shape[:2]
    pil = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
    d = ImageDraw.Draw(pil)
    for _ in range(n):
        a = rng.uniform(0, 2 * math.pi)
        r0 = rng.uniform(0.05, 0.9) * w / 2
        r1 = r0 * rng.uniform(1.05, 1.3)
        cx, cy = w / 2, h * 0.42
        b = int(rng.uniform(120, 255))
        d.line([(cx + math.cos(a) * r0, cy + math.sin(a) * r0 * 0.75), (cx + math.cos(a) * r1, cy + math.sin(a) * r1 * 0.75)],
               fill=(b, b, b), width=max(1, int(scale)))
    return np.asarray(pil, np.float32) / 255


def logo(img, text, size, cy, outline_px):
    """the chrome logo, outlined and glowing, centred at row cy"""
    h, w = img.shape[:2]
    font = ImageFont.truetype(bungee, size)
    m = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(m)
    tw = d.textlength(text, font=font)
    bbox = d.textbbox((0, 0), text, font=font)
    y = cy - (bbox[1] + bbox[3]) // 2
    d.text(((w - tw) / 2, y), text, font=font, fill=255)
    mask = np.asarray(m, np.float32) / 255
    outline = np.asarray(m.filter(ImageFilter.MaxFilter(outline_px | 1)), np.float32) / 255
    glow = np.asarray(m.filter(ImageFilter.MaxFilter(outline_px | 1)).filter(ImageFilter.GaussianBlur(outline_px * 2)),
                      np.float32) / 255
    rows = np.nonzero(mask.max(axis=1) > 0)[0]
    fill = np.zeros((h, 3), np.float32)
    fill[rows[0]:rows[-1] + 1] = chrome(rows[-1] - rows[0] + 1) / 255
    img = np.clip(img + glow[..., None] * np.array([0.3, 0.6, 1.0]) * 0.9, 0, 1)
    img = img * (1 - outline[..., None]) + np.array([0.04, 0.0, 0.12]) * outline[..., None]
    img = img * (1 - mask[..., None]) + fill[:, None, :] * mask[..., None]
    return img


def text(img, s, font_path, size, cy, color):
    pil = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
    d = ImageDraw.Draw(pil)
    f = ImageFont.truetype(font_path, size)
    w = d.textlength(s, font=f)
    off = max(2, size // 12)
    d.text(((pil.width - w) / 2 + off, cy + off), s, font=f, fill=(0, 0, 0))
    d.text(((pil.width - w) / 2, cy), s, font=f, fill=color)
    return np.asarray(pil, np.float32) / 255


def save_dds_bc7(img, path):
    import etcpak
    rgba = np.concatenate([(np.clip(img, 0, 1) * 255).astype(np.uint8),
                           np.full(img.shape[:2] + (1,), 255, np.uint8)], -1)
    h, w = rgba.shape[:2]
    data = etcpak.compress_bc7(rgba.tobytes(), w, h)
    hdr = bytearray(b"DDS ")
    # DDS_HEADER: size, flags (caps|height|width|pixelformat|linearsize), height, width, linear size, depth, mips
    hdr += struct.pack("<7I", 124, 0x000A1007, h, w, len(data), 1, 1)
    hdr += b"\0" * 44
    hdr += struct.pack("<8I", 32, 4, 0x30315844, 0, 0, 0, 0, 0)  # 'DX10'
    hdr += struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    hdr += struct.pack("<5I", 98, 3, 0, 1, 1)  # DXGI_FORMAT_BC7_UNORM, 2D, array size 1, alpha straight
    open(path, "wb").write(bytes(hdr) + data)


# ---- the background: 3840x2160 -------------------------------------------------------------------------------------
W, H = 3840, 2160
bg = plasma(W, H, 1.7, 0.30)
bg = stars(bg, 900, 3)
bg = logo(bg, "FCEUXS5", 620, int(H * 0.40), 41)
bg = text(bg, "NES EMULATOR FOR PS5", pixel_font, 104, int(H * 0.64), (255, 210, 80))
bg = text(bg, "POWERED BY FCEUX", pixel_font, 64, int(H * 0.73), (200, 230, 255))
Image.fromarray((np.clip(bg, 0, 1) * 255).astype(np.uint8)).save(f"{out}/background-source.png")
save_dds_bc7(bg, f"{out}/pic0.dds")
save_dds_bc7(bg, f"{out}/pic1.dds")

# ---- the icon: 512x512 ---------------------------------------------------------------------------------------------
S = 512
ic = plasma(S, S, 0.6, 0.45)
ic = stars(ic, 120, 2)
# a little NES pad above the name
pil = Image.fromarray((np.clip(ic, 0, 1) * 255).astype(np.uint8))
d = ImageDraw.Draw(pil)
d.rounded_rectangle((96, 92, 416, 232), radius=26, fill=(222, 222, 228), outline=(20, 10, 40), width=8)
d.rounded_rectangle((120, 116, 392, 208), radius=14, fill=(40, 40, 46))
d.rectangle((150, 150, 210, 174), fill=(15, 15, 18))
d.rectangle((168, 132, 192, 192), fill=(15, 15, 18))
d.rounded_rectangle((232, 154, 262, 168), radius=6, fill=(120, 120, 128))
d.rounded_rectangle((272, 154, 302, 168), radius=6, fill=(120, 120, 128))
d.ellipse((318, 146, 346, 174), fill=(220, 40, 40))
d.ellipse((356, 146, 384, 174), fill=(220, 40, 40))
ic = np.asarray(pil, np.float32) / 255
ic = logo(ic, "FCEUXS5", 104, 330, 13)
ic = text(ic, "PS5", pixel_font, 40, 404, (255, 210, 80))
Image.fromarray((np.clip(ic, 0, 1) * 255).astype(np.uint8)).save(f"{out}/icon0.png")
Image.fromarray((np.clip(ic, 0, 1) * 255).astype(np.uint8)).save(f"{out}/icon-source.png")
print("art written to", out)
