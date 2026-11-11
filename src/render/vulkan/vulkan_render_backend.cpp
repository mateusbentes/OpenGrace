#include "vulkan_render_backend.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

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

#include "vk_loader.hpp"

#include "vulkan_mesh.hpp"
#include "vulkan_render_target.hpp"
#include "vulkan_shader.hpp"

namespace grace::render {

namespace {

// The teardown of a backend instance releases the video subsystem with
// SDL_QuitSubSystem, not SDL_Quit: several backends can live in one process
// (the tests build one per suite), and SDL_Quit would tear down the video
// subsystem for all of them.
void sdlVideoRelease()
{
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
/// Bitwise OR for the loader's scoped flag enums (plain C-style flags).
constexpr vulkan::MemoryProperty operator|(vulkan::MemoryProperty lhs,
                                           vulkan::MemoryProperty rhs)
{
    return static_cast<vulkan::MemoryProperty>(
        static_cast<vulkan::VkFlags>(lhs)
        | static_cast<vulkan::VkFlags>(rhs));
}
constexpr vulkan::BufferUsage operator|(vulkan::BufferUsage lhs,
                                        vulkan::BufferUsage rhs)
{
    return static_cast<vulkan::BufferUsage>(
        static_cast<vulkan::VkFlags>(lhs)
        | static_cast<vulkan::VkFlags>(rhs));
}
constexpr vulkan::ImageUsage operator|(vulkan::ImageUsage lhs,
                                       vulkan::ImageUsage rhs)
{
    return static_cast<vulkan::ImageUsage>(
        static_cast<vulkan::VkFlags>(lhs)
        | static_cast<vulkan::VkFlags>(rhs));
}
constexpr vulkan::ShaderStage operator|(vulkan::ShaderStage lhs,
                                        vulkan::ShaderStage rhs)
{
    return static_cast<vulkan::ShaderStage>(
        static_cast<vulkan::VkFlags>(lhs)
        | static_cast<vulkan::VkFlags>(rhs));
}

/// Maps the project's PixelFormat onto Vulkan formats (inside the Vulkan
/// module only, the same scoping rule the OpenGL backend follows).
vulkan::Format toVkFormat(PixelFormat format)
{
    switch (format)
    {
    case PixelFormat::Rgba8:
        return vulkan::Format::R8G8B8A8Unorm;
    case PixelFormat::Rgba16F:
        return vulkan::Format::R16G16B16A16Sfloat;
    case PixelFormat::R8:
        return vulkan::Format::R8Unorm;
    case PixelFormat::Depth24Stencil8:
        return vulkan::Format::D24UnormS8Uint;
    }
    return vulkan::Format::R8G8B8A8Unorm;
}

/// The device's depth/stencil attachment format: D24UnormS8Uint when the
/// device supports it, falling back to D32Sfloat and then D16Unorm through
/// vkGetPhysicalDeviceFormatProperties (the depth attachment's format
/// feature support is a device property, not an assumption).
vulkan::Format pickDepthFormat(vulkan::VkLoader& loader,
                               vulkan::VkPhysicalDevice physicalDevice)
{
    const vulkan::Format candidates[] = {
        vulkan::Format::D24UnormS8Uint,
        vulkan::Format::D32Sfloat,
        vulkan::Format::D16Unorm,
    };
    for (const vulkan::Format candidate : candidates)
    {
        vulkan::VkFormatProperties properties{};
        loader.vkGetPhysicalDeviceFormatProperties(
            physicalDevice, candidate, &properties);
        if ((properties.optimalTilingFeatures
             & static_cast<vulkan::VkFlags>(
                 vulkan::FormatFeature::DepthStencilAttachment))
            != 0u)
        {
            return candidate;
        }
    }
    // The device reports no depth attachment format: D24S8 stays the
    // contract's declared format and the device's failure surfaces at
    // image creation (the same explicit error the OpenGL backend gives).
    return vulkan::Format::D24UnormS8Uint;
}

/// The SDL_Vulkan bridge's vkGetInstanceProcAddr, matching the loader's
/// ResolveFn signature (void* (*)(const char*)).
vulkan::VkLoader::ResolveFn sdlResolve()
{
    return reinterpret_cast<vulkan::VkLoader::ResolveFn>(
        SDL_Vulkan_GetVkGetInstanceProcAddr());
}

/// Combines a clear colour's 8-bit channels into the packed float the Vulkan
/// clear value carries.
vulkan::VkClearValue clearValueFor(const ClearColor& color, float depth,
                                   std::int32_t stencil)
{
    vulkan::VkClearValue value{};
    value.color[0] = static_cast<float>(color.r) / 255.0f;
    value.color[1] = static_cast<float>(color.g) / 255.0f;
    value.color[2] = static_cast<float>(color.b) / 255.0f;
    value.color[3] = static_cast<float>(color.a) / 255.0f;
    (void)depth;
    (void)stencil;
    return value;
}

vulkan::VkRenderTarget* findVkTarget(
    std::vector<std::unique_ptr<vulkan::VkRenderTarget>>& targets,
    RenderTarget* target)
{
    for (auto& candidate : targets)
        if (candidate.get() == target)
            return candidate.get();
    return nullptr;
}

vulkan::VkTexture3DView* findVkVolume(
    std::vector<std::unique_ptr<vulkan::VkTexture3DView>>& volumes,
    const TextureView* view)
{
    for (auto& candidate : volumes)
        if (candidate.get() == view)
            return candidate.get();
    return nullptr;
}

/// Turns a clear-mask into the Vulkan clear value's colour loadOp.
vulkan::AttachmentLoadOp colorLoadOp(std::uint32_t clear_mask)
{
    return (clear_mask & static_cast<std::uint32_t>(ClearBuffer::Color))
               != 0u
               ? vulkan::AttachmentLoadOp::Clear
               : vulkan::AttachmentLoadOp::Load;
}

/// Same for the depth/stencil attachment (the two load ops are one bit).
vulkan::AttachmentLoadOp depthLoadOp(std::uint32_t clear_mask)
{
    return (clear_mask
            & static_cast<std::uint32_t>(ClearBuffer::Depth
                                         | ClearBuffer::Stencil))
               != 0u
               ? vulkan::AttachmentLoadOp::Clear
               : vulkan::AttachmentLoadOp::Load;
}

}  // namespace

struct VulkanRenderBackend::Impl
{
    enum class State
    {
        Created,
        Initialised,
        Shutdown
    };

    VulkanBackendConfig config;
    vulkan::VkLoader loader;

    SDL_Window* window = nullptr;
    vulkan::VkInstance instance = nullptr;
    vulkan::VkSurfaceKHR surface = nullptr;
    vulkan::VkPhysicalDevice physicalDevice = nullptr;
    vulkan::VkDevice device = nullptr;
    vulkan::VkQueue queue = nullptr;
    std::uint32_t queueFamily = 0;
    vulkan::VkCommandPool commandPool = nullptr;
    vulkan::VkSwapchainKHR swapchain = nullptr;
    vulkan::VkExtent2D swapchainExtent{0, 0};
    vulkan::Format swapchainFormat = vulkan::Format::R8G8B8A8Unorm;
    std::vector<vulkan::VkImage> swapchainImages;
    std::vector<vulkan::VkImageView> swapchainViews;
    std::vector<vulkan::VkFramebuffer> swapchainFramebuffers;
    vulkan::VkRenderPass swapchainRenderPass = nullptr;
    std::uint32_t swapchainClearMask = 0;
    // The device's depth/stencil attachment format (the fallback chain D24S8
    // -> D32Sfloat -> D16Unorm ran at initialise; every depth attachment, the
    // render passes and the readback unpack use this one).
    vulkan::Format depthFormat = vulkan::Format::D24UnormS8Uint;
    vulkan::VkSemaphore imageAcquired = nullptr;
    vulkan::VkSemaphore renderDone = nullptr;
    vulkan::VkFence frameDone = nullptr;
    std::uint32_t nextImageIndex = 0;
    // Whether the open frame acquired a swapchain image (an off-screen
    // target's frame has no semaphore to wait on and nothing to present).
    bool swapchain_frame = false;
    // A present that returns OUT_OF_DATE or SUBOPTIMAL marks the swapchain
    // stale: the next frame's acquire recreates it (the surface resize path).
    bool swapchainStale = false;
    State state = State::Created;
    int api_major = 0;
    int api_minor = 0;
    std::string device_name;
    std::string vendor_name;

    // Device memory allocations (the backend owns them all; the shutdown
    // frees them in reverse order).
    std::vector<vulkan::VkDeviceMemory> memory_blocks;

    // The default framebuffer's depth image (the swapchain's depth
    // attachment; the contract's clear() clears depth/stencil on it).
    vulkan::VkImage swapchainDepthImage = nullptr;
    vulkan::VkDeviceMemory swapchainDepthMemory = nullptr;
    vulkan::VkImageView swapchainDepthView = nullptr;

    // The frame's primary command buffer (reset and recorded per frame).
    vulkan::VkCommandBuffer commandBuffer = nullptr;

    // The readback staging buffer (host-visible, mapped for the CPU).
    vulkan::VkBuffer readbackBuffer = nullptr;
    vulkan::VkDeviceMemory readbackMemory = nullptr;
    void* readbackMapped = nullptr;

    // The one shared sampler (nearest, clamp-to-edge): the GLSL's
    // sampler2D/sampler3D uniforms each combine a sampler with an image
    // view at descriptor time, and the engine's sampling rules (the
    // shadow maps, the volumes, the post-chain inputs) are all nearest
    // with clamp-to-edge, the same rules the OpenGL backend's
    // GL_NEAREST/GL_CLAMP_TO_EDGE state implements.
    vulkan::VkSampler sharedSampler = nullptr;
    // The default texture (1x1 opaque white): the descriptor set's sampler
    // bindings the engine never writes still name an image view, and the
    // shader samples a valid, opaque image either way.
    vulkan::VkImage defaultImage = nullptr;
    vulkan::VkDeviceMemory defaultImageMemory = nullptr;
    vulkan::VkImageView defaultImageView = nullptr;

    ClearColor clear_color{0, 0, 0, 0};
    std::uint32_t clear_mask = static_cast<std::uint32_t>(ClearBuffer::Color);
    float clear_depth = 1.0f;
    std::int32_t clear_stencil = 0;
    // Device state (decision 20): when false, passes run without a depth
    // test and without depth writes, so fullscreen passes never overwrite a
    // target's scene depth. Default true; geometry passes rely on it.
    bool depth_test = true;

    bool has_center = false;
    ClearColor center_pixel{0, 0, 0, 0};
    float center_depth = 0.0f;
    std::int32_t center_stencil = 0;

    std::vector<std::pair<std::uint32_t, std::uint32_t>> readback_queue;
    std::vector<ReadbackSample> last_readback;
    // The pixel readback() last copied into the staging buffer (the read
    // path maps the buffer and copies the values out).
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
    std::vector<std::unique_ptr<vulkan::VkRenderTarget>> targets;
    vulkan::VkRenderTarget* bound_target = nullptr;
    // The frame's open render pass (the target it renders into; the
    // command buffer records every pass between beginFrame/endFrame).
    vulkan::VkRenderTarget* bound_pass = nullptr;
    bool frame_open = false;
    bool pass_open = false;

    // 3D textures: backend-owned registry.
    std::vector<std::unique_ptr<vulkan::VkTexture3DView>> volumes;

    // Camera used by submit()/draw() (documented diagnostic default).
    CameraParams camera;

    // Per-program device objects. The backend owns the pipeline layout and
    // the descriptor set; the program's VkShaderProgram holds the SPIR-V.
    struct ProgramResources
    {
        vulkan::VkBuffer uniformBuffer = nullptr;
        vulkan::VkDeviceMemory uniformMemory = nullptr;
        vulkan::VkDescriptorSetLayout setLayout = nullptr;
        vulkan::VkDescriptorSet set = nullptr;
        vulkan::VkPipelineLayout pipelineLayout = nullptr;
    };
    std::map<const void*, ProgramResources> programResources;

    // Per-program pipelines (the pipeline is per program + depth test +
    // render pass compatibility). The key is the program's SPIR-V pair
    // (the same bytes the program's shader stages were built from).
    struct PipelineKey
    {
        const void* vertexSpirv = nullptr;
        const void* fragmentSpirv = nullptr;
        bool depthTest = true;
        vulkan::VkRenderPass renderPass = nullptr;
        bool operator<(const PipelineKey& other) const
        {
            if (vertexSpirv != other.vertexSpirv)
                return vertexSpirv < other.vertexSpirv;
            if (fragmentSpirv != other.fragmentSpirv)
                return fragmentSpirv < other.fragmentSpirv;
            if (depthTest != other.depthTest)
                return depthTest < other.depthTest;
            return renderPass < other.renderPass;
        }
    };
    std::map<PipelineKey, vulkan::VkPipeline> pipelines;

    // Per-target device objects: the backend owns the VkImages, the
    // VkImageViews, the VkRenderPass and the VkFramebuffer. The target's
    // handle is plain data the engine holds through the contract.
    struct TargetResources
    {
        vulkan::VkRenderPass renderPass = nullptr;
        vulkan::VkFramebuffer framebuffer = nullptr;
        std::vector<vulkan::VkImage> images;
        std::vector<vulkan::VkImageView> views;
        std::vector<vulkan::VkDeviceMemory> memories;
    };
    std::map<const RenderTarget*, TargetResources> targetResources;

    // Per-volume device objects: the backend owns the VkImage and the
    // VkImageView (the volume's view reports the volume's layer count).
    struct VolumeResources
    {
        vulkan::VkImage image = nullptr;
        vulkan::VkImageView view = nullptr;
        vulkan::VkDeviceMemory memory = nullptr;
    };
    std::map<const TextureView*, VolumeResources> volumeResources;
    // The texture views bound to sampler units at draw time. The draw's
    // descriptor set write binds them at the sampler binding the shader's
    // layout table declares.
    std::vector<const TextureView*> boundTextures;

    bool ready() const { return state == State::Initialised; }

    /// The memory-type bits a device image accepts (the allocation picks
    /// the matching device-local type among them).
    std::uint32_t imageMemoryBits(vulkan::VkImage image) const
    {
        vulkan::VkMemoryRequirements requirements{};
        loader.vkGetImageMemoryRequirements(device, image, &requirements);
        return requirements.memoryTypeBits;
    }

    // -- Device resource helpers -------------------------------------------
    // The memory type the backend's own allocations land on: host-visible
    // for the staging buffers (the CPU writes them), device-local for the
    // render targets (the GPU writes them).
    std::uint32_t findMemoryType(std::uint32_t typeBits,
                                 vulkan::MemoryProperty properties) const
    {
        vulkan::VkPhysicalDeviceMemoryProperties memory{};
        loader.vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memory);
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        {
            if ((typeBits & (1u << i)) != 0u
                && (memory.memoryTypes[i].propertyFlags
                    & static_cast<vulkan::VkFlags>(properties))
                       == static_cast<vulkan::VkFlags>(properties))
            {
                return i;
            }
        }
        return 0xFFFFFFFFu;
    }

    /// Allocates a VkDeviceMemory of the given size and properties and
    /// registers it for shutdown (the backend owns the memory).
    vulkan::VkDeviceMemory allocateMemory(std::size_t bytes,
                                          vulkan::MemoryProperty properties)
    {
        vulkan::VkMemoryAllocateInfo info{};
        info.allocationSize =
            static_cast<vulkan::VkDeviceSize>(bytes);
        info.memoryTypeIndex =
            findMemoryType(0xFFFFFFFFu, properties);
        if (info.memoryTypeIndex == 0xFFFFFFFFu)
            throw std::runtime_error(
                "VulkanRenderBackend: no matching memory type");
        vulkan::VkDeviceMemory memory = nullptr;
        if (loader.vkAllocateMemory(device, &info, nullptr, &memory)
            != vulkan::Result::Success)
            throw std::runtime_error("VulkanRenderBackend: "
                                     "vkAllocateMemory failed");
        memory_blocks.push_back(memory);
        return memory;
    }

