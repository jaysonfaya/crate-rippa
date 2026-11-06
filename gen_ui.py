#!/usr/bin/env python3
"""CRATE RIPPA (JJ DA PLUG): writes params.json, layout.conf, skin.css, vst.json and the skin images (images/).
Run by build.sh before the build (needs Pillow and fontTools). The look is CRATE DIGGA's: black sticker, white hand-drawn lines,
gold, Bebas Neue lettering (SIL Open Font License, OFL-Bebas.txt) turned into outlines. The dog logo sits next to the name,
the gold JF logo on INFO. Layout coordinates: the Force Shadow frame, y = plugin y + 86; the plugin area is 1280 x 628."""
import json, math, os, random, shutil
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from PIL import Image, ImageDraw

OUT = os.path.dirname(os.path.abspath(__file__))
IMG = OUT + "/images"
os.makedirs(IMG, exist_ok=True)
for f in ("dog_logo.png", "jf_gold.png"):
    shutil.copy(OUT + "/" + f, IMG + "/" + f)
VERSION_TXT = "1.0"
ROWS, WCOLS, WLEV, NEV, NPLAT_ = 7, 80, 24, 32, 24
NISO = ["FULL MIX", "NO VOCALS", "VOCAL BAND", "BASS", "KICK + HATS", "NO BASS", "HIGHS"]
INK, BLK, GOLD, PINK, DIM2 = "#f4f1ea", "#0e0e0f", "#ffb52e", "#ff6b8a", "#3a3834"

# ------------------------------------------------------------------ params (VST index order; keep appending at the end in later versions)
P = []
def opt(key, name, options, default=0): P.append({"key": key, "name": name, "options": options, "default": default})
def ro(key, name, poll=True):
    d = {"key": key, "name": name, "type": "readout", "display": "string"}
    if not poll: d["poll"] = False
    P.append(d)
def trig(key, name): P.append({"key": key, "name": name, "type": "trigger", "momentary": True})
def num(key, name, a, b, d, integer=False, unit=None):
    e = {"key": key, "name": name, "min": a, "max": b, "default": d}
    if integer: e["display"] = "int"
    if unit: e["unit"] = unit
    P.append(e)
num("vol", "Song Mix", 0, 1, 0.8)
num("padvol", "Pad Mix", 0, 1, 1.0)
opt("loop", "Loop", ["1X", "LOOP"])
opt("find", "Find", ["SONGS", "LISTS"])
opt("kb", "Keyboard", ["off", "on"])
trig("kb_open", "Type")
for k, n in (("q_txt", "Search text"), ("query_kb", "Typing"), ("status", "Status"), ("res_head", "Results"), ("now", "Now playing"), ("time", "Time")): ro(k, n)
num("seek", "Position", 0, 1, 0)
trig("search", "Search"); trig("history", "History")
num("scroll", "Scroll", 0, 1, 1)
for i in range(1, ROWS + 1): ro("r%d" % i, "Result %d" % i)
for i in range(1, ROWS + 1): trig("rt%d" % i, "Result %d tap" % i)
for k, n in (("play", "Start/Stop"), ("stop", "Stop"), ("rew", "Back 5s"), ("fwd", "Forward 5s")): trig(k, n)
num("deck_pitch", "Pitch Fader", 0, 1, 0.5)
opt("prange", "Pitch Range", ["8%", "16%", "50%", "OCT"], 3)
opt("klock", "Key Lock", ["KEY OFF", "KEY ON"])
num("jog", "Scratch", 0, 1, 0.5)
num("plat", "Platter", 0, 1, 0)
trig("deck_cue", "Deck Cue")
ro("pitch_txt", "Pitch")
opt("chop_by", "Chop By", ["HITS", "BEATS", "LAZY"], 2)
opt("assign", "Lazy Mode", ["NEXT PAD", "PAD HIT"])
opt("arm", "Pads", ["PADS PLAY", "PADS CHOP"], 1)
trig("tap", "Tap Chop"); trig("auto", "Auto Chop")
num("sens", "Sensitivity", 0, 1, 0.5)
num("bpm", "Tempo", 70, 170, 90, integer=True, unit="BPM")
trig("bpm_detect", "Detect Tempo"); trig("bpm_mpc", "MPC Tempo")
opt("slice", "Slice", ["1/2 BEAT", "1 BEAT", "2 BEATS", "1 BAR", "2 BARS"], 1)
trig("cue_set", "Set Cue"); trig("cue_go", "Go To Cue")
ro("cue_txt", "Cue"); ro("hint", "Hint")
for i in range(1, 17): trig("pd%d" % i, "Pad %d" % i)
for i in range(1, 17): ro("pn%d" % i, "Pad %d name" % i)
for i in range(1, 5): trig("bk%d" % i, "Bank %s" % "ABCD"[i - 1])
for i in range(1, 5): ro("bt%d" % i, "Bank %s text" % "ABCD"[i - 1])
ro("sel", "Selected pad"); ro("ops_txt", "Pad action")
for k, n in (("undo", "Undo"), ("clr_pad", "Clear Pad"), ("clr_bank", "Clear Bank"), ("learn", "Learn Pads"), ("export", "Export"),
             ("op_copy", "Copy Pad"), ("op_move", "Move Pad"), ("op_swap", "Swap Pads"), ("op_del", "Delete Pad"),
             ("bk_copy", "Copy Bank"), ("bk_move", "Move Bank"), ("bk_swap", "Swap Bank"), ("stop_all", "Stop All")): trig(k, n)
opt("zoom", "View", ["SONG", "PAD"], 1)
ro("ed_pad_txt", "Edited pad"); ro("ed_pos_txt", "Start and end")
num("ed_start", "Start", 0, 1, 0); num("ed_end", "End", 0, 1, 1)
for i in range(WCOLS): num("wv%d" % i, "Wave %d" % i, 0, 1, 0)
for i in range(NEV): num("ev%d" % i, "Envelope %d" % i, 0, 1, 0)
num("ed_a", "Attack", 0, 1, 0); num("ed_d", "Decay", 0, 1, 0.3); num("ed_s", "Sustain", 0, 1, 1); num("ed_r", "Release", 0, 1, 0.12)
for k in ("ed_a_txt", "ed_d_txt", "ed_s_txt", "ed_r_txt", "grp_txt"): ro(k, k.replace("_txt", "").replace("ed_", "").upper())
for k, n in (("ed_play", "Play Pad"), ("fit", "Fit"), ("pad_prev", "Previous Pad"), ("pad_next", "Next Pad"), ("grp_m", "Group -"), ("grp_p", "Group +"),
             ("s_mm", "Start -50ms"), ("s_m", "Start -5ms"), ("s_p", "Start +5ms"), ("s_pp", "Start +50ms"),
             ("e_mm", "End -50ms"), ("e_m", "End -5ms"), ("e_p", "End +5ms"), ("e_pp", "End +50ms")): trig(k, n)
num("pitch", "Pad Pitch", -24, 24, 0, integer=True, unit="st")
num("level", "Pad Level", 0, 1, 1)
opt("pmode", "Pad Mode", ["ONE SHOT", "GATE"])
opt("rev", "Reverse", ["FORWARD", "REVERSE"])
opt("velo", "Velocity", ["VEL OFF", "VEL ON"], 1)
opt("iso", "Isolate", NISO)
opt("iso_pad", "Pad Isolate", NISO)
trig("iso_sel", "To Selected Pad"); trig("iso_bank", "To Whole Bank")
ro("hint_iso", "Isolate hint")
ro("version", "Version", poll=False); ro("ytdlp_txt", "yt-dlp"); trig("ytdlp_update", "Update yt-dlp")
ro("display_rev", "Display rev", poll=False)
KB_ROWS = ["1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"]
for c in "".join(KB_ROWS): trig("k_" + c, "Key " + c.upper())
for k, n in (("k_spc", "Space"), ("k_del", "Delete"), ("k_clr", "Clear text"), ("k_go", "Search now"), ("k_close", "Close keys")): trig(k, n)
json.dump({"name": "Crate Rippa", "params": P}, open(OUT + "/params.json", "w"), indent=1)

