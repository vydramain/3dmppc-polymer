#!/usr/bin/env python3
# The editor's bitmap icons (spec section 16): every picture is shapes on a 32-unit grid,
# rasterized without smoothing at each size, then given the shared dark outline and a light
# top-left edge. Usage: draw_icons.py <out dir>; writes <name>-<size>.png for 16/24/32/48.
import sys, os
from PIL import Image, ImageDraw

OUTLINE = (0x20, 0x25, 0x1c, 255)
C = {
    'brass': (0xc8, 0xb4, 0x4a), 'brass_dk': (0x95, 0x88, 0x31), 'paper': (0xe6, 0xe6, 0xdc),
    'paper_dk': (0xb4, 0xb4, 0xa8), 'peach': (0xfa, 0xb3, 0x87), 'mauve': (0xcb, 0xa6, 0xf7),
    'blue': (0x89, 0xb4, 0xfa), 'teal': (0x94, 0xe2, 0xd5), 'green': (0xa6, 0xe3, 0xa1),
    'red': (0xf3, 0x8b, 0xa8), 'yellow': (0xf9, 0xe2, 0xaf), 'steel': (0xa8, 0xae, 0xb4),
    'steel_dk': (0x6c, 0x72, 0x78), 'silver': (0xd4, 0xd8, 0xdc), 'wood': (0xb0, 0x7a, 0x48),
    'screen': (0x1e, 0x1e, 0x2e), 'olive': (0x7e, 0x87, 0x76), 'white': (0xf1, 0xf2, 0xf0),
    'grey': (0x8a, 0x8f, 0x88), 'dark': (0x32, 0x39, 0x2c),
}


class Pic:
    """Shapes on a 32-unit grid, drawn at `n` pixels."""

    def __init__(self, n):
        self.n = n
        self.img = Image.new('RGBA', (n, n), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)

    def p(self, v):
        return int(round(v * self.n / 32.0))

    def pts(self, pts):
        return [(self.p(x), self.p(y)) for x, y in pts]

    def rect(self, x0, y0, x1, y1, col):
        a, b, c, d = self.p(x0), self.p(y0), self.p(x1) - 1, self.p(y1) - 1
        if c >= a and d >= b:
            self.d.rectangle((a, b, c, d), fill=C[col] + (255,))

    def poly(self, pts, col):
        self.d.polygon(self.pts(pts), fill=C[col] + (255,))

    def line(self, pts, col, w=2):
        self.d.line(self.pts(pts), fill=C[col] + (255,), width=max(1, self.p(w)))

    def box(self, x0, y0, x1, y1):
        """A bounding box of at least one pixel."""
        a, b = self.p(x0), self.p(y0)
        return (a, b, max(a, self.p(x1) - 1), max(b, self.p(y1) - 1))

    def ellipse(self, x0, y0, x1, y1, col):
        self.d.ellipse(self.box(x0, y0, x1, y1), fill=C[col] + (255,))

    def ring(self, x0, y0, x1, y1, col, w=3):
        self.d.ellipse(self.box(x0, y0, x1, y1), outline=C[col] + (255,), width=max(1, self.p(w)))

    def arc(self, x0, y0, x1, y1, a0, a1, col, w=3):
        self.d.arc(self.box(x0, y0, x1, y1), a0, a1, fill=C[col] + (255,), width=max(1, self.p(w)))

    def finish(self):
        """The shared outline around everything drawn, then a light top-left edge."""
        src = self.img.load()
        n = self.n
        out = self.img.copy()
        o = out.load()
        for y in range(n):
            for x in range(n):
                if src[x, y][3] != 0:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < n and 0 <= yy < n and src[xx, yy][3] != 0:
                        o[x, y] = OUTLINE
                        break
        for y in range(n):
            for x in range(n):
                r, g, b, a = src[x, y]
                if a == 0:
                    continue
                up = y == 0 or src[x, y - 1][3] == 0
                left = x == 0 or src[x - 1, y][3] == 0
                if up or left:
                    o[x, y] = (min(255, r + 40), min(255, g + 40), min(255, b + 40), 255)
        return out


# --- shared parts -------------------------------------------------------------------------------

def sheet(p, accent=None):
    p.poly([(7, 3), (20, 3), (26, 9), (26, 29), (7, 29)], 'paper')
    p.poly([(20, 3), (20, 9), (26, 9)], 'paper_dk')
    if accent:
        p.rect(7, 3, 11, 29, accent)


def folder(p, open_=False):
    p.poly([(3, 8), (12, 8), (14, 11), (29, 11), (29, 27), (3, 27)], 'brass_dk')
    if open_:
        p.poly([(7, 15), (31, 15), (27, 27), (3, 27)], 'brass')
    else:
        p.rect(3, 13, 29, 27, 'brass')


