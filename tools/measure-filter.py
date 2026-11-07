#!/usr/bin/env python3
"""The same measurement with payloads shaped like the ones a game ships.

The first version of this measurement used an index buffer that was almost
linear, which made the delta filter look far better than it is. This version
builds a triangulated grid, so the indices have real locality and real jumps,
and it measures the vertex buffer in two orders: grid order, which is coherent,
and a permuted order, which is what an exporter that does not care produces.
The difference between those two is the whole question for the delta filter.

Run: python3 measure-shuffle2.py
"""

import math
import random
import struct
import zlib


def deflate(data, level=9):
    c = zlib.compressobj(level, zlib.DEFLATED, -15)
    return c.compress(data) + c.flush()


def shuffle(data, stride):
    return b"".join(data[i::stride] for i in range(stride))


def delta_words(data, stride):
    words = stride // 4
    if words == 0:
        return data
    out = bytearray()
    prev = [0] * words
    for off in range(0, len(data), stride):
        rec = struct.unpack_from(f"<{words}I", data, off)
        out += struct.pack(f"<{words}I",
                           *((rec[i] - prev[i]) & 0xFFFFFFFF
                             for i in range(words)))
        prev = rec
    return bytes(out)


def row(label, data, stride, note=""):
    n = len(data)
    raw = len(deflate(data))
    sh = len(deflate(shuffle(data, stride)))
    dl = len(deflate(shuffle(delta_words(data, stride), stride)))
    print(f"{label}")
    if note:
        print(f"  {note}")
    print(f"  {'payload':<24}{n:>10,}")
    print(f"  {'deflate':<24}{raw:>10,}  {raw/n:>6.1%}")
    print(f"  {'shuffle+deflate':<24}{sh:>10,}  {sh/n:>6.1%}"
          f"   {raw/sh:>5.2f}x vs deflate")
    print(f"  {'delta+shuffle+deflate':<24}{dl:>10,}  {dl/n:>6.1%}"
          f"   {raw/dl:>5.2f}x vs deflate")
    print()


W, H = 500, 400
N = W * H

# A realistic index buffer: a triangulated grid. Locally coherent, with jumps
# at every row boundary, which is what a real mesh looks like.
tris = []
for y in range(H - 1):
    for x in range(W - 1):
        a = y * W + x
        tris += [a, a + 1, a + W, a + 1, a + W + 1, a + W]
idx = b"".join(struct.pack("<I", i) for i in tris)
row("Index buffer, triangulated grid, 500x400", idx, 4,
    "locally coherent, jumps at row boundaries")

# The same mesh's vertices, in grid order.
verts = []
for i in range(N):
    x = (i % W) / W * 10.0
    y = (i // W) / H * 10.0
    z = math.sin(x) * math.cos(y)
    nx, ny, nz = -math.cos(x) * math.cos(y), math.sin(x) * math.sin(y), 1.0
    ln = math.sqrt(nx * nx + ny * ny + nz * nz)
    verts.append(struct.pack("<8f", x, y, z, nx / ln, ny / ln, nz / ln,
                             x / 10.0, y / 10.0))
vb = b"".join(verts)
row("Vertex buffer, same grid, 8 floats per vertex, grid order", vb, 32,
    "coherent order: neighbours are numerically close")

# The same vertices, permuted. Same data, same size, different order.
random.seed(7)
order = list(range(N))
random.shuffle(order)
vbp = b"".join(verts[i] for i in order)
row("Vertex buffer, same data, permuted order", vbp, 32,
    "incoherent order: the delta filter has nothing to subtract")

print("Reading: shuffle survives a change of order because it only groups "
      "bytes by position. Delta depends on the order, so it is a build-time "
      "choice that a writer cannot make correctly without knowing how the "
      "buffer was produced.")