# ================================================================== CRATE DIGGA drawing helpers (unchanged)
# ------------------------------------------------------------------ lettering as outlines
FONTS = {}
def font(name):
    if name not in FONTS:
        t = TTFont(OUT + "/%s.ttf" % name)
        FONTS[name] = (t, t.getGlyphSet(), t.getBestCmap(), t["head"].unitsPerEm, t["hmtx"])
    return FONTS[name]
_gcache = {}
def glyph_d(fn, g):
    k = (fn, g)
    if k not in _gcache:
        t, gs, cm, upm, hm = font(fn)
        pen = SVGPathPen(gs); gs[g].draw(pen); _gcache[k] = pen.getCommands()
    return _gcache[k]
def text_w(s, size, fn="BebasNeue", spacing=0.0):
    t, gs, cm, upm, hm = font(fn)
    return sum(hm[cm.get(ord(c), cm[ord("?")])][0] * size / upm + spacing for c in s) - spacing
def tx(s, x, y, size, fill, anchor="start", fn="BebasNeue", spacing=0.0):
    t, gs, cm, upm, hm = font(fn)
    w = text_w(s, size, fn, spacing)
    cx = x - (w / 2 if anchor == "middle" else w if anchor == "end" else 0)
    out = []; sc = size / upm
    for c in s:
        g = cm.get(ord(c), cm[ord("?")]); d = glyph_d(fn, g)
        if d: out.append('<path transform="translate(%.2f %.2f) scale(%.5f %.5f)" d="%s"/>' % (cx, y, sc, -sc, d))
        cx += hm[g][0] * sc + spacing
    return '<g fill="%s">%s</g>' % (fill, "".join(out))
def txc(s, x, y, size, fill, anchor="middle", fn="BebasNeue", spacing=0.0):
    return tx(s, x, y + size * 0.35, size, fill, anchor, fn, spacing)
def svg(name, w, h, body, defs=""):
    open("%s/%s.svg" % (IMG, name), "w").write('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d"><defs>%s</defs>%s</svg>' % (w, h, w, h, defs, body))

# ------------------------------------------------------------------ hand-drawn helpers (wobbly lines, deterministic)
RND = random.Random(11)
def smooth(pts, closed=True):
    n = len(pts)
    if n < 3: return ""
    d = "M%.1f %.1f" % pts[0]
    rng = range(n) if closed else range(n - 1)
    for i in rng:
        p0, p1, p2, p3 = pts[(i - 1) % n], pts[i], pts[(i + 1) % n], pts[(i + 2) % n]
        if not closed:
            p0 = pts[max(i - 1, 0)]; p3 = pts[min(i + 2, n - 1)]
        c1 = (p1[0] + (p2[0] - p0[0]) / 6, p1[1] + (p2[1] - p0[1]) / 6)
        c2 = (p2[0] - (p3[0] - p1[0]) / 6, p2[1] - (p3[1] - p1[1]) / 6)
        d += " C%.1f %.1f %.1f %.1f %.1f %.1f" % (c1 + c2 + p2)
    return d + (" Z" if closed else "")
def jit(pts, j): return [(x + RND.uniform(-j, j), y + RND.uniform(-j, j)) for x, y in pts]
def rr_pts(x, y, w, h, r, step=22):
    pts = []
    def edge(x1, y1, x2, y2):
        L = math.hypot(x2 - x1, y2 - y1); k = max(1, int(L / step))
        for i in range(k): pts.append((x1 + (x2 - x1) * i / k, y1 + (y2 - y1) * i / k))
    def arc(cx, cy, a0):
        for i in range(4): a = math.radians(a0 + 90 * i / 4); pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    edge(x + r, y, x + w - r, y); arc(x + w - r, y + r, -90)
    edge(x + w, y + r, x + w, y + h - r); arc(x + w - r, y + h - r, 0)
    edge(x + w - r, y + h, x + r, y + h); arc(x + r, y + h - r, 90)
    edge(x, y + h - r, x, y + r); arc(x + r, y + r, 180)
    return pts
def sk_rect(x, y, w, h, r, fill, stroke, sw=3, j=1.3, twice=True):
    """a rounded box drawn by hand: wobbly outline, optionally a second slightly different pass"""
    s = '<path d="%s" fill="%s" stroke="%s" stroke-width="%s" stroke-linejoin="round"/>' % (smooth(jit(rr_pts(x, y, w, h, r), j)), fill, stroke, sw)
    if twice: s += '<path d="%s" fill="none" stroke="%s" stroke-width="%s" opacity=".55" stroke-linejoin="round"/>' % (smooth(jit(rr_pts(x, y, w, h, r), j * 1.5)), stroke, max(1, sw - 1.5))
    return s
def sk_line(x1, y1, x2, y2, stroke=INK, sw=3, j=1.2, bend=2.5):
    mx, my = (x1 + x2) / 2 + RND.uniform(-bend, bend), (y1 + y2) / 2 + RND.uniform(-bend, bend)
    return '<path d="M%.1f %.1f Q%.1f %.1f %.1f %.1f" fill="none" stroke="%s" stroke-width="%s" stroke-linecap="round"/>' % (x1 + RND.uniform(-j, j), y1 + RND.uniform(-j, j), mx, my, x2 + RND.uniform(-j, j), y2 + RND.uniform(-j, j), stroke, sw)
def sk_ell(cx, cy, rx, ry, fill, stroke=INK, sw=3, j=1.2, rot=0):
    pts = [(cx + rx * math.cos(2 * math.pi * i / 14), cy + ry * math.sin(2 * math.pi * i / 14)) for i in range(14)]
    return '<path d="%s" fill="%s" stroke="%s" stroke-width="%s" stroke-linejoin="round" transform="rotate(%s %s %s)"/>' % (smooth(jit(pts, j)), fill, stroke, sw, rot, cx, cy)
def sk_poly(pts, fill, stroke=INK, sw=3, j=1.2):
    return '<path d="%s" fill="%s" stroke="%s" stroke-width="%s" stroke-linejoin="round"/>' % (smooth(jit(pts, j)), fill, stroke, sw)

