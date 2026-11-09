#pragma once

// The OWD codec: the internal CRC-32 and Deflate helpers the writer and the
// reader share. This header is not part of the public API: the contract's
// types are the EntryDesc and the Payload, and every byte layout is
// contained here (docs/resource-container.md).

#include <cstdint>
#include <string>
#include <vector>

#include "grace/owd/types.hpp"

namespace grace {
namespace owd {

/// CRC-32 (IEEE 802.3), the polynomial the header checksum and the
/// per-entry checksum use. The spec's requirement is a checksum per entry;
/// this table-driven implementation is the standard one.
std::uint32_t crc32(const std::uint8_t* data, std::size_t size);

/// CRC-32 over two disjoint ranges, so the checksum of a header whose
/// trailing bytes change is a second pass over them, not a copy.
std::uint32_t crc32(const std::uint8_t* data, std::size_t size,
                    std::uint32_t seed);

/// Deflate (RFC 1951) at level 9, raw stream, no wrapper. Returns the
/// compressed bytes. Measured on the payloads this format carries the
/// filter ahead of it matters more than the level beyond 9.
std::vector<std::uint8_t> deflate(const std::uint8_t* data, std::size_t size);

/// Inflate a raw Deflate stream. Returns false on any malformed input
/// rather than throwing, because a reader that throws inside a corrupt
/// container is worse than a reader that reports the corruption.
bool inflate(const std::uint8_t* data, std::size_t size,
             std::vector<std::uint8_t>& out);

/// The byte transpose filter (filter 1), section 4.6. The payload is treated
/// as records of elementSize bytes; the output is every record's first byte,
/// then every record's second byte, and so on. Bytes past the last whole
/// record are copied unchanged.
std::vector<std::uint8_t> shuffle(const std::uint8_t* data, std::size_t size,
                                  std::uint8_t elementSize);

/// The inverse of the shuffle filter. The element-size tail that the
/// forward filter copied is copied back unchanged.
std::vector<std::uint8_t> unshuffle(const std::uint8_t* data, std::size_t size,
                                    std::uint8_t elementSize);

/// The delta then shuffle filter (filter 2), section 4.6. Each 32-bit word
/// of each record is replaced by its difference from the same word of the
/// previous record; the result is shuffled as above. The first record is
/// its own delta, and the tail is the same as the shuffle's.
std::vector<std::uint8_t> deltaShuffle(const std::uint8_t* data,
                                       std::size_t size,
                                       std::uint8_t elementSize);

/// The inverse of deltaShuffle: unshuffle first, then accumulate.
std::vector<std::uint8_t> deltaUnshuffle(const std::uint8_t* data,
                                         std::size_t size,
                                         std::uint8_t elementSize);

/// The filter's encoder, by the filter field in the flags and the record's
/// element size. filter 0 is the identity, and the bytes past the last
/// whole element are copied either way; the function is the only branch the
/// writer needs.
std::vector<std::uint8_t> applyFilter(const std::uint8_t* data,
                                      std::size_t size,
                                      std::uint16_t flags,
                                      std::uint8_t elementSize);

/// The filter's decoder, by the same fields. filter 0 is the identity.
std::vector<std::uint8_t> reverseFilter(const std::uint8_t* data,
                                        std::size_t size,
                                        std::uint16_t flags,
                                        std::uint8_t elementSize);

}  // namespace owd
}  // namespace grace
