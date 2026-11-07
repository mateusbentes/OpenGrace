#!/usr/bin/env python3
"""Is a shared dictionary worth specifying? Verified against its own premise.

The first measurement reported ~10% on a 38 KB mesh and ~29% on a 2.8 KB mesh.
Before believing it, the payloads it used were compared byte for byte. They are
nearly identical to each other: the generator's geometry does not depend on its
seed, so every "different" mesh in the sample was the same mesh with different
noise, and the dictionary built from forty of them contained the measured
payload's own records. That is self-reference, not compression.

This script (1) demonstrates that byte identity, (2) measures the case a
dictionary could legitimately win — an LOD chain, where the coarser level is a
byte-for-byte subsequence of the finer one — and (3) states the structural bound
the deflate window puts on any dictionary.

Run: python3 measure-dict.py
"""

import math
import struct
import zlib

WINDOW = 15
MAX_DICT = 1 << WINDOW      # 32768
RECORD = 48                 # 12 floats, the shape the first script used


def deflate(data, level=9, zdict=None):
    if zdict is None:
        c = zlib.compressobj(level, zlib.DEFLATED, -WINDOW, 9)
    else:
        c = zlib.compressobj(level, zlib.DEFLATED, -WINDOW, 9,
                             zlib.Z_DEFAULT_STRATEGY, zdict)
    return c.compress(data) + c.flush()


def byte_shuffle(data, stride):
    n = len(data) // stride
    head = data[:n * stride]
    out = bytearray(head)
    for b in range(stride):
        out[b * n:(b + 1) * n] = head[b::stride]
    return bytes(out) + data[n * stride:]


def pct(a, b):
    return 100.0 * a / b if b else 0.0


# ------------------------------------------------ 1. the artefact, verified

print("=" * 78)
print("1. The first measurement's payloads, compared byte for byte")
print("=" * 78)
print()


def first_mesh(vertex_count, seed):
    """Reproduced from the first version of measure-owd.py, exactly."""
    import random
    r = random.Random(seed)
    out = bytearray()
    for i in range(vertex_count):
        x = i * 0.017
        pos = (x, 1.5 + 0.3 * (i % 7), 2.0 + 0.1 * (i % 13))
        nrm = (0.0, 1.0, 0.0)
        uv = (i * 0.001, (i % 512) * 0.002)
        tan = (1.0, 0.0, 0.0, 1.0)
        for v in pos + nrm + uv + tan:
            out += struct.pack("<f", v)
    for i in range(0, len(out), 4):
        if r.random() < 0.02:
            out[i] ^= 0x01
    return bytes(out)


a = first_mesh(800, 2)
b = first_mesh(800, 200)
differ = sum(1 for x, y in zip(a, b) if x != y)
print(f"   mesh 800 verts, seed 2   vs mesh 800 verts, seed 200")
print(f"     both are {len(a):,} bytes")
print(f"     bytes that differ: {differ:,} ({pct(differ, len(a)):.1f}%)")
print()

