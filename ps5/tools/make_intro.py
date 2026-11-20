#!/usr/bin/env python3
# fceuxs5: renders the intro video -- the "fceuxs5" logo with demoscene effects (CRT power-on, warp starfield,
# letters flying in on the beats, chrome, sine wobble, RGB-split glitches, plasma, copper bars, a sine scroller,
# a zoom-pulse climax, an explosion into pixels, the logo reformed, CRT power-off), timed to the music's beats.
#
# usage: make_intro.py <music.wav> <out_dir> <Bungee.ttf> <PressStart2P.ttf>
# writes out_dir/f0000.png ... at 960x540, 30 fps, for as long as the music lasts (ffmpeg muxes them with it).
#
# SPDX-License-Identifier: GPL-3.0-or-later
import math
import os
import sys
import wave

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H, FPS = 960, 540, 30
wav, out_dir, bungee, pixel_font = sys.argv[1:5]
os.makedirs(out_dir, exist_ok=True)
rng = np.random.default_rng(1911)

# ---- the music: length, loudness and beats ------------------------------------------------------------------------
with wave.open(wav) as w:
    ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
    a = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float32) / 32768
a = a.reshape(-1, ch).mean(axis=1)
FRAMES = int(len(a) / rate * FPS)
hop = rate // FPS
rms = np.array([np.sqrt(np.mean(a[i * hop:(i + 1) * hop] ** 2) + 1e-12) for i in range(FRAMES)])
rms /= rms.max()
low, prev = [], 0.0
for i in range(FRAMES):
    seg = a[i * hop:(i + 1) * hop]
    sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    e = sp[:40].sum()
    low.append(max(0.0, e - prev))
    prev = e
low = np.array(low)
low /= low.max()
beats = [i for i in range(1, FRAMES - 1) if low[i] > 0.25 and low[i] >= low[i - 1] and low[i] >= low[i + 1]]
# beat envelope: 1 on a beat, decaying
pulse = np.zeros(FRAMES)
for i in range(FRAMES):
    for b in beats:
        if 0 <= i - b < 12:
            pulse[i] = max(pulse[i], math.exp(-(i - b) / 3.0))
print(f"{FRAMES} frames, beats at {beats}", file=sys.stderr)

# the acts, in frames (scaled to the music's length; the original timing is 8 s)
S = FRAMES / 240.0
T_ON_END = int(20 * S)      # CRT power-on and static
T_FLYIN_END = int(64 * S)   # the letters fly in
T_CLIMAX = int(150 * S)     # zoom pulse, kaleidoscope, shake
T_BOOM = int(186 * S)       # explosion into pixels
T_REFORM = int(200 * S)     # the logo comes back together
T_OFF = int(226 * S)        # CRT power-off

# ---- the logo ------------------------------------------------------------------------------------------------------
TEXT = "fceuxs5"
font = ImageFont.truetype(bungee, 168)
letters = []
x = 0
for c in TEXT:
    img = Image.new("L", (260, 260), 0)
    ImageDraw.Draw(img).text((40, 20), c, font=font, fill=255)
    bbox = img.getbbox()
    img = img.crop(bbox)
    letters.append({"c": c, "mask": np.asarray(img, np.float32) / 255, "x": x})
    x += img.width + 10
LOGO_W = x - 10
LOGO_H = max(l["mask"].shape[0] for l in letters)
LOGO_X, LOGO_Y = (W - LOGO_W) // 2, 150


def chrome(h, shift=0.0):
    # a chrome ramp: sky, white horizon, dark ground, warm glow (rows 0..h-1)
    t = (np.arange(h, dtype=np.float32) / max(1, h - 1) + shift) % 1.0
    keys = np.array([0.0, 0.42, 0.5, 0.56, 0.8, 1.0])
    cols = np.array([[40, 110, 255], [190, 235, 255], [255, 255, 255], [40, 30, 70], [255, 120, 30], [255, 230, 120]],
                    np.float32)
    return np.stack([np.interp(t, keys, cols[:, k]) for k in range(3)], axis=1) / 255  # h x 3


