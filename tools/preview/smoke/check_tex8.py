"""CI's check of the smoke snapshot: each texel of the smoke app's 4 x 4
A8R8G8B8 texture (drawn 64 px a texel, nearest) must fill most of its own
64 x 64 block on a grid anchored at texel (0, 0), so the preview's 8-bit
texture path reads the colours the app wrote, in their places (a transposed,
mirrored or flipped texture fails).
Plain Python: CI's runner has no Pillow.
    python3 -I tools/preview/smoke/check_tex8.py <home.ppm>"""
import collections
import sys


def ppm(path):
    data = open(path, "rb").read()
    tokens, pos = [], 0
    while len(tokens) < 4:  # P6, width, height, max: whitespace apart
        while data[pos:pos + 1].isspace():
            pos += 1
        end = pos
        while end < len(data) and not data[end:end + 1].isspace():
            end += 1
        if end == pos:  # the data ended inside the header
            sys.exit(f"{path}: a short PPM header")
        tokens.append(data[pos:end])
        pos = end
    if tokens[0] != b"P6" or tokens[3] != b"255":
        sys.exit(f"{path}: not an 8-bit P6 file")
    w, h = int(tokens[1]), int(tokens[2])
    return w, h, data[pos + 1:pos + 1 + 3 * w * h]


def main():
    w, h, px = ppm(sys.argv[1])
    texel = {bytes((0x13 + 0x20 * i, 0x17 + 0x20 * j, 0xC5 - 0x10 * (i + j))): (i, j) for j in range(4) for i in range(4)}
    at = collections.defaultdict(list)  # (i, j): the (y, x) of every pixel of that colour
    for k in range(0, len(px), 3):
        ij = texel.get(px[k:k + 3])
        if ij:
            at[ij].append(divmod(k // 3, w))
    if not at[(0, 0)]:
        print("tex8: texel (0, 0) is not in the snapshot")
        sys.exit(1)
    y00 = min(y for y, x in at[(0, 0)])  # the grid: texel (i, j) fills the 64 x 64 block at (x00 + 64 i, y00 + 64 j)
    x00 = min(x for y, x in at[(0, 0)])
    bad = 0
    for (i, j) in sorted(texel.values(), key=lambda t: (t[1], t[0])):
        x0, y0 = x00 + 64 * i, y00 + 64 * j
        n = sum(x0 <= x < x0 + 64 and y0 <= y < y0 + 64 for y, x in at[(i, j)])
        if n < 3600:  # of 4,096: a block's edge pixels may fall on the next texel
            print(f"texel ({i}, {j}): {n} pixels in its block at ({x0}, {y0}), want at least 3600")
            bad += 1
    print(f"tex8: {16 - bad} of 16 texels in their blocks of the {w} x {h} snapshot")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
