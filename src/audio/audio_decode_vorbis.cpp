// The Ogg Vorbis decoder: it links libvorbisfile and lives with the
// adapter's sources (the SDK option). The decode never touches the
// filesystem: the read callbacks run over the byte span, and the caller
// receives PCM samples.
#include "grace/audio/audio_decode.hpp"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <vorbis/vorbisfile.h>
namespace grace {
namespace audio {
Decoded decodeVorbis(const std::vector<std::uint8_t>& bytes)
{
    Decoded result;
    if (bytes.size() < 4u || std::memcmp(bytes.data(), "OggS", 4) != 0)
    {
        result.reason = "not an Ogg container";
        return result;
    }
    // libvorbisfile decodes the whole stream through the read callbacks
    // over the byte span: the identification and the setup headers, then
    // every page, into one PCM payload.
    struct SpanCursor
    {
        const std::uint8_t* data;
        std::size_t size;
        std::size_t offset;
    };
    SpanCursor cursor{bytes.data(), bytes.size(), 0u};
    const ov_callbacks callbacks = {
        // read: forward from the cursor, clamped to the remaining bytes.
        [](void* dest, size_t elementSize, size_t elements, void* source)
            -> size_t {
            auto* c = static_cast<SpanCursor*>(source);
            const size_t requested = elementSize * elements;
            const size_t remaining = c->size - c->offset;
            const size_t take = std::min(requested, remaining);
            std::memcpy(dest, c->data + c->offset, take);
            c->offset += take;
            return take;
        },
        // seek: the absolute position, clamped to the size.
        [](void* source, ogg_int64_t offset, int whence) -> int {
            auto* c = static_cast<SpanCursor*>(source);
            std::size_t target = c->offset;
            if (whence == SEEK_SET)
            {
                target = static_cast<std::size_t>(offset);
            }
            else if (whence == SEEK_CUR)
            {
                target = static_cast<std::size_t>(
                    static_cast<ogg_int64_t>(c->offset) + offset);
            }
            else if (whence == SEEK_END)
            {
                target = static_cast<std::size_t>(
                    static_cast<ogg_int64_t>(c->size) + offset);
            }
            c->offset = std::min(target, c->size);
            return 0;
        },
        // close: the span is the caller's.
        [](void*) -> int { return 0; },
        // tell: the cursor's offset.
        [](void* source) -> long {
            auto* c = static_cast<SpanCursor*>(source);
            return static_cast<long>(c->offset);
        }};
    OggVorbis_File file;
    std::memset(&file, 0, sizeof(file));
    if (ov_open_callbacks(&cursor, &file, nullptr, 0, callbacks) < 0)
    {
        result.reason = "not an Ogg Vorbis stream";
        return result;
    }
    const vorbis_info* info = ov_info(&file, -1);
    if (!info)
    {
        ov_clear(&file);
        result.reason = "no Vorbis identification header";
        return result;
    }
    result.sampleRate = static_cast<std::uint32_t>(info->rate);
    result.format = info->channels == 1 ? SampleFormat::Mono16
                                        : SampleFormat::Stereo16;
    // The whole stream: ov_read returns 16-bit signed little-endian
    // samples in the file's channel order.
    std::vector<std::uint8_t> pcm;
    char buffer[4096];
    int bitstream = 0;
    for (;;)
    {
        const long read = ov_read(&file, buffer, sizeof(buffer), 0, 2, 1,
                                  &bitstream);
        if (read == 0)
        {
            break;
        }
        if (read < 0)
        {
            ov_clear(&file);
            result.reason = "Vorbis decode error";
            return result;
        }
        pcm.insert(pcm.end(), buffer, buffer + read);
    }
    ov_clear(&file);
    result.samples = std::move(pcm);
    result.ok = true;
    return result;
}
}  // namespace audio
}  // namespace grace
