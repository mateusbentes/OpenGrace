#include "mtl_loader.hpp"

#ifdef __APPLE__
#include <dlfcn.h>
#endif

namespace grace {
namespace render {
namespace metal {

// The loader's resolved runtime entry points. The header declares them
// as extern; the definitions live here and load() fills them.
FnObjcGetClass objc_getClass = nullptr;
FnSelRegisterName sel_registerName = nullptr;
FnObjcMsgSend objc_msgSend = nullptr;

namespace {
}  // namespace

namespace sel {
void* device_key = nullptr;
void* current_drawable_key = nullptr;
void* pixel_format_key = nullptr;
void* set_pixel_format_key = nullptr;
void* drawable_size_key = nullptr;
void* set_drawable_size_key = nullptr;
void* frame_key = nullptr;
void* set_frame_key = nullptr;
}  // namespace sel

bool MtlLoader::load(std::string& missingSymbol)
{
    if (m_loaded)
        return true;

#ifndef __APPLE__
    // The dlfcn.h header does not exist on Windows/MinGW, so the
    // runtime probing below is only compiled on Apple platforms.
    // Everywhere else the framework is absent by definition and the
    // loader reports the missing symbol the backend's message shows.
    missingSymbol =
        "Metal runtime (dlopen: no Metal on this platform)";
    return false;
#else
    // The Objective-C runtime: on Apple platforms libobjc is part of the
    // system and already loaded into every process, so dlopen of the
    // well-known path succeeds trivially; the dlsym calls resolve the
    // three entry points the backend sends messages through.
    void* runtime = dlopen(nullptr, RTLD_LAZY | RTLD_GLOBAL);
    if (runtime == nullptr)
    {
        missingSymbol = "Objective-C runtime (dlopen)";
        return false;
    }
    void* getClass = dlsym(RTLD_DEFAULT, "objc_getClass");
    if (getClass == nullptr)
    {
        missingSymbol = "objc_getClass";
        return false;
    }
    void* selReg = dlsym(RTLD_DEFAULT, "sel_registerName");
    if (selReg == nullptr)
    {
        missingSymbol = "sel_registerName";
        return false;
    }
    void* msgSend = dlsym(RTLD_DEFAULT, "objc_msgSend");
    if (msgSend == nullptr)
    {
        missingSymbol = "objc_msgSend";
        return false;
    }
    objc_getClass = reinterpret_cast<FnObjcGetClass>(getClass);
    sel_registerName = reinterpret_cast<FnSelRegisterName>(selReg);
    objc_msgSend = reinterpret_cast<FnObjcMsgSend>(msgSend);

    // The Metal framework itself. On Apple platforms the framework is
    // present; on Linux (and any non-Apple host) the dlopen fails with
    // the platform's own error, which the backend's message reports.
    void* framework = dlopen(
        "/System/Library/Frameworks/Metal.framework/Metal", RTLD_LAZY);
    if (framework == nullptr)
    {
        missingSymbol =
            "Metal.framework (dlopen: no Metal on this platform)";
        return false;
    }
    void* createDevice = dlsym(framework, "MTLCreateSystemDefaultDevice");
    if (createDevice == nullptr)
    {
        missingSymbol = "MTLCreateSystemDefaultDevice";
        return false;
    }
    MTLCreateSystemDefaultDevice_fn =
        reinterpret_cast<void* (*)()>(createDevice);

    // The framework's classes the backend allocates descriptor objects
    // from (a class not present means a framework too old for the
    // backend; the missing-symbol message names it).
    const struct
    {
        const char* name;
        void** target;
    } classes[] = {
        {"MTLTextureDescriptor", &cls_MTLTextureDescriptor},
        {"MTLSamplerDescriptor", &cls_MTLSamplerDescriptor},
        {"MTLRenderPipelineDescriptor",
         &cls_MTLRenderPipelineDescriptor},
        {"MTLDepthStencilDescriptor",
         &cls_MTLDepthStencilDescriptor},
        {"MTLVertexDescriptor", &cls_MTLVertexDescriptor},
        {"MTLCompileOptions", &cls_MTLCompileOptions},
        {"MTLRenderPassDescriptor", &cls_MTLRenderPassDescriptor},
        {"CAMetalLayer", &cls_CAMetalLayer},
    };
    for (const auto& entry : classes)
    {
        void* cls = objc_getClass(entry.name);
        if (cls == nullptr)
        {
            missingSymbol = entry.name;
            return false;
        }
        *entry.target = cls;
    }

    // The presentation layer (QuartzCore) carries CAMetalLayer's
    // framework; the class handle resolved above is enough for the
    // message sends, but the framework must be loaded for the class to
    // respond. Loading it here keeps the dependency explicit.
    dlopen("/System/Library/Frameworks/QuartzCore.framework/QuartzCore",
           RTLD_LAZY);

    // The selectors (the published method names, registered once; the
    // runtime interns selector strings, so sel_registerName is idempotent).
    sel::device_key = sel_registerName("device");
    sel::current_drawable_key = sel_registerName("currentDrawable");
    sel::pixel_format_key = sel_registerName("pixelFormat");
    sel::set_pixel_format_key = sel_registerName("setPixelFormat:");
    sel::drawable_size_key = sel_registerName("drawableSize");
    sel::set_drawable_size_key = sel_registerName("setDrawableSize:");
    sel::frame_key = sel_registerName("frame");
    sel::set_frame_key = sel_registerName("setFrame:");

    m_loaded = true;
    return true;
#endif
}

}  // namespace metal
}  // namespace render
}  // namespace grace
