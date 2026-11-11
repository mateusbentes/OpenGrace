#pragma once

// The Metal render module's own runtime loader. RULE (the same rule the
// Vulkan and the OpenGL loaders record): every constant, structure and
// function signature the engine uses is declared here, so no Apple SDK
// header and no third-party framework join the tree, and the
// platform-neutral contract never includes it.
//
// Metal has no C entry points to resolve: it is an Objective-C framework.
// The loader therefore declares the Objective-C runtime's own C entry
// points (objc_getClass, sel_registerName, objc_msgSend and friends) and
// talks to the framework through them, with every Metal class handle an
// opaque pointer and every call a runtime message send. The enum values
// are the published API's values (the M19 lesson: the values the device
// receives must be the API's own, never a local guess).
//
// On a non-Apple platform the loader still compiles (the declarations are
// plain C), but the runtime entry points resolve to nothing and load()
// reports that cleanly: the mapping unit tests run on Linux, the hardware
// path needs a Mac (milestone 20's recorded scope).
#include <cstddef>
#include <cstdint>
#include <string>

namespace grace {
namespace render {
namespace metal {

// NSUInteger is the framework's own index/size/count type: 64-bit on
// every architecture Metal supports.
using NSUInteger = std::uint64_t;

// Metal handles are opaque Objective-C objects. Every handle is a plain
// pointer; the runtime never dereferences one through this header.
using MTLDeviceHandle = void*;
using MTLCommandQueueHandle = void*;
using MTLCommandBufferHandle = void*;
using MTLRenderCommandEncoderHandle = void*;
using MTLLibraryHandle = void*;
using MTLFunctionHandle = void*;
using MTLRenderPipelineStateHandle = void*;
using MTLDepthStencilStateHandle = void*;
using MTLBufferHandle = void*;
using MTLTextureHandle = void*;
using MTLSamplerStateHandle = void*;

// The presentation layer's handles (QuartzCore, the same framework the
// CAMetalLayer class lives in).
using CAMetalLayerHandle = void*;
using NSObjectHandle = void*;

// --- Published enum values (factual sources; NSUInteger-sized) -----------

enum class MTLPixelFormat : NSUInteger
{
    Invalid = 0,
    R8Unorm = 10,
    Rgba8Unorm = 70,
    Bgra8Unorm = 80,
    Rgba16Float = 115,
    Depth32Float = 252,
    Stencil8 = 253,
    Depth24Unorm_Stencil8 = 255,       // macOS only
    Depth32Float_Stencil8 = 260,       // macOS 10.11+, iOS 9.0+
};

enum class MTLCompareFunction : NSUInteger
{
    Never = 0,
    Less = 1,
    Equal = 2,
    LessEqual = 3,
    Greater = 4,
    NotEqual = 5,
    GreaterEqual = 6,
    Always = 7,
};

enum class MTLCullMode : NSUInteger
{
    None = 0,
    Front = 1,
    Back = 2,
};

enum class MTLWinding : NSUInteger
{
    Clockwise = 0,
    CounterClockwise = 1,
};

enum class MTLBlendFactor : NSUInteger
{
    Zero = 0,
    One = 1,
    SourceColor = 2,
    OneMinusSourceColor = 3,
    SourceAlpha = 4,
    OneMinusSourceAlpha = 5,
    DestinationColor = 6,
    OneMinusDestinationColor = 7,
    DestinationAlpha = 8,
    OneMinusDestinationAlpha = 9,
    SourceAlphaSaturated = 10,
    BlendColor = 11,
    OneMinusBlendColor = 12,
    BlendAlpha = 13,
    OneMinusBlendAlpha = 14,
};

enum class MTLBlendOperation : NSUInteger
{
    Add = 0,
    Subtract = 1,
    ReverseSubtract = 2,
    Min = 3,
    Max = 4,
};

enum class MTLLoadAction : NSUInteger
{
    DontCare = 0,
    Load = 1,
    Clear = 2,
};

enum class MTLStoreAction : NSUInteger
{
    DontCare = 0,
    Store = 1,
};

enum class MTLSamplerMinMagFilter : NSUInteger
{
    Nearest = 0,
    Linear = 1,
};

enum class MTLSamplerMipFilter : NSUInteger
{
    NotMipmapped = 0,
    Nearest = 1,
    Linear = 2,
};

enum class MTLSamplerAddressMode : NSUInteger
{
    ClampToEdge = 0,
    MirrorClampToEdge = 1,
    Repeat = 2,
    MirrorRepeat = 3,
};

enum class MTLPrimitiveType : NSUInteger
{
    Point = 0,
    Line = 1,
    LineStrip = 2,
    Triangle = 3,
    TriangleStrip = 4,
};

enum class MTLIndexType : NSUInteger
{
    UInt16 = 0,
    UInt32 = 1,
};

enum class MTLVertexFormat : NSUInteger
{
    Invalid = 0,
    Float2 = 29,
    Float3 = 30,
    Float4 = 31,
};

enum class MTLVertexStepFunction : NSUInteger
{
    Constant = 0,
    PerVertex = 1,
    PerInstance = 2,
};

enum class MTLTextureType : NSUInteger
{
    Type2D = 1,
    Type3D = 5,
    Type2DArray = 6,
};

// The storage mode the backend's own resources use. The published
// values: 0 Managed (macOS), 1 Private, 2 Shared, 3 Memoryless.
enum class MTLStorageMode : NSUInteger
{
    Managed = 0,
    Private = 1,
    Shared = 2,
    Memoryless = 3,
};

enum class MTLResourceUsage : NSUInteger
{
    Read = 1,
    Write = 2,
};

enum class MTLResourceOptions : NSUInteger
{
    // The published allocation mode bits (storage mode at bits 0-3, the
    // cache mode at bits 4-7). The backend builds the value from a
    // storage mode plus the default cache mode (0 = Default cache).
    Stage = static_cast<NSUInteger>(MTLStorageMode::Shared),
    GpuPrivate = static_cast<NSUInteger>(MTLStorageMode::Private),
};

// The published texture usage bits.
enum class MTLTextureUsage : NSUInteger
{
    Unknown = 0,
    ShaderRead = 1,
    ShaderWrite = 2,
    RenderTarget = 4,
};

// The published feature flags the device reports.
enum class MTLDeviceFeature : NSUInteger
{
    // The published family flag bit (MTLGPUFamilyMac2 = 1002 lives in a
    // different enumeration; this project only needs the base feature
    // set's existence, not a family gate).
    None = 0,
};

// --- Fixed-size structures (the published C layouts) ---------------------

// The published struct: four 64-bit floats. Clear colours are doubles
// on the Metal ABI (the Vulkan clear value's floats are not).
struct MTLClearColor
{
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double alpha = 0.0;
};

// The published struct: six 64-bit doubles (origin, size, depthRange).
struct MTLViewport
{
    double originX = 0.0;
    double originY = 0.0;
    double width = 0.0;
    double height = 0.0;
    double znear = 0.0;
    double zfar = 0.0;
};

struct MTLOrigin
{
    NSUInteger x = 0;
    NSUInteger y = 0;
    NSUInteger z = 0;
};

struct MTLSize
{
    NSUInteger width = 0;
    NSUInteger height = 0;
    NSUInteger depth = 0;
};

struct MTLRegion
{
    MTLOrigin origin;
    MTLSize size;
};

// The published vertex descriptor structures. The layout is the
// published one: an array of 31 attribute descriptors (each: format
// NSUInteger, offset NSUInteger, bufferIndex NSUInteger) followed by an
// array of 31 layout descriptors (each: stride NSUInteger,
// stepFunction NSUInteger, stepRate NSUInteger). The backend declares
// one descriptor object per pipeline through the runtime's alloc/init,
// so the fixed-size mirror structs below are only what the setter
// messages take and return — the framework's own object carries the
// fields, the engine never lays one out flat.

// --- Objective-C runtime declarations (the platform's public runtime
// ABI, declared here so no Apple SDK header is required) ------------------

// The runtime's C entry points. On Apple platforms the symbols live in
// libobjc.dylib (loaded as part of the system, and by dlopen here); on
// non-Apple platforms the loader's dlopen reports the failure and the
// backend's error message names it.
using FnObjcGetClass = void* (*)(const char* name);
using FnObjcGetProtocol = void* (*)(const char* name);
using FnSelRegisterName = void* (*)(const char* name);
// objc_msgSend's real signature is the ABI's own variadic trampoline: a
// call through it casts to the target method's signature. The loader
// stores the raw pointer and the call sites cast (the documented
// pattern for calling Objective-C from C code without headers).
using FnObjcMsgSend = void (*)();

// The runtime's own C entry points the loader resolves.
extern FnObjcGetClass objc_getClass;
extern FnSelRegisterName sel_registerName;
extern FnObjcMsgSend objc_msgSend;

// The message-send wrappers the backend uses. Each is a thin inline
// cast of objc_msgSend to the method's real signature, declared once
// here so the call sites read like the API's own names.

// id objc_msgSend(id self, SEL op, ...) — the generic form used with
// explicit casts at the call site.
inline void* msgSend0(void* self, void* op)
{
    using Fn = void* (*)(void*, void*);
    return reinterpret_cast<Fn>(objc_msgSend)(self, op);
}
inline void* msgSend1(void* self, void* op, void* arg)
{
    using Fn = void* (*)(void*, void*, void*);
    return reinterpret_cast<Fn>(objc_msgSend)(self, op, arg);
}
inline void* msgSendPtr(void* self, void* op, const void* arg)
{
    using Fn = void* (*)(void*, void*, const void*);
    return reinterpret_cast<Fn>(objc_msgSend)(self, op, arg);
}
inline void* msgSendUL(void* self, void* op, NSUInteger arg)
{
    using Fn = void* (*)(void*, void*, NSUInteger);
    return reinterpret_cast<Fn>(objc_msgSend)(self, op, arg);
}
inline void* msgSend2UL(void* self, void* op, NSUInteger a, NSUInteger b)
{
    using Fn = void* (*)(void*, void*, NSUInteger, NSUInteger);
    return reinterpret_cast<Fn>(objc_msgSend)(self, op, a, b);
}
inline NSUInteger msgReturnUL(void* self, void* op)
{
    using Fn = NSUInteger (*)(void*, void*);
    return reinterpret_cast<Fn>(objc_msgSend)(self, op);
}
inline double msgReturnDouble(void* self, void* op)
{
    using Fn = double (*)(void*, void*);
    return reinterpret_cast<Fn>(objc_msgSend)(self, op);
}
inline void msgSendVoid(void* self, void* op)
{
    using Fn = void (*)(void*, void*);
    reinterpret_cast<Fn>(objc_msgSend)(self, op);
}
inline void msgSendVoid1(void* self, void* op, void* arg)
{
    using Fn = void (*)(void*, void*, void*);
    reinterpret_cast<Fn>(objc_msgSend)(self, op, arg);
}

// The Metal framework's selectors the backend sends. Declared once so a
// misspelling is a compile error at the one declaration, not a silent
// nil-return at every call site. (The strings are the published method
// names.)
namespace sel {
extern void* device_key;             // "device" (CAMetalLayer)
extern void* current_drawable_key;   // "currentDrawable"
extern void* pixel_format_key;       // "pixelFormat"
extern void* set_pixel_format_key;   // "setPixelFormat:"
extern void* drawable_size_key;      // "drawableSize"
extern void* set_drawable_size_key;  // "setDrawableSize:"
extern void* frame_key;              // "frame" (CAMetalLayer)
extern void* set_frame_key;          // "setFrame:" (CAMetalLayer)
}  // namespace sel

/// The Metal loader: resolves the Objective-C runtime and the Metal
/// framework's entry points at runtime (dlopen/dlsym; no link-time
/// dependency, the same rule the OpenGL and the Vulkan loaders record).
/// On a platform without the framework, load() reports false and
/// missingSymbol names the first symbol that could not be resolved.
class MtlLoader
{
public:
    /// The dlopen path attempt. On Apple platforms the framework loads;
    /// elsewhere it reports false with a clear missing-symbol message.
    bool load(std::string& missingSymbol);

