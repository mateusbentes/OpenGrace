#pragma once

// The Vulkan render module's own entry-point loader. RULE (the same rule the
// OpenGL loader records): every constant, structure and function signature
// the engine uses is declared here, so no generated header and no third-party
// loader joins the tree, and the platform-neutral contract never includes it.
// Entry points are resolved at runtime; the backend passes the resolve
// callback in, so this header needs no SDL.
#include <cstddef>
#include <cstdint>
#include <string>

namespace grace::render {
namespace vulkan {

using VkBool32 = std::uint32_t;
using VkFlags = std::uint32_t;

// Vulkan handles are opaque pointers for the types the engine uses.
using VkInstance = void*;
using VkPhysicalDevice = void*;
using VkDevice = void*;
using VkQueue = void*;
using VkCommandBuffer = void*;
using VkBuffer = void*;
using VkImage = void*;
using VkImageView = void*;
using VkDeviceMemory = void*;
using VkCommandPool = void*;
using VkFence = void*;
using VkSemaphore = void*;
using VkFramebuffer = void*;
using VkRenderPass = void*;
using VkPipeline = void*;
using VkPipelineCache = void*;
using VkPipelineLayout = void*;
using VkShaderModule = void*;
using VkDescriptorSet = void*;
using VkDescriptorSetLayout = void*;
using VkDescriptorPool = void*;
using VkSampler = void*;
using VkSurfaceKHR = void*;
using VkSwapchainKHR = void*;

// Fixed-size Vulkan constants (the published API's values).
inline constexpr std::uint32_t kMaxExtensionNameSize = 256u;
inline constexpr std::uint32_t kQueueFamilyIgnored = ~0u;
inline constexpr std::uint32_t kLodClampNone = 1000u;
inline constexpr std::uint32_t kWholeSize = ~0u;

enum class Format : std::uint32_t
{
    R8Unorm = 9,
    R32Sfloat = 100,
    Rg32Sfloat = 103,
    Rgba32Sfloat = 109,
    R8G8B8A8Unorm = 37,
    B8G8R8A8Unorm = 44,
    B8G8R8A8Srgb = 50,
    R32G32B32Sfloat = 106,
    R16G16B16A16Sfloat = 97,
    D24UnormS8Uint = 129,
    R32Uint = 98,
    R32Sint = 99,
    Rg32Sint = 102,
    Rgba32Sint = 108,
};

enum class ImageLayout : std::uint32_t
{
    Undefined = 0,
    General = 1,
    ColorAttachmentOptimal = 2,
    DepthStencilAttachmentOptimal = 3,
    DepthStencilReadOnlyOptimal = 4,
    ShaderReadOnlyOptimal = 5,
    TransferSrcOptimal = 6,
    TransferDstOptimal = 7,
    PresentSrc = 1000001002,
};

enum class ImageAspect : std::uint32_t
{
    Color = 0x1u,
    Depth = 0x2u,
    Stencil = 0x4u,
};

enum class BufferUsage : std::uint32_t
{
    TransferSrc = 0x1u,
    TransferDst = 0x2u,
    UniformBuffer = 0x10u,
    VertexBuffer = 0x80u,
    IndexBuffer = 0x40u,
};

enum class ImageUsage : std::uint32_t
{
    TransferSrc = 0x1u,
    TransferDst = 0x2u,
    Sampled = 0x4u,
    ColorAttachment = 0x10u,
    DepthStencilAttachment = 0x20u,
};

enum class MemoryProperty : std::uint32_t
{
    DeviceLocal = 0x1u,
    HostVisible = 0x2u,
    HostCoherent = 0x4u,
};

enum class PipelineStage : std::uint32_t
{
    TopOfPipe = 0x1u,
    BottomOfPipe = 0x2u,
    VertexInput = 0x10u,
    FragmentShader = 0x80u,
    EarlyFragmentTests = 0x100u,
    LateFragmentTests = 0x200u,
    ColorAttachmentOutput = 0x400u,
    Transfer = 0x2000u,
    AllCommands = 0x10000u,
};

enum class Access : std::uint32_t
{
    ShaderRead = 0x20u,
    ShaderWrite = 0x40u,
    ColorAttachmentRead = 0x80u,
    ColorAttachmentWrite = 0x100u,
    DepthStencilAttachmentRead = 0x200u,
    DepthStencilAttachmentWrite = 0x400u,
    TransferRead = 0x800u,
    TransferWrite = 0x1000u,
    MemoryRead = 0x8000u,
    MemoryWrite = 0x10000u,
};

enum class StructureType : std::uint32_t
{
    ApplicationInfo = 0,
    InstanceCreateInfo = 1,
    DeviceQueueCreateInfo = 2,
    DeviceCreateInfo = 3,
    SubmitInfo = 4,
    MemoryAllocateInfo = 5,
    MappedMemoryRange = 6,
    FenceCreateInfo = 8,
    SemaphoreCreateInfo = 9,
    BufferCreateInfo = 12,
    ImageCreateInfo = 14,
    ImageViewCreateInfo = 15,
    ShaderModuleCreateInfo = 16,
    PipelineShaderStageCreateInfo = 18,
    PipelineVertexInputStateCreateInfo = 19,
    PipelineInputAssemblyStateCreateInfo = 20,
    PipelineViewportStateCreateInfo = 22,
    PipelineRasterizationStateCreateInfo = 23,
    PipelineMultisampleStateCreateInfo = 24,
    PipelineDepthStencilStateCreateInfo = 25,
    PipelineColorBlendStateCreateInfo = 26,
    PipelineDynamicStateCreateInfo = 27,
    GraphicsPipelineCreateInfo = 28,
    PipelineCacheCreateInfo = 17,
    PipelineTessellationStateCreateInfo = 21,
    PipelineLayoutCreateInfo = 30,
    SamplerCreateInfo = 31,
    RenderPassCreateInfo = 38,
    DescriptorSetLayoutCreateInfo = 32,
    DescriptorPoolCreateInfo = 33,
    DescriptorSetAllocateInfo = 34,
    WriteDescriptorSet = 35,
    CommandPoolCreateInfo = 39,
    CommandBufferAllocateInfo = 40,
    CommandBufferBeginInfo = 42,
    RenderPassBeginInfo = 43,
    FramebufferCreateInfo = 37,
    BufferMemoryBarrier = 44,
    ImageMemoryBarrier = 45,
    MemoryBarrier = 46,
    SwapchainCreateInfoKHR = 1000001000,
    PresentInfoKHR = 1000001001,
};

enum class ShaderStage : std::uint32_t
{
    Vertex = 0x1u,
    Fragment = 0x10u,
};

enum class BlendFactor : std::uint32_t
{
    Zero = 0,
    One = 1,
    SrcAlpha = 6,
    OneMinusSrcAlpha = 7,
};

enum class BlendOp : std::uint32_t
{
    Add = 0,
};

enum class CullMode : std::uint32_t
{
    None = 0,
    Back = 0x2u,
};

enum class FrontFace : std::uint32_t
{
    CounterClockwise = 0,
};

enum class SampleCount : std::uint32_t
{
    One = 0x1u,
};

enum class CompareOp : std::uint32_t
{
    Less = 1,
    Always = 7,
};

enum class StencilOp : std::uint32_t
{
    Keep = 0,
    Zero = 1,
    Replace = 2,
};

enum class AttachmentLoadOp : std::uint32_t
{
    Load = 0,
    Clear = 1,
    DontCare = 2,
};

enum class AttachmentStoreOp : std::uint32_t
{
    Store = 0,
    DontCare = 1,
};

enum class PipelineBindPoint : std::uint32_t
{
    Graphics = 0,
};

enum class DescriptorType : std::uint32_t
{
    CombinedImageSampler = 1,
    UniformBuffer = 6,
};

enum class IndexType : std::uint32_t
{
    Uint32 = 1,
};

enum class VertexInputRate : std::uint32_t
{
    Vertex = 0,
};

enum class CommandBufferUsage : std::uint32_t
{
    OneTimeSubmit = 0x1u,
};

enum class FenceCreate : std::uint32_t
{
    Signaled = 0x1u,
};

enum class CommandPoolReset : std::uint32_t
{
    ReleaseResources = 0x1u,
};

enum class Result : std::int32_t
{
    Success = 0,
    NotReady = 2,
    ErrorExtensionNotPresent = -7,
    ErrorInitializationFailed = -3,
};

enum class Filter : std::uint32_t
{
    Nearest = 0,
    Linear = 1,
};

enum class SamplerAddressMode : std::uint32_t
{
    ClampToEdge = 2,
};

enum class ComponentSwizzle : std::uint32_t
{
    Identity = 0,
};

enum class CommandBufferLevel : std::uint32_t
{
    Primary = 0,
};

enum class DynamicState : std::uint32_t
{
    Viewport = 0,
    Scissor = 1,
};

enum class PolygonMode : std::uint32_t
{
    Fill = 0,
};

enum class SharingMode : std::uint32_t
{
    Exclusive = 0,
};

enum class ColorSpace : std::uint32_t
{
    SrgbNonlinear = 2,
};

enum class CompositeAlpha : std::uint32_t
{
    Opaque = 1,
};

enum class PresentMode : std::uint32_t
{
    Fifo = 2,
};

enum class ImageTiling : std::uint32_t
{
    Optimal = 0,
};

enum class ImageType : std::uint32_t
{
    Type2D = 1,
    Type3D = 2,
};

enum class SwapchainCreate : std::uint32_t
{
    None = 0,
};

enum class PrimitiveTopology : std::uint32_t
{
    TriangleList = 3,
};

enum class LogicOp : std::uint32_t
{
    Clear = 0,
};

enum class StencilFace : std::uint32_t
{
    FrontAndBack = 0x3u,
};

// -- Structures (the published API's field layout) --------------------------

struct VkApplicationInfo
{
    StructureType sType = StructureType::ApplicationInfo;
    const void* pNext = nullptr;
    const char* pApplicationName = nullptr;
    std::uint32_t applicationVersion = 0;
    const char* pEngineName = nullptr;
    std::uint32_t engineVersion = 0;
    std::uint32_t apiVersion = 0;
};

struct VkInstanceCreateInfo
{
    StructureType sType = StructureType::InstanceCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    const void* pApplicationInfo = nullptr;
    std::uint32_t enabledLayerCount = 0;
    const char* const* ppEnabledLayerNames = nullptr;
    std::uint32_t enabledExtensionCount = 0;
    const char* const* ppEnabledExtensionNames = nullptr;
};

struct VkDeviceQueueCreateInfo
{
    StructureType sType = StructureType::DeviceQueueCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    std::uint32_t queueFamilyIndex = 0;
    std::uint32_t queueCount = 1;
    const float* pQueuePriorities = nullptr;
};

struct VkDeviceCreateInfo
{
    StructureType sType = StructureType::DeviceCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    std::uint32_t queueCreateInfoCount = 0;
    const VkDeviceQueueCreateInfo* pQueueCreateInfos = nullptr;
    std::uint32_t enabledLayerCount = 0;
    const char* const* ppEnabledLayerNames = nullptr;
    std::uint32_t enabledExtensionCount = 0;
    const char* const* ppEnabledExtensionNames = nullptr;
    const void* pEnabledFeatures = nullptr;
};

struct VkSubmitInfo
{
    StructureType sType = StructureType::SubmitInfo;
    const void* pNext = nullptr;
    std::uint32_t waitSemaphoreCount = 0;
    const VkSemaphore* pWaitSemaphores = nullptr;
    const VkFlags* pWaitDstStageMask = nullptr;
    std::uint32_t commandBufferCount = 0;
    const VkCommandBuffer* pCommandBuffers = nullptr;
    std::uint32_t signalSemaphoreCount = 0;
    const VkSemaphore* pSignalSemaphores = nullptr;
};

struct VkMemoryAllocateInfo
{
    StructureType sType = StructureType::MemoryAllocateInfo;
    const void* pNext = nullptr;
    std::uint32_t allocationSize = 0;
    std::uint32_t memoryTypeIndex = 0;
};

struct VkMappedMemoryRange
{
    StructureType sType = StructureType::MappedMemoryRange;
    const void* pNext = nullptr;
    VkDeviceMemory memory = nullptr;
    std::uint32_t offset = 0;
    std::size_t size = 0;
};

struct VkFenceCreateInfo
{
    StructureType sType = StructureType::FenceCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
};

struct VkSemaphoreCreateInfo
{
    StructureType sType = StructureType::SemaphoreCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
};

struct VkBufferCreateInfo
{
    StructureType sType = StructureType::BufferCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    std::size_t size = 0;
    VkFlags usage = 0;
    SharingMode sharingMode = SharingMode::Exclusive;
    std::uint32_t queueFamilyIndexCount = 0;
    const std::uint32_t* pQueueFamilyIndices = nullptr;
};

struct VkImageCreateInfo
{
    StructureType sType = StructureType::ImageCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    ImageType imageType = ImageType::Type2D;
    Format format = static_cast<Format>(0);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
    std::uint32_t mipLevels = 1;
    std::uint32_t arrayLayers = 1;
    SampleCount samples = SampleCount::One;
    ImageTiling tiling = ImageTiling::Optimal;
    VkFlags usage = 0;
    SharingMode sharingMode = SharingMode::Exclusive;
    std::uint32_t queueFamilyIndexCount = 0;
    const std::uint32_t* pQueueFamilyIndices = nullptr;
    ImageLayout initialLayout = ImageLayout::Undefined;
};

struct VkComponentMapping
{
    ComponentSwizzle r = ComponentSwizzle::Identity;
    ComponentSwizzle g = ComponentSwizzle::Identity;
    ComponentSwizzle b = ComponentSwizzle::Identity;
    ComponentSwizzle a = ComponentSwizzle::Identity;
};

struct VkImageSubresourceRange
{
    ImageAspect aspectMask = ImageAspect::Color;
    std::uint32_t baseMipLevel = 0;
    std::uint32_t levelCount = 1;
    std::uint32_t baseArrayLayer = 0;
    std::uint32_t layerCount = 1;
};
struct VkImageSubresourceLayers
{
    ImageAspect aspectMask = ImageAspect::Color;
    std::uint32_t mipLevel = 0;
    std::uint32_t baseArrayLayer = 0;
    std::uint32_t layerCount = 1;
};
struct VkOffset3D
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
};
struct VkExtent3D
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
};
struct VkBufferCopy
{
    std::size_t srcOffset = 0;
    std::size_t dstOffset = 0;
    std::size_t size = 0;
};
struct VkBufferImageCopy
{
    std::size_t bufferOffset = 0;
    std::uint32_t bufferRowLength = 0;
    std::uint32_t bufferImageHeight = 0;
    VkImageSubresourceLayers imageSubresource;
    VkOffset3D imageOffset;
    VkExtent3D imageExtent;
};
struct VkImageViewCreateInfo
{
    StructureType sType = StructureType::ImageViewCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    VkImage image = nullptr;
    ImageType viewType = ImageType::Type2D;
    Format format = static_cast<Format>(0);
    VkComponentMapping components;
    VkImageSubresourceRange subresourceRange;
};

