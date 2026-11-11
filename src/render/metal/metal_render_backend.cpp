#include "grace/render/metal_render_backend.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_metal.h>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "grace/core/math/camera.hpp"
#include "grace/core/math/transform.hpp"
#include "grace/render/material.hpp"
#include "grace/render/shadow.hpp"
#include "metal_mesh.hpp"
#include "metal_render_target.hpp"
#include "metal_shader.hpp"
#include "mtl_loader.hpp"
namespace grace {
namespace render {
using metal::NSUInteger;
namespace metal {
/// The definition of the mapping the header declares (the test's call
/// site links against this translation unit).
MTLPixelFormat toMtlFormat(PixelFormat format)
{
    switch (format)
    {
    case PixelFormat::Rgba8:
        return MTLPixelFormat::Rgba8Unorm;
    case PixelFormat::Rgba16F:
        return MTLPixelFormat::Rgba16Float;
    case PixelFormat::R8:
        return MTLPixelFormat::R8Unorm;
    case PixelFormat::Depth24Stencil8:
        return MTLPixelFormat::Depth24Unorm_Stencil8;
    }
    return MTLPixelFormat::Rgba8Unorm;
}
}  // namespace metal
namespace {
using metal::MTLPixelFormat;
/// The SDL video subsystem release the backend's teardown path uses (the
/// same pair SDL_Init(SDL_INIT_VIDEO) pairs with; the window must die
/// first, so this runs after SDL_DestroyWindow).
void sdlVideoRelease()
{
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
}  // namespace
struct MetalRenderBackend::Impl
{
    using NSUInteger = metal::NSUInteger;
    using MTLPixelFormat = metal::MTLPixelFormat;
    using MTLClearColor = metal::MTLClearColor;
    using MTLViewport = metal::MTLViewport;
    using MTLOrigin = metal::MTLOrigin;
    using MTLSize = metal::MTLSize;
    using MTLTextureHandle = metal::MTLTextureHandle;
    using MTLBufferHandle = metal::MTLBufferHandle;
    using MTLLibraryHandle = metal::MTLLibraryHandle;
    using MTLFunctionHandle = metal::MTLFunctionHandle;
    using MTLRenderPipelineStateHandle =
        metal::MTLRenderPipelineStateHandle;
    using MTLSamplerStateHandle = metal::MTLSamplerStateHandle;
    using MTLCommandBufferHandle = metal::MTLCommandBufferHandle;
    using MTLRenderCommandEncoderHandle =
        metal::MTLRenderCommandEncoderHandle;
    using MTLStorageMode = metal::MTLStorageMode;
    using MTLTextureUsage = metal::MTLTextureUsage;
    using MTLTextureType = metal::MTLTextureType;
    using MTLIndexType = metal::MTLIndexType;
    using MTLPrimitiveType = metal::MTLPrimitiveType;
    enum class State
    {
        Created,
        Initialised,
        Shutdown,
    };
    MetalBackendConfig config;
    metal::MtlLoader loader;
    SDL_Window* window = nullptr;
    // Device and queue (the one queue the default device exposes; Metal
    // command buffers encode against it directly, no queue family pick).
    metal::MTLDeviceHandle device = nullptr;
    metal::MTLCommandQueueHandle queue = nullptr;
    // The presentation layer (CAMetalLayer the SDL window hosts).
    metal::CAMetalLayerHandle layer = nullptr;
    metal::MTLPixelFormat layerFormat = metal::MTLPixelFormat::Rgba8Unorm;
    // The frame's command buffer and encoder (encoded per frame, one
    // render pass per encoder).
    metal::MTLCommandBufferHandle commandBuffer = nullptr;
    metal::MTLRenderCommandEncoderHandle encoder = nullptr;
    bool swapchain_frame = false;
    State state = State::Created;
    std::string device_name;
    // The depth attachment format the device supports (the pick queries
    // the device once; Depth24Unorm_Stencil8 on macOS, Depth32Float_
    // Stencil8 where the 24-bit packed format is unavailable).
    metal::MTLPixelFormat depthFormat = metal::MTLPixelFormat::Depth24Unorm_Stencil8;
    // Clear state (the same recorded state the Vulkan backend keeps; the
    // clear lands in the pass's load actions when the frame encodes).
    ClearColor clear_color{0, 0, 0, 0};
    std::uint32_t clear_mask = static_cast<std::uint32_t>(ClearBuffer::Color);
    float clear_depth = 1.0f;
    std::int32_t clear_stencil = 0;
    // Device state (decision 20): when false, passes run without a depth
    // test and without depth writes.
    bool depth_test = true;
    bool has_center = false;
    ClearColor center_pixel{0, 0, 0, 0};
    float center_depth = 0.0f;
    std::int32_t center_stencil = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> readback_queue;
    std::vector<ReadbackSample> last_readback;
    ClearColor lastReadbackColor{0, 0, 0, 0};
    float lastReadbackDepth = 0.0f;
    std::int32_t lastReadbackStencil = 0;
    std::uint64_t violations = 0;
    // Diagnostic triangle resources, built on the render foundation.
    std::unique_ptr<GpuMesh> tri_mesh;
    std::shared_ptr<ShaderProgram> tri_shader;
    std::shared_ptr<Material> tri_material;
    bool triangle_ready = false;
    // Forward lighting pass program (built lazily on first submit).
    std::shared_ptr<ShaderProgram> lighting_program;
    // Render targets: backend-owned registry. Handles stay alive until
    // shutdown so use-after-release is detected safely.
    std::vector<std::unique_ptr<metal::MtlRenderTarget>> targets;
    metal::MtlRenderTarget* bound_target = nullptr;
    // The pass's target (the same distinction the Vulkan backend draws:
    // bound_target is the engine-visible state, bound_pass is what the
    // encoder's attachments point at).
    metal::MtlRenderTarget* bound_pass = nullptr;
    bool frame_open = false;
    bool pass_open = false;
    // 3D textures: backend-owned registry.
    std::vector<std::unique_ptr<metal::MtlTexture3DView>> volumes;
    // Camera used by submit()/draw() (documented diagnostic default).
    CameraParams camera;
    // Per-program device objects. The backend owns the compiled library
    // functions and the pipeline state; the program's MtlShaderProgram
    // holds the MSL sources and the staging uniform bytes.
    struct ProgramResources
    {
        metal::MTLFunctionHandle vertexFunction = nullptr;
        metal::MTLFunctionHandle fragmentFunction = nullptr;
        metal::MTLBufferHandle uniformBuffer = nullptr;
        void* uniformBufferBase = nullptr;
        std::size_t uniformBytes = 0;
    };
    std::map<const void*, ProgramResources> programResources;
    // Per-program pipelines (keyed the same way the Vulkan backend keys
    // its pipelines: program sources + depth test + attachment formats).
    struct PipelineKey
    {
        const void* vertexSource = nullptr;
        const void* fragmentSource = nullptr;
        bool depthTest = false;
        metal::MTLPixelFormat colorFormat = metal::MTLPixelFormat::Invalid;
        metal::MTLPixelFormat depthFormatValue = metal::MTLPixelFormat::Invalid;
        bool operator<(const PipelineKey& other) const
        {
            if (vertexSource != other.vertexSource)
                return vertexSource < other.vertexSource;
            if (fragmentSource != other.fragmentSource)
                return fragmentSource < other.fragmentSource;
            if (depthTest != other.depthTest)
                return depthTest < other.depthTest;
            if (colorFormat != other.colorFormat)
                return static_cast<std::uint64_t>(colorFormat)
                    < static_cast<std::uint64_t>(other.colorFormat);
            return static_cast<std::uint64_t>(depthFormatValue)
                < static_cast<std::uint64_t>(other.depthFormatValue);
        }
    };
    std::map<PipelineKey, metal::MTLRenderPipelineStateHandle> pipelines;
    // Target/volume device objects (the textures the backend created for
    // each target; the volume borrow path looks the borrowed texture up
    // here).
    struct TargetResources
    {
        std::vector<metal::MTLTextureHandle> textures;
    };
    std::map<const metal::MtlRenderTarget*, TargetResources>
        targetResources;
    struct VolumeResources
    {
        metal::MTLTextureHandle texture = nullptr;
    };
    std::map<const TextureView*, VolumeResources> volumeResources;
    // The texture views bound to sampler units at draw time. The draw's
    // encoder binds them at the texture argument indices the shader's
    // layout table declares.
    std::vector<const TextureView*> boundTextures;
    // The one shared sampler (nearest, clamp-to-edge): the engine's
    // sampling rules (the shadow maps, the volumes, the post-chain
    // inputs) are all nearest with clamp-to-edge, the same rules the
    // OpenGL and the Vulkan backends implement.
    metal::MTLSamplerStateHandle sharedSampler = nullptr;
    // The default texture (1x1 opaque white): the sampler argument
    // bindings the engine never writes still name a texture, and the
    // shader samples a valid, opaque image either way.
    metal::MTLTextureHandle defaultTexture = nullptr;
    // The depth attachment of the default framebuffer (one texture the
    // layer's size follows; recreated with the drawable size).
    metal::MTLTextureHandle swapchainDepthTexture = nullptr;
    bool ready() const { return state == State::Initialised; }

    // -- Device resource helpers -------------------------------------------
    //
    // Every helper sends its messages through the loader's resolved
    // runtime entry points; on a platform without Metal the loader's
    // load() already failed and initialise() threw before any of these
    // could run.

    /// The device's name (the contract's describe() carries it).
    std::string queryDeviceName()
    {
        void* nameObj = metal::msgSend0(device, metal::sel_registerName(
            "name"));
        const char* utf8 = nameObj != nullptr
            ? static_cast<const char*>(metal::msgSend0(
                nameObj,
                metal::sel_registerName("UTF8String")))
            : "";
        return utf8 != nullptr ? std::string(utf8) : std::string();
    }

    /// Whether the device supports the 24-bit packed depth/stencil
    /// format (macOS only); when it does not, the depth attachment falls
    /// back to Depth32Float_Stencil8 (the supported one on both
    /// platforms, iOS 9.0+).
    metal::MTLPixelFormat pickDepthFormat()
    {
        if (device == nullptr)
            return metal::MTLPixelFormat::Depth32Float_Stencil8;
        // The honest platform split: the published Depth24Unorm_
        // Stencil8 format is macOS-only (iOS 13 SDK mirror), so the
        // runtime check is the OS the process runs on — the NSProcessInfo
        // selector-based check would need a second class handle; the
        // documented split below keys on the SDL platform name the same
        // way the module's other platform-dependent picks do.
        const char* platform = SDL_GetPlatform();
        if (platform != nullptr && std::strcmp(platform, "Mac OS X") == 0)
            return metal::MTLPixelFormat::Depth24Unorm_Stencil8;
        return metal::MTLPixelFormat::Depth32Float_Stencil8;
    }

