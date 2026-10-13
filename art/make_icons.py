#!/usr/bin/env python3
"""Generate data/icons.bin and source/icons_ids.h: the icon atlases of
source/icons.c, rendered from Phosphor Icons 2.1.1.

    python3 art/make_icons.py <folder> [sheet.png]

<folder> holds Phosphor-Bold.ttf and Phosphor-Duotone.ttf of the npm package
@phosphor-icons/web@2.1.1 (src/bold/ and src/duotone/; ps3sdk keeps them in
fonts/). The script checks their sha256 first: other bytes give other
atlases. Phosphor is MIT (Copyright (c) 2020-2021 Phosphor Icons): the
licence text is LICENSES/PHOSPHOR.txt, and every pkg that links icons.bin
ships it.

Three sets, each one cell size and one atlas:
    S  32 px, bold: one glyph per icon, for chips, rows and the header;
    M  64 px, duotone: two layers per icon, the fill (the CSS draws it at
       20 % opacity) and the outline, for the three-day strip;
    L 168 px, duotone: two layers, for the hero icon that turns and scales.
The code points are the classes ph-<name> of the package's style.css (bold
has regular's code points; a duotone icon is :before, the fill, and :after,
the outline). Each glyph is drawn with its 'mm' anchor (the middle of the
advance and of the font's ascender and descender) at the centre of its cell,
so the two layers of an icon overlay at one centre, and a turn about the
centre keeps them together. A cell is twice the largest extent from the
anchor in the set plus one clear pixel on each side (even, so the centre is
a whole pixel and the atlas rows are whole bytes).

icons.bin, big-endian (the PPU's order), read by source/icons.c:
    'PHI1', u16 set count, u16 glyph count, u16 0, u16 0
    per glyph, in id order: u16 set, cell x, cell y, cell w, cell h,
              centre x, centre y (from the cell's top left), 0
    per set: u16 pixel size, u16 turns (1: always bilinear, the L set),
             u16 atlas w, atlas h; the atlas, 4 bits per pixel (alpha
             0..15), two pixels per byte, the left pixel in the high nibble;
             zero bytes to a multiple of 4.
The S and M sets bind as the fonts do (nearest at 1080p); the L set always
binds bilinear, because its layers turn and scale.
"""
import hashlib, os, struct, sys
from PIL import Image, ImageDraw, ImageFont

SUMS = {'Phosphor-Bold.ttf': '10a0a1cb4f8156a420f9f84cf34c4e9871e58ed2ddea1f6a8079ad07243a7fb2',
        'Phosphor-Duotone.ttf': '80e6f0d6fa8ce96a9933f48aaabddc592c5d2b52caf8f010a7e54a17c74084bf'}
GAMMA = 0.85                                  # make_font.py's: light glyphs on a dark background

# The code points, from @phosphor-icons/web@2.1.1's src/bold/style.css and
# src/duotone/style.css: name, then the bold glyph, or the fill and the outline.
BOLD = [('caret-up', 0xe13c), ('caret-down', 0xe136), ('minus', 0xe32a), ('drop', 0xe210), ('clock', 0xe19a),
        ('wifi-high', 0xe4ea), ('wifi-slash', 0xe4f2), ('currency-dollar', 0xe550), ('newspaper', 0xe344),
        ('info', 0xe2ce), ('warning', 0xe4e0), ('arrows-clockwise', 0xe094), ('wind', 0xe5d2),
        ('sun-horizon', 0xe5b6), ('thermometer-simple', 0xe5cc), ('umbrella', 0xe684)]
DUO = {'sun': (0xe472, 0xe473), 'moon': (0xe330, 0xe331), 'moon-stars': (0xe58e, 0xe58f), 'cloud': (0xe1aa, 0xe1ab),
       'cloud-sun': (0xe540, 0xe541), 'cloud-moon': (0xe53e, 0xe53f), 'cloud-fog': (0xe53c, 0xe53d),
       'cloud-rain': (0xe1b4, 0xe1b5), 'cloud-snow': (0xe1b8, 0xe1b9), 'cloud-lightning': (0xe1b2, 0xe1b3),
       'lightning': (0xe2de, 0xe2df), 'snowflake': (0xe5aa, 0xe5ab)}
WEATHER = ['sun', 'moon-stars', 'cloud-sun', 'cloud-moon', 'cloud', 'cloud-fog', 'cloud-rain', 'cloud-snow',
           'cloud-lightning']
HERO = ['sun', 'moon', 'cloud', 'lightning', 'snowflake']


def sets():
    """The three sets: letter, pixel size, font file, turns, columns, [(id name, code point)]."""
    s = [('IC_S_' + n, cp) for n, cp in BOLD]
    m = [(f'IC_M_{n}_{k}', DUO[n][i]) for n in WEATHER for i, k in enumerate(('FILL', 'LINE'))]
    l = [(f'IC_L_{n}_{k}', DUO[n][i]) for n in HERO for i, k in enumerate(('FILL', 'LINE'))]
    fix = lambda g: [(name.replace('-', '_').upper(), cp) for name, cp in g]
    # columns: the fewest empty cells (16 = 8 x 2, 18 = 6 x 3, 10 = 5 x 2)
    return [('S', 32, 'Phosphor-Bold.ttf', 0, 8, fix(s)), ('M', 64, 'Phosphor-Duotone.ttf', 0, 6, fix(m)),
            ('L', 168, 'Phosphor-Duotone.ttf', 1, 5, fix(l))]


