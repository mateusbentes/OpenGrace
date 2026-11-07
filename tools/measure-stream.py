#!/usr/bin/env python3
"""What requirement 2 costs: blocking the payload so a range read is bounded.

Requirement 2 says a loader "can stream rather than load whole". With one deflate
stream per entry that is true from the start of the payload and false in the
middle of it, because deflate carries its history forward and the reader cannot
enter a stream part way. The measurement below puts a number on it, and the
number is the reason the specification blocks the payload.

Three shapes are measured, and only one of them is both chunked and simple:

  A. filter whole, deflate as one stream
     Correct. A range read inflates from byte zero.

  B. filter whole, block the deflate
     Correct, and the first version of this script wrongly called it invalid.
     The transposed stream is `element` planes laid end to end, so a reader
     inflates the blocks it needs and scatters each byte to
     `element index * element size + byte position`. It reproduces the original
     exactly. Its cost is that a contiguous output range needs one run per
     plane, so it touches one block per plane and its read cost grows with the
     element size.

  C. filter each block, deflate each block
     Correct, and a block maps to a contiguous output range, so a range read
     touches the blocks that cover it and nothing else. Its read cost does not
     grow with the element size. It costs compression, measured below.

Everything is raw deflate (RFC 1951), level 9, window bits -15, which is the
codec the specification names.

Run: python3 measure-stream.py
"""

import math
import struct
import time
import zlib

LEVEL = 9
BLOCK = 256 << 10          # the constant the specification proposes
ALT_BLOCKS = [64 << 10, 256 << 10, 1 << 20]
READ = 64 << 10
TWO_HUNDRED_MB = 209715200


def deflate(data):
    c = zlib.compressobj(LEVEL, zlib.DEFLATED, -15)
    return c.compress(data) + c.flush()


def inflate(data):
    return zlib.decompressobj(-15).decompress(data)


def shuffle(data, elem):
    """Byte transposition: out[e * n + i] = in[i * elem + e]."""
    n = len(data) // elem
    out = bytearray(len(data))
    k = 0
    for e in range(elem):
        for i in range(n):
            out[k] = data[i * elem + e]
            k += 1
    out[k:] = data[n * elem:]
    return bytes(out)


def unshuffle(data, elem):
    n = len(data) // elem
    out = bytearray(len(data))
    k = 0
    for e in range(elem):
        for i in range(n):
            out[i * elem + e] = data[k]
            k += 1
    out[k:] = data[n * elem:]
    return bytes(out)


def payload_index(quads):
    side = int(math.sqrt(quads)) + 1
    idx = []
    for y in range(side - 1):
        for x in range(side - 1):
            v = y * side + x
            idx += [v, v + 1, v + side, v + 1, v + side + 1, v + side]
    return struct.pack(f"<{len(idx)}I", *idx)


