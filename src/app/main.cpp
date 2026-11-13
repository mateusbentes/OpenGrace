#include <charconv>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include "grace/app/application.hpp"
#include "grace/render/renderer.hpp"
#include "grace/render/null_render_backend.hpp"
#ifdef GRACE_HAVE_OPENGL
#include "grace/render/opengl_render_backend.hpp"
#endif
#ifdef GRACE_HAVE_VULKAN
#include "grace/render/vulkan_render_backend.hpp"
#endif
#ifdef GRACE_HAVE_METAL
#include "grace/render/metal_render_backend.hpp"
#endif

namespace {

struct Options
{
    bool headless = false;
    bool vulkan = false;
    bool metal = false;
    bool post = true;  // 6e: the post chain is the default (decision 23); --no-post opts out
    bool dof = false;   ///< enable the 6d depth-of-field post pass (with --post)
    bool mlaa = false;  ///< enable the 6d morphological AA post pass (with --post)
    std::string path = "forward";
    bool help = false;
    bool hidden = false;
    bool readback = false;
    bool verbose = false;
    std::uint64_t frames = 10;
    std::int64_t frame_time_ns = 33'333'333;  // ~30 fps
    std::int64_t step_ns = 16'666'666;         // ~60 Hz
    std::int64_t max_steps = 5;
    int swap_interval = 1;
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
};

void printUsage(std::FILE* out)
{
    std::fprintf(out,
                 "grace - OpenGrace application driver\n"
                 "\n"
                 "Usage: grace [options]\n"
                 "\n"
                 "Backend selection:\n"
                "  (default)           OpenGL backend, when built with GRACE_ENABLE_OPENGL=ON.\n"
               "  --vulkan            Vulkan backend, when built with GRACE_ENABLE_VULKAN=ON.\n"
                "  --metal             Metal backend, when built with GRACE_ENABLE_METAL=ON.\n"
                "  --headless          Null (headless) render backend.\n"
                 "  --post              Post chain (HDR, bloom, tonemap, SSAO, MLAA) — DEFAULT.\n"
                 "  --no-post           Legacy compatibility path: linear -> 8-bit, no chain.\n"
                 "  --dof               With the chain: depth of field (off by default).\n"
                 "  --mlaa              With the chain: morphological AA (on by default).\n"
                 "\n"
                 "Options:\n"
                 "  --frames <n>          Number of frames to run (default: 10).\n"
                 "  --frame-time-ns <ns>  Simulated frame time in nanoseconds\n"
                 "                        (default: 33333333).\n"
                 "  --step-ns <ns>        Fixed simulation step in nanoseconds\n"
                 "                        (default: 16666666).\n"
                 "  --max-steps <n>       Step cap per frame (default: 5).\n"
                 "  --width <px>          Frame width in pixels (default: 1280).\n"
                 "  --height <px>         Frame height in pixels (default: 720).\n"
                "  --hidden              Create the window hidden (OpenGL backend; Xvfb runs).\n"
                "  --readback            Read back the centre pixel before the buffer swap\n"
                "                        (OpenGL and Vulkan backends).\n"
                "  --swap-interval <n>   Buffer swap interval (OpenGL and Vulkan backends, default: 1).\n"
                 "  -v, --verbose         Print backend details (GL version, vendor, renderer).\n"
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
    std::fprintf(stderr, "grace: usage error: %s\n", message.c_str());
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
        else if (arg == "--vulkan")
        {
            options.vulkan = true;
        }
        else if (arg == "--metal")
        {
            options.metal = true;
        }
        else if (arg == "--post")
        {
            options.post = true;  // already the default; accepted for clarity
        }
        else if (arg == "--no-post")
        {
            options.post = false;  // legacy compatibility path (linear -> 8-bit)
        }
        else if (arg == "--dof")
        {
            options.dof = true;
        }
        else if (arg == "--mlaa")
        {
            options.mlaa = true;
        }
        else if (arg == "--path")
        {
            if (i + 1 >= argc)
                return usageError("--path requires a value");
            options.path = argv[++i];
        }
        else if (arg == "--hidden")
        {
            options.hidden = true;
        }
        else if (arg == "--readback")
        {
            options.readback = true;
        }
        else if (arg == "--verbose" || arg == "-v")
        {
            options.verbose = true;
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
        else if (arg == "--swap-interval")
        {
            if (i + 1 >= argc)
                return usageError("--swap-interval requires a value");
            if (!parseNumber(std::string_view{argv[++i]}, options.swap_interval))
                return usageError("--swap-interval expects an integer");
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

#ifndef GRACE_HAVE_OPENGL
    if (!options.headless && !options.vulkan && !options.metal)
        return usageError("no render backend selected; this build provides --headless only "
                          "(configure with -DGRACE_ENABLE_OPENGL=ON for the OpenGL backend)");
#endif
#ifndef GRACE_HAVE_VULKAN
    if (options.vulkan)
        return usageError("--vulkan requires a Vulkan build "
                          "(configure with -DGRACE_ENABLE_VULKAN=ON)");
#endif
#ifndef GRACE_HAVE_METAL
    if (options.metal)
        return usageError("--metal requires a Metal build "
                          "(configure with -DGRACE_ENABLE_METAL=ON)");
#endif

    if (options.vulkan && options.headless)
        return usageError("--vulkan and --headless are mutually exclusive");
    if (options.metal && options.headless)
        return usageError("--metal and --headless are mutually exclusive");

    // Deterministic clear colour for every run.
    constexpr grace::render::ClearColor kClearColor{0x1E, 0x1E, 0x1E, 0xFF};

    grace::render::RendererConfig rendererConfig;
    if (options.path == "forward")
        rendererConfig.path = grace::render::RenderPath::Forward;
    else if (options.path == "deferred")
        rendererConfig.path = grace::render::RenderPath::Deferred;
    else
        return usageError("--path expects 'forward' or 'deferred'");
    rendererConfig.clear_color = kClearColor;
    rendererConfig.post.enabled = options.post;  // default on (6e, decision 23)
    rendererConfig.post.dof.enabled = options.dof;   // 6d: off unless --dof
    rendererConfig.post.mlaa.enabled = options.mlaa; // 6d: off unless --mlaa

    grace::app::ApplicationConfig config;
    config.frame_ns = options.frame_time_ns;
    config.step = grace::core::FixedStepConfig{options.step_ns, options.max_steps};
    config.clear_color = kClearColor;

    try
    {
        std::unique_ptr<grace::render::RenderBackend> backend;
        grace::render::NullRenderBackend* null_backend = nullptr;
#ifdef GRACE_HAVE_VULKAN
        grace::render::VulkanRenderBackend* vulkan_backend = nullptr;
#endif
#ifdef GRACE_HAVE_METAL
        grace::render::MetalRenderBackend* metal_backend = nullptr;
#endif
#ifdef GRACE_HAVE_OPENGL
        grace::render::OpenGLRenderBackend* gl_backend = nullptr;
        if (!options.headless && !options.vulkan && !options.metal)
        {
            grace::render::OpenGLBackendConfig gl_config;
            gl_config.width = options.width;
            gl_config.height = options.height;
            gl_config.hidden = options.hidden;
            gl_config.readback_center = options.readback;
            gl_config.swap_interval = options.swap_interval;
            auto concrete = std::make_unique<grace::render::OpenGLRenderBackend>(gl_config);
            concrete->initialise();
            gl_backend = concrete.get();
            backend = std::move(concrete);
        }
        else
#endif
#ifdef GRACE_HAVE_VULKAN
        if (options.vulkan)
        {
            grace::render::VulkanBackendConfig vulkan_config;
            vulkan_config.width = options.width;
            vulkan_config.height = options.height;
            vulkan_config.hidden = options.hidden;
            vulkan_config.readback_center = options.readback;
            vulkan_config.swap_interval = options.swap_interval;
            auto concrete =
                std::make_unique<grace::render::VulkanRenderBackend>(vulkan_config);
            concrete->initialise();
            vulkan_backend = concrete.get();
            backend = std::move(concrete);
        }
        else
#endif
        {
#ifdef GRACE_HAVE_METAL
            if (options.metal)
            {
                grace::render::MetalBackendConfig metal_config;
                metal_config.width = options.width;
                metal_config.height = options.height;
                metal_config.hidden = options.hidden;
                metal_config.readback_center = options.readback;
                metal_config.swap_interval = options.swap_interval;
                auto concrete =
                    std::make_unique<grace::render::MetalRenderBackend>(metal_config);
                concrete->initialise();
                metal_backend = concrete.get();
                backend = std::move(concrete);
            }
            else
#endif
            {
                auto concrete =
                    std::make_unique<grace::render::NullRenderBackend>(options.width, options.height);
                null_backend = concrete.get();
                backend = std::move(concrete);
            }
        }

        grace::render::Renderer renderer(*backend);
        renderer.setConfig(rendererConfig);
        grace::app::Application app(config, renderer);
        const std::uint64_t total_steps = app.run(options.frames);

        std::printf("grace: path=%s backend=%s frame=%" PRIu32 "x%" PRIu32 "\n",
                    options.path.c_str(),
                    backend->describe().c_str(), backend->width(), backend->height());
        std::printf("grace: frames=%" PRIu64 " steps=%" PRIu64 " clamps=%" PRIu64 "\n",
                    options.frames, total_steps, app.clock().clampCount());
#ifdef GRACE_HAVE_OPENGL
        if (gl_backend != nullptr)
        {
            std::printf("grace: violations=%" PRIu64 " gl_errors=%" PRIu64 "\n",
                        gl_backend->violationCount(), gl_backend->glErrorCount());
            if (options.verbose)
            {
                std::printf("grace: detail: %s\n", gl_backend->describe().c_str());
                std::printf("grace: hidden=%d swap_interval=%d readback=%d\n",
                            static_cast<int>(options.hidden),
                            static_cast<int>(options.swap_interval),
                            static_cast<int>(options.readback));
            }
            if (options.readback && gl_backend->hasCenterPixel())
            {
                const grace::render::ClearColor pixel = gl_backend->centerPixel();
                std::printf("grace: center_pixel=%d,%d,%d,%d depth=%.6f stencil=%d\n",
                            static_cast<int>(pixel.r), static_cast<int>(pixel.g),
                            static_cast<int>(pixel.b), static_cast<int>(pixel.a),
                            static_cast<double>(gl_backend->centerDepth()),
                            static_cast<int>(gl_backend->centerStencil()));
            }
        }
        else
#endif
#ifdef GRACE_HAVE_VULKAN
        if (vulkan_backend != nullptr)
        {
            std::printf("grace: violations=%" PRIu64 "\n",
                        vulkan_backend->violationCount());
            if (options.verbose)
            {
                std::printf("grace: detail: %s\n", vulkan_backend->describe().c_str());
                std::printf("grace: hidden=%d swap_interval=%d readback=%d\n",
                            static_cast<int>(options.hidden),
                            static_cast<int>(options.swap_interval),
                            static_cast<int>(options.readback));
            }
            if (options.readback && vulkan_backend->hasCenterPixel())
            {
                const grace::render::ClearColor pixel = vulkan_backend->centerPixel();
                std::printf("grace: center_pixel=%d,%d,%d,%d depth=%.6f stencil=%d\n",
                            static_cast<int>(pixel.r), static_cast<int>(pixel.g),
                            static_cast<int>(pixel.b), static_cast<int>(pixel.a),
                            static_cast<double>(vulkan_backend->centerDepth()),
                            static_cast<int>(vulkan_backend->centerStencil()));
            }
        }
        else
#endif
        {
#ifdef GRACE_HAVE_METAL
            if (metal_backend != nullptr)
            {
                std::printf("grace: violations=%" PRIu64 "\n",
                            metal_backend->violationCount());
                if (options.verbose)
                {
                    std::printf("grace: detail: %s\n",
                                metal_backend->describe().c_str());
                    std::printf("grace: hidden=%d swap_interval=%d readback=%d\n",
                                static_cast<int>(options.hidden),
                                static_cast<int>(options.swap_interval),
                                static_cast<int>(options.readback));
                }
                if (options.readback && metal_backend->hasCenterPixel())
                {
                    const grace::render::ClearColor pixel =
                        metal_backend->centerPixel();
                    std::printf("grace: center_pixel=%d,%d,%d,%d depth=%.6f stencil=%d\n",
                                static_cast<int>(pixel.r),
                                static_cast<int>(pixel.g),
                                static_cast<int>(pixel.b),
                                static_cast<int>(pixel.a),
                                static_cast<double>(metal_backend->centerDepth()),
                                static_cast<int>(metal_backend->centerStencil()));
                }
            }
            else
#endif
            {
                std::printf("grace: violations=%" PRIu64 "\n",
                            null_backend->violationCount());
            }
        }

    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "grace: runtime failure: %s\n", error.what());
        return 1;
    }

    return 0;
}