# ------------------------------------------------------------------ the characters (my own): a dog with headphones and a cat with sunglasses
def dog(cx, cy, s=1.0, tongue=True):
    g = ""
    g += sk_ell(cx - 44 * s, cy + 6 * s, 15 * s, 30 * s, INK, INK, 3, 1, 18)            # floppy ears
    g += sk_ell(cx + 44 * s, cy + 6 * s, 15 * s, 30 * s, INK, INK, 3, 1, -18)
    g += sk_ell(cx, cy, 40 * s, 36 * s, BLK, INK, 3.5, 1.4)                              # head
    g += '<path d="M%.1f %.1f Q%.1f %.1f %.1f %.1f" fill="none" stroke="%s" stroke-width="%.1f" stroke-linecap="round"/>' % (cx - 38 * s, cy - 6 * s, cx, cy - 66 * s, cx + 38 * s, cy - 6 * s, GOLD, 7 * s)   # headphone band
    g += sk_ell(cx - 41 * s, cy - 2 * s, 8 * s, 13 * s, GOLD, INK, 2.5, .8) + sk_ell(cx + 41 * s, cy - 2 * s, 8 * s, 13 * s, GOLD, INK, 2.5, .8)
    g += sk_ell(cx, cy + 12 * s, 20 * s, 15 * s, INK, INK, 2.5, 1)                        # snout
    g += sk_ell(cx, cy + 4 * s, 7.5 * s, 5.5 * s, BLK, BLK, 1, .6)                        # nose
    g += '<circle cx="%.1f" cy="%.1f" r="%.1f" fill="#fff"/>' % (cx - 2 * s, cy + 2.5 * s, 1.6 * s)
    g += sk_ell(cx - 16 * s, cy - 10 * s, 6.5 * s, 7.5 * s, INK, INK, 2, .6) + sk_ell(cx + 16 * s, cy - 10 * s, 6.5 * s, 7.5 * s, INK, INK, 2, .6)   # eyes
    g += '<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s"/><circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s"/>' % (cx - 15 * s, cy - 9 * s, 3.2 * s, BLK, cx + 17 * s, cy - 9 * s, 3.2 * s, BLK)
    g += '<path d="M%.1f %.1f Q%.1f %.1f %.1f %.1f M%.1f %.1f Q%.1f %.1f %.1f %.1f" fill="none" stroke="%s" stroke-width="%.1f" stroke-linecap="round"/>' % (cx, cy + 9 * s, cx - 5 * s, cy + 20 * s, cx - 12 * s, cy + 17 * s, cx, cy + 9 * s, cx + 5 * s, cy + 20 * s, cx + 12 * s, cy + 17 * s, BLK, 2.4 * s)
    if tongue: g += sk_ell(cx, cy + 25 * s, 6 * s, 8 * s, PINK, BLK, 2, .6)
    return g
def cat(cx, cy, s=1.0, shades=True):
    g = ""
    g += sk_poly([(cx - 40 * s, cy - 10 * s), (cx - 36 * s, cy - 50 * s), (cx - 10 * s, cy - 30 * s)], BLK, INK, 3.5, 1) + sk_poly([(cx - 33 * s, cy - 20 * s), (cx - 32 * s, cy - 40 * s), (cx - 19 * s, cy - 29 * s)], PINK, PINK, 1, .6)
    g += sk_poly([(cx + 40 * s, cy - 10 * s), (cx + 36 * s, cy - 50 * s), (cx + 10 * s, cy - 30 * s)], BLK, INK, 3.5, 1) + sk_poly([(cx + 33 * s, cy - 20 * s), (cx + 32 * s, cy - 40 * s), (cx + 19 * s, cy - 29 * s)], PINK, PINK, 1, .6)
    g += sk_ell(cx, cy, 44 * s, 35 * s, BLK, INK, 3.5, 1.4)
    for k in (-1, 0, 1): g += sk_line(cx + k * 9 * s, cy - 33 * s, cx + k * 9 * s, cy - 22 * s, INK, 2.5, .5, 1)   # stripes
    if shades:
        g += sk_rect(cx - 36 * s, cy - 14 * s, 31 * s, 19 * s, 7 * s, "#000", INK, 3, .8, False) + sk_rect(cx + 5 * s, cy - 14 * s, 31 * s, 19 * s, 7 * s, "#000", INK, 3, .8, False)
        g += sk_line(cx - 6 * s, cy - 8 * s, cx + 6 * s, cy - 8 * s, INK, 3, .3, .5)
        g += sk_line(cx - 30 * s, cy - 10 * s, cx - 20 * s, cy - 10 * s, "#999", 2, .2, .3) + sk_line(cx + 11 * s, cy - 10 * s, cx + 21 * s, cy - 10 * s, "#999", 2, .2, .3)
    else:
        for sx in (-1, 1): g += '<path d="M%.1f %.1f Q%.1f %.1f %.1f %.1f" fill="none" stroke="%s" stroke-width="%.1f" stroke-linecap="round"/>' % (cx + sx * 24 * s - 8 * s, cy - 5 * s, cx + sx * 24 * s, cy - 15 * s, cx + sx * 24 * s + 8 * s, cy - 5 * s, INK, 3 * s)
    g += sk_poly([(cx - 5 * s, cy + 8 * s), (cx + 5 * s, cy + 8 * s), (cx, cy + 14 * s)], PINK, PINK, 1, .4)
    g += '<path d="M%.1f %.1f Q%.1f %.1f %.1f %.1f M%.1f %.1f Q%.1f %.1f %.1f %.1f" fill="none" stroke="%s" stroke-width="%.1f" stroke-linecap="round"/>' % (cx, cy + 14 * s, cx - 5 * s, cy + 22 * s, cx - 11 * s, cy + 18 * s, cx, cy + 14 * s, cx + 5 * s, cy + 22 * s, cx + 11 * s, cy + 18 * s, INK, 2.4 * s)
    for sx in (-1, 1):
        for dy in (-3, 5): g += sk_line(cx + sx * 20 * s, cy + (14 + dy / 2) * s, cx + sx * 58 * s, cy + (12 + dy * 2) * s, INK, 2, .4, 1)
    return g
def paw(x, y, s=1.0, fill=DIM2):
    g = sk_ell(x, y + 5 * s, 9 * s, 7 * s, fill, fill, 1, .3)
    for dx, dy in ((-11, -4), (-4, -11), (4, -11), (11, -4)): g += sk_ell(x + dx * s, y + dy * s, 3.6 * s, 4.4 * s, fill, fill, 1, .2)
    return g
def note(x, y, s=1.0, col=DIM2):
    return '<g fill="%s" stroke="%s" stroke-width="%.1f" stroke-linecap="round"><ellipse cx="%.1f" cy="%.1f" rx="%.1f" ry="%.1f" transform="rotate(-20 %.1f %.1f)"/><path d="M%.1f %.1f L%.1f %.1f L%.1f %.1f" fill="none"/></g>' % (col, col, 2.5 * s, x, y, 7 * s, 5 * s, x, y, x + 6 * s, y, x + 6 * s, y - 24 * s, x + 16 * s, y - 20 * s)
def crate_art(x, y, w, h):
    """a plastic record crate, drawn by hand: ribs, lattice, handle slot and a row of sleeves on top"""
    g = ""
    for i in range(9):   # sleeves sticking out
        sx = x + 10 + i * (w - 30) / 9
        g += sk_poly([(sx, y), (sx + 14, y - 6), (sx + 14, y + 14), (sx, y + 20)], INK if i % 3 else GOLD, INK, 2, .6)
    g += sk_rect(x, y + 18, w, h - 18, 8, BLK, INK, 4, 1.6)
    g += sk_line(x + 6, y + 48, x + w - 6, y + 48, INK, 3, 1, 2) + sk_line(x + 6, y + h - 40, x + w - 6, y + h - 40, INK, 3, 1, 2)
    gx0, gx1, gy0, gy1 = x + 14, x + w - 14, y + 56, y + h - 48
    step = 34
    k = -int((gy1 - gy0) / step) - 1
    while gx0 + k * step < gx1 + (gy1 - gy0):
        xa = gx0 + k * step
        for sgn in (1, -1):
            xs, ys, xe, ye = (xa, gy0, xa + sgn * (gy1 - gy0), gy1)
            # clip the diagonal to the lattice box
            def clip(xs, ys, xe, ye):
                t0, t1 = 0.0, 1.0
                for p, q in ((-(xe - xs), xs - gx0), ((xe - xs), gx1 - xs), (-(ye - ys), ys - gy0), ((ye - ys), gy1 - ys)):
                    if p == 0:
                        if q < 0: return None
                    else:
                        t = q / p
                        if p < 0: t0 = max(t0, t)
                        else: t1 = min(t1, t)
                if t0 >= t1: return None
                return xs + (xe - xs) * t0, ys + (ye - ys) * t0, xs + (xe - xs) * t1, ys + (ye - ys) * t1
            c = clip(xs, ys, xe, ye)
            if c: g += sk_line(*c, stroke=INK, sw=2, j=.8, bend=1)
        k += 1
    g += sk_rect(x + w / 2 - 38, y + 26, 76, 16, 7, BLK, INK, 3, .8, False)    # handle slot
    return g

