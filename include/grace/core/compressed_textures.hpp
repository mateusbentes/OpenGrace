#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace grace::core {

/// The compressed texture formats the engine supports (GRACE 2's documented
/// "compressed texture support", restricted to the block formats every one
/// of the three render backends can carry: DXT1/BC1, DXT3/BC2, DXT5/BC3,
/// BC4 and BC5 — the PVR/ETC/ATC entries of the original's list are the
/// platform-specific formats of other vendors and have no meaning on the
/// API trio this project targets).
enum class BlockFormat : std::uint8_t
{
    /// DXT1/BC1: one 64-bit RGB block per 4x4 texel tile, 1-bit alpha.
    BC1,
    /// DXT3/BC2: explicit 4-bit alpha (64 bits) + the BC1 colour block.
    BC2,
    /// DXT5/BC3: interpolated alpha (64 bits) + the BC1 colour block.
    BC3,
    /// BC4: single-channel interpolated, 64 bits per block.
    BC4,
    /// BC5: two single-channel interpolated blocks (normal maps), 128 bits.
    BC5
};

/// One block's byte size for the given format: 8 for BC1 and BC4, 16 for
/// the rest (BC2, BC3, BC5).
constexpr std::size_t blockSize(BlockFormat format) noexcept
{
    return (format == BlockFormat::BC1 || format == BlockFormat::BC4) ? 8u : 16u;
}

/// The decoded RGBA8 pixel count of one 4x4 block: 16 pixels, 4 bytes each.
constexpr std::size_t kDecodedBlockBytes = 4u * 4u * 4u;

/// The decoded image: width x height RGBA8 bytes, row-major, top to bottom,
/// no row padding — the layout every fill call in the render contract uses.
struct DecodedImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba8;
};

/// The row pitch of a compressed image: the 4x4 block columns per row times
/// the block size. Width and height must be multiples of 4 (the block
/// formats' constraint); a compressed image is validated, not silently
/// padded.
constexpr std::size_t compressedRowPitch(BlockFormat format, std::uint32_t width) noexcept
{
    return static_cast<std::size_t>(width / 4u) * blockSize(format);
}

/// The full compressed byte count of a validated image.
constexpr std::size_t compressedSize(BlockFormat format, std::uint32_t width,
                                     std::uint32_t height) noexcept
{
    return compressedRowPitch(format, width) * static_cast<std::size_t>(height / 4u);
}

/// Validates a compressed image's shape: dimensions positive, multiples of
/// 4, and byteCount exactly what the shape demands. Returns false instead
/// of throwing (the host decides between a fallback and an error).
bool isValidCompressedShape(BlockFormat format, std::uint32_t width, std::uint32_t height,
                            std::size_t byteCount) noexcept;

/// Decodes one 4x4 block into 16 RGBA8 pixels (64 bytes). Deterministic:
/// the same block decodes to the same pixels on every platform and in
/// every thread. The block's layout is the published one per format.
void decodeBlock(BlockFormat format, const std::uint8_t* block, std::uint8_t* outRgba8) noexcept;

/// Decodes a whole compressed image into RGBA8. Throws std::invalid_argument
/// when the shape is invalid (the same rule isValidCompressedShape checks).
DecodedImage decodeImage(BlockFormat format, std::uint32_t width, std::uint32_t height,
                         const std::uint8_t* data, std::size_t byteCount);

}  // namespace grace::core