    /// Allocates a VkDeviceMemory for a resource of the given type bits
    /// (the memory type index is constrained by the resource's own bits).
    vulkan::VkDeviceMemory allocateMemoryFor(std::uint32_t typeBits,
                                             std::size_t bytes,
                                             vulkan::MemoryProperty properties)
    {
        vulkan::VkMemoryAllocateInfo info{};
        info.allocationSize =
            static_cast<vulkan::VkDeviceSize>(bytes);
        info.memoryTypeIndex = findMemoryType(typeBits, properties);
        if (info.memoryTypeIndex == 0xFFFFFFFFu)
            throw std::runtime_error(
                "VulkanRenderBackend: no matching memory type");
        vulkan::VkDeviceMemory memory = nullptr;
        if (loader.vkAllocateMemory(device, &info, nullptr, &memory)
            != vulkan::Result::Success)
            throw std::runtime_error("VulkanRenderBackend: "
                                     "vkAllocateMemory failed");
        memory_blocks.push_back(memory);
        return memory;
    }

    /// Creates a host-visible buffer (the CPU writes the staging data).
    void createHostBuffer(vulkan::VkFlags usage, std::size_t bytes,
                          vulkan::VkBuffer& buffer,
                          vulkan::VkDeviceMemory& memory)
    {
        vulkan::VkBufferCreateInfo info{};
        info.size = bytes;
        info.usage = usage;
        info.sharingMode = vulkan::SharingMode::Exclusive;
        if (loader.vkCreateBuffer(device, &info, nullptr, &buffer)
            != vulkan::Result::Success)
            throw std::runtime_error("VulkanRenderBackend: "
                                     "vkCreateBuffer failed");
        vulkan::VkMemoryRequirements requirements{};
        loader.vkGetBufferMemoryRequirements(device, buffer, &requirements);
        memory = allocateMemoryFor(
            requirements.memoryTypeBits,
            static_cast<std::size_t>(requirements.size),
            vulkan::MemoryProperty::HostVisible
                | vulkan::MemoryProperty::HostCoherent);
        if (loader.vkBindBufferMemory(device, buffer, memory, 0)
            != vulkan::Result::Success)
            throw std::runtime_error("VulkanRenderBackend: "
                                     "vkBindBufferMemory failed");
    }

    /// Creates a VkImage of the given format and usage (a 2D image).
    vulkan::VkImage createImage(vulkan::Format format, std::uint32_t width,
                                std::uint32_t height,
                                vulkan::VkFlags usage, vulkan::VkFlags samples)
    {
        vulkan::VkImageCreateInfo info{};
        info.imageType = vulkan::ImageType::Type2D;
        info.format = format;
        info.width = width;
        info.height = height;
        info.depth = 1;
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = static_cast<vulkan::SampleCount>(samples);
        info.tiling = vulkan::ImageTiling::Optimal;
        info.usage = usage;
        info.sharingMode = vulkan::SharingMode::Exclusive;
        info.initialLayout = vulkan::ImageLayout::Undefined;
        vulkan::VkImage image = nullptr;
        if (loader.vkCreateImage(device, &info, nullptr, &image)
            != vulkan::Result::Success)
            throw std::runtime_error("VulkanRenderBackend: "
                                     "vkCreateImage failed");
        return image;
    }

    vulkan::VkImage createVolumeImage(vulkan::Format format,
                                      std::uint32_t width,
                                      std::uint32_t height,
                                      std::uint32_t depth,
                                      vulkan::VkFlags usage)
    {
        vulkan::VkImageCreateInfo info{};
        info.imageType = vulkan::ImageType::Type3D;
        info.format = format;
        info.width = width;
        info.height = height;
        info.depth = depth;
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = vulkan::SampleCount::One;
        info.tiling = vulkan::ImageTiling::Optimal;
        info.usage = usage;
        info.sharingMode = vulkan::SharingMode::Exclusive;
        info.initialLayout = vulkan::ImageLayout::Undefined;
        vulkan::VkImage image = nullptr;
        if (loader.vkCreateImage(device, &info, nullptr, &image)
            != vulkan::Result::Success)
            throw std::runtime_error("VulkanRenderBackend: "
                                     "vkCreateImage failed");
        return image;
    }

    /// Creates a VkImageView of the given image (a 2D view).
    vulkan::VkImageView createImageView(vulkan::VkImage image,
                                        vulkan::Format format,
                                        vulkan::ImageAspect aspect)
    {
        vulkan::VkImageViewCreateInfo info{};
        info.image = image;
        info.viewType = vulkan::ImageType::Type2D;
        info.format = format;
        info.components = vulkan::VkComponentMapping{};
        info.subresourceRange.aspectMask = aspect;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount = 1;
        vulkan::VkImageView view = nullptr;
        if (loader.vkCreateImageView(device, &info, nullptr, &view)
            != vulkan::Result::Success)
            throw std::runtime_error("VulkanRenderBackend: "
                                     "vkCreateImageView failed");
        return view;
    }

    /// Creates a VkImageView of a volume's one layer (the layer target
    /// borrows the volume's image, milestone 7).
    vulkan::VkImageView createVolumeLayerView(vulkan::VkImage image,
                                              vulkan::Format format,
                                              std::uint32_t layer)
    {
        vulkan::VkImageViewCreateInfo info{};
        info.image = image;
        info.viewType = vulkan::ImageType::Type2D;
        info.format = format;
        info.components = vulkan::VkComponentMapping{};
        info.subresourceRange.aspectMask = vulkan::ImageAspect::Color;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = layer;
        info.subresourceRange.layerCount = 1;
        vulkan::VkImageView view = nullptr;
        if (loader.vkCreateImageView(device, &info, nullptr, &view)
            != vulkan::Result::Success)
            throw std::runtime_error("VulkanRenderBackend: "
                                     "vkCreateImageView failed");
        return view;
    }

    /// Creates a one-off command buffer (the backend's single command
    /// buffer, reset each frame) and records one pass.
    vulkan::VkCommandBuffer beginOneOff()
    {
        vulkan::VkCommandBufferAllocateInfo info{};
        info.commandPool = commandPool;
        info.level = vulkan::CommandBufferLevel::Primary;
        info.commandBufferCount = 1;
        vulkan::VkCommandBuffer oneOff = nullptr;
        if (loader.vkAllocateCommandBuffers(device, &info, &oneOff)
            != vulkan::Result::Success)
            throw std::runtime_error("VulkanRenderBackend: "
                                     "vkAllocateCommandBuffers failed");
        vulkan::VkCommandBufferBeginInfo begin{};
        begin.flags = static_cast<vulkan::VkFlags>(
            vulkan::CommandBufferUsage::OneTimeSubmit);
        if (loader.vkBeginCommandBuffer(oneOff, &begin)
            != vulkan::Result::Success)
            throw std::runtime_error("VulkanRenderBackend: "
                                     "vkBeginCommandBuffer failed");
        return oneOff;
    }

    void submitOneOff(vulkan::VkCommandBuffer oneOff)
    {
        if (loader.vkEndCommandBuffer(oneOff)
            != vulkan::Result::Success)
            throw std::runtime_error("VulkanRenderBackend: "
                                     "vkEndCommandBuffer failed");
        vulkan::VkSubmitInfo submit{};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &oneOff;
        loader.vkQueueSubmit(queue, 1, &submit, nullptr);
        loader.vkQueueWaitIdle(queue);
    }

    /// Records the layout transition of one image into a command buffer
    /// the caller owns (a one-off or the frame's command buffer).
    void recordLayoutTransition(vulkan::VkCommandBuffer command_buffer,
                                vulkan::VkImage image,
                                vulkan::ImageAspect aspect,
                                vulkan::ImageLayout oldLayout,
                                vulkan::ImageLayout newLayout)
    {
        vulkan::VkImageMemoryBarrier barrier{};
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = 0xFFFFFFFFu;
        barrier.dstQueueFamilyIndex = 0xFFFFFFFFu;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspect;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        vulkan::PipelineStage srcStage = vulkan::PipelineStage::TopOfPipe;
        vulkan::PipelineStage dstStage = vulkan::PipelineStage::BottomOfPipe;
        vulkan::VkFlags srcAccess = 0;
        vulkan::VkFlags dstAccess = 0;
        if (newLayout == vulkan::ImageLayout::ColorAttachmentOptimal)
        {
            dstStage = vulkan::PipelineStage::ColorAttachmentOutput;
            dstAccess = static_cast<vulkan::VkFlags>(
                vulkan::Access::ColorAttachmentWrite);
        }
        else if (newLayout == vulkan::ImageLayout::DepthStencilAttachmentOptimal)
        {
            dstStage = vulkan::PipelineStage::EarlyFragmentTests;
            dstAccess = static_cast<vulkan::VkFlags>(
                vulkan::Access::DepthStencilAttachmentWrite);
        }
        else if (newLayout == vulkan::ImageLayout::ShaderReadOnlyOptimal)
        {
            dstStage = vulkan::PipelineStage::FragmentShader;
            dstAccess = static_cast<vulkan::VkFlags>(
                vulkan::Access::ShaderRead);
        }
        else if (newLayout == vulkan::ImageLayout::TransferSrcOptimal)
        {
            dstStage = vulkan::PipelineStage::Transfer;
            dstAccess = static_cast<vulkan::VkFlags>(
                vulkan::Access::TransferRead);
        }
        else if (newLayout == vulkan::ImageLayout::TransferDstOptimal)
        {
            dstStage = vulkan::PipelineStage::Transfer;
            dstAccess = static_cast<vulkan::VkFlags>(
                vulkan::Access::TransferWrite);
        }
        if (oldLayout == vulkan::ImageLayout::ColorAttachmentOptimal)
        {
            srcStage = vulkan::PipelineStage::ColorAttachmentOutput;
            srcAccess = static_cast<vulkan::VkFlags>(
                vulkan::Access::ColorAttachmentWrite);
        }
        else if (oldLayout == vulkan::ImageLayout::DepthStencilAttachmentOptimal)
        {
            srcStage = vulkan::PipelineStage::EarlyFragmentTests;
            srcAccess = static_cast<vulkan::VkFlags>(
                vulkan::Access::DepthStencilAttachmentWrite);
        }
        else if (oldLayout == vulkan::ImageLayout::ShaderReadOnlyOptimal)
        {
            srcStage = vulkan::PipelineStage::FragmentShader;
            srcAccess = static_cast<vulkan::VkFlags>(
                vulkan::Access::ShaderRead);
        }
        else if (oldLayout == vulkan::ImageLayout::TransferSrcOptimal)
        {
            srcStage = vulkan::PipelineStage::Transfer;
            srcAccess = static_cast<vulkan::VkFlags>(
                vulkan::Access::TransferRead);
        }
        else if (oldLayout == vulkan::ImageLayout::TransferDstOptimal)
        {
            srcStage = vulkan::PipelineStage::Transfer;
            srcAccess = static_cast<vulkan::VkFlags>(
                vulkan::Access::TransferWrite);
        }
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        loader.vkCmdPipelineBarrier(command_buffer, srcStage, dstStage, 0,
                                    0,
                                    nullptr, 0, nullptr, 1, &barrier);
    }

    void transitionImageLayout(vulkan::VkImage image, vulkan::Format format,
                               vulkan::ImageAspect aspect,
                               vulkan::ImageLayout oldLayout,
                               vulkan::ImageLayout newLayout)
    {
        (void)format;  // the barrier keys on layouts, not on the format
        vulkan::VkCommandBuffer oneOff = beginOneOff();
        recordLayoutTransition(oneOff, image, aspect, oldLayout, newLayout);
        loader.vkEndCommandBuffer(oneOff);
        vulkan::VkSubmitInfo submit{};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &oneOff;
        loader.vkQueueSubmit(queue, 1, &submit, nullptr);
        loader.vkQueueWaitIdle(queue);
    }

    /// Copies the CPU-side data of a volume's one layer into the volume's
    /// VkImage (the layer target's borrowed attachment).
    void fillVolumeLayer(VulkanRenderBackend& backend,
                         vulkan::VkTexture3DView* volume,
                         std::uint32_t layer, const void* data,
                         std::size_t sizeBytes)
    {
        vulkan::VkBuffer stagingBuffer = nullptr;
        vulkan::VkDeviceMemory stagingMemory = nullptr;
        createHostBuffer(
            static_cast<vulkan::VkFlags>(vulkan::BufferUsage::TransferSrc),
            sizeBytes, stagingBuffer, stagingMemory);
        void* mapped = nullptr;
        loader.vkMapMemory(device, stagingMemory, 0, sizeBytes, 0, &mapped);
        std::memcpy(mapped, data, sizeBytes);
        loader.vkUnmapMemory(device, stagingMemory);
        vulkan::VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vulkan::ImageAspect::Color;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = layer;
        region.imageSubresource.layerCount = 1;
        // A VK_IMAGE_TYPE_3D image addresses its slices through z, not
        // through the array layer range (that is a Type2D image's scheme).
        region.imageOffset = {0, 0,
                              static_cast<std::int32_t>(layer)};
        region.imageExtent = {volume->width(), volume->height(), 1};
        vulkan::VkCommandBuffer oneOff = beginOneOff();
        transitionImageLayout(volume->image(), toVkFormat(volume->format()),
                              vulkan::ImageAspect::Color,
                              vulkan::ImageLayout::Undefined,
                              vulkan::ImageLayout::TransferDstOptimal);
        loader.vkCmdCopyBufferToImage(oneOff, stagingBuffer,
                                      volume->image(),
                                      vulkan::ImageLayout::TransferDstOptimal,
                                      1, &region);
        transitionImageLayout(volume->image(), toVkFormat(volume->format()),
                              vulkan::ImageAspect::Color,
                              vulkan::ImageLayout::TransferDstOptimal,
                              vulkan::ImageLayout::General);
        loader.vkEndCommandBuffer(oneOff);
        vulkan::VkSubmitInfo submit{};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &oneOff;
        loader.vkQueueSubmit(queue, 1, &submit, nullptr);
        loader.vkQueueWaitIdle(queue);
        (void)backend;
    }

    /// Reads one layer of a volume back into the caller's buffer.
    bool readVolumeLayer(VulkanRenderBackend& backend,
                         vulkan::VkTexture3DView* volume,
                         std::uint32_t layer, void* data,
                         std::size_t sizeBytes)
    {
        vulkan::VkBuffer readBuffer = nullptr;
        vulkan::VkDeviceMemory readMemory = nullptr;
        createHostBuffer(
            static_cast<vulkan::VkFlags>(
                vulkan::BufferUsage::TransferDst),
            sizeBytes, readBuffer, readMemory);
        vulkan::VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vulkan::ImageAspect::Color;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = layer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0,
                              static_cast<std::int32_t>(layer)};
        region.imageExtent = {volume->width(), volume->height(), 1};
        transitionImageLayout(volume->image(), toVkFormat(volume->format()),
                              vulkan::ImageAspect::Color,
                              vulkan::ImageLayout::General,
                              vulkan::ImageLayout::TransferSrcOptimal);
        vulkan::VkCommandBuffer oneOff = beginOneOff();
        loader.vkCmdCopyImageToBuffer(oneOff, volume->image(),
                                      vulkan::ImageLayout::TransferSrcOptimal,
                                      readBuffer, 1, &region);
        loader.vkEndCommandBuffer(oneOff);
        vulkan::VkSubmitInfo submit{};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &oneOff;
        loader.vkQueueSubmit(queue, 1, &submit, nullptr);
        loader.vkQueueWaitIdle(queue);
        transitionImageLayout(volume->image(), toVkFormat(volume->format()),
                              vulkan::ImageAspect::Color,
                              vulkan::ImageLayout::TransferSrcOptimal,
                              vulkan::ImageLayout::General);
        void* mapped = nullptr;
        loader.vkMapMemory(device, readMemory, 0, sizeBytes, 0, &mapped);
        std::memcpy(data, mapped, sizeBytes);
        loader.vkUnmapMemory(device, readMemory);
        loader.vkDestroyBuffer(device, readBuffer, nullptr);
        // The staging memory dies with the backend (it stays registered).
        (void)backend;
        return true;
    }

