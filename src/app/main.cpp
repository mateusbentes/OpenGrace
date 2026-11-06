#include <charconv>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <system_error>

#include "grace/core/application.hpp"
#include "grace/render/null_render_backend.hpp"

namespace {

struct Options
{
    bool headless = false;
    bool help = false;
    std::uint64_t frames = 10;
    std::int64_t frame_time_ns = 33'333'333;  // ~30 fps
    std::int64_t step_ns = 16'666'666;         // ~60 Hz
    std::int64_t max_steps = 5;
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
};

void printUsage(std::FILE* out)
{
    std::fprintf(out,
                 "grace_app - OpenGrace headless application driver\n"
                 "\n"
                 "Usage: grace_app --headless [options]\n"
                 "\n"
                 "Options:\n"
                 "  --headless            Use the null (headless) render backend.\n"
                 "  --frames <n>          Number of frames to run (default: 10).\n"
                 "  --frame-time-ns <ns>  Simulated frame time in nanoseconds\n"
                 "                        (default: 33333333).\n"
                 "  --step-ns <ns>        Fixed simulation step in nanoseconds\n"
                 "                        (default: 16666666).\n"
                 "  --max-steps <n>       Step cap per frame (default: 5).\n"
                 "  --width <px>          Frame width in pixels (default: 1280).\n"
                 "  --height <px>         Frame height in pixels (default: 720).\n"
                 "  -h, --help            Print this help and exit.\n"
                 "\n"
                 "Exit codes: 0 success, 1 runtime failure, 2 usage error.\n");
}

template <typename T>
bool parseNumber(std::string_view text, T& out)
{
    const char* const first = text.data();
    const char* const last = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(first, last, out);
    return result.ec == std::errc{} && result.ptr == last;
}

int usageError(const std::string& message)
{
    std::fprintf(stderr, "grace_app: usage error: %s\n", message.c_str());
    printUsage(stderr);
    return 2;
}

}  // namespace

int main(int argc, char** argv)
{
    Options options;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg{argv[i]};

        if (arg == "--help" || arg == "-h")
        {
            options.help = true;
        }
        else if (arg == "--headless")
        {
            options.headless = true;
        }
        else if (arg == "--frames")
        {
            if (i + 1 >= argc)
                return usageError("--frames requires a value");
            if (!parseNumber(std::string_view{argv[++i]}, options.frames))
                return usageError("--frames expects an unsigned integer");
        }
        else if (arg == "--frame-time-ns")
        {
            if (i + 1 >= argc)
                return usageError("--frame-time-ns requires a value");
            if (!parseNumber(std::string_view{argv[++i]}, options.frame_time_ns))
                return usageError("--frame-time-ns expects an integer");
        }
        else if (arg == "--step-ns")
        {
            if (i + 1 >= argc)
                return usageError("--step-ns requires a value");
            if (!parseNumber(std::string_view{argv[++i]}, options.step_ns))
                return usageError("--step-ns expects an integer");
        }
        else if (arg == "--max-steps")
        {
            if (i + 1 >= argc)
                return usageError("--max-steps requires a value");
            if (!parseNumber(std::string_view{argv[++i]}, options.max_steps))
                return usageError("--max-steps expects an integer");
        }
        else if (arg == "--width")
        {
            if (i + 1 >= argc)
                return usageError("--width requires a value");
            if (!parseNumber(std::string_view{argv[++i]}, options.width))
                return usageError("--width expects an unsigned integer");
        }
        else if (arg == "--height")
        {
            if (i + 1 >= argc)
                return usageError("--height requires a value");
            if (!parseNumber(std::string_view{argv[++i]}, options.height))
                return usageError("--height expects an unsigned integer");
        }
        else
        {
            return usageError("unknown option '" + std::string{arg} + "'");
        }
    }

    if (options.help)
    {
        printUsage(stdout);
        return 0;
    }

    if (!options.headless)
        return usageError("no render backend selected; this build provides --headless only");

    // Deterministic clear colour for headless runs.
    constexpr grace::render::ClearColor kClearColor{0x1E, 0x1E, 0x1E, 0xFF};

    grace::core::ApplicationConfig config;
    config.frame_ns = options.frame_time_ns;
    config.step = grace::core::FixedStepConfig{options.step_ns, options.max_steps};
    config.clear_color = kClearColor;

    try
    {
        grace::render::NullRenderBackend backend(options.width, options.height);
        grace::core::Application app(config, backend);

        const std::uint64_t total_steps = app.run(options.frames);

        std::printf("grace_app: backend=%s frame=%" PRIu32 "x%" PRIu32 "\n",
                    backend.describe().c_str(), backend.width(), backend.height());
        std::printf("grace_app: frames=%" PRIu64 " steps=%" PRIu64 " clamps=%" PRIu64
                    " violations=%" PRIu64 "\n",
                    options.frames, total_steps, app.clock().clampCount(),
                    backend.violationCount());
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "grace_app: runtime failure: %s\n", error.what());
        return 1;
    }

    return 0;
}
