#pragma once

// The OWD container: the writer and the reader.
//
// OWD is Open World Data, this project's resource container
// (docs/resource-container.md). The writer is deterministic (section 8):
// two runs with the same inputs and the same writer version produce
// byte-identical files. The reader is the reference: the header, the index
// blocks, the filters, the per-entry checksum, and the overlay resolution
// (section 6) a loader composes. Nothing in this file names a graphics API
// or a host platform.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "grace/owd/types.hpp"

namespace grace {
namespace owd {

/// A payload to pack: the entry's description without the offsets. The
/// writer computes the offsets, the on-disk size and the checksum; the
/// caller never sets them.
struct PackDesc
{
    std::string name;
    std::uint16_t kind = 0;
    std::uint16_t flags = 0;
    std::uint8_t elementSize = 0;
    std::uint8_t alignmentExponent = 0;
    std::vector<std::string> dependencies;
    std::vector<std::uint8_t> payload;
};

/// A writer error: the writer refuses to produce a container it would
/// not read back (duplicate names, empty names, a filter with no element
/// size, a filter on a stored payload). The message is the spec's own
/// words, and the writer's failure is a build error, not a runtime
/// fallback.
class WriterError : public std::runtime_error
{
public:
    explicit WriterError(const std::string& message);
};

/// A reader error: the container's bytes are malformed, a checksum does
/// not match, or the name is not in the index. The message is the spec's
/// own words, and the reader never hands out bytes it did not verify.
class ReaderError : public std::runtime_error
{
public:
    explicit ReaderError(const std::string& message);
};

/// The writer.
class Writer
{
public:
    Writer();
    ~Writer();

    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    /// Adds an entry. The entry's name is the index key; a duplicate name
    /// is a WriterError.
    void add(const PackDesc& desc);

    /// Serialises the container to bytes. The index is canonical: records
    /// are sorted by name in byte order, padding bytes are a fixed value,
    /// and no field depends on a timestamp or on the writer's visit order.
    std::vector<std::uint8_t> finish() const;

    /// The entries added so far.
    std::size_t count() const;

private:
    struct Impl;
    Impl* m_impl;
};

/// The reader over one container's bytes. The bytes are not copied; the
/// Reader holds the pointer and the size the caller owns.
class Reader
{
public:
    Reader();
    ~Reader();

    /// Parses the header, the index block table and the index blocks.
    /// Throws ReaderError on any malformed input.
    Reader(const std::uint8_t* data, std::size_t size);

    /// The entries, in the index's canonical order.
    std::vector<EntryDesc> entries() const;

    /// The entry's record, by name.
    const EntryDesc& entry(const std::string& name) const;

    /// Whether the container has an entry with the name.
    bool has(const std::string& name) const;

    /// The inflated, unfiltered payload bytes. The checksum is verified
    /// before the bytes are handed out; a mismatch is a ReaderError.
    Payload read(const std::string& name) const;

    /// A range of the inflated, unfiltered payload bytes. Only the blocks
    /// covering the range are inflated, which is requirement 5's read
    /// half: the read cost does not grow with the payload.
    std::vector<std::uint8_t> readRange(const std::string& name,
                                        std::uint64_t offset,
                                        std::uint64_t size) const;

private:
    struct Impl;
    Impl* m_impl;
};

/// The overlay resolution (section 6): an ordered list of readers, from
/// lowest to highest priority. A later provider overrides an earlier one.
class Loader
{
public:
    /// Adds a provider. The provider the caller owns stays owned by the
    /// caller; the Loader holds the pointer and the order.
    void add(Reader* reader);

    /// The number of providers.
    std::size_t count() const;

    /// Resolves a name by walking the list from the highest priority down,
    /// and returns the first provider that has it. Returns nullptr when
    /// no provider has it.
    Reader* find(const std::string& name) const;

private:
    std::vector<Reader*> m_readers;
};

}  // namespace owd
}  // namespace grace