    /// Creates a device-local VkBuffer (the GPU writes it).
    vulkan::VkBuffer createDeviceBuffer(vulkan::VkFlags usage,
                                        std::size_t bytes,
                                        vulkan::VkDeviceMemory& memory)
    {
        vulkan::VkBufferCreateInfo info{};
        info.size = bytes;
        info.usage = usage;
        info.sharingMode = vulkan::SharingMode::Exclusive;
        vulkan::VkBuffer buffer = nullptr;
        if (loader.vkCreateBuffer(device, &info, nullptr, &buffer)
            != vulkan::Result::Success)
            throw std::runtime_error(
                "VulkanRenderBackend: vkCreateBuffer failed");
        vulkan::VkMemoryRequirements requirements{};
        loader.vkGetBufferMemoryRequirements(device, buffer, &requirements);
        memory = allocateMemoryFor(
            requirements.memoryTypeBits,
            static_cast<std::size_t>(requirements.size),
            vulkan::MemoryProperty::DeviceLocal);
        if (loader.vkBindBufferMemory(device, buffer, memory, 0)
            != vulkan::Result::Success)
            throw std::runtime_error(
                "VulkanRenderBackend: vkBindBufferMemory failed");
        return buffer;
    }

    /// Copies the CPU-side data of a mesh into the backend's device buffers.
    void createMeshBuffers(VulkanRenderBackend& backend,
                           vulkan::VkMesh* mesh)
    {
        vulkan::VkBuffer stagingBuffer = nullptr;
        vulkan::VkDeviceMemory stagingMemory = nullptr;
        const std::size_t vertexBytes = mesh->vertexBytes();
        const std::size_t indexBytes = mesh->indices().size()
            * sizeof(std::uint32_t);
        // The staging buffer holds both uploads: the vertices at offset 0
        // and the indices at offset vertexBytes (the copy region below).
        const std::size_t stagingBytes = vertexBytes + indexBytes;
        createHostBuffer(
            static_cast<vulkan::VkFlags>(
                vulkan::BufferUsage::TransferSrc),
            stagingBytes, stagingBuffer, stagingMemory);
        void* mapped = nullptr;
        loader.vkMapMemory(device, stagingMemory, 0, stagingBytes, 0, &mapped);
        if (vertexBytes > 0)
            std::memcpy(mapped, mesh->vertexData().data(), vertexBytes);
        if (indexBytes > 0)
            std::memcpy(
                static_cast<std::byte*>(mapped) + vertexBytes,
                mesh->indices().data(), indexBytes);
        loader.vkUnmapMemory(device, stagingMemory);

        vulkan::VkBuffer vertexBuffer = nullptr;
        vulkan::VkBuffer indexBuffer = nullptr;
        vulkan::VkDeviceMemory vertexMemory = nullptr;
        vulkan::VkDeviceMemory indexMemory = nullptr;
        if (vertexBytes > 0)
        {
            vertexBuffer = createDeviceBuffer(
                static_cast<vulkan::VkFlags>(
                    vulkan::BufferUsage::TransferDst
                    | vulkan::BufferUsage::VertexBuffer),
                vertexBytes, vertexMemory);
        }
        if (indexBytes > 0)
        {
            indexBuffer = createDeviceBuffer(
                static_cast<vulkan::VkFlags>(
                    vulkan::BufferUsage::TransferDst
                    | vulkan::BufferUsage::IndexBuffer),
                indexBytes, indexMemory);
        }
        vulkan::VkCommandBuffer oneOff = beginOneOff();
        vulkan::VkBufferCopy region{};
        if (vertexBuffer != nullptr)
        {
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = vertexBytes;
            loader.vkCmdCopyBuffer(oneOff, stagingBuffer,
                                   vertexBuffer, 1, &region);
        }
        if (indexBuffer != nullptr)
        {
            region.srcOffset = vertexBytes;
            region.dstOffset = 0;
            region.size = indexBytes;
            loader.vkCmdCopyBuffer(oneOff, stagingBuffer,
                                   indexBuffer, 1, &region);
        }
        loader.vkEndCommandBuffer(oneOff);
        vulkan::VkSubmitInfo submit{};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &oneOff;
        loader.vkQueueSubmit(queue, 1, &submit, nullptr);
        loader.vkQueueWaitIdle(queue);
        loader.vkDestroyBuffer(device, stagingBuffer, nullptr);
        mesh->setGpuBuffers(vertexBuffer, indexBuffer);
        (void)backend;
    }

    /// Creates a VkRenderPass for the given attachment-format combination.
    vulkan::VkRenderPass createRenderPass(vulkan::Format colorFormat,
                                          vulkan::Format depthFormatParam,
                                          std::uint32_t clearMask,
                                          bool withDepth = true,
                                          bool withColor = true,
                                          vulkan::ImageLayout
                                              colourInitialLayout =
                                                  vulkan::ImageLayout::
                                                      Undefined,
                                          vulkan::ImageLayout
                                              colourFinalLayout =
                                                  vulkan::ImageLayout::
                                                      ColorAttachmentOptimal)
    {
        vulkan::VkAttachmentDescription colorAttachment{};
        colorAttachment.format = colorFormat;
        colorAttachment.samples = vulkan::SampleCount::One;
        colorAttachment.loadOp = colorLoadOp(clearMask);
        colorAttachment.storeOp = vulkan::AttachmentStoreOp::Store;
        colorAttachment.stencilLoadOp = vulkan::AttachmentLoadOp::DontCare;
        colorAttachment.stencilStoreOp = vulkan::AttachmentStoreOp::DontCare;
        // A borrowed volume layer lives in General between passes (the fill
        // and read paths transition into it), so the pass restores that.
        colorAttachment.initialLayout = colourInitialLayout;
        colorAttachment.finalLayout = colourFinalLayout;
        vulkan::VkAttachmentDescription depthAttachment{};
        depthAttachment.format = depthFormatParam;
        depthAttachment.samples = vulkan::SampleCount::One;
        depthAttachment.loadOp = depthLoadOp(clearMask);
        depthAttachment.storeOp = vulkan::AttachmentStoreOp::Store;
        depthAttachment.stencilLoadOp = depthLoadOp(clearMask);
        depthAttachment.stencilStoreOp =
            vulkan::AttachmentStoreOp::Store;
        depthAttachment.initialLayout =
            vulkan::ImageLayout::Undefined;
        depthAttachment.finalLayout =
            vulkan::ImageLayout::DepthStencilAttachmentOptimal;
        vulkan::VkAttachmentReference colorReference{};
        colorReference.attachment = 0;
        colorReference.layout =
            vulkan::ImageLayout::ColorAttachmentOptimal;
        vulkan::VkAttachmentReference depthReference{};
        depthReference.attachment = 1;
        depthReference.layout =
            vulkan::ImageLayout::DepthStencilAttachmentOptimal;
        if (!withColor)
        {
            // A depth-only pass's single attachment is the depth one: the
            // framebuffer carries the depth view at index 0.
            depthReference.attachment = 0;
        }
        vulkan::VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = vulkan::PipelineBindPoint::Graphics;
        // A depth-only pass (the shadow targets) references the depth
        // attachment alone; the colour reference stays unused then.
        subpass.colorAttachmentCount = withColor ? 1u : 0u;
        subpass.pColorAttachments = withColor ? &colorReference : nullptr;
        // A depthless pass (the volume layer targets) references the colour
        // attachment alone: the framebuffer carries one view, so a depth
        // reference would name an attachment the framebuffer does not have.
        subpass.pDepthStencilAttachment = withDepth ? &depthReference : nullptr;
        vulkan::VkAttachmentDescription attachments[2] = {
            colorAttachment, depthAttachment};
        vulkan::VkRenderPassCreateInfo info{};
        info.pAttachments = attachments;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.attachmentCount = withDepth ? (withColor ? 2u : 1u) : 1u;
        vulkan::VkRenderPass renderPass = nullptr;
        if (loader.vkCreateRenderPass(device, &info, nullptr, &renderPass)
            != vulkan::Result::Success)
            throw std::runtime_error(
                "VulkanRenderBackend: vkCreateRenderPass failed");
        return renderPass;
    }

    /// Creates a VkFramebuffer for the target's attachments.
    vulkan::VkFramebuffer createFramebuffer(
        vulkan::VkRenderPass renderPass, std::uint32_t width,
        std::uint32_t height,
        const vulkan::VkImageView* views, std::uint32_t viewCount)
    {
        vulkan::VkFramebufferCreateInfo info{};
        info.renderPass = renderPass;
        info.attachmentCount = viewCount;
        info.pAttachments = views;
        info.width = width;
        info.height = height;
        info.layers = 1;
        vulkan::VkFramebuffer framebuffer = nullptr;
        if (loader.vkCreateFramebuffer(device, &info, nullptr, &framebuffer)
            != vulkan::Result::Success)
            throw std::runtime_error(
                "VulkanRenderBackend: vkCreateFramebuffer failed");
        return framebuffer;
    }