# where does the geometry stop depending on the seed?
geom_a = a[:len(a) // 1]
print(f"   the geometry fields do not read the seed at all: the loop writes")
print(f"   x = i*0.017, a constant normal and a constant tangent. Only the")
print(f"   noise pass reads the seed, and it touches {pct(differ, len(a)):.1f}% of")
print(f"   the bytes. So 'a dictionary built from 40 other meshes' was a")
print(f"   dictionary built from the measured payload with slightly different")
print(f"   noise, which is why it helped.")
print()

print(f"   re-measured with that noise removed, so the two payloads are")
print(f"   byte-identical, which is the strongest form of the artefact:")
identical = first_mesh(800, 2)
dict_src = identical * 40
D = dict_src[-MAX_DICT:]
x1 = len(deflate(identical))
x2 = len(deflate(identical, zdict=D))
print(f"     payload                  {len(identical):>10,} bytes")
print(f"     deflate alone            {x1:>10,}  {pct(x1, len(identical)):>6.1f}%")
print(f"     deflate with dictionary  {x2:>10,}  {pct(x2, len(identical)):>6.1f}%"
      f"   {100.0 * (x1 - x2) / x1:+.1f}%")
print()
print("   the dictionary helps here and the number is meaningless: the")
print("   dictionary is the payload.")

# --------------------------------------------- 2. the case it could win

print()
print("=" * 78)
print("2. The case a dictionary could legitimately win: an LOD chain")
print("=" * 78)
print()
print("   Decimation keeps the surviving vertices in their original order, so")
print("   the coarser level is a byte-for-byte subsequence of the finer one.")
print("   This is the strongest legitimate overlap that exists.")
print()


def vertex(i, w):
    x = (i % w) / w * 10.0
    y = (i // w) / w * 10.0
    z = math.sin(x) * math.cos(y)
    nx = -math.cos(x) * math.cos(y)
    ny = math.sin(x) * math.sin(y)
    nz = 1.0
    ln = math.sqrt(nx * nx + ny * ny + nz * nz)
    return struct.pack("<8f", x, y, z, nx / ln, ny / ln, nz / ln,
                       x / 10.0, y / 10.0)


W = 160
N = 25600
LOD0 = b"".join(vertex(i, W) for i in range(N))
print(f"   LOD0 is {N:,} vertices, {len(LOD0):,} bytes, 32-byte records")
print(f"   the window holds {MAX_DICT // 32:,} records, so a dictionary can")
print(f"   hold {pct(MAX_DICT, len(LOD0)):.1f}% of LOD0 at most")
print()
print(f"{'level':<14}{'vertices':>10}{'bytes':>12}{'nodict':>11}{'dict':>11}"
      f"{'gain':>8}")
for step in (1, 2, 4, 8):
    keep = list(range(0, N, step))
    data = b"".join(LOD0[i * 32:(i + 1) * 32] for i in keep)
    # the dictionary is the finer level, which genuinely contains this level
    D = LOD0[:MAX_DICT]
    x1 = len(deflate(data))
    x2 = len(deflate(data, zdict=D))
    label = f"LOD{int(math.log2(step))}"
    print(f"{label:<14}{len(keep):>10,}{len(data):>12,}{x1:>11,}{x2:>11,}"
          f"{100.0 * (x1 - x2) / x1:>7.2f}%")

print()
print("   and the ceiling is arithmetic, not implementation: the dictionary")
print(f"   holds {MAX_DICT:,} bytes, so it can only ever supply matches for")
print(f"   {MAX_DICT:,} of the payload's bytes. On a payload of {len(LOD0):,}")
print(f"   bytes that is {pct(MAX_DICT, len(LOD0)):.1f}%, and the surviving records")
print("   inside that window are the only ones it can match.")

# --------------------------------------------- 3. unrelated payloads

print()
print("=" * 78)
print("3. Unrelated payloads of the same kind, geometry that varies")
print("=" * 78)
print()


def varying(i, w, seed):
    """A vertex whose every field depends on the payload's identity."""
    x = (i % w) / w * 10.0
    y = (i // w) / w * 10.0
    z = math.sin(x * (1.0 + seed * 0.01)) * math.cos(y)
    nx = -math.cos(x) * math.cos(y)
    ny = math.sin(x) * math.sin(y)
    nz = 1.0 + seed * 0.001
    ln = math.sqrt(nx * nx + ny * ny + nz * nz)
    return struct.pack("<8f", x, y, z, nx / ln, ny / ln, nz / ln,
                       x / 10.0, y / 10.0)


others = b"".join(b"".join(varying(i, W, 100 + k) for i in range(1200))
                  for k in range(40))
D = others[-MAX_DICT:]
print(f"{'payload':<28}{'bytes':>10}{'nodict':>10}{'dict':>10}{'gain':>9}")
for k in range(6):
    data = b"".join(varying(i, W, 200 + k) for i in range(1200))
    x1 = len(deflate(data))
    x2 = len(deflate(data, zdict=D))
    print(f"  mesh 1200 verts #{k:<11}{len(data):>10,}{x1:>10,}{x2:>10,}"
          f"{100.0 * (x1 - x2) / x1:>8.1f}%")

print()
print("   and with the filter applied first, since the two are meant to")
print("   compose:")
print(f"{'payload':<28}{'shuffle':>10}{'shuf+dict':>10}{'gain':>9}")
for k in range(6):
    data = b"".join(varying(i, W, 200 + k) for i in range(1200))
    f = byte_shuffle(data, 32)
    x1 = len(deflate(f))
    x2 = len(deflate(f, zdict=D))
    print(f"  mesh 1200 verts #{k:<11}{x1:>10,}{x2:>10,}"
          f"{100.0 * (x1 - x2) / x1:>8.1f}%")

print()
print("=" * 78)
print("4. What the filter does, for comparison")
print("=" * 78)
print()
data = LOD0
x1 = len(deflate(data))
f = byte_shuffle(data, 32)
x2 = len(deflate(f))
print(f"   vertices, {N:,} records of 32 bytes, {len(data):,} bytes")
print(f"     raw deflate                  {x1:>11,}  {pct(x1, len(data)):>6.1f}%")
print(f"     byte transpose, record 32    {x2:>11,}  {pct(x2, len(data)):>6.1f}%"
      f"   {x1 / x2:.2f}x")
print()
print("   the filter needs no dictionary, no header field, no build step and")
print("   no invalidation rule: it is a rearrangement the decoder reverses from")
print("   a field the record already carries.")