def letter_rgba(l, hue_shift=0.0, outline_col=(10, 0, 30)):
    m = l["mask"]
    h, w = m.shape
    pad = 6
    mp = np.zeros((h + 2 * pad, w + 2 * pad), np.float32)
    mp[pad:pad + h, pad:pad + w] = m
    outline = np.asarray(Image.fromarray((mp * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(9)), np.float32) / 255
    fill = chrome(h + 2 * pad, hue_shift)[:, None, :] * np.ones((1, w + 2 * pad, 1), np.float32)
    rgb = fill * mp[..., None] + np.array(outline_col, np.float32)[None, None, :] / 255 * (1 - mp[..., None])
    return rgb, outline  # colour, alpha (the outline's coverage)


LETTER_IMG = [letter_rgba(l) for l in letters]


def logo_layer(hue_shift=0.0, outline_col=(10, 0, 30), shine=None):
    """the whole logo as an RGB + alpha canvas of the screen's size"""
    rgb = np.zeros((H, W, 3), np.float32)
    al = np.zeros((H, W), np.float32)
    for l in letters:
        c, o = letter_rgba(l, hue_shift, outline_col) if hue_shift or outline_col != (10, 0, 30) else LETTER_IMG[letters.index(l)]
        h, w = o.shape
        x0, y0 = LOGO_X + l["x"] - 6, LOGO_Y - 6
        rgb[y0:y0 + h, x0:x0 + w] = c * o[..., None] + rgb[y0:y0 + h, x0:x0 + w] * (1 - o[..., None])
        al[y0:y0 + h, x0:x0 + w] = np.maximum(al[y0:y0 + h, x0:x0 + w], o)
    if shine is not None:
        # a diagonal light sweeping across
        yy, xx = np.mgrid[0:H, 0:W]
        band = np.exp(-(((xx + yy * 0.6) - shine) / 26.0) ** 2)
        rgb += band[..., None] * 0.9 * al[..., None]
    return np.clip(rgb, 0, 1), al


def blit(dst, rgb, al, dx=0, dy=0):
    """rgb/al (full screen) composited over dst, offset"""
    src_rgb = np.roll(np.roll(rgb, dy, 0), dx, 1)
    src_al = np.roll(np.roll(al, dy, 0), dx, 1)
    dst[:] = src_rgb * src_al[..., None] + dst * (1 - src_al[..., None])


def scale_about(rgb, al, s, cx, cy, angle=0.0):
    """zoom / rotate a full-screen layer about (cx, cy), nearest neighbour"""
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    ca, sa = math.cos(-angle), math.sin(-angle)
    u = ((xx - cx) * ca - (yy - cy) * sa) / s + cx
    v = ((xx - cx) * sa + (yy - cy) * ca) / s + cy
    ui, vi = u.astype(np.int32), v.astype(np.int32)
    ok = (ui >= 0) & (ui < W) & (vi >= 0) & (vi < H)
    ui, vi = np.clip(ui, 0, W - 1), np.clip(vi, 0, H - 1)
    return rgb[vi, ui] * ok[..., None], al[vi, ui] * ok


def wobble(rgb, al, t, amp, freq=0.045):
    out_rgb, out_al = np.empty_like(rgb), np.empty_like(al)
    for y in range(H):
        s = int(amp * math.sin(y * freq + t))
        out_rgb[y] = np.roll(rgb[y], s, 0)
        out_al[y] = np.roll(al[y], s, 0)
    return out_rgb, out_al


def glitch(img, strength, seed):
    r = np.random.default_rng(seed)
    out = img.copy()
    for _ in range(int(3 + strength * 14)):
        y = r.integers(0, H - 8)
        h = r.integers(2, int(10 + 40 * strength))
        dx = int(r.integers(-60, 61) * strength)
        out[y:y + h] = np.roll(out[y:y + h], dx, 1)
    # channel split
    k = int(2 + 10 * strength)
    out[..., 0] = np.roll(out[..., 0], k, 1)
    out[..., 2] = np.roll(out[..., 2], -k, 1)
    return out


# ---- backgrounds --------------------------------------------------------------------------------------------------
YY, XX = np.mgrid[0:H, 0:W].astype(np.float32)
NX, NY = (XX - W / 2) / H, (YY - H / 2) / H


def plasma(t, bright=0.35):
    v = (np.sin(XX * 0.018 + t) + np.sin(YY * 0.023 - t * 1.3) + np.sin((XX + YY) * 0.012 + t * 0.7) +
         np.sin(np.sqrt((XX - W / 2 - 200 * math.sin(t * 0.5)) ** 2 + (YY - H / 2) ** 2) * 0.03 - t * 2))
    r = 0.5 + 0.5 * np.sin(v * math.pi)
    g = 0.5 + 0.5 * np.sin(v * math.pi + 2.1)
    b = 0.5 + 0.5 * np.sin(v * math.pi + 4.2)
    return np.stack([r, g, b], -1) * bright


def kaleido(t, bright=0.5):
    ang = np.arctan2(NY, NX)
    rad = np.sqrt(NX ** 2 + NY ** 2)
    seg = math.pi / 4
    a = np.abs(((ang + t * 0.6) % (2 * seg)) - seg)
    v = np.sin(rad * 18 - t * 6) + np.sin(a * 10 + t * 3) + np.sin((rad * 7 + a * 5) - t * 2)
    r = 0.5 + 0.5 * np.sin(v * 2.0 + t)
    g = 0.5 + 0.5 * np.sin(v * 2.0 + t + 2.1)
    b = 0.5 + 0.5 * np.sin(v * 2.0 + t + 4.2)
    return np.stack([r, g, b], -1) * bright * (0.3 + 0.7 * np.clip(rad * 1.5, 0, 1))[..., None]


STARS = rng.uniform(-1, 1, (900, 3)).astype(np.float32)
STARS[:, 2] = rng.uniform(0.05, 1.0, 900)


def starfield(img, speed, tint=(1, 1, 1)):
    global STARS
    STARS[:, 2] -= speed
    dead = STARS[:, 2] <= 0.02
    STARS[dead, 0:2] = rng.uniform(-1, 1, (dead.sum(), 2))
    STARS[dead, 2] = 1.0
    pil = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
    d = ImageDraw.Draw(pil)
    for sx, sy, sz in STARS:
        px, py = W / 2 + sx / sz * 260, H / 2 + sy / sz * 260
        pz = sz + speed * 6
        qx, qy = W / 2 + sx / pz * 260, H / 2 + sy / pz * 260
        if not (0 <= px < W and 0 <= py < H):
            continue
        b = int(255 * min(1, (1 - sz) * 1.4))
        col = (int(b * tint[0]), int(b * tint[1]), int(b * tint[2]))
        d.line([(qx, qy), (px, py)], fill=col, width=1 if sz > 0.4 else 2)
    return np.asarray(pil, np.float32) / 255


def copper(img, t, alpha=0.85):
    for k in range(6):
        cy = H / 2 + math.sin(t * 2.2 + k * 0.7) * 170
        hue = k / 6
        col = np.array([0.5 + 0.5 * math.sin(6.28 * hue), 0.5 + 0.5 * math.sin(6.28 * hue + 2.1),
                        0.5 + 0.5 * math.sin(6.28 * hue + 4.2)], np.float32)
        y0, y1 = int(cy - 16), int(cy + 16)
        for y in range(max(0, y0), min(H, y1)):
            k2 = 1 - abs(y - cy) / 16
            img[y] = img[y] * (1 - alpha * k2) + col * (0.35 + 0.65 * k2) * alpha * k2
    return img


# ---- the scroller -------------------------------------------------------------------------------------------------
SCROLL = ("   FCEUXS5  ::  THE NES ON YOUR PS5  ::  POWERED BY FCEUX  ::  BY RICHIE133  ::  CRT SHADERS - HQ2X - "
          "NTSC - ROM HACKS - BOX ART  ::  GREETINGS TO THE WHOLE PS5 SCENE  ::  PRESS X TO START  ::   ")
sfont = ImageFont.truetype(pixel_font, 28)
CHAR_W = 30


def scroller(img, f, y_base=468):
    pil = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
    d = ImageDraw.Draw(pil)
    off = f * 7
    first = off // CHAR_W
    for i in range(W // CHAR_W + 2):
        ci = (first + i) % len(SCROLL)
        x = i * CHAR_W - off % CHAR_W
        y = y_base + 22 * math.sin((x + f * 6) * 0.018)
        hue = (x / W + f * 0.01) % 1
        col = tuple(int(255 * (0.55 + 0.45 * math.sin(6.28 * hue + p))) for p in (0, 2.1, 4.2))
        d.text((x + 3, y + 3), SCROLL[ci], font=sfont, fill=(0, 0, 0))
        d.text((x, y), SCROLL[ci], font=sfont, fill=col)
    return np.asarray(pil, np.float32) / 255


def subtitle(img, text, n_chars, y=360, size=34):
    f2 = ImageFont.truetype(pixel_font, size)
    pil = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
    d = ImageDraw.Draw(pil)
    shown = text[:n_chars]
    w = d.textlength(text, font=f2)
    x = (W - w) / 2
    d.text((x + 3, y + 3), shown, font=f2, fill=(0, 0, 0))
    d.text((x, y), shown, font=f2, fill=(255, 210, 80))
    return np.asarray(pil, np.float32) / 255


def scanlines(img):
    # (no scanline rows: they cost the MPEG-1 encoder more than the rest of the picture)
    v = 1 - 0.35 * (NX ** 2 + NY ** 2)
    return img * v[..., None]


def crt_scale(img, sy, sx=1.0):
    """CRT power on/off: the picture squeezed into a line (sy -> 0) and a dot (sx -> 0)"""
    out = np.zeros_like(img)
    h = max(1, int(H * sy))
    w = max(1, int(W * sx))
    small = np.asarray(Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8)).resize((w, h), Image.BILINEAR),
                       np.float32) / 255
    y0, x0 = (H - h) // 2, (W - w) // 2
    out[y0:y0 + h, x0:x0 + w] = small + (0.6 if sy < 0.05 else 0.0)
    return np.clip(out, 0, 1)