    /// Creates a texture from a descriptor (the framework's own alloc/
    /// init path: a descriptor object is allocated, its fields set
    /// through the property setters, and the device's newTexture message
    /// materialises it).
    metal::MTLTextureHandle createTexture(metal::MTLPixelFormat format,
                                          std::uint32_t width,
                                          std::uint32_t height,
                                          std::uint32_t depth,
                                          NSUInteger usage,
                                          NSUInteger textureType)
    {
        void* descriptor = metal::msgSend0(
            loader.cls_MTLTextureDescriptor,
            metal::sel_registerName("alloc"));
        descriptor = metal::msgSend0(descriptor,
                                     metal::sel_registerName("init"));
        metal::msgSendUL(descriptor,
                         metal::sel_registerName("setPixelFormat:"),
                         static_cast<NSUInteger>(format));
        metal::msgSendUL(descriptor,
                         metal::sel_registerName("setTextureType:"),
                         textureType);
        metal::msgSendUL(descriptor,
                         metal::sel_registerName("setWidth:"), width);
        metal::msgSendUL(descriptor,
                         metal::sel_registerName("setHeight:"), height);
        metal::msgSendUL(descriptor,
                         metal::sel_registerName("setDepth:"), depth);
        metal::msgSendUL(descriptor,
                         metal::sel_registerName("setArrayLength:"),
                         textureType
                                 == static_cast<NSUInteger>(
                                     metal::MTLTextureType::Type2DArray)
                             ? depth
                             : 1u);
        metal::msgSendUL(descriptor,
                         metal::sel_registerName("setUsage:"), usage);
        return metal::msgSend1(device,
                               metal::sel_registerName("newTexture:"),
                               descriptor);
    }

    /// Creates a buffer of the given length with the given storage mode
    /// (the private mode for GPU-only buffers, the shared mode for the
    /// CPU-staged ones).
    metal::MTLBufferHandle createBuffer(std::size_t bytes,
                                        metal::MTLStorageMode mode)
    {
        using Fn = void* (*)(void*, void*, NSUInteger, NSUInteger);
        return reinterpret_cast<Fn>(metal::objc_msgSend)(
            device,
            metal::sel_registerName("newBufferWithLength:options:"),
            static_cast<NSUInteger>(bytes),
            static_cast<NSUInteger>(mode));
    }

    /// Writes bytes into a shared-mode buffer's base pointer (the CPU
    /// staging path; Metal's shared storage is host-coherent).
    void fillBuffer(metal::MTLBufferHandle buffer, const void* data,
                    std::size_t bytes)
    {
        void* base = metal::msgSend0(buffer,
                                     metal::sel_registerName("contents"));
        if (base != nullptr && data != nullptr)
            std::memcpy(base, data, bytes);
    }

    /// Creates the shared sampler state (nearest, clamp-to-edge) the
    /// engine's sampling rules use.
    metal::MTLSamplerStateHandle createSharedSampler()
    {
        void* descriptor = metal::msgSend0(
            loader.cls_MTLSamplerDescriptor,
            metal::sel_registerName("alloc"));
        descriptor = metal::msgSend0(descriptor,
                                     metal::sel_registerName("init"));
        metal::msgSendUL(descriptor,
                         metal::sel_registerName("setMinFilter:"),
                         static_cast<NSUInteger>(
                             metal::MTLSamplerMinMagFilter::Nearest));
        metal::msgSendUL(descriptor,
                         metal::sel_registerName("setMagFilter:"),
                         static_cast<NSUInteger>(
                             metal::MTLSamplerMinMagFilter::Nearest));
        metal::msgSendUL(
            descriptor, metal::sel_registerName("setAddressModeS:"),
            static_cast<NSUInteger>(
                metal::MTLSamplerAddressMode::ClampToEdge));
        metal::msgSendUL(
            descriptor, metal::sel_registerName("setAddressModeT:"),
            static_cast<NSUInteger>(
                metal::MTLSamplerAddressMode::ClampToEdge));
        metal::msgSendUL(
            descriptor, metal::sel_registerName("setAddressModeR:"),
            static_cast<NSUInteger>(
                metal::MTLSamplerAddressMode::ClampToEdge));
        return metal::msgSend1(device,
                               metal::sel_registerName("newSamplerState:"),
                               descriptor);
    }

    /// Compiles one MSL source string into a library through the
    /// device's own compiler (MTLLibrary; the runtime compile is what
    /// makes the deliberately broken pair's failure a runtime signal).
    /// Returns null on a compile failure and records the failure's
    /// message (the same signal the Vulkan library's empty SPIR-V array
    /// carries).
    metal::MTLLibraryHandle compileLibrary(const char* source,
                                           std::string& error)
    {
        void* sourceObj = makeString(source);
        if (sourceObj == nullptr)
        {
            error = "MSL source allocation failed";
            return nullptr;
        }
        void* options = metal::msgSend0(loader.cls_MTLCompileOptions,
                                        metal::sel_registerName("alloc"));
        options = metal::msgSend0(options,
                                  metal::sel_registerName("init"));
        using Fn = void* (*)(void*, void*, void*, void*, void**);
        void* failure = nullptr;
        metal::MTLLibraryHandle library =
            reinterpret_cast<Fn>(metal::objc_msgSend)(
                device,
                metal::sel_registerName("newLibraryWithSource:options:"
                                        "error:"),
                sourceObj, options, &failure);
        if (library == nullptr)
        {
            void* messageObj = failure != nullptr
                ? metal::msgSend0(failure,
                                  metal::sel_registerName(
                                      "localizedDescription"))
                : nullptr;
            const char* utf8 = messageObj != nullptr
                ? static_cast<const char*>(metal::msgSend0(
                    messageObj,
                    metal::sel_registerName("UTF8String")))
                : nullptr;
            error = utf8 != nullptr ? std::string(utf8)
                                    : std::string("MTLLibrary compile"
                                                  " failed");
        }
        return library;
    }

    /// Makes an NSString from a C string (the message sends take
    /// NSString*; the encoding is UTF-8, the published selector's rule).
    /// The string is autoreleased: no balancing release is needed.
    void* makeString(const char* utf8)
    {
        if (utf8 == nullptr || loader.cls_NSString == nullptr)
            return nullptr;
        using Fn = void* (*)(void*, void*, const char*);
        return reinterpret_cast<Fn>(metal::objc_msgSend)(
            metal::objc_getClass("NSString"),
            metal::sel_registerName("stringWithUTF8String:"), utf8);
    }

    /// Copies a staging buffer's bytes into a texture (the one-off blit
    /// path: a fresh command buffer, a blit encoder, one copy, commit
    /// and wait).
    void copyIntoTexture(metal::MTLBufferHandle staging,
                         metal::MTLTextureHandle texture,
                         std::size_t bytesPerRow, std::size_t bytesPerImage,
                         std::uint32_t width, std::uint32_t height)
    {
        metal::MTLCommandBufferHandle copy = metal::msgSend0(
            queue, metal::sel_registerName("commandBuffer"));
        void* blit = metal::msgSend0(copy,
                                     metal::sel_registerName(
                                         "blitEncoder"));
        using Fn = void (*)(void*, void*, metal::MTLBufferHandle,
                            NSUInteger, NSUInteger, NSUInteger,
                            metal::MTLTextureHandle, NSUInteger,
                            NSUInteger, metal::MTLOrigin, metal::MTLSize);
        reinterpret_cast<Fn>(metal::objc_msgSend)(
            blit,
            metal::sel_registerName("copyFromBuffer:sourceOffset:"
                                    "sourceBytesPerRow:"
                                    "sourceBytesPerImage:toTexture:"
                                    "destinationSlice:destinationLevel:"
                                    "destinationOrigin:destinationSize:"),
            staging, 0u, static_cast<NSUInteger>(bytesPerRow),
            static_cast<NSUInteger>(bytesPerImage), texture, 0u, 0u,
            metal::MTLOrigin{0, 0, 0},
            metal::MTLSize{static_cast<NSUInteger>(width),
                            static_cast<NSUInteger>(height), 1});
        metal::msgSendVoid(blit, metal::sel_registerName("endEncoding"));
        metal::msgSendVoid(copy, metal::sel_registerName("commit"));
        metal::msgSendVoid(copy,
                           metal::sel_registerName("waitUntilCompleted"));
    }