# ------------------------------------------------------------------ the platter: a record with a happy dog on the label, 48 turned pictures for the live meter
NPLAT, PS = 24, 250
def platter_base(S=500):
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(im); c = S / 2
    def circ(r, fill=None, outline=None, w=1, cx=None, cy=None):
        cx = c if cx is None else cx; cy = c if cy is None else cy
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill, outline=outline, width=w)
    circ(c - 3, fill=(14, 14, 15, 255), outline=(244, 241, 234, 255), w=6)
    for k in range(0, 9): circ(c - 20 - k * 13, outline=(52, 52, 54, 255), w=2)
    for a0 in (20, 200):                                                              # light streaks that turn with the record
        d.arc([c - 205, c - 205, c + 205, c + 205], a0, a0 + 34, fill=(120, 118, 112, 255), width=9)
        d.arc([c - 170, c - 170, c + 170, c + 170], a0 + 6, a0 + 26, fill=(86, 84, 80, 255), width=6)
    for a0 in (110, 290): d.arc([c - 215, c - 215, c + 215, c + 215], a0, a0 + 14, fill=(244, 241, 234, 255), width=5)
    circ(104, fill=(255, 181, 46, 255), outline=(244, 241, 234, 255), w=5)
    # the dog on the label (ears, head, snout, eyes, tongue)
    for sx in (-1, 1):
        d.ellipse([c + sx * 56 - 16, c - 36, c + sx * 56 + 16, c + 30], fill=(244, 241, 234, 255), outline=(14, 14, 15, 255), width=4)
    circ(58, fill=(14, 14, 15, 255), outline=(244, 241, 234, 255), w=4, cy=c + 4)
    d.ellipse([c - 30, c + 12, c + 30, c + 52], fill=(244, 241, 234, 255), outline=(14, 14, 15, 255), width=3)
    d.ellipse([c - 11, c + 14, c + 11, c + 30], fill=(14, 14, 15, 255))
    for sx in (-1, 1):
        d.ellipse([c + sx * 24 - 10, c - 30, c + sx * 24 + 10, c - 8], fill=(244, 241, 234, 255)); d.ellipse([c + sx * 24 - 5, c - 24, c + sx * 24 + 5, c - 12], fill=(14, 14, 15, 255))
    d.ellipse([c - 9, c + 36, c + 9, c + 62], fill=(255, 107, 138, 255), outline=(14, 14, 15, 255), width=2)
    circ(7, fill=(14, 14, 15, 255), outline=(244, 241, 234, 255), w=2)
    d.polygon([(c - 10, 8), (c + 10, 8), (c, 34)], fill=(255, 181, 46, 255))              # a gold marker at the rim
    return im
_base = platter_base()
_strip = Image.new("RGBA", (PS, PS * NPLAT), (0, 0, 0, 0))
for _k in range(NPLAT):
    _f = _base.rotate(-360.0 * _k / NPLAT, resample=Image.BICUBIC).resize((PS, PS), Image.LANCZOS)
    _strip.paste(_f, (0, _k * PS))
_strip.save(IMG + "/platter.png")

# ------------------------------------------------------------------ keys, rows, pads, segments
KIND = {   # fill, outline, text, shadow
    "rubber":    (BLK, INK, INK),
    "rubber_on": (INK, INK, BLK),
    "silver":    (INK, INK, BLK),
    "silver_on": (GOLD, INK, BLK),
}
def key_img(name, w, h, label, size, kind="rubber", on=False):
    fill, line, tc = KIND[kind + ("_on" if on else "")]
    body = sk_rect(6, 8, w - 8, h - 10, 11, "#000", "#000", 1, 0.3, False).replace('fill="#000"', 'fill="#000" opacity=".0"')
    body = '<path d="%s" fill="%s" opacity=".9"/>' % (smooth(rr_pts(8, 9, w - 10, h - 11, 11)), "#2c2b28")   # a hard comic shadow
    body += sk_rect(3, 3, w - 9, h - 11, 11, fill, line, 3)
    if label: body += txc(label, (w - 6) / 2, (h - 8) / 2 + 3, size, tc, "middle", "BebasNeue", 1.5)
    svg(name, w, h, body)
def pair(key, w, h, label, size, kind="rubber", fn=None):
    key_img("b_" + key, w, h, label, size, kind, False); key_img("b_%s_on" % key, w, h, label, size, kind, True)

# ------------------------------------------------------------------ the platter of CRATE RIPPA: the record with the dog logo on its gold label
def platter_rippa(S=500):
    im = platter_base(S)
    c = S / 2
    d = ImageDraw.Draw(im)
    d.ellipse([c - 104, c - 104, c + 104, c + 104], fill=(255, 181, 46, 255), outline=(244, 241, 234, 255), width=5)
    lg = Image.open(IMG + "/dog_logo.png").convert("RGBA")
    k = 176 / max(lg.size)
    lg = lg.resize((int(lg.width * k), int(lg.height * k)), Image.LANCZOS)
    im.alpha_composite(lg, (int(c - lg.width / 2), int(c - lg.height / 2)))
    d.ellipse([c - 7, c - 7, c + 7, c + 7], fill=(14, 14, 15, 255), outline=(244, 241, 234, 255), width=2)
    return im
_pb = platter_rippa()
_strip = Image.new("RGBA", (PS, PS * NPLAT), (0, 0, 0, 0))
for _k in range(NPLAT):
    _strip.paste(_pb.rotate(-360.0 * _k / NPLAT, resample=Image.BICUBIC).resize((PS, PS), Image.LANCZOS), (0, _k * PS))
_strip.save(IMG + "/platter.png")