struct VkShaderModuleCreateInfo
{
    StructureType sType = StructureType::ShaderModuleCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    std::size_t codeSize = 0;
    const std::uint32_t* pCode = nullptr;
};

struct VkViewport
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct VkOffset2D
{
    std::int32_t x = 0;
    std::int32_t y = 0;
};

struct VkExtent2D
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct VkRect2D
{
    VkOffset2D offset;
    VkExtent2D extent;
};

struct VkPipelineShaderStageCreateInfo
{
    StructureType sType = StructureType::PipelineShaderStageCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    ShaderStage stage = ShaderStage::Vertex;
    VkShaderModule module = nullptr;
    const char* pName = "main";
    const void* pSpecializationInfo = nullptr;
};

struct VkPipelineVertexInputStateCreateInfo
{
    StructureType sType = StructureType::PipelineVertexInputStateCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    std::uint32_t vertexBindingDescriptionCount = 0;
    const void* pVertexBindingDescriptions = nullptr;
    std::uint32_t vertexAttributeDescriptionCount = 0;
    const void* pVertexAttributeDescriptions = nullptr;
};

struct VkVertexInputBindingDescription
{
    std::uint32_t binding = 0;
    std::uint32_t stride = 0;
    VertexInputRate inputRate = VertexInputRate::Vertex;
};

