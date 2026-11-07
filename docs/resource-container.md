# OWD: Open World Data container format

## 1. Scope

OpenGrace needs a container for shipped resources. The format is called OWD,
for Open World Data, and its files use the extension .owd.

The documented feature set lists a resource manager among the core
components, but the original's container format was never published and
containers were not exposed through the plugin architecture, so there is
nothing documented to reproduce. This document specifies the format this
project uses instead.

The format is specified by requirements first and by encoding second, because
the requirements are what a caller depends on and the encoding is only one way
to satisfy them. Every requirement below can be stated without reference to any
existing container, which is the test that separates a requirement from a
copied structure. Nothing here was derived from any shipped
product's format.

The name is a description and not a reference. Its two letters coincide
with those of a proprietary container belonging to an engine this project
does not reproduce, and the coincidence is one of initials: the expansion
names what the container holds, which is the data a world is built from,
and nothing about the format was derived from that other one. The two
share a payload codec and nothing else, and neither may be used to derive
the other.

## 2. Payload encoding

Payloads are DEFLATE streams as defined by RFC 1951, raw, with no wrapper.

The distinction is easy to get wrong and it has a consequence. RFC 1951 defines
a compressed stream and nothing else: no header, no length, and no checksum.
The two-byte header and the four-byte Adler-32 that make a stream self-checking
belong to RFC 1950, the zlib wrapper, which is what `compress()`,
`uncompress()` and most tooling produce by default. A container built on raw
Deflate therefore has no integrity check unless it stores one itself, and this
one does, per entry, in its index. "zlib provides the check for free" is true
of the wrapper and false of the stream.

zlib is the reference implementation and the only one the build depends on
today, but the format is defined over Deflate streams rather than over zlib's
interface: any conforming encoder or decoder satisfies it. The Deflate window
is at most 32 KiB, so inflating an entry never needs more than that plus the
output buffer.

Raw Deflate is chosen over the zlib wrapper for three reasons:

- A per-entry checksum is required anyway (requirement 8), and a stronger one
  can be stored than Adler-32.
- A wrapped payload embeds zlib's framing inside our framing, so the same bytes
  mean different things depending on which layer reads them.
- The container does not change if the payload codec is ever replaced, because
  the index already records the method per entry.

## 3. Requirements

These eight properties are the specification. Each is stated as what a caller
needs, followed by what it forces on the encoding.

1. **A stable identity per entry.** A name maps to exactly one resource, so a
   lookup is a function and not a search. Forces: names are unique within a
   container and are the index key.

2. **A resource kind and declared dependencies.** A loader can answer what a
   resource needs before reading it, and can stream rather than load whole.
   Forces: a kind field and a dependency list per entry.

3. **A per-entry method, stored or deflated, chosen by the build, with an
   optional byte filter ahead of it.** A payload that is already compressed
   gains nothing from being deflated again and pays for the attempt in build
   time. A payload whose bytes are interleaved gains a great deal from being
   transposed before it is deflated, and gains nothing at all from the
   transposition if it is already compressed. Forces: a method per entry
   naming both a filter and a codec, an element size for the filter, and a
   decoder that reverses the filter after inflating.

4. **Alignment per entry.** A texture or vertex buffer can be uploaded without
   an intermediate copy. Forces: a per-entry alignment exponent and padding
   between payloads.

5. **Random access without inflating the archive.** A level load reads only
   what it needs. Forces: an index that is readable without touching any
   payload, and absolute payload offsets.

6. **An index that permits appending.** A build adds resources rather than
   rewriting the container. Forces: the index is not a fixed-size region at a
   fixed offset, and appending does not require rewriting the payloads.

7. **Overlay priority between containers.** A loose file or a later container
   overrides an earlier one without rewriting anything. Without this there is
   no modding story, and the alternative is the full directory rebuild this
   design exists to avoid. Forces: a defined resolution order across
   containers, and a loader that can hold more than one open.

8. **An integrity check per entry.** Corruption is detected at the entry, not
   at the archive. Forces: a checksum per entry, stored by the container,
   because the payload encoding provides none (section 2).

## 4. Encoding

### 4.1 File structure

```
+---------------------------+
| header                    |  fixed size, at offset 0
+---------------------------+
| payload region            |  entries, each aligned
+---------------------------+
| index                     |  one record per entry
+---------------------------+
```

The header is at a fixed offset and states where the index is. The index is not
at a fixed offset, because requirement 6 forbids reserving a fixed-size region
for it. All integers are little-endian.

This is the one place where the design departs from the obvious archive layout,
and it departs on purpose. A container that locates its index from the end of
the file needs a seek to the tail before it can read anything, which is the
cost this format exists to avoid: the header is the first read, and it already
knows where the index is.

### 4.2 Header