    /// Creates a VkPipeline for the given SPIR-V pair, depth test and
    /// render pass compatibility (the pipeline is per program + depth
    /// test + render pass; the pipeline is the pipeline cache).
    vulkan::VkPipeline createPipeline(
        vulkan::VkShaderProgram* program, bool depthTest,
        vulkan::VkRenderPass renderPass,
        vulkan::VkPipelineLayout pipelineLayout)
    {
        vulkan::VkShaderModuleCreateInfo vertexInfo{};
        vertexInfo.codeSize = program->vertexSpirvWords()
            * sizeof(std::uint32_t);
        vertexInfo.pCode = program->vertexSpirv();
        vulkan::VkShaderModule vertexModule = nullptr;
        if (loader.vkCreateShaderModule(device, &vertexInfo, nullptr,
                                        &vertexModule)
            != vulkan::Result::Success)
            throw std::runtime_error(
                "VulkanRenderBackend: vkCreateShaderModule failed");
        vulkan::VkShaderModuleCreateInfo fragmentInfo{};
        fragmentInfo.codeSize = program->fragmentSpirvWords()
            * sizeof(std::uint32_t);
        fragmentInfo.pCode = program->fragmentSpirv();
        vulkan::VkShaderModule fragmentModule = nullptr;
        if (loader.vkCreateShaderModule(device, &fragmentInfo, nullptr,
                                        &fragmentModule)
            != vulkan::Result::Success)
            throw std::runtime_error(
                "VulkanRenderBackend: vkCreateShaderModule failed");
        vulkan::VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].stage = vulkan::ShaderStage::Vertex;
        stages[0].module = vertexModule;
        stages[0].pName = "main";
        stages[1].stage = vulkan::ShaderStage::Fragment;
        stages[1].module = fragmentModule;
        stages[1].pName = "main";
        // The engine's meshes store position at location 0 with a
        // 3-float stride (the layout the FlatUnlit vertex shader reads).
        vulkan::VkVertexInputBindingDescription vertexBinding{};
        vertexBinding.binding = 0;
        vertexBinding.stride = 3 * sizeof(float);
        vertexBinding.inputRate = vulkan::VertexInputRate::Vertex;
        vulkan::VkVertexInputAttributeDescription vertexAttribute{};
        vertexAttribute.location = 0;
        vertexAttribute.binding = 0;
        vertexAttribute.format = vulkan::Format::R32G32B32Sfloat;
        vertexAttribute.offset = 0;
        vulkan::VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &vertexBinding;
        vertexInput.vertexAttributeDescriptionCount = 1;
        vertexInput.pVertexAttributeDescriptions = &vertexAttribute;
        vulkan::VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vulkan::PrimitiveTopology::TriangleList;
        vulkan::VkPipelineViewportStateCreateInfo viewportState{};
        vulkan::VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(config.width);
        viewport.height = static_cast<float>(config.height);
        viewport.maxDepth = 1.0f;
        vulkan::VkRect2D scissor{};
        scissor.extent = {config.width, config.height};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;
        vulkan::VkPipelineRasterizationStateCreateInfo rasterization{};
        rasterization.polygonMode = vulkan::PolygonMode::Fill;
        rasterization.cullMode =
            static_cast<vulkan::VkFlags>(vulkan::CullMode::None);
        rasterization.frontFace = vulkan::FrontFace::CounterClockwise;
        rasterization.lineWidth = 1.0f;
        vulkan::VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.rasterizationSamples = vulkan::SampleCount::One;
        vulkan::VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = depthTest ? 1 : 0;
        depthStencil.depthWriteEnable = depthTest ? 1 : 0;
        depthStencil.depthCompareOp = vulkan::CompareOp::Less;
        depthStencil.minDepthBounds = 0.0f;
        depthStencil.maxDepthBounds = 1.0f;
        vulkan::VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.blendEnable = 0;
        blendAttachment.colorWriteMask = 0xF;
        vulkan::VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAttachment;
        vulkan::VkPipelineDynamicStateCreateInfo dynamicState{};
        vulkan::VkGraphicsPipelineCreateInfo info{};
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &rasterization;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depthStencil;
        info.pColorBlendState = &colorBlend;
        info.pDynamicState = &dynamicState;
        info.layout = pipelineLayout;
        info.renderPass = renderPass;
        vulkan::VkPipeline pipeline = nullptr;
        if (loader.vkCreateGraphicsPipelines(device, nullptr, 1, &info,
                                             nullptr, &pipeline)
            != vulkan::Result::Success)
            throw std::runtime_error(
                "VulkanRenderBackend: vkCreateGraphicsPipelines failed");
        loader.vkDestroyShaderModule(device, vertexModule, nullptr);
        loader.vkDestroyShaderModule(device, fragmentModule, nullptr);
        return pipeline;
    }

    /// Creates the program resources for a shader program (the uniform
    /// buffer, the descriptor set layout and the descriptor set).
    void createProgramResources(VulkanRenderBackend& backend,
                                vulkan::VkShaderProgram* program)
    {
        // Set 0 carries the uniform block at binding 0 plus one combined
        // image sampler per entry of the program's layout table: the SPIR-V
        // decorates those bindings, and a pipeline layout that omits them
        // makes vkCreateGraphicsPipelines invalid (the lavapipe crash).
        // The layout table's sampler entries are the GLSL declaration
        // (binding 1..7), the same binding numbers the SPIR-V carries.
        vulkan::VkDescriptorSetLayoutBinding uniformBinding{};
        uniformBinding.binding = 0;
        uniformBinding.descriptorType =
            vulkan::DescriptorType::UniformBuffer;
        uniformBinding.descriptorCount = 1;
        uniformBinding.stageFlags =
            vulkan::ShaderStage::Vertex | vulkan::ShaderStage::Fragment;
        std::vector<vulkan::VkDescriptorSetLayoutBinding> bindings;
        bindings.push_back(uniformBinding);
        for (const auto& sampler : program->samplerEntries())
        {
            vulkan::VkDescriptorSetLayoutBinding samplerBinding{};
            samplerBinding.binding = sampler.binding;
            samplerBinding.descriptorType =
                vulkan::DescriptorType::CombinedImageSampler;
            samplerBinding.descriptorCount = sampler.count;
            samplerBinding.stageFlags =
                static_cast<vulkan::ShaderStage>(
                    static_cast<vulkan::VkFlags>(
                        vulkan::ShaderStage::Fragment));
            bindings.push_back(samplerBinding);
        }
        vulkan::VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount =
            static_cast<std::uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        vulkan::VkDescriptorSetLayout setLayout = nullptr;
        if (loader.vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                               &setLayout)
            != vulkan::Result::Success)
            throw std::runtime_error(
                "VulkanRenderBackend: vkCreateDescriptorSetLayout failed");
        // The pool carries the uniform block and the samplers' descriptors
        // (the set is written again at draw time: sampler writes are the
        // same descriptor set the draw binds).
        vulkan::VkDescriptorPoolSize poolSizes[2]{};
        poolSizes[0].type = vulkan::DescriptorType::UniformBuffer;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = vulkan::DescriptorType::CombinedImageSampler;
        // The sampler count is the sum of the entries' descriptor counts
        // (the sampler2D arrays declare 4 or 2 elements, the scalar samplers
        // one). The write is the same: the set is written again at draw
        // time, the same descriptor set the draw binds.
        std::uint32_t samplerCount = 0;
        for (const auto& sampler : program->samplerEntries())
            samplerCount += sampler.count;
        poolSizes[1].descriptorCount = samplerCount;
        vulkan::VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        vulkan::VkDescriptorPool descriptorPool = nullptr;
        if (loader.vkCreateDescriptorPool(device, &poolInfo, nullptr,
                                          &descriptorPool)
            != vulkan::Result::Success)
            throw std::runtime_error(
                "VulkanRenderBackend: vkCreateDescriptorPool failed");
        vulkan::VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.descriptorPool = descriptorPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &setLayout;
        vulkan::VkDescriptorSet set = nullptr;
        if (loader.vkAllocateDescriptorSets(device, &allocateInfo, &set)
            != vulkan::Result::Success)
            throw std::runtime_error(
                "VulkanRenderBackend: vkAllocateDescriptorSets failed");
        vulkan::VkBuffer uniformBuffer = nullptr;
        vulkan::VkDeviceMemory uniformMemory = nullptr;
        const std::size_t uniformBytes = program->uniformBytes().size();
        if (uniformBytes > 0)
            createHostBuffer(
                static_cast<vulkan::VkFlags>(
                    vulkan::BufferUsage::TransferDst
                    | vulkan::BufferUsage::UniformBuffer),
                uniformBytes, uniformBuffer, uniformMemory);
        vulkan::VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = uniformBytes;
        vulkan::VkWriteDescriptorSet write{};
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = vulkan::DescriptorType::UniformBuffer;
        write.pBufferInfo = &bufferInfo;
        loader.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        vulkan::VkPushConstantRange pushConstant{};
        pushConstant.stageFlags =
            static_cast<vulkan::ShaderStage>(
                static_cast<vulkan::VkFlags>(vulkan::ShaderStage::Vertex)
                | static_cast<vulkan::VkFlags>(
                    vulkan::ShaderStage::Fragment));
        pushConstant.offset = 0;
        pushConstant.size = 128;
        vulkan::VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &setLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
        vulkan::VkPipelineLayout pipelineLayout = nullptr;
        if (loader.vkCreatePipelineLayout(device, &pipelineLayoutInfo,
                                          nullptr, &pipelineLayout)
            != vulkan::Result::Success)
            throw std::runtime_error(
                "VulkanRenderBackend: vkCreatePipelineLayout failed");
        ProgramResources& entry = programResources[program];
        entry.uniformBuffer = uniformBuffer;
        entry.uniformMemory = uniformMemory;
        entry.setLayout = setLayout;
        entry.set = set;
        entry.pipelineLayout = pipelineLayout;
        (void)backend;
    }

    /// The program's resources, created on first use (createProgramResources
    /// runs once per program; the map lookup returns the stored handles).
    ProgramResources& ensureProgramResources(VulkanRenderBackend& backend,
                                             vulkan::VkShaderProgram* program)
    {
        auto it = programResources.find(program);
        if (it == programResources.end())
        {
            createProgramResources(backend, program);
            it = programResources.find(program);
        }
        return it->second;
    }

    /// Presents the frame to the swapchain (the backend's swapBuffers). The
    /// acquired image transitions to PresentSrc first (one-off submit).
    /// Tears the partial device state down after a mid-initialise failure
    /// (the objects created so far; the caller releases the surface/window).
    void shutdownPartial()
    {
        if (commandPool != nullptr)
        {
            loader.vkDestroyCommandPool(device, commandPool, nullptr);
            commandPool = nullptr;
        }
        if (swapchain != nullptr)
        {
            loader.vkDestroySwapchainKHR(device, swapchain, nullptr);
            swapchain = nullptr;
        }
        if (device != nullptr)
        {
            loader.vkDestroyDevice(device, nullptr);
            device = nullptr;
        }
    }

    /// Creates the swapchain resources: the surface query, the swapchain
    /// itself, the image views, the depth attachment. The initialisation
    /// runs it once; the acquire path runs it again when the swapchain
    /// goes out of date (a surface resize).
    void createSwapchainResources()
    {
        // Swapchain: the surface's current extent and the first supported
        // format (R8G8B8A8Unorm preferred, the format the readback
        // expects).
        vulkan::VkSurfaceCapabilitiesKHR caps{};
        if (loader.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                physicalDevice, surface, &caps)
            != vulkan::Result::Success)
            throw std::runtime_error(
                "VulkanRenderBackend: "
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
        std::uint32_t format_count = 0;
        loader.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface,
                                                    &format_count, nullptr);
        std::vector<vulkan::VkSurfaceFormatKHR> formats(format_count);
        loader.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface,
                                                    &format_count,
                                                    formats.data());
        // The swapchain format is the readback's contract: RGBA8 (37) is
        // the format the readback expects, B8G8R8A8 (44) reads back with
        // the R and B bytes swapped, and an SRGB format (50) would need
        // the sRGB decode the UNORM formats avoid, so an UNORM format is
        // always preferred.
        swapchainFormat = formats.empty()
            ? vulkan::Format::R8G8B8A8Unorm
            : formats[0].format;
        for (const vulkan::VkSurfaceFormatKHR& surface_format : formats)
        {
            if (surface_format.format == vulkan::Format::B8G8R8A8Unorm)
            {
                swapchainFormat = surface_format.format;
                break;
            }
        }
        for (const vulkan::VkSurfaceFormatKHR& surface_format : formats)
        {
            if (surface_format.format == vulkan::Format::R8G8B8A8Unorm)
            {
                swapchainFormat = surface_format.format;
                break;
            }
        }
        swapchainExtent = caps.currentExtent;
        vulkan::VkSwapchainCreateInfoKHR swapInfo{};
        swapInfo.surface = surface;
        swapInfo.minImageCount = caps.minImageCount + 1;
        if (caps.maxImageCount != 0
            && swapInfo.minImageCount > caps.maxImageCount)
            swapInfo.minImageCount = caps.maxImageCount;
        swapInfo.imageFormat = swapchainFormat;
        swapInfo.imageColorSpace = vulkan::ColorSpace::SrgbNonlinear;
        swapInfo.imageExtent = swapchainExtent;
        swapInfo.imageUsage = static_cast<vulkan::VkFlags>(
            vulkan::ImageUsage::ColorAttachment
            | vulkan::ImageUsage::TransferSrc);
        swapInfo.preTransform = vulkan::SurfaceTransform::Identity;
        swapInfo.compositeAlpha = vulkan::CompositeAlpha::Opaque;
        // The swap interval maps onto the present mode: 0 is the
        // immediate mode, which a device may not carry — the query
        // decides, FIFO stays the fallback.
        swapInfo.presentMode = vulkan::PresentMode::Fifo;
        if (config.swap_interval == 0)
        {
            std::uint32_t mode_count = 0;
            loader.vkGetPhysicalDeviceSurfacePresentModesKHR(
                physicalDevice, surface, &mode_count, nullptr);
            std::vector<vulkan::PresentMode> modes(mode_count);
            loader.vkGetPhysicalDeviceSurfacePresentModesKHR(
                physicalDevice, surface, &mode_count, modes.data());
            for (vulkan::PresentMode mode : modes)
            {
                if (mode == vulkan::PresentMode::Immediate)
                {
                    swapInfo.presentMode = vulkan::PresentMode::Immediate;
                    break;
                }
            }
        }
        swapInfo.clipped = 1;
        if (loader.vkCreateSwapchainKHR(device, &swapInfo, nullptr,
                                        &swapchain)
            != vulkan::Result::Success)
            throw std::runtime_error(
                "VulkanRenderBackend: vkCreateSwapchainKHR failed");
        std::uint32_t image_count = 0;
        loader.vkGetSwapchainImagesKHR(device, swapchain, &image_count,
                                       nullptr);
        swapchainImages.resize(image_count);
        loader.vkGetSwapchainImagesKHR(device, swapchain, &image_count,
                                       swapchainImages.data());
        swapchainViews.reserve(image_count);
        for (vulkan::VkImage image : swapchainImages)
        {
            swapchainViews.push_back(createImageView(
                image, swapchainFormat, vulkan::ImageAspect::Color));
        }

        // The default framebuffer's depth attachment (the device's depth
        // format) and the shared swapchain render pass + framebuffers.
        swapchainDepthImage = createImage(
            depthFormat, swapchainExtent.width, swapchainExtent.height,
            static_cast<vulkan::VkFlags>(
                vulkan::ImageUsage::DepthStencilAttachment
                | vulkan::ImageUsage::TransferSrc),
            static_cast<vulkan::VkFlags>(vulkan::SampleCount::One));
        vulkan::VkMemoryRequirements depthRequirements{};
        loader.vkGetImageMemoryRequirements(device, swapchainDepthImage,
                                            &depthRequirements);
        swapchainDepthMemory = allocateMemoryFor(
            depthRequirements.memoryTypeBits,
            static_cast<std::size_t>(depthRequirements.size),
            vulkan::MemoryProperty::DeviceLocal);
        loader.vkBindImageMemory(device, swapchainDepthImage,
                                 swapchainDepthMemory, 0);
        swapchainDepthView = createImageView(
            swapchainDepthImage, depthFormat,
            static_cast<vulkan::ImageAspect>(
                static_cast<std::uint32_t>(vulkan::ImageAspect::Depth)
                | static_cast<std::uint32_t>(
                    vulkan::ImageAspect::Stencil)));
        // A fresh depth buffer reads 1.0f (the OpenGL clear default): the
        // attachment starts as undefined content, so a clear-once pass
        // gives the first clear's loadOp=Load a defined 1.0f to test
        // against.
        {
            vulkan::VkCommandBuffer oneOff = beginOneOff();
            recordLayoutTransition(
                oneOff, swapchainDepthImage,
                static_cast<vulkan::ImageAspect>(
                    static_cast<std::uint32_t>(vulkan::ImageAspect::Depth)
                    | static_cast<std::uint32_t>(
                        vulkan::ImageAspect::Stencil)),
                vulkan::ImageLayout::Undefined,
                vulkan::ImageLayout::TransferDstOptimal);
            vulkan::VkClearDepthStencilValue clearDepth{};
            clearDepth.depth = 1.0f;
            clearDepth.stencil = 0;
            vulkan::VkImageSubresourceRange range{};
            range.aspectMask = static_cast<vulkan::ImageAspect>(
                static_cast<std::uint32_t>(vulkan::ImageAspect::Depth)
                | static_cast<std::uint32_t>(
                    vulkan::ImageAspect::Stencil));
            range.levelCount = 1;
            range.layerCount = 1;
            loader.vkCmdClearDepthStencilImage(
                oneOff, swapchainDepthImage,
                vulkan::ImageLayout::TransferDstOptimal, &clearDepth, 1,
                &range);
            recordLayoutTransition(
                oneOff, swapchainDepthImage,
                static_cast<vulkan::ImageAspect>(
                    static_cast<std::uint32_t>(vulkan::ImageAspect::Depth)
                    | static_cast<std::uint32_t>(
                        vulkan::ImageAspect::Stencil)),
                vulkan::ImageLayout::TransferDstOptimal,
                vulkan::ImageLayout::DepthStencilAttachmentOptimal);
            loader.vkEndCommandBuffer(oneOff);
            submitOneOff(oneOff);
        }
    }

    /// Recreates the swapchain resources after an out-of-date acquire:
    /// the stale swapchain and its resources are destroyed first, then
    /// the same creation path runs against the surface's current extent.
    /// The depth image's device memory is unregistered before the free,
    /// so the shutdown's memory loop does not free it a second time.
    void recreateSwapchainResources()
    {
        loader.vkDeviceWaitIdle(device);
        for (vulkan::VkFramebuffer framebuffer : swapchainFramebuffers)
            loader.vkDestroyFramebuffer(device, framebuffer, nullptr);
        swapchainFramebuffers.clear();
        if (swapchainRenderPass != nullptr)
            loader.vkDestroyRenderPass(device, swapchainRenderPass,
                                       nullptr);
        swapchainRenderPass = nullptr;
        swapchainClearMask = 0;
        for (vulkan::VkImageView view : swapchainViews)
            loader.vkDestroyImageView(device, view, nullptr);
        swapchainViews.clear();
        if (swapchainDepthView != nullptr)
            loader.vkDestroyImageView(device, swapchainDepthView, nullptr);
        swapchainDepthView = nullptr;
        if (swapchainDepthImage != nullptr)
            loader.vkDestroyImage(device, swapchainDepthImage, nullptr);
        swapchainDepthImage = nullptr;
        if (swapchainDepthMemory != nullptr)
        {
            for (auto block = memory_blocks.begin();
                 block != memory_blocks.end(); ++block)
            {
                if (*block == swapchainDepthMemory)
                {
                    memory_blocks.erase(block);
                    break;
                }
            }
            loader.vkFreeMemory(device, swapchainDepthMemory, nullptr);
            swapchainDepthMemory = nullptr;
        }
        if (swapchain != nullptr)
            loader.vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = nullptr;
        createSwapchainResources();
    }

    void present()
    {
        // An off-screen frame never acquired an image: nothing to present.
        if (!swapchain_frame)
            return;
        transitionImageLayout(swapchainImages[nextImageIndex],
                              swapchainFormat, vulkan::ImageAspect::Color,
                              vulkan::ImageLayout::ColorAttachmentOptimal,
                              vulkan::ImageLayout::PresentSrc);
        vulkan::VkPresentInfoKHR present{};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &renderDone;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &nextImageIndex;
        // A present that reports the swapchain as out of date (or only
        // suboptimal) marks it stale: the next frame's acquire recreates
        // it (the surface resize path). The frame itself was already
        // submitted; the present result only steers the next frame.
        const vulkan::Result presented = loader.vkQueuePresentKHR(queue,
                                                                 &present);
        swapchainStale = presented == vulkan::Result::ErrorOutOfDateKHR
            || presented == vulkan::Result::SuboptimalKHR;
    }

    /// Copies one pixel of the bound target's colour (or the swapchain's)
    /// into the readback staging buffer and maps it. The depth/stencil pixel
    /// comes from the target's depth attachment (or the swapchain's).
    void readPixelInto(const std::uint32_t x, const std::uint32_t y)
    {
        vulkan::VkImage colour_image = nullptr;
        vulkan::Format colour_format = swapchainFormat;
        vulkan::VkImage depth_image = nullptr;
        vulkan::VkExtent2D extent = swapchainExtent;
        if (bound_target != nullptr)
        {
            TargetResources& res = targetResources[bound_target];
            colour_image = res.images[0];
            colour_format = toVkFormat(bound_target->colorView(0)->format());
            extent = {bound_target->width(), bound_target->height()};
            if (bound_target->hasDepthAttachment())
                depth_image = res.images.back();
        }
        else if (!swapchainImages.empty())
        {
            colour_image = swapchainImages[nextImageIndex];
            depth_image = swapchainDepthImage;
        }
        vulkan::VkCommandBuffer command_buffer = beginOneOff();
        vulkan::VkBufferImageCopy regions[2]{};
        regions[0].imageSubresource.aspectMask = vulkan::ImageAspect::Color;
        regions[0].imageOffset = {static_cast<std::int32_t>(x),
                                  static_cast<std::int32_t>(y), 0};
        regions[0].imageExtent = {1, 1, 1};
        regions[0].bufferOffset = 0;
        if (depth_image != nullptr)
        {
            regions[1].imageSubresource.aspectMask =
                static_cast<vulkan::ImageAspect>(
                    static_cast<std::uint32_t>(vulkan::ImageAspect::Depth)
                    | static_cast<std::uint32_t>(
                        vulkan::ImageAspect::Stencil));
            regions[1].imageOffset = {static_cast<std::int32_t>(x),
                                      static_cast<std::int32_t>(y), 0};
            regions[1].imageExtent = {1, 1, 1};
            regions[1].bufferOffset = 4 * sizeof(float);
        }
        transitionImageLayout(colour_image, colour_format,
                              vulkan::ImageAspect::Color,
                              vulkan::ImageLayout::ColorAttachmentOptimal,
                              vulkan::ImageLayout::TransferSrcOptimal);
        loader.vkCmdCopyImageToBuffer(command_buffer, colour_image,
                                      vulkan::ImageLayout::TransferSrcOptimal,
                                      readbackBuffer, 1, &regions[0]);
        transitionImageLayout(colour_image, colour_format,
                              vulkan::ImageAspect::Color,
                              vulkan::ImageLayout::TransferSrcOptimal,
                              vulkan::ImageLayout::ColorAttachmentOptimal);
        if (depth_image != nullptr)
        {
            transitionImageLayout(
                depth_image, depthFormat,
                regions[1].imageSubresource.aspectMask,
                vulkan::ImageLayout::DepthStencilAttachmentOptimal,
                vulkan::ImageLayout::TransferSrcOptimal);
            loader.vkCmdCopyImageToBuffer(
                command_buffer, depth_image,
                vulkan::ImageLayout::TransferSrcOptimal, readbackBuffer, 1,
                &regions[1]);
            transitionImageLayout(
                depth_image, depthFormat,
                regions[1].imageSubresource.aspectMask,
                vulkan::ImageLayout::TransferSrcOptimal,
                vulkan::ImageLayout::DepthStencilAttachmentOptimal);
        }
        loader.vkEndCommandBuffer(command_buffer);
        vulkan::VkSubmitInfo submit{};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command_buffer;
        loader.vkQueueSubmit(queue, 1, &submit, nullptr);
        loader.vkQueueWaitIdle(queue);
        void* mapped = nullptr;
        loader.vkMapMemory(device, readbackMemory, 0, 5 * sizeof(float), 0,
                           &mapped);
        readbackMapped = mapped;
        // The colour bytes follow the swapchain's actual format. RGBA8
        // (format 37) stores R in the first byte; B8G8R8A8 (44) stores B
        // there, and the SRGB format (50) stores B there too, with the
        // bytes sRGB-encoded. The depth float lands at 16 bytes and the
        // stencil at 20 bytes.
        const std::uint32_t* pixels = static_cast<const std::uint32_t*>(mapped);
        const std::uint32_t word = pixels[0];
        const std::uint8_t byte0 = static_cast<std::uint8_t>(word & 0xFFu);
        const std::uint8_t byte1 =
            static_cast<std::uint8_t>((word >> 8) & 0xFFu);
        const std::uint8_t byte2 =
            static_cast<std::uint8_t>((word >> 16) & 0xFFu);
        const std::uint8_t byte3 =
            static_cast<std::uint8_t>((word >> 24) & 0xFFu);
        // An SRGB attachment encodes the linear clear values the sRGB
        // transfer function stores. The decode is the exact inverse of the
        // /255.0f the clear value applies.
        const bool srgb = colour_format == vulkan::Format::B8G8R8A8Srgb;
        const auto decode = [srgb](std::uint8_t byte) -> std::uint8_t {
            if (!srgb)
                return byte;
            const float normalized = static_cast<float>(byte) / 255.0f;
            const float linear = normalized > 0.04045f
                ? std::pow((normalized + 0.055f) / 1.055f, 2.4f)
                : normalized / 12.92f;
            return static_cast<std::uint8_t>(
                std::lround(linear * 255.0f));
        };
        lastReadbackColor =
            colour_format == vulkan::Format::R8G8B8A8Unorm
                ? ClearColor{decode(byte0), decode(byte1), decode(byte2),
                             decode(byte3)}
                : ClearColor{decode(byte2), decode(byte1), decode(byte0),
                             decode(byte3)};
        // The depth/stencil pixel copies as the D24S8 layout: the 24
        // depth bits in the word's low end, the 8 stencil bits at the top
        // (a D32S8 copy would carry a float; this one needs the unpack).
        const std::uint32_t depth_word = pixels[4];
        lastReadbackDepth = static_cast<float>(depth_word & 0xFFFFFFu)
            / 16777215.0f;
        lastReadbackStencil =
            static_cast<std::int32_t>((depth_word >> 24) & 0xFFu);
    }

    /// Creates the device objects of one render target: the colour and
    /// depth images, the views, the render pass and the framebuffer.
    void createTargetResources(VulkanRenderBackend& backend,
                               vulkan::VkRenderTarget* target)
    {
        (void)backend;  // the volume registry lives on the Impl itself
        TargetResources& res = targetResources[target];
        const std::uint32_t width = target->width();
        const std::uint32_t height = target->height();
        if (target->borrowsVolume())
        {
            // The layer target borrows the volume's image: the view is a
            // 2D view of one layer of the volume's image.
            auto* volume =
                findVkVolume(volumes, target->volumeView());
            res.images.push_back(volume->image());
            res.views.push_back(createVolumeLayerView(
                volume->image(), toVkFormat(volume->format()),
                target->borrowedLayer()));
            res.renderPass =
                createRenderPass(toVkFormat(target->colorFormat(0)),
                                 vulkan::Format::R8G8B8A8Unorm,
                                 static_cast<std::uint32_t>(
                                     ClearBuffer::Color),
                                 false, true,
                                 vulkan::ImageLayout::General,
                                 vulkan::ImageLayout::General);
            res.framebuffer = createFramebuffer(
                res.renderPass, width, height, res.views.data(), 1);
            return;
        }
        for (std::uint32_t i = 0; i < target->colorAttachmentCount(); ++i)
        {
            vulkan::Format format = toVkFormat(target->colorFormat(i));
            vulkan::VkImage image = createImage(
                format, width, height,
                static_cast<vulkan::VkFlags>(
                    vulkan::ImageUsage::ColorAttachment
                    | vulkan::ImageUsage::TransferSrc),
                static_cast<vulkan::VkFlags>(
                    vulkan::SampleCount::One));
            vulkan::VkMemoryRequirements colourRequirements{};
            loader.vkGetImageMemoryRequirements(device, image,
                                                &colourRequirements);
            vulkan::VkDeviceMemory memory = allocateMemoryFor(
                colourRequirements.memoryTypeBits,
                static_cast<std::size_t>(colourRequirements.size),
                vulkan::MemoryProperty::DeviceLocal);
            loader.vkBindImageMemory(device, image, memory, 0);
            res.images.push_back(image);
            res.memories.push_back(memory);
            res.views.push_back(createImageView(
                image, format, vulkan::ImageAspect::Color));
            // The handle's contract-facing view reports the attachment's
            // format and non-depth kind (the checks read them back).
            target->setColorView(
                i, res.views.back(), width, height,
                target->colorFormat(i), false);
        }
        if (target->hasDepthAttachment())
        {
            vulkan::VkImage image = createImage(
                depthFormat, width, height,
                static_cast<vulkan::VkFlags>(
                    vulkan::ImageUsage::DepthStencilAttachment
                    | vulkan::ImageUsage::TransferSrc),
                static_cast<vulkan::VkFlags>(
                    vulkan::SampleCount::One));
            vulkan::VkMemoryRequirements depthRequirements{};
            loader.vkGetImageMemoryRequirements(device, image,
                                                &depthRequirements);
            vulkan::VkDeviceMemory memory = allocateMemoryFor(
                depthRequirements.memoryTypeBits,
                static_cast<std::size_t>(depthRequirements.size),
                vulkan::MemoryProperty::DeviceLocal);
            loader.vkBindImageMemory(device, image, memory, 0);
            res.images.push_back(image);
            res.memories.push_back(memory);
            res.views.push_back(createImageView(
                image, depthFormat,
                vulkan::ImageAspect::Depth));
            // The depth view reports the depth format the descriptor asks
            // for and the depth kind (the contract's isDepth()).
            target->setDepthView(
                res.views.back(), width, height,
                target->desc().depthFormat, true);
        }
        // The attachment set follows the target's descriptor: a depth-only
        // target (the shadow pass) carries the depth attachment alone, a
        // colour target carries colour (+depth when it has one).
        if (target->hasColorAttachment())
        {
            res.renderPass = createRenderPass(
                toVkFormat(target->colorFormat(0)),
                target->hasDepthAttachment()
                    ? depthFormat
                    : vulkan::Format::R8G8B8A8Unorm,
                static_cast<std::uint32_t>(ClearBuffer::Color
                                           | ClearBuffer::Depth
                                           | ClearBuffer::Stencil),
                target->hasDepthAttachment());
        }
        else
        {
            res.renderPass = createRenderPass(
                vulkan::Format::R8G8B8A8Unorm,
                depthFormat,
                static_cast<std::uint32_t>(ClearBuffer::Depth
                                           | ClearBuffer::Stencil),
                true, false);
        }
        res.framebuffer = createFramebuffer(
            res.renderPass, width, height, res.views.data(),
            static_cast<std::uint32_t>(res.views.size()));
    }

    /// Releases the device objects of one render target (the images and
    /// the borrowed views stay with the volume).
    void destroyTargetResources(VulkanRenderBackend& backend,
                                vulkan::VkRenderTarget* target)
    {
        auto it = targetResources.find(target);
        if (it == targetResources.end())
            return;
        TargetResources& res = it->second;
        // Frees a device memory and removes it from the shutdown registry, so
        // the memory_blocks loop does not free it a second time.
        auto freeRegisteredMemory =
            [this](vulkan::VkDeviceMemory memory)
        {
            for (auto block = memory_blocks.begin();
                 block != memory_blocks.end(); ++block)
            {
                if (*block == memory)
                {
                    memory_blocks.erase(block);
                    break;
                }
            }
            loader.vkFreeMemory(device, memory, nullptr);
        };
        if (target->borrowsVolume())
        {
            loader.vkDestroyFramebuffer(device, res.framebuffer, nullptr);
            loader.vkDestroyRenderPass(device, res.renderPass, nullptr);
            res.views.clear();
        }
        else
        {
            loader.vkDestroyFramebuffer(device, res.framebuffer, nullptr);
            loader.vkDestroyRenderPass(device, res.renderPass, nullptr);
            for (vulkan::VkImageView view : res.views)
                loader.vkDestroyImageView(device, view, nullptr);
            for (vulkan::VkImage image : res.images)
                loader.vkDestroyImage(device, image, nullptr);
            for (vulkan::VkDeviceMemory memory : res.memories)
                freeRegisteredMemory(memory);
        }
        targetResources.erase(it);
        (void)backend;
    }
    /// Creates the device objects of one 3D texture: the VkImage and the
    /// view (the volume's own view reports the volume's layer count).
    void createVolumeResources(VulkanRenderBackend& backend,
                               vulkan::VkTexture3DView* volume)
    {
        VolumeResources& res = volumeResources[volume];
        const Texture3DDesc& desc = volume->desc();
        res.image = createVolumeImage(
            toVkFormat(desc.format), desc.width, desc.height, desc.depth,
            static_cast<vulkan::VkFlags>(vulkan::ImageUsage::TransferDst
                                         | vulkan::ImageUsage::TransferSrc));
        vulkan::VkMemoryRequirements requirements{};
        loader.vkGetImageMemoryRequirements(device, res.image, &requirements);
        res.memory = allocateMemoryFor(
            requirements.memoryTypeBits,
            static_cast<std::size_t>(requirements.size),
            vulkan::MemoryProperty::DeviceLocal);
        loader.vkBindImageMemory(device, res.image, res.memory, 0);
        volume->setImage(res.image);
        // The volume's sampler view (the drawMesh descriptor write binds
        // it): a 3D view of the whole image, the aspect the sampler3D
        // declaration reads.
        vulkan::VkImageViewCreateInfo viewInfo{};
        viewInfo.image = res.image;
        viewInfo.viewType = vulkan::ImageType::Type3D;
        viewInfo.format = toVkFormat(desc.format);
        viewInfo.components = vulkan::VkComponentMapping{};
        viewInfo.subresourceRange.aspectMask = vulkan::ImageAspect::Color;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (loader.vkCreateImageView(device, &viewInfo, nullptr, &res.view)
            != vulkan::Result::Success)
            throw std::runtime_error(
                "VulkanRenderBackend: vkCreateImageView failed");
        transitionImageLayout(res.image, toVkFormat(desc.format),
                              vulkan::ImageAspect::Color,
                              vulkan::ImageLayout::Undefined,
                              vulkan::ImageLayout::General);
        (void)backend;
    }

    /// Releases the device objects of one 3D texture.
    void destroyVolumeResources(VulkanRenderBackend& backend,
                                vulkan::VkTexture3DView* volume)
    {
        auto it = volumeResources.find(volume);
        if (it == volumeResources.end())
            return;
        if (it->second.view != nullptr)
            loader.vkDestroyImageView(device, it->second.view, nullptr);
        loader.vkDestroyImage(device, it->second.image, nullptr);
        for (auto block = memory_blocks.begin();
             block != memory_blocks.end(); ++block)
        {
            if (*block == it->second.memory)
            {
                memory_blocks.erase(block);
                break;
            }
        }
        loader.vkFreeMemory(device, it->second.memory, nullptr);
        volumeResources.erase(it);
        (void)backend;
    }

    /// Copies one depth attachment onto another (vkCmdCopyImage of the
    /// depth aspects; the two targets must be the same size and the same
    /// format, the same rejection the OpenGL backend makes).
    void copyDepthAttachment(VulkanRenderBackend& backend,
                             vulkan::VkRenderTarget* source,
                             vulkan::VkRenderTarget* destination)
    {
        TargetResources& src = targetResources[source];
        TargetResources& dst = targetResources[destination];
        const std::uint32_t depthIndex = source->colorAttachmentCount();
        vulkan::VkImage srcImage = src.images[depthIndex];
        vulkan::VkImage dstImage = dst.images[destination->colorAttachmentCount()];
        vulkan::VkCommandBuffer oneOff = beginOneOff();
        vulkan::VkImageMemoryBarrier srcBarrier{};
        srcBarrier.oldLayout = vulkan::ImageLayout::Undefined;
        srcBarrier.newLayout = vulkan::ImageLayout::TransferSrcOptimal;
        srcBarrier.image = srcImage;
        srcBarrier.subresourceRange.aspectMask =
            static_cast<vulkan::ImageAspect>(
                static_cast<std::uint32_t>(vulkan::ImageAspect::Depth)
                | static_cast<std::uint32_t>(vulkan::ImageAspect::Stencil));
        srcBarrier.subresourceRange.layerCount = 1;
        vulkan::VkImageMemoryBarrier dstBarrier{};
        dstBarrier.oldLayout = vulkan::ImageLayout::Undefined;
        dstBarrier.newLayout = vulkan::ImageLayout::TransferDstOptimal;
        dstBarrier.image = dstImage;
        dstBarrier.subresourceRange.aspectMask =
            static_cast<vulkan::ImageAspect>(
                static_cast<std::uint32_t>(vulkan::ImageAspect::Depth)
                | static_cast<std::uint32_t>(vulkan::ImageAspect::Stencil));
        dstBarrier.subresourceRange.layerCount = 1;
        vulkan::VkImageMemoryBarrier barriers[2] = {srcBarrier, dstBarrier};
        loader.vkCmdPipelineBarrier(
            oneOff, vulkan::PipelineStage::TopOfPipe,
            vulkan::PipelineStage::Transfer, 0, 0, nullptr, 0, nullptr, 2,
            barriers);
        // One packed image-copy region (the project loader declares the
        // region pointer plainly, the layout below is the documented one).
        struct ImageCopyRegion
        {
            vulkan::VkImageSubresourceLayers srcSubresource{};
            vulkan::VkOffset3D srcOffset{};
            vulkan::VkImageSubresourceLayers dstSubresource{};
            vulkan::VkOffset3D dstOffset{};
            vulkan::VkExtent3D extent{};
        };
        vulkan::VkImageSubresourceLayers layers{};
        layers.aspectMask =
            static_cast<vulkan::ImageAspect>(
                static_cast<std::uint32_t>(vulkan::ImageAspect::Depth)
                | static_cast<std::uint32_t>(vulkan::ImageAspect::Stencil));
        layers.layerCount = 1;
        ImageCopyRegion region{};
        region.srcSubresource = layers;
        region.dstSubresource = layers;
        region.extent = {source->width(), source->height(), 1};
        loader.vkCmdCopyImage(
            oneOff, srcImage, vulkan::ImageLayout::TransferSrcOptimal,
            dstImage, vulkan::ImageLayout::TransferDstOptimal, 1, &region);
        loader.vkEndCommandBuffer(oneOff);
        vulkan::VkSubmitInfo submit{};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &oneOff;
        loader.vkQueueSubmit(queue, 1, &submit, nullptr);
        loader.vkQueueWaitIdle(queue);
        (void)backend;
    }

    /// Records the draw of one mesh through one program inside the frame's
    /// command buffer (the pipeline, the descriptor set and the draw).
    void drawMesh(VulkanRenderBackend& backend,
                  vulkan::VkMesh* mesh,
                  vulkan::VkShaderProgram* program)
    {
        if (commandBuffer == nullptr)
            return;
        ProgramResources& resources = ensureProgramResources(backend, program);
        // Upload the program's staged uniform bytes to the uniform buffer.
        if (program->uniformStaged())
        {
            const std::vector<std::byte>& bytes = program->uniformBytes();
            void* mapped = nullptr;
            loader.vkMapMemory(device, resources.uniformMemory, 0,
                               bytes.size(), 0, &mapped);
            std::memcpy(mapped, bytes.data(), bytes.size());
            loader.vkUnmapMemory(device, resources.uniformMemory);
            program->clearStaging();
        }
        vulkan::VkPipeline pipeline =
            ensurePipeline(backend, program, resources.pipelineLayout);
        loader.vkCmdBindPipeline(commandBuffer,
                                 vulkan::PipelineBindPoint::Graphics,
                                 pipeline);
        // The sampler descriptors are written at draw time from the
        // bound texture views: the layout table's sampler entry maps the
        // engine's GL-style texture units onto (binding, array element),
        // element e of an array landing on unitBase + unitStride * e. An
        // unbound unit keeps the default view (the contract's tolerance:
        // the shader samples a valid, opaque image either way).
        std::vector<vulkan::VkDescriptorImageInfo> samplerInfos;
        std::vector<vulkan::VkWriteDescriptorSet> samplerWrites;
        std::vector<std::uint32_t> samplerInfoOffsets;
        samplerInfos.reserve(program->samplerEntries().size());
        for (const auto& sampler : program->samplerEntries())
        {
            samplerInfoOffsets.push_back(
                static_cast<std::uint32_t>(samplerInfos.size()));
            for (std::uint32_t element = 0; element < sampler.count;
                 ++element)
            {
                const std::uint32_t unit =
                    sampler.unitBase + sampler.unitStride * element;
                const TextureView* view =
                    unit < boundTextures.size()
                        ? boundTextures[unit]
                        : nullptr;
                vulkan::VkImageView imageView = nullptr;
                if (view != nullptr)
                {
                    if (const auto* vkView =
                            dynamic_cast<const vulkan::VkTextureView*>(view))
                        imageView = vkView->imageView();
                    else if (const auto* volume =
                                 dynamic_cast<const vulkan::VkTexture3DView*>(
                                     view))
                    {
                        const auto it = volumeResources.find(volume);
                        if (it != volumeResources.end())
                            imageView = it->second.view;
                    }
                }
                if (imageView == nullptr)
                    imageView = defaultImageView;
                vulkan::VkDescriptorImageInfo info{};
                info.sampler = sharedSampler;
                info.imageView = imageView;
                info.imageLayout =
                    vulkan::ImageLayout::ShaderReadOnlyOptimal;
                samplerInfos.push_back(info);
            }
        }
        for (const auto& sampler : program->samplerEntries())
        {
            vulkan::VkWriteDescriptorSet write{};
            write.dstSet = resources.set;
            write.dstBinding = sampler.binding;
            write.dstArrayElement = 0;
            write.descriptorCount = sampler.count;
            write.descriptorType =
                vulkan::DescriptorType::CombinedImageSampler;
            samplerWrites.push_back(write);
        }
        for (std::size_t i = 0; i < samplerWrites.size(); ++i)
            samplerWrites[i].pImageInfo = &samplerInfos[samplerInfoOffsets[i]];
        if (!samplerWrites.empty())
            loader.vkUpdateDescriptorSets(
                device, static_cast<std::uint32_t>(samplerWrites.size()),
                samplerWrites.data(), 0, nullptr);
        loader.vkCmdBindDescriptorSets(
            commandBuffer, vulkan::PipelineBindPoint::Graphics,
            resources.pipelineLayout, 0, 1, &resources.set, 0, nullptr);
        std::size_t offset = 0;
        vulkan::VkBuffer vertexBuffer = mesh->vertexBuffer();
        loader.vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer,
                                      &offset);
        loader.vkCmdBindIndexBuffer(commandBuffer, mesh->indexBuffer(), 0,
                                    vulkan::IndexType::Uint32);
        loader.vkCmdDrawIndexed(commandBuffer, mesh->indexCount(), 1, 0, 0, 0);
    }

    /// Records the dynamic viewport/scissor of the pass target.
    void applyDynamicViewport()
    {
        vulkan::VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.maxDepth = 1.0f;
        if (bound_pass != nullptr)
        {
            viewport.width = static_cast<float>(bound_pass->width());
            viewport.height = static_cast<float>(bound_pass->height());
        }
        else
        {
            viewport.width = static_cast<float>(config.width);
            viewport.height = static_cast<float>(config.height);
        }
        vulkan::VkRect2D scissor{};
        scissor.extent = {static_cast<std::uint32_t>(viewport.width),
                          static_cast<std::uint32_t>(viewport.height)};
        loader.vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        loader.vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    /// Acquires the frame's command buffer and opens the render pass on the
    /// pass target (the swapchain image when target is null). The clear
    /// values come from the device state (setClearColor/setClearBuffers).
    /// Creates the swapchain render pass and framebuffers on demand, when
    /// the clear mask changes: the render pass freezes the loadOps, so a
    /// new mask needs a new pass (the same as the OpenGL clear()).
    void ensureSwapchainPass()
    {
        if (swapchainRenderPass != nullptr
            && swapchainClearMask == clear_mask)
            return;
        if (swapchainRenderPass != nullptr)
        {
            for (vulkan::VkFramebuffer framebuffer
                     : swapchainFramebuffers)
                loader.vkDestroyFramebuffer(device, framebuffer, nullptr);
            swapchainFramebuffers.clear();
            loader.vkDestroyRenderPass(device, swapchainRenderPass,
                                       nullptr);
            swapchainRenderPass = nullptr;
        }
        swapchainClearMask = clear_mask;
        swapchainRenderPass = createRenderPass(
            swapchainFormat, depthFormat, clear_mask);
        for (vulkan::VkImageView view : swapchainViews)
        {
            const vulkan::VkImageView attachments[2] = {
                view, swapchainDepthView};
            swapchainFramebuffers.push_back(createFramebuffer(
                swapchainRenderPass, swapchainExtent.width,
                swapchainExtent.height, attachments, 2));
        }
    }
    void beginFrame(VulkanRenderBackend& backend,
                    vulkan::VkRenderTarget* target)
    {
        (void)backend;
        if (pass_open)
        {
            // A pass on another target simply replaces the open one.
            loader.vkCmdEndRenderPass(commandBuffer);
            pass_open = false;
            bound_pass = nullptr;
        }
        if (!frame_open)
        {
            // Only a pass on the default framebuffer touches the swapchain:
            // an off-screen target's frame records into the command buffer
            // alone. Acquiring for a pass that never presents would starve
            // the swapchain's image pool and block the next acquire forever.
            if (target == nullptr)
            {
                vulkan::Result acquired = vulkan::Result::ErrorOutOfDateKHR;
                for (;;)
                {
                    acquired = loader.vkAcquireNextImageKHR(
                        device, swapchain, ~0ull, imageAcquired, nullptr,
                        &nextImageIndex);
                    if (acquired == vulkan::Result::Success
                        || acquired == vulkan::Result::SuboptimalKHR)
                        break;
                    if (acquired != vulkan::Result::ErrorOutOfDateKHR)
                        throw std::runtime_error(
                            "VulkanRenderBackend: "
                            "vkAcquireNextImageKHR failed");
                    // The swapchain is out of date (a surface resize, a
                    // minimise): the recreate path runs before the acquire
                    // can succeed. The render pass and framebuffers are
                    // rebuilt against the new extent too.
                    recreateSwapchainResources();
                }
                swapchainStale = acquired == vulkan::Result::SuboptimalKHR;
            }
            vulkan::VkCommandBufferBeginInfo begin{};
            if (loader.vkResetCommandBuffer(commandBuffer, 0)
                != vulkan::Result::Success)
                throw std::runtime_error(
                    "VulkanRenderBackend: vkResetCommandBuffer failed");
            if (loader.vkBeginCommandBuffer(commandBuffer, &begin)
                != vulkan::Result::Success)
                throw std::runtime_error(
                    "VulkanRenderBackend: vkBeginCommandBuffer failed");
            swapchain_frame = target == nullptr;
            frame_open = true;
        }
        bound_pass = target;
        // Open the render pass with the device state's clear values.
        vulkan::VkRenderPass renderPass = nullptr;
        vulkan::VkFramebuffer framebuffer = nullptr;
        if (target != nullptr)
        {
            TargetResources& res = targetResources[target];
            renderPass = res.renderPass;
            framebuffer = res.framebuffer;
        }
        else
        {
            ensureSwapchainPass();
            renderPass = swapchainRenderPass;
            framebuffer = swapchainFramebuffers[nextImageIndex];
        }
        vulkan::VkClearValue clearValues[2] = {};
        // The attachment order: the depth-only pass clears its depth
        // attachment at index 0; the other passes clear colour then depth.
        const bool depth_only =
            target != nullptr && !target->hasColorAttachment();
        if (depth_only)
        {
            clearValues[0].depthStencil.depth = clear_depth;
            clearValues[0].depthStencil.stencil =
                static_cast<std::uint32_t>(clear_stencil);
        }
        else
        {
            clearValues[0] = clearValueFor(clear_color, clear_depth,
                                           clear_stencil);
            clearValues[1].depthStencil.depth = clear_depth;
            clearValues[1].depthStencil.stencil =
                static_cast<std::uint32_t>(clear_stencil);
        }
        vulkan::VkRect2D area{};
        area.extent = target != nullptr
            ? vulkan::VkExtent2D{target->width(), target->height()}
            : swapchainExtent;
        vulkan::VkRenderPassBeginInfo passBegin{};
        passBegin.renderPass = renderPass;
        passBegin.framebuffer = framebuffer;
        passBegin.renderArea = area;
        // The clear values follow the pass's attachments: a depth-only pass
        // has no colour attachment to clear (the second value is unused).
        passBegin.clearValueCount = depth_only ? 1u : 2u;
        passBegin.pClearValues = clearValues;
        loader.vkCmdBeginRenderPass(commandBuffer, &passBegin, 0);
        pass_open = true;
        applyDynamicViewport();
    }

    /// Closes the render pass (the recorded draws are kept until the
    /// frame's end submits the command buffer).
    void endFrame(VulkanRenderBackend& backend)
    {
        if (commandBuffer == nullptr || !frame_open)
            return;
        if (pass_open)
        {
            loader.vkCmdEndRenderPass(commandBuffer);
            pass_open = false;
        }
        bound_pass = nullptr;
        // An off-screen frame is not presented: its command buffer runs at
        // the pass end (the GL's unbind is immediate the same way). A
        // swapchain frame keeps recording until swapBuffers presents it.
        if (!swapchain_frame)
            submitFrame();
        (void)backend;
    }

    /// Submits and presents the frame (swapBuffers: the readback happened
    /// pre-present; the command buffer runs, then the image is presented).
    void submitFrame()
    {
        if (commandBuffer == nullptr || !frame_open)
            return;
        if (pass_open)
        {
            loader.vkCmdEndRenderPass(commandBuffer);
            pass_open = false;
        }
        frame_open = false;
        loader.vkResetFences(device, 1, &frameDone);
        vulkan::VkSubmitInfo submit{};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commandBuffer;
        // The semaphore wait lands where the colour output starts: the
        // swapchain image may still be in the presentation engine's hands.
        vulkan::VkFlags waitStage = static_cast<vulkan::VkFlags>(
            vulkan::PipelineStage::ColorAttachmentOutput);
        if (swapchain_frame)
        {
            submit.waitSemaphoreCount = 1;
            submit.pWaitSemaphores = &imageAcquired;
            submit.pWaitDstStageMask = &waitStage;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores = &renderDone;
        }
        loader.vkQueueSubmit(queue, 1, &submit, frameDone);
        loader.vkWaitForFences(device, 1, &frameDone, 1, ~0ull);
    }

    /// The pipeline of one program at the current depth state and render
    /// pass (created on first use, cached in the pipelines map).
    vulkan::VkPipeline ensurePipeline(VulkanRenderBackend& backend,
                                      vulkan::VkShaderProgram* program,
                                      vulkan::VkPipelineLayout layout)
    {
        vulkan::VkRenderPass renderPass = nullptr;
        if (bound_pass != nullptr)
            renderPass = targetResources[bound_pass].renderPass;
        else
            renderPass = swapchainRenderPass;
        PipelineKey key{};
        key.vertexSpirv = program->vertexSpirv();
        key.fragmentSpirv = program->fragmentSpirv();
        key.depthTest = depth_test;
        key.renderPass = renderPass;
        auto it = pipelines.find(key);
        if (it != pipelines.end())
            return it->second;
        vulkan::VkPipeline pipeline =
            createPipeline(program, depth_test, renderPass, layout);
        pipelines.emplace(key, pipeline);
        (void)backend;
        return pipeline;
    }
};