def build(folder, letter, px, ttf, turns, cols, glyphs):
    f = ImageFont.truetype(os.path.join(folder, ttf), px)
    ex = ey = 0
    for name, cp in glyphs:
        x0, y0, x1, y1 = f.getbbox(chr(cp), anchor='mm')
        if x1 <= x0 or y1 <= y0:
            sys.exit(f'{name}: U+{cp:04X} has no ink in {ttf}')
        ex, ey = max(ex, -x0, x1), max(ey, -y0, y1)
    cw, ch = 2 * (ex + 1), 2 * (ey + 1)
    rows = (len(glyphs) + cols - 1) // cols
    atlas = Image.new('L', (cols * cw, rows * ch), 0)
    d = ImageDraw.Draw(atlas)
    cells = []
    for i, (name, cp) in enumerate(glyphs):
        x, y = (i % cols) * cw, (i // cols) * ch
        d.text((x + cw // 2, y + ch // 2), chr(cp), font=f, fill=255, anchor='mm')
        cells.append((x, y))
    for x, y in cells:                         # one clear pixel around every glyph
        c = atlas.crop((x, y, x + cw, y + ch))
        bb = c.getbbox()
        assert bb and bb[0] >= 1 and bb[1] >= 1 and bb[2] <= cw - 1 and bb[3] <= ch - 1, (letter, x, y, bb)
    return dict(letter=letter, px=px, turns=turns, cw=cw, ch=ch, cells=cells, glyphs=glyphs, atlas=atlas)


def pack(built):
    n = sum(len(b['glyphs']) for b in built)
    out = bytearray(b'PHI1' + struct.pack('>HHHH', len(built), n, 0, 0))
    for k, b in enumerate(built):
        for x, y in b['cells']:
            out += struct.pack('>8H', k, x, y, b['cw'], b['ch'], b['cw'] // 2, b['ch'] // 2, 0)
    for b in built:
        a = b['atlas']
        out += struct.pack('>HHHH', b['px'], b['turns'], a.width, a.height)
        q = [min(15, round(((v / 255) ** GAMMA) * 15)) for v in a.tobytes()]
        out += bytes((q[i] << 4) | q[i + 1] for i in range(0, len(q), 2))
        out += bytes(-len(out) % 4)
    return bytes(out)


def ids_h(built):
    lines = ['/* The icon ids of data/icons.bin, in its order: written by art/make_icons.py',
             ' * from Phosphor Icons 2.1.1 (MIT, LICENSES/PHOSPHOR.txt). Do not edit:',
             ' * run the script. IC_S_*: 32 px, bold. IC_M_*: 64 px, IC_L_*: 168 px,',
             ' * duotone: _FILL is the fill layer (tint it, for example at alpha 0x55),',
             ' * _LINE the outline; draw the fill first. */',
             '#ifndef ICONS_IDS_H', '#define ICONS_IDS_H', '', 'enum {']
    for b in built:
        for name, cp in b['glyphs']:
            lines.append(f'    {name},'.ljust(36) + f'/* U+{cp:04X} */')
    lines += ['    IC_COUNT', '};', '', '#endif', '']
    return '\n'.join(lines)


def sheet(built, path):
    """Every glyph on the app's background, with its cell outlined: a check by eye."""
    W = 1920
    H = sum(b['atlas'].height + 30 for b in built) + 30
    img = Image.new('RGB', (W, H), (12, 18, 34))
    d = ImageDraw.Draw(img)
    y = 30
    for b in built:
        img.paste((242, 245, 250), (40, y), b['atlas'])
        for x0, y0 in b['cells']:
            d.rectangle((40 + x0, y + y0, 40 + x0 + b['cw'] - 1, y + y0 + b['ch'] - 1), outline=(43, 61, 96))
        y += b['atlas'].height + 30
    img.save(path)


if __name__ == '__main__':
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    folder = sys.argv[1]
    for name, want in SUMS.items():
        got = hashlib.sha256(open(os.path.join(folder, name), 'rb').read()).hexdigest()
        if got != want:
            sys.exit(f'{name}: sha256 {got}, want {want} (@phosphor-icons/web@2.1.1)')
    built = [build(folder, *s) for s in sets()]
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, '..')
    data = pack(built)
    open(os.path.join(root, 'data', 'icons.bin'), 'wb').write(data)
    open(os.path.join(root, 'source', 'icons_ids.h'), 'w').write(ids_h(built))
    rsx = 0
    for b in built:
        a = b['atlas']
        pitch = (a.width * 2 + 63) // 64 * 64
        rsx += pitch * a.height
        print(f"{b['letter']} {b['px']:3} px: {len(b['glyphs'])} glyphs, cell {b['cw']}x{b['ch']}, atlas {a.width}x{a.height} "
              f"({pitch * a.height} B of RSX memory as A4R4G4B4)")
    print(f'data/icons.bin {len(data)} bytes, {sum(len(b["glyphs"]) for b in built)} glyphs, {rsx} B of RSX memory')
    if len(sys.argv) == 3:
        sheet(built, sys.argv[2])
