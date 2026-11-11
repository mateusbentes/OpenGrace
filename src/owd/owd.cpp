// The OWD container: the writer and the reader.
//
// The writer is deterministic (section 8): records sorted by name in byte
// order, padding bytes a fixed value, and no field that depends on a
// timestamp or on the writer's visit order. The reader is the reference
// read half: the header, the index block table and the index blocks, the
// payload block table and the payload blocks, the filters reversed per
// block, and the per-entry checksum verified before the bytes are handed
// out. The spec is docs/resource-container.md; section numbers in the
// comments below are its sections.

#include "grace/owd/owd.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <map>
#include <utility>

#include "grace/owd/internal.hpp"

namespace grace {
namespace owd {

namespace {

// The index block table is 12 bytes per block: 8 offset, 4 on-disk size.
constexpr std::size_t kBlockTableEntry = 12;

void put16(std::uint8_t* out, std::uint16_t value)
{
    out[0] = static_cast<std::uint8_t>(value & 0xFFu);
    out[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
}

void put32(std::uint8_t* out, std::uint32_t value)
{
    out[0] = static_cast<std::uint8_t>(value & 0xFFu);
    out[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    out[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    out[3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

void put64(std::uint8_t* out, std::uint64_t value)
{
    for (std::size_t i = 0; i < 8; ++i)
    {
        out[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
    }
}

std::uint16_t get16(const std::uint8_t* in)
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(in[0]) |
        (static_cast<std::uint16_t>(in[1]) << 8));
}

std::uint32_t get32(const std::uint8_t* in)
{
    return static_cast<std::uint32_t>(in[0]) |
           (static_cast<std::uint32_t>(in[1]) << 8) |
           (static_cast<std::uint32_t>(in[2]) << 16) |
           (static_cast<std::uint32_t>(in[3]) << 24);
}

std::uint64_t get64(const std::uint8_t* in)
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i)
    {
        value |= static_cast<std::uint64_t>(in[i]) << (8 * i);
    }
    return value;
}

// The writer's padded payload region offset: the payload offset is a
// multiple of 2^exponent when the exponent is nonzero, and the padding
// bytes are the fixed value 0x00 (section 4.5).
std::uint64_t alignUp(std::uint64_t offset, std::uint8_t exponent)
{
    if (exponent == 0)
    {
        return offset;
    }
    const std::uint64_t alignment = static_cast<std::uint64_t>(1) << exponent;
    const std::uint64_t remainder = offset % alignment;
    return remainder == 0 ? offset : offset + alignment - remainder;
}

// The filter's encoder by the flags' filter field. Filter 0 is the
// identity, and the bytes past the last whole element are copied either
// way.
std::vector<std::uint8_t> encode(const std::uint8_t* data, std::size_t size,
                                 std::uint16_t flags,
                                 std::uint8_t elementSize)
{
    const std::uint16_t filter =
        static_cast<std::uint16_t>((flags & kFlagFilterMask) >>
                                   kFlagFilterShift);
    if (filter == 1)
    {
        return shuffle(data, size, elementSize);
    }
    if (filter == 2)
    {
        return deltaShuffle(data, size, elementSize);
    }
    return std::vector<std::uint8_t>(data, data + size);
}

std::vector<std::uint8_t> decode(const std::uint8_t* data, std::size_t size,
                                 std::uint16_t flags,
                                 std::uint8_t elementSize)
{
    const std::uint16_t filter =
        static_cast<std::uint16_t>((flags & kFlagFilterMask) >>
                                   kFlagFilterShift);
    if (filter == 1)
    {
        return unshuffle(data, size, elementSize);
    }
    if (filter == 2)
    {
        return deltaUnshuffle(data, size, elementSize);
    }
    return std::vector<std::uint8_t>(data, data + size);
}

}  // namespace

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

WriterError::WriterError(const std::string& message)
    : std::runtime_error(message)
{
}

struct Writer::Impl
{
    std::vector<PackDesc> entries;
};

Writer::Writer()
    : m_impl(new Impl())
{
}

Writer::~Writer()
{
    delete m_impl;
}

void Writer::add(const PackDesc& desc)
{
    if (desc.name.empty())
    {
        throw WriterError("owd: the entry name is empty");
    }
    if (desc.name.size() > 255)
    {
        throw WriterError("owd: the entry name is longer than 255 bytes");
    }
    for (const PackDesc& existing : m_impl->entries)
    {
        if (existing.name == desc.name)
        {
            throw WriterError("owd: duplicate entry name '" + desc.name +
                              "'");
        }
    }
    const std::uint16_t filter =
        static_cast<std::uint16_t>((desc.flags & kFlagFilterMask) >>
                                   kFlagFilterShift);
    if (filter != 0 && desc.elementSize == 0)
    {
        throw WriterError("owd: the entry '" + desc.name +
                          "' carries a filter with no element size");
    }
    if (filter != 0 && (desc.flags & kFlagDeflated) == 0)
    {
        throw WriterError("owd: the entry '" + desc.name +
                          "' carries a filter on a stored payload");
    }
    if (desc.dependencies.size() > 255)
    {
        throw WriterError("owd: the entry '" + desc.name +
                          "' carries more than 255 dependencies");
    }
    for (const std::string& dependency : desc.dependencies)
    {
        if (dependency.empty())
        {
            throw WriterError("owd: the entry '" + desc.name +
                              "' carries an empty dependency name");
        }
    }
    m_impl->entries.push_back(desc);
}

std::size_t Writer::count() const
{
    return m_impl->entries.size();
}

std::vector<std::uint8_t> Writer::finish() const
{
    if (m_impl->entries.empty())
    {
        throw WriterError("owd: the container has no entries");
    }

    // The index is canonical: records sorted by name in byte order
    // (section 8). The writer sorts the entries it was given, so the
    // caller's order is irrelevant.
    std::vector<const PackDesc*> sorted;
    sorted.reserve(m_impl->entries.size());
    for (const PackDesc& desc : m_impl->entries)
    {
        sorted.push_back(&desc);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const PackDesc* left, const PackDesc* right) {
                  return left->name < right->name;
              });

    // Serialise the index records once (section 4.3): 38 fixed bytes, then
    // the name and the dependency names. The payload fields are patched in
    // after the payload region is laid out, because an offset is not known
    // until the region that holds it is.
    std::vector<std::uint8_t> indexRecords;
    for (const PackDesc* desc : sorted)
    {
        std::size_t recordSize = 38 + desc->name.size();
        for (const std::string& dependency : desc->dependencies)
        {
            recordSize += 1 + dependency.size();
        }
        const std::size_t recordStart = indexRecords.size();
        indexRecords.resize(recordStart + recordSize);
        std::uint8_t* record = indexRecords.data() + recordStart;
        put16(record, static_cast<std::uint16_t>(recordSize));
        put16(record + 2, desc->flags);
        put64(record + 4, 0);
        put64(record + 12, 0);
        put64(record + 20, desc->payload.size());
        put32(record + 28, crc32(desc->payload.data(), desc->payload.size()));
        record[32] = desc->elementSize;
        record[33] = desc->alignmentExponent;
        put16(record + 34, desc->kind);
        record[36] = static_cast<std::uint8_t>(desc->name.size());
        record[37] = static_cast<std::uint8_t>(desc->dependencies.size());
        std::memcpy(record + 38, desc->name.data(), desc->name.size());
        std::size_t cursor = 38 + desc->name.size();
        for (const std::string& dependency : desc->dependencies)
        {
            record[cursor] = static_cast<std::uint8_t>(dependency.size());
            std::memcpy(record + cursor + 1, dependency.data(),
                        dependency.size());
            cursor += 1 + dependency.size();
        }
    }

    // The payload region: each deflated entry is split into blocks of
    // blockPayload inflated bytes (section 4.8), the blocks are deflated
    // independently, and a payload of more than one block carries a
    // 12-byte-per-block table at the head of its on-disk region. The block
    // offsets are absolute, so the table can be written before the blocks
    // it locates: kHeaderSize + base + tableSize is the first block's
    // offset, and the rest follow it.
    std::vector<std::uint8_t> payloadRegion;
    std::vector<std::uint64_t> payloadOffsets;
    std::vector<std::uint64_t> payloadDiskSizes;
    for (const PackDesc* desc : sorted)
    {
        const std::uint64_t base =
            alignUp(payloadRegion.size(), desc->alignmentExponent);
        while (payloadRegion.size() < base)
        {
            payloadRegion.push_back(0x00);
        }
        payloadOffsets.push_back(base);
        std::vector<std::uint8_t> onDisk;
        std::size_t blockPayload = kPayloadBlockSize;
        if ((desc->flags & kFlagDeflated) != 0 && desc->elementSize != 0)
        {
            blockPayload = kPayloadBlockSize - (kPayloadBlockSize %
                                                desc->elementSize);
        }
        if ((desc->flags & kFlagDeflated) != 0)
        {
            std::vector<std::uint32_t> blockSizes;
            std::vector<std::uint8_t> blocks;
            for (std::size_t offset = 0; offset < desc->payload.size();
                 offset += blockPayload)
            {
                const std::size_t chunk =
                    std::min(blockPayload, desc->payload.size() - offset);
                const std::vector<std::uint8_t> filtered =
                    encode(desc->payload.data() + offset, chunk,
                           desc->flags, desc->elementSize);
                const std::vector<std::uint8_t> block =
                    deflate(filtered.data(), filtered.size());
                blockSizes.push_back(
                    static_cast<std::uint32_t>(block.size()));
                blocks.insert(blocks.end(), block.begin(), block.end());
            }
            if (blockSizes.size() > 1)
            {
                const std::size_t tableSize =
                    blockSizes.size() * kBlockTableEntry;
                std::vector<std::uint8_t> tableBytes(tableSize);
                std::uint64_t blockOffset = kHeaderSize + base + tableSize;
                for (std::size_t i = 0; i < blockSizes.size(); ++i)
                {
                    std::uint8_t* entry =
                        tableBytes.data() + i * kBlockTableEntry;
                    put64(entry, blockOffset);
                    put32(entry + 8, blockSizes[i]);
                    blockOffset += blockSizes[i];
                }
                onDisk.insert(onDisk.end(), tableBytes.begin(),
                              tableBytes.end());
                onDisk.insert(onDisk.end(), blocks.begin(), blocks.end());
            }
            else
            {
                onDisk.swap(blocks);
            }
        }
        else
        {
            onDisk.insert(onDisk.end(), desc->payload.begin(),
                          desc->payload.end());
        }
        payloadDiskSizes.push_back(onDisk.size());
        payloadRegion.insert(payloadRegion.end(), onDisk.begin(),
                             onDisk.end());
    }

    // Patch the index records' payload fields: the payload offset is
    // absolute, and the disk size is the blocks and not the table
    // (section 4.3).
    std::size_t recordCursor = 0;
    for (std::size_t i = 0; i < sorted.size(); ++i)
    {
        std::uint8_t* record = indexRecords.data() + recordCursor;
        put64(record + 4, kHeaderSize + payloadOffsets[i]);
        put64(record + 12, payloadDiskSizes[i]);
        recordCursor += get16(record);
    }

    // The index region: the block table, then the blocks (section 4.7).
    // The blocks are deflated independently, so a lookup inflates one
    // block and not the index. A record never spans two blocks, so the
    // records are walked by their self-delimiting length.
    std::vector<std::uint32_t> indexBlockSizes;
    std::vector<std::uint8_t> indexBlocks;
    std::size_t cursor = 0;
    while (cursor < indexRecords.size())
    {
        const std::size_t blockStart = cursor;
        for (std::size_t count = 0; count < kIndexRecordsPerBlock &&
             cursor < indexRecords.size(); ++count)
        {
            cursor += get16(indexRecords.data() + cursor);
        }
        const std::vector<std::uint8_t> block =
            deflate(indexRecords.data() + blockStart, cursor - blockStart);
        indexBlockSizes.push_back(
            static_cast<std::uint32_t>(block.size()));
        indexBlocks.insert(indexBlocks.end(), block.begin(), block.end());
    }

    // The header is at a fixed offset (section 4.1), the payload region
    // follows it, and the index follows the payload region: its block
    // table is at the head of the index region, and the blocks follow the
    // table.
    const std::uint64_t indexOffset =
        kHeaderSize + payloadRegion.size();
    const std::size_t tableSize =
        indexBlockSizes.size() * kBlockTableEntry;
    std::vector<std::uint8_t> indexTable(tableSize);
    std::uint64_t blockOffset = indexOffset + tableSize;
    for (std::size_t i = 0; i < indexBlockSizes.size(); ++i)
    {
        std::uint8_t* entry = indexTable.data() + i * kBlockTableEntry;
        put64(entry, blockOffset);
        put32(entry + 8, indexBlockSizes[i]);
        blockOffset += indexBlockSizes[i];
    }

    std::vector<std::uint8_t> out;
    out.resize(kHeaderSize + payloadRegion.size() + tableSize +
               indexBlocks.size());
    std::memcpy(out.data() + kHeaderSize, payloadRegion.data(),
                payloadRegion.size());
    std::memcpy(out.data() + kHeaderSize + payloadRegion.size(),
                indexTable.data(), indexTable.size());
    std::memcpy(out.data() + kHeaderSize + payloadRegion.size() +
                    indexTable.size(),
                indexBlocks.data(), indexBlocks.size());

    // The header (section 4.2): magic, versions, flags, entry count, block
    // count, index offset, index size, index checksum, header checksum.
    std::uint8_t* header = out.data();
    std::memcpy(header, kMagic, 4);
    header[4] = static_cast<std::uint8_t>(kVersionMajor & 0xFFu);
    header[5] = static_cast<std::uint8_t>(kVersionMinor & 0xFFu);
    put16(header + 6, 0);
    put32(header + 8, static_cast<std::uint32_t>(sorted.size()));
    put32(header + 12, static_cast<std::uint32_t>(indexBlockSizes.size()));
    put64(header + 16, indexOffset);
    put32(header + 24,
          static_cast<std::uint32_t>(tableSize + indexBlocks.size()));
    const std::uint32_t indexChecksum = crc32(out.data() + indexOffset,
                                              tableSize + indexBlocks.size());
    put32(header + 28, indexChecksum);
    const std::uint32_t headerChecksum = crc32(header, 32);
    put32(header + 32, headerChecksum);
    return out;
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

ReaderError::ReaderError(const std::string& message)
    : std::runtime_error(message)
{
}

struct Reader::Impl
{
    std::map<std::string, EntryDesc> entries;
    std::vector<EntryDesc> order;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

namespace {

// The index block table and the index blocks are read together, so the
// record's offset is known before the block is inflated.
void readIndex(const std::uint8_t* data, std::size_t size,
               std::uint64_t indexOffset, std::uint32_t indexSize,
               std::uint32_t indexChecksum, std::size_t blockCount,
               std::vector<EntryDesc>& out)
{
    if (indexOffset + indexSize > size)
    {
        throw ReaderError("owd: the index region is out of bounds");
    }
    const std::uint32_t checksum = crc32(data + indexOffset, indexSize);
    if (checksum != indexChecksum)
    {
        throw ReaderError("owd: the index checksum does not match");
    }
    // The block table is at the head of the index region, and the block
    // count is in the header (section 4.2): the table is one 12-byte entry
    // per block, and the blocks follow it. The table's size is not stored,
    // so the reader derives it from the block count.
    // The records are parsed from the blocks until the block's inflated
    // size is consumed, and a record that would run past the block's end
    // is corruption.
    for (std::size_t block = 0; block < blockCount; ++block)
    {
        if (indexOffset + block * kBlockTableEntry + kBlockTableEntry > size)
        {
            throw ReaderError("owd: the index block table is out of bounds");
        }
        const std::uint8_t* tableEntry =
            data + indexOffset + block * kBlockTableEntry;
        const std::uint64_t blockOffset = get64(tableEntry);
        const std::uint32_t blockDiskSize = get32(tableEntry + 8);
        std::vector<std::uint8_t> inflated;
        if (!inflate(data + blockOffset, blockDiskSize, inflated))
        {
            throw ReaderError("owd: an index block does not inflate");
        }
        std::size_t cursor = 0;
        while (cursor < inflated.size())
        {
            if (cursor + 2 > inflated.size())
            {
                throw ReaderError("owd: a record length is out of bounds");
            }
            const std::uint16_t length = get16(inflated.data() + cursor);
            if (length < 38 || cursor + length > inflated.size())
            {
                throw ReaderError("owd: a record length is out of bounds");
            }
            const std::uint8_t* record = inflated.data() + cursor;
            EntryDesc desc;
            desc.flags = get16(record + 2);
            desc.payloadOffset = get64(record + 4);
            const std::uint64_t diskSize = get64(record + 12);
            desc.payloadSizeOnDisk = diskSize;
            desc.payloadSizeInflated = get64(record + 20);
            desc.payloadChecksum = get32(record + 28);
            desc.elementSize = record[32];
            desc.alignmentExponent = record[33];
            desc.kind = get16(record + 34);
            const std::uint8_t nameLength = record[36];
            const std::uint8_t dependencyCount = record[37];
            if (38 + nameLength > length)
            {
                throw ReaderError("owd: a record name is out of bounds");
            }
            desc.name.assign(reinterpret_cast<const char*>(record + 38),
                             nameLength);
            std::size_t depsCursor = 38 + nameLength;
            for (std::size_t i = 0; i < dependencyCount; ++i)
            {
                if (depsCursor + 1 > length)
                {
                    throw ReaderError(
                    "owd: a dependency name is out of bounds");
                }
                const std::uint8_t dependencyLength = record[depsCursor];
                if (depsCursor + 1 + dependencyLength > length)
                {
                    throw ReaderError(
                        "owd: a dependency name is out of bounds");
                }
                desc.dependencies.push_back(
                    std::string(reinterpret_cast<const char*>(
                                    record + depsCursor + 1),
                                dependencyLength));
                depsCursor += 1 + dependencyLength;
            }
            (void)diskSize;
            out.push_back(desc);
            cursor += length;
        }
    }
}

}  // namespace

Reader::Reader(const std::uint8_t* data, std::size_t size)
    : m_impl(new Impl())
{
    if (size < kHeaderSize)
    {
        throw ReaderError("owd: the container is smaller than the header");
    }
    if (std::memcmp(data, kMagic, 4) != 0)
    {
        throw ReaderError("owd: the magic does not match");
    }
    const std::uint16_t major = data[4];
    const std::uint16_t minor = data[5];
    if (major > kVersionMajor)
    {
        throw ReaderError("owd: the major version is unknown");
    }
    (void)minor;
    const std::uint32_t headerChecksum = get32(data + 32);
    if (crc32(data, 32) != headerChecksum)
    {
        throw ReaderError("owd: the header checksum does not match");
    }
    const std::uint32_t entryCount = get32(data + 8);
    const std::uint32_t blockCount = get32(data + 12);
    const std::uint64_t indexOffset = get64(data + 16);
    const std::uint32_t indexSize = get32(data + 24);
    const std::uint32_t indexChecksum = get32(data + 28);
    m_impl->data = data;
    m_impl->size = size;
    readIndex(data, size, indexOffset, indexSize, indexChecksum,
              blockCount, m_impl->order);
    if (m_impl->order.size() != entryCount)
    {
        throw ReaderError("owd: the entry count does not match the index");
    }
    for (const EntryDesc& desc : m_impl->order)
    {
        m_impl->entries[desc.name] = desc;
    }
}

Reader::Reader() = default;

Reader::~Reader() = default;

std::vector<EntryDesc> Reader::entries() const
{
    return m_impl->order;
}

const EntryDesc& Reader::entry(const std::string& name) const
{
    const auto it = m_impl->entries.find(name);
    if (it == m_impl->entries.end())
    {
        throw ReaderError("owd: no entry named '" + name + "'");
    }
    return it->second;
}

bool Reader::has(const std::string& name) const
{
    return m_impl->entries.count(name) != 0;
}

Payload Reader::read(const std::string& name) const
{
    const EntryDesc& desc = entry(name);
    if (desc.payloadOffset + desc.payloadSizeOnDisk > m_impl->size)
    {
        throw ReaderError("owd: the payload of '" + name +
                          "' is out of bounds");
    }
    std::vector<std::uint8_t> inflated;
    if ((desc.flags & kFlagDeflated) != 0)
    {
        const std::uint8_t* source = m_impl->data + desc.payloadOffset;
        const std::size_t blockPayload =
            desc.elementSize == 0
                ? kPayloadBlockSize
                : kPayloadBlockSize - (kPayloadBlockSize % desc.elementSize);
        if (desc.payloadSizeInflated > blockPayload)
        {
            // The payload block table precedes the blocks (section 4.8).
            const std::size_t blockCount =
                (desc.payloadSizeInflated + blockPayload - 1) /
                blockPayload;
            const std::size_t tableSize = blockCount * kBlockTableEntry;
            const std::uint8_t* table = m_impl->data + desc.payloadOffset;
            const std::uint8_t* blocks = table + tableSize;
            inflated.reserve(desc.payloadSizeInflated);
            for (std::size_t i = 0; i < blockCount; ++i)
            {
                const std::uint64_t blockOffset =
                get64(table + i * kBlockTableEntry);
                const std::uint32_t blockDiskSize =
                    get32(table + i * kBlockTableEntry + 8);
                if (blockOffset + blockDiskSize > m_impl->size)
                {
                    throw ReaderError("owd: a payload block is out of bounds");
                }
                std::vector<std::uint8_t> block;
                if (!inflate(m_impl->data + blockOffset, blockDiskSize,
                             block))
                {
                    throw ReaderError("owd: a payload block does not inflate");
                }
                const std::vector<std::uint8_t> unfiltered =
                    decode(block.data(), block.size(), desc.flags,
                           desc.elementSize);
                inflated.insert(inflated.end(), unfiltered.begin(),
                                unfiltered.end());
            }
            (void)blocks;
        }
        else
        {
            std::vector<std::uint8_t> block;
            if (!inflate(source, desc.payloadSizeOnDisk, block))
            {
                throw ReaderError("owd: the payload of '" + name +
                                  "' does not inflate");
            }
            inflated = decode(block.data(), block.size(), desc.flags,
                              desc.elementSize);
        }
    }
    else
    {
        inflated.assign(m_impl->data + desc.payloadOffset,
                        m_impl->data + desc.payloadOffset +
                            desc.payloadSizeOnDisk);
    }
    if (inflated.size() != desc.payloadSizeInflated)
    {
        throw ReaderError("owd: the payload of '" + name +
                          "' does not inflate to its recorded size");
    }
    const std::uint32_t checksum =
        crc32(inflated.data(), inflated.size());
    if (checksum != desc.payloadChecksum)
    {
        throw ReaderError("owd: the payload of '" + name +
                          "' does not match its checksum");
    }
    Payload payload;
    payload.bytes = std::move(inflated);
    return payload;
}

std::vector<std::uint8_t> Reader::readRange(const std::string& name,
                                            std::uint64_t offset,
                                            std::uint64_t size) const
{
    const EntryDesc& desc = entry(name);
    if (offset + size > desc.payloadSizeInflated)
    {
        throw ReaderError("owd: the range of '" + name +
                          "' is out of bounds");
    }
    std::vector<std::uint8_t> out;
    if ((desc.flags & kFlagDeflated) == 0)
    {
        out.assign(m_impl->data + desc.payloadOffset + offset,
                   m_impl->data + desc.payloadOffset + offset + size);
        return out;
    }
    // The blocks that cover the range: the block size is derived from
    // the record, so the read cost does not grow with the payload.
    const std::size_t blockPayload =
        desc.elementSize == 0
            ? kPayloadBlockSize
            : kPayloadBlockSize - (kPayloadBlockSize % desc.elementSize);
    const std::uint8_t* table = m_impl->data + desc.payloadOffset;
    const std::size_t first = static_cast<std::size_t>(offset / blockPayload);
    const std::size_t last =
        static_cast<std::size_t>((offset + size - 1) / blockPayload);
    if (desc.payloadSizeInflated > blockPayload)
    {
        for (std::size_t i = first; i <= last; ++i)
        {
            const std::uint64_t blockOffset =
                get64(table + i * kBlockTableEntry);
            const std::uint32_t blockDiskSize =
                get32(table + i * kBlockTableEntry + 8);
            if (blockOffset + blockDiskSize > m_impl->size)
            {
                throw ReaderError("owd: a payload block is out of bounds");
            }
            std::vector<std::uint8_t> block;
            if (!inflate(m_impl->data + blockOffset, blockDiskSize, block))
            {
                throw ReaderError("owd: a payload block does not inflate");
            }
            const std::vector<std::uint8_t> unfiltered =
                decode(block.data(), block.size(), desc.flags,
                       desc.elementSize);
            const std::uint64_t blockStart =
                static_cast<std::uint64_t>(i) * blockPayload;
            const std::uint64_t blockEnd = blockStart + unfiltered.size();
            const std::uint64_t from = std::max(offset, blockStart);
            const std::uint64_t to = std::min(offset + size, blockEnd);
            out.insert(out.end(),
                       unfiltered.begin() +
                           static_cast<std::ptrdiff_t>(from - blockStart),
                       unfiltered.begin() +
                           static_cast<std::ptrdiff_t>(to - blockStart));
        }
    }
    else
    {
        std::vector<std::uint8_t> block;
        if (!inflate(m_impl->data + desc.payloadOffset,
                     desc.payloadSizeOnDisk, block))
        {
            throw ReaderError("owd: the payload of '" + name +
                              "' does not inflate");
        }
        const std::vector<std::uint8_t> unfiltered =
            decode(block.data(), block.size(), desc.flags,
                   desc.elementSize);
        out.assign(unfiltered.begin() + static_cast<std::ptrdiff_t>(offset),
                   unfiltered.begin() +
                       static_cast<std::ptrdiff_t>(offset + size));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Loader (section 6, overlay resolution)
// ---------------------------------------------------------------------------

void Loader::add(Reader* reader)
{
    m_readers.push_back(reader);
}

std::size_t Loader::count() const
{
    return m_readers.size();
}

Reader* Loader::find(const std::string& name) const
{
    for (auto it = m_readers.rbegin(); it != m_readers.rend(); ++it)
    {
        if ((*it)->has(name))
        {
            return *it;
        }
    }
    return nullptr;
}

}  // namespace owd
}  // namespace grace