VulkanRenderBackend::VulkanRenderBackend(VulkanBackendConfig config)
    : m_impl(std::make_unique<Impl>())
{
    if (config.width == 0 || config.height == 0)
        throw std::invalid_argument(
            "VulkanRenderBackend: width and height must be positive");
    m_impl->config = config;
    // Documented diagnostic default camera: eye (0,0,1) at the origin,
    // up +Y, fovY 90 degrees, aspect from the frame size.
    m_impl->camera.aspect =
        static_cast<float>(config.width) / static_cast<float>(config.height);
}

VulkanRenderBackend::~VulkanRenderBackend()
{
    if (m_impl->state == Impl::State::Initialised)
        shutdownImpl(false);
}

void VulkanRenderBackend::initialise()
{
    Impl& impl = *m_impl;
    if (impl.state != Impl::State::Created)
        throw std::logic_error(
            "VulkanRenderBackend::initialise: already initialised");

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        throw std::runtime_error(std::string("SDL_Init failed: ")
                                 + SDL_GetError());

    if (SDL_Vulkan_LoadLibrary(nullptr) != 0)
    {
        const std::string message =
            std::string("SDL_Vulkan_LoadLibrary failed: ") + SDL_GetError();
        sdlVideoRelease();
        throw std::runtime_error(message);
    }

    const int width = static_cast<int>(impl.config.width);
    const int height = static_cast<int>(impl.config.height);
    Uint32 window_flags = static_cast<Uint32>(SDL_WINDOW_VULKAN);
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

    // Instance level: the loader resolves through the SDL bridge's
    // vkGetInstanceProcAddr, the instance is created with the extensions
    // the platform surface needs, and the surface comes from SDL's bridge.
    std::string missing;
    if (!impl.loader.load(sdlResolve(), missing))
    {
        const std::string message = "Vulkan loader: could not resolve '"
            + missing + "'";
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }

    unsigned extension_count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(impl.window, &extension_count,
                                          nullptr))
    {
        const std::string message = std::string(
            "SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError();
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    std::vector<const char*> extensions(extension_count);
    if (!SDL_Vulkan_GetInstanceExtensions(impl.window, &extension_count,
                                          extensions.data()))
    {
        const std::string message = std::string(
            "SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError();
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }

    vulkan::VkApplicationInfo appInfo{};
    appInfo.pApplicationName = "OpenGrace";
    appInfo.apiVersion = (1u << 22) | (3u << 12);
    vulkan::VkInstanceCreateInfo instanceInfo{};
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledExtensionCount = extension_count;
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    if (impl.loader.vkCreateInstance(&instanceInfo, nullptr, &impl.instance)
        != vulkan::Result::Success)
    {
        const std::string message("vkCreateInstance failed");
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    if (!impl.loader.loadInstance(
            sdlResolve(),
            static_cast<vulkan::VkInstance>(impl.instance), missing))
    {
        const std::string message = "Vulkan loader: could not resolve '"
            + missing + "'";
        impl.loader.vkDestroyInstance(impl.instance, nullptr);
        impl.instance = nullptr;
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    // The surface comes from SDL's Vulkan bridge: the real
    // vkCreateSurfaceKHR is platform-specific (VkXcbSurfaceCreateInfoKHR on
    // Linux), and SDL owns the window handle. SDL_Vulkan_CreateSurface
    // returns the same VkSurfaceKHR handle.
    // The loader's handles are void*-based; SDL's bridge takes the real
    // Vulkan handles, so the call casts through them.
    VkInstance instanceHandle = static_cast<VkInstance>(impl.instance);
    VkSurfaceKHR surfaceHandle = nullptr;
    if (SDL_Vulkan_CreateSurface(impl.window, instanceHandle, &surfaceHandle)
        == SDL_FALSE)
    {
        const std::string message(std::string("SDL_Vulkan_CreateSurface"
                                              " failed: ")
                                  + SDL_GetError());
        impl.loader.vkDestroyInstance(impl.instance, nullptr);
        impl.instance = nullptr;
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    impl.surface = static_cast<vulkan::VkSurfaceKHR>(surfaceHandle);
    // Device pick: the first physical device with a graphics queue family
    // that presents on the surface. The properties fill the describe()
    // fields (api version, device name, vendor id).
    std::uint32_t device_count = 0;
    impl.loader.vkEnumeratePhysicalDevices(impl.instance, &device_count,
                                           nullptr);
    if (device_count == 0)
    {
        const std::string message("no Vulkan physical device");
        impl.loader.vkDestroySurfaceKHR(impl.instance, impl.surface, nullptr);
        impl.surface = nullptr;
        impl.loader.vkDestroyInstance(impl.instance, nullptr);
        impl.instance = nullptr;
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    std::vector<vulkan::VkPhysicalDevice> devices(device_count);
    impl.loader.vkEnumeratePhysicalDevices(impl.instance, &device_count,
                                           devices.data());
    vulkan::VkBool32 presentable = 0;
    for (vulkan::VkPhysicalDevice candidate : devices)
    {
        std::uint32_t family_count = 0;
        impl.loader.vkGetPhysicalDeviceQueueFamilyProperties(
            candidate, &family_count, nullptr);
        std::vector<vulkan::VkQueueFamilyProperties> families(family_count);
        impl.loader.vkGetPhysicalDeviceQueueFamilyProperties(
            candidate, &family_count, families.data());
        for (std::uint32_t f = 0; f < family_count; ++f)
        {
            if ((families[f].queueFlags
                 & static_cast<vulkan::VkFlags>(
                     vulkan::QueueCapability::Graphics))
                != 0u)
            {
                impl.loader.vkGetPhysicalDeviceSurfaceSupportKHR(
                    candidate, f, impl.surface, &presentable);
                if (presentable != 0)
                {
                    impl.physicalDevice = candidate;
                    impl.queueFamily = f;
                    break;
                }
            }
        }
        if (impl.physicalDevice != nullptr)
            break;
    }
    if (impl.physicalDevice == nullptr)
    {
        const std::string message(
            "no Vulkan device with a graphics+present queue");
        impl.loader.vkDestroySurfaceKHR(impl.instance, impl.surface, nullptr);
        impl.surface = nullptr;
        impl.loader.vkDestroyInstance(impl.instance, nullptr);
        impl.instance = nullptr;
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    vulkan::VkPhysicalDeviceProperties properties{};
    impl.loader.vkGetPhysicalDeviceProperties(impl.physicalDevice,
                                              &properties);
    impl.api_major = static_cast<int>(properties.apiVersion >> 22);
    impl.api_minor = static_cast<int>((properties.apiVersion >> 12) & 0x3FFu);
    impl.device_name = properties.deviceName;
    impl.vendor_name = std::to_string(properties.vendorID);

    const char* device_extensions[] = {"VK_KHR_swapchain"};
    float queue_priority = 1.0f;
    vulkan::VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.queueFamilyIndex = impl.queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queue_priority;
    vulkan::VkDeviceCreateInfo deviceInfo{};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = device_extensions;
    if (impl.loader.vkCreateDevice(impl.physicalDevice, &deviceInfo, nullptr,
                                   &impl.device)
        != vulkan::Result::Success)
    {
        const std::string message("vkCreateDevice failed");
        impl.loader.vkDestroySurfaceKHR(impl.instance, impl.surface, nullptr);
        impl.surface = nullptr;
        impl.loader.vkDestroyInstance(impl.instance, nullptr);
        impl.instance = nullptr;
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    if (!impl.loader.loadDevice(impl.device, missing))
    {
        const std::string message = "Vulkan loader: could not resolve '"
            + missing + "'";
        impl.loader.vkDestroyDevice(impl.device, nullptr);
        impl.device = nullptr;
        impl.loader.vkDestroySurfaceKHR(impl.instance, impl.surface, nullptr);
        impl.surface = nullptr;
        impl.loader.vkDestroyInstance(impl.instance, nullptr);
        impl.instance = nullptr;
        SDL_DestroyWindow(impl.window);
        impl.window = nullptr;
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    impl.loader.vkGetDeviceQueue(impl.device, impl.queueFamily, 0,
                                 &impl.queue);

    // The device's depth/stencil attachment format: the pick runs once
    // (the fallback chain D24S8 -> D32Sfloat -> D16Unorm queries the
    // device's format properties) and every depth attachment, render
    // pass and readback unpack use this one.
    impl.depthFormat = pickDepthFormat(impl.loader, impl.physicalDevice);

    // The one shared sampler (nearest, clamp-to-edge): the GLSL's
    // sampler2D/sampler3D uniforms each combine a sampler with an image
    // view at descriptor time, and the engine's sampling rules (the
    // shadow maps, the volumes, the post-chain inputs) are all nearest
    // with clamp-to-edge, the same rules the OpenGL backend's
    // GL_NEAREST/GL_CLAMP_TO_EDGE state implements.
    vulkan::VkSamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vulkan::Filter::Nearest;
    samplerInfo.minFilter = vulkan::Filter::Nearest;
    samplerInfo.addressModeU = vulkan::SamplerAddressMode::ClampToEdge;
    samplerInfo.addressModeV = vulkan::SamplerAddressMode::ClampToEdge;
    samplerInfo.addressModeW = vulkan::SamplerAddressMode::ClampToEdge;
    if (impl.loader.vkCreateSampler(impl.device, &samplerInfo, nullptr,
                                    &impl.sharedSampler)
        != vulkan::Result::Success)
    {
        const std::string message("vkCreateSampler failed");
        impl.shutdownPartial();
        sdlVideoRelease();
        throw std::runtime_error(message);
    }

    vulkan::VkCommandPoolCreateInfo poolInfo{};
    poolInfo.queueFamilyIndex = impl.queueFamily;
    if (impl.loader.vkCreateCommandPool(impl.device, &poolInfo, nullptr,
                                        &impl.commandPool)
        != vulkan::Result::Success)
    {
        const std::string message("vkCreateCommandPool failed");
        impl.shutdownPartial();
        sdlVideoRelease();
        throw std::runtime_error(message);
    }

    // The default texture (1x1 opaque white): the descriptor set's sampler
    // bindings the engine never writes still name an image view (the
    // device's own samplers on the OpenGL contract's unbound slots), and
    // the shader samples a valid, opaque image either way.
    impl.defaultImage = impl.createImage(
        vulkan::Format::R8G8B8A8Unorm, 1, 1,
        static_cast<vulkan::VkFlags>(
            vulkan::ImageUsage::TransferDst
            | vulkan::ImageUsage::Sampled),
        static_cast<vulkan::VkFlags>(vulkan::SampleCount::One));
    {
        vulkan::VkMemoryRequirements defaultRequirements{};
        impl.loader.vkGetImageMemoryRequirements(
            impl.device, impl.defaultImage, &defaultRequirements);
        impl.defaultImageMemory = impl.allocateMemoryFor(
            defaultRequirements.memoryTypeBits,
            static_cast<std::size_t>(defaultRequirements.size),
            vulkan::MemoryProperty::DeviceLocal);
        impl.loader.vkBindImageMemory(impl.device, impl.defaultImage,
                                      impl.defaultImageMemory, 0);
        impl.defaultImageView = impl.createImageView(
            impl.defaultImage, vulkan::Format::R8G8B8A8Unorm,
            vulkan::ImageAspect::Color);
        // The one pixel is opaque white: the staging buffer carries the
        // RGBA8 bytes and the one-off submit copies them into the image.
        vulkan::VkBuffer stagingBuffer = nullptr;
        vulkan::VkDeviceMemory stagingMemory = nullptr;
        impl.createHostBuffer(
            static_cast<vulkan::VkFlags>(vulkan::BufferUsage::TransferSrc),
            4, stagingBuffer, stagingMemory);
        void* mapped = nullptr;
        impl.loader.vkMapMemory(impl.device, stagingMemory, 0, 4, 0,
                                &mapped);
        const std::uint8_t white[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        std::memcpy(mapped, white, sizeof(white));
        impl.loader.vkUnmapMemory(impl.device, stagingMemory);
        vulkan::VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = vulkan::ImageAspect::Color;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {1, 1, 1};
        vulkan::VkCommandBuffer oneOff = impl.beginOneOff();
        impl.recordLayoutTransition(
            oneOff, impl.defaultImage, vulkan::ImageAspect::Color,
            vulkan::ImageLayout::Undefined,
            vulkan::ImageLayout::TransferDstOptimal);
        impl.loader.vkCmdCopyBufferToImage(
            oneOff, stagingBuffer, impl.defaultImage,
            vulkan::ImageLayout::TransferDstOptimal, 1, &region);
        impl.recordLayoutTransition(
            oneOff, impl.defaultImage, vulkan::ImageAspect::Color,
            vulkan::ImageLayout::TransferDstOptimal,
            vulkan::ImageLayout::ShaderReadOnlyOptimal);
        impl.loader.vkEndCommandBuffer(oneOff);
        impl.submitOneOff(oneOff);
        impl.loader.vkDestroyBuffer(impl.device, stagingBuffer, nullptr);
        // The staging memory stays in memory_blocks: the shutdown's free
        // loop releases every allocation exactly once.
    }

    // The swapchain resources: the surface query, the swapchain itself,
    // the image views and the depth attachment. The initialisation runs
    // it once; the acquire path runs it again when the swapchain goes
    // out of date (a surface resize), so a failed first run cleans up
    // the same partial state the recreate path would.
    try
    {
        impl.createSwapchainResources();
    }
    catch (const std::exception&)
    {
        impl.shutdownPartial();
        sdlVideoRelease();
        throw;
    }
    vulkan::VkSemaphoreCreateInfo semaphoreInfo{};
    if (impl.loader.vkCreateSemaphore(impl.device, &semaphoreInfo, nullptr,
                                      &impl.imageAcquired)
            != vulkan::Result::Success
        || impl.loader.vkCreateSemaphore(impl.device, &semaphoreInfo, nullptr,
                                         &impl.renderDone)
               != vulkan::Result::Success)
    {
        const std::string message("vkCreateSemaphore failed");
        impl.shutdownPartial();
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    vulkan::VkFenceCreateInfo fenceInfo{};
    fenceInfo.flags = static_cast<vulkan::VkFlags>(
        vulkan::FenceCreate::Signaled);
    if (impl.loader.vkCreateFence(impl.device, &fenceInfo, nullptr,
                                  &impl.frameDone)
        != vulkan::Result::Success)
    {
        const std::string message("vkCreateFence failed");
        impl.shutdownPartial();
        sdlVideoRelease();
        throw std::runtime_error(message);
    }
    vulkan::VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.commandPool = impl.commandPool;
    commandInfo.commandBufferCount = 1;
    if (impl.loader.vkAllocateCommandBuffers(impl.device, &commandInfo,
                                             &impl.commandBuffer)
        != vulkan::Result::Success)
    {
        const std::string message("vkAllocateCommandBuffers failed");
        impl.shutdownPartial();
        sdlVideoRelease();
        throw std::runtime_error(message);
    }

    // The readback staging buffer (one RGBA8 pixel + depth float).
    impl.createHostBuffer(
        static_cast<vulkan::VkFlags>(vulkan::BufferUsage::TransferDst),
        5 * sizeof(float), impl.readbackBuffer, impl.readbackMemory);

    impl.state = Impl::State::Initialised;
}

std::string VulkanRenderBackend::describe() const
{

    const Impl& impl = *m_impl;
    if (impl.state == Impl::State::Created)
        return "vulkan (not initialised)";
    return "vulkan " + std::to_string(impl.api_major) + "." +
           std::to_string(impl.api_minor) + "; vendor=" + impl.vendor_name +
           "; renderer=" + impl.device_name;
}

void VulkanRenderBackend::setClearColor(ClearColor color)
{
    if (!m_impl->ready())
    {
        ++m_impl->violations;
        return;
    }
    m_impl->clear_color = color;
}

ClearColor VulkanRenderBackend::clearColor() const
{
    return m_impl->clear_color;
}
void VulkanRenderBackend::setClearBuffers(std::uint32_t mask) noexcept
{
    m_impl->clear_mask = mask;
}
void VulkanRenderBackend::setClearDepth(float depth) noexcept
{
    m_impl->clear_depth = depth;
}
void VulkanRenderBackend::setClearStencil(std::int32_t stencil) noexcept
{
    m_impl->clear_stencil = stencil;
}
void VulkanRenderBackend::setDepthTestEnabled(bool enabled)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    impl.depth_test = enabled;
}

std::uint32_t VulkanRenderBackend::width() const
{
    return m_impl->config.width;
}

std::uint32_t VulkanRenderBackend::height() const
{
    return m_impl->config.height;
}

void VulkanRenderBackend::shutdown()
{
    shutdownImpl(true);
}

void VulkanRenderBackend::shutdownImpl(bool countViolation)
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
    impl.loader.vkDeviceWaitIdle(impl.device);
    // Release the foundation objects while the device is still alive.
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
    for (vulkan::VkFramebuffer framebuffer : impl.swapchainFramebuffers)
        impl.loader.vkDestroyFramebuffer(impl.device, framebuffer, nullptr);
    impl.swapchainFramebuffers.clear();
    if (impl.swapchainRenderPass != nullptr)
        impl.loader.vkDestroyRenderPass(impl.device, impl.swapchainRenderPass,
                                        nullptr);
    impl.swapchainRenderPass = nullptr;
    for (vulkan::VkImageView view : impl.swapchainViews)
        impl.loader.vkDestroyImageView(impl.device, view, nullptr);
    impl.swapchainViews.clear();
    if (impl.defaultImageView != nullptr)
        impl.loader.vkDestroyImageView(impl.device, impl.defaultImageView,
                                       nullptr);
    impl.defaultImageView = nullptr;
    if (impl.defaultImage != nullptr)
        impl.loader.vkDestroyImage(impl.device, impl.defaultImage, nullptr);
    impl.defaultImage = nullptr;
    if (impl.sharedSampler != nullptr)
        impl.loader.vkDestroySampler(impl.device, impl.sharedSampler,
                                     nullptr);
    impl.sharedSampler = nullptr;
    if (impl.readbackMapped != nullptr)
    {
        impl.loader.vkUnmapMemory(impl.device, impl.readbackMemory);
        impl.readbackMapped = nullptr;
    }
    for (vulkan::VkDeviceMemory memory : impl.memory_blocks)
        impl.loader.vkFreeMemory(impl.device, memory, nullptr);
    impl.memory_blocks.clear();
    impl.swapchainDepthImage = nullptr;
    impl.swapchainDepthMemory = nullptr;
    impl.swapchainDepthView = nullptr;
    if (impl.readbackBuffer != nullptr)
        impl.loader.vkDestroyBuffer(impl.device, impl.readbackBuffer, nullptr);
    impl.readbackBuffer = nullptr;
    impl.loader.vkFreeCommandBuffers(impl.device, impl.commandPool, 1,
                                     &impl.commandBuffer);
    impl.commandBuffer = nullptr;
    impl.loader.vkDestroyCommandPool(impl.device, impl.commandPool, nullptr);
    impl.commandPool = nullptr;
    impl.loader.vkDestroyFence(impl.device, impl.frameDone, nullptr);
    impl.frameDone = nullptr;
    impl.loader.vkDestroySemaphore(impl.device, impl.renderDone, nullptr);
    impl.renderDone = nullptr;
    impl.loader.vkDestroySemaphore(impl.device, impl.imageAcquired, nullptr);
    impl.imageAcquired = nullptr;
    impl.loader.vkDestroySwapchainKHR(impl.device, impl.swapchain, nullptr);
    impl.swapchain = nullptr;
    impl.loader.vkDestroyDevice(impl.device, nullptr);
    impl.device = nullptr;
    impl.loader.vkDestroySurfaceKHR(impl.instance, impl.surface, nullptr);
    impl.surface = nullptr;
    impl.loader.vkDestroyInstance(impl.instance, nullptr);
    impl.instance = nullptr;
    SDL_DestroyWindow(impl.window);
    impl.window = nullptr;
    sdlVideoRelease();
    impl.state = Impl::State::Shutdown;
}

std::uint64_t VulkanRenderBackend::violationCount() const noexcept
{
    return m_impl->violations;
}

std::unique_ptr<ShaderProgram> VulkanRenderBackend::createShaderProgram(
    ShaderProgramId id)
{
    if (!m_impl->ready())
    {
        ++m_impl->violations;
        throw std::logic_error(
            "VulkanRenderBackend::createShaderProgram: not initialised");
    }
    return std::make_unique<vulkan::VkShaderProgram>(id);
}

std::unique_ptr<GpuMesh> VulkanRenderBackend::createGpuMesh()
{
    if (!m_impl->ready())
    {
        ++m_impl->violations;
        throw std::logic_error(
            "VulkanRenderBackend::createGpuMesh: not initialised");
    }
    return std::make_unique<vulkan::VkMesh>();
}

void VulkanRenderBackend::applyViewport()
{
    Impl& impl = *m_impl;
    if (impl.commandBuffer == nullptr)
        return;
    vulkan::VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.maxDepth = 1.0f;
    if (impl.bound_target != nullptr)
    {
        viewport.width = static_cast<float>(impl.bound_target->width());
        viewport.height = static_cast<float>(impl.bound_target->height());
    }
    else
    {
        viewport.width = static_cast<float>(impl.config.width);
        viewport.height = static_cast<float>(impl.config.height);
    }
    vulkan::VkRect2D scissor{};
    scissor.extent = {static_cast<std::uint32_t>(viewport.width),
                      static_cast<std::uint32_t>(viewport.height)};
    impl.loader.vkCmdSetViewport(impl.commandBuffer, 0, 1, &viewport);
    impl.loader.vkCmdSetScissor(impl.commandBuffer, 0, 1, &scissor);
}

void VulkanRenderBackend::applyDepthState()
{
    Impl& impl = *m_impl;
    if (impl.commandBuffer == nullptr)
        return;
    // The pipeline is per program + depth test + render pass; the depth
    // toggle lives in the pipeline (decision 20's no-test/no-write state).
}

void VulkanRenderBackend::ensureTriangle()
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
    vulkan::VkMesh* meshHandle = static_cast<vulkan::VkMesh*>(impl.tri_mesh.get());
    impl.createMeshBuffers(*this, meshHandle);
    meshHandle->clearCpuData();
    impl.triangle_ready = true;
}

void VulkanRenderBackend::clear()
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    // The clear happens inside the render pass that records the frame: the
    // attachments load the clear colour (or the previous contents) when the
    // frame's command buffer runs. The recorded state lives until the
    // swapBuffers presents the frame.
    // Inside an open pass the clear targets the bound target (the GL clear
    // writes the bound framebuffer); outside one it opens the default
    // framebuffer's pass. The triangle follows the same rule as the GL's
    // diagnostic draw: it runs on the default framebuffer only.
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
    impl.submitFrame();
}

void VulkanRenderBackend::swapBuffers()
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    // Readback happens before the present: the swapchain image contents are
    // undefined after a present (the same pre-present rule the OpenGL
    // backend follows).
    impl.submitFrame();
    if (impl.config.readback_center || !impl.readback_queue.empty())
        readback();
    impl.present();
}

void VulkanRenderBackend::draw(GpuMesh& mesh, ShaderProgram& program)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    applyViewport();
    vulkan::VkMesh* meshHandle = static_cast<vulkan::VkMesh*>(&mesh);
    vulkan::VkShaderProgram* programHandle =
        static_cast<vulkan::VkShaderProgram*>(&program);
    impl.drawMesh(*this, meshHandle, programHandle);
}

void VulkanRenderBackend::submit(const DrawSubmission& submission)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    submitImpl(submission);
}

void VulkanRenderBackend::ensureLightingProgram()
{
    Impl& impl = *m_impl;
    if (!impl.lighting_program)
        impl.lighting_program = std::shared_ptr<ShaderProgram>(
            createShaderProgram(ShaderProgramId::ForwardLighting));
}

void VulkanRenderBackend::setCamera(const CameraParams& camera)
{
    camera.validate();
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        throw std::logic_error(
            "VulkanRenderBackend::setCamera: not initialised");
    }
    impl.camera = camera;
}

CameraParams VulkanRenderBackend::camera() const
{
    return m_impl->camera;
}

void VulkanRenderBackend::drawTriangle()
{
    Impl& impl = *m_impl;
    ensureTriangle();
    // Full-frame viewport: NDC maps onto the whole framebuffer/target, so
    // the result is correct for non-square sizes.
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
    vulkan::VkShaderProgram* program =
        static_cast<vulkan::VkShaderProgram*>(impl.tri_shader.get());
    program->setUniform("mvp", mvp);
    const ClearColor color = impl.config.triangle_color;
    program->setUniform(
        "color", core::vec4{static_cast<float>(color.r) / 255.0f,
                            static_cast<float>(color.g) / 255.0f,
                            static_cast<float>(color.b) / 255.0f,
                            static_cast<float>(color.a) / 255.0f});
    vulkan::VkMesh* meshHandle =
        static_cast<vulkan::VkMesh*>(impl.tri_mesh.get());
    // The OpenGL context's depth test is still disabled when clear() draws
    // this triangle (applyDepthState first runs in beginRenderPass), so the
    // diagnostic draw never writes depth. The pipeline key carries the
    // depth flag, so this scoped toggle selects the no-test pipeline.
    const bool depth_state = impl.depth_test;
    impl.depth_test = false;
    impl.drawMesh(*this, meshHandle, program);
    impl.depth_test = depth_state;
}

int VulkanRenderBackend::apiMajor() const noexcept
{
    return m_impl->api_major;
}

int VulkanRenderBackend::apiMinor() const noexcept
{
    return m_impl->api_minor;
}

std::string VulkanRenderBackend::deviceName() const noexcept
{
    return m_impl->device_name;
}

std::string VulkanRenderBackend::vendorName() const noexcept
{
    return m_impl->vendor_name;
}

RenderTarget* VulkanRenderBackend::createRenderTarget(
    const RenderTargetDesc& desc)
{
    Impl& impl = *m_impl;
    validateRenderTargetDesc(desc);
    if (!impl.ready())
    {
        ++impl.violations;
        throw std::logic_error(
            "VulkanRenderBackend::createRenderTarget: not initialised");
    }
    auto target = std::make_unique<vulkan::VkRenderTarget>(desc);
    vulkan::VkRenderTarget* raw = target.get();
    impl.createTargetResources(*this, raw);
    impl.targets.push_back(std::move(target));
    return raw;
}

void VulkanRenderBackend::beginRenderPass(RenderTarget* target)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    if (target == nullptr)
    {
        // Target the default framebuffer (the swapchain image).
        impl.beginFrame(*this, nullptr);
        impl.bound_target = nullptr;
        return;
    }
    vulkan::VkRenderTarget* known =
        findVkTarget(impl.targets, target);
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

void VulkanRenderBackend::endRenderPass()
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

void VulkanRenderBackend::releaseRenderTarget(RenderTarget* target)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    vulkan::VkRenderTarget* known =
        findVkTarget(impl.targets, target);
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

TextureView* VulkanRenderBackend::renderTargetTexture(
    RenderTarget* target)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return nullptr;
    }
    vulkan::VkRenderTarget* known =
        findVkTarget(impl.targets, target);
    if (known == nullptr || known->released || impl.bound_target == known)
    {
        ++impl.violations;
        return nullptr;
    }
    if (known->hasColorAttachment())
        return known->colorView(0);
    // A depth-only target's contract-facing texture is its depth attachment
    // (the shadow-pass target's sampled depth view).
    if (known->hasDepthAttachment())
        return known->depthView();
    ++impl.violations;
    return nullptr;
}

TextureView* VulkanRenderBackend::renderTargetTexture(
    RenderTarget* target, std::uint32_t attachment)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return nullptr;
    }
    vulkan::VkRenderTarget* known =
        findVkTarget(impl.targets, target);
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

