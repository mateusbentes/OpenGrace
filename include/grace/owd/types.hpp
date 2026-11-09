#pragma once

// The OWD container format constants and the shared value types.
//
// OWD is Open World Data, this project's resource container
// (docs/resource-container.md). Nothing in it names a graphics API or a host
// platform; every integer that crosses a file boundary is little-endian and
// unsigned unless the specification says otherwise. This header carries no
// type whose size depends on the compiler beyond std::uintN_t.

#include <cstdint>
#include <string>
#include <vector>

namespace grace {
namespace owd {

/// Magic bytes at offset 0: 'O', 'W', 'D', NUL.
inline constexpr std::uint8_t kMagic[4] = {'O', 'W', 'D', '\0'};

/// Format version, recorded by the writer and accepted by readers of the
/// same major with a known minor. The format lives in
/// docs/resource-container.md; the bytes it fixes are version 1.0.
inline constexpr std::uint8_t kVersionMajor = 1;
inline constexpr std::uint8_t kVersionMinor = 0;

/// The 36-byte header, section 4.2.
inline constexpr std::size_t kHeaderSize = 36;

/// The index block size, section 4.7: 256 records per block.
inline constexpr std::size_t kIndexRecordsPerBlock = 256;

/// The payload block size, section 4.8: 256 KiB of uncompressed output,
/// reduced to the largest multiple of the element size that fits.
inline constexpr std::size_t kPayloadBlockSize = 256 * 1024;

/// The entry flags, section 4.3: bit 0 payload is deflated, bit 1 has
/// dependencies, bits 2-3 the filter (0 none, 1 shuffle, 2 delta+shuffle).
inline constexpr std::uint16_t kFlagDeflated = 1 << 0;
inline constexpr std::uint16_t kFlagHasDeps = 1 << 1;
inline constexpr std::uint16_t kFlagFilterMask = 0x3 << 2;
inline constexpr std::uint16_t kFlagFilterShift = 2;

/// One entry of the container, as data: the record's fields, not the bytes.
/// The writer and the reader both speak this shape; the wire format is
/// theirs to encode and decode.
struct EntryDesc
{
    std::string name;
    std::uint16_t kind = 0;
    std::uint16_t flags = 0;
    std::uint8_t elementSize = 0;
    std::uint8_t alignmentExponent = 0;
    std::uint64_t payloadOffset = 0;
    std::uint64_t payloadSizeOnDisk = 0;
    std::uint64_t payloadSizeInflated = 0;
    std::uint32_t payloadChecksum = 0;
    std::vector<std::string> dependencies;
};

/// The payload content of an entry: the inflated, unfiltered bytes. Read
/// whole, the reader's cost is the entry, not the container.
struct Payload
{
    std::vector<std::uint8_t> bytes;
};

}  // namespace owd
}  // namespace grace