| offset | size | field |
| --- | --- | --- |
| 0 | 4 | magic, `OWD` followed by a NUL byte |
| 4 | 1 | format major version |
| 5 | 1 | format minor version |
| 6 | 2 | flags, reserved, must be zero |
| 8 | 4 | entry count |
| 12 | 8 | index offset, absolute |
| 20 | 4 | index size in bytes |
| 24 | 4 | index checksum, CRC-32 of the index bytes |
| 28 | 4 | header checksum, CRC-32 of bytes 0 to 27 |

A reader that cannot read the header cannot read the container, so the header
carries its own check. The version is major and minor separately: a reader
accepts any minor it knows and refuses an unknown major, which is the usual
rule and is stated here so the first version bump does not have to invent one.

### 4.3 Index records

The index is a sequence of records, each self-delimiting by its own length, so
a reader parses until the index size is consumed. A record that would run past
the index size is corruption.

| size | field |
| --- | --- |
| 2 | record length in bytes, including this field |
| 2 | entry flags, bit 0: payload is deflated, bit 1: has dependencies, bits 2-3: filter |
| 8 | payload offset, absolute |
| 8 | payload size on disk |
| 8 | payload size inflated |
| 4 | payload checksum, CRC-32 of the inflated, unfiltered bytes |
| 1 | filter element size in bytes, `0` when no filter is applied |
| 1 | alignment exponent, `0` meaning no alignment |
| 2 | resource kind |
| 1 | name length in bytes |
| 1 | dependency count |
| n | name, UTF-8, not NUL-terminated |
| m | dependency names, each prefixed by a one-byte length |

Dependencies are stored as names rather than as indices into the index, so a
record is meaningful on its own and a reader never has to resolve a reference
before it can report what a resource needs. The cost is repeated name bytes,
which is trivial next to the payloads and does not grow with them.

The checksum covers the **inflated and unfiltered** bytes, not the stored
bytes. Checking the stored bytes would detect a corrupted file, but checking
the bytes the loader hands out also detects a compressor, a decompressor or a
filter that is wrong, which is the failure this project is more likely to
have. A filter recorded incorrectly is therefore caught by the same check as a
corrupt payload, and not by a crash inside the loader.

### 4.4 Names and kinds

A name is a UTF-8 string, case-sensitive, with `/` as the separator, and no
leading or trailing separator. Uniqueness is exact byte equality. Case-folding
is deliberately not performed: two names that differ only in case are two
resources, because a case-insensitive lookup on a case-sensitive filesystem is
a class of bug this project does not need.

A kind is a `u16` from a registry owned by the loader, not by the format. The
format does not enumerate kinds, because the set of resource types is a
property of the engine and will grow; it only requires that the field exists
and is stable for a given resource. Kind `0` is reserved for "unknown".

### 4.5 Alignment

The alignment exponent `a` requires the payload offset to be a multiple of
`2**a`. The writer inserts padding before the payload to satisfy it; padding
bytes are unspecified and are not read. `a = 0` means no requirement.

Alignment is expressed as a number and not as a graphics-API enumeration. It is
the one place where the renderer and the container meet, and a number is the
only form of that meeting that survives a change of graphics API.

### 4.6 Filters

A filter is a reversible rearrangement of the payload bytes applied before
Deflate and reversed after inflate. It changes no bit of the resource; it
changes the order the bits are in, so that Deflate sees runs instead of
interleaving. Two are defined.

**Shuffle** (filter `1`). The payload is treated as an array of records of
`element size` bytes. The output is every record's first byte, then every
record's second byte, and so on. A vertex that is eight floats becomes eight
planes of four-byte values, and each plane compresses on its own.

**Delta then shuffle** (filter `2`). Each 32-bit word is replaced by its
difference from the same word of the previous record, and the result is
shuffled. This only helps when consecutive records are numerically close,
which is a property of how the buffer was produced and not of the buffer
itself.

Measured on payloads shaped like the ones this format carries, Deflate at
level 9, raw streams, sizes in bytes and as a fraction of the payload:

| payload | deflate | shuffle | delta+shuffle |
| --- | --- | --- | --- |
| index buffer, triangulated grid, 4.78 MB | 1,101,749 (23.1%) | 26,735 (0.6%) | 7,509 (0.2%) |
| vertex buffer, grid order, 6.40 MB | 4,073,233 (63.6%) | 1,959,907 (30.6%) | 1,455,067 (22.7%) |
| vertex buffer, permuted order, 6.40 MB | 4,824,534 (75.4%) | 4,586,279 (71.7%) | 4,767,878 (74.5%) |

Three readings, and the third is why the filter is recorded per entry rather
than chosen globally.

The index buffer is the large win: 41 times smaller than Deflate alone after a
shuffle, because its four-byte indices share their high bytes and differ in
their low ones. Deflate cannot see that in the interleaved form.

Shuffle is modest on 32-bit float data, and the reason is worth stating: the
low bytes of a float's mantissa are close to random, and they are a third of
every word. Transposing groups the bytes that are similar, but the random ones
are still there. A build that quantises its vertex format first gets much more,
and that is a property of the mesh format, not of this container: the element
size field means a quantised record filters exactly as well as a wide one.