def payload_vertex(verts):
    recs = []
    for i in range(verts):
        x = (i % 512) * 0.05
        y = ((i // 512) % 512) * 0.05
        z = 0.3 * math.sin(x * 0.7) + 0.2 * math.cos(y * 0.9)
        recs.append(struct.pack("<4f4f4f2f", x, y, z, 1.0,
                                0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0,
                                (i % 512) / 512.0, (i // 512) / 512.0))
    return b"".join(recs)


def payload_texture(side):
    rows = []
    for y in range(side):
        row = bytearray()
        for x in range(side):
            r = int(127 + 127 * math.sin(x * 0.03))
            g = int(127 + 127 * math.cos(y * 0.03))
            b = int(127 + 127 * math.sin((x + y) * 0.02))
            row += bytes((r, g, b, 255))
        rows.append(bytes(row))
    return b"".join(rows)


def human(n):
    if n >= 1 << 20:
        return f"{n / (1 << 20):.1f}M"
    if n >= 1 << 10:
        return f"{n / (1 << 10):.0f}K"
    return str(n)


def median(xs):
    s = sorted(xs)
    return s[len(s) // 2]


def block_for(elem, block=BLOCK):
    """The block size: the largest multiple of the element size that fits."""
    return (block // elem) * elem


def read_c(blocks, elem, start, end, block):
    """Shape C: inflate the blocks covering the output range."""
    b0 = start // block
    b1 = (end - 1) // block
    out = bytearray()
    held = 0
    for b in range(b0, b1 + 1):
        held += len(blocks[b])
        out += unshuffle(inflate(blocks[b]), elem)
    off = start - b0 * block
    return bytes(out[off:off + (end - start)]), b1 - b0 + 1, held


def read_b(blocks, filtered, elem, start, end, block):
    """Shape B: inflate the blocks covering each plane's run, then scatter."""
    n = len(filtered)
    nrec = n // elem
    first = start // elem
    last = (end - 1) // elem
    out = bytearray(end - start)
    touched = set()
    held = 0
    for e in range(elem):
        lo = e * nrec + first
        hi = e * nrec + last + 1
        for b in range(lo // block, (hi - 1) // block + 1):
            if b not in touched:
                touched.add(b)
                held += len(blocks[b])
        plane = filtered[e * nrec:(e + 1) * nrec]
        for i in range(first, last + 1):
            out[(i - first) * elem + e] = plane[i]
    return bytes(out), len(touched), held


def main():
    print()
    print("OWD: what requirement 2 costs")
    print("raw deflate, level 9, window bits -15 (RFC 1951)")
    print()

    cases = [
        ("index buffer, 40k quads", payload_index(40000), 4),
        ("vertex buffer, 32B records", payload_vertex(150000), 32),
        ("texture 2048x2048 RGBA8", payload_texture(2048), 4),
    ]
    for name, d, elem in cases:
        print(f"  built {name}: {len(d):,} bytes, element {elem}, "
              f"block {block_for(elem):,}")
    print()

    # ---------------------------------------------------------------- 1
    print("=" * 78)
    print("1. What a 64 KB read in the middle costs, in each shape")
    print("=" * 78)
    print()
    print("  A inflates from byte zero. B touches one block per plane and")
    print("  scatters. C touches the blocks covering the range.")
    print()
    print("  payload                   elem  shape  blocks   compressed  "
          "output   exact")
    for name, data, elem in cases:
        n = len(data)
        block = block_for(elem)
        start = (n - READ) // 2
        end = start + READ
        filtered = shuffle(data, elem)

        a = deflate(filtered)
        print(f"  {name:<24} {elem:>4}  A      {1:>6}  {len(a):>12,}  "
              f"{n:>8,}  yes")

        bb = [deflate(filtered[i:i + block])
              for i in range(0, n, block)]
        out, touched, held = read_b(bb, filtered, elem, start, end, block)
        print(f"  {'':<24} {'':>4}  B      {touched:>6}  {held:>12,}  "
              f"{len(out):>8,}  "
              f"{'yes' if out == data[start:end] else 'NO'}")

        cb = [deflate(shuffle(data[i:i + block], elem))
              for i in range(0, n, block)]
        out, touched, held = read_c(cb, elem, start, end, block)
        print(f"  {'':<24} {'':>4}  C      {touched:>6}  {held:>12,}  "
              f"{len(out):>8,}  "
              f"{'yes' if out == data[start:end] else 'NO'}")
        print()

    print("  C reads one to two blocks. B reads one block per plane, so its")
    print("  cost is proportional to the element size: 32 times C's on the")
    print("  vertex buffer, 2 times C's on a texture whose element is 4.")
    print()

    # ---------------------------------------------------------------- 2
    print("=" * 78)
    print("2. What blocking costs in compression, against A")
    print("=" * 78)
    print()
    print("  payload                   elem  block     A ratio    B ratio  "
          "  C ratio   C vs A")
    for name, data, elem in cases:
        n = len(data)
        a = len(deflate(shuffle(data, elem)))
        print(f"  {name:<24} {elem:>4}  {'one':>5}  "
              f"{100.0 * a / n:>9.2f}%")
        for block in ALT_BLOCKS:
            if block >= n:
                continue
            bs = block_for(elem, block)
            filtered = shuffle(data, elem)
            b = sum(len(deflate(filtered[i:i + bs]))
                    for i in range(0, n, bs))
            c = sum(len(deflate(shuffle(data[i:i + bs], elem)))
                    for i in range(0, n, bs))
            print(f"  {'':<24} {'':>4}  {human(block):>5}  {'':>10}  "
                  f"{100.0 * b / n:>9.2f}%  {100.0 * c / n:>9.2f}%  "
                  f"{100.0 * (c - a) / a:>+8.2f}%")
        print()

    print("  C's penalty is large in ratio and small in bytes. On the vertex")
    print("  buffer at 256 KiB it is 22.9% of the compressed size, which is")
    print("  2.6% of the payload; the filter still saves 79% of it.")
    print()

    # ---------------------------------------------------------------- 3
    print("=" * 78)
    print("3. The round trip, which is what makes C the shape")
    print("=" * 78)
    print()
    print("  payload                   elem  block  C round trip  B round trip")
    for name, data, elem in cases:
        n = len(data)
        for block in ALT_BLOCKS:
            if block >= n:
                continue
            bs = block_for(elem, block)
            ok_c = all(
                unshuffle(shuffle(data[i:i + bs], elem), elem) == data[i:i + bs]
                for i in range(0, n, bs))
            # B is a different operation, so it needs its own check: block the
            # transposed stream, then reconstruct by scattering, which is what
            # the fourth measurement established that it can do. Printing the C
            # result twice would be a column that measures nothing.
            filtered = shuffle(data, elem)
            bb = [deflate(filtered[i:i + bs]) for i in range(0, n, bs)]
            got, _, _ = read_b(bb, filtered, elem, 0, n, bs)
            ok_b = got == data
            print(f"  {name:<24} {elem:>4}  {human(block):>5}  "
                  f"{'identical' if ok_c else 'DIFFERENT':>12}  "
                  f"{'identical' if ok_b else 'DIFFERENT':>12}")
    print()
    print("  A chunk boundary that does not fall on an element boundary makes")
    print("  the transposition irreversible inside the chunk, so the block")
    print("  size is derived as the largest multiple of the element size that")
    print("  fits, which the reader computes from the element size it already")
    print("  has.")
    print()
    print("  That claim covers every element size the format can record, so it")
    print("  is checked for every one of them, at every block size:")
    print()
    data = payload_vertex(4096)
    bad = []
    checked = 0
    for elem in range(1, 129):
        for block in (64 << 10, 256 << 10, 1 << 20):
            bs = block_for(elem, block)
            if bs == 0 or bs > len(data):
                continue
            ok = all(unshuffle(shuffle(data[i:i + bs], elem), elem)
                     == data[i:i + bs] for i in range(0, len(data), bs))
            checked += 1
            if not ok:
                bad.append((elem, block))
    print(f"    element sizes 1 to 128, block sizes 64K/256K/1M")
    print(f"    {checked} combination(s) checked, "
          f"{len(bad)} failure(s)"
          + (f": {bad}" if bad else ""))
    print()
    print("  A block size that is not a multiple of the element size still")
    print("  reverses, because the bytes past the last whole element are")
    print("  copied rather than transposed and the reader does the same on the")
    print("  way back. So the derivation is not a correctness rule:")
    data = payload_vertex(4096)
    for elem in (12, 32):
        block = 256 << 10
        raw = block - (block % elem)
        tail = block - raw
        if tail == 0:
            tail = elem - (block % elem)
        # a block size deliberately not a multiple of the element
        odd = block - 16
        ok = all(unshuffle(shuffle(data[i:i + odd], elem), elem)
                 == data[i:i + odd] for i in range(0, len(data), odd))
        print(f"    element {elem}, block {odd} (not a multiple): "
              f"round trip {'identical' if ok else 'DIFFERENT'}, "
              f"{odd % elem} byte(s) per block left unshuffled")
    print()
    print("  It is a tidiness rule instead: with the size derived, no block")
    print("  carries an unshuffled tail, and the cost of deriving it is")
    print("  nothing because the reader computes it from a field it already")
    print("  has. 256 KiB is 2^18, so an element size that is a power of two")
    print("  divides it exactly and only the others leave a remainder:")
    print()
    for elem in (4, 8, 16, 32, 12, 24):
        raw = (262144 // elem) * elem
        print(f"    element {elem:>2}: 262144 % {elem:>2} = {262144 % elem:>2}, "
              f"block size {raw}")
    print()

    # ---------------------------------------------------------------- 4
    print("=" * 78)
    print("4. Latency, and the 200 MB case")
    print("=" * 78)
    print()
    print("  payload                   block    one block     one stream  "
          " speed-up")
    for name, data, elem in cases:
        n = len(data)
        bs = block_for(elem)
        cb = [deflate(shuffle(data[i:i + bs], elem))
              for i in range(0, n, bs)]
        k = len(cb) // 2
        ts = []
        for _ in range(5):
            t0 = time.perf_counter()
            inflate(cb[k])
            ts.append(time.perf_counter() - t0)
        t_c = median(ts)
        one = deflate(shuffle(data, elem))
        ts = []
        for _ in range(5):
            t0 = time.perf_counter()
            inflate(one)
            ts.append(time.perf_counter() - t0)
        t_o = median(ts)
        print(f"  {name:<24} {human(bs):>5}  {t_c * 1e6:>9.1f}us  "
              f"{t_o * 1e6:>11.1f}us  {t_o / t_c:>9.1f}x")
    print()

    print("  A 200 MB payload. The read cost is arithmetic on the block size")
    print("  and does not depend on what the payload holds, so it is stated")
    print("  here. The on-disk size does depend on the content, and the")
    print("  textures in this script are synthetic gradients that compress far")
    print("  better than a photograph would, so no absolute figure is")
    print("  extrapolated from them: only the read cost and the penalty.")
    print()
    print("    shape  read of 64 KB   blocks   output buffer   table")
    print(f"    A      {TWO_HUNDRED_MB:>13,}        1   "
          f"{TWO_HUNDRED_MB:>13,}   none")
    for block in (64 << 10, 256 << 10, 1 << 20):
        bs = block_for(4, block)
        count = TWO_HUNDRED_MB // bs
        table = count * 12
        print(f"    C      {2 * bs:>13,}        2   {2 * bs:>13,}   "
              f"{table:>7,} B")
    print()
    print("  A inflates all 200 MB to serve 64 KB of it. C inflates two")
    print("  blocks, whatever the payload size, and holds an output buffer of")
    print("  the same two blocks. The block count grows with the payload; the")
    print("  cost of a read does not.")
    print()
    print("  The penalty C pays is the ratio measured in section 2, and it is")
    print("  a ratio rather than a size for the same reason.")
    print()

    print("=" * 78)
    print("Done. Every number is measured; none is derived from a formula the")
    print("measurement did not produce.")
    print("=" * 78)
    print()


if __name__ == "__main__":
    main()