struct VkVertexInputAttributeDescription
{
    std::uint32_t location = 0;
    std::uint32_t binding = 0;
    Format format = static_cast<Format>(0);
    std::uint32_t offset = 0;
};

struct VkPipelineInputAssemblyStateCreateInfo
{
    StructureType sType = StructureType::PipelineInputAssemblyStateCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    VkBool32 primitiveRestartEnable = 0;
};

struct VkPipelineViewportStateCreateInfo
{
    StructureType sType = StructureType::PipelineViewportStateCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    std::uint32_t viewportCount = 1;
    const VkViewport* pViewports = nullptr;
    std::uint32_t scissorCount = 1;
    const VkRect2D* pScissors = nullptr;
};

struct VkPipelineRasterizationStateCreateInfo
{
    StructureType sType = StructureType::PipelineRasterizationStateCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    VkBool32 depthClampEnable = 0;
    VkBool32 rasterizerDiscardEnable = 0;
    PolygonMode polygonMode = PolygonMode::Fill;
    VkFlags cullMode = 0;
    FrontFace frontFace = FrontFace::CounterClockwise;
    VkBool32 depthBiasEnable = 0;
    float depthBiasConstantFactor = 0.0f;
    float depthBiasClamp = 0.0f;
    float depthBiasSlopeFactor = 0.0f;
    float lineWidth = 1.0f;
};

struct VkPipelineMultisampleStateCreateInfo
{
    StructureType sType = StructureType::PipelineMultisampleStateCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    SampleCount rasterizationSamples = SampleCount::One;
    VkBool32 sampleShadingEnable = 0;
    float minSampleShading = 0.0f;
    const VkFlags* pSampleMask = nullptr;
    VkBool32 alphaToCoverageEnable = 0;
    VkBool32 alphaToOneEnable = 0;
};

struct VkStencilOpState
{
    StencilOp failOp = StencilOp::Keep;
    StencilOp passOp = StencilOp::Keep;
    StencilOp depthFailOp = StencilOp::Keep;
    CompareOp compareOp = CompareOp::Always;
    std::uint32_t compareMask = 0;
    std::uint32_t writeMask = 0;
    std::uint32_t reference = 0;
};

struct VkPipelineDepthStencilStateCreateInfo
{
    StructureType sType = StructureType::PipelineDepthStencilStateCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    VkBool32 depthTestEnable = 0;
    VkBool32 depthWriteEnable = 0;
    CompareOp depthCompareOp = CompareOp::Less;
    VkBool32 depthBoundsTestEnable = 0;
    VkBool32 stencilTestEnable = 0;
    VkStencilOpState front;
    VkStencilOpState back;
    float minDepthBounds = 0.0f;
    float maxDepthBounds = 1.0f;
};

struct VkPipelineColorBlendAttachmentState
{
    VkBool32 blendEnable = 0;
    BlendFactor srcColorBlendFactor = BlendFactor::One;
    BlendFactor dstColorBlendFactor = BlendFactor::Zero;
    BlendOp colorBlendOp = BlendOp::Add;
    BlendFactor srcAlphaBlendFactor = BlendFactor::One;
    BlendFactor dstAlphaBlendFactor = BlendFactor::Zero;
    BlendOp alphaBlendOp = BlendOp::Add;
    VkFlags colorWriteMask = 0xFu;
};

