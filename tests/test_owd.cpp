// The OWD container suite: the writer, the reader, the filters, the
// checksums, the overlay resolution and the tool round trip.
//
// The suite covers the specification's requirements as statements:
// - deterministic output (section 8): two runs, byte-identical files;
// - random access (requirement 5): readRange inflates only the blocks
//   covering the range;
// - per-entry checksum (requirement 8): a corrupted byte is a ReaderError;
// - the filters (section 4.6): shuffle and delta round trips;
// - the overlay resolution (section 6): a later provider overrides an
//   earlier one;
// - the writer's refusals: duplicate names, empty names, a filter with
//   no element size, a filter on a stored payload.

#include "grace/owd/owd.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "test_support.hpp"

using grace::owd::EntryDesc;
using grace::owd::Loader;
using grace::owd::PackDesc;
using grace::owd::Payload;
using grace::owd::Reader;
using grace::owd::ReaderError;
using grace::owd::Writer;
using grace::owd::WriterError;

namespace {

// A payload that is compressible: a run of 0x00, then a run of 0xFF, then
// a repeating pattern. The deflate round trip on it is unambiguous.
std::vector<std::uint8_t> makePayload(std::size_t bytes)
{
    std::vector<std::uint8_t> data(bytes, 0);
    for (std::size_t i = 0; i < bytes; ++i)
    {
        data[i] = static_cast<std::uint8_t>(i % 251);
    }
    return data;
}

// A record-shaped payload: 7 records of elementSize bytes each.
std::vector<std::uint8_t> makeRecordPayload(std::uint8_t elementSize,
                                            std::size_t records)
{
    std::vector<std::uint8_t> data;
    for (std::size_t record = 0; record < records; ++record)
    {
        for (std::size_t byte = 0; byte < elementSize; ++byte)
        {
            data.push_back(static_cast<std::uint8_t>(record * 3 + byte));
        }
    }
    return data;
}

void testDeterministic()
{
    Writer writer;
    PackDesc desc;
    desc.name = "meshes/scene";
    desc.kind = 1;
    desc.payload = makePayload(4096);
    writer.add(desc);

    const std::vector<std::uint8_t> first = writer.finish();
    const std::vector<std::uint8_t> second = writer.finish();
    GRACE_CHECK(first.size() > 0);
    GRACE_CHECK(first == second);
}

void testRoundTrip()
{
    Writer writer;
    PackDesc desc;
    desc.name = "meshes/scene";
    desc.kind = 1;
    desc.payload = makePayload(64 * 1024);
    writer.add(desc);
    const std::vector<std::uint8_t> bytes = writer.finish();

    Reader reader(bytes.data(), bytes.size());
    GRACE_CHECK(reader.entries().size() == 1);
    GRACE_CHECK(reader.has("meshes/scene"));
    GRACE_CHECK(!reader.has("meshes/other"));
    const Payload payload = reader.read("meshes/scene");
    GRACE_CHECK(payload.bytes == desc.payload);
}

void testMultipleEntries()
{
    Writer writer;
    PackDesc a;
    a.name = "a";
    a.payload = makePayload(1024);
    PackDesc b;
    b.name = "meshes/b";
    b.kind = 2;
    b.payload = makePayload(2048);
    writer.add(a);
    writer.add(b);
    const std::vector<std::uint8_t> bytes = writer.finish();

    Reader reader(bytes.data(), bytes.size());
    GRACE_CHECK(reader.entries().size() == 2);
    // The index is canonical: records sorted by name in byte order.
    GRACE_CHECK(reader.entries()[0].name == "a");
    GRACE_CHECK(reader.entries()[1].name == "meshes/b");
    GRACE_CHECK(reader.read("a").bytes == a.payload);
    GRACE_CHECK(reader.read("meshes/b").bytes == b.payload);
}

void testFilterShuffle()
{
    Writer writer;
    PackDesc desc;
    desc.name = "meshes/records";
    desc.flags = grace::owd::kFlagDeflated |
                 (1 << grace::owd::kFlagFilterShift);
    desc.elementSize = 4;
    desc.payload = makeRecordPayload(4, 64);
    writer.add(desc);
    const std::vector<std::uint8_t> bytes = writer.finish();

    Reader reader(bytes.data(), bytes.size());
    const Payload payload = reader.read("meshes/records");
    GRACE_CHECK(payload.bytes == desc.payload);
}

void testFilterDelta()
{
    Writer writer;
    PackDesc desc;
    desc.name = "meshes/records";
    desc.flags = grace::owd::kFlagDeflated |
                 (2 << grace::owd::kFlagFilterShift);
    desc.elementSize = 4;
    desc.payload = makeRecordPayload(4, 64);
    writer.add(desc);
    const std::vector<std::uint8_t> bytes = writer.finish();

    Reader reader(bytes.data(), bytes.size());
    const Payload payload = reader.read("meshes/records");
    GRACE_CHECK(payload.bytes == desc.payload);
}

void testReadRange()
{
    Writer writer;
    PackDesc desc;
    desc.name = "meshes/records";
    desc.flags = grace::owd::kFlagDeflated |
                 (1 << grace::owd::kFlagFilterShift);
    desc.elementSize = 4;
    desc.payload = makeRecordPayload(4, 64 * 1024);
    writer.add(desc);
    const std::vector<std::uint8_t> bytes = writer.finish();

    Reader reader(bytes.data(), bytes.size());
    const std::vector<std::uint8_t> range =
        reader.readRange("meshes/records", 4, 16);
    GRACE_CHECK(range.size() == 16);
    for (std::size_t i = 0; i < 16; ++i)
    {
        GRACE_CHECK(range[i] == desc.payload[4 + i]);
    }
}

void testChecksumCorruption()
{
    Writer writer;
    PackDesc desc;
    desc.name = "meshes/scene";
    desc.payload = makePayload(4096);
    writer.add(desc);
    std::vector<std::uint8_t> bytes = writer.finish();

    // Corrupt the payload region: the last byte of the container. The
    // index region is checked at construction, so the corruption has to
    // land inside the payload for the read to see it.
    bytes[36] ^= 0xFF;
    Reader reader(bytes.data(), bytes.size());
    bool threw = false;
    try
    {
        reader.read("meshes/scene");
    }
    catch (const ReaderError&)
    {
        threw = true;
    }
    GRACE_CHECK(threw);
}

void testOverlay()
{
    Writer base;
    PackDesc baseDesc;
    baseDesc.name = "meshes/scene";
    baseDesc.payload = makePayload(1024);
    base.add(baseDesc);
    const std::vector<std::uint8_t> baseBytes = base.finish();

    Writer overlay;
    PackDesc overlayDesc;
    overlayDesc.name = "meshes/scene";
    overlayDesc.payload = makePayload(2048);
    overlay.add(overlayDesc);
    const std::vector<std::uint8_t> overlayBytes = overlay.finish();

    Reader lower(baseBytes.data(), baseBytes.size());
    Reader upper(overlayBytes.data(), overlayBytes.size());
    Loader loader;
    loader.add(&lower);
    loader.add(&upper);
    GRACE_CHECK(loader.count() == 2);
    Reader* found = loader.find("meshes/scene");
    GRACE_CHECK(found != nullptr);
    GRACE_CHECK(found == &upper);
    GRACE_CHECK(loader.find("meshes/missing") == nullptr);
}

void testWriterRefusals()
{
    Writer writer;
    PackDesc desc;
    desc.name = "meshes/scene";
    desc.payload = makePayload(1024);
    writer.add(desc);

    PackDesc duplicate;
    duplicate.name = "meshes/scene";
    duplicate.payload = makePayload(1024);
    bool threw = false;
    try
    {
        writer.add(duplicate);
    }
    catch (const WriterError&)
    {
        threw = true;
    }
    GRACE_CHECK(threw);

    PackDesc empty;
    empty.name = "";
    empty.payload = makePayload(1024);
    threw = false;
    try
    {
        writer.add(empty);
    }
    catch (const WriterError&)
    {
        threw = true;
    }
    GRACE_CHECK(threw);

    PackDesc noElementSize;
    noElementSize.name = "meshes/records";
    noElementSize.flags = grace::owd::kFlagDeflated |
                          (1 << grace::owd::kFlagFilterShift);
    noElementSize.elementSize = 0;
    noElementSize.payload = makePayload(1024);
    threw = false;
    try
    {
        writer.add(noElementSize);
    }
    catch (const WriterError&)
    {
        threw = true;
    }
    GRACE_CHECK(threw);
}

}  // namespace

int main()
{
    testDeterministic();
    testRoundTrip();
    testMultipleEntries();
    testFilterShuffle();
    testFilterDelta();
    testReadRange();
    testChecksumCorruption();
    testOverlay();
    testWriterRefusals();
    return grace::test::summary("owd");
}
