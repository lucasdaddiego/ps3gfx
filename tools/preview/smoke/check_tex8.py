"""CI's check of the smoke snapshot: each texel of the smoke app's 4 x 4
A8R8G8B8 texture (drawn 64 px a texel, nearest) must fill most of a 64 x 64
block, so the preview's 8-bit texture path reads the colours the app wrote.
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
        while not data[end:end + 1].isspace():
            end += 1
        tokens.append(data[pos:end])
        pos = end
    if tokens[0] != b"P6" or tokens[3] != b"255":
        sys.exit(f"{path}: not an 8-bit P6 file")
    w, h = int(tokens[1]), int(tokens[2])
    return w, h, data[pos + 1:pos + 1 + 3 * w * h]


def main():
    w, h, px = ppm(sys.argv[1])
    counts = collections.Counter(px[k:k + 3] for k in range(0, len(px), 3))
    bad = 0
    for j in range(4):
        for i in range(4):
            rgb = bytes((0x13 + 0x20 * i, 0x17 + 0x20 * j, 0xC5 - 0x10 * (i + j)))
            n = counts.get(rgb, 0)
            if n < 3600:  # of 4,096: the block's edge pixels may fall on the next texel
                print(f"texel ({i}, {j}) #{rgb.hex()}: {n} pixels, want at least 3600")
                bad += 1
    print(f"tex8: {16 - bad} of 16 texels found in the {w} x {h} snapshot")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
