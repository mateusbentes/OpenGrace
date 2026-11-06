#include "grace/render/opengl_render_backend.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

#include "test_support.hpp"

namespace {

using grace::render::ClearColor;
using grace::render::OpenGLBackendConfig;
using grace::render::OpenGLRenderBackend;

bool throwsInvalidArgument(OpenGLBackendConfig config)
{
    try
    {
        const OpenGLRenderBackend backend(config);
        static_cast<void>(backend);
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }
    return false;
}

}  // namespace

int main()
{
    OpenGLBackendConfig config;
    config.width = 64;
    config.height = 48;
    config.hidden = true;

    std::unique_ptr<OpenGLRenderBackend> backend;
    try
    {
        backend = std::make_unique<OpenGLRenderBackend>(config);
        backend->initialise();
    }
    catch (const std::exception& error)
    {
        // No GL context in this environment: report as skipped (CTest code 77).
        std::printf("SKIP: no GL context in this environment: %s\n", error.what());
        return 77;
    }

    // Context creation reports a version at or above the requested one. The
    // primary request is 3.3 core; the recorded 3.0 fallback is separate.
    GRACE_CHECK(backend->contextMajor() >= 3);
    if (!backend->usedFallbackContext())
    {
        GRACE_CHECK(backend->contextMajor() > 3 ||
                    (backend->contextMajor() == 3 && backend->contextMinor() >= 3));
    }
    GRACE_CHECK(!backend->glVersionString().empty());
    GRACE_CHECK(!backend->glVendor().empty());
    GRACE_CHECK(!backend->glRenderer().empty());

    // The backend description reports version, vendor and renderer.
    const std::string description = backend->describe();
    GRACE_CHECK(description.find("opengl") != std::string::npos);
    GRACE_CHECK(description.find("vendor=") != std::string::npos);
    GRACE_CHECK(description.find("renderer=") != std::string::npos);

    // Invalid configuration is rejected.
    {
        OpenGLBackendConfig bad = config;
        bad.width = 0;
        GRACE_CHECK(throwsInvalidArgument(bad));
    }

    // Protocol violations: calls before initialisation and after shutdown.
    {
        OpenGLRenderBackend early(config);
        early.clear();                                // before initialise
        early.setClearColor(ClearColor{9, 9, 9, 9});  // before initialise
        early.shutdown();                             // before initialise
        early.readback();                             // before initialise
        GRACE_CHECK_EQ(early.violationCount(), 4u);
        // Handle factories before initialise: violation + logic_error.
        bool threw_mesh = false;
        try
        {
            static_cast<void>(early.createGpuMesh());
        }
        catch (const std::logic_error&)
        {
            threw_mesh = true;
        }
        GRACE_CHECK(threw_mesh);
        bool threw_shader = false;
        try
        {
            static_cast<void>(early.createShaderProgram("vertex", "fragment"));
        }
        catch (const std::logic_error&)
        {
            threw_shader = true;
        }
        GRACE_CHECK(threw_shader);
        GRACE_CHECK_EQ(early.violationCount(), 6u);
        try
        {
            early.initialise();
        }
        catch (const std::exception& error)
        {
            std::printf("SKIP: no GL context in this environment: %s\n", error.what());
            return 77;
        }
        early.setClearColor(ClearColor{1, 2, 3, 4});
        early.clear();
        GRACE_CHECK_EQ(early.violationCount(), 6u);  // no new violations
        early.shutdown();
        early.shutdown();  // safe to call twice, but counted
        GRACE_CHECK_EQ(early.violationCount(), 7u);
    }

    // A clean contract sequence produces no violations and no GL errors.
    const ClearColor expected{1, 2, 3, 4};
    backend->setClearColor(expected);
    backend->clear();
    backend->clear();
    GRACE_CHECK(backend->clearColor() == expected);
    GRACE_CHECK_EQ(backend->violationCount(), 0u);
    GRACE_CHECK_EQ(backend->glErrorCount(), 0u);
    GRACE_CHECK(backend->lastError().empty());
    backend->shutdown();
    backend->shutdown();
    GRACE_CHECK_EQ(backend->violationCount(), 1u);

    return grace::test::summary("opengl_context");
}