def plus(p, x, y, col='green'):
    p.rect(x - 1.5, y - 5, x + 1.5, y + 5, col)
    p.rect(x - 5, y - 1.5, x + 5, y + 1.5, col)


def disc(p, x0=4, y0=4, x1=28, y1=28, label='blue'):
    p.ellipse(x0, y0, x1, y1, 'silver')
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    r = (x1 - x0) / 2
    p.ellipse(cx - r * 0.72, cy - r * 0.72, cx + r * 0.1, cy + r * 0.1, label)
    p.ellipse(cx - r * 0.18, cy - r * 0.18, cx + r * 0.18, cy + r * 0.18, 'paper_dk')
    p.ellipse(cx - r * 0.07, cy - r * 0.07, cx + r * 0.07, cy + r * 0.07, 'dark')


def monitor(p, screen='screen'):
    p.rect(3, 4, 29, 23, 'steel')
    p.rect(6, 7, 26, 20, screen)
    p.rect(12, 23, 20, 27, 'steel_dk')
    p.rect(8, 27, 24, 29, 'steel')


def magnifier(p, x=13, y=13, r=8):
    p.ring(x - r, y - r, x + r, y + r, 'steel', 3)
    p.ellipse(x - r + 3, y - r + 3, x + r - 3, y + r - 3, 'blue')
    p.line([(x + r * 0.7, y + r * 0.7), (29, 29)], 'wood', 4)


def lines(p, x0, y0, x1, n, col='grey', gap=4):
    for i in range(n):
        p.rect(x0, y0 + i * gap, x1 - (i % 2) * 4, y0 + i * gap + 2, col)


def arrow_circle(p, col='teal', both=False):
    p.arc(5, 5, 27, 27, 200, 500 if not both else 340, col, 4)
    p.poly([(22, 2), (29, 9), (21, 12)], col)
    if both:
        p.arc(5, 5, 27, 27, 20, 160, col, 4)
        p.poly([(10, 30), (3, 23), (11, 20)], col)


# --- the inventory ------------------------------------------------------------------------------

def i_folder(p): folder(p)
def i_folder_open(p): folder(p, True)
def i_file(p): sheet(p)
def i_file_toml(p): sheet(p, 'peach'); lines(p, 13, 12, 23, 4, 'peach')
def i_file_lua(p): sheet(p, 'mauve'); p.ellipse(12, 12, 24, 24, 'mauve'); p.ellipse(18, 11, 25, 18, 'paper')
def i_file_cpp(p):
    sheet(p, 'blue'); p.line([(17, 12), (13, 17), (17, 22)], 'blue', 2); p.line([(20, 12), (24, 17), (20, 22)], 'blue', 2)
def i_file_header(p): sheet(p, 'teal'); p.rect(14, 11, 16, 24, 'teal'); p.rect(14, 16, 22, 18, 'teal'); p.rect(20, 16, 22, 24, 'teal')
def i_file_image(p):
    sheet(p); p.rect(10, 12, 24, 25, 'screen'); p.poly([(10, 25), (15, 17), (19, 22), (21, 19), (24, 25)], 'green')
    p.ellipse(19, 13, 22, 16, 'yellow')
def i_file_sound(p):
    sheet(p); p.rect(10, 15, 14, 21, 'teal'); p.poly([(14, 15), (19, 11), (19, 25), (14, 21)], 'teal')
    p.arc(17, 12, 25, 24, -60, 60, 'teal', 2)
def i_file_doc(p): sheet(p); lines(p, 10, 10, 23, 5)
def i_file_unknown(p): sheet(p); p.rect(14, 12, 19, 14, 'grey'); p.rect(18, 14, 20, 18, 'grey'); p.rect(15, 18, 18, 21, 'grey'); p.rect(15, 23, 18, 26, 'grey')
def i_file_disc(p): sheet(p, 'peach'); disc(p, 11, 12, 25, 26)
def i_symlink(p):
    sheet(p); p.line([(11, 24), (21, 14)], 'blue', 3); p.poly([(16, 12), (23, 12), (23, 19)], 'blue')

def i_new_file(p): sheet(p); plus(p, 22, 23)
def i_new_folder(p): folder(p); plus(p, 23, 22)
def i_rename(p):
    sheet(p); p.poly([(10, 26), (12, 20), (25, 7), (29, 11), (16, 24)], 'yellow'); p.poly([(10, 26), (12, 20), (16, 24)], 'wood')
def i_delete(p):
    p.rect(8, 9, 24, 29, 'steel'); p.rect(6, 6, 26, 9, 'steel_dk'); p.rect(13, 3, 19, 6, 'steel_dk')
    for x in (11, 15, 19):
        p.rect(x, 12, x + 2, 26, 'steel_dk')
    p.line([(4, 4), (28, 28)], 'red', 3)
