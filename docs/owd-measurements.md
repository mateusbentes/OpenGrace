# OWD: the measurements, and one retraction

The previous measurement of the shared dictionary was wrong, and the reason is
worth recording, because it is the same class of mistake this project has been
catching all along: a measurement whose inputs were never inspected.

Three things were found.

1. The dictionary's numbers came from payloads that were nearly identical to
   each other. Re-measured honestly, a dictionary does not pay, and the
   specification should record its absence.
2. The specification's filter section contradicts itself: its prose describes
   two different operations, and the measured table belongs to only one of
   them.
3. The index measurement stands, and it is the one that decides a requirement.

## 1. The dictionary: retracted

### What was measured, and why it was not what it said

The first script built a "dictionary from 40 other meshes" and applied it to
ten meshes it claimed were not part of it. Before believing the result, the
payloads were compared byte for byte:

```
mesh 800 verts, seed 2   vs mesh 800 verts, seed 200
  both are 38,400 bytes
  bytes that differ: 368 (1.0%)
```

The generator's geometry never read its seed. Every vertex wrote
`x = i * 0.017`, a constant normal `(0, 1, 0)` and a constant tangent
`(1, 0, 0, 1)`; only a final noise pass touched the seed, and it changed 1.0%
of the bytes. So the dictionary was built from the measured payload with
slightly different noise. The reported gain was self-reference.

With the noise removed, making the two byte-identical, the gain is 9.8% — and
it means nothing, because the dictionary is the payload.

### Re-measured on payloads that actually differ

Geometry whose every field depends on the payload's identity, a dictionary
built from forty other meshes:

| payload | bytes | no dictionary | dictionary | gain |
| --- | ---: | ---: | ---: | ---: |
| mesh 1200 verts #0 | 38,400 | 24,378 | 24,830 | **−1.9%** |
| mesh 1200 verts #1 | 38,400 | 24,387 | 24,825 | −1.8% |
| mesh 1200 verts #2 | 38,400 | 24,385 | 24,825 | −1.8% |
| mesh 1200 verts #3 | 38,400 | 24,380 | 24,819 | −1.8% |
| mesh 1200 verts #4 | 38,400 | 24,389 | 24,832 | −1.9% |
| mesh 1200 verts #5 | 38,400 | 24,376 | 24,842 | −1.9% |

The dictionary makes the payload **larger**. The window's fixed cost is paid
and no match is found, because two meshes of the same kind share no bytes
beyond the accidental.

Over the filter, the gain is exactly nothing:

| payload | shuffle | shuffle + dictionary | gain |
| --- | ---: | ---: | ---: |
| mesh 1200 verts #0 | 14,138 | 14,137 | 0.0% |
| mesh 1200 verts #1 | 14,255 | 14,255 | 0.0% |
| mesh 1200 verts #2 | 14,226 | 14,225 | 0.0% |

This is the decisive row. The filter has already grouped the bytes that
repeat; whatever the dictionary could have matched has already been captured,
so the two do not compose at all. The earlier claim that they compose was
measured on the artefact.

### The one legitimate case, and its ceiling

An LOD chain is the strongest overlap that exists: decimation keeps the
surviving vertices in their original order, so the coarser level is a
byte-for-byte subsequence of the finer one. A dictionary built from LOD0,
applied to the levels that are literally inside it:

| level | vertices | bytes | no dictionary | dictionary | gain |
| --- | ---: | ---: | ---: | ---: | ---: |
| LOD0 | 25,600 | 819,200 | 539,210 | 539,486 | −0.05% |
| LOD1 (1/2) | 12,800 | 409,600 | 267,658 | 259,125 | 3.19% |
| LOD2 (1/4) | 6,400 | 204,800 | 132,573 | 128,280 | 3.24% |
| LOD3 (1/8) | 3,200 | 102,400 | 65,443 | 63,257 | 3.34% |

Three percent, in the best case that can be constructed, and the ceiling is
arithmetic rather than implementation. A dictionary cannot exceed the deflate
window, 32,768 bytes, so it can only ever supply matches for 32,768 bytes of a
payload. Against LOD0's 819,200 bytes that is **4.0%**, and only the records
falling inside that window can be reached at all. LOD0 itself gains nothing,
because its own content is not in its own dictionary.

### Conclusion