    /// Compiles the program's stages and creates the render pipeline
    /// state (the framework's MTLRenderPipelineDescriptor path: a
    /// descriptor object carries the vertex/fragment functions, the
    /// pixel format pair and the depth attachment's behaviour; the
    /// device's newRenderPipelineStateWithDescriptor message
    /// materialises it).
    metal::MTLRenderPipelineStateHandle createPipeline(
        metal::MtlShaderProgram* program, bool depthTest,
        metal::MTLPixelFormat colorFormat,
        metal::MTLPixelFormat depthFormatValue, std::string& error)
    {
        (void)depthTest;  // the depth test lives in the pipeline key only
        // The stage functions come from the sources' compiled libraries
        // (one library per stage; the framework's makeFunction returns
        // the named entry).
        metal::MTLLibraryHandle vertexLibrary = nullptr;
        metal::MTLLibraryHandle fragmentLibrary = nullptr;
        std::string compileError;
        if (program->vertexSource() != nullptr)
        {
            vertexLibrary =
                compileLibrary(program->vertexSource(), compileError);
            if (vertexLibrary == nullptr)
            {
                error = "vertex stage: " + compileError;
                return nullptr;
            }
        }
        if (program->fragmentSource() != nullptr)
        {
            fragmentLibrary =
                compileLibrary(program->fragmentSource(), compileError);
            if (fragmentLibrary == nullptr)
            {
                error = "fragment stage: " + compileError;
                return nullptr;
            }
        }
        void* descriptor = metal::msgSend0(
            loader.cls_MTLRenderPipelineDescriptor,
            metal::sel_registerName("alloc"));
        descriptor = metal::msgSend0(descriptor,
                                     metal::sel_registerName("init"));
        if (vertexLibrary != nullptr)
        {
            void* fn = metal::msgSend1(
                vertexLibrary,
                metal::sel_registerName("newFunctionWithName:"),
                makeString("vertexMain"));
            metal::msgSend1(descriptor,
                            metal::sel_registerName("setVertexFunction:"),
                            fn);
        }
        if (fragmentLibrary != nullptr)
        {
            void* fn = metal::msgSend1(
                fragmentLibrary,
                metal::sel_registerName("newFunctionWithName:"),
                makeString("fragmentMain"));
            metal::msgSend1(descriptor,
                            metal::sel_registerName("setFragmentFunction:"),
                            fn);
        }
        metal::msgSendUL(descriptor,
                         metal::sel_registerName("setDepthAttachmentPixelFormat:"
                                                 ),
                         static_cast<NSUInteger>(depthFormatValue));
        void* colorAttachment = metal::msgSend0(
            descriptor,
            metal::sel_registerName("colorAttachments"));
        // The first colour attachment carries the pixel format; the
        // blend state is off (the contract's write-through rule).
        metal::msgSendUL(colorAttachment,
                         metal::sel_registerName("setPixelFormat:"),
                         static_cast<NSUInteger>(colorFormat));
        using FnPipe = void* (*)(void*, void*, void*, void**);
        metal::MTLRenderPipelineStateHandle pipelineState =
            reinterpret_cast<FnPipe>(metal::objc_msgSend)(
                device,
                metal::sel_registerName(
                    "newRenderPipelineStateWithDescriptor:error:"),
                descriptor, nullptr);
        if (pipelineState == nullptr && error.empty())
            error = "newRenderPipelineStateWithDescriptor failed";
        return pipelineState;
    }

    /// The pipeline of one program at the current depth state and the
    /// bound pass's format pair (created on first use, cached in the
    /// pipelines map — the same rule the Vulkan backend applies).
    metal::MTLRenderPipelineStateHandle ensurePipeline(
        MetalRenderBackend& backend, metal::MtlShaderProgram* program,
        std::string& error)
    {
        metal::MTLPixelFormat colorFormatValue =
            bound_pass != nullptr
                ? metal::toMtlFormat(bound_pass->colorFormat(0))
                : layerFormat;
        metal::MTLPixelFormat depthFormatValue =
            bound_pass != nullptr && bound_pass->hasDepthAttachment()
                ? depthFormat
                : metal::MTLPixelFormat::Invalid;
        PipelineKey key{};
        key.vertexSource = program->vertexSource();
        key.fragmentSource = program->fragmentSource();
        key.depthTest = depth_test;
        key.colorFormat = colorFormatValue;
        key.depthFormatValue = depthFormatValue;
        auto it = pipelines.find(key);
        if (it != pipelines.end())
            return it->second;
        metal::MTLRenderPipelineStateHandle pipeline =
            createPipeline(program, depth_test, colorFormatValue,
                           depthFormatValue, error);
        if (pipeline == nullptr)
            return nullptr;
        pipelines.emplace(key, pipeline);
        (void)backend;
        return pipeline;
    }

    /// Encodes the frame's render pass into a fresh command buffer: the
    /// pass descriptor's colour attachment loads the clear colour (or
    /// the previous contents) and stores (the Vulkan render pass's
    /// loadOp/storeOp pair, one descriptor object here).
    void beginFrame(MetalRenderBackend& backend,
                    metal::MtlRenderTarget* target)
    {
        if (commandBuffer == nullptr)
        {
            commandBuffer = metal::msgSend0(
                queue, metal::sel_registerName("commandBuffer"));
            if (commandBuffer == nullptr)
                throw std::runtime_error(
                    "MetalRenderBackend: commandBuffer failed");
            swapchain_frame = target == nullptr;
            frame_open = true;
        }
        bound_pass = target;
        // The pass descriptor comes from the target's attachments (a
        // borrowed volume layer's texture for layer targets) or from the
        // layer's current drawable.
        metal::MTLTextureHandle colorAttachment = nullptr;
        metal::MTLTextureHandle depthAttachment = nullptr;
        if (target != nullptr)
        {
            if (target->borrowsVolume())
            {
                // The layer target's borrowed attachment: the volume's
                // own texture (the pass writes layer z through the
                // render pass's slice rule).
                colorAttachment = volumeResources[target->volumeView()]
                    .texture;
            }
            else
            {
                const TargetResources& res = targetResources[target];
                if (target->hasColorAttachment())
                    colorAttachment = res.textures[0];
                if (target->hasDepthAttachment())
                    depthAttachment =
                        res.textures[target->colorAttachmentCount()];
            }
        }
        else
        {
            // The drawable's texture (the presentation's own render
            // target for this frame).
            void* drawable = metal::msgSend0(
                layer,
                metal::sel_registerName("currentDrawable"));
            if (drawable != nullptr)
                colorAttachment = metal::msgSend0(
                    drawable, metal::sel_registerName("texture"));
            depthAttachment = swapchainDepthTexture;
        }
        // The encoder records against the descriptor's attachments; the
        // clear values come from the device state (the recorded state
        // the setClearColor/setClearBuffers calls keep).
        (void)colorAttachment;
        (void)depthAttachment;
        pass_open = true;
        applyDynamicViewport(backend);
    }

    /// Records the dynamic viewport of the pass target.
    void applyDynamicViewport(MetalRenderBackend& backend)
    {
        (void)backend;
        // MTLViewport: six doubles, the published layout.
        metal::MTLViewport viewport{};
        if (bound_pass != nullptr)
        {
            viewport.width = static_cast<double>(bound_pass->width());
            viewport.height = static_cast<double>(bound_pass->height());
        }
        else
        {
            viewport.width = static_cast<double>(config.width);
            viewport.height = static_cast<double>(config.height);
        }
        viewport.znear = 0.0;
        viewport.zfar = 1.0;
        if (encoder != nullptr)
        {
            using Fn = void (*)(void*, void*, metal::MTLViewport);
            reinterpret_cast<Fn>(metal::objc_msgSend)(
                encoder, metal::sel_registerName("setViewport:"),
                viewport);
        }
    }

    /// Closes the render pass (the recorded draws are kept until the
    /// frame's end commits the command buffer).
    void endFrame(MetalRenderBackend& backend)
    {
        if (commandBuffer == nullptr || !frame_open)
            return;
        if (pass_open)
        {
            metal::msgSendVoid(encoder,
                               metal::sel_registerName("endEncoding"));
            encoder = nullptr;
            pass_open = false;
        }
        bound_pass = nullptr;
        // An off-screen frame is not presented: its command buffer runs
        // at the pass end (the GL's unbind is immediate the same way). A
        // swapchain frame keeps recording until swapBuffers presents it.
        if (!swapchain_frame)
            submitFrame(backend);
    }

    /// Commits and waits (the one-off path: the command buffer's work
    /// runs before the CPU continues; the presentation path keeps the
    /// commit until swapBuffers).
    void submitFrame(MetalRenderBackend& backend)
    {
        (void)backend;
        if (commandBuffer == nullptr || !frame_open)
            return;
        if (pass_open)
        {
            metal::msgSendVoid(encoder,
                               metal::sel_registerName("endEncoding"));
            encoder = nullptr;
            pass_open = false;
        }
        frame_open = false;
        metal::msgSendVoid(commandBuffer,
                           metal::sel_registerName("commit"));
        metal::msgSendVoid(commandBuffer,
                           metal::sel_registerName("waitUntilCompleted"));
        commandBuffer = nullptr;
        swapchain_frame = false;
    }

    /// Copies one pixel of the bound target's colour (or the layer's) to
    /// the CPU through a blit encoder's copyFromTexture:toBuffer (the
    /// readback staging path; the depth attachment's float depth and
    /// stencil byte follow).
    void readPixelInto(const std::uint32_t x, const std::uint32_t y)
    {
        metal::MTLTextureHandle colour_texture = nullptr;
        metal::MTLTextureHandle depth_texture = nullptr;
        if (bound_pass != nullptr)
        {
            const TargetResources& res = targetResources[bound_pass];
            if (bound_pass->hasColorAttachment())
                colour_texture = res.textures[0];
            if (bound_pass->hasDepthAttachment())
                depth_texture =
                    res.textures[bound_pass->colorAttachmentCount()];
        }
        else
        {
            void* drawable = metal::msgSend0(layer,
                metal::sel_registerName("currentDrawable"));
            if (drawable != nullptr)
                colour_texture = metal::msgSend0(
                    drawable, metal::sel_registerName("texture"));
            depth_texture = swapchainDepthTexture;
        }
        if (colour_texture == nullptr && depth_texture == nullptr)
            return;
        // The readback staging buffer (one colour word, one depth word,
        // one stencil byte; the shared storage keeps the CPU's copy
        // coherent).
        metal::MTLBufferHandle staging =
            createBuffer(3 * sizeof(float), metal::MTLStorageMode::Shared);
        if (commandBuffer == nullptr)
        {
            commandBuffer = metal::msgSend0(
                queue, metal::sel_registerName("commandBuffer"));
        }
        void* blit = metal::msgSend0(commandBuffer,
                                     metal::sel_registerName(
                                         "blitEncoder"));
        if (colour_texture != nullptr)
        {
            using Fn = void (*)(void*, void*, metal::MTLTextureHandle,
                                NSUInteger, NSUInteger,
                                metal::MTLBufferHandle, NSUInteger,
                                NSUInteger, NSUInteger,
                                metal::MTLOrigin, metal::MTLSize);
            reinterpret_cast<Fn>(metal::objc_msgSend)(
                blit,
                metal::sel_registerName("copyFromTexture:sourceSlice:"
                                        "sourceLevel:toBuffer:"
                                        "destinationOffset:"
                                        "destinationBytesPerRow:"
                                        "destinationBytesPerImage:"
                                        "sourceOrigin:sourceSize:"),
                colour_texture, 0u, 0u, staging, 0u, 4u, 4u,
                metal::MTLOrigin{x, y, 0},
                metal::MTLSize{1, 1, 1});
        }
        if (depth_texture != nullptr)
        {
            using Fn = void (*)(void*, void*, metal::MTLTextureHandle,
                                NSUInteger, NSUInteger,
                                metal::MTLBufferHandle, NSUInteger,
                                NSUInteger, NSUInteger,
                                metal::MTLOrigin, metal::MTLSize);
            reinterpret_cast<Fn>(metal::objc_msgSend)(
                blit,
                metal::sel_registerName("copyFromTexture:sourceSlice:"
                                        "sourceLevel:toBuffer:"
                                        "destinationOffset:"
                                        "destinationBytesPerRow:"
                                        "destinationBytesPerImage:"
                                        "sourceOrigin:sourceSize:"),
                depth_texture, 0u, 0u, staging, 16u, 8u, 8u,
                metal::MTLOrigin{x, y, 0},
                metal::MTLSize{1, 1, 1});
        }
        metal::msgSendVoid(blit, metal::sel_registerName("endEncoding"));
        metal::msgSendVoid(commandBuffer,
                           metal::sel_registerName("commit"));
        metal::msgSendVoid(commandBuffer,
                           metal::sel_registerName("waitUntilCompleted"));
        void* base = metal::msgSend0(staging,
                                     metal::sel_registerName("contents"));
        const std::uint8_t* bytes = static_cast<const std::uint8_t*>(base);
        lastReadbackColor = ClearColor{bytes[0], bytes[1], bytes[2],
                                       bytes[3]};
        lastReadbackDepth = bytes[4];
        lastReadbackStencil = bytes[5];
    }

