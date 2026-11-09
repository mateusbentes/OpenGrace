// The scene's serialisation strategy (decision 36): the scene's state
// writes into the OWD container as one entry, and reads back from it.
//
// The format is deliberately simple and engine-owned, so the writer and
// the reader can both be proven deterministic: the same entities produce
// byte-identical bytes, and the reader accepts exactly what the writer
// produces. The integers are little-endian and unsigned. The bytes are
// the payload of one OWD entry; the container's own checksums and the
// overlay loader carry the rest of the integrity.
//
// Layout (version 1):
//   u32    magic 'O', 'G', 'S', 1 (OpenGrace Scene, version 1): the
//          bytes are 'O', 'G', 'S', 0x01 in that order
//   u32    entity count
//   for each entity:
//     u32    name length in bytes
//     bytes  name, UTF-8
//     f32    position x, y, z
//     f32    scale x, y, z
//     u32    mesh index
//     u32    material index
//     u32    LOD step count
//     for each LOD step:
//       f32  distance
//       u32  mesh index
#include "grace/scene/scene_serial.hpp"
#include <cstring>
#include <stdexcept>
namespace grace {
namespace scene {
namespace {
// 0x0153474F little-endian is 'O', 'G', 'S', 0x01.
inline constexpr std::uint32_t kMagic = 0x0153474Fu;
inline constexpr std::uint32_t kVersion = 1;
// Little-endian put/get helpers. The container's own bytes are
// little-endian too; the scene's payload matches the platform's usual
// storage order so the engine can stream the bytes directly.
void put32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}
void putFloat(std::vector<std::uint8_t>& out, float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    put32(out, bits);
}
std::uint32_t get32(const std::uint8_t* data, std::size_t& cursor,
                    std::size_t size)
{
    if (cursor + 4 > size)
        throw SerialError("scene: the serialised payload is truncated");
    const std::uint32_t value = static_cast<std::uint32_t>(
        data[cursor] | (data[cursor + 1] << 8) | (data[cursor + 2] << 16) |
        (data[cursor + 3] << 24));
    cursor += 4;
    return value;
}
float getFloat(const std::uint8_t* data, std::size_t& cursor,
               std::size_t size)
{
    const std::uint32_t bits = get32(data, cursor, size);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
}  // namespace
SerialError::SerialError(const std::string& message)
    : std::runtime_error(message)
{
}
std::vector<std::uint8_t> serialise(const std::vector<EntityDesc>& entities)
{
    std::vector<std::uint8_t> out;
    out.reserve(64 + entities.size() * 96);
    put32(out, kMagic);
    put32(out, kVersion);
    put32(out, static_cast<std::uint32_t>(entities.size()));
    for (const EntityDesc& entity : entities)
    {
        const std::string& name = entity.name;
        put32(out, static_cast<std::uint32_t>(name.size()));
        out.insert(out.end(), name.begin(), name.end());
        putFloat(out, entity.position.x);
        putFloat(out, entity.position.y);
        putFloat(out, entity.position.z);
        putFloat(out, entity.scale.x);
        putFloat(out, entity.scale.y);
        putFloat(out, entity.scale.z);
        put32(out, entity.meshIndex);
        put32(out, entity.materialIndex);
        put32(out, static_cast<std::uint32_t>(entity.lods.size()));
        for (const LodStep& step : entity.lods)
        {
            putFloat(out, step.distance);
            put32(out, step.meshIndex);
        }
    }
    return out;
}
std::vector<EntityDesc> deserialise(const std::vector<std::uint8_t>& bytes)
{
    std::size_t cursor = 0;
    const std::size_t size = bytes.size();
    const std::uint32_t magic = get32(bytes.data(), cursor, size);
    if (magic != kMagic)
        throw SerialError("scene: the serialised payload is not a scene");
    const std::uint32_t version = get32(bytes.data(), cursor, size);
    if (version != kVersion)
        throw SerialError("scene: the serialised payload version is unknown");
    const std::uint32_t count = get32(bytes.data(), cursor, size);
    std::vector<EntityDesc> entities;
    entities.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        EntityDesc entity;
        const std::uint32_t nameLength = get32(bytes.data(), cursor, size);
        if (nameLength == 0)
            throw SerialError("scene: an empty entity name is invalid");
        if (cursor + nameLength > size)
            throw SerialError("scene: the serialised payload is truncated");
        entity.name.assign(
            reinterpret_cast<const char*>(bytes.data() + cursor),
            nameLength);
        cursor += nameLength;
        entity.position.x = getFloat(bytes.data(), cursor, size);
        entity.position.y = getFloat(bytes.data(), cursor, size);
        entity.position.z = getFloat(bytes.data(), cursor, size);
        entity.scale.x = getFloat(bytes.data(), cursor, size);
        entity.scale.y = getFloat(bytes.data(), cursor, size);
        entity.scale.z = getFloat(bytes.data(), cursor, size);
        entity.meshIndex = get32(bytes.data(), cursor, size);
        entity.materialIndex = get32(bytes.data(), cursor, size);
        const std::uint32_t lodCount = get32(bytes.data(), cursor, size);
        entity.lods.resize(lodCount);
        for (std::uint32_t lod = 0; lod < lodCount; ++lod)
        {
            entity.lods[lod].distance = getFloat(bytes.data(), cursor, size);
            entity.lods[lod].meshIndex = get32(bytes.data(), cursor, size);
        }
        std::string message;
        if (!entity.validate(message))
            throw SerialError(message);
        entities.push_back(std::move(entity));
    }
    if (cursor != size)
        throw SerialError("scene: the serialised payload has trailing bytes");
    return entities;
}
}  // namespace scene
}  // namespace grace
