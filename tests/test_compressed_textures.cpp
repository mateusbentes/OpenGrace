#include "grace/core/compressed_textures.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "test_support.hpp"

namespace {

using grace::core::BlockFormat;
using grace::core::DecodedImage;

/// Builds a BC1 colour block from two 565 endpoints and a code fill.
std::vector<std::uint8_t> bc1Block(std::uint16_t c0, std::uint16_t c1, std::uint32_t codes)
{
    std::vector<std::uint8_t> block(8, 0);
    block[0] = static_cast<std::uint8_t>(c0 & 0xFFu);
    block[1] = static_cast<std::uint8_t>((c0 >> 8) & 0xFFu);
    block[2] = static_cast<std::uint8_t>(c1 & 0xFFu);
    block[3] = static_cast<std::uint8_t>((c1 >> 8) & 0xFFu);
    for (std::uint32_t i = 0; i < 4u; ++i)
        block[4u + i] = static_cast<std::uint8_t>((codes >> (8u * i)) & 0xFFu);
    return block;
}

/// A 565 value from component values (0..31, 0..63, 0..31).
std::uint16_t rgb565(std::uint32_t r5, std::uint32_t g6, std::uint32_t b5)
{
    return static_cast<std::uint16_t>((r5 << 11) | (g6 << 5) | b5);
}

/// The published 565 -> 8888 expansion the decoder uses, stated once so the
/// tests derive their expectations from it rather than duplicating code.
std::uint8_t expand565(std::uint32_t component5, std::uint32_t componentMax)
{
    const std::uint32_t scale = componentMax == 31u ? 527u : 259u;
    const std::uint32_t offset = componentMax == 31u ? 23u : 33u;
    return static_cast<std::uint8_t>((component5 * scale + offset) >> 6);
}

bool throwsInvalidShape(BlockFormat format, std::uint32_t w, std::uint32_t h,
                        std::size_t bytes)
{
    try
    {
        const DecodedImage image =
            grace::core::decodeImage(format, w, h, nullptr, bytes);
        static_cast<void>(image);
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }
    return false;
}

}  // namespace