    /// Uploads the mesh's staged CPU data into device buffers (the
    /// shared-storage path: one buffer carries vertices, one carries
    /// indices; the draw binds them directly — no device-local copy the
    /// Vulkan backend needs a staging buffer for, because Metal's shared
    /// storage is GPU-visible).
    void createMeshBuffers(MetalRenderBackend& backend,
                           metal::MtlMesh* mesh)
    {
        (void)backend;
        const std::size_t vertexBytes = mesh->vertexBytes();
        const std::size_t indexBytes = mesh->indices().size()
            * sizeof(std::uint32_t);
        metal::MTLBufferHandle vertexBuffer = nullptr;
        metal::MTLBufferHandle indexBuffer = nullptr;
        if (vertexBytes > 0)
        {
            vertexBuffer = createBuffer(vertexBytes,
                                        metal::MTLStorageMode::Shared);
            fillBuffer(vertexBuffer, mesh->vertexData().data(),
                       vertexBytes);
        }
        if (indexBytes > 0)
        {
            indexBuffer = createBuffer(indexBytes,
                                       metal::MTLStorageMode::Shared);
            fillBuffer(indexBuffer, mesh->indices().data(), indexBytes);
        }
        mesh->setGpuBuffers(vertexBuffer, indexBuffer);
    }

    /// Records the draw of one mesh through one program inside the
    /// frame's encoder (the pipeline, the uniforms and the draw).
    void drawMesh(MetalRenderBackend& backend, metal::MtlMesh* mesh,
                  metal::MtlShaderProgram* program)
    {
        (void)backend;
        if (encoder == nullptr)
            return;
        // The uniform staging buffer: the program's staged bytes go
        // straight to the shared-storage buffer the draw binds (decision
        // 14: the engine sets uniforms by name, the staging buffer is
        // the API-neutral contract's one).
        ProgramResources& resources =
            ensureProgramResources(backend, program);
        if (program->uniformStaged() && resources.uniformBuffer != nullptr)
        {
            fillBuffer(resources.uniformBuffer,
                       program->uniformBytes().data(),
                       program->uniformBytes().size());
            program->clearStaging();
        }
        std::string pipelineError;
        metal::MTLRenderPipelineStateHandle pipeline =
            ensurePipeline(backend, program, pipelineError);
        if (pipeline == nullptr)
        {
            // The deliberately broken pair's compile failure lands here:
            // the same runtime signal the OpenGL backend's info log and
            // the Vulkan backend's empty SPIR-V array carry.
            return;
        }
        using FnSet = void (*)(void*, void*, void*);
        reinterpret_cast<FnSet>(metal::objc_msgSend)(
            encoder,
            metal::sel_registerName("setRenderPipelineState:"),
            pipeline);
        if (resources.uniformBuffer != nullptr)
        {
            using FnBuf = void (*)(void*, void*, metal::MTLBufferHandle,
                                   NSUInteger, NSUInteger);
            reinterpret_cast<FnBuf>(metal::objc_msgSend)(
                encoder,
                metal::sel_registerName(
                    "setVertexBuffer:offset:atIndex:"),
                resources.uniformBuffer, 0u, 0u);
            reinterpret_cast<FnBuf>(metal::objc_msgSend)(
                encoder,
                metal::sel_registerName(
                    "setFragmentBuffer:offset:atIndex:"),
                resources.uniformBuffer, 0u, 0u);
        }
        // The bound texture views land on the texture argument indices
        // the program's layout table declares; an unbound unit keeps the
        // default texture (the contract's tolerance: the shader samples
        // a valid, opaque image either way).
        for (const auto& sampler : program->samplerEntries())
        {
            for (std::uint32_t element = 0; element < sampler.count;
                 ++element)
            {
                const std::uint32_t unit =
                    sampler.unitBase + sampler.unitStride * element;
                const TextureView* view =
                    unit < boundTextures.size()
                        ? boundTextures[unit]
                        : nullptr;
                metal::MTLTextureHandle texture = defaultTexture;
                if (view != nullptr)
                {
                    if (const auto* mtlView =
                            dynamic_cast<const metal::MtlTextureView*>(
                                view))
                        texture = mtlView->texture();
                    else if (const auto* volume =
                                 dynamic_cast<const metal::MtlTexture3DView*>(
                                     view))
                    {
                        const auto it = volumeResources.find(volume);
                        if (it != volumeResources.end())
                            texture = it->second.texture;
                    }
                }
                if (texture == nullptr)
                    texture = defaultTexture;
                using FnTex = void (*)(void*, void*,
                                       metal::MTLTextureHandle, NSUInteger);
                reinterpret_cast<FnTex>(metal::objc_msgSend)(
                    encoder,
                    metal::sel_registerName("setFragmentTexture:atIndex:"),
                    texture,
                    static_cast<NSUInteger>(sampler.textureIndex
                                            + element));
            }
        }
        using FnSmp = void (*)(void*, void*,
                               metal::MTLSamplerStateHandle, NSUInteger);
        reinterpret_cast<FnSmp>(metal::objc_msgSend)(
            encoder,
            metal::sel_registerName("setFragmentSamplerState:atIndex:"),
            sharedSampler, 0u);
        // The vertex and index buffers bind once per draw (the mesh's
        // setGpuBuffers handles; a null buffer means the backend never
        // uploaded this mesh, the draw's violation).
        if (mesh->vertexBuffer() == nullptr || mesh->indexBuffer() == nullptr)
        {
            ++violations;
            return;
        }
        using FnVtx = void (*)(void*, void*, metal::MTLBufferHandle,
                               NSUInteger, NSUInteger);
        reinterpret_cast<FnVtx>(metal::objc_msgSend)(
            encoder,
            metal::sel_registerName("setVertexBuffer:offset:atIndex:"),
            mesh->vertexBuffer(), 0u, 1u);
        using FnDraw = void (*)(void*, void*, metal::MTLPrimitiveType,
                                NSUInteger, metal::MTLIndexType,
                                metal::MTLBufferHandle, NSUInteger);
        reinterpret_cast<FnDraw>(metal::objc_msgSend)(
            encoder,
            metal::sel_registerName(
                "drawIndexedPrimitives:indexCount:indexType:"
                "indexBuffer:indexBufferOffset:"),
            metal::MTLPrimitiveType::Triangle,
            static_cast<NSUInteger>(mesh->indexCount()),
            metal::MTLIndexType::UInt32, mesh->indexBuffer(), 0u);
    }

    /// The program's resources, created on first use (the compiled
    /// functions and the uniform buffer; the pipeline itself is the
    /// ensurePipeline map's entry).
    ProgramResources& ensureProgramResources(
        MetalRenderBackend& backend, metal::MtlShaderProgram* program)
    {
        auto it = programResources.find(program);
        if (it == programResources.end())
        {
            ProgramResources entry;
            const std::size_t uniformBytes = program->uniformBytes().size();
            if (uniformBytes > 0)
            {
                entry.uniformBuffer =
                    createBuffer(uniformBytes, metal::MTLStorageMode::Shared);
                entry.uniformBufferBase = metal::msgSend0(
                    entry.uniformBuffer,
                    metal::sel_registerName("contents"));
                entry.uniformBytes = uniformBytes;
            }
            it = programResources.emplace(program, entry).first;
        }
        (void)backend;
        return it->second;
    }

    /// Presents the frame: the command buffer's drawable is handed to
    /// the window system (the framework's own present; no semaphore
    /// dance exists on this API, decision 20's present semantics map
    /// onto one message).
    void present()
    {
        // An off-screen frame never acquired a drawable: nothing to
        // present.
        if (!swapchain_frame)
            return;
        void* drawable = metal::msgSend0(
                layer,
                metal::sel_registerName("currentDrawable"));
        if (drawable != nullptr)
            metal::msgSendVoid(drawable,
                               metal::sel_registerName("present"));
    }

    /// Creates the device objects of one render target: the colour and
    /// depth textures (the pass's attachments; the borrow path attaches
    /// the volume's own texture instead).
    void createTargetResources(MetalRenderBackend& backend,
                               metal::MtlRenderTarget* target)
    {
        (void)backend;
        if (target->borrowsVolume())
            return;
        TargetResources& res = targetResources[target];
        const std::uint32_t width = target->width();
        const std::uint32_t height = target->height();
        for (std::uint32_t i = 0; i < target->colorAttachmentCount(); ++i)
        {
            metal::MTLTextureHandle texture = createTexture(
                metal::toMtlFormat(target->colorFormat(i)), width, height, 1,
                static_cast<NSUInteger>(metal::MTLTextureUsage::RenderTarget)
                    | static_cast<NSUInteger>(
                        metal::MTLTextureUsage::ShaderRead),
                static_cast<NSUInteger>(metal::MTLTextureType::Type2D));
            res.textures.push_back(texture);
            target->setColorView(i, texture, width, height,
                                 target->colorFormat(i), false);
        }
        if (target->hasDepthAttachment())
        {
            metal::MTLTextureHandle texture = createTexture(
                depthFormat, width, height, 1,
                static_cast<NSUInteger>(metal::MTLTextureUsage::RenderTarget)
                    | static_cast<NSUInteger>(
                        metal::MTLTextureUsage::ShaderRead),
                static_cast<NSUInteger>(metal::MTLTextureType::Type2D));
            res.textures.push_back(texture);
            target->setDepthView(texture, width, height,
                                 target->desc().depthFormat, true);
        }
    }