void VulkanRenderBackend::bindTexture(const TextureView* view,
                                      std::uint32_t unit)
{
    Impl& impl = *m_impl;
    if (!impl.ready() || view == nullptr)
    {
        ++impl.violations;
        return;
    }
    const auto* vkView = dynamic_cast<const vulkan::VkTextureView*>(view);
    if (vkView == nullptr)
    {
        ++impl.violations;
        return;
    }
    // The sampler unit is recorded: the draw's descriptor set write binds
    // the view's image view at the unit's slot.
    if (unit < impl.boundTextures.size())
        impl.boundTextures[unit] = vkView;
    else
        impl.boundTextures.resize(unit + 1, nullptr);
    if (impl.boundTextures[unit] == nullptr)
        impl.boundTextures[unit] = vkView;
}

TextureView* VulkanRenderBackend::createTexture3D(const Texture3DDesc& desc)
{
    Impl& impl = *m_impl;
    validateTexture3DDesc(desc);
    if (!impl.ready())
    {
        ++impl.violations;
        throw std::logic_error(
            "VulkanRenderBackend::createTexture3D: not initialised");
    }
    auto volume = std::make_unique<vulkan::VkTexture3DView>(desc);
    vulkan::VkTexture3DView* raw = volume.get();
    impl.createVolumeResources(*this, raw);
    impl.volumes.push_back(std::move(volume));
    return raw;
}

