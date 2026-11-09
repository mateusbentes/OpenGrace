// The audio module's data types: the validation of the descriptors and
// the format helpers. The validation functions return the empty string
// when the descriptor is valid, or the reason when it is not.
#include "grace/audio/audio_types.hpp"
namespace grace {
namespace audio {
std::uint32_t channelCount(SampleFormat format)
{
    switch (format)
    {
    case SampleFormat::Mono8:
    case SampleFormat::Mono16:
        return 1u;
    case SampleFormat::Stereo8:
    case SampleFormat::Stereo16:
        return 2u;
    }
    return 1u;
}
std::uint32_t bytesPerFrame(SampleFormat format)
{
    // One frame is one sample per channel; the 16-bit formats carry two
    // bytes per sample.
    const std::uint32_t bytesPerSample = (format == SampleFormat::Mono16 ||
                                          format == SampleFormat::Stereo16)
                                             ? 2u
                                             : 1u;
    return bytesPerSample * channelCount(format);
}
std::uint32_t frameCount(const std::vector<std::uint8_t>& samples,
                         SampleFormat format)
{
    const std::uint32_t frameSize = bytesPerFrame(format);
    if (frameSize == 0u || samples.size() % frameSize != 0u)
    {
        return 0u;
    }
    return static_cast<std::uint32_t>(samples.size() / frameSize);
}
std::string validate(const SoundDesc& desc)
{
    if (desc.samples.empty())
    {
        return "sound has no samples";
    }
    if (desc.sampleRate == 0u)
    {
        return "sound has a zero sample rate";
    }
    if (frameCount(desc.samples, desc.format) == 0u)
    {
        return "sound payload does not divide into frames";
    }
    return {};
}
std::string validate(const VoiceDesc& desc)
{
    if (desc.gain < 0.0f || desc.gain > 1.0f)
    {
        return "voice gain is outside the unit range";
    }
    if (desc.pitch <= 0.0f)
    {
        return "voice pitch is not positive";
    }
    if (desc.referenceDistance <= 0.0f)
    {
        return "voice reference distance is not positive";
    }
    if (desc.maxDistance < desc.referenceDistance)
    {
        return "voice max distance is below the reference distance";
    }
    return {};
}
std::string validate(const ReverbDesc& desc)
{
    if (desc.decay < 0.0f)
    {
        return "reverb decay is negative";
    }
    if (desc.damping < 0.0f || desc.damping > 1.0f)
    {
        return "reverb damping is outside the unit range";
    }
    if (desc.wetGain < 0.0f || desc.wetGain > 1.0f)
    {
        return "reverb wet gain is outside the unit range";
    }
    return {};
}
std::string validate(const StreamDesc& desc)
{
    if (desc.sampleRate == 0u)
    {
        return "stream has a zero sample rate";
    }
    if (desc.chunkSamples == 0u)
    {
        return "stream has a zero chunk size";
    }
    return {};
}
}  // namespace audio
}  // namespace grace