int main()
{
    // The shape rules: positive, multiple of 4, byte count exact.
    {
        GRACE_CHECK(!grace::core::isValidCompressedShape(BlockFormat::BC1, 0, 4, 8));
        GRACE_CHECK(!grace::core::isValidCompressedShape(BlockFormat::BC1, 3, 4, 8));
        GRACE_CHECK(!grace::core::isValidCompressedShape(BlockFormat::BC1, 4, 5, 8));
        GRACE_CHECK(grace::core::isValidCompressedShape(BlockFormat::BC1, 4, 4, 8));
        GRACE_CHECK(!grace::core::isValidCompressedShape(BlockFormat::BC1, 4, 4, 16));
        GRACE_CHECK(!grace::core::isValidCompressedShape(BlockFormat::BC3, 4, 4, 8));
        GRACE_CHECK(grace::core::isValidCompressedShape(BlockFormat::BC3, 4, 4, 16));
        GRACE_CHECK(!grace::core::isValidCompressedShape(BlockFormat::BC1, 4, 4, 7));
        GRACE_CHECK(throwsInvalidShape(BlockFormat::BC1, 3, 4, 8));
        GRACE_CHECK(throwsInvalidShape(BlockFormat::BC1, 4, 4, 999));
    }

    // The block sizes: 8 for BC1/BC4, 16 for the rest.
    {
        GRACE_CHECK_EQ(grace::core::blockSize(BlockFormat::BC1), static_cast<std::size_t>(8));
        GRACE_CHECK_EQ(grace::core::blockSize(BlockFormat::BC4), static_cast<std::size_t>(8));
        GRACE_CHECK_EQ(grace::core::blockSize(BlockFormat::BC2), static_cast<std::size_t>(16));
        GRACE_CHECK_EQ(grace::core::blockSize(BlockFormat::BC3), static_cast<std::size_t>(16));
        GRACE_CHECK_EQ(grace::core::blockSize(BlockFormat::BC5), static_cast<std::size_t>(16));
    }

    // A BC1 block with both endpoints equal: every texel is that colour
    // (the code bits are irrelevant — every palette entry collapses).
    {
        const std::uint16_t grey = rgb565(16, 32, 16);
        const std::vector<std::uint8_t> block = bc1Block(grey, grey, 0xAAAAAAAAu);
        const DecodedImage image =
            grace::core::decodeImage(BlockFormat::BC1, 4, 4, block.data(), block.size());
        GRACE_CHECK_EQ(image.width, 4u);
        GRACE_CHECK_EQ(image.height, 4u);
        GRACE_CHECK_EQ(image.rgba8.size(), static_cast<std::size_t>(4) * 4 * 4);
        const std::uint8_t r = expand565(16, 31);
        const std::uint8_t g = expand565(32, 63);
        const std::uint8_t b = expand565(16, 31);
        for (std::size_t i = 0; i < 16; ++i)
        {
            GRACE_CHECK_EQ(image.rgba8[i * 4 + 0], r);
            GRACE_CHECK_EQ(image.rgba8[i * 4 + 1], g);
            GRACE_CHECK_EQ(image.rgba8[i * 4 + 2], b);
            GRACE_CHECK_EQ(image.rgba8[i * 4 + 3], 255);
        }
    }

    // The BC1 four-point palette: c0 > c1 (the opaque variant). With all
    // texels on code 0 the image is endpoint 0; with all on code 1 it is
    // endpoint 1; with code 2 the 2:1 blend.
    {
        const std::uint16_t red = rgb565(31, 0, 0);
        const std::uint16_t blue = rgb565(0, 0, 31);
        {
            const std::vector<std::uint8_t> block = bc1Block(red, blue, 0x00000000u);
            const DecodedImage image =
                grace::core::decodeImage(BlockFormat::BC1, 4, 4, block.data(), block.size());
            for (std::size_t i = 0; i < 16; ++i)
                GRACE_CHECK_EQ(image.rgba8[i * 4 + 0], 255);
        }
        {
            const std::vector<std::uint8_t> block = bc1Block(red, blue, 0x55555555u);
            const DecodedImage image =
                grace::core::decodeImage(BlockFormat::BC1, 4, 4, block.data(), block.size());
            for (std::size_t i = 0; i < 16; ++i)
                GRACE_CHECK_EQ(image.rgba8[i * 4 + 0], 0);
        }
        {
            // Code 2 (binary 10 in each 2-bit slot): 0xAA = 10101010, every
            // slot reads 2. The blend is (2*255 + 0)/3 = 170.
            const std::vector<std::uint8_t> block = bc1Block(red, blue, 0xAAAAAAAAu);
            const DecodedImage image =
                grace::core::decodeImage(BlockFormat::BC1, 4, 4, block.data(), block.size());
            for (std::size_t i = 0; i < 16; ++i)
                GRACE_CHECK_EQ(image.rgba8[i * 4 + 0], 170);
        }
    }

    // The BC1 transparent variant: c0 <= c1 makes code 3 transparent black.
    {
        const std::uint16_t red = rgb565(31, 0, 0);
        const std::uint16_t darkRed = rgb565(1, 0, 0);
        // All slots at code 3 (binary 11): 0xFFFFFFFF.
        const std::vector<std::uint8_t> block = bc1Block(darkRed, red, 0xFFFFFFFFu);
        const DecodedImage image =
            grace::core::decodeImage(BlockFormat::BC1, 4, 4, block.data(), block.size());
        for (std::size_t i = 0; i < 16; ++i)
        {
            GRACE_CHECK_EQ(image.rgba8[i * 4 + 0], 0);
            GRACE_CHECK_EQ(image.rgba8[i * 4 + 1], 0);
            GRACE_CHECK_EQ(image.rgba8[i * 4 + 2], 0);
            GRACE_CHECK_EQ(image.rgba8[i * 4 + 3], 0);
        }
    }

    // BC2: the explicit alpha plane. Alpha nibbles 0 and 15 decode to 0 and
    // 255 through the *17 multiply.
    {
        std::vector<std::uint8_t> block(16, 0);
        // Colour block at bytes 8..15: endpoints equal, all codes 0.
        const std::uint16_t grey = rgb565(16, 32, 16);
        block[8] = static_cast<std::uint8_t>(grey & 0xFFu);
        block[9] = static_cast<std::uint8_t>((grey >> 8) & 0xFFu);
        block[10] = block[8];
        block[11] = block[9];
        // Alpha plane bytes 0..7: texel 0 nibble 15, texel 1 nibble 0.
        block[0] = 0x0Fu;  // texel 0 low nibble = 15, texel 1 high nibble = 0
        const DecodedImage image =
            grace::core::decodeImage(BlockFormat::BC2, 4, 4, block.data(), block.size());
        GRACE_CHECK_EQ(image.rgba8[3], 255);   // texel 0
        GRACE_CHECK_EQ(image.rgba8[7], 0);     // texel 1
        GRACE_CHECK_EQ(image.rgba8[11], 0);    // texel 2 (high nibble 0)
    }

    // BC3: the interpolated alpha. Endpoints a0 > a1 use the 7-point
    // palette: code 0 is a0, code 1 is a1, code 2 is (6*a0 + a1)/7.
    {
        std::vector<std::uint8_t> block(16, 0);
        block[0] = 255;  // a0
        block[1] = 0;    // a1
        // 48 code bits: all zero = every texel on code 0 = a0.
        const DecodedImage allZero =
            grace::core::decodeImage(BlockFormat::BC3, 4, 4, block.data(), block.size());
        for (std::size_t i = 0; i < 16; ++i)
            GRACE_CHECK_EQ(allZero.rgba8[i * 4 + 3], 255);
        // All texels on code 1: the 48 bits packed LSB-first give the
        // byte pattern 49 92 24 49 92 24 (each 3-bit slot reads 1).
        block[2] = 0x49;
        block[3] = 0x92;
        block[4] = 0x24;
        block[5] = 0x49;
        block[6] = 0x92;
        block[7] = 0x24;
        const DecodedImage allOne =
            grace::core::decodeImage(BlockFormat::BC3, 4, 4, block.data(), block.size());
        for (std::size_t i = 0; i < 16; ++i)
            GRACE_CHECK_EQ(allOne.rgba8[i * 4 + 3], 0);
    }

    // BC4: the single channel replicates into RGB, alpha 255.
    {
        std::vector<std::uint8_t> block(8, 0);
        block[0] = 128;  // a0
        block[1] = 64;   // a1
        const DecodedImage image =
            grace::core::decodeImage(BlockFormat::BC4, 4, 4, block.data(), block.size());
        for (std::size_t i = 0; i < 16; ++i)
        {
            GRACE_CHECK_EQ(image.rgba8[i * 4 + 0], 128);
            GRACE_CHECK_EQ(image.rgba8[i * 4 + 1], 128);
            GRACE_CHECK_EQ(image.rgba8[i * 4 + 2], 128);
            GRACE_CHECK_EQ(image.rgba8[i * 4 + 3], 255);
        }
    }

    // BC5: two blocks into R and G.
    {
        std::vector<std::uint8_t> block(16, 0);
        block[0] = 200;   // first block's a0 -> R
        block[8 + 0] = 100;  // second block's a0 -> G
        const DecodedImage image =
            grace::core::decodeImage(BlockFormat::BC5, 4, 4, block.data(), block.size());
        for (std::size_t i = 0; i < 16; ++i)
        {
            GRACE_CHECK_EQ(image.rgba8[i * 4 + 0], 200);
            GRACE_CHECK_EQ(image.rgba8[i * 4 + 1], 100);
            GRACE_CHECK_EQ(image.rgba8[i * 4 + 2], 0);
            GRACE_CHECK_EQ(image.rgba8[i * 4 + 3], 255);
        }
    }

    // A 4x8 image: two block rows, and the decoder places them correctly.
    {
        std::vector<std::uint8_t> data(2 * 8, 0);
        const std::uint16_t colourTop = rgb565(31, 63, 31);
        const std::uint16_t colourBottom = rgb565(0, 0, 0);
        data[0] = static_cast<std::uint8_t>(colourTop & 0xFFu);
        data[1] = static_cast<std::uint8_t>((colourTop >> 8) & 0xFFu);
        data[8] = static_cast<std::uint8_t>(colourBottom & 0xFFu);
        data[9] = static_cast<std::uint8_t>((colourBottom >> 8) & 0xFFu);
        const DecodedImage image =
            grace::core::decodeImage(BlockFormat::BC1, 4, 8, data.data(), data.size());
        // Row 0: the top block's colour. Row 7: the bottom's.
        GRACE_CHECK_EQ(image.rgba8[0], expand565(31, 31));
        GRACE_CHECK_EQ(image.rgba8[(7u * 4u) * 4u + 0u], 0);
    }

    // Determinism: two decodes of the same block are bit-identical.
    {
        const std::uint16_t red = rgb565(31, 0, 0);
        const std::uint16_t blue = rgb565(0, 0, 31);
        const std::vector<std::uint8_t> block = bc1Block(red, blue, 0x1B1B1B1Bu);
        const DecodedImage a =
            grace::core::decodeImage(BlockFormat::BC1, 4, 4, block.data(), block.size());
        const DecodedImage b =
            grace::core::decodeImage(BlockFormat::BC1, 4, 4, block.data(), block.size());
        GRACE_CHECK(a.rgba8 == b.rgba8);
    }

    return grace::test::summary("compressed_textures");
}
