#pragma once
// The scene's serialisation strategy (decision 36): the scene's state
// writes into the OWD container as one entry, and reads back from it.
//
// The open question (decision 33's note) asked what the save format's
// relationship to script state is, given that AngelScript has no built-in
// object serialisation. The answer is the one the note recorded: game
// state lives in engine-owned structures wherever practical, with scripts
// holding behaviour rather than authoritative state. The scene IS the
// engine-owned structure, so it serialises itself: the writer produces
// the container's bytes (the writer, the reader, the filters, the
// checksums, the overlay loader), the scene serialises into it as one
// entry, and the reader reads back.
//
// The entry name is the scene's own choice and is the caller's to
// control: the container is the storage, not the scene. The serialisation
// is deterministic: the same entities and the same LOD steps produce
// byte-identical bytes, so the container's own checksum requirement
// (section 8) is met by the scene's data alone.
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include "grace/scene/scene_types.hpp"
namespace grace {
namespace scene {
/// The serialisation error: the bytes are malformed, a version is
/// unknown, or the reader's payload is truncated.
class SerialError : public std::runtime_error
{
public:
    explicit SerialError(const std::string& message);
};
/// Serialises the entities into the OWD container's entry payload. The
/// bytes are little-endian, unsigned integers, and deterministic: the
/// same entities produce byte-identical bytes.
std::vector<std::uint8_t> serialise(const std::vector<EntityDesc>& entities);
/// Reads back the serialised entities. Throws SerialError on any
/// malformed input: a version the reader does not know, a truncated
/// payload, or a name that does not validate.
std::vector<EntityDesc> deserialise(const std::vector<std::uint8_t>& bytes);
}  // namespace scene
}  // namespace grace