void VulkanRenderBackend::releaseTexture3D(TextureView* view)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    vulkan::VkTexture3DView* known =
        findVkVolume(impl.volumes, view);
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

void VulkanRenderBackend::bindTexture3D(const TextureView* view,
                                        std::uint32_t unit)
{
    Impl& impl = *m_impl;
    if (!impl.ready() || view == nullptr)
    {
        ++impl.violations;
        return;
    }
    const auto* vkView =
        dynamic_cast<const vulkan::VkTexture3DView*>(view);
    if (vkView == nullptr)
    {
        ++impl.violations;  // a 2D attachment view is not a volume
        return;
    }
    if (unit < impl.boundTextures.size())
        impl.boundTextures[unit] = vkView;
    else
        impl.boundTextures.resize(unit + 1, nullptr);
    if (impl.boundTextures[unit] == nullptr)
        impl.boundTextures[unit] = vkView;
}

void VulkanRenderBackend::fillTexture3D(TextureView* view,
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
    vulkan::VkTexture3DView* volume =
        findVkVolume(impl.volumes, view);
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

bool VulkanRenderBackend::readTexture3D(const TextureView* view,
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
    vulkan::VkTexture3DView* volume =
        findVkVolume(impl.volumes, view);
    if (volume == nullptr || volume->released
        || layer >= volume->depth()
        || sizeBytes != volume->sliceBytes())
    {
        ++impl.violations;
        return false;
    }
    return impl.readVolumeLayer(*this, volume, layer, data, sizeBytes);
}

RenderTarget* VulkanRenderBackend::createVolumeLayerTarget(
    TextureView* volume, std::uint32_t layer)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        throw std::logic_error(
            "VulkanRenderBackend::createVolumeLayerTarget: not initialised");
    }
    vulkan::VkTexture3DView* known =
        findVkVolume(impl.volumes, volume);
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
    auto target = std::make_unique<vulkan::VkRenderTarget>(desc);
    vulkan::VkRenderTarget* raw = target.get();
    // The layer target records the volume it borrows: the resource creation
    // reads the volume's image from the registry through this handle.
    raw->setVolumeView(volume);
    impl.createTargetResources(*this, raw);
    impl.targets.push_back(std::move(target));
    return raw;
}