# ------------------------------------------------------------------ pictures: waveform column (4 states x 24 heights), envelope column, sliders, scroll bar
WW, WH = 14, 120
_w = Image.new("RGBA", (WW, WH * WLEV * 4), (0, 0, 0, 0)); _wd = ImageDraw.Draw(_w)
for _st, _col in enumerate(((88, 85, 78, 255), (244, 241, 234, 255), (255, 107, 138, 255), (255, 181, 46, 255))):
    for _lv in range(WLEV):
        _y0 = (_st * WLEV + _lv) * WH; _h = int(_lv / (WLEV - 1.0) * (WH / 2 - 4)) + 1
        _wd.line([0, _y0 + WH // 2, WW - 1, _y0 + WH // 2], fill=(58, 56, 52, 255))
        if _st == 2: _wd.rectangle([WW // 2 - 1, _y0 + 1, WW // 2 + 1, _y0 + WH - 2], fill=(255, 107, 138, 255))   # a pad start / an end
        if _st == 3: _wd.rectangle([WW // 2 - 1, _y0 + 1, WW // 2 + 1, _y0 + WH - 2], fill=(255, 181, 46, 255))   # the playhead
        _wd.rectangle([2, _y0 + WH // 2 - _h, WW - 3, _y0 + WH // 2 + _h - 1], fill=_col)
_w.save(IMG + "/wave.png")
EW, EH = 17, 52
_e = Image.new("RGBA", (EW, EH * 24), (0, 0, 0, 0)); _ed = ImageDraw.Draw(_e)
for _lv in range(24):
    _h = int(_lv / 23.0 * (EH - 6)) + 2
    _ed.rectangle([1, _lv * EH + EH - 2 - _h, EW - 2, _lv * EH + EH - 2], fill=(255, 181, 46, 255 if _lv else 120))
_e.save(IMG + "/env.png")
Image.new("RGBA", (12, 250), (0, 0, 0, 0)).save(IMG + "/clr_thumb.png")
Image.new("RGBA", (300, 250), (0, 0, 0, 0)).save(IMG + "/clr_track.png")
def thumb(name, w, h):
    t = Image.new("RGBA", (w, h), (0, 0, 0, 0)); ImageDraw.Draw(t).polygon([(2, 2), (w - 2, 2), (w - 2, h - 12), (w // 2, h - 2), (2, h - 12)], fill=(255, 181, 46, 255), outline=(244, 241, 234, 255)); t.save(IMG + "/" + name)
def track(name, w, h, th=7):
    k = Image.new("RGBA", (w, h), (0, 0, 0, 0)); ImageDraw.Draw(k).rounded_rectangle([0, h // 2 - th // 2, w - 1, h // 2 + th // 2], radius=3, fill=(88, 85, 78, 255), outline=(244, 241, 234, 255)); k.save(IMG + "/" + name)
def pill(name, w, h):
    t = Image.new("RGBA", (w, h), (0, 0, 0, 0)); ImageDraw.Draw(t).rounded_rectangle([1, 1, w - 2, h - 2], radius=min(w, h) // 2 - 1, fill=(255, 181, 46, 255), outline=(244, 241, 234, 255), width=2); t.save(IMG + "/" + name)
thumb("ed_thumb.png", 22, 38); track("ed_track.png", 1120, 38)
pill("thumb_s.png", 16, 24); track("track_s.png", 270, 24, 5)
pill("seek_thumb.png", 18, 26); track("seek_track.png", 700, 26, 6)
pill("scr_thumb.png", 34, 64)
_sc = Image.new("RGBA", (40, 330), (0, 0, 0, 0)); ImageDraw.Draw(_sc).rounded_rectangle([14, 2, 25, 327], radius=5, fill=(40, 39, 36, 255), outline=(244, 241, 234, 255)); _sc.save(IMG + "/scr_track.png")

# ------------------------------------------------------------------ keys and buttons (CRATE DIGGA's sketchy keys)
for on in (False, True):
    fill, stroke = (("#3a2606", GOLD) if on else (BLK, INK))
    svg("row" + ("_on" if on else ""), 670, 44, sk_rect(3, 3, 663, 37, 10, fill, stroke, 2.8))
    svg("field" + ("_on" if on else ""), 470, 56, sk_rect(3, 3, 463, 49, 12, "#000", GOLD if not on else INK, 3))
for nm, on in (("pad", False), ("pad_on", True)):
    sh = '<path d="%s" fill="#2c2b28"/>' % smooth(rr_pts(8, 9, 130, 54, 12))
    svg(nm, 140, 66, sh + sk_rect(3, 3, 130, 54, 12, GOLD if on else BLK, INK, 3.2) + ('' if on else sk_line(14, 12, 48, 12, "#55524b", 3, .4, .6)))
for nm, on in (("seg_off", False), ("seg_on", True)):
    svg(nm, 180, 44, sk_rect(3, 3, 172, 36, 9, GOLD if on else BLK, INK, 2.8))
open(OUT + "/skin.css", "w").write(":root { --seg-size: 24px; --sheen: 0; --shadow: none; --label-size: 15px; }\n.seg-tx { font-weight: 700; letter-spacing: 0.05em; }\n")
pair("dyn", 142, 50, "", 1)
pair("dynw", 300, 50, "", 1)
for k, w, h, l, sz, kind in (("search", 120, 56, "SEARCH", 32, "silver"), ("history", 120, 56, "HISTORY", 28, "rubber"),
                             ("startstop", 160, 56, "START / STOP", 28, "silver"), ("cue", 100, 56, "CUE", 34, "rubber"),
                             ("rew", 76, 52, "<<", 32, "rubber"), ("fwd", 76, 52, ">>", 32, "rubber"), ("stop", 76, 52, "STOP", 28, "rubber"),
                             ("tap", 620, 70, "TAP TO CHOP", 48, "silver"), ("autohits", 380, 70, "AUTO CHOP HITS", 38, "silver"),
                             ("autobeats", 620, 40, "AUTO CHOP SLICES", 32, "silver"), ("detect", 140, 44, "DETECT", 28, "rubber"),
                             ("mpcbpm", 140, 44, "MPC BPM", 28, "rubber"), ("playstop", 170, 54, "PLAY / STOP", 28, "silver"),
                             ("cueset", 160, 54, "SET CUE", 30, "rubber"), ("cuego", 170, 54, "GO TO CUE", 30, "rubber"),
                             ("undo", 150, 54, "UNDO", 32, "rubber"), ("clrpad", 150, 54, "CLEAR PAD", 28, "rubber"),
                             ("clrbank", 160, 54, "CLEAR BANK", 28, "rubber"), ("learn", 200, 54, "LEARN PADS", 30, "rubber"),
                             ("export", 200, 54, "EXPORT", 34, "silver"), ("opcopy", 140, 48, "COPY", 28, "rubber"),
                             ("opmove", 140, 48, "MOVE", 28, "rubber"), ("opswap", 140, 48, "SWAP", 28, "rubber"),
                             ("opdel", 140, 48, "DELETE", 28, "rubber"), ("bkcopy", 140, 48, "BANK COPY", 24, "rubber"),
                             ("bkmove", 140, 48, "BANK MOVE", 24, "rubber"), ("bkswap", 140, 48, "BANK SWAP", 24, "rubber"),
                             ("stopall", 140, 48, "STOP ALL", 26, "silver"), ("fit", 100, 46, "FIT", 30, "rubber"),
                             ("padprev", 110, 46, "< PAD", 28, "rubber"), ("padnext", 110, 46, "PAD >", 28, "rubber"),
                             ("smm", 64, 46, "<<", 28, "rubber"), ("sm", 56, 46, "<", 28, "rubber"), ("sp", 56, 46, ">", 28, "rubber"),
                             ("spp", 64, 46, ">>", 28, "rubber"), ("edplay", 140, 50, "PLAY PAD", 28, "silver"),
                             ("minus", 52, 46, "-", 38, "rubber"), ("plus", 52, 46, "+", 38, "rubber"),
                             ("isosel", 330, 56, "TO SELECTED PAD", 30, "rubber"), ("isobank", 330, 56, "TO WHOLE BANK", 30, "silver"),
                             ("ytdlp", 240, 52, "UPDATE YT-DLP", 28, "rubber"),
                             ("kclose", 150, 62, "CLOSE", 30, "rubber"), ("kclr", 150, 62, "CLEAR", 30, "rubber"),
                             ("kspc", 420, 62, "SPACE", 32, "rubber"), ("kgo", 300, 62, "SEARCH", 36, "silver"), ("kdel", 160, 62, "DEL", 32, "rubber")):
    pair(k, w, h, l, sz, kind)
for c in "".join(KB_ROWS): pair("k" + c, 104, 62, c.upper(), 36)

# ------------------------------------------------------------------ background (every tab): black, sketchy header, the dog logo next to the name
bg = '<rect width="1280" height="628" fill="%s"/>' % BLK
bg += sk_line(10, 94, 1270, 94, INK, 4, 1.2, 2.5) + sk_line(14, 99, 1266, 99, INK, 1.8, 1, 2)
bg += sk_ell(56, 47, 44, 44, "none", INK, 3, 1.2)
bg += tx("CRATE RIPPA", 112, 62, 70, INK, "start", "BebasNeue", 3)
bg += sk_line(114, 70, 420, 70, GOLD, 5, 1.5, 2)
bg += tx("DIG IT  -  RIP IT  -  FLIP IT", 114, 88, 17, GOLD, "start", "BebasNeue", 3)
bg += paw(470, 30, 0.7) + note(490, 80, 0.6) + paw(1262, 300, .8) + paw(1250, 470, .6) + note(1262, 560, .8) + note(14, 330, .8) + paw(16, 520, .7)
bg += '<rect x="560" y="12" width="704" height="72" rx="9" fill="none" stroke="%s" stroke-width="3" stroke-linecap="round" stroke-dasharray="0.1 8" opacity=".75"/>' % INK
svg("bg", 1280, 628, bg)

def dots(cx, cy, w, h):   # a dotted white outline that shows where a live text line sits
    x, y = cx - w / 2, cy - 86 - h / 2
    return '<rect x="%.1f" y="%.1f" width="%d" height="%d" rx="9" fill="none" stroke="%s" stroke-width="3" stroke-linecap="round" stroke-dasharray="0.1 8" opacity=".75"/>' % (x, y, w, h, INK)
def L(s, x, y, size=18, fill="#a8a49a", anchor="start", sp=2, fn="BebasNeue"): return txc(s, x, y - 86, size, fill, anchor, fn, sp)
def overlay(name, parts, dl=()): svg(name, 1280, 628, "".join(parts) + "".join(dots(*d) for d in dl))
def box(x, y, w, h, title=None):   # a hand-drawn panel (plugin coordinates)
    g = sk_rect(x, y, w, h, 16, "none", INK, 3.5, 1.6)
    if title: g += '<rect x="%d" y="%d" width="%d" height="22" fill="%s"/>' % (x + 18, y - 11, int(text_w(title, 18, spacing=2)) + 16, BLK) + txc(title, x + 26, y, 18, GOLD, "start", "BebasNeue", 2)
    return g

_arm = (sk_ell(455, 150, 22, 22, "#2a2927", INK, 3, .8) + sk_ell(455, 150, 8, 8, INK, INK, 1, .3) +
        '<path d="M455 150 L300 292" stroke="%s" stroke-width="12" stroke-linecap="round"/><path d="M455 150 L300 292" stroke="%s" stroke-width="5" stroke-linecap="round"/>' % (INK, BLK) +
        sk_rect(280, 282, 38, 20, 6, GOLD, INK, 3, .8, False).replace('/>', ' transform="rotate(-45 299 292)"/>', 1) +
        sk_rect(446, 100, 18, 40, 5, INK, INK, 2, .6, False))
overlay("lbl_player", [sk_rect(16, 108, 504, 508, 22, "none", INK, 4, 1.8), '<circle cx="175" cy="259" r="138" fill="#050505" stroke="%s" stroke-width="3"/>' % INK, _arm,
    L("PITCH", 492, 318, 18, INK, "middle", 2), L("TOUCH THE RECORD TO SCRATCH", 175, 498, 17, INK, "middle", 2), L("RANGE", 352, 670, 15, "#a8a49a", sp=2),
    box(530, 108, 734, 508), L("SWIPE", 1238, 304, 13, GOLD, "middle", 1), L("SWIPE", 1238, 610, 13, GOLD, "middle", 1),
    L("SONG MIX", 1040, 652, 13, "#a8a49a", "middle", 2), L("PAD MIX", 1170, 652, 13, "#a8a49a", "middle", 2)],
    [(780, 284, 460, 26)])
_kb = '<rect x="10" y="104" width="1260" height="520" fill="%s"/>' % BLK + sk_rect(16, 108, 1248, 508, 22, BLK, INK, 4, 1.8) + sk_rect(48, 122, 1184, 56, 12, "#000", GOLD, 3)
svg("kb_panel", 1280, 628, _kb)
overlay("lbl_chop", [box(16, 108, 586, 508), box(612, 108, 652, 250, "CHOP"), box(612, 372, 652, 110, "TRANSPORT + CUE"), box(612, 496, 652, 120, "PADS")],
        [(305, 670, 560, 24), (940, 262, 620, 24), (790, 484, 300, 26), (1100, 484, 300, 26)])
overlay("lbl_wave", [box(16, 108, 1248, 326), box(16, 444, 1248, 172),
    L("START", 30, 270, 15, GOLD, sp=2), L("END", 30, 422, 15, GOLD, sp=2), L("ENVELOPE", 40, 532, 14, "#a8a49a", sp=3),
    L("PITCH", 700, 604, 14, "#a8a49a", "middle", 2), L("LEVEL", 820, 604, 14, "#a8a49a", "middle", 2)],
    [(640, 222, 600, 26), (640, 494, 420, 28)])
overlay("lbl_isolate", [box(16, 108, 1248, 246, "ISOLATE THE SONG"), box(16, 370, 1248, 246, "THE PADS"),
    L("LIGHT FILTERS, NO AI STEMS: WORKS BEST ON WIDE STEREO MIXES", 1240, 696, 14, "#a8a49a", "end", 1.5)],
    [(640, 400, 1180, 28), (440, 516, 800, 28)])
info = [("CRATE RIPPA  V" + VERSION_TXT, 42, GOLD), ("CREATOR: JJ DA PLUG", 26, INK), ("", 0, 0),
        ("WHAT IT DOES", 24, GOLD),
        ("SEARCH YOUTUBE (SONGS OR PLAYLISTS), SPIN THE SONG ON THE TURNTABLE, SCRATCH IT,", 19, "#cfcbc0"),
        ("CHOP IT ONTO 64 PADS (4 BANKS) BY HITS, BEATS OR LAZY, EDIT EVERY CHOP, EXPORT WAVS.", 19, "#cfcbc0"), ("", 0, 0),
        ("HOW", 24, GOLD),
        ("PLAYER: TAP THE SEARCH FIELD, TYPE, SEARCH. SWIPE THE BAR NEXT TO THE LIST TO SCROLL.", 19, "#cfcbc0"),
        ("CHOP: LAZY = PLAY AND HIT PADS. HITS / BEATS = SET THE CUE, AUTO CHOP. COPY, MOVE, SWAP, UNDO.", 19, "#cfcbc0"),
        ("WAVE: DRAG START AND END, FINE STEPS, ATTACK DECAY SUSTAIN RELEASE, PITCH, MUTE GROUPS.", 19, "#cfcbc0"),
        ("PADS: LEARN PADS ASKS FOR THE FIRST AND THE LAST PAD, ANY BANK, ANY KEY OR SCALE.", 19, "#cfcbc0"), ("", 0, 0),
        ("USES YT-DLP (UNLICENSE), FFMPEG (GPL), PYTHON (PSF). NOT MADE BY OR WITH YOUTUBE OR AKAI.", 16, "#a8a49a"),
        ("ONLY SAMPLE WHAT YOU MAY USE. CLEAR SAMPLES BEFORE YOU RELEASE MUSIC.", 16, "#a8a49a")]
parts = [box(16, 108, 336, 508), box(366, 108, 898, 508)]
yy = 236
for s_, sz, col in info:
    if s_: parts.append(L(s_, 396, yy, sz, col, sp=1.5))
    yy += 27 if s_ else 10
overlay("lbl_info", parts, [(1000, 664, 320, 30), (820, 700, 760, 26)])

# ------------------------------------------------------------------ layout
Lc = []
def add(s): Lc.append(s)
add("# CRATE RIPPA %s - JJ DA PLUG. Generated by gen_ui.py (edit that). Shadow coordinates: y = plugin y + 86." % VERSION_TXT)
add("art_css=skin.css\ntheme_bg=0e0e0f\ntheme_ink=f4f1ea\ntheme_accent=ffb52e\ntheme_accent_hi=ffb52e\ntheme_line=f4f1ea\ntheme_seg_active_tx=0e0e0f\n"
    "theme_knob_face=1c1b19\ntheme_knob_ring=3a3834\ntheme_knob_dot=ffb52e\ntheme_lcd=0e0e0f\ntheme_box=0e0e0f\nknob_look=rubber")
def W(when): return (" when=" + when) if when else ""
def btn(key, cx, cy, w, h, img, when=None):
    add("button cx=%d cy=%d label=\"\" key=%s img=images/b_%s.svg img_on=images/b_%s_on.svg w=%d h=%d%s" % (cx, cy, key, img, img, w, h, W(when)))
def gbtn(key, get, cx, cy, w, h, img, size=22, color="ffb52e", align="center", when=None):
    add("button cx=%d cy=%d label=\"\" key=%s get=%s tsize=%d tweight=bold tcolor=%s talign=%s img=images/%s.svg img_on=images/%s_on.svg w=%d h=%d%s" % (cx, cy, key, get, size, color, align, img, img, w, h, W(when)))
def ro_(key, cx, cy, w, h, size, color, align, weight="semibold", when=None):
    add("readout box=0 w=%d h=%d cx=%d cy=%d label=\"\" key=%s tsize=%d tweight=%s tcolor=%s talign=%s tpad=0%s" % (w, h, cx, cy, key, size, weight, color, align, W(when)))
def seg(key, cx, cy, sw, sh=44, when=None, rows=None):
    add("enum_h cx=%d cy=%d label=\"\" key=%s sw=%d sh=%d img=images/seg_off.svg img_on=images/seg_on.svg%s%s" % (cx, cy, key, sw, sh, " rows=%d" % rows if rows else "", W(when)))
def tab(name, lbl, ql):
    add("\n[tab %s]" % name)
    add("art file=images/bg.svg")
    add("art file=images/%s.svg" % lbl)
    add("art file=images/dog_logo.png x=14 y=90 w=84 h=88")
    ro_("status", 912, 120, 680, 30, 22, "ffb52e", "right", "bold")
    ro_("now", 912, 150, 680, 26, 19, "f4f1ea", "right")
    add('qlinks "%s" = %s' % (name, ql))
Y = lambda v: v + 86   # plugin y -> layout y
KBH = "kb:off"

# ---- PLAYER: the turntable (left), the search and the results (right), the mix knobs
tab("PLAYER", "lbl_player", "vol,padvol,deck_pitch,scroll")
add("meter cx=175 cy=345 w=250 h=250 key=plat strip=images/platter.png frames=%d when=%s" % (NPLAT, KBH))
add("slider_v cx=492 cy=420 w=50 h=170 label=\"\" key=deck_pitch look=fader ns=0 vs=0 bw=50 when=%s" % KBH)
ro_("pitch_txt", 500, 540, 100, 26, 18, "ffb52e", "center", "bold", KBH)
add("slider_h cx=175 cy=345 w=300 h=250 label=\"\" key=jog img=images/clr_thumb.png base=images/clr_track.png ns=0 vs=0 bw=300 when=%s" % KBH)
btn("play", 100, 604, 160, 56, "startstop", KBH)
btn("deck_cue", 235, 604, 100, 56, "cue", KBH)
seg("klock", 390, 604, 95, 50, KBH)
seg("prange", 180, 664, 80, 44, KBH)
gbtn("kb_open", "q_txt", 770, Y(138), 440, 56, "field", 24, "ffb52e", "left", KBH)
seg("find", 1105, Y(138), 95, 50, KBH)
btn("history", 1206, Y(198), 100, 50, "history", KBH)
btn("search", 1086, Y(198), 120, 50, "search", KBH)
ro_("res_head", 780, Y(198), 460, 26, 17, "a8a49a", "left", "semibold", KBH)
for i in range(ROWS): gbtn("rt%d" % (i + 1), "r%d" % (i + 1), 878, Y(238 + i * 42), 664, 38, "row", 19, "f4f1ea", "left", KBH)
add("slider_v cx=1238 cy=%d w=40 h=290 label=\"\" key=scroll img=images/scr_thumb.png base=images/scr_track.png ns=0 vs=0 bw=56 when=%s" % (Y(370), KBH))
add("slider_h cx=830 cy=%d w=560 h=26 label=\"\" key=seek img=images/seek_thumb.png base=images/seek_track.png ns=0 vs=0 bw=560 when=%s" % (Y(526), KBH))
ro_("time", 1180, Y(526), 150, 28, 19, "ffb52e", "right", "bold", KBH)
btn("rew", 584, Y(594), 76, 44, "rew", KBH)
btn("fwd", 664, Y(594), 76, 44, "fwd", KBH)
btn("stop", 744, Y(594), 76, 44, "stop", KBH)
seg("loop", 870, Y(594), 80, 42, KBH)
add("knob cx=1040 cy=%d r=20 label=\"\" key=vol lay=side bw=100 bh=44 vs=17 when=%s" % (Y(596), KBH))
add("knob cx=1170 cy=%d r=20 label=\"\" key=padvol lay=side bw=100 bh=44 vs=17 when=%s" % (Y(596), KBH))
# the keyboard over the whole page
add("art file=images/kb_panel.svg when=kb:on")
ro_("query_kb", 640, Y(150), 1140, 44, 30, "ffb52e", "left", "bold", "kb:on")
KW, KH, KG = 104, 62, 10
for r, row in enumerate(KB_ROWS):
    x0 = (1280 - (len(row) * (KW + KG) - KG)) // 2 - (40 if r == 3 else 0)
    for i, ch in enumerate(row): btn("k_" + ch, x0 + i * (KW + KG) + KW // 2, Y(224 + r * 74), KW, KH, "k" + ch, "kb:on")
btn("k_del", 1100, Y(224 + 3 * 74), 160, 62, "kdel", "kb:on")
for k, w, cx, im in (("k_close", 150, 150, "kclose"), ("k_clr", 150, 320, "kclr"), ("k_spc", 420, 640, "kspc"), ("k_go", 300, 1060, "kgo")):
    btn(k, cx, Y(224 + 4 * 74), w, 62, im, "kb:on")

# ---- CHOP: 16 pads (pad 1 bottom left), banks, pad actions (left); chop modes, transport + cue, the main actions (right)
tab("CHOP", "lbl_chop", "chop_by,sens,bpm,slice")
for r in range(4):
    for c in range(4):
        n = (3 - r) * 4 + c + 1
        gbtn("pd%d" % n, "pn%d" % n, 92 + c * 144, Y(146 + r * 70), 140, 66, "pad", 20, "f4f1ea")
for c in range(4): gbtn("bk%d" % (c + 1), "bt%d" % (c + 1), 92 + c * 144, Y(436), 140, 50, "b_dyn", 22)
for c, (k, im) in enumerate((("op_copy", "opcopy"), ("op_move", "opmove"), ("op_swap", "opswap"), ("op_del", "opdel"))): btn(k, 92 + c * 144, Y(490), 140, 48, im)
for c, (k, im) in enumerate((("bk_copy", "bkcopy"), ("bk_move", "bkmove"), ("bk_swap", "bkswap"), ("stop_all", "stopall"))): btn(k, 92 + c * 144, Y(542), 140, 48, im)
ro_("ops_txt", 305, Y(584), 560, 24, 17, "ffb52e", "left")
ro_("sel", 305, Y(606), 560, 22, 16, "f4f1ea", "left")
seg("chop_by", 840, Y(138), 130, 50)
ro_("hint", 940, Y(176), 620, 24, 15, "a8a49a", "left")
seg("assign", 790, Y(216), 140, 46, "chop_by:LAZY")
seg("arm", 1100, Y(216), 140, 46, "chop_by:LAZY")
btn("tap", 938, Y(296), 620, 70, "tap", "chop_by:LAZY")
add("knob cx=700 cy=%d r=34 label=\"SENSITIVITY\" key=sens when=chop_by:HITS" % Y(270))
btn("auto", 1040, Y(272), 380, 70, "autohits", "chop_by:HITS")
add("knob cx=680 cy=%d r=24 label=\"TEMPO\" key=bpm bw=110 when=chop_by:BEATS" % Y(222))
btn("bpm_detect", 860, Y(212), 140, 44, "detect", "chop_by:BEATS")
btn("bpm_mpc", 860, Y(262), 140, 44, "mpcbpm", "chop_by:BEATS")
seg("slice", 1090, Y(236), 104, 40, "chop_by:BEATS", rows=2)
btn("auto", 938, Y(330), 620, 40, "autobeats", "chop_by:BEATS")
ro_("time", 790, Y(398), 300, 26, 19, "ffb52e", "left", "bold")
ro_("cue_txt", 1100, Y(398), 300, 26, 19, "ff6b8a", "right", "bold")
btn("play", 710, Y(446), 170, 54, "playstop")
btn("cue_set", 885, Y(446), 160, 54, "cueset")
btn("cue_go", 1060, Y(446), 170, 54, "cuego")
btn("undo", 700, Y(536), 150, 54, "undo")
btn("clr_pad", 860, Y(536), 150, 54, "clrpad")
btn("clr_bank", 1025, Y(536), 160, 54, "clrbank")
btn("learn", 740, Y(594), 200, 54, "learn")
btn("export", 960, Y(594), 200, 54, "export")

# ---- WAVE: the selected pad on the waveform (drag START / END, fine steps), its envelope and its sound
tab("WAVE", "lbl_wave", "pitch,level,ed_a,ed_r")
seg("zoom", 130, Y(136), 100, 46)
ro_("ed_pad_txt", 640, Y(136), 600, 26, 18, "f4f1ea", "center", "bold")
btn("fit", 978, Y(136), 100, 46, "fit")
btn("pad_prev", 1090, Y(136), 110, 46, "padprev")
btn("pad_next", 1200, Y(136), 110, 46, "padnext")
add("slider_h cx=640 cy=%d w=1120 h=38 label=\"\" key=ed_start img=images/ed_thumb.png base=images/ed_track.png ns=0 vs=0 bw=1120" % Y(184))
for i in range(WCOLS): add("meter cx=%d cy=%d w=%d h=%d key=wv%d strip=images/wave.png frames=%d" % (80 + i * WW + WW // 2, Y(268), WW, WH, i, WLEV * 4))
add("slider_h cx=640 cy=%d w=1120 h=38 label=\"\" key=ed_end img=images/ed_thumb.png base=images/ed_track.png ns=0 vs=0 bw=1120" % Y(336))
for i, (k, im, w) in enumerate((("s_mm", "smm", 64), ("s_m", "sm", 56), ("s_p", "sp", 56), ("s_pp", "spp", 64))): btn(k, (66, 130, 188, 252)[i], Y(410), w, 44, im)
for i, (k, im, w) in enumerate((("e_mm", "smm", 64), ("e_m", "sm", 56), ("e_p", "sp", 56), ("e_pp", "spp", 64))): btn(k, (1028, 1092, 1150, 1214)[i], Y(410), w, 44, im)
ro_("ed_pos_txt", 640, Y(408), 420, 28, 20, "ffb52e", "center", "bold")
for i in range(NEV): add("meter cx=%d cy=%d w=%d h=%d key=ev%d strip=images/env.png frames=24" % (40 + i * EW + EW // 2, Y(484), EW, EH, i))
for c, key in enumerate(("ed_a", "ed_d", "ed_s", "ed_r")):
    ro_(key + "_txt", 104 + c * 146, Y(530), 140, 22, 16, "ffb52e", "center", "bold")
    add("slider_h cx=%d cy=%d w=136 h=24 label=\"\" key=%s img=images/thumb_s.png base=images/track_s.png ns=0 vs=0 bw=136" % (104 + c * 146, Y(560), key))
seg("pmode", 750, Y(466), 120, 44)
seg("rev", 1010, Y(466), 120, 44)
add("knob cx=700 cy=%d r=24 label=\"\" key=pitch ns=0 bw=110" % Y(546))
add("knob cx=820 cy=%d r=24 label=\"\" key=level ns=0 bw=110" % Y(546))
btn("grp_m", 935, Y(530), 52, 46, "minus")
ro_("grp_txt", 1010, Y(530), 96, 30, 17, "ffb52e", "center", "bold")
btn("grp_p", 1085, Y(530), 52, 46, "plus")
seg("velo", 1010, Y(588), 90, 40)
btn("ed_play", 1185, Y(530), 140, 50, "edplay")
ro_("sel", 1185, Y(588), 150, 26, 14, "a8a49a", "center")

# ---- ISOLATE
tab("ISOLATE", "lbl_isolate", "iso,iso_pad")
seg("iso", 640, Y(178), 300, 54, rows=2)
ro_("hint_iso", 640, Y(314), 1180, 28, 18, "a8a49a", "center")
btn("play", 120, Y(430), 160, 56, "startstop")
ro_("sel", 440, Y(430), 400, 28, 19, "ffb52e", "left", "bold")
seg("iso_pad", 640, Y(500), 170, 46)
btn("iso_sel", 450, Y(570), 330, 56, "isosel")
btn("iso_bank", 830, Y(570), 330, 56, "isobank")

# ---- INFO (last)
tab("INFO", "lbl_info", "-")
add("art file=images/jf_gold.png x=44 y=%d w=280 h=448" % Y(140))
ro_("version", 1180, Y(132), 140, 30, 22, "ffb52e", "right", "bold")
ro_("ytdlp_txt", 1000, Y(578), 320, 30, 18, "a8a49a", "left")
btn("ytdlp_update", 1140, Y(530), 240, 52, "ytdlp")
open(OUT + "/layout.conf", "w").write("\n".join(Lc) + "\n")

json.dump({"name": "Crate Rippa", "vendor": "JJ DA PLUG", "uid": "JJcR", "version": 1002, "so": "jj_craterippa.so", "params": "params.json",
           "layout": "layout.conf", "art": "html", "targets": ["armv7", "aarch64"],
           "defines": {"PARAM_TEXT_MAX": 128, "HAS_DISPLAY_REV": 1, "HAS_SONG_POS": 1, "HAS_LFO_BPM": 1, "MODULE_SUBDIR": "\"craterippa\""},
           "build": {"root": ".", "sources": ["engine.c"], "cflags": ["-O2", "-Wno-format-truncation"],
                     # Live III (Gen2, RK3588: Cortex-A76 + A55): -O3 tuned for its cores, plain ARMv8-A code so every core runs it
                     "cflags_aarch64": ["-O3", "-march=armv8-a", "-mtune=cortex-a76.cortex-a55"], "libs": ["-lpthread", "-lm", "-ldl"]}},
          open(OUT + "/vst.json", "w"), indent=1)
print("params", len(P), "layout lines", len(Lc))