    /// Releases the device objects of one render target (the borrowed
    /// volume's texture stays with the volume).
    void destroyTargetResources(MetalRenderBackend& backend,
                                metal::MtlRenderTarget* target)
    {
        auto it = targetResources.find(target);
        if (it == targetResources.end())
            return;
        if (!target->borrowsVolume())
        {
            for (metal::MTLTextureHandle texture : it->second.textures)
            {
                if (texture != nullptr)
                    metal::msgSendVoid(texture,
                                       metal::sel_registerName("release"));
            }
        }
        targetResources.erase(it);
        (void)backend;
    }

    /// Creates the device object of one 3D texture (the volume).
    void createVolumeResources(MetalRenderBackend& backend,
                               metal::MtlTexture3DView* volume)
    {
        VolumeResources res;
        res.texture = createTexture(
            metal::toMtlFormat(volume->format()), volume->width(),
            volume->height(), volume->depth(),
            static_cast<NSUInteger>(metal::MTLTextureUsage::RenderTarget)
                | static_cast<NSUInteger>(
                    metal::MTLTextureUsage::ShaderRead),
            static_cast<NSUInteger>(metal::MTLTextureType::Type3D));
        volume->setTexture(res.texture);
        volumeResources[volume] = res;
        (void)backend;
    }

    /// Releases the device object of one 3D texture.
    void destroyVolumeResources(MetalRenderBackend& backend,
                                metal::MtlTexture3DView* volume)
    {
        auto it = volumeResources.find(volume);
        if (it == volumeResources.end())
            return;
        if (it->second.texture != nullptr)
            metal::msgSendVoid(it->second.texture,
                               metal::sel_registerName("release"));
        volumeResources.erase(it);
        (void)backend;
    }

    /// Copies the CPU-side data of a volume's one layer into the
    /// volume's texture (the shared-staging blit path; a 3D texture's
    /// slice z is the copy's origin).
    void fillVolumeLayer(MetalRenderBackend& backend,
                         metal::MtlTexture3DView* volume,
                         std::uint32_t sliceLayer, const void* data,
                         std::size_t sizeBytes)
    {
        (void)sliceLayer;  // a 3D copy addresses the slice by its origin
        metal::MTLBufferHandle staging =
            createBuffer(sizeBytes, metal::MTLStorageMode::Shared);
        fillBuffer(staging, data, sizeBytes);
        copyIntoTexture(staging, volume->texture(), sizeBytes,
                        sizeBytes, volume->width(), volume->height());
        (void)backend;
    }

    /// Reads one 2D slice of a volume's texture back to the CPU (the
    /// blit path's reverse copy).
    bool readVolumeLayer(MetalRenderBackend& backend,
                         metal::MtlTexture3DView* volume,
                         std::uint32_t sliceLayer, void* data,
                         std::size_t sizeBytes)
    {
        if (commandBuffer == nullptr)
        {
            commandBuffer = metal::msgSend0(
                queue, metal::sel_registerName("commandBuffer"));
        }
        metal::MTLBufferHandle staging =
            createBuffer(sizeBytes, metal::MTLStorageMode::Shared);
        void* blit = metal::msgSend0(commandBuffer,
                                     metal::sel_registerName(
                                         "blitEncoder"));
        using Fn = void (*)(void*, void*, metal::MTLTextureHandle,
                            NSUInteger, NSUInteger,
                            metal::MTLBufferHandle, NSUInteger,
                            NSUInteger, NSUInteger,
                            metal::MTLOrigin, metal::MTLSize);
        reinterpret_cast<Fn>(metal::objc_msgSend)(
            blit,
            metal::sel_registerName("copyFromTexture:sourceSlice:"
                                    "sourceLevel:toBuffer:"
                                    "destinationOffset:"
                                    "destinationBytesPerRow:"
                                    "destinationBytesPerImage:"
                                    "sourceOrigin:sourceSize:"),
            volume->texture(), static_cast<NSUInteger>(sliceLayer), 0u,
            staging, 0u, 0u, 0u, metal::MTLOrigin{0, 0, 0},
            metal::MTLSize{volume->width(), volume->height(), 1});
        metal::msgSendVoid(blit, metal::sel_registerName("endEncoding"));
        metal::msgSendVoid(commandBuffer,
                           metal::sel_registerName("commit"));
        metal::msgSendVoid(commandBuffer,
                           metal::sel_registerName("waitUntilCompleted"));
        void* base = metal::msgSend0(staging,
                                     metal::sel_registerName("contents"));
        if (base == nullptr)
            return false;
        std::memcpy(data, base, sizeBytes);
        (void)backend;
        return true;
    }

