#!/usr/bin/env python3
#
# Genere resources/tuneforge.ico : le losange de la barre de titre, pose sur
# une tuile sombre arrondie.
#
# Python pur (zlib + struct) : reconstruire l'icone ne demande rien
# d'installer. Chaque taille est dessinee a sa resolution, avec un
# sur-echantillonnage 4x4 par pixel — reduire une image de 256 px a 16 px
# donnerait un losange baveux, illisible dans la barre des taches.
#
#   python tools/make_icon.py                 -> resources/tuneforge.ico
#   python tools/make_icon.py --preview x.png -> et un apercu 256 px
#
import os
import struct
import sys
import zlib

SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
SS = 4  # sous-echantillons par axe

# Couleurs du theme par defaut (Ardoise & cyan), reprises de theme.hpp.
TILE_TOP = (0x15, 0x1C, 0x26)
TILE_BOTTOM = (0x09, 0x0D, 0x12)
ACCENT_TOP = (0x7D, 0xD3, 0xFC)     # accent_hover
ACCENT_BOTTOM = (0x0E, 0xA5, 0xE9)  # accent_active


def lerp(a, b, t):
    t = min(max(t, 0.0), 1.0)
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def render(n):
    # Les petites tailles occupent toute la case : a 16 px, chaque pixel de
    # marge est un pixel de losange en moins.
    pad = 0.0 if n < 32 else n * 0.06
    x0, y0, x1, y1 = pad, pad, n - pad, n - pad
    side = x1 - x0
    radius = side * 0.22
    cx, cy = n / 2.0, n / 2.0
    outer = side * 0.34
    hole = outer * 0.45   # meme proportion que dans la barre de titre

    rows = bytearray()
    for py in range(n):
        rows.append(0)  # filtre PNG « aucun »
        for px in range(n):
            r = g = b = 0.0
            covered = 0
            for sy in range(SS):
                y = py + (sy + 0.5) / SS
                for sx in range(SS):
                    x = px + (sx + 0.5) / SS
                    if x < x0 or x > x1 or y < y0 or y > y1:
                        continue
                    # Coins arrondis : distance au rectangle interieur.
                    dx = max(x0 + radius - x, 0.0, x - (x1 - radius))
                    dy = max(y0 + radius - y, 0.0, y - (y1 - radius))
                    if dx * dx + dy * dy > radius * radius:
                        continue
                    col = lerp(TILE_TOP, TILE_BOTTOM, (y - y0) / side)
                    d = abs(x - cx) + abs(y - cy)
                    if hole < d <= outer:
                        col = lerp(ACCENT_TOP, ACCENT_BOTTOM, (y - (cy - outer)) / (2 * outer))
                    r += col[0]
                    g += col[1]
                    b += col[2]
                    covered += 1
            if covered:
                rows += bytes((int(r / covered + 0.5), int(g / covered + 0.5),
                               int(b / covered + 0.5), int(255 * covered / (SS * SS) + 0.5)))
            else:
                rows += b"\x00\x00\x00\x00"
    return bytes(rows)


def png(n, raw):
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", n, n, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) +
            chunk(b"IEND", b""))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "resources", "tuneforge.ico")
    os.makedirs(os.path.dirname(out), exist_ok=True)

    images = [(n, png(n, render(n))) for n in SIZES]

    # Conteneur ICO a entrees PNG : lu nativement depuis Windows Vista, et
    # accepte tel quel par rc.exe.
    offset = 6 + 16 * len(images)
    entries, blobs = b"", b""
    for n, blob in images:
        dim = 0 if n >= 256 else n   # 0 signifie 256 dans l'en-tete ICO
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob),
                               offset + len(blobs))
        blobs += blob
    with open(out, "wb") as f:
        f.write(struct.pack("<HHH", 0, 1, len(images)) + entries + blobs)
    print("icone ecrite : %s (%d octets, %d tailles)"
          % (os.path.normpath(out), os.path.getsize(out), len(images)))

    if "--preview" in sys.argv:
        path = sys.argv[sys.argv.index("--preview") + 1]
        with open(path, "wb") as f:
            f.write(dict(images)[256])
        print("apercu : " + path)


if __name__ == "__main__":
    main()