    /// Whether the runtime resolved (the hardware path exists here).
    bool loaded() const noexcept { return m_loaded; }

    // The resolved runtime entry points (null until load() succeeded).
    FnObjcGetClass objc_getClass_fn = nullptr;
    FnSelRegisterName sel_registerName_fn = nullptr;
    FnObjcMsgSend objc_msgSend_fn = nullptr;

    // The Metal framework's entry point: MTLCreateSystemDefaultDevice is
    // the one published C function Metal exposes.
    void* (*MTLCreateSystemDefaultDevice_fn)() = nullptr;

    // The class handles the backend sends messages to (null until
    // load() resolved them).
    void* cls_CAMetalLayer = nullptr;
    void* cls_NSString = nullptr;
    void* cls_MTLTextureDescriptor = nullptr;
    void* cls_MTLSamplerDescriptor = nullptr;
    void* cls_MTLRenderPipelineDescriptor = nullptr;
    void* cls_MTLDepthStencilDescriptor = nullptr;
    void* cls_MTLVertexDescriptor = nullptr;
    void* cls_MTLCompileOptions = nullptr;
    void* cls_MTLRenderPassDescriptor = nullptr;
    void* cls_NSProcessInfo = nullptr;

private:
    bool m_loaded = false;
};

}  // namespace metal
}  // namespace render
}  // namespace grace