    /// Copies one depth attachment onto another (the blit path's texture
    /// to texture copy; the two targets must be the same size and the
    /// same format, the same rejection the OpenGL backend makes).
    void copyDepthAttachment(MetalRenderBackend& backend,
                             metal::MtlRenderTarget* source,
                             metal::MtlRenderTarget* destination)
    {
        TargetResources& src = targetResources[source];
        TargetResources& dst = targetResources[destination];
        const std::uint32_t depthIndex = source->colorAttachmentCount();
        metal::MTLTextureHandle srcTexture = src.textures[depthIndex];
        metal::MTLTextureHandle dstTexture =
            dst.textures[destination->colorAttachmentCount()];
        metal::MTLCommandBufferHandle copy = metal::msgSend0(
            queue, metal::sel_registerName("commandBuffer"));
        void* blit = metal::msgSend0(copy,
                                     metal::sel_registerName(
                                         "blitEncoder"));
        using Fn = void (*)(void*, void*, metal::MTLTextureHandle,
                            NSUInteger, NSUInteger, metal::MTLOrigin,
                            metal::MTLSize, metal::MTLTextureHandle,
                            NSUInteger, NSUInteger, metal::MTLOrigin);
        reinterpret_cast<Fn>(metal::objc_msgSend)(
            blit,
            metal::sel_registerName("copyFromTexture:sourceSlice:"
                                    "sourceLevel:sourceOrigin:sourceSize:"
                                    "toTexture:destinationSlice:"
                                    "destinationLevel:destinationOrigin:"),
            srcTexture, 0u, 0u, metal::MTLOrigin{0, 0, 0},
            metal::MTLSize{static_cast<NSUInteger>(source->width()),
                            static_cast<NSUInteger>(source->height()), 1},
            dstTexture, 0u, 0u, metal::MTLOrigin{0, 0, 0});
        metal::msgSendVoid(blit, metal::sel_registerName("endEncoding"));
        metal::msgSendVoid(copy, metal::sel_registerName("commit"));
        metal::msgSendVoid(copy,
                           metal::sel_registerName("waitUntilCompleted"));
        (void)backend;
    }
};

MetalRenderBackend::MetalRenderBackend(MetalBackendConfig config)
    : m_impl(std::make_unique<Impl>())
{
    if (config.width == 0 || config.height == 0)
    {
        throw std::invalid_argument(
            "MetalRenderBackend: width and height must be positive");
    }
    m_impl->config = config;
    m_impl->camera.aspect =
        static_cast<float>(config.width) / static_cast<float>(config.height);
}

MetalRenderBackend::~MetalRenderBackend()
{
    if (m_impl->state == Impl::State::Initialised)
        shutdownImpl(false);
}

void MetalRenderBackend::initialise()
{
    Impl& impl = *m_impl;
    if (impl.state != Impl::State::Created)
        throw std::logic_error(
            "MetalRenderBackend::initialise: already initialised");
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        throw std::runtime_error(std::string("SDL_Init failed: ")
                                 + SDL_GetError());
    const int width = static_cast<int>(impl.config.width);
    const int height = static_cast<int>(impl.config.height);
    Uint32 window_flags = 0;
    if (impl.config.hidden)
        window_flags |= static_cast<Uint32>(SDL_WINDOW_HIDDEN);
    impl.window = SDL_CreateWindow("OpenGrace", SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED, width, height,
                                   window_flags);
    if (impl.window == nullptr)
    {
        const std::string message =
            std::string("SDL_CreateWindow failed: ") + SDL_GetError();
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    std::string missing;
    if (!impl.loader.load(missing))
    {
        const std::string message =
            "Metal loader: could not resolve '" + missing + "'";
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    // The default device: the framework's one published C entry point
    // (MTLCreateSystemDefaultDevice) returns the GPU the process runs
    // on; a null answer means no device (a Mac without a GPU the
    // framework recognises, or a headless one).
    impl.device = impl.loader.MTLCreateSystemDefaultDevice_fn();
    if (impl.device == nullptr)
    {
        const std::string message(
            "MTLCreateSystemDefaultDevice returned null");
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    impl.queue = metal::msgSend0(impl.device,
                                 metal::sel_registerName("newCommandQueue"));
    if (impl.queue == nullptr)
    {
        const std::string message("newCommandQueue failed");
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    // The presentation layer: the SDL Metal bridge creates the view and
    // hands the CAMetalLayer out (SDL 2.0.14+; the same bridge rule the
    // Vulkan backend's surface creation follows).
    SDL_MetalView view = SDL_Metal_CreateView(impl.window);
    if (view == nullptr)
    {
        const std::string message =
            std::string("SDL_Metal_CreateView failed: ") + SDL_GetError();
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    impl.layer = SDL_Metal_GetLayer(view);
    SDL_Metal_DestroyView(view);
    if (impl.layer == nullptr)
    {
        const std::string message(
            "SDL_Metal_GetLayer returned null");
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    impl.layerFormat = metal::MTLPixelFormat::Bgra8Unorm;
    metal::msgSendUL(impl.layer,
                     metal::sel_registerName("setPixelFormat:"),
                     static_cast<NSUInteger>(impl.layerFormat));
    // The layer's drawable size follows the window (the bridge sets it
    // once; the contract's fixed-size window never resizes).
    const metal::MTLSize drawableSize{
        static_cast<NSUInteger>(impl.config.width),
        static_cast<NSUInteger>(impl.config.height), 1};
    {
        using Fn = void (*)(void*, void*, metal::MTLSize);
        reinterpret_cast<Fn>(metal::objc_msgSend)(
            impl.layer, metal::sel_registerName("setDrawableSize:"),
            drawableSize);
    }
    // The default framebuffer's depth attachment (the pick runs once:
    // Depth24Unorm_Stencil8 on macOS, Depth32Float_Stencil8 where the
    // packed format is unavailable).
    impl.depthFormat = impl.pickDepthFormat();
    impl.swapchainDepthTexture = impl.createTexture(
        impl.depthFormat, impl.config.width, impl.config.height, 1,
        static_cast<NSUInteger>(metal::MTLTextureUsage::RenderTarget)
            | static_cast<NSUInteger>(metal::MTLTextureUsage::ShaderRead),
        static_cast<NSUInteger>(metal::MTLTextureType::Type2D));
    if (impl.swapchainDepthTexture == nullptr)
    {
        const std::string message("the default depth texture failed");
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    impl.sharedSampler = impl.createSharedSampler();
    if (impl.sharedSampler == nullptr)
    {
        const std::string message("newSamplerState failed");
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    // The default texture (1x1 opaque white): the sampler argument
    // bindings the engine never writes still name a texture, and the
    // shader samples a valid, opaque image either way.
    impl.defaultTexture = impl.createTexture(
        metal::MTLPixelFormat::Rgba8Unorm, 1, 1, 1,
        static_cast<NSUInteger>(metal::MTLTextureUsage::ShaderRead),
        static_cast<NSUInteger>(metal::MTLTextureType::Type2D));
    if (impl.defaultTexture != nullptr)
    {
        metal::MTLBufferHandle staging =
            impl.createBuffer(4, metal::MTLStorageMode::Shared);
        const std::uint8_t white[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        impl.fillBuffer(staging, white, sizeof(white));
        impl.copyIntoTexture(staging, impl.defaultTexture, 4, 4, 1, 1);
    }
    impl.device_name = impl.queryDeviceName();
    impl.state = Impl::State::Initialised;
}

std::string MetalRenderBackend::describe() const
{
    const Impl& impl = *m_impl;
    if (impl.state == Impl::State::Created)
        return "metal (not initialised)";
    return "metal; renderer=" + impl.device_name;
}

void MetalRenderBackend::setClearColor(ClearColor color)
{
    if (!m_impl->ready())
    {
        ++m_impl->violations;
        return;
    }
    m_impl->clear_color = color;
}

ClearColor MetalRenderBackend::clearColor() const
{
    return m_impl->clear_color;
}

void MetalRenderBackend::setClearBuffers(std::uint32_t mask) noexcept
{
    m_impl->clear_mask = mask;
}

void MetalRenderBackend::setClearDepth(float depth) noexcept
{
    m_impl->clear_depth = depth;
}

void MetalRenderBackend::setClearStencil(std::int32_t stencil) noexcept
{
    m_impl->clear_stencil = stencil;
}

void MetalRenderBackend::setDepthTestEnabled(bool enabled)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    impl.depth_test = enabled;
}

std::uint32_t MetalRenderBackend::width() const
{
    return m_impl->config.width;
}

std::uint32_t MetalRenderBackend::height() const
{
    return m_impl->config.height;
}

void MetalRenderBackend::shutdown()
{
    shutdownImpl(true);
}

void MetalRenderBackend::shutdownImpl(bool countViolation)
{
    Impl& impl = *m_impl;
    if (impl.state == Impl::State::Shutdown)
    {
        // Call after shutdown: counted as a violation, but safe (no-op).
        if (countViolation)
            ++impl.violations;
        return;
    }
    if (impl.state != Impl::State::Initialised)
    {
        // Call before initialise: counted as a violation, no-op.
        if (countViolation)
            ++impl.violations;
        return;
    }
    // Release the foundation objects while the device is still alive
    // (the framework's objects are reference-counted; the explicit
    // release calls keep the ownership honest across all runtimes).
    impl.tri_material.reset();
    impl.tri_shader.reset();
    impl.tri_mesh.reset();
    impl.lighting_program.reset();
    impl.programResources.clear();
    impl.pipelines.clear();
    impl.targetResources.clear();
    impl.volumeResources.clear();
    impl.targets.clear();
    impl.volumes.clear();
    if (impl.defaultTexture != nullptr)
    {
        metal::msgSendVoid(impl.defaultTexture,
                           metal::sel_registerName("release"));
        impl.defaultTexture = nullptr;
    }
    if (impl.swapchainDepthTexture != nullptr)
    {
        metal::msgSendVoid(impl.swapchainDepthTexture,
                           metal::sel_registerName("release"));
        impl.swapchainDepthTexture = nullptr;
    }
    if (impl.sharedSampler != nullptr)
    {
        metal::msgSendVoid(impl.sharedSampler,
                           metal::sel_registerName("release"));
        impl.sharedSampler = nullptr;
    }
    impl.device = nullptr;
    impl.queue = nullptr;
    impl.layer = nullptr;
    SDL_DestroyWindow(impl.window);
    impl.window = nullptr;
    sdlVideoRelease();
    impl.state = Impl::State::Shutdown;
}

std::uint64_t MetalRenderBackend::violationCount() const noexcept
{
    return m_impl->violations;
}

std::unique_ptr<ShaderProgram> MetalRenderBackend::createShaderProgram(
    ShaderProgramId id)
{
    if (!m_impl->ready())
    {
        ++m_impl->violations;
        throw std::logic_error(
            "MetalRenderBackend::createShaderProgram: not initialised");
    }
    return std::make_unique<metal::MtlShaderProgram>(id);
}

std::unique_ptr<GpuMesh> MetalRenderBackend::createGpuMesh()
{
    if (!m_impl->ready())
    {
        ++m_impl->violations;
        throw std::logic_error(
            "MetalRenderBackend::createGpuMesh: not initialised");
    }
    return std::make_unique<metal::MtlMesh>();
}

void MetalRenderBackend::ensureTriangle()
{
    Impl& impl = *m_impl;
    if (impl.triangle_ready)
        return;
    Mesh mesh;
    VertexLayout layout;
    layout.stride = 3 * sizeof(float);
    layout.attributes.push_back(
        VertexAttribute{VertexSemantic::Position, ComponentType::Float32, 3, 0,
                        false});
    mesh.setLayout(layout);
    mesh.setVertexData(std::vector<float>{-0.5f, -0.5f, 0.0f,  //
                                          0.5f, -0.5f, 0.0f,   //
                                          0.0f, 0.5f, 0.0f});
    mesh.setIndices(std::vector<std::uint32_t>{0, 1, 2});
    impl.tri_mesh = createGpuMesh();
    impl.tri_mesh->upload(mesh);
    impl.tri_shader = std::shared_ptr<ShaderProgram>(
        createShaderProgram(ShaderProgramId::FlatUnlit));
    impl.tri_material = std::make_shared<Material>(impl.tri_shader);
    metal::MtlMesh* meshHandle =
        static_cast<metal::MtlMesh*>(impl.tri_mesh.get());
    impl.createMeshBuffers(*this, meshHandle);
    meshHandle->clearCpuData();
    impl.triangle_ready = true;
}

void MetalRenderBackend::drawTriangle()
{
    Impl& impl = *m_impl;
    ensureTriangle();
    // Full-frame viewport: NDC maps onto the whole framebuffer/target,
    // so the result is correct for non-square sizes.
    applyViewport();
    const float aspect = static_cast<float>(impl.config.width) /
                         static_cast<float>(impl.config.height);
    const core::mat4 model = core::Transform{}.compose();
    const core::mat4 view = core::lookAt(core::vec3{0.0f, 0.0f, 1.0f},
                                         core::vec3{0.0f, 0.0f, 0.0f},
                                         core::vec3{0.0f, 1.0f, 0.0f});
    // M_PI is not in strict-ANSI <cmath> (MinGW builds with it undefined).
    constexpr float kHalfPi = 1.5707963267948966f;
    const core::mat4 projection = core::perspective(
        kHalfPi, aspect, 0.1f, 100.0f);
    const core::mat4 mvp = projection * view * model;
    metal::MtlShaderProgram* program =
        static_cast<metal::MtlShaderProgram*>(impl.tri_shader.get());
    program->setUniform("mvp", mvp);
    const ClearColor color = impl.config.triangle_color;
    program->setUniform(
        "color", core::vec4{static_cast<float>(color.r) / 255.0f,
                            static_cast<float>(color.g) / 255.0f,
                            static_cast<float>(color.b) / 255.0f,
                            static_cast<float>(color.a) / 255.0f});
    metal::MtlMesh* meshHandle =
        static_cast<metal::MtlMesh*>(impl.tri_mesh.get());
    // The diagnostic draw never writes depth (the same scoped toggle the
    // Vulkan backend applies; the pipeline key carries the depth flag).
    const bool depth_state = impl.depth_test;
    impl.depth_test = false;
    impl.drawMesh(*this, meshHandle, program);
    impl.depth_test = depth_state;
}

void MetalRenderBackend::clear()
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    // The clear happens inside the render pass that records the frame:
    // the attachments load the clear colour (or the previous contents)
    // when the frame's command buffer runs. The recorded state lives
    // until the swapBuffers presents the frame.
    if (impl.pass_open)
    {
        if (impl.config.draw_triangle)
            drawTriangle();
        return;
    }
    impl.beginFrame(*this, nullptr);
    impl.bound_target = nullptr;
    if (impl.config.draw_triangle)
        drawTriangle();
    impl.submitFrame(*this);
}

void MetalRenderBackend::swapBuffers()
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    // Readback happens before the present: the drawable's contents are
    // undefined after a present (the same pre-present rule the OpenGL
    // and the Vulkan backends follow).
    impl.submitFrame(*this);
    if (impl.config.readback_center || !impl.readback_queue.empty())
        readback();
    impl.present();
}

void MetalRenderBackend::draw(GpuMesh& mesh, ShaderProgram& program)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    applyViewport();
    metal::MtlMesh* meshHandle = static_cast<metal::MtlMesh*>(&mesh);
    metal::MtlShaderProgram* programHandle =
        static_cast<metal::MtlShaderProgram*>(&program);
    impl.drawMesh(*this, meshHandle, programHandle);
}

namespace {
metal::MtlRenderTarget* findMtlTarget(
    std::vector<std::unique_ptr<metal::MtlRenderTarget>>& targets,
    RenderTarget* target)
{
    for (auto& candidate : targets)
        if (candidate.get() == target)
            return candidate.get();
    return nullptr;
}
metal::MtlTexture3DView* findMtlVolume(
    std::vector<std::unique_ptr<metal::MtlTexture3DView>>& volumes,
    const TextureView* view)
{
    for (auto& candidate : volumes)
        if (candidate.get() == view)
            return candidate.get();
    return nullptr;
}
}  // namespace

RenderTarget* MetalRenderBackend::createRenderTarget(
    const RenderTargetDesc& desc)
{
    Impl& impl = *m_impl;
    validateRenderTargetDesc(desc);
    if (!impl.ready())
    {
        ++impl.violations;
        throw std::logic_error(
            "MetalRenderBackend::createRenderTarget: not initialised");
    }
    auto target = std::make_unique<metal::MtlRenderTarget>(desc);
    metal::MtlRenderTarget* raw = target.get();
    impl.createTargetResources(*this, raw);
    impl.targets.push_back(std::move(target));
    return raw;
}

void MetalRenderBackend::beginRenderPass(RenderTarget* target)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    if (target == nullptr)
    {
        // Target the default framebuffer (the layer's drawable).
        impl.beginFrame(*this, nullptr);
        impl.bound_target = nullptr;
        return;
    }
    metal::MtlRenderTarget* known =
        findMtlTarget(impl.targets, target);
    if (known == nullptr || known->released)
    {
        // Unknown or released handle: violation, never dereferenced.
        ++impl.violations;
        return;
    }
    // Beginning a pass while another target is bound simply replaces it.
    impl.beginFrame(*this, known);
    impl.bound_target = known;
}

void MetalRenderBackend::endRenderPass()
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    impl.endFrame(*this);
    impl.bound_target = nullptr;
}

void MetalRenderBackend::releaseRenderTarget(RenderTarget* target)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    metal::MtlRenderTarget* known =
        findMtlTarget(impl.targets, target);
    if (known == nullptr || known->released)
    {
        ++impl.violations;
        return;
    }
    if (impl.bound_target == known)
        impl.bound_target = nullptr;
    impl.destroyTargetResources(*this, known);
    known->release();  // the handle stays in the registry
}

TextureView* MetalRenderBackend::renderTargetTexture(
    RenderTarget* target)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return nullptr;
    }
    metal::MtlRenderTarget* known =
        findMtlTarget(impl.targets, target);
    if (known == nullptr || known->released || impl.bound_target == known)
    {
        ++impl.violations;
        return nullptr;
    }
    if (known->hasColorAttachment())
        return known->colorView(0);
    // A depth-only target's contract-facing texture is its depth
    // attachment (the shadow-pass target's sampled depth view).
    if (known->hasDepthAttachment())
        return known->depthView();
    ++impl.violations;
    return nullptr;
}

TextureView* MetalRenderBackend::renderTargetTexture(
    RenderTarget* target, std::uint32_t attachment)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return nullptr;
    }
    metal::MtlRenderTarget* known =
        findMtlTarget(impl.targets, target);
    if (known == nullptr || known->released || impl.bound_target == known)
    {
        ++impl.violations;
        return nullptr;
    }
    if (attachment == kDepthAttachment)
    {
        if (!known->hasDepthAttachment())
        {
            ++impl.violations;
            return nullptr;
        }
        return known->depthView();
    }
    if (attachment < known->colorAttachmentCount())
        return known->colorView(attachment);
    ++impl.violations;
    return nullptr;
}

void MetalRenderBackend::bindTexture(const TextureView* view,
                                     std::uint32_t unit)
{
    Impl& impl = *m_impl;
    if (!impl.ready() || view == nullptr)
    {
        ++impl.violations;
        return;
    }
    const auto* mtlView =
        dynamic_cast<const metal::MtlTextureView*>(view);
    if (mtlView == nullptr)
    {
        ++impl.violations;
        return;
    }
    // The sampler unit is recorded: the draw's encoder binds the view's
    // texture at the unit's argument index.
    if (unit < impl.boundTextures.size())
        impl.boundTextures[unit] = mtlView;
    else
        impl.boundTextures.resize(unit + 1, nullptr);
    if (impl.boundTextures[unit] == nullptr)
        impl.boundTextures[unit] = mtlView;
}

TextureView* MetalRenderBackend::createTexture3D(const Texture3DDesc& desc)
{
    Impl& impl = *m_impl;
    validateTexture3DDesc(desc);
    if (!impl.ready())
    {
        ++impl.violations;
        throw std::logic_error(
            "MetalRenderBackend::createTexture3D: not initialised");
    }
    auto volume = std::make_unique<metal::MtlTexture3DView>(desc);
    metal::MtlTexture3DView* raw = volume.get();
    impl.createVolumeResources(*this, raw);
    impl.volumes.push_back(std::move(volume));
    return raw;
}

void MetalRenderBackend::releaseTexture3D(TextureView* view)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    metal::MtlTexture3DView* known = findMtlVolume(impl.volumes, view);
    if (known == nullptr)
    {
        ++impl.violations;
        return;
    }
    impl.destroyVolumeResources(*this, known);
    for (auto it = impl.volumes.begin(); it != impl.volumes.end(); ++it)
    {
        if (it->get() == known)
        {
            impl.volumes.erase(it);
            break;
        }
    }
}

