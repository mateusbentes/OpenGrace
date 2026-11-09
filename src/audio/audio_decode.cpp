// The decode layer's implementation: the WAV decoder is hand-written
// (the RIFF format is small) and lives in the base library; the Ogg Vorbis
// decoder links libvorbisfile and lives in the adapter's sources. Both hand
// the caller PCM samples; neither throws.
#include "grace/audio/audio_decode.hpp"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
namespace grace {
namespace audio {
namespace {
/// Reads a little-endian 16-bit value from a byte span.
std::uint16_t readU16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(static_cast<unsigned>(data[1]) << 8);
}
/// Reads a little-endian 32-bit value from a byte span.
std::uint32_t readU32(const std::uint8_t* data)
{
    return static_cast<std::uint32_t>(data[0]) |
           static_cast<std::uint32_t>(static_cast<unsigned>(data[1]) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned>(data[2]) << 16) |
           static_cast<std::uint32_t>(static_cast<unsigned>(data[3]) << 24);
}
/// The WAV sample format of a fmt chunk: the channels and the bits. The
/// IEEE float (bits 32) and the planar formats are rejected.
bool wavFormat(std::uint16_t channels, std::uint16_t bits, SampleFormat& out)
{
    if (channels == 1 && bits == 8)
    {
        out = SampleFormat::Mono8;
        return true;
    }
    if (channels == 1 && bits == 16)
    {
        out = SampleFormat::Mono16;
        return true;
    }
    if (channels == 2 && bits == 8)
    {
        out = SampleFormat::Stereo8;
        return true;
    }
    if (channels == 2 && bits == 16)
    {
        out = SampleFormat::Stereo16;
        return true;
    }
    return false;
}
}  // namespace
Decoded decodeWav(const std::vector<std::uint8_t>& bytes)
{
    Decoded result;
    // The RIFF header: "RIFF" + the size + "WAVE".
    if (bytes.size() < 12u || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
    {
        result.reason = "not a RIFF/WAVE file";
        return result;
    }
    // The chunks: each is a 4-byte id, a 4-byte size and the payload.
    std::size_t offset = 12u;
    bool haveFormat = false;
    bool haveData = false;
    std::uint16_t channels = 0;
    std::uint16_t bits = 0;
    std::uint32_t sampleRate = 0;
    while (offset + 8u <= bytes.size())
    {
        const std::uint32_t chunkSize = readU32(bytes.data() + offset + 4u);
        if (std::memcmp(bytes.data() + offset, "fmt ", 4) == 0 &&
            chunkSize >= 16u)
        {
            const std::uint8_t* fmt = bytes.data() + offset + 8u;
            channels = readU16(fmt + 2u);
            sampleRate = readU32(fmt + 4u);
            bits = readU16(fmt + 14u);
            haveFormat = wavFormat(channels, bits, result.format);
        }
        else if (std::memcmp(bytes.data() + offset, "data", 4) == 0)
        {
            const std::uint8_t* data = bytes.data() + offset + 8u;
            const std::size_t available = bytes.size() - (offset + 8u);
            const std::size_t take =
                std::min<std::size_t>(chunkSize, available);
            result.samples.assign(data, data + take);
            haveData = true;
        }
        // Chunks are word-aligned: the size rounds up to an even value.
        offset += 8u + chunkSize + (chunkSize % 2u);
    }
    if (!haveFormat)
    {
        result.reason = "no supported fmt chunk (8-bit or 16-bit PCM only)";
        return result;
    }
    if (!haveData)
    {
        result.reason = "no data chunk";
        return result;
    }
    result.sampleRate = sampleRate;
    result.ok = true;
    return result;
}

std::string validate(const Decoded& decoded)
{
    if (!decoded.ok)
    {
        return decoded.reason;
    }
    SoundDesc desc;
    desc.samples = decoded.samples;
    desc.format = decoded.format;
    desc.sampleRate = decoded.sampleRate;
    return validate(desc);
}
}  // namespace audio
}  // namespace grace