**A dictionary is not worth specifying.** It costs a header field, a build
step, an invalidation rule and a cross-build compatibility constraint — an
archive written by one build would not be readable by another unless the
dictionary is carried in the file, which is another field and another
checksum. It returns −1.9% on unrelated payloads, 0.0% over the filter, and
3.2% in the single contrived case of an LOD chain, capped at 4.0% by the
window.

For comparison, the filter returns this, needing none of it:

| payload | raw deflate | byte transpose | ratio |
| --- | ---: | ---: | ---: |
| vertices, 25,600 records of 32 bytes, 819,200 bytes | 539,210 (65.8%) | 285,573 (34.9%) | **1.89×** |

Section 9 of the specification should gain a line, in the voice of the
others: a shared dictionary is absent, because the deflate window bounds it to
a fraction of any payload large enough to want one, and because it does not
compose with the filter that already captures the same repetition.

## 2. The filter section contradicts itself

Section 4.6 defines filter 1 in two sentences that describe different
operations:

> The payload is treated as an array of records of `element size` bytes. The
> output is every record's first byte, then every record's second byte, and so
> on. A vertex that is eight floats becomes eight planes of four-byte values,
> and each plane compresses on its own.

The first sentence is byte transposition and yields `element size` planes of
**one byte** each; for a 32-byte vertex that is 32 planes. The second sentence
describes record transposition, which yields 8 planes of **four bytes** each.
Measured on the same 819,200-byte vertex buffer:

| transform | bytes | of raw | vs raw deflate |
| --- | ---: | ---: | ---: |
| none (raw deflate) | 539,210 | 65.8% | 1.00× |
| byte transpose, record 32 (32 planes) | 285,573 | 34.9% | **1.89×** |
| record transpose, 8 planes of 4 | 353,087 | 43.1% | 1.53× |
| delta + byte transpose | 238,600 | 29.1% | 2.26× |

They are not the same filter and they do not perform the same. The table
already in section 4.6 reports 30.6% for its grid vertex buffer, which is the
byte transposition; the prose's second sentence describes an operation that
table never measured.

The resolution is the one the record already implies: `element size` is the
record size, the output is `element size` planes of one byte, and the second
sentence is corrected to say so. That keeps the field's meaning, keeps the
measured table valid, and removes the ambiguity.

## 3. The index measurement stands

This is the one that decides a requirement. Requirement 5 says random access
without inflating the archive, and if the index is one deflated stream then
finding one entry inflates all of it.

One stream, 20 inflates, best and median:

| entries | index raw | deflated | of raw | best | median |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 69,000 | 9,671 | 14.0% | 0.0 ms | 0.0 ms |
| 10,000 | 690,000 | 90,651 | 13.1% | 0.3 ms | 0.4 ms |
| 100,000 | 6,900,000 | 890,459 | 12.9% | 3.4 ms | 3.6 ms |
| 400,000 | 27,600,000 | 3,513,508 | 12.7% | 22.8 ms | 32.9 ms |

The same index in independently deflated blocks of 256 records:

| entries | blocks | table | one block | of whole | best | median |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 4 | 40 | 2,696 | 27.9% | 12 µs | 12 µs |
| 10,000 | 40 | 328 | 2,704 | 3.0% | 12 µs | 12 µs |
| 100,000 | 391 | 3,136 | 2,705 | 0.3% | 12 µs | 12 µs |
| 400,000 | 1,563 | 12,512 | 2,710 | 0.1% | 12 µs | 12 µs |

The table is uncompressed and grows by eight bytes per 256 entries: 12,512
bytes at four hundred thousand entries, against the 3,513,508 it replaces. A
lookup costs the table plus one block and does not grow with the archive — 12
µs at every size measured, against 22.8 ms at the largest.

Timings come from a shared sandbox and vary between runs: the first run of
this script reported 44.8 ms for the largest index where the final one reports
22.8 ms. The decision rests on the ratio between the two designs, which is
three orders of magnitude in every run, not on any single figure.

**Conclusion: the index is a sequence of blocks.** This is what makes
requirement 5 true at the level of the index and not only of the payload.

## 4. What was not measured, and is a larger question

Requirement 2 promises that a loader "can stream rather than load whole", and
the encoding cannot deliver it. A record carries one payload offset, one size
on disk and one size inflated, so an entry is one address range and a 200 MB
texture is read whole. Streaming is not an addition to the current record; it
changes the record's shape, because a reader has to be able to ask for a byte
range and the writer has to have arranged the payload so that a range is
independently decodable. It is a new requirement, and it is raised here on its
own rather than folded into the dictionary question, where it would have been
buried.