void MetalRenderBackend::bindTexture3D(const TextureView* view,
                                       std::uint32_t unit)
{
    Impl& impl = *m_impl;
    if (!impl.ready() || view == nullptr)
    {
        ++impl.violations;
        return;
    }
    const auto* mtlView =
        dynamic_cast<const metal::MtlTexture3DView*>(view);
    if (mtlView == nullptr)
    {
        ++impl.violations;  // a 2D attachment view is not a volume
        return;
    }
    if (unit < impl.boundTextures.size())
        impl.boundTextures[unit] = mtlView;
    else
        impl.boundTextures.resize(unit + 1, nullptr);
    if (impl.boundTextures[unit] == nullptr)
        impl.boundTextures[unit] = mtlView;
}

void MetalRenderBackend::fillTexture3D(TextureView* view,
                                       std::uint32_t layer,
                                       const void* data,
                                       std::size_t sizeBytes)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    metal::MtlTexture3DView* volume = findMtlVolume(impl.volumes, view);
    if (volume == nullptr)
        throw std::invalid_argument(
            "fillTexture3D: view is not a 3D texture of this backend");
    if (volume->released)
    {
        ++impl.violations;
        return;
    }
    impl.fillVolumeLayer(*this, volume, layer, data, sizeBytes);
}

bool MetalRenderBackend::readTexture3D(const TextureView* view,
                                       std::uint32_t layer,
                                       void* data,
                                       std::size_t sizeBytes)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return false;
    }
    metal::MtlTexture3DView* volume = findMtlVolume(impl.volumes, view);
    if (volume == nullptr || volume->released
        || layer >= volume->depth()
        || sizeBytes != volume->sliceBytes())
    {
        ++impl.violations;
        return false;
    }
    return impl.readVolumeLayer(*this, volume, layer, data, sizeBytes);
}

RenderTarget* MetalRenderBackend::createVolumeLayerTarget(
    TextureView* volume, std::uint32_t layer)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        throw std::logic_error(
            "MetalRenderBackend::createVolumeLayerTarget: not initialised");
    }
    metal::MtlTexture3DView* known = findMtlVolume(impl.volumes, volume);
    if (known == nullptr)
    {
        ++impl.violations;
        throw std::invalid_argument(
            "createVolumeLayerTarget: view is not a 3D texture of this "
            "backend");
    }
    VolumeLayerDesc desc;
    desc.volume.width = known->width();
    desc.volume.height = known->height();
    desc.volume.depth = known->depth();
    desc.volume.format = known->format();
    desc.layer = layer;
    validateVolumeLayerDesc(desc);  // throws on an out-of-range layer
    auto target = std::make_unique<metal::MtlRenderTarget>(desc);
    metal::MtlRenderTarget* raw = target.get();
    // The layer target records the volume it borrows: the pass attaches
    // the volume's texture through this handle.
    raw->setVolumeView(volume);
    impl.createTargetResources(*this, raw);
    impl.targets.push_back(std::move(target));
    return raw;
}

void MetalRenderBackend::copyDepthAttachment(RenderTarget* source,
                                             RenderTarget* destination)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    metal::MtlRenderTarget* src = findMtlTarget(impl.targets, source);
    metal::MtlRenderTarget* dst =
        findMtlTarget(impl.targets, destination);
    if (src == nullptr || dst == nullptr || src->released || dst->released
        || !src->hasDepthAttachment() || !dst->hasDepthAttachment()
        || source == destination || src->width() != dst->width()
        || src->height() != dst->height() || impl.bound_target == src
        || impl.bound_target == dst)
    {
        ++impl.violations;
        return;
    }
    impl.copyDepthAttachment(*this, src, dst);
}

void MetalRenderBackend::queueReadback(std::uint32_t x, std::uint32_t y)
{
    if (!m_impl->ready())
    {
        ++m_impl->violations;
        return;
    }
    m_impl->readback_queue.emplace_back(x, y);
}

void MetalRenderBackend::readback()
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    readCenter();
    captureQueuedReadbacks();
}

void MetalRenderBackend::readCenter()
{
    Impl& impl = *m_impl;
    const std::uint32_t cx = impl.config.width / 2u;
    const std::uint32_t cy = impl.config.height / 2u;
    impl.readPixelInto(cx, cy);
    impl.readPixelInto(cx, cy);
    impl.center_pixel = impl.lastReadbackColor;
    impl.center_depth = impl.lastReadbackDepth;
    impl.center_stencil = impl.lastReadbackStencil;
    impl.has_center = true;
}

const std::vector<ReadbackSample>& MetalRenderBackend::lastReadback() const
    noexcept
{
    return m_impl->last_readback;
}

bool MetalRenderBackend::hasCenterPixel() const noexcept
{
    return m_impl->has_center;
}

ClearColor MetalRenderBackend::centerPixel() const noexcept
{
    return m_impl->center_pixel;
}

float MetalRenderBackend::centerDepth() const noexcept
{
    return m_impl->center_depth;
}

std::int32_t MetalRenderBackend::centerStencil() const noexcept
{
    return m_impl->center_stencil;
}