struct VkPipelineColorBlendStateCreateInfo
{
    StructureType sType = StructureType::PipelineColorBlendStateCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    VkBool32 logicOpEnable = 0;
    LogicOp logicOp = LogicOp::Clear;
    std::uint32_t attachmentCount = 0;
    const VkPipelineColorBlendAttachmentState* pAttachments = nullptr;
    float blendConstants[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct VkPipelineDynamicStateCreateInfo
{
    StructureType sType = StructureType::PipelineDynamicStateCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    std::uint32_t dynamicStateCount = 0;
    const DynamicState* pDynamicStates = nullptr;
};

struct VkPipelineLayoutCreateInfo
{
    StructureType sType = StructureType::PipelineLayoutCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    std::uint32_t setLayoutCount = 0;
    const VkDescriptorSetLayout* pSetLayouts = nullptr;
    std::uint32_t pushConstantRangeCount = 0;
    const void* pPushConstantRanges = nullptr;
};

struct VkPushConstantRange
{
    ShaderStage stageFlags = ShaderStage::Vertex;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct VkAttachmentDescription
{
    VkFlags flags = 0;
    Format format = static_cast<Format>(0);
    SampleCount samples = SampleCount::One;
    AttachmentLoadOp loadOp = AttachmentLoadOp::Clear;
    AttachmentStoreOp storeOp = AttachmentStoreOp::Store;
    AttachmentLoadOp stencilLoadOp = AttachmentLoadOp::DontCare;
    AttachmentStoreOp stencilStoreOp = AttachmentStoreOp::DontCare;
    ImageLayout initialLayout = ImageLayout::Undefined;
    ImageLayout finalLayout = ImageLayout::Undefined;
};

struct VkAttachmentReference
{
    std::uint32_t attachment = 0;
    ImageLayout layout = ImageLayout::Undefined;
};

struct VkSubpassDescription
{
    VkFlags flags = 0;
    PipelineBindPoint pipelineBindPoint = PipelineBindPoint::Graphics;
    std::uint32_t inputAttachmentCount = 0;
    const VkAttachmentReference* pInputAttachments = nullptr;
    std::uint32_t colorAttachmentCount = 0;
    const VkAttachmentReference* pColorAttachments = nullptr;
    const VkAttachmentReference* pResolveAttachments = nullptr;
    const VkAttachmentReference* pDepthStencilAttachment = nullptr;
    std::uint32_t preserveAttachmentCount = 0;
    const std::uint32_t* pPreserveAttachments = nullptr;
};

struct VkSubpassDependency
{
    std::uint32_t srcSubpass = 0;
    std::uint32_t dstSubpass = 0;
    VkFlags srcStageMask = 0;
    VkFlags dstStageMask = 0;
    VkFlags srcAccessMask = 0;
    VkFlags dstAccessMask = 0;
    VkFlags dependencyFlags = 0;
};

struct VkRenderPassCreateInfo
{
    StructureType sType = StructureType::RenderPassCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    std::uint32_t attachmentCount = 0;
    const VkAttachmentDescription* pAttachments = nullptr;
    std::uint32_t subpassCount = 0;
    const VkSubpassDescription* pSubpasses = nullptr;
    std::uint32_t dependencyCount = 0;
    const VkSubpassDependency* pDependencies = nullptr;
};

struct VkFramebufferCreateInfo
{
    StructureType sType = StructureType::FramebufferCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    VkRenderPass renderPass = nullptr;
    std::uint32_t attachmentCount = 0;
    const VkImageView* pAttachments = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t layers = 1;
};

struct VkCommandPoolCreateInfo
{
    StructureType sType = StructureType::CommandPoolCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    std::uint32_t queueFamilyIndex = 0;
};

struct VkCommandBufferAllocateInfo
{
    StructureType sType = StructureType::CommandBufferAllocateInfo;
    const void* pNext = nullptr;
    VkCommandPool commandPool = nullptr;
    CommandBufferLevel level = CommandBufferLevel::Primary;
    std::uint32_t commandBufferCount = 1;
};

struct VkCommandBufferBeginInfo
{
    StructureType sType = StructureType::CommandBufferBeginInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    const void* pInheritanceInfo = nullptr;
};

struct VkClearDepthStencilValue
{
    float depth = 0.0f;
    std::uint32_t stencil = 0;
};

union VkClearValue
{
    float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    VkClearDepthStencilValue depthStencil;
};

struct VkRenderPassBeginInfo
{
    StructureType sType = StructureType::RenderPassBeginInfo;
    const void* pNext = nullptr;
    VkRenderPass renderPass = nullptr;
    VkFramebuffer framebuffer = nullptr;
    VkRect2D renderArea;
    std::uint32_t clearValueCount = 0;
    const VkClearValue* pClearValues = nullptr;
};

struct VkDescriptorSetLayoutBinding
{
    std::uint32_t binding = 0;
    DescriptorType descriptorType = DescriptorType::UniformBuffer;
    std::uint32_t descriptorCount = 1;
    ShaderStage stageFlags = ShaderStage::Vertex;
    const VkSampler* pImmutableSamplers = nullptr;
};

struct VkDescriptorSetLayoutCreateInfo
{
    StructureType sType = StructureType::DescriptorSetLayoutCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    std::uint32_t bindingCount = 0;
    const VkDescriptorSetLayoutBinding* pBindings = nullptr;
};

struct VkDescriptorPoolSize
{
    DescriptorType type = DescriptorType::UniformBuffer;
    std::uint32_t descriptorCount = 0;
};

struct VkDescriptorPoolCreateInfo
{
    StructureType sType = StructureType::DescriptorPoolCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    std::uint32_t maxSets = 0;
    std::uint32_t poolSizeCount = 0;
    const VkDescriptorPoolSize* pPoolSizes = nullptr;
};

struct VkDescriptorSetAllocateInfo
{
    StructureType sType = StructureType::DescriptorSetAllocateInfo;
    const void* pNext = nullptr;
    VkDescriptorPool descriptorPool = nullptr;
    std::uint32_t descriptorSetCount = 1;
    const VkDescriptorSetLayout* pSetLayouts = nullptr;
};

struct VkDescriptorBufferInfo
{
    VkBuffer buffer = nullptr;
    std::size_t offset = 0;
    std::size_t range = 0;
};

struct VkDescriptorImageInfo
{
    VkSampler sampler = nullptr;
    VkImageView imageView = nullptr;
    ImageLayout imageLayout = ImageLayout::ShaderReadOnlyOptimal;
};

struct VkWriteDescriptorSet
{
    StructureType sType = StructureType::WriteDescriptorSet;
    const void* pNext = nullptr;
    VkDescriptorSet dstSet = nullptr;
    std::uint32_t dstBinding = 0;
    std::uint32_t dstArrayElement = 0;
    std::uint32_t descriptorCount = 1;
    DescriptorType descriptorType = DescriptorType::UniformBuffer;
    const VkDescriptorImageInfo* pImageInfo = nullptr;
    const VkDescriptorBufferInfo* pBufferInfo = nullptr;
    const void* pTexelBufferView = nullptr;
};

struct VkSamplerCreateInfo
{
    StructureType sType = StructureType::SamplerCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    Filter magFilter = Filter::Nearest;
    Filter minFilter = Filter::Nearest;
    SamplerAddressMode addressModeU = SamplerAddressMode::ClampToEdge;
    SamplerAddressMode addressModeV = SamplerAddressMode::ClampToEdge;
    SamplerAddressMode addressModeW = SamplerAddressMode::ClampToEdge;
    float mipLodBias = 0.0f;
    VkBool32 anisotropyEnable = 0;
    float maxAnisotropy = 0.0f;
    VkBool32 compareEnable = 0;
    CompareOp compareOp = CompareOp::Always;
    float minLod = 0.0f;
    float maxLod = 0.0f;
    ColorSpace borderColor = ColorSpace::SrgbNonlinear;
    VkBool32 unnormalizedCoordinates = 0;
};

struct VkImageMemoryBarrier
{
    StructureType sType = StructureType::ImageMemoryBarrier;
    const void* pNext = nullptr;
    VkFlags srcAccessMask = 0;
    VkFlags dstAccessMask = 0;
    ImageLayout oldLayout = ImageLayout::Undefined;
    ImageLayout newLayout = ImageLayout::Undefined;
    std::uint32_t srcQueueFamilyIndex = 0;
    std::uint32_t dstQueueFamilyIndex = 0;
    VkImage image = nullptr;
    VkImageSubresourceRange subresourceRange;
};

struct VkBufferMemoryBarrier
{
    StructureType sType = StructureType::BufferMemoryBarrier;
    const void* pNext = nullptr;
    VkFlags srcAccessMask = 0;
    VkFlags dstAccessMask = 0;
    std::uint32_t srcQueueFamilyIndex = 0;
    std::uint32_t dstQueueFamilyIndex = 0;
    VkBuffer buffer = nullptr;
    std::size_t offset = 0;
    std::size_t size = 0;
};

enum class SurfaceTransform : std::uint32_t
{
    Identity = 0x1u,
};

enum class QueueCapability : std::uint32_t
{
    Graphics = 0x1u,
    Compute = 0x2u,
    Transfer = 0x4u,
};

struct VkPhysicalDeviceLimits
{
    std::uint32_t maxImageDimension1D;
    std::uint32_t maxImageDimension2D;
    std::uint32_t maxImageDimension3D;
    std::uint32_t maxImageDimensionCube;
    std::uint32_t maxImageArrayLayers;
    std::uint32_t maxTexelBufferElements;
    std::uint32_t maxUniformBufferRange;
    std::uint32_t maxStorageBufferRange;
    std::uint32_t maxPushConstantsSize;
    std::uint32_t maxMemoryAllocationCount;
    std::uint32_t maxSamplerAllocationCount;
    std::uint64_t bufferImageGranularity;
    std::uint64_t sparseAddressSpaceSize;
    std::uint32_t maxBoundDescriptorSets;
    std::uint32_t maxPerStageDescriptorSamplers;
    std::uint32_t maxPerStageDescriptorUniformBuffers;
    std::uint32_t maxPerStageDescriptorStorageBuffers;
    std::uint32_t maxPerStageDescriptorSampledImages;
    std::uint32_t maxPerStageDescriptorStorageImages;
    std::uint32_t maxPerStageDescriptorInputAttachments;
    std::uint32_t maxPerStageResources;
    std::uint32_t maxDescriptorSetSamplers;
    std::uint32_t maxDescriptorSetUniformBuffers;
    std::uint32_t maxDescriptorSetUniformBuffersDynamic;
    std::uint32_t maxDescriptorSetStorageBuffers;
    std::uint32_t maxDescriptorSetStorageBuffersDynamic;
    std::uint32_t maxDescriptorSetSampledImages;
    std::uint32_t maxDescriptorSetStorageImages;
    std::uint32_t maxDescriptorSetInputAttachments;
    std::uint32_t maxVertexInputAttributes;
    std::uint32_t maxVertexInputBindings;
    std::uint32_t maxVertexInputAttributeOffset;
    std::uint32_t maxVertexInputBindingStride;
    std::uint32_t maxVertexOutputComponents;
    std::uint32_t maxTessellationGenerationLevel;
    std::uint32_t maxTessellationPatchSize;
    std::uint32_t maxTessellationControlPerVertexInputComponents;
    std::uint32_t maxTessellationControlPerVertexOutputComponents;
    std::uint32_t maxTessellationControlPerPatchOutputComponents;
    std::uint32_t maxTessellationControlTotalOutputComponents;
    std::uint32_t maxTessellationEvaluationInputComponents;
    std::uint32_t maxTessellationEvaluationOutputComponents;
    std::uint32_t maxGeometryShaderInvocations;
    std::uint32_t maxGeometryInputComponents;
    std::uint32_t maxGeometryOutputComponents;
    std::uint32_t maxGeometryOutputVertices;
    std::uint32_t maxGeometryTotalOutputComponents;
    std::uint32_t maxFragmentInputComponents;
    std::uint32_t maxFragmentOutputAttachments;
    std::uint32_t maxFragmentDualSrcAttachments;
    std::uint32_t maxFragmentCombinedOutputResources;
    std::uint32_t maxComputeSharedMemorySize;
    std::uint32_t maxComputeWorkGroupCount[3];
    std::uint32_t maxComputeWorkGroupInvocations;
    std::uint32_t maxComputeWorkGroupSize[3];
    std::uint32_t subPixelPrecisionBits;
    std::uint32_t subTexelPrecisionBits;
    std::uint32_t mipmapPrecisionBits;
    std::uint32_t maxDrawIndexedIndexValue;
    std::uint32_t maxDrawIndirectCount;
    float maxSamplerLodBias;
    float maxSamplerAnisotropy;
    std::uint32_t maxViewports;
    std::uint32_t maxViewportDimensions[2];
    float viewportBoundsRange[2];
    std::uint32_t viewportSubPixelBits;
    std::size_t minMemoryMapAlignment;
    std::uint64_t minTexelBufferOffsetAlignment;
    std::uint64_t minUniformBufferOffsetAlignment;
    std::uint64_t minStorageBufferOffsetAlignment;
    std::int32_t minTexelOffset;
    std::uint32_t maxTexelOffset;
    std::int32_t minTexelGatherOffset;
    std::uint32_t maxTexelGatherOffset;
    float minInterpolationOffset;
    float maxInterpolationOffset;
    std::uint32_t subPixelInterpolationOffsetBits;
    std::uint32_t maxFramebufferWidth;
    std::uint32_t maxFramebufferHeight;
    std::uint32_t maxFramebufferLayers;
    VkFlags framebufferColorSampleCounts;
    VkFlags framebufferDepthSampleCounts;
    VkFlags framebufferStencilSampleCounts;
    VkFlags framebufferNoAttachmentsSampleCounts;
    std::uint32_t maxColorAttachments;
    VkFlags sampledImageColorSampleCounts;
    VkFlags sampledImageIntegerSampleCounts;
    VkFlags sampledImageDepthSampleCounts;
    VkFlags sampledImageStencilSampleCounts;
    VkFlags storageImageSampleCounts;
    std::uint32_t maxSampleMaskWords;
    VkBool32 timestampComputeAndGraphics;
    float timestampPeriod;
    std::uint32_t maxClipDistances;
    std::uint32_t maxCullDistances;
    std::uint32_t maxCombinedClipAndCullDistances;
    std::uint32_t discreteQueuePriorities;
    float pointSizeRange[2];
    float lineWidthRange[2];
    float pointSizeGranularity;
    float lineWidthGranularity;
    VkBool32 strictLines;
    VkBool32 standardSampleLocations;
    std::uint64_t optimalBufferCopyOffsetAlignment;
    std::uint64_t optimalBufferCopyRowPitchAlignment;
    std::uint64_t nonCoherentAtomSize;
};

struct VkPhysicalDeviceFeatures
{
    VkBool32 robustBufferAccess;
    VkBool32 fullDrawIndexUint32;
    VkBool32 imageCubeArray;
    VkBool32 independentBlend;
    VkBool32 geometryShader;
    VkBool32 tessellationShader;
    VkBool32 sampleRateShading;
    VkBool32 dualSrcBlend;
    VkBool32 logicOp;
    VkBool32 multiDrawIndirect;
    VkBool32 drawIndirectFirstInstance;
    VkBool32 depthClamp;
    VkBool32 depthBiasClamp;
    VkBool32 fillModeNonSolid;
    VkBool32 depthBounds;
    VkBool32 wideLines;
    VkBool32 largePoints;
    VkBool32 alphaToOne;
    VkBool32 multiViewport;
    VkBool32 samplerAnisotropy;
    VkBool32 textureCompressionETC2;
    VkBool32 textureCompressionASTC_LDR;
    VkBool32 textureCompressionBC;
    VkBool32 occlusionQueryPrecise;
    VkBool32 pipelineStatisticsQuery;
    VkBool32 vertexPipelineStoresAndAtomics;
    VkBool32 fragmentStoresAndAtomics;
    VkBool32 shaderTessellationAndGeometryPointSize;
    VkBool32 shaderImageGatherExtended;
    VkBool32 shaderStorageImageExtendedFormats;
    VkBool32 shaderStorageImageMultisample;
    VkBool32 shaderStorageImageReadWithoutFormat;
    VkBool32 shaderStorageImageWriteWithoutFormat;
    VkBool32 shaderUniformBufferArrayDynamicIndexing;
    VkBool32 shaderSampledImageArrayDynamicIndexing;
    VkBool32 shaderStorageBufferArrayDynamicIndexing;
    VkBool32 shaderStorageImageArrayDynamicIndexing;
    VkBool32 shaderClipDistance;
    VkBool32 shaderCullDistance;
    VkBool32 shaderFloat64;
    VkBool32 shaderInt64;
    VkBool32 shaderInt16;
    VkBool32 shaderResourceResidency;
    VkBool32 shaderResourceMinLod;
    VkBool32 sparseBinding;
    VkBool32 sparseResidencyBuffer;
    VkBool32 sparseResidencyImage2D;
    VkBool32 sparseResidencyImage3D;
    VkBool32 sparseResidency2Samples;
    VkBool32 sparseResidency4Samples;
    VkBool32 sparseResidency8Samples;
    VkBool32 sparseResidency16Samples;
    VkBool32 sparseResidencyAliased;
    VkBool32 variableMultisampleRate;
    VkBool32 inheritedQueries;
};

struct VkPhysicalDeviceSparseProperties
{
    VkBool32 residencyStandard2DBlockShape;
    VkBool32 residencyStandard2DMultisampleBlockShape;
    VkBool32 residencyStandard3DBlockShape;
    VkBool32 residencyAlignedMipSize;
    VkBool32 residencyNonResidentStrict;
};

struct VkPhysicalDeviceProperties
{
    std::uint32_t apiVersion;
    std::uint32_t driverVersion;
    std::uint32_t vendorID;
    std::uint32_t deviceID;
    std::uint32_t deviceType;
    char deviceName[256];
    std::uint8_t pipelineCacheUUID[16];
    VkPhysicalDeviceLimits limits;
    VkPhysicalDeviceSparseProperties sparseProperties;
};

struct VkQueueFamilyProperties
{
    VkFlags queueFlags;
    std::uint32_t queueCount;
    std::uint32_t timestampValidBits;
    VkExtent2D minImageTransferGranularity;
};

struct VkMemoryType
{
    VkFlags propertyFlags;
    std::uint32_t heapIndex;
};

struct VkMemoryHeap
{
    std::uint64_t size;
    VkFlags flags;
};

struct VkPhysicalDeviceMemoryProperties
{
    std::uint32_t memoryTypeCount;
    VkMemoryType memoryTypes[32];
    std::uint32_t memoryHeapCount;
    VkMemoryHeap memoryHeaps[16];
};

struct VkMemoryRequirements
{
    std::uint64_t size;
    std::uint64_t alignment;
    std::uint32_t memoryTypeBits;
};

struct VkExtensionProperties
{
    char extensionName[256];
    std::uint32_t specVersion;
};

struct VkSurfaceCapabilitiesKHR
{
    std::uint32_t minImageCount;
    std::uint32_t maxImageCount;
    VkExtent2D currentExtent;
    VkExtent2D minImageExtent;
    VkExtent2D maxImageExtent;
    std::uint32_t maxImageArrayLayers;
    VkFlags supportedTransforms;
    SurfaceTransform currentTransform;
    VkFlags supportedCompositeAlpha;
    VkFlags supportedUsageFlags;
};

struct VkSurfaceFormatKHR
{
    Format format;
    ColorSpace colorSpace;
};

struct VkSwapchainCreateInfoKHR
{
    StructureType sType = StructureType::SwapchainCreateInfoKHR;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    VkSurfaceKHR surface = nullptr;
    std::uint32_t minImageCount = 0;
    Format imageFormat = static_cast<Format>(0);
    ColorSpace imageColorSpace = ColorSpace::SrgbNonlinear;
    VkExtent2D imageExtent;
    std::uint32_t imageArrayLayers = 1;
    VkFlags imageUsage = 0;
    SharingMode imageSharingMode = SharingMode::Exclusive;
    std::uint32_t queueFamilyIndexCount = 0;
    const std::uint32_t* pQueueFamilyIndices = nullptr;
    SurfaceTransform preTransform = SurfaceTransform::Identity;
    CompositeAlpha compositeAlpha = CompositeAlpha::Opaque;
    PresentMode presentMode = PresentMode::Fifo;
    VkBool32 clipped = 0;
    VkSwapchainKHR oldSwapchain = nullptr;
};

struct VkPresentInfoKHR
{
    StructureType sType = StructureType::PresentInfoKHR;
    const void* pNext = nullptr;
    std::uint32_t waitSemaphoreCount = 0;
    const VkSemaphore* pWaitSemaphores = nullptr;
    std::uint32_t swapchainCount = 0;
    const VkSwapchainKHR* pSwapchains = nullptr;
    const std::uint32_t* pImageIndices = nullptr;
    Result* pResults = nullptr;
};

struct VkPipelineTessellationStateCreateInfo
{
    StructureType sType = StructureType::PipelineTessellationStateCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    std::uint32_t patchControlPoints = 0;
};

struct VkGraphicsPipelineCreateInfo
{
    StructureType sType = StructureType::GraphicsPipelineCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    std::uint32_t stageCount = 0;
    const VkPipelineShaderStageCreateInfo* pStages = nullptr;
    const VkPipelineVertexInputStateCreateInfo* pVertexInputState = nullptr;
    const VkPipelineInputAssemblyStateCreateInfo* pInputAssemblyState =
        nullptr;
    const VkPipelineTessellationStateCreateInfo* pTessellationState = nullptr;
    const VkPipelineViewportStateCreateInfo* pViewportState = nullptr;
    const VkPipelineRasterizationStateCreateInfo* pRasterizationState =
        nullptr;
    const VkPipelineMultisampleStateCreateInfo* pMultisampleState = nullptr;
    const VkPipelineDepthStencilStateCreateInfo* pDepthStencilState = nullptr;
    const VkPipelineColorBlendStateCreateInfo* pColorBlendState = nullptr;
    const VkPipelineDynamicStateCreateInfo* pDynamicState = nullptr;
    VkPipelineLayout layout = nullptr;
    VkRenderPass renderPass = nullptr;
    std::uint32_t subpass = 0;
    VkPipeline basePipelineHandle = nullptr;
    std::int32_t basePipelineIndex = 0;
};

struct VkPipelineCacheCreateInfo
{
    StructureType sType = StructureType::PipelineCacheCreateInfo;
    const void* pNext = nullptr;
    VkFlags flags = 0;
    std::size_t initialDataSize = 0;
    const void* pInitialData = nullptr;
};

struct VkMemoryBarrier
{
    StructureType sType = StructureType::MemoryBarrier;
    const void* pNext = nullptr;
    VkFlags srcAccessMask = 0;
    VkFlags dstAccessMask = 0;
};

// -- Function-pointer signatures (the published API's symbols) --------------

// The loader resolves through a resolve callback (the backend passes the
// SDL_Vulkan_GetInstanceProcAddress / vkGetInstanceProcAddr pair in).
using FnGetInstanceProcAddr = void* (*)(VkInstance, const char*);
using FnGetDeviceProcAddr = void* (*)(VkDevice, const char*);

using FnEnumerateInstanceExtensionProperties =
    Result (*)(const char*, std::uint32_t*, VkExtensionProperties*);
using FnCreateInstance = Result (*)(const VkInstanceCreateInfo*, const void*,
                                    VkInstance*);
using FnDestroyInstance = void (*)(VkInstance, const void*);
using FnEnumeratePhysicalDevices = Result (*)(VkInstance, std::uint32_t*,
                                              VkPhysicalDevice*);
using FnGetPhysicalDeviceProperties = void (*)(VkPhysicalDevice, void*);
using FnGetPhysicalDeviceFeatures = void (*)(VkPhysicalDevice, void*);
using FnGetPhysicalDeviceMemoryProperties = void (*)(VkPhysicalDevice, void*);
using FnGetPhysicalDeviceQueueFamilyProperties =
    void (*)(VkPhysicalDevice, std::uint32_t*, void*);
using FnEnumerateDeviceExtensionProperties = Result (*)(VkPhysicalDevice,
                                                        const char*,
                                                        std::uint32_t*, void*);
using FnCreateDevice = Result (*)(VkPhysicalDevice, const VkDeviceCreateInfo*,
                                  const void*, VkDevice*);
using FnDestroyDevice = void (*)(VkDevice, const void*);
using FnGetDeviceQueue = void (*)(VkDevice, std::uint32_t, std::uint32_t,
                                  VkQueue*);
using FnGetDeviceProcAddr = FnGetDeviceProcAddr;

using FnCreateBuffer = Result (*)(VkDevice, const VkBufferCreateInfo*,
                                  const void*, VkBuffer*);
using FnDestroyBuffer = void (*)(VkDevice, VkBuffer, const void*);
using FnAllocateMemory = Result (*)(VkDevice, const VkMemoryAllocateInfo*,
                                    const void*, VkDeviceMemory*);
using FnFreeMemory = void (*)(VkDevice, VkDeviceMemory, const void*);
using FnGetBufferMemoryRequirements = void (*)(VkDevice, VkBuffer, void*);
using FnGetImageMemoryRequirements = void (*)(VkDevice, VkImage, void*);
using FnBindBufferMemory = Result (*)(VkDevice, VkBuffer, VkDeviceMemory,
                                      std::size_t);
using FnBindImageMemory = Result (*)(VkDevice, VkImage, VkDeviceMemory,
                                     std::size_t);
using FnMapMemory = Result (*)(VkDevice, VkDeviceMemory, std::size_t,
                               std::size_t, VkFlags, void**);
using FnUnmapMemory = void (*)(VkDevice, VkDeviceMemory);
using FnFlushMappedMemoryRanges = Result (*)(VkDevice, std::uint32_t,
                                             const VkMappedMemoryRange*);
using FnCreateImage = Result (*)(VkDevice, const VkImageCreateInfo*,
                                 const void*, VkImage*);
using FnDestroyImage = void (*)(VkDevice, VkImage, const void*);
using FnCreateImageView = Result (*)(VkDevice, const VkImageViewCreateInfo*,
                                     const void*, VkImageView*);
using FnDestroyImageView = void (*)(VkDevice, VkImageView, const void*);
using FnCreateShaderModule = Result (*)(VkDevice,
                                        const VkShaderModuleCreateInfo*,
                                        const void*, VkShaderModule*);
using FnDestroyShaderModule = void (*)(VkDevice, VkShaderModule, const void*);
using FnCreateCommandPool = Result (*)(VkDevice,
                                       const VkCommandPoolCreateInfo*,
                                       const void*, VkCommandPool*);
using FnDestroyCommandPool = void (*)(VkDevice, VkCommandPool, const void*);
using FnAllocateCommandBuffers = Result (*)(VkDevice,
                                            const VkCommandBufferAllocateInfo*,
                                            VkCommandBuffer*);
using FnFreeCommandBuffers = void (*)(VkDevice, VkCommandPool,
                                      std::uint32_t,
                                      const VkCommandBuffer*);
using FnCreateFence = Result (*)(VkDevice, const VkFenceCreateInfo*,
                                 const void*, VkFence*);
using FnDestroyFence = void (*)(VkDevice, VkFence, const void*);
using FnCreateSemaphore = Result (*)(VkDevice, const VkSemaphoreCreateInfo*,
                                     const void*, VkSemaphore*);
using FnDestroySemaphore = void (*)(VkDevice, VkSemaphore, const void*);
using FnWaitForFences = Result (*)(VkDevice, std::uint32_t,
                                   const VkFence*, VkBool32, std::uint64_t);
using FnResetFences = Result (*)(VkDevice, std::uint32_t, const VkFence*);
using FnBeginCommandBuffer = Result (*)(VkCommandBuffer,
                                        const VkCommandBufferBeginInfo*);
using FnEndCommandBuffer = Result (*)(VkCommandBuffer);
using FnResetCommandBuffer = Result (*)(VkCommandBuffer, VkFlags);
using FnQueueSubmit = Result (*)(VkQueue, std::uint32_t,
                                 const VkSubmitInfo*, VkFence);
using FnQueueWaitIdle = Result (*)(VkQueue);
using FnDeviceWaitIdle = Result (*)(VkDevice);
using FnCmdBeginRenderPass = void (*)(VkCommandBuffer,
                                      const VkRenderPassBeginInfo*,
                                      /* SubpassContents */ VkFlags);
using FnCmdEndRenderPass = void (*)(VkCommandBuffer);
using FnCmdSetViewport = void (*)(VkCommandBuffer, std::uint32_t,
                                  std::uint32_t, const VkViewport*);
using FnCmdSetScissor = void (*)(VkCommandBuffer, std::uint32_t,
                                 std::uint32_t, const VkRect2D*);
using FnCmdDrawIndexed = void (*)(VkCommandBuffer, std::uint32_t,
                                  std::uint32_t, std::uint32_t,
                                  std::int32_t, std::uint32_t);
using FnCmdDraw = void (*)(VkCommandBuffer, std::uint32_t, std::uint32_t,
                           std::uint32_t, std::uint32_t);
using FnCmdBindPipeline = void (*)(VkCommandBuffer, PipelineBindPoint,
                                   VkPipeline);
using FnCmdBindVertexBuffers = void (*)(VkCommandBuffer, std::uint32_t,
                                        std::uint32_t,
                                        const VkBuffer*,
                                        const std::size_t*);
using FnCmdBindIndexBuffer = void (*)(VkCommandBuffer, VkBuffer,
                                      std::size_t, IndexType);
using FnCmdBindDescriptorSets = void (*)(VkCommandBuffer, PipelineBindPoint,
                                         VkPipelineLayout, std::uint32_t,
                                         std::uint32_t,
                                         const VkDescriptorSet*,
                                         std::uint32_t,
                                         const std::uint32_t*);
using FnCmdPushConstants = void (*)(VkCommandBuffer, VkPipelineLayout,
                                    ShaderStage, std::uint32_t,
                                    std::uint32_t, const void*);
using FnCmdPipelineBarrier = void (*)(VkCommandBuffer,
                                      PipelineStage, PipelineStage,
                                      VkFlags, std::uint32_t,
                                      const void*, std::uint32_t,
                                      const VkBufferMemoryBarrier*,
                                      std::uint32_t,
                                      const VkImageMemoryBarrier*);
using FnCmdCopyBuffer = void (*)(VkCommandBuffer, VkBuffer, VkBuffer,
                                 std::uint32_t, const void*);
using FnCmdCopyBufferToImage = void (*)(VkCommandBuffer, VkBuffer, VkImage,
                                        ImageLayout, std::uint32_t,
                                        const void*);
using FnCmdCopyImage = void (*)(VkCommandBuffer, VkImage, ImageLayout,
                                VkImage, ImageLayout, std::uint32_t,
                                const void*);
using FnCmdCopyImageToBuffer = void (*)(VkCommandBuffer, VkImage,
                                        ImageLayout, VkBuffer,
                                        std::uint32_t, const void*);
using FnCmdClearAttachments = void (*)(VkCommandBuffer, std::uint32_t,
                                       const void*, std::uint32_t,
                                       const VkRect2D*);
using FnCmdClearDepthStencilImage = void (*)(VkCommandBuffer, VkImage,
                                             ImageLayout,
                                             const VkClearDepthStencilValue*,
                                             std::uint32_t,
                                             const VkImageSubresourceRange*);
using FnCmdFillBuffer = void (*)(VkCommandBuffer, VkBuffer, std::size_t,
                                 std::size_t, std::uint32_t);
using FnCreateRenderPass = Result (*)(VkDevice,
                                      const VkRenderPassCreateInfo*,
                                      const void*, VkRenderPass*);
using FnDestroyRenderPass = void (*)(VkDevice, VkRenderPass, const void*);
using FnCreateFramebuffer = Result (*)(VkDevice,
                                       const VkFramebufferCreateInfo*,
                                       const void*, VkFramebuffer*);
using FnDestroyFramebuffer = void (*)(VkDevice, VkFramebuffer, const void*);
using FnCreatePipelineLayout = Result (*)(VkDevice,
                                          const VkPipelineLayoutCreateInfo*,
                                          const void*, VkPipelineLayout*);
using FnDestroyPipelineLayout = void (*)(VkDevice, VkPipelineLayout,
                                         const void*);
using FnCreateGraphicsPipelines = Result (*)(VkDevice,
                                             VkPipelineCache,
                                             std::uint32_t,
                                             const void*,
                                             const void*,
                                             VkPipeline*);
using FnDestroyPipeline = void (*)(VkDevice, VkPipeline, const void*);
using FnCreateDescriptorSetLayout =
    Result (*)(VkDevice, const VkDescriptorSetLayoutCreateInfo*,
    const void*, VkDescriptorSetLayout*);
using FnDestroyDescriptorSetLayout = void (*)(VkDevice,
                                              VkDescriptorSetLayout,
                                              const void*);
using FnCreateDescriptorPool = Result (*)(VkDevice,
                                          const VkDescriptorPoolCreateInfo*,
                                          const void*, VkDescriptorPool*);
using FnDestroyDescriptorPool = void (*)(VkDevice, VkDescriptorPool,
                                         const void*);
using FnAllocateDescriptorSets = Result (*)(VkDevice,
                                            const VkDescriptorSetAllocateInfo*,
                                            VkDescriptorSet*);
using FnResetDescriptorPool = Result (*)(VkDevice, VkDescriptorPool,
                                         VkFlags);
using FnUpdateDescriptorSets = void (*)(VkDevice, std::uint32_t,
                                        const VkWriteDescriptorSet*,
                                        std::uint32_t, const void*);
using FnCreateSampler = Result (*)(VkDevice, const VkSamplerCreateInfo*,
                                   const void*, VkSampler*);
using FnDestroySampler = void (*)(VkDevice, VkSampler, const void*);
using FnCreateSurfaceKHR = Result (*)(VkInstance, const void*,
                                      const void*, VkSurfaceKHR*);
using FnDestroySurfaceKHR = void (*)(VkInstance, VkSurfaceKHR,
                                     const void*);
using FnGetPhysicalDeviceSurfaceSupportKHR = Result (*)(VkPhysicalDevice,
                                                        std::uint32_t,
                                                        VkSurfaceKHR,
                                                        VkBool32*);
using FnGetPhysicalDeviceSurfaceCapabilitiesKHR =
    Result (*)(VkPhysicalDevice, VkSurfaceKHR,
    VkSurfaceCapabilitiesKHR*);
using FnGetPhysicalDeviceSurfaceFormatsKHR = Result (*)(VkPhysicalDevice,
                                                        VkSurfaceKHR,
                                                        std::uint32_t*,
                                                        VkSurfaceFormatKHR*);
using FnCreateSwapchainKHR = Result (*)(VkDevice,
                                        const VkSwapchainCreateInfoKHR*,
                                        const void*, VkSwapchainKHR*);
using FnDestroySwapchainKHR = void (*)(VkDevice, VkSwapchainKHR,
                                       const void*);
using FnGetSwapchainImagesKHR = Result (*)(VkDevice, VkSwapchainKHR,
                                           std::uint32_t*, VkImage*);
using FnAcquireNextImageKHR = Result (*)(VkDevice, VkSwapchainKHR,
                                         std::uint64_t, VkSemaphore,
                                         VkFence, std::uint32_t*);
using FnQueuePresentKHR = Result (*)(VkQueue, const VkPresentInfoKHR*);
using FnAllocateCommandBuffer = Result (*)(VkDevice,
                                           const VkCommandBufferAllocateInfo*,
                                           VkCommandBuffer*);

/// Minimal runtime loader for the Vulkan entry points the engine uses.
///
/// The backend resolves symbols through a resolve callback (the backend passes
/// the SDL_Vulkan_GetInstanceProcAddress / vkGetInstanceProcAddr pair in), so
/// this header needs no SDL. Every pointer is checked at load time: the first
/// symbol that cannot be resolved is reported by name.
class VkLoader
{
public:
    /// Matches SDL_Vulkan_GetInstanceProcAddress on Linux x86_64: the
    /// loader-global entry points are resolved through it, and the device
    /// ones through vkGetDeviceProcAddr, which is itself resolved here.
    using ResolveFn = void* (*)(const char*);

    /// Resolves the loader-global entry points (the ones callable before an
    /// instance exists) from the given callback. Returns false and sets
    /// missingSymbol to the first name that could not be resolved.
    bool load(ResolveFn resolve, std::string& missingSymbol);

    /// Resolves every remaining instance-level entry point through a new
    /// callback bound after vkCreateInstance succeeded. Instance-bound entry
    /// points must come from the instance's dispatch, so they can never be
    /// resolved against a null instance handle.
    bool loadInstance(ResolveFn resolve, VkInstance instance,
                      std::string& missingSymbol);

    /// Resolves every device-level entry point by calling the previously
    /// loaded vkGetDeviceProcAddr on this device directly. Returns false and
    /// sets missingSymbol to the first name that could not be resolved.
    bool loadDevice(VkDevice device, std::string& missingSymbol);

    FnGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    FnEnumerateInstanceExtensionProperties
        vkEnumerateInstanceExtensionProperties = nullptr;
    FnCreateInstance vkCreateInstance = nullptr;
    FnDestroyInstance vkDestroyInstance = nullptr;
    FnEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
    FnGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
    FnGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures = nullptr;
    FnGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties =
        nullptr;
    FnGetPhysicalDeviceQueueFamilyProperties
        vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
    FnEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties =
        nullptr;
    FnCreateDevice vkCreateDevice = nullptr;
    FnDestroyDevice vkDestroyDevice = nullptr;
    FnGetDeviceQueue vkGetDeviceQueue = nullptr;
    FnGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;

    FnCreateBuffer vkCreateBuffer = nullptr;
    FnDestroyBuffer vkDestroyBuffer = nullptr;
    FnAllocateMemory vkAllocateMemory = nullptr;
    FnFreeMemory vkFreeMemory = nullptr;
    FnGetBufferMemoryRequirements vkGetBufferMemoryRequirements = nullptr;
    FnGetImageMemoryRequirements vkGetImageMemoryRequirements = nullptr;
    FnBindBufferMemory vkBindBufferMemory = nullptr;
    FnBindImageMemory vkBindImageMemory = nullptr;
    FnMapMemory vkMapMemory = nullptr;
    FnUnmapMemory vkUnmapMemory = nullptr;
    FnFlushMappedMemoryRanges vkFlushMappedMemoryRanges = nullptr;
    FnCreateImage vkCreateImage = nullptr;
    FnDestroyImage vkDestroyImage = nullptr;
    FnCreateImageView vkCreateImageView = nullptr;
    FnDestroyImageView vkDestroyImageView = nullptr;
    FnCreateShaderModule vkCreateShaderModule = nullptr;
    FnDestroyShaderModule vkDestroyShaderModule = nullptr;
    FnCreateCommandPool vkCreateCommandPool = nullptr;
    FnDestroyCommandPool vkDestroyCommandPool = nullptr;
    FnAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
    FnFreeCommandBuffers vkFreeCommandBuffers = nullptr;
    FnCreateFence vkCreateFence = nullptr;
    FnDestroyFence vkDestroyFence = nullptr;
    FnCreateSemaphore vkCreateSemaphore = nullptr;
    FnDestroySemaphore vkDestroySemaphore = nullptr;
    FnWaitForFences vkWaitForFences = nullptr;
    FnResetFences vkResetFences = nullptr;
    FnBeginCommandBuffer vkBeginCommandBuffer = nullptr;
    FnEndCommandBuffer vkEndCommandBuffer = nullptr;
    FnResetCommandBuffer vkResetCommandBuffer = nullptr;
    FnQueueSubmit vkQueueSubmit = nullptr;
    FnQueueWaitIdle vkQueueWaitIdle = nullptr;
    FnDeviceWaitIdle vkDeviceWaitIdle = nullptr;
    FnCmdBeginRenderPass vkCmdBeginRenderPass = nullptr;
    FnCmdEndRenderPass vkCmdEndRenderPass = nullptr;
    FnCmdSetViewport vkCmdSetViewport = nullptr;
    FnCmdSetScissor vkCmdSetScissor = nullptr;
    FnCmdDrawIndexed vkCmdDrawIndexed = nullptr;
    FnCmdDraw vkCmdDraw = nullptr;
    FnCmdBindPipeline vkCmdBindPipeline = nullptr;
    FnCmdBindVertexBuffers vkCmdBindVertexBuffers = nullptr;
    FnCmdBindIndexBuffer vkCmdBindIndexBuffer = nullptr;
    FnCmdBindDescriptorSets vkCmdBindDescriptorSets = nullptr;
    FnCmdPushConstants vkCmdPushConstants = nullptr;
    FnCmdPipelineBarrier vkCmdPipelineBarrier = nullptr;
    FnCmdCopyBuffer vkCmdCopyBuffer = nullptr;
    FnCmdCopyBufferToImage vkCmdCopyBufferToImage = nullptr;
    FnCmdCopyImage vkCmdCopyImage = nullptr;
    FnCmdCopyImageToBuffer vkCmdCopyImageToBuffer = nullptr;
    FnCmdClearAttachments vkCmdClearAttachments = nullptr;
    FnCmdClearDepthStencilImage vkCmdClearDepthStencilImage = nullptr;
    FnCmdFillBuffer vkCmdFillBuffer = nullptr;
    FnCreateRenderPass vkCreateRenderPass = nullptr;
    FnDestroyRenderPass vkDestroyRenderPass = nullptr;
    FnCreateFramebuffer vkCreateFramebuffer = nullptr;
    FnDestroyFramebuffer vkDestroyFramebuffer = nullptr;
    FnCreatePipelineLayout vkCreatePipelineLayout = nullptr;
    FnDestroyPipelineLayout vkDestroyPipelineLayout = nullptr;
    FnCreateGraphicsPipelines vkCreateGraphicsPipelines = nullptr;
    FnDestroyPipeline vkDestroyPipeline = nullptr;
    FnCreateDescriptorSetLayout vkCreateDescriptorSetLayout = nullptr;
    FnDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout = nullptr;
    FnCreateDescriptorPool vkCreateDescriptorPool = nullptr;
    FnDestroyDescriptorPool vkDestroyDescriptorPool = nullptr;
    FnAllocateDescriptorSets vkAllocateDescriptorSets = nullptr;
    FnResetDescriptorPool vkResetDescriptorPool = nullptr;
    FnUpdateDescriptorSets vkUpdateDescriptorSets = nullptr;
    FnCreateSampler vkCreateSampler = nullptr;
    FnDestroySampler vkDestroySampler = nullptr;
    FnCreateSurfaceKHR vkCreateSurfaceKHR = nullptr;
    FnDestroySurfaceKHR vkDestroySurfaceKHR = nullptr;
    FnGetPhysicalDeviceSurfaceSupportKHR
        vkGetPhysicalDeviceSurfaceSupportKHR = nullptr;
    FnGetPhysicalDeviceSurfaceCapabilitiesKHR
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
    FnGetPhysicalDeviceSurfaceFormatsKHR
        vkGetPhysicalDeviceSurfaceFormatsKHR = nullptr;
    FnCreateSwapchainKHR vkCreateSwapchainKHR = nullptr;
    FnDestroySwapchainKHR vkDestroySwapchainKHR = nullptr;
    FnGetSwapchainImagesKHR vkGetSwapchainImagesKHR = nullptr;
    FnAcquireNextImageKHR vkAcquireNextImageKHR = nullptr;
    FnQueuePresentKHR vkQueuePresentKHR = nullptr;
};

}  // namespace vulkan
}  // namespace grace::render
