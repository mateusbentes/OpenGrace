// The OWD codec: CRC-32, raw Deflate through zlib, and the two filters.
// The spec is docs/resource-container.md; the filter shapes are section
// 4.6 and the per-block application is section 4.6.1.

#include "grace/owd/internal.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <stdexcept>

#include <zlib.h>

namespace grace {
namespace owd {

namespace {

// CRC-32 (IEEE 802.3), the polynomial 0xEDB88320, table-driven. The
// checksums are computed over the inflated, unfiltered bytes (section 4.3),
// and the header checksum covers bytes 0 to 31 (section 4.2).
std::array<std::uint32_t, 256> buildCrcTable()
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i)
    {
        std::uint32_t crc = i;
        for (int bit = 0; bit < 8; ++bit)
        {
            const std::uint32_t mask =
                static_cast<std::uint32_t>(0u) - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
        table[i] = crc;
    }
    return table;
}

const std::array<std::uint32_t, 256> kCrcTable = buildCrcTable();

}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t size)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i)
    {
        const std::size_t index = (crc ^ data[i]) & 0xFFu;
        crc = (crc >> 8) ^ kCrcTable[index];
    }
    return ~crc;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size,
                    std::uint32_t seed)
{
    std::uint32_t crc = seed ^ 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i)
    {
        const std::size_t index = (crc ^ data[i]) & 0xFFu;
        crc = (crc >> 8) ^ kCrcTable[index];
    }
    return ~crc;
}

std::vector<std::uint8_t> deflate(const std::uint8_t* data, std::size_t size)
{
    // zlib's deflateInit2 with a negative window-bits value produces a raw
    // Deflate stream (RFC 1951), which is what the payload encoding is
    // (section 2). The container's framing is the format's own; the wrapper
    // zlib's framing is not inside it.
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    // deflateInit2 is a macro; the cast is to unsigned long, which is what
    // zlib's uLong is on every platform this project targets.
    if (deflateInit2(&stream, 9, Z_DEFLATED, -15, 9,
                     Z_DEFAULT_STRATEGY) != Z_OK)
    {
        throw std::runtime_error("owd: deflateInit2 failed");
    }
    stream.next_in = const_cast<Bytef*>(data);
    stream.avail_in = static_cast<uInt>(size);
    std::vector<std::uint8_t> out;
    out.reserve(size / 2);
    std::array<std::uint8_t, 64 * 1024> chunk{};
    int status = Z_OK;
    do
    {
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<uInt>(chunk.size());
        status = deflate(&stream, Z_FINISH);
        const std::size_t produced = chunk.size() - stream.avail_out;
        out.insert(out.end(), chunk.begin(), chunk.begin() + produced);
    }
    while (status == Z_OK);
    deflateEnd(&stream);
    if (status != Z_STREAM_END)
    {
        throw std::runtime_error("owd: deflate failed");
    }
    return out;
}

bool inflate(const std::uint8_t* data, std::size_t size,
             std::vector<std::uint8_t>& out)
{
    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, -15) != Z_OK)
    {
        return false;
    }
    stream.next_in = const_cast<Bytef*>(data);
    stream.avail_in = static_cast<uInt>(size);
    std::array<std::uint8_t, 64 * 1024> chunk{};
    int status = Z_OK;
    do
    {
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<uInt>(chunk.size());
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR)
        {
            inflateEnd(&stream);
            return false;
        }
        const std::size_t produced = chunk.size() - stream.avail_out;
        out.insert(out.end(), chunk.begin(), chunk.begin() + produced);
    }
    while (status != Z_STREAM_END && status != Z_BUF_ERROR);
    inflateEnd(&stream);
    return status == Z_STREAM_END;
}

std::vector<std::uint8_t> shuffle(const std::uint8_t* data, std::size_t size,
                                  std::uint8_t elementSize)
{
    if (elementSize == 0 || elementSize >= size)
    {
        return std::vector<std::uint8_t>(data, data + size);
    }
    const std::size_t records = size / elementSize;
    const std::size_t tail = size % elementSize;
    std::vector<std::uint8_t> out;
    out.reserve(size);
    for (std::size_t byte = 0; byte < elementSize; ++byte)
    {
        for (std::size_t record = 0; record < records; ++record)
        {
            out.push_back(data[record * elementSize + byte]);
        }
    }
    for (std::size_t i = 0; i < tail; ++i)
    {
        out.push_back(data[records * elementSize + i]);
    }
    return out;
}

std::vector<std::uint8_t> unshuffle(const std::uint8_t* data, std::size_t size,
                                    std::uint8_t elementSize)
{
    if (elementSize == 0 || elementSize >= size)
    {
        return std::vector<std::uint8_t>(data, data + size);
    }
    const std::size_t records = size / elementSize;
    const std::size_t tail = size % elementSize;
    std::vector<std::uint8_t> out(size);
    for (std::size_t byte = 0; byte < elementSize; ++byte)
    {
        for (std::size_t record = 0; record < records; ++record)
        {
            out[record * elementSize + byte] =
                data[byte * records + record];
        }
    }
    for (std::size_t i = 0; i < tail; ++i)
    {
        out[records * elementSize + i] = data[records * elementSize + i];
    }
    return out;
}

