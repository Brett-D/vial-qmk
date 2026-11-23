"""Pixel art for the OLED pets: seven original species, each growing
egg -> baby -> child -> adult.

Bodies are built from simple shapes (their union becomes one clean outline,
plus a few detail strokes), drawn at three sizes; shared face glyphs (eyes,
mouths) are stamped at each species' anchors. Every frame is pre-rendered
here, so the firmware (../pet_art.h) and the config page (pet_art.json) just
copy pixels and can never disagree.

    python gen_pet_art.py            writes pet_art.h, pet_art.json
    python gen_pet_art.py --sheet F  also renders a review sheet PNG (needs Pillow)
"""
import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

SPECIES = ["pip", "cat", "dog", "owl", "dragon", "blob", "axolotl"]
SPECIES_NAMES = ["Pip", "Cat", "Dog", "Owl", "Dragon", "Blob", "Axolotl"]
STAGES = ["baby", "child", "adult"]  # the egg is separate
FRAMES = ["idle", "idle2", "blink", "happy", "sad", "tired", "sleep", "eat", "dead"]
SCALE = {"baby": 0.62, "child": 0.78, "adult": 1.0}
SIZE = {"baby": (18, 16), "child": (22, 20), "adult": (28, 25)}
GLYPH = {"baby": "S", "child": "M", "adult": "L"}

# ---------------------------------------------------------------- face glyphs
# Eyes: anchor = top-left of the box; the right eye is the mirror image.
EYES = {
    "L": {
        "open": [".##.", ".##.", ".##.", "...."],
        "blink": ["....", "....", ".##.", "...."],
        "happy": ["....", ".##.", "#..#", "...."],
        "sad": ["#...", ".#..", ".##.", ".##."],
        "tired": ["....", "####", ".##.", "...."],
        "sleep": ["....", "....", "#..#", ".##."],
        "dead": ["#.#.", ".#..", "#.#.", "...."],
    },
    "M": {
        "open": ["##.", "##.", "..."],
        "blink": ["...", "##.", "..."],
        "happy": [".#.", "#.#", "..."],
        "sad": ["#..", ".##", ".##"],
        "tired": ["###", ".#.", "..."],
        "sleep": ["...", "#.#", ".#."],
        "dead": ["#.#", ".#.", "#.#"],
    },
    "S": {
        "open": ["#.", "#."],
        "blink": ["..", "#."],
        "happy": [".#", "#."],
        "sad": ["#.", "#."],
        "tired": ["##", ".."],
        "sleep": ["..", "##"],
        "dead": ["#.", ".#"],
    },
}
MOUTHS = {
    "L": {
        "smile": ["#..#", ".##.", "....", "...."],
        "happy": ["####", "#..#", ".##.", "...."],
        "eat": [".##.", "#..#", "#..#", ".##."],
        "frown": [".##.", "#..#", "....", "...."],
        "flat": ["####", "....", "....", "...."],
        "o": [".##.", ".##.", "....", "...."],
    },
    "M": {
        "smile": ["#.#", ".#.", "..."],
        "happy": ["###", ".#.", "..."],
        "eat": [".#.", "#.#", ".#."],
        "frown": [".#.", "#.#", "..."],
        "flat": ["###", "...", "..."],
        "o": [".#.", "...", "..."],
    },
    "S": {
        "smile": ["#.#", ".#."],
        "happy": ["###", ".#."],
        "eat": [".#.", "#.#"],
        "frown": [".#.", "#.#"],
        "flat": ["##.", "..."],
        "o": [".#.", "..."],
    },
}
FACE = {  # frame -> (eyes, mouth)
    "idle": ("open", "smile"), "idle2": ("open", "smile"), "blink": ("blink", "smile"), "happy": ("happy", "happy"),
    "sad": ("sad", "frown"), "tired": ("tired", "flat"), "sleep": ("sleep", "o"), "eat": ("open", "eat"), "dead": ("dead", "flat"),
}


