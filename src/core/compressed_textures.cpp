#include "grace/core/compressed_textures.hpp"

#include <stdexcept>

namespace grace::core {
namespace {

/// Reads a little-endian 16-bit value (the block formats' byte order).
std::uint16_t readU16(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                      (static_cast<std::uint16_t>(p[1]) << 8));
}

/// Reads a little-endian 32-bit value.
std::uint32_t readU32(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint32_t>(static_cast<std::uint32_t>(p[0])) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

/// Expands a 565 colour to 8888 (alpha 255).
void expand565(std::uint16_t value, std::uint8_t* out) noexcept
{
    const std::uint32_t r5 = (value >> 11) & 0x1Fu;
    const std::uint32_t g6 = (value >> 5) & 0x3Fu;
    const std::uint32_t b5 = value & 0x1Fu;
    out[0] = static_cast<std::uint8_t>((r5 * 527u + 23u) >> 6);  // 255/31 scaled
    out[1] = static_cast<std::uint8_t>((g6 * 259u + 33u) >> 6);  // 255/63 scaled
    out[2] = static_cast<std::uint8_t>((b5 * 527u + 23u) >> 6);
    out[3] = 255u;
}

/// The BC4/BC5 alpha-style 3-point interpolation: a0/a1 are the endpoints,
/// the 3-bit codes pick between them and the two interpolated thirds.
std::uint8_t interpolate3(std::uint8_t a0, std::uint8_t a1, std::uint32_t code) noexcept
{
    switch (code)
    {
    case 0: return a0;
    case 1: return a1;
    case 2: return static_cast<std::uint8_t>((6u * a0 + 1u * a1) / 7u);
    case 3: return static_cast<std::uint8_t>((5u * a0 + 2u * a1) / 7u);
    case 4: return static_cast<std::uint8_t>((4u * a0 + 3u * a1) / 7u);
    case 5: return static_cast<std::uint8_t>((3u * a0 + 4u * a1) / 7u);
    case 6: return static_cast<std::uint8_t>((2u * a0 + 5u * a1) / 7u);
    default: return static_cast<std::uint8_t>((1u * a0 + 6u * a1) / 7u);
    }
}

/// The BC1 colour block: 2 565 endpoints, a 2-bit code per texel, and the
/// c0 == c1 (or c0 <= c1 in the transparent variant) degenerate cases.
/// Decodes into the 16 RGB triplets; alpha stays 255 (the transparent
/// variant writes 0 into code 3 texels when writeTransparentAlpha is set).
void decodeColourBlock(const std::uint8_t* block, std::uint8_t* out,
                       bool writeTransparentAlpha) noexcept
{
    const std::uint16_t c0 = readU16(block);
    const std::uint16_t c1 = readU16(block + 2);
    const std::uint32_t codes = readU32(block + 4);

    std::uint8_t endpoint0[4];
    std::uint8_t endpoint1[4];
    expand565(c0, endpoint0);
    expand565(c1, endpoint1);

    // The degenerate palette of the transparent variant: when c0 <= c1 the
    // palette's fourth entry is transparent black instead of a blend.
    const bool degenerate = c0 <= c1;

    std::uint8_t palette[4][4];
    for (int channel = 0; channel < 3; ++channel)
    {
        const std::uint32_t e0 = endpoint0[channel];
        const std::uint32_t e1 = endpoint1[channel];
        palette[0][channel] = static_cast<std::uint8_t>(e0);
        palette[1][channel] = static_cast<std::uint8_t>(e1);
        palette[2][channel] = static_cast<std::uint8_t>(degenerate ? ((e0 + e1) / 2u)
                                                                    : ((2u * e0 + e1) / 3u));
        palette[3][channel] = static_cast<std::uint8_t>(degenerate ? 0u
                                                                   : ((e0 + 2u * e1) / 3u));
    }
    palette[0][3] = 255u;
    palette[1][3] = 255u;
    palette[2][3] = 255u;
    // The transparent variant's code 3 is transparent black. The published
    // rule: the degenerate case (c0 <= c1) carries it.
    palette[3][3] = (degenerate && writeTransparentAlpha) ? 0u : 255u;

    for (std::uint32_t texel = 0; texel < 16; ++texel)
    {
        const std::uint32_t code = (codes >> (2u * texel)) & 0x3u;
        out[texel * 4u + 0u] = palette[code][0];
        out[texel * 4u + 1u] = palette[code][1];
        out[texel * 4u + 2u] = palette[code][2];
        out[texel * 4u + 3u] = palette[code][3];
    }
}

/// The BC2 explicit 4-bit alpha plane: 8 bytes, one nibble per texel,
/// lowest nibble = texel 0.
void decodeBc2Alpha(const std::uint8_t* alpha, std::uint8_t* out) noexcept
{
    for (std::uint32_t texel = 0; texel < 16; ++texel)
    {
        const std::uint8_t nibble = (texel % 2u == 0u) ? (alpha[texel / 2u] & 0x0Fu)
                                                       : (alpha[texel / 2u] >> 4);
        out[texel * 4u + 3u] = static_cast<std::uint8_t>(nibble * 17u);  // 0..15 -> 0..255
    }
}

/// The BC3 interpolated alpha block: 2 8-bit endpoints and 16 3-bit codes
/// packed into 48 bits (6 bytes) after them.
void decodeBc3Alpha(const std::uint8_t* alpha, std::uint8_t* out) noexcept
{
    const std::uint8_t a0 = alpha[0];
    const std::uint8_t a1 = alpha[1];
   // The 48 code bits, as one integer (6 bytes fit exactly).
   std::uint64_t codes = 0;
    for (std::uint32_t i = 0; i < 6u; ++i)
        codes |= static_cast<std::uint64_t>(alpha[2u + i]) << (8u * i);

    for (std::uint32_t texel = 0; texel < 16; ++texel)
    {
        const std::uint32_t code = static_cast<std::uint32_t>((codes >> (3u * texel)) & 0x7u);
        std::uint8_t value;
        if (a0 > a1)
            value = interpolate3(a0, a1, code);
        else
        {
            // The degenerate palette: codes 0-3 are the endpoints and the
            // blend, codes 4-7 are the documented specials (0, 255 and the
            // two blends that fall through).
            switch (code)
            {
            case 0: value = a0; break;
            case 1: value = a1; break;
            case 2: value = static_cast<std::uint8_t>((4u * a0 + a1) / 5u); break;
            case 3: value = static_cast<std::uint8_t>((3u * a0 + 2u * a1) / 5u); break;
            case 4: value = static_cast<std::uint8_t>((2u * a0 + 3u * a1) / 5u); break;
            case 5: value = static_cast<std::uint8_t>((a0 + 4u * a1) / 5u); break;
            case 6: value = 0u; break;
            default: value = 255u; break;
            }
        }
        out[texel * 4u + 3u] = value;
    }
}

/// The BC4 single-channel block: the same interpolated alpha structure,
/// decoded into every RGBA channel (the convention the consumers of a
/// single-channel map state: replicate, alpha 255).
void decodeBc4(const std::uint8_t* block, std::uint8_t* out) noexcept
{
    std::uint8_t decoded[64];
    decodeBc3Alpha(block, decoded);
    for (std::uint32_t texel = 0; texel < 16; ++texel)
    {
        out[texel * 4u + 0u] = decoded[texel * 4u + 3u];
        out[texel * 4u + 1u] = decoded[texel * 4u + 3u];
        out[texel * 4u + 2u] = decoded[texel * 4u + 3u];
        out[texel * 4u + 3u] = 255u;
    }
}

}  // namespace

bool isValidCompressedShape(BlockFormat format, std::uint32_t width, std::uint32_t height,
                            std::size_t byteCount) noexcept
{
    if (width == 0u || height == 0u)
        return false;
    if (width % 4u != 0u || height % 4u != 0u)
        return false;
    return byteCount == compressedSize(format, width, height);
}

void decodeBlock(BlockFormat format, const std::uint8_t* block, std::uint8_t* outRgba8) noexcept
{
    switch (format)
    {
    case BlockFormat::BC1:
        decodeColourBlock(block, outRgba8, true);
        return;
    case BlockFormat::BC2:
        decodeColourBlock(block + 8u, outRgba8, false);
        decodeBc2Alpha(block, outRgba8);
        return;
    case BlockFormat::BC3:
        decodeColourBlock(block + 8u, outRgba8, false);
        decodeBc3Alpha(block, outRgba8);
        return;
    case BlockFormat::BC4:
        decodeBc4(block, outRgba8);
        return;
    case BlockFormat::BC5:
        {
            // Two BC4 blocks: the first into R (as G in the swizzle the
            // consumers state — the published normal-map convention is
            // x in R, y in G), the second into G. Here the two channels
            // land in R and G directly.
            std::uint8_t first[64];
            std::uint8_t second[64];
            decodeBc3Alpha(block, first);
            decodeBc3Alpha(block + 8u, second);
            for (std::uint32_t texel = 0; texel < 16u; ++texel)
            {
                outRgba8[texel * 4u + 0u] = first[texel * 4u + 3u];
                outRgba8[texel * 4u + 1u] = second[texel * 4u + 3u];
                outRgba8[texel * 4u + 2u] = 0u;
                outRgba8[texel * 4u + 3u] = 255u;
            }
        }
        return;
    }
}

DecodedImage decodeImage(BlockFormat format, std::uint32_t width, std::uint32_t height,
                         const std::uint8_t* data, std::size_t byteCount)
{
    if (!isValidCompressedShape(format, width, height, byteCount))
        throw std::invalid_argument(
            "decodeImage: the compressed image's shape does not match its byte count");
    if (data == nullptr)
        throw std::invalid_argument("decodeImage: the compressed data pointer is null");

    DecodedImage image;
    image.width = width;
    image.height = height;
    image.rgba8.resize(static_cast<std::size_t>(width) * height * 4u);

    const std::size_t pitch = compressedRowPitch(format, width);
    for (std::uint32_t blockRow = 0; blockRow < height / 4u; ++blockRow)
    {
        for (std::uint32_t blockCol = 0; blockCol < width / 4u; ++blockCol)
        {
            std::uint8_t decoded[kDecodedBlockBytes];
            decodeBlock(format, data + blockRow * pitch + blockCol * blockSize(format), decoded);
            // Write the 4x4 texels into the image.
            for (std::uint32_t y = 0; y < 4u; ++y)
            {
                for (std::uint32_t x = 0; x < 4u; ++x)
                {
                    const std::size_t src = (y * 4u + x) * 4u;
                    const std::size_t dst =
                        (static_cast<std::size_t>(blockRow * 4u + y) * width +
                         (blockCol * 4u + x)) * 4u;
                    image.rgba8[dst + 0u] = decoded[src + 0u];
                    image.rgba8[dst + 1u] = decoded[src + 1u];
                    image.rgba8[dst + 2u] = decoded[src + 2u];
                    image.rgba8[dst + 3u] = decoded[src + 3u];
                }
            }
        }
    }
    return image;
}

}  // namespace grace::core