std::vector<std::uint8_t> deltaShuffle(const std::uint8_t* data,
                                       std::size_t size,
                                       std::uint8_t elementSize)
{
    if (elementSize == 0 || elementSize % 4 != 0 || elementSize >= size)
    {
        return std::vector<std::uint8_t>(data, data + size);
    }
    const std::size_t records = size / elementSize;
    const std::size_t words = elementSize / 4;
    const std::size_t tail = size % elementSize;
    std::vector<std::uint8_t> delta(size);
    for (std::size_t record = 0; record < records; ++record)
    {
        const std::uint8_t* recordBytes = data + record * elementSize;
        // The first record's delta is the record itself: the previous
        // record is zero.
        const std::uint8_t* previous =
            record == 0 ? nullptr : data + (record - 1) * elementSize;
        for (std::size_t word = 0; word < words; ++word)
        {
            const std::size_t offset = word * 4;
            for (std::size_t byte = 0; byte < 4; ++byte)
            {
                const std::uint32_t current =
                    static_cast<std::uint32_t>(recordBytes[offset + byte]);
                const std::uint32_t prior =
                    previous == nullptr
                        ? 0u
                        : static_cast<std::uint32_t>(previous[offset + byte]);
                // The delta is modular: the difference is stored as a
                // byte, and the unsigned subtraction wraps, which is the
                // arithmetic the filter is defined over.
                const std::uint32_t difference =
                    current - prior;
                delta[record * elementSize + offset + byte] =
                    static_cast<std::uint8_t>(difference & 0xFFu);
            }
        }
    }
    for (std::size_t i = 0; i < tail; ++i)
    {
        delta[records * elementSize + i] = data[records * elementSize + i];
    }
    return shuffle(delta.data(), size, elementSize);
}

std::vector<std::uint8_t> deltaUnshuffle(const std::uint8_t* data,
                                         std::size_t size,
                                         std::uint8_t elementSize)
{
    if (elementSize == 0 || elementSize % 4 != 0 || elementSize >= size)
    {
        return std::vector<std::uint8_t>(data, data + size);
    }
    const std::vector<std::uint8_t> shuffled =
        unshuffle(data, size, elementSize);
    const std::size_t records = size / elementSize;
    const std::size_t words = elementSize / 4;
    const std::size_t tail = size % elementSize;
    std::vector<std::uint8_t> out(size);
    for (std::size_t record = 0; record < records; ++record)
    {
        const std::uint8_t* recordBytes =
            shuffled.data() + record * elementSize;
        for (std::size_t word = 0; word < words; ++word)
        {
            const std::size_t offset = word * 4;
            for (std::size_t byte = 0; byte < 4; ++byte)
            {
                // The delta is modular: the inverse is the same arithmetic
                // modulo 256. The first record's delta is the record
                // itself, so the accumulator starts at zero.
                const std::uint32_t delta =
                    static_cast<std::uint32_t>(recordBytes[offset + byte]);
                const std::uint32_t prior =
                    record == 0
                        ? 0u
                        : static_cast<std::uint32_t>(
                              out[(record - 1) * elementSize + offset + byte]);
                out[record * elementSize + offset + byte] =
                    static_cast<std::uint8_t>((delta + prior) & 0xFFu);
            }
        }
    }
    for (std::size_t i = 0; i < tail; ++i)
    {
        out[records * elementSize + i] = shuffled[records * elementSize + i];
    }
    return out;
}

std::vector<std::uint8_t> applyFilter(const std::uint8_t* data,
                                      std::size_t size,
                                      std::uint16_t flags,
                                      std::uint8_t elementSize)
{
    const std::uint16_t filter =
        (flags & kFlagFilterMask) >> kFlagFilterShift;
    if (filter == 0)
    {
        return std::vector<std::uint8_t>(data, data + size);
    }
    if (filter == 1)
    {
        return shuffle(data, size, elementSize);
    }
    return deltaShuffle(data, size, elementSize);
}

std::vector<std::uint8_t> reverseFilter(const std::uint8_t* data,
                                        std::size_t size,
                                        std::uint16_t flags,
                                        std::uint8_t elementSize)
{
    const std::uint16_t filter =
        (flags & kFlagFilterMask) >> kFlagFilterShift;
    if (filter == 0)
    {
        return std::vector<std::uint8_t>(data, data + size);
    }
    if (filter == 1)
    {
        return unshuffle(data, size, elementSize);
    }
    return deltaUnshuffle(data, size, elementSize);
}

}  // namespace owd
}  // namespace grace