# ---------------------------------------------------------------- canvas
class Canvas:
    """Shapes in adult coordinates, scaled into a stage-sized grid."""

    def __init__(self, w, h, s, ox=0.0, oy=0.0):
        self.w, self.h, self.s, self.ox, self.oy = w, h, s, ox, oy
        self.fine = s >= 0.99   # adult: all details
        self.mid = s >= 0.7     # child and adult
        self.mask = set()
        self.detail = set()
        self.erase = set()

    def T(self, x, y):
        return x * self.s + self.ox, y * self.s + self.oy

    def _fill(self, inside, target):
        for y in range(self.h):
            for x in range(self.w):
                if inside(x + 0.5, y + 0.5):
                    target.add((x, y))

    def ell(self, cx, cy, rx, ry, target=None):
        cx, cy = self.T(cx, cy)
        rx, ry = max(rx * self.s, 0.8), max(ry * self.s, 0.8)
        self._fill(lambda x, y: ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0, self.mask if target is None else target)

    def poly(self, pts, target=None):
        P = [self.T(x, y) for x, y in pts]

        def inside(x, y):
            c = False
            for i in range(len(P)):
                (x1, y1), (x2, y2) = P[i], P[i - 1]
                if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
                    c = not c
            return c

        self._fill(inside, self.mask if target is None else target)

    def rect(self, x0, y0, x1, y1, target=None):
        (a, b), (c, d) = self.T(x0, y0), self.T(x1, y1)
        self._fill(lambda x, y: a <= x <= c and b <= y <= d, self.mask if target is None else target)

    def thick(self, pts, r, target=None):
        P = [self.T(x, y) for x, y in pts]
        r = max(r * self.s, 0.75)

        def near(x, y):
            for (x1, y1), (x2, y2) in zip(P, P[1:]):
                dx, dy = x2 - x1, y2 - y1
                t = max(0, min(1, ((x - x1) * dx + (y - y1) * dy) / (dx * dx + dy * dy or 1)))
                if (x - x1 - t * dx) ** 2 + (y - y1 - t * dy) ** 2 <= r * r:
                    return True
            return False

        self._fill(near, self.mask if target is None else target)

    def line(self, pts):
        """Detail stroke (1 px), adult coordinates."""
        P = [self.T(x, y) for x, y in pts]
        for (x1, y1), (x2, y2) in zip(P, P[1:]):
            n = int(max(abs(x2 - x1), abs(y2 - y1)) * 2) + 1
            for i in range(n + 1):
                t = i / n
                self.detail.add((int(x1 + (x2 - x1) * t), int(y1 + (y2 - y1) * t)))

    def ring(self, cx, cy, r):
        """Detail circle outline."""
        tmp = set()
        self.ell(cx, cy, r, r, tmp)
        for x, y in tmp:
            if any((x + dx, y + dy) not in tmp for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                self.detail.add((x, y))

    def dot(self, x, y):
        x, y = self.T(x, y)
        self.detail.add((int(x), int(y)))

    def grid(self):
        g = [[0] * self.w for _ in range(self.h)]
        for x, y in self.mask:
            if any((x + dx, y + dy) not in self.mask for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                g[y][x] = 1
        for x, y in self.detail:
            if 0 <= x < self.w and 0 <= y < self.h:
                g[y][x] = 1
        for x, y in self.erase:
            if 0 <= x < self.w and 0 <= y < self.h:
                g[y][x] = 0
        return g


def stamp(g, glyph, x, y, mirror=False):
    for r, row in enumerate(glyph):
        row = row[::-1] if mirror else row
        for c, ch in enumerate(row):
            if ch == "#" and 0 <= y + r < len(g) and 0 <= x + c < len(g[0]):
                g[y + r][x + c] = 1


def clear_box(g, x, y, w, h):
    for r in range(h):
        for c in range(w):
            if 0 <= y + r < len(g) and 0 <= x + c < len(g[0]):
                g[y + r][x + c] = 0


# ---------------------------------------------------------------- species
# Each draws in adult coordinates (about 28 x 25) and returns face anchors
# (adult coordinates): left eye box, right eye box, mouth box, head top.

def pip(c, f, st):
    c.ell(13.5, 14.5, 10.6, 8.6)
    for x in (9, 17):
        c.rect(x - 1, 23, x + 1, 23.9)
    tip = (16.5, 2) if f != "idle2" else (18, 3)
    c.thick([(14, 6), (14.5, 4), tip], 0.5)
    c.ell(tip[0] + 0.5, tip[1] - 0.5, 1.3, 1.3)
    return dict(eyes=((7, 10), (16, 10)), mouth=(11.5, 16), top=4)


def cat(c, f, st):
    c.ell(13, 10.5, 8, 6.8)  # head
    c.poly([(5.5, 7), (6, -0.5), (11, 4.5)])  # ears
    c.poly([(15, 4.5), (20, -0.5), (20.5, 7)])
    c.ell(14, 19.5, 7, 4.8)  # body
    tail = [(19.5, 20), (24, 18), (25, 13), (23, 9.5)] if f != "idle2" else [(19.5, 20), (24, 18.5), (26.5, 14.5), (27, 10.5)]
    c.thick(tail, 1.1)
    if c.fine:
        c.line([(7, 3), (8.5, 6)])  # inner ears
        c.line([(19, 3), (17.5, 6)])
    for y0, y1 in ((12.5, 12), (14, 14.5)) if c.mid else ():  # whiskers
        c.line([(1.5, y0), (4.5, y1 + 0.3)])
        c.line([(21.5, y1 + 0.3), (24.5, y0)])
    for x in (10, 17):
        c.rect(x - 1.2, 23.3, x + 1.2, 24.5)
    return dict(eyes=((7.5, 8), (14.5, 8)), mouth=(11, 12.5), top=1)


def dog(c, f, st):
    c.ell(13.5, 10, 7.6, 6.8)  # head
    for sgn in (-1, 1):  # long floppy ears, hanging past the cheeks
        x = 13.5 + sgn * 6.5
        c.poly([(x, 4.5), (x + sgn * 3, 5.5), (x + sgn * 4, 11), (x + sgn * 3, 16), (x + sgn * 0.5, 15.5), (x - sgn * 0.5, 9)])
        c.line([(x - sgn * 0.3, 5.5), (x - sgn * 0.6, 9), (x + sgn * 0.3, 14.5)])
    c.ell(13.5, 20, 7, 4.5)  # body
    tail = [(20, 19), (24, 15.5), (24.5, 12.5)] if f != "idle2" else [(20, 19), (24.5, 18.5), (27, 16)]
    c.thick(tail, 1.1)
    c.ell(13.5, 10.8, 1.7, 1.0, c.detail)  # nose
    for x in (10, 17):
        c.rect(x - 1.2, 23.5, x + 1.2, 24.5)
    return dict(eyes=((7.8, 6), (15.2, 6)), mouth=(12, 13), top=3)


def owl(c, f, st):
    tilt = 1 if f == "idle2" else 0
    c.ell(13.5, 13.5, 10, 10.5)  # body
    c.poly([(5, 6 + tilt), (6.5, -0.5 + tilt), (10, 4)])  # ear tufts
    c.poly([(17, 4), (20.5, -0.5 - tilt * 0), (22, 6 - tilt)])
    if c.mid:
        c.ring(9.5, 10.5, 4)  # eye rings
        c.ring(17.5, 10.5, 4)
    c.line([(12.5, 14), (13.5, 16), (14.5, 14)])  # beak
    if c.mid:
        c.line([(4.5, 14), (5.5, 19), (8, 22)])  # wings
        c.line([(22.5, 14), (21.5, 19), (19, 22)])
    for x, y in ((11, 19), (13.5, 20), (16, 19)) if c.fine else ():  # chest
        c.line([(x - 1, y), (x, y + 1), (x + 1, y)])
    for x in (10, 17):
        c.line([(x - 1, 24.3), (x, 23.5), (x + 1, 24.3)])
    return dict(eyes=((8, 9), (16, 9)), mouth=(12, 16.5), top=1, beak=True)


def dragon(c, f, st):
    c.ell(13.5, 9.5, 7, 6.2)  # head
    c.poly([(8, 5), (6.5, -0.5), (10.5, 3.5)])  # horns
    c.poly([(16.5, 3.5), (20.5, -0.5), (19, 5)])
    c.ell(13.5, 19.5, 6.3, 5)  # body
    tail = [(18.5, 21.5), (23, 22.5), (26, 20)] if f != "idle2" or st == "adult" else [(18.5, 21.5), (23, 21), (25, 17)]
    c.thick(tail, 1.0)
    tx, ty = tail[-1]
    c.poly([(tx - 1, ty - 1.5), (tx + 2.5, ty - 2.5), (tx + 1, ty + 1.5)])  # tail spike
    c.dot(12.3, 11.6)  # nostrils
    c.dot(14.7, 11.6)
    for y in (18, 20.5) if c.mid else ():  # belly scales
        c.line([(11.5, y), (15.5, y)])
    if st == "adult":  # wings, flapping in idle2
        up = -3 if f == "idle2" else 0
        for sgn in (-1, 1):
            c.poly([(13.5 + sgn * 5, 14.5), (13.5 + sgn * 12.5, 6 + up), (13.5 + sgn * 12, 12 + up), (13.5 + sgn * 14, 14.5 + up), (13.5 + sgn * 8, 18)])
    for x in (10, 17):
        c.rect(x - 1.2, 23.6, x + 1.2, 24.6)
    return dict(eyes=((7.5, 6.5), (15.5, 6.5)), mouth=(12, 13), top=2)


def blob(c, f, st):
    if f == "idle2":  # squish
        c.ell(13.5, 18, 12.6, 9)
    else:
        c.ell(13.5, 16.5, 11, 10.5)
    c.erase.update((x, y) for x in range(c.w) for y in range(c.h) if y > c.T(0, 24.3)[1])
    c.mask = {(x, y) for x, y in c.mask if y <= c.T(0, 24.3)[1]}
    c.line([(3.5, 17), (4, 14.5), (5.5, 12.5)])  # shine
    c.line([(1, 24), (26, 24)])
    lift = 1.5 if f == "idle2" else 0
    return dict(eyes=((7.5, 11 + lift), (15.5, 11 + lift)), mouth=(11.5, 16 + lift), top=6 + lift)


def axolotl(c, f, st):
    c.ell(14, 11, 9.5, 6.3)  # head
    w = 1 if f == "idle2" else 0
    for sgn in (-1, 1):  # gill fronds
        x0 = 14 + sgn * 8
        for dy, (ex, ey) in ((-3, (5, -4 - w)), (0, (6, 0 + w)), (3, (5, 4 - w))):
            c.thick([(x0, 11 + dy), (x0 + sgn * ex, 11 + dy + ey)], 0.7)
            if c.fine:  # feathery
                c.dot(x0 + sgn * ex * 0.6, 11 + dy + ey * 0.6 - 1.2)
    c.ell(14, 19.5, 5.5, 4)  # body
    c.thick([(18.5, 20.5), (23.5, 21.5), (27, 20.5 - w)], 1.0)  # tail
    for x in (10.5, 17.5):
        c.rect(x - 1, 22.5, x + 1, 24.4)
    return dict(eyes=((7, 9.5), (17, 9.5)), mouth=(12, 13.5), top=5)


DRAW = {"pip": pip, "cat": cat, "dog": dog, "owl": owl, "dragon": dragon, "blob": blob, "axolotl": axolotl}


def frame(species, stage, f):
    s = SCALE[stage]
    w, h = SIZE[stage]
    if species == "dragon" and stage == "adult":
        w = 32
    ox = (w - 28 * s) / 2  # centred
    oy = h - 25 * s
    c = Canvas(w, h, s, ox, oy)
    a = DRAW[species](c, "idle2" if f == "idle2" else "idle", stage)
    g = c.grid()
    gs = GLYPH[stage]
    eyes, mouth = FACE[f]
    ex = [int(round(c.T(x, y)[0])) for x, y in a["eyes"]]
    ey = [int(round(c.T(x, y)[1])) for x, y in a["eyes"]]
    eye = EYES[gs][eyes]
    stamp(g, eye, ex[0], ey[0])
    stamp(g, eye, ex[1], ey[1], mirror=True)
    if f == "sad":
        stamp(g, ["#", "#"], ex[1] + len(eye[0]), ey[1] + len(eye))  # tear
    mx, my = (int(round(v)) for v in c.T(*a["mouth"]))
    if a.get("beak") and mouth in ("smile", "flat", "o", "frown"):
        pass  # the owl's beak says it all
    else:
        mg = MOUTHS[gs][mouth]
        if a.get("beak"):
            mg = MOUTHS["S"][mouth]
        stamp(g, mg, mx, my)
    if f == "dead":  # halo above the head
        hx, _ = c.T(13.5, 0)
        hw = max(2, int(round(3.5 * s)))
        top = max(0, int(round(c.T(0, a["top"])[1])) - 3)
        g = [[0] * w for _ in range(3)] + g  # 3 rows taller
        for x in range(int(hx) - hw, int(hx) + hw + 1):
            g[top][x] = 1 if 0 <= x < w and x not in (int(hx) - hw, int(hx) + hw) else g[top][x]
            if x in (int(hx) - hw, int(hx) + hw) and 0 <= x < w:
                g[top + 1][x] = 1
        for x in range(int(hx) - hw + 1, int(hx) + hw):
            if 0 <= x < w:
                g[top + 2][x] = 1
    return g


def egg(species):
    """16 x 18 egg with a species pattern."""
    w, h = 16, 18
    g = [[0] * w for _ in range(h)]
    cx, cy = 7.5, 9.5
    inside = lambda x, y: ((x - cx) / 6.5) ** 2 + ((y - cy) / (8.6 if y < cy else 7.6)) ** 2 <= 1
    for y in range(h):
        for x in range(w):
            if inside(x, y) and not all(inside(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                g[y][x] = 1
    pat = {
        "pip": [(7, 4), (8, 4), (7, 3)],
        "cat": [(4, 7), (5, 8), (6, 7), (9, 7), (10, 8), (11, 7), (5, 12), (6, 13), (7, 12), (8, 13), (9, 12), (10, 13)],
        "dog": [(5, 6), (6, 6), (5, 7), (10, 10), (11, 10), (10, 11), (6, 13), (7, 13)],
        "owl": [(5, 8), (6, 9), (7, 8), (8, 8), (9, 9), (10, 8), (6, 12), (7, 13), (8, 12), (9, 13)],
        "dragon": [(3, 9), (5, 7), (7, 9), (9, 7), (11, 9), (12, 8), (4, 12), (6, 11), (8, 12), (10, 11), (12, 12)],
        "blob": [(6, 6), (7, 6), (6, 7), (9, 11), (9, 12), (10, 12)],
        "axolotl": [(3, 10), (4, 9), (5, 10), (6, 11), (7, 10), (8, 9), (9, 10), (10, 11), (11, 10), (12, 9)],
    }[species]
    for x, y in pat:
        g[y][x] = 1
    return g


# ---------------------------------------------------------------- icons (8x8)
ICONS = {
    "heart": ["........", ".##.##..", "#######.", "#######.", ".#####..", "..###...", "...#....", "........"],
    "z": ["........", ".#####..", "....#...", "...#....", "..#.....", ".#####..", "........", "........"],
    "apple": ["....#...", "...#.##.", ".##.##..", "#######.", "#######.", "#######.", ".#####..", "..#.#..."],
    "ball": ["..###...", ".#.#.#..", "#..#..#.", "#######.", "#..#..#.", ".#.#.#..", "..###...", "........"],
    "moon": ["..###...", ".##.....", "##......", "##......", "##......", ".##...#.", "..####..", "........"],
    "sparkle": ["...#....", "...#....", ".#.#.#..", "..###...", "#######.", "..###...", ".#.#.#..", "...#...."],
    "poop": ["...#....", "..##....", ".####...", ".#..#...", "######..", "#.##.##.", "########", "........"],
    "no": ["#.....#.", ".#...#..", "..#.#...", "...#....", "..#.#...", ".#...#..", "#.....#.", "........"],
    "egg": ["..###...", ".#...#..", "#.#...#.", "#.....#.", "#..#..#.", "#.....#.", ".#####..", "........"],
    "smile": ["..###...", ".#...#..", "#.#.#.#.", "#.....#.", "#.#.#.#.", "#..#..#.", ".#...#..", "..###..."],
    "tear": ["...#....", "...#....", "..###...", "..###...", ".#####..", ".##.##..", "..###...", "........"],
    "bolt": ["....##..", "...##...", "..##....", ".######.", "....##..", "...##...", "..##....", ".##....."],
    "star": ["...#....", "...#....", "..###...", "#######.", ".#####..", "..###...", ".##.##..", "##...##."],
    # species food / play
    "fish": ["........", "......#.", ".####.##", "#.#..###", "#....###", ".####.##", "......#.", "........"],
    "yarn": ["..####..", ".#.#..#.", "#.#..#.#", "#..##..#", "#.#..#.#", ".#..#.#.", "..####.#", "........"],
    "bone": ["........", ".#....#.", "#.####.#", ".#....#.", "#.####.#", ".#....#.", "........", "........"],
    "mouse": ["........", ".##.....", "#..#....", "#.####..", ".#....#.", ".#.#..##", "..####.#", "......#."],
    "feather": ["......##", ".....###", "....###.", "...###..", "..###...", ".###....", ".#......", "#......."],
    "coal": ["........", "..###...", ".#.#.#..", "#.#.#.#.", "#..#..#.", ".#.#.#..", "..###...", "........"],
    "flame": ["...#....", "..##....", "..###...", ".####.#.", ".#####..", "##.#.##.", "#..#..#.", ".#####.."],
    "jelly": ["..###...", ".#...#..", ".#.#.#..", ".#...#..", "#######.", "#.#.#.#.", "#######.", "........"],
    "bubble": ["..###...", ".#...#..", "#..#..#.", "#.#...#.", "#.....#.", ".#...#..", "..###...", "........"],
    "worm": ["........", "........", ".##..##.", "#..##..#", "........", "........", "........", "........"],
    "bubbles": [".##.....", "#..#....", ".##..##.", ".....#.#", "..#..##.", ".#.#....", "..#.....", "........"],
}
FOOD = {"pip": "apple", "cat": "fish", "dog": "bone", "owl": "mouse", "dragon": "coal", "blob": "jelly", "axolotl": "worm"}
PLAY = {"pip": "ball", "cat": "yarn", "dog": "ball", "owl": "feather", "dragon": "flame", "blob": "bubble", "axolotl": "bubbles"}

STAT_ICONS = {  # 5x5: food, joy, energy, clean
    "food": [".#.#.", "#####", "#####", "#####", ".###."],
    "joy": ["##.##", "#####", "#####", ".###.", "..#.."],
    "energy": ["..##.", ".##..", "#####", "..##.", ".##.."],
    "clean": ["..#..", ".###.", "#####", "#####", ".###."],
}


# ---------------------------------------------------------------- output
def bits(row):
    return int("".join("1" if v else "0" for v in row), 2)


def build():
    frames = []  # (w, h, rows)
    index = {}
    for sp in SPECIES:
        for st in STAGES:
            for f in FRAMES:
                index[(sp, st, f)] = len(frames)
                frames.append(frame(sp, st, f))
        index[(sp, "egg", "egg")] = len(frames)
        frames.append(egg(sp))
    return frames, index


def main():
    frames, index = build()
    rows, offs = [], []
    for g in frames:
        offs.append(len(rows))
        rows.extend(bits(r) for r in g)
    for name, rws in list(ICONS.items()) + list(STAT_ICONS.items()):
        n = len(rws[0])
        assert len(rws) == n and all(len(r) == n for r in rws), name
    L = [
        "// Generated by config_page/gen_pet_art.py - edit the art there.",
        "// The OLED pets: original pixel art, seven species x egg/baby/child/adult.",
        "",
        "#pragma once",
        "",
        "#include <stdint.h>",
        '#include "pet_ids.h"',
        "",
        "typedef struct {",
        "    uint8_t  w, h;",
        "    uint16_t row; // first row in pet_rows (bit w-1 = leftmost pixel)",
        "} pet_frame_t;",
        "",
        f"static const uint32_t pet_rows[{len(rows)}] = {{",
    ]
    for i in range(0, len(rows), 8):
        L.append("    " + ", ".join(f"0x{v:X}" for v in rows[i:i + 8]) + ",")
    L += ["};", "", "static const pet_frame_t pet_frames[] = {"]
    for g, o in zip(frames, offs):
        L.append(f"    {{{len(g[0])}, {len(g)}, {o}}},")
    L += ["};", "", "// pet_frame_index[species][stage - 1][frame]; eggs: pet_egg_index[species]",
          f"static const uint8_t pet_frame_index[{len(SPECIES)}][{len(STAGES)}][{len(FRAMES)}] = {{"]
    for sp in SPECIES:
        L.append("    {" + ", ".join("{" + ", ".join(str(index[(sp, st, f)]) for f in FRAMES) + "}" for st in STAGES) + "},")
    L += ["};", "static const uint8_t pet_egg_index[] = {" + ", ".join(str(index[(sp, "egg", "egg")]) for sp in SPECIES) + "};", ""]
    L += ["static const uint8_t pet_food_icon[] = {" + ", ".join(f"PET_ICON_{FOOD[s].upper()}" for s in SPECIES) + "};",
          "static const uint8_t pet_play_icon[] = {" + ", ".join(f"PET_ICON_{PLAY[s].upper()}" for s in SPECIES) + "};", ""]
    L += ["// 8x8 icons: bit 7 = leftmost pixel.", "static const uint8_t pet_icons[PET_ICON_COUNT][8] = {"]
    for name, rws in ICONS.items():
        L.append("    {" + ", ".join(f"0x{bits([c == '#' for c in r]):02X}" for r in rws) + f"}}, // {name}")
    L += ["};", "", "// 5x5 stat icons (food, joy, energy, clean): bit 4 = leftmost pixel.", "static const uint8_t pet_stat_icons[4][5] = {"]
    for name, rws in STAT_ICONS.items():
        L.append("    {" + ", ".join(f"0x{bits([c == '#' for c in r]):02X}" for r in rws) + f"}}, // {name}")
    L += ["};", ""]
    (HERE.parent / "pet_art.h").write_text("\n".join(L), encoding="utf-8", newline="\n")
    ids = [
        "// Generated by config_page/gen_pet_art.py - species, stages, frames, icons.",
        "",
        "#pragma once",
        "",
        f"#define PET_SPECIES_COUNT {len(SPECIES)}",
        "enum { " + ", ".join(f"PET_SP_{s.upper()}" for s in SPECIES) + " };",
        "enum { PET_STAGE_EGG, " + ", ".join(f"PET_STAGE_{s.upper()}" for s in STAGES) + ", PET_STAGE_COUNT };",
        "enum { " + ", ".join(f"PET_FR_{f.upper()}" for f in FRAMES) + ", PET_FR_COUNT };",
        "enum { " + ", ".join(f"PET_ICON_{s.upper()}" for s in ICONS) + ", PET_ICON_COUNT };",
        "",
    ]
    (HERE.parent / "pet_ids.h").write_text("\n".join(ids), encoding="utf-8", newline="\n")

    # The page: each frame as width, height and hex rows.
    js = {
        "species": SPECIES, "speciesNames": SPECIES_NAMES, "stages": ["egg"] + STAGES, "frames": FRAMES,
        "frame": {f"{sp}/{st}/{f}": [len(frames[index[(sp, st, f)]][0])] + [f"{bits(r):x}" for r in frames[index[(sp, st, f)]]]
                  for sp in SPECIES for st in STAGES for f in FRAMES},
        "egg": {sp: [16] + [f"{bits(r):x}" for r in egg(sp)] for sp in SPECIES},
        "icons": ICONS, "food": FOOD, "play": PLAY,
    }
    (HERE / "pet_art.json").write_text(json.dumps(js, separators=(",", ":")), encoding="utf-8", newline="\n")
    print(f"pet art: {len(frames)} frames, {len(rows)} rows ({len(rows) * 4 + len(frames) * 4} bytes), {len(ICONS)} icons")
    if "--sheet" in sys.argv:
        sheet(frames, index, sys.argv[sys.argv.index("--sheet") + 1])


def sheet(frames, index, path):
    """Review sheet: every species x stage x state, 4x."""
    from PIL import Image, ImageDraw
    S, cw, ch, lab = 4, 34, 32, 16
    cols = [("egg", "egg")] + [(st, f) for st in STAGES for f in FRAMES] + [("icons", "food/play")]
    W = 70 + len(cols) * (cw * S + 6)
    H = 40 + len(SPECIES) * (ch * S + 10)
    im = Image.new("RGB", (W, H), (255, 255, 255))
    dr = ImageDraw.Draw(im)
    for i, (st, f) in enumerate(cols):
        dr.text((70 + i * (cw * S + 6), 8), f"{st}" if st in ("egg", "icons") else f"{st}\n{f}", fill=(0, 0, 0))
    for r, sp in enumerate(SPECIES):
        y0 = 40 + r * (ch * S + 10)
        dr.text((6, y0 + ch * S // 2), SPECIES_NAMES[r], fill=(0, 0, 0))
        for i, (st, f) in enumerate(cols):
            x0 = 70 + i * (cw * S + 6)
            dr.rectangle([x0, y0, x0 + cw * S - 1, y0 + ch * S - 1], fill=(10, 10, 30))
            if st == "icons":
                for k, name in enumerate((FOOD[sp], PLAY[sp])):
                    for yy, row in enumerate(ICONS[name]):
                        for xx, c in enumerate(row):
                            if c == "#":
                                X, Y = x0 + (4 + k * 14 + xx) * S, y0 + (12 + yy) * S
                                dr.rectangle([X, Y, X + S - 2, Y + S - 2], fill=(255, 200, 120))
                continue
            g = frames[index[(sp, st, f)]]
            gx, gy = (cw - len(g[0])) // 2, ch - len(g) - 1
            for yy, row in enumerate(g):
                for xx, v in enumerate(row):
                    if v:
                        X, Y = x0 + (gx + xx) * S, y0 + (gy + yy) * S
                        dr.rectangle([X, Y, X + S - 2, Y + S - 2], fill=(140, 220, 255))
    im.save(path)
    print("sheet:", path, im.size)


if __name__ == "__main__":
    main()
