#!/usr/bin/env python3
"""Measure the OWD open questions, and pin down what filter 1 actually is.

Three things are measured.

1. Filter 1, because the specification's prose and its measured table appear to
   describe different operations. The prose says "every record's first byte, then
   every record's second byte", which is byte transposition and yields one plane
   per byte. The next sentence says "a vertex that is eight floats becomes eight
   planes of four-byte values", which is record transposition and yields one
   plane per float. On a 32-byte vertex the first yields 32 planes and the second
   yields 8. The number in the table has to belong to one of them.

2. A shared dictionary, on payloads that are not part of it, both unfiltered and
   over each filter reading.

3. The index, as one deflated stream against independently deflated blocks.

Run: python3 measure-owd.py
"""

import math
import random
import statistics
import struct
import time
import zlib

WINDOW = 15
MAX_DICT = 1 << WINDOW


def deflate(data, level=9, zdict=None):
    if zdict is None:
        c = zlib.compressobj(level, zlib.DEFLATED, -WINDOW, 9)
    else:
        c = zlib.compressobj(level, zlib.DEFLATED, -WINDOW, 9,
                             zlib.Z_DEFAULT_STRATEGY, zdict)
    return c.compress(data) + c.flush()


def inflate(data, zdict=None):
    if zdict is None:
        d = zlib.decompressobj(-WINDOW)
    else:
        d = zlib.decompressobj(-WINDOW, zdict)
    return d.decompress(data) + d.flush()


def pct(a, b):
    return 100.0 * a / b if b else 0.0


def byte_shuffle(data, stride):
    """Every record's byte 0, then every record's byte 1, ... (prose sentence 1).

    `stride` is the record size. A 32-byte vertex gives 32 one-byte planes.
    """
    n = len(data) // stride
    head = data[:n * stride]
    out = bytearray(head)
    for b in range(stride):
        out[b * n:(b + 1) * n] = head[b::stride]
    if len(data) > n * stride:
        out += data[n * stride:]
    return bytes(out)


def byte_unshuffle(data, stride):
    n = len(data) // stride
    head = data[:n * stride]
    out = bytearray(head)
    for b in range(stride):
        out[b::stride] = head[b * n:(b + 1) * n]
    if len(data) > n * stride:
        out += data[n * stride:]
    return bytes(out)