def i_refresh(p): arrow_circle(p, 'teal')
def i_open(p): folder(p, True)
def i_save(p):
    p.rect(4, 4, 28, 28, 'blue'); p.rect(9, 4, 23, 12, 'silver'); p.rect(18, 5, 21, 11, 'dark'); p.rect(8, 16, 24, 28, 'paper')
    lines(p, 10, 19, 22, 2)
def i_undo(p): p.arc(8, 8, 28, 28, 180, 360, 'blue', 4); p.poly([(2, 18), (13, 18), (8, 10)], 'blue')
def i_redo(p): p.arc(4, 8, 24, 28, 180, 360, 'blue', 4); p.poly([(30, 18), (19, 18), (24, 10)], 'blue')
def i_find(p): magnifier(p)

def i_build(p):
    p.poly([(4, 8), (18, 3), (22, 7), (12, 14)], 'steel'); p.poly([(10, 12), (15, 9), (29, 26), (25, 29)], 'wood')
def i_cancel_build(p): i_build(p); p.ring(3, 3, 29, 29, 'red', 3); p.line([(8, 24), (24, 8)], 'red', 3)
def i_run(p): p.poly([(8, 4), (27, 16), (8, 28)], 'green')
def i_pause(p): p.rect(7, 5, 14, 27, 'yellow'); p.rect(18, 5, 25, 27, 'yellow')
def i_step(p): p.poly([(5, 5), (20, 16), (5, 27)], 'blue'); p.rect(22, 5, 27, 27, 'blue')
def i_stop(p): p.rect(6, 6, 26, 26, 'red')
def i_force_stop(p): i_stop(p); p.poly([(20, 14), (30, 30), (10, 30)], 'yellow'); p.rect(19, 20, 21, 25, 'dark'); p.rect(19, 27, 21, 29, 'dark')
def i_reload(p): arrow_circle(p, 'blue', both=True)
def i_capture(p):
    p.rect(3, 9, 29, 27, 'steel_dk'); p.rect(9, 5, 17, 9, 'steel_dk'); p.ellipse(10, 11, 23, 24, 'steel'); p.ellipse(13, 14, 20, 21, 'screen')
    p.rect(24, 11, 27, 13, 'yellow')
def i_record(p):
    p.rect(6, 4, 24, 29, 'paper'); p.rect(6, 4, 24, 8, 'red'); lines(p, 9, 12, 21, 4)
    p.poly([(16, 28), (18, 22), (28, 12), (31, 15), (21, 25)], 'yellow')
def i_verify(p):
    p.rect(6, 5, 26, 30, 'wood'); p.rect(8, 8, 24, 28, 'paper'); p.rect(11, 3, 21, 8, 'steel')
    p.line([(10, 18), (14, 23), (23, 11)], 'green', 3)
def i_report(p):
    p.rect(10, 3, 28, 25, 'paper_dk'); p.rect(7, 5, 25, 27, 'paper_dk'); p.rect(4, 7, 22, 29, 'paper'); lines(p, 7, 11, 19, 4, 'blue')
def i_inspect(p): disc(p, 3, 3, 23, 23); magnifier(p, 20, 20, 7)
def i_session(p): monitor(p); p.poly([(12, 9), (20, 13.5), (12, 18)], 'green')
def i_source(p): sheet(p); lines(p, 10, 10, 23, 5, 'blue')
def i_logs(p):
    p.rect(4, 4, 28, 28, 'screen'); lines(p, 7, 8, 25, 5, 'teal'); p.rect(7, 8, 10, 10, 'yellow')
def i_candidate(p): disc(p, 3, 3, 29, 29, 'peach')

def i_select(p): p.poly([(8, 3), (8, 26), (13, 21), (17, 29), (21, 27), (17, 19), (24, 19)], 'white')
def i_move(p):
    p.rect(14.5, 6, 17.5, 26, 'green'); p.rect(6, 14.5, 26, 17.5, 'red')
    p.poly([(16, 1), (21, 7), (11, 7)], 'green'); p.poly([(16, 31), (21, 25), (11, 25)], 'green')
    p.poly([(1, 16), (7, 11), (7, 21)], 'red'); p.poly([(31, 16), (25, 11), (25, 21)], 'red')
def i_rotate(p): p.arc(4, 4, 28, 28, 140, 420, 'blue', 4); p.poly([(21, 2), (29, 8), (20, 12)], 'blue')
def i_scale(p): p.rect(4, 14, 18, 28, 'steel'); p.line([(12, 20), (26, 6)], 'yellow', 3); p.poly([(18, 4), (28, 4), (28, 14)], 'yellow')
def i_snap(p):
    for i in range(4):
        p.rect(4 + i * 7, 4, 5.5 + i * 7, 28, 'olive'); p.rect(4, 4 + i * 7, 28, 5.5 + i * 7, 'olive')
    p.ellipse(16, 16, 25, 25, 'yellow')