void VulkanRenderBackend::copyDepthAttachment(RenderTarget* source,
                                              RenderTarget* destination)
{
    Impl& impl = *m_impl;
    if (!impl.ready())
    {
        ++impl.violations;
        return;
    }
    vulkan::VkRenderTarget* src =
        findVkTarget(impl.targets, source);
    vulkan::VkRenderTarget* dst =
        findVkTarget(impl.targets, destination);
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

void VulkanRenderBackend::queueReadback(std::uint32_t x, std::uint32_t y)
{
    if (!m_impl->ready())
    {
        ++m_impl->violations;
        return;
    }
    m_impl->readback_queue.emplace_back(x, y);
}

void VulkanRenderBackend::readback()
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

void VulkanRenderBackend::readCenter()
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

const std::vector<ReadbackSample>& VulkanRenderBackend::lastReadback() const
    noexcept
{
    return m_impl->last_readback;
}

bool VulkanRenderBackend::hasCenterPixel() const noexcept
{
    return m_impl->has_center;
}

ClearColor VulkanRenderBackend::centerPixel() const noexcept
{
    return m_impl->center_pixel;
}

float VulkanRenderBackend::centerDepth() const noexcept
{
    return m_impl->center_depth;
}

std::int32_t VulkanRenderBackend::centerStencil() const noexcept
{
    return m_impl->center_stencil;
}

void VulkanRenderBackend::captureQueuedReadbacks()
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

void VulkanRenderBackend::submitImpl(const DrawSubmission& submission)
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

    // Cascaded sun shadow map carried by the submission (milestone 4c). The
    // backend treats it as plain data: the cascade views land on the sampler
    // bindings the layout table declares.
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
    // from the submission. The front/back views land on bindings 2 and 3.
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

    vulkan::VkMesh* meshHandle =
        static_cast<vulkan::VkMesh*>(const_cast<GpuMesh*>(submission.mesh));
    vulkan::VkShaderProgram* programHandle =
        static_cast<vulkan::VkShaderProgram*>(&program);
    impl.drawMesh(*this, meshHandle, programHandle);
}

}  // namespace grace::render