# ---- particles for the explosion -------------------------------------------------------------------------------------
base_rgb, base_al = logo_layer()
pts_y, pts_x = np.nonzero(base_al > 0.5)
pick = rng.choice(len(pts_x), 5000, replace=False)
P_X, P_Y = pts_x[pick].astype(np.float32), pts_y[pick].astype(np.float32)
P_C = base_rgb[pts_y[pick], pts_x[pick]]
ang = np.arctan2(P_Y - (LOGO_Y + LOGO_H / 2), P_X - W / 2) + rng.normal(0, 0.4, len(P_X))
spd = rng.uniform(4, 22, len(P_X))
P_VX, P_VY = np.cos(ang) * spd, np.sin(ang) * spd - 4


def particles(img, k):
    """k frames after the explosion (negative: still flying back for the re-form)"""
    if k >= 0:
        x = P_X + P_VX * k
        y = P_Y + P_VY * k + 0.35 * k * k
    else:
        t = -k
        x = P_X + P_VX * t
        y = P_Y + P_VY * t + 0.35 * t * t
    xi, yi = x.astype(np.int32), y.astype(np.int32)
    ok = (xi >= 1) & (xi < W - 3) & (yi >= 1) & (yi < H - 3)
    # each pixel a 3x3 spark, white-hot at first, then the logo's colour shifting through the rainbow
    heat = max(0.0, 1 - abs(k) / 6)
    hue = (np.arange(len(P_X)) * 0.002 + abs(k) * 0.05) % 1.0
    rainbow = np.stack([0.5 + 0.5 * np.sin(6.28 * hue + q) for q in (0, 2.1, 4.2)], -1)
    col = np.clip(P_C * 0.6 + rainbow * 0.6 + heat, 0, 1)
    glow = np.zeros((H, W), np.float32)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            img[yi[ok] + dy, xi[ok] + dx] = col[ok]
            glow[yi[ok] + dy, xi[ok] + dx] = 1
    g = np.asarray(Image.fromarray((glow * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(6)), np.float32) / 255
    return np.clip(img + g[..., None] * np.array([0.5, 0.4, 1.0]) * 0.8, 0, 1)


# ---- the letters flying in ----------------------------------------------------------------------------------------
fly_beats = [b for b in beats if T_ON_END - 2 <= b < T_FLYIN_END]
while len(fly_beats) < len(TEXT):
    fly_beats.append((fly_beats[-1] if fly_beats else T_ON_END) + 5)
fly_beats = sorted(fly_beats)[:len(TEXT)]
arrive = {i: fly_beats[i] for i in range(len(TEXT))}


def letter_alone(i):
    rgb = np.zeros((H, W, 3), np.float32)
    al = np.zeros((H, W), np.float32)
    c, o = LETTER_IMG[i]
    h, w = o.shape
    x0, y0 = LOGO_X + letters[i]["x"] - 6, LOGO_Y - 6
    rgb[y0:y0 + h, x0:x0 + w] = c
    al[y0:y0 + h, x0:x0 + w] = o
    return rgb, al


LETTER_LAYERS = [letter_alone(i) for i in range(len(TEXT))]

# ---- render ---------------------------------------------------------------------------------------------------------
for f in range(FRAMES):
    t = f / FPS
    p = pulse[f]
    img = np.zeros((H, W, 3), np.float32)

    if f < T_ON_END:
        # static, then the tube warms up
        noise = rng.uniform(0, 1, (H // 2, W // 2)).repeat(2, 0).repeat(2, 1)
        img[:] = (noise * (0.25 + 0.5 * (f / T_ON_END)))[..., None] * np.array([0.8, 0.9, 1.0])
        img = starfield(img, 0.004)
        img = crt_scale(img, min(1.0, (f + 1) / (T_ON_END * 0.6)) ** 2)
    elif f < T_OFF:
        # background
        if f < T_FLYIN_END:
            img = starfield(img * 0, 0.01 + 0.03 * (f - T_ON_END) / (T_FLYIN_END - T_ON_END) + 0.03 * p)
        elif f < T_CLIMAX:
            img = plasma(t, 0.22 + 0.25 * p)
            img = starfield(img, 0.02 + 0.03 * p)
            img = copper(img, t)
        elif f < T_BOOM:
            img = kaleido(t * 1.6, 0.45 + 0.4 * p)
            img = starfield(img, 0.05 + 0.06 * p)
        elif f < T_REFORM:
            img = plasma(t * 1.5, 0.06)
            img = starfield(img, 0.03, (0.6, 0.7, 1.0))
        else:
            img = plasma(t * 1.5, 0.16)
            img = starfield(img, 0.015, (0.8, 0.9, 1.0))

        # the logo
        if f < T_FLYIN_END:
            for i in range(len(TEXT)):
                k = f - arrive[i]
                rgb, al = LETTER_LAYERS[i]
                if k < -8:
                    continue
                if k < 0:
                    # flying in from far away, spinning, with a trail of RGB ghosts
                    s = 1 + (-k) * 0.45
                    ang = (-k) * 0.3 * (1 if i % 2 else -1)
                    cx = LOGO_X + letters[i]["x"] + letters[i]["mask"].shape[1] / 2
                    r2, a2 = scale_about(rgb, al, s, cx, LOGO_Y + LOGO_H / 2, ang)
                    fade = min(1.0, (8 + k) / 6)
                    # cyan and magenta ghosts either side: the RGB-split trail
                    for gdx, gcol in ((-14, (0.1, 0.9, 1.0)), (14, (1.0, 0.1, 0.8))):
                        ghost = np.ones_like(img) * np.array(gcol, np.float32)
                        blit(img, ghost, a2 * 0.35 * fade, dx=gdx)
                    blit(img, r2, a2 * fade)
                else:
                    bounce = int(28 * math.exp(-k / 3) * math.cos(k * 1.4))
                    blit(img, rgb, al, dy=-bounce)
                    if k < 3:
                        img = np.clip(img + al[..., None] * (1 - k / 3) * 0.8, 0, 1)
        elif f < T_BOOM:
            shine = ((f - T_FLYIN_END) * 34) % 1700 - 300
            hue = 0.0 if f < T_CLIMAX else (f - T_CLIMAX) * 0.02
            outline = (10, 0, 30) if f < T_CLIMAX else tuple(int(255 * (0.5 + 0.5 * math.sin(f * 0.4 + q))) for q in (0, 2.1, 4.2))
            rgb, al = logo_layer(hue, outline, shine)
            amp = 6 + 26 * p if f < T_CLIMAX else 12 + 40 * p
            rgb, al = wobble(rgb, al, t * 7, amp)
            if f >= T_CLIMAX:
                s = 1.0 + 0.18 * p + 0.06 * math.sin(t * 9)
                rgb, al = scale_about(rgb, al, s, W / 2, LOGO_Y + LOGO_H / 2, 0.05 * math.sin(t * 5))
            # glow under the logo
            glow = np.asarray(Image.fromarray((al * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(14)),
                              np.float32) / 255
            img = np.clip(img + glow[..., None] * np.array([0.3, 0.6, 1.0]) * (0.6 + 0.8 * p), 0, 1)
            shake = (int(rng.integers(-10, 11) * p), int(rng.integers(-8, 9) * p)) if f >= T_CLIMAX else (0, 0)
            blit(img, rgb, al, dx=shake[0], dy=shake[1])
            if p > 0.6:
                img = glitch(img, p * (1.4 if f >= T_CLIMAX else 0.8), f)
            img = scroller(img, f)
        elif f < T_REFORM:
            # boom: a flash, the pixels flying apart, then back
            k = f - T_BOOM
            span = T_REFORM - T_BOOM
            if k < 3:
                img = np.clip(img + (1 - k / 3), 0, 1)
            kk = k if k < span / 2 else -(span - k)
            img = particles(img, kk)
        else:
            k = f - T_REFORM
            rgb, al = logo_layer(0.0, (10, 0, 30), (k * 40) % 1700 - 300)
            glow = np.asarray(Image.fromarray((al * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(10)),
                              np.float32) / 255
            img = np.clip(img + glow[..., None] * np.array([0.3, 0.6, 1.0]) * 0.8, 0, 1)
            blit(img, rgb, al)
            if k < 2:
                img = np.clip(img + 0.7, 0, 1)
            img = subtitle(img, "NES EMULATOR FOR PS5", k * 2)
            if k > 10:
                img = subtitle(img, "POWERED BY FCEUX", (k - 10) * 2, y=420, size=22)
    else:
        # power-off: the last picture squeezed into a line, then a dot
        k = f - T_OFF
        span = max(1, FRAMES - T_OFF)
        rgb, al = logo_layer()
        img = plasma(t * 1.5, 0.18)
        blit(img, rgb, al)
        img = subtitle(img, "NES EMULATOR FOR PS5", 99)
        q = k / span
        img = crt_scale(img, max(0.003, 1 - q * 2.2), 1.0 if q < 0.45 else max(0.002, 1 - (q - 0.45) * 2.2))
        if q > 0.95:
            img *= 0
    img = scanlines(img)
    Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8)).save(f"{out_dir}/f{f:04d}.png")
    if f % 30 == 0:
        print(f"frame {f}/{FRAMES}", file=sys.stderr)