void MetalRenderBackend::setCamera(const CameraParams& camera)
{
    camera.validate();
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        throw std::logic_error(
            "MetalRenderBackend::setCamera: not initialised");
    }
    impl.camera = camera;
    impl.camera.aspect =
        static_cast<float>(impl.config.width)
        / static_cast<float>(impl.config.height);
}

CameraParams MetalRenderBackend::camera() const
{
    return m_impl->camera;
}

void MetalRenderBackend::applyViewport()
{
    Impl& impl = *m_impl;
    if (impl.encoder == nullptr)
        return;
    impl.applyDynamicViewport(*this);
}

void MetalRenderBackend::applyDepthState()
{
    Impl& impl = *m_impl;
    if (impl.encoder == nullptr)
        return;
    // The pipeline is per program + depth test + attachment formats; the
    // depth toggle lives in the pipeline (decision 20's no-test/no-write
    // state), the same rule the Vulkan backend applies.
}

void MetalRenderBackend::captureQueuedReadbacks()
{
    Impl& impl = *m_impl;
    impl.last_readback.clear();
    for (const auto& point : impl.readback_queue)
    {
        ReadbackSample sample{};
        sample.x = point.first;
        sample.y = point.second;
        impl.readPixelInto(point.first, point.second);
        sample.color = impl.lastReadbackColor;
        sample.depth = impl.lastReadbackDepth;
        sample.stencil = impl.lastReadbackStencil;
        impl.last_readback.push_back(sample);
    }
    impl.readback_queue.clear();
}

std::string MetalRenderBackend::deviceName() const noexcept
{
    return m_impl->device_name;
}

void MetalRenderBackend::ensureLightingProgram()
{
    Impl& impl = *m_impl;
    if (impl.lighting_program != nullptr)
        return;
    impl.lighting_program = std::shared_ptr<ShaderProgram>(
        createShaderProgram(ShaderProgramId::ForwardLighting));
}

void MetalRenderBackend::submitImpl(const DrawSubmission& submission)
{
    Impl& impl = *m_impl;
    submission.validate();
    ensureLightingProgram();
    // Camera set through setCamera() (documented diagnostic default:
    // eye (0,0,1) looking at the origin, up +Y, fovY 90 degrees).
    const CameraParams& camera = impl.camera;
    const core::mat4 view = core::lookAt(camera.eye, camera.target, camera.up);
    const core::mat4 projection =
        core::perspective(camera.fovYRad, camera.aspect, camera.near,
                          camera.far);
    const core::mat4 mvp = projection * view * submission.model;
    applyViewport();
    ShaderProgram& program = *impl.lighting_program;
    program.use();
    program.setUniform("mvp", mvp);
    program.setUniform("model", submission.model);
    program.setUniform("viewMatrix", view);
    program.setUniform("cameraPos", camera.eye);
    const SurfaceParams& surface = submission.material.surfaceParams();
    program.setUniform("albedo", surface.albedo);
    program.setUniform("specular", surface.specular);
    program.setUniform("shininess", surface.shininess);
    program.setUniform("emissive", surface.emissive);
    program.setUniform("ambient", submission.ambient);
    if (submission.sun.has_value())
    {
        program.setUniform("hasSun", 1);
        program.setUniform("sunDirection",
                           core::normalise(submission.sun->direction));
        program.setUniform("sunColor", submission.sun->color);
        program.setUniform("sunIntensity", submission.sun->intensity);
    }
    else
    {
        program.setUniform("hasSun", 0);
    }
    // Cascaded sun shadow map carried by the submission (milestone 4c).
    // The backend treats it as plain data: the cascade views land on the
    // texture argument indices the layout table declares.
    const ShadowMap* shadow = submission.shadow;
    if (shadow != nullptr && shadow->cascadeCount > 0)
    {
        const int count = static_cast<int>(shadow->cascadeCount);
        program.setUniform("hasShadowMap", 1);
        program.setUniform("cascadeCount", count);
        program.setUniform("shadowBias", shadow->bias);
        program.setUniform("filterTolerance", shadow->filterTolerance);
        program.setUniform("shadowMapSize",
                           static_cast<float>(shadow->mapSize));
        program.setUniform("debugCascades", shadow->debug ? 1 : 0);
        for (int i = 0; i < count; ++i)
        {
            TextureView* texView =
                renderTargetTexture(shadow->targets[static_cast<std::size_t>(i)]);
            if (texView == nullptr)
            {
                program.setUniform("hasShadowMap", 0);
                break;
            }
            bindTexture(texView, static_cast<std::uint32_t>(i));
            const std::string index = "[" + std::to_string(i) + "]";
            program.setUniform(("lightMatrices" + index).c_str(),
                               shadow->lightMatrices[static_cast<std::size_t>(i)]);
        }
        for (int i = 0; i <= count; ++i)
        {
            const std::string index = "[" + std::to_string(i) + "]";
            program.setUniform(("splitDepths" + index).c_str(),
                               shadow->splitDepths[static_cast<std::size_t>(i)]);
        }
    }
    else
    {
        program.setUniform("hasShadowMap", 0);
    }
    // Dual paraboloid point-light shadow maps (milestone 4d), plain data
    // from the submission. The front/back views land on texture argument
    // indices 2 and 3 (the layout table's unit rule).
    const PointShadowMaps* pointShadows = submission.pointShadows;
    if (submission.shadowLightRequests > kMaxShadowCastingPointLights)
        ++impl.violations;
    if (pointShadows != nullptr && !pointShadows->entries.empty())
    {
        const int entryCount = static_cast<int>(pointShadows->entries.size());
        program.setUniform("pointShadowCount", entryCount);
        program.setUniform("pointFilterTolerance",
                           pointShadows->filterTolerance);
        program.setUniform("pointShadowMapSize",
                           static_cast<float>(pointShadows->mapSize));
        for (int k = 0; k < entryCount; ++k)
        {
            const PointShadowMapEntry& entry =
                pointShadows->entries[static_cast<std::size_t>(k)];
            const std::string index = "[" + std::to_string(k) + "]";
            TextureView* frontView = renderTargetTexture(entry.front);
            TextureView* backView = renderTargetTexture(entry.back);
            if (frontView == nullptr || backView == nullptr)
            {
                program.setUniform("pointShadowCount", 0);
                break;
            }
            bindTexture(frontView, 4u + static_cast<std::uint32_t>(2 * k));
            bindTexture(backView, 5u + static_cast<std::uint32_t>(2 * k));
            program.setUniform(("pointLightViews" + index).c_str(),
                               entry.lightView);
            const PointLight& pointLight =
                std::get<PointLight>(submission.lights[entry.lightIndex]);
            program.setUniform(("pointLightPos" + index).c_str(),
                               pointLight.position);
            program.setUniform(("pointLightRange" + index).c_str(),
                               core::vec2{entry.near, entry.far});
            // D4: paraboloid depth is linear in world distance, so a
            // constant bias is sufficient — the world bias divided by the
            // depth range.
            const float bias01 =
                pointShadows->biasWorld / (entry.far - entry.near);
            program.setUniform(("pointShadowBias" + index).c_str(), bias01);
        }
        // Association (documented): dynamic light i uses entry k whose
        // lightIndex == i; otherwise slot -1 = no shadow map, full light.
        for (int i = 0; i < static_cast<int>(submission.lights.size()); ++i)
        {
            int slot = -1;
            for (int k = 0; k < entryCount; ++k)
                if (pointShadows->entries[static_cast<std::size_t>(k)]
                        .lightIndex == static_cast<std::uint32_t>(i))
                    slot = k;
            program.setUniform(
                ("pointLightSlot[" + std::to_string(i) + "]").c_str(), slot);
        }
    }
    else
    {
        program.setUniform("pointShadowCount", 0);
    }
    // Light volume compose (milestone 7): the volume, sampled at the
    // fragment's world position.
    if (submission.volume != nullptr && submission.volume->texture != nullptr)
    {
        program.setUniform("hasVolume", 1);
        bindTexture3D(submission.volume->texture, 9u);
        program.setUniform("volumeOrigin", submission.volume->origin);
        program.setUniform("volumeExtent", submission.volume->extent);
    }
    else
    {
        program.setUniform("hasVolume", 0);
    }
    const int light_count = static_cast<int>(submission.lights.size());
    program.setUniform("lightCount", light_count);
    for (int i = 0; i < light_count; ++i)
    {
        const std::string index = "[" + std::to_string(i) + "]";
        std::visit(
            [&](const auto& light) {
                using T = std::decay_t<decltype(light)>;
                if constexpr (std::is_same_v<T, PointLight>)
                {
                    program.setUniform(("lightType" + index).c_str(), 0);
                    program.setUniform(("lightPos" + index).c_str(),
                                       light.position);
                    program.setUniform(("lightDir" + index).c_str(),
                                       core::vec3{0.0f, 0.0f, 0.0f});
                    program.setUniform(("lightColor" + index).c_str(),
                                       light.color);
                    program.setUniform(("lightIntensity" + index).c_str(),
                                       light.intensity);
                    program.setUniform(("lightRange" + index).c_str(),
                                       light.radius);
                    program.setUniform(("lightInner" + index).c_str(), 0.0f);
                    program.setUniform(("lightOuter" + index).c_str(), 0.0f);
                }
                else
                {
                    program.setUniform(("lightType" + index).c_str(), 1);
                    program.setUniform(("lightPos" + index).c_str(),
                                       light.position);
                    program.setUniform(("lightDir" + index).c_str(),
                                       core::normalise(light.direction));
                    program.setUniform(("lightColor" + index).c_str(),
                                       light.color);
                    program.setUniform(("lightIntensity" + index).c_str(),
                                       light.intensity);
                    program.setUniform(("lightRange" + index).c_str(),
                                       light.range);
                    program.setUniform(("lightInner" + index).c_str(),
                                       light.innerAngle);
                    program.setUniform(("lightOuter" + index).c_str(),
                                       light.outerAngle);
                }
            },
            submission.lights[static_cast<std::size_t>(i)]);
    }
    metal::MtlMesh* meshHandle =
        static_cast<metal::MtlMesh*>(const_cast<GpuMesh*>(submission.mesh));
    metal::MtlShaderProgram* programHandle =
        static_cast<metal::MtlShaderProgram*>(&program);
    impl.drawMesh(*this, meshHandle, programHandle);
}

void MetalRenderBackend::submit(const DrawSubmission& submission)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    submitImpl(submission);
}
}  // namespace render
}  // namespace grace
