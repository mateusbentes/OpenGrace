// The Ogg Theora decoder: it links libtheora and lives with the
// adapter's sources (the SDK option). The decode never touches the
// filesystem: the Ogg sync layer reads over the byte span, and the
// caller receives the packed frame payload.
#include "grace/video/video_decode.hpp"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <ogg/ogg.h>
#include <theora/theoradec.h>
namespace grace {
namespace video {
namespace {
/// The byte span the Ogg sync layer reads over.
struct SpanCursor
{
    const std::uint8_t* data;
    std::size_t size;
    std::size_t offset;
};
/// The Ogg sync layer's read callback: forward from the cursor,
/// clamped to the remaining bytes.
size_t spanRead(void* dest, size_t elementSize, size_t elements,
                void* source)
{
    auto* cursor = static_cast<SpanCursor*>(source);
    const size_t requested = elementSize * elements;
    const size_t remaining = cursor->size - cursor->offset;
    const size_t take = std::min(requested, remaining);
    std::memcpy(dest, cursor->data + cursor->offset, take);
    cursor->offset += take;
    return take / elementSize;
}
}  // namespace
Decoded decodeTheora(const std::vector<std::uint8_t>& bytes)
{
    Decoded result;
    if (bytes.size() < 4u || std::memcmp(bytes.data(), "OggS", 4) != 0)
    {
        result.reason = "not an Ogg container";
        return result;
    }
    SpanCursor cursor{bytes.data(), bytes.size(), 0u};
    ogg_sync_state sync;
    ogg_sync_init(&sync);
    ogg_stream_state stream;
    bool streamOpen = false;
    th_info info;
    th_comment comment;
    th_setup_info* setup = nullptr;
    th_dec_ctx* decoder = nullptr;
    th_info_init(&info);
    th_comment_init(&comment);
    // The headers: the identification, the comment and the setup.
    int headersRemaining = 3;
    // The frames: the packed payload the decode fills.
    std::vector<std::uint8_t> frames;
    bool headersDone = false;
    bool failed = false;
    while (!failed && cursor.offset < bytes.size())
    {
        // The next page: the sync layer fills the buffer, the span's
        // read callback fills the buffer, the sync layer writes the
        // page out.
        char* buffer = ogg_sync_buffer(&sync, 4096);
        if (buffer == nullptr)
        {
            result.reason = "the Ogg sync layer rejected the buffer";
            failed = true;
            break;
        }
        const size_t filled = spanRead(buffer, 1u, 4096u, &cursor);
        if (filled == 0u)
        {
            break;
        }
        ogg_sync_wrote(&sync, static_cast<long>(filled));
        ogg_page page;
        while (ogg_sync_pageout(&sync, &page) == 1)
        {
            if (!streamOpen)
            {
                // The first page opens the stream.
                if (ogg_stream_init(&stream, ogg_page_serialno(&page))
                    != 0)
                {
                    result.reason = "the Ogg stream rejected the serial";
                    failed = true;
                    break;
                }
                streamOpen = true;
            }
            ogg_stream_pagein(&stream, &page);
            ogg_packet packet;
            while (ogg_stream_packetout(&stream, &packet) == 1)
            {
                if (!headersDone)
                {
                    const int header =
                        th_decode_headerin(&info, &comment, &setup,
                                           &packet);
                    if (header < 0)
                    {
                        result.reason = "the Theora header rejected the "
                                        "packet";
                        failed = true;
                        break;
                    }
                    --headersRemaining;
                    if (headersRemaining == 0)
                    {
                        headersDone = true;
                        decoder = th_decode_alloc(&info, setup);
                        if (decoder == nullptr)
                        {
                            result.reason = "the Theora decoder failed "
                                            "to allocate";
                            failed = true;
                        }
                    }
                    continue;
                }
                // The frame packet: the decode layer fills the packed
                // payload.
                if (th_decode_packetin(decoder, &packet, nullptr) != 0)
                {
                    result.reason = "the Theora frame rejected the "
                                    "packet";
                    failed = true;
                    break;
                }
                th_ycbcr_buffer plane;
                if (th_decode_ycbcr_out(decoder, plane) != 0)
                {
                    result.reason = "the Theora frame rejected the "
                                    "output";
                    failed = true;
                    break;
                }
                // The packed payload: the luma plane, then the chroma
                // planes, at half the luma resolution.
                const std::uint32_t width = info.frame_width;
                const std::uint32_t height = info.frame_height;
                const std::uint32_t chromaWidth = width / 2u;
                const std::uint32_t chromaHeight = height / 2u;
                for (std::uint32_t row = 0u; row < height; ++row)
                {
                    const std::uint8_t* src =
                        plane[0].data + static_cast<std::size_t>(row)
                        * static_cast<std::size_t>(plane[0].stride);
                    frames.insert(frames.end(), src, src + width);
                }
                for (std::uint32_t row = 0u; row < chromaHeight; ++row)
                {
                    const std::uint8_t* src =
                        plane[1].data + static_cast<std::size_t>(row)
                        * static_cast<std::size_t>(plane[1].stride);
                    frames.insert(frames.end(), src, src + chromaWidth);
                }
                for (std::uint32_t row = 0u; row < chromaHeight; ++row)
                {
                    const std::uint8_t* src =
                        plane[2].data + static_cast<std::size_t>(row)
                        * static_cast<std::size_t>(plane[2].stride);
                    frames.insert(frames.end(), src, src + chromaWidth);
                }
                ++result.frameCount;
            }
        }
    }
    // The decode: the frame rate and the frame count come from the
    // file's headers.
    if (info.fps_denominator != 0)
    {
        result.frameRate = info.fps_numerator / info.fps_denominator;
    }
    result.width = info.frame_width;
    result.height = info.frame_height;
    result.frames = std::move(frames);
    if (result.frameCount == 0u)
    {
        result.reason = "the Ogg stream carried no frames";
        failed = true;
    }
    if (!failed)
    {
        result.ok = true;
    }
    // The decoder state.
    if (decoder != nullptr)
    {
        th_decode_free(decoder);
    }
    if (setup != nullptr)
    {
        th_setup_free(setup);
    }
    th_comment_clear(&comment);
    th_info_clear(&info);
    if (streamOpen)
    {
        ogg_stream_clear(&stream);
    }
    ogg_sync_clear(&sync);
    return result;
}
}  // namespace video
}  // namespace grace