Delta depends on the order of the data, and the third row is the case that
decides the design. The same 6.40 MB of vertices in permuted order compress
worse under delta than under a plain shuffle, because there is nothing for the
delta to subtract. A writer cannot tell the two cases apart without knowing how
the buffer was produced, so delta is never chosen automatically: it is an
opt-in the build sets per entry, and the build is the only party that knows.

The filter is part of the method, and the method is part of the entry. A
container may therefore hold filtered and unfiltered entries side by side,
which is what a build produces when it knows the shape of some resources and
not of others.

Filter `0` is no filter, and `element size` is `0` with it. An entry whose
filter is `0` and whose flags say it is deflated is the plain case this
specification described before this section existed, so a reader that
implements only that case reads any container whose entries do not use a
filter.

## 5. Appending and compaction

Appending an entry writes its payload after the current payload region, writes
a new index after that, and patches the header's entry count, index offset,
index size, index checksum and header checksum in place. The patch touches 28
bytes at a known offset, which is why the header carries its own checksum: a
reader can tell a half-written patch from a complete one.

The previous index is not removed, so it becomes unreachable space. This is the
honest cost of an appendable index and it is bounded: it is proportional to the
number of append operations, not to the number of resources. A `compact`
operation rewrites the container with one index and no dead space, and is the
answer to repeated appends. Nothing in the format requires compaction to be
frequent, and a build that writes the container once never needs it at all.

## 6. Overlay resolution

A loader holds an ordered list of providers. Later providers override earlier
ones. The list, from lowest to highest priority, is: containers in the order
the application opened them, then a loose directory if one is configured. A
loose file therefore overrides a packed one without the container being
touched, which is requirement 7 and the whole reason the ordering is defined
rather than implied.

Resolving a name means walking the list from the highest priority down and
taking the first provider that has it. The result is a handle that names both
the resource and the provider it came from, so a later load of the same name
does not silently switch providers.

A duplicate name **within one container** is a build error and not an override.
Requirement 1 says a name maps to exactly one resource; two entries with the
same name in one container is a broken container, and the writer refuses to
produce it.

## 7. Integrity

Two checks exist and they cover different things. The header checksum covers
the 28 bytes that tell a reader where everything is, so a truncated or half-
patched file is rejected before any payload is touched. The per-entry checksum
covers the inflated payload, so corruption is attributed to an entry.

Neither is a signature and neither is a security control. They detect accident,
not tampering. The format has no signing, no encryption and no key material, by
design: it is a runtime container for content the application already ships,
and adding a security layer to it would suggest a threat model this project
does not have.

## 8. Determinism

Two runs of the writer with the same inputs and the same writer version produce
byte-identical output. This is required of our own tool and is not required of
any other tool's output.

Determinism is achieved by making the index canonical: records are sorted by
name in byte order, padding bytes are a fixed value, and no field depends on
hash iteration order or on a timestamp. The payload bytes are whatever the
configured compressor produces at the configured level, so the compressor and
its version are part of the build's reproducibility contract and are recorded
alongside it. The filter choice is part of the same contract: it is a build
decision recorded in the entry, not a property the writer infers, so two runs
with the same inputs and the same configuration produce the same filter and the
same bytes.

Byte-identity with the output of a different tool is explicitly **not** a goal.
Two conforming writers may produce different but equally valid containers.

## 9. Deliberately not specified

The following are absent, and their absence is a decision rather than an
oversight. Each is a property that has been attributed to other containers at
some point, sometimes without evidence, and none of them is a requirement this
project has:

- **Encryption.** See section 7. No threat model.
- **Per-entry identifiers other than the name.** A GUID would be a second
  identity for the same resource, and requirement 1 asks for one.
- **A per-entry compression-quality flag or "light encryption" bit.**
  Compression level is a build setting, not a resource property.
- **A global directory at a fixed offset.** See section 4.1.
- **A per-entry compression level.** The level is a build setting. It is not
  recorded because it is not needed to read the payload back, and a reader that
  wanted it would be measuring the writer's effort rather than the resource.
- **A filter that is chosen by the writer.** Filtering is recorded, never
  inferred. A writer that guessed would be wrong on exactly the payloads where
  the choice matters, as the permuted case above shows.
- **A self-extracting or executable form.** The container is read by the
  engine, not by the operating system.

## 10. Boundaries

**Graphics APIs.** Nothing in this document names OpenGL, Vulkan, Metal or
OpenGL ES, and nothing in the encoding depends on which one is in use. The
container stores bytes, offsets, kinds and alignment exponents. A backend
change does not touch it.

**Host platforms.** The encoding is byte-order fixed, offset-based and free of
platform-specific alignment, so the same file is read on every target. A
resource authored on one platform is not re-encoded for another.

**The two projects.** This format belongs to OpenGrace. A separate project that
reads a shipped game's archives has its own tool and its own format, and the
two share nothing but the fact that both may use Deflate as a payload codec.
Sharing a codec is not sharing a design, and no layout here was derived from,
or is intended to be compatible with, any other container.