def i_grid(p):
    p.rect(4, 4, 28, 28, 'screen')
    for i in range(1, 4):
        p.rect(4 + i * 6, 4, 5 + i * 6, 28, 'olive'); p.rect(4, 4 + i * 6, 28, 5 + i * 6, 'olive')
def i_view(p):
    p.rect(3, 10, 22, 26, 'steel_dk'); p.poly([(22, 14), (29, 9), (29, 27), (22, 22)], 'steel'); p.ellipse(7, 13, 17, 23, 'blue')

def i_project(p): folder(p); disc(p, 14, 13, 28, 27)
def i_files(p): folder(p)
def i_assets(p):
    p.poly([(4, 12), (14, 7), (24, 12), (14, 17)], 'wood'); p.poly([(4, 12), (14, 17), (14, 29), (4, 24)], 'brass_dk')
    p.poly([(14, 17), (24, 12), (24, 24), (14, 29)], 'brass'); p.ellipse(20, 3, 30, 13, 'teal')
def i_scene(p):
    p.poly([(16, 3), (29, 10), (16, 17), (3, 10)], 'silver'); p.poly([(3, 10), (16, 17), (16, 30), (3, 23)], 'steel_dk')
    p.poly([(16, 17), (29, 10), (29, 23), (16, 30)], 'steel')
def i_hierarchy(p):
    p.rect(4, 4, 12, 10, 'brass'); p.rect(7, 10, 9, 27, 'grey'); p.rect(9, 15, 16, 17, 'grey'); p.rect(9, 24, 16, 26, 'grey')
    p.rect(16, 12, 28, 19, 'steel'); p.rect(16, 21, 28, 28, 'steel')
def i_inspector(p):
    p.rect(4, 4, 28, 28, 'paper')
    for i, x in enumerate((18, 11, 22)):
        p.rect(7, 8 + i * 7, 25, 10 + i * 7, 'grey'); p.rect(x, 6 + i * 7, x + 3, 12 + i * 7, 'blue')
def i_game(p):
    monitor(p, 'screen'); p.poly([(8, 18), (14, 11), (19, 15), (22, 12), (25, 18)], 'green'); p.ellipse(19, 8, 23, 12, 'yellow')
def i_code(p): sheet(p, 'blue'); p.line([(16, 12), (13, 17), (16, 22)], 'mauve', 2); p.line([(19, 12), (22, 17), (19, 22)], 'mauve', 2)
def i_controls(p): p.rect(2, 7, 30, 25, 'steel'); p.poly([(7, 11), (15, 16), (7, 21)], 'green'); p.rect(18, 11, 20, 21, 'yellow'); p.rect(22, 11, 24, 21, 'yellow')
def i_config(p):
    p.ellipse(5, 5, 27, 27, 'steel')
    for (x0, y0, x1, y1) in ((14, 1, 18, 7), (14, 25, 18, 31), (1, 14, 7, 18), (25, 14, 31, 18)):
        p.rect(x0, y0, x1, y1, 'steel')
    p.ellipse(11, 11, 21, 21, 'dark')
def i_output(p): p.rect(3, 4, 29, 28, 'screen'); lines(p, 6, 8, 26, 5, 'paper_dk')
def i_terminal(p):
    monitor(p); p.line([(9, 10), (13, 13), (9, 16)], 'green', 2); p.rect(15, 16, 21, 18, 'green')
def i_problems(p): p.poly([(16, 3), (30, 28), (2, 28)], 'yellow'); p.rect(14.5, 11, 17.5, 21, 'dark'); p.rect(14.5, 23, 17.5, 26, 'dark')
def i_search(p): magnifier(p)
def i_toolchest(p):
    p.rect(3, 11, 29, 28, 'red'); p.rect(3, 11, 29, 15, 'wood'); p.rect(11, 5, 21, 8, 'steel'); p.rect(11, 5, 13, 11, 'steel')
    p.rect(19, 5, 21, 11, 'steel'); p.rect(14, 16, 18, 19, 'steel')
def i_catalog(p):
    for i, col in enumerate(('red', 'green', 'blue', 'brass')):
        x, y = 4 + (i % 2) * 13, 4 + (i // 2) * 13
        p.rect(x, y, x + 11, y + 11, col)

ICONS = {n[2:]: f for n, f in globals().items() if n.startswith('i_') and callable(f)}
SIZES = (16, 24, 32, 48)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, draw in sorted(ICONS.items()):
        for n in SIZES:
            p = Pic(n)
            draw(p)
            p.finish().save(os.path.join(out, '%s-%d.png' % (name.replace('_', '-'), n)))
    print('%d icons x %d sizes' % (len(ICONS), len(SIZES)))


if __name__ == '__main__':
    main()