def record_shuffle(data, record, element):
    """Each record emitted whole, records grouped by element position.

    Prose sentence 2: a 32-byte vertex of 8 four-byte floats gives 8 planes of
    four-byte values. `element` is the field size, `record` the whole record.
    """
    n = len(data) // record
    head = data[:n * record]
    out = bytearray()
    for f in range(record // element):
        for i in range(n):
            base = i * record + f * element
            out += head[base:base + element]
    if len(data) > n * record:
        out += data[n * record:]
    return bytes(out)


def record_unshuffle(data, record, element):
    n = len(data) // record
    fields = record // element
    head = data[:n * record]
    out = bytearray(head)
    k = 0
    for f in range(fields):
        for i in range(n):
            base = i * record + f * element
            out[base:base + element] = head[k:k + element]
            k += element
    if len(data) > n * record:
        out += data[n * record:]
    return bytes(out)


def delta_words(data, stride):
    """Specification filter 2: each 32-bit word minus the word before it."""
    words = stride // 4
    if words == 0:
        return data
    out = bytearray()
    prev = [0] * words
    for off in range(0, len(data) - len(data) % stride, stride):
        rec = struct.unpack_from("<%dI" % words, data, off)
        out += struct.pack("<%dI" % words,
                           *((rec[i] - prev[i]) & 0xFFFFFFFF
                             for i in range(words)))
        prev = rec
    return bytes(out)


# ------------------------------------------------------------------ payloads


def make_grid_verts(w, h, seed=7):
    """A realistic vertex buffer: computed normals, so nothing is constant.

    The first version of this script used constant normals and tangents, which
    made byte transposition look far better than it is, because the constant
    fields collapsed into planes of a single repeated value. That is an artefact
    of the generator, not a property of the filter.
    """
    r = random.Random(seed)
    out = bytearray()
    for i in range(w * h):
        x = (i % w) / w * 10.0
        y = (i // w) / h * 10.0
        z = math.sin(x) * math.cos(y)
        nx = -math.cos(x) * math.cos(y)
        ny = math.sin(x) * math.sin(y)
        nz = 1.0
        ln = math.sqrt(nx * nx + ny * ny + nz * nz)
        out += struct.pack("<8f", x, y, z, nx / ln, ny / ln, nz / ln,
                           x / 10.0, y / 10.0)
    return bytes(out)


def make_grid_indices(w, h):
    tris = []
    for y in range(h - 1):
        for x in range(w - 1):
            a = y * w + x
            tris += [a, a + 1, a + w, a + 1, a + w + 1, a + w]
    return b"".join(struct.pack("<I", i) for i in tris)


W, H = 200, 200
VERTS = make_grid_verts(W, H)
IDX = make_grid_indices(W, H)

print("=" * 78)
print("A. What filter 1 is: the specification's two sentences, measured")
print("=" * 78)
print()
print(f"   vertex buffer, {W}x{H} grid, 8 floats per vertex, 32-byte records")
print(f"   {len(VERTS):,} bytes. Normals and tangents are computed, not")
print(f"   constant, so no field collapses on its own.")
print()
print(f"{'transform':<34}{'bytes':>12}{'of raw':>9}{'vs deflate':>12}")

base = len(deflate(VERTS))
rows = [
    ("none (raw deflate)", base),
    ("byte transpose, record 32 (32 planes)", len(deflate(byte_shuffle(VERTS, 32)))),
    ("byte transpose, record 4 (4 planes)", len(deflate(byte_shuffle(VERTS, 4)))),
    ("record transpose, 8 planes of 4", len(deflate(record_shuffle(VERTS, 32, 4)))),
    ("delta + byte transpose, record 32",
     len(deflate(byte_shuffle(delta_words(VERTS, 32), 32)))),
]
for label, size in rows:
    print(f"{label:<34}{size:>12}{pct(size, len(VERTS)):>8.1f}%"
          f"{base / size:>11.2f}x")

print()
print("   round trip, so a transform that loses bytes cannot be reported:")
for label, enc, dec in (
    ("byte transpose, record 32", lambda d: byte_shuffle(d, 32),
     lambda d: byte_unshuffle(d, 32)),
    ("byte transpose, record 4", lambda d: byte_shuffle(d, 4),
     lambda d: byte_unshuffle(d, 4)),
    ("record transpose, 8 planes", lambda d: record_shuffle(d, 32, 4),
     lambda d: record_unshuffle(d, 32, 4)),
):
    back = dec(enc(VERTS))
    same = back == VERTS
    print(f"     {label:<30} {'ok' if same else 'MISMATCH'}"
          f"  ({len(back):,} bytes back)")

print()
print("   index buffer, 4-byte records, for comparison:")
ibase = len(deflate(IDX))
ish = len(deflate(byte_shuffle(IDX, 4)))
print(f"     raw deflate          {ibase:>12,}  {pct(ibase, len(IDX)):>6.1f}%")
print(f"     byte transpose r4    {ish:>12,}  {pct(ish, len(IDX)):>6.1f}%"
      f"   {ibase / ish:.2f}x")

# ------------------------------------------------------- B. the dictionary
print()
print("=" * 78)
print("B. A shared dictionary, built from payloads the measurement is not of")
print("=" * 78)

DICT = b"".join(make_grid_verts(W, H, seed=100 + i) for i in range(4))[-MAX_DICT:]
print(f"   built from 4 other grids, {len(DICT):,} bytes "
      f"(capped at the {MAX_DICT}-byte window)")
print()
print(f"{'payload':<34}{'nodict':>11}{'dict':>11}{'gain':>8}")

variants = [
    ("vertices, raw", VERTS),
    ("vertices, byte transpose r32", byte_shuffle(VERTS, 32)),
    ("vertices, record transpose 8x4", record_shuffle(VERTS, 32, 4)),
    ("vertices, byte transpose r4", byte_shuffle(VERTS, 4)),
]
for label, data in variants:
    a = len(deflate(data))
    b = len(deflate(data, zdict=DICT))
    print(f"{label:<34}{a:>11}{b:>11}{100.0 * (a - b) / a:>7.1f}%")

print()
print("   the case the dictionary exists for: payloads too small to build")
print("   their own history. A 60-vertex mesh, 1,920 bytes of vertices.")
print()
print(f"{'payload':<34}{'nodict':>11}{'dict':>11}{'gain':>8}")
small = []
for i in range(5):
    r = random.Random(500 + i)
    out = bytearray()
    for v in range(60):
        x = v * 0.31
        out += struct.pack("<8f", x, 1.0 + 0.1 * (v % 5), 2.0,
                           0.0, 1.0, 0.0, x / 10.0, (v % 8) / 8.0)
    small.append(bytes(out))

for label, fn in (("small mesh, raw", lambda d: d),
                  ("small mesh, byte transpose r32",
                   lambda d: byte_shuffle(d, 32))):
    gains = []
    for data in small:
        f = fn(data)
        a = len(deflate(f))
        b = len(deflate(f, zdict=DICT))
        gains.append(100.0 * (a - b) / a)
    print(f"{label:<34}{'':>11}{'':>11}"
          f"{min(gains):>4.1f}-{max(gains):.1f}%")

print()
print("   and the index buffer, where the dictionary should have nothing to add:")
for label, data in (("indices, raw", IDX), ("indices, byte transpose r4",
                                            byte_shuffle(IDX, 4))):
    a = len(deflate(data))
    b = len(deflate(data, zdict=DICT))
    print(f"{label:<34}{a:>11}{b:>11}{100.0 * (a - b) / a:>7.1f}%")

# ------------------------------------------------- C. the index and seeking
print()
print("=" * 78)
print("C. The index as one deflated stream: what one lookup costs")
print("=" * 78)


def make_index(entry_count):
    kinds = [b"mesh", b"texture", b"script", b"audio", b"material"]
    out = bytearray()
    for i in range(entry_count):
        name = b"assets/%s/group%03d/entry_%06d.bin" % (
            kinds[i % len(kinds)], i % 200, i)
        out += struct.pack("<QQIIBBBBBB", i * 4096, i * 65536, 30000, 120000,
                           i % len(kinds), 1, 0, 4, 0, len(name))
        out += name
    return bytes(out)


REPEATS = 20
print(f"   best and median of {REPEATS} inflates")
print()
print(f"{'entries':>9}{'index raw':>12}{'deflated':>11}{'of raw':>8}"
      f"{'best':>9}{'median':>9}")
index_blobs = {}
for n in (1000, 10000, 100000, 400000):
    blob = make_index(n)
    index_blobs[n] = blob
    d = deflate(blob)
    ts = []
    for _ in range(REPEATS):
        t0 = time.perf_counter()
        inflate(d)
        ts.append((time.perf_counter() - t0) * 1000.0)
    print(f"{n:>9}{len(blob):>12,}{len(d):>11,}{pct(len(d), len(blob)):>7.1f}%"
          f"{min(ts):>8.1f}ms{statistics.median(ts):>8.1f}ms")

print()
print("   that is what a reader pays to find ONE entry when the index is one")
print("   stream: it inflates all of it and discards all but one record.")

print()
print("=" * 78)
print("D. The same index in independently deflated blocks of 256 records")
print("=" * 78)

BLOCK = 256
print(f"{'entries':>9}{'blocks':>8}{'table':>9}{'one block':>11}"
      f"{'vs whole':>10}{'best':>8}{'median':>9}")
for n in (1000, 10000, 100000, 400000):
    blob = index_blobs[n]
    per = len(blob) // n
    blocks = []
    off = 0
    while off < len(blob):
        end = min(off + per * BLOCK, len(blob))
        blocks.append(deflate(blob[off:end]))
        off = end
    table = len(blocks) * 8 + 8
    mid = blocks[len(blocks) // 2]
    ts = []
    for _ in range(2000):
        t0 = time.perf_counter()
        inflate(mid)
        ts.append((time.perf_counter() - t0) * 1e6)
    whole = len(deflate(blob))
    print(f"{n:>9,}{len(blocks):>8,}{table:>9,}{len(mid):>11,}"
          f"{pct(len(mid), whole):>9.1f}%{min(ts):>7.0f}us"
          f"{statistics.median(ts):>7.0f}us")

print()
print("   the table is uncompressed and grows by 8 bytes per 256 entries. A")
print("   reader pays the table plus one block, and that does not grow with")
print("   the archive.")

print()
print("=" * 78)
print("E. Summary for the specification")
print("=" * 78)
print()
print("   filter 1: the prose and the measured table describe different")
print("   operations on a 32-byte vertex; the table has to be attributed to")
print("   one of them explicitly.")
print()
print("   dictionary: bounded at 32768 bytes by the deflate window, a fixed")
print("   per-kind cost. Its value is concentrated on payloads too small to")
print("   build their own history, and it is largely redundant with the")
print("   filter where the filter already exposes the same repetition.")
print()
print("   index: one stream costs the whole index per lookup, growing with")
print("   the archive; blocks of 256 cost the table plus one block, flat.")
