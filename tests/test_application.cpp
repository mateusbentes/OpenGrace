#include "grace/core/application.hpp"
#include "grace/render/null_render_backend.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_support.hpp"

namespace {

using grace::core::Application;
using grace::core::ApplicationConfig;
using grace::core::FixedStepConfig;
using grace::render::ClearColor;
using grace::render::NullRenderBackend;

template <typename F>
bool throwsInvalidArgument(F&& fn)
{
    try
    {
        fn();
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }
    return false;
}

template <typename F>
bool throwsLogicError(F&& fn)
{
    try
    {
        fn();
    }
    catch (const std::logic_error&)
    {
        return true;
    }
    return false;
}

}  // namespace

int main()
{
    // Invalid configuration rejection.
    {
        NullRenderBackend backend(64, 48);
        ApplicationConfig config;
        config.frame_ns = 20;
        config.step = FixedStepConfig{10, 4};
        GRACE_CHECK(Application::isValid(config));

        config.frame_ns = 0;
        GRACE_CHECK(!Application::isValid(config));
        GRACE_CHECK(throwsInvalidArgument([&] {
            const Application app(config, backend);
            static_cast<void>(app);
        }));

        config.frame_ns = 20;
        config.step = FixedStepConfig{0, 4};
        GRACE_CHECK(!Application::isValid(config));
        GRACE_CHECK(throwsInvalidArgument([&] {
            const Application app(config, backend);
            static_cast<void>(app);
        }));
    }

    // A zero-sized frame target is rejected by the null backend.
    GRACE_CHECK(throwsInvalidArgument([] { const NullRenderBackend backend(0, 48); static_cast<void>(backend); }));

    // Lifecycle ordering against the null backend: one clear per frame, all
    // steps of a frame before that frame's clear, shutdown last, no
    // violations.
    {
        NullRenderBackend backend(320, 240);
        ApplicationConfig config;
        config.frame_ns = 20;
        config.step = FixedStepConfig{10, 4};
        config.clear_color = ClearColor{1, 2, 3, 4};

        std::vector<std::uint64_t> step_indices;
        std::vector<std::size_t> events_at_step;
        Application app(config, backend, [&](std::uint64_t index) {
            step_indices.push_back(index);
            events_at_step.push_back(backend.events().size());
        });

        // runFrame before initialise is a lifecycle error.
        GRACE_CHECK(throwsLogicError([&] { static_cast<void>(app.runFrame()); }));

        const std::uint64_t total = app.run(3);
        GRACE_CHECK_EQ(total, 6u);
        GRACE_CHECK_EQ(app.clock().totalSteps(), 6u);

        // Step indices are global and contiguous.
        const std::vector<std::uint64_t> expected_indices{0, 1, 2, 3, 4, 5};
        GRACE_CHECK(step_indices == expected_indices);

        // Frame f's clear is event f+1, so every step of frame f ran while
        // the log still had f+1 entries.
        const std::vector<std::size_t> expected_events{1, 1, 2, 2, 3, 3};
        GRACE_CHECK(events_at_step == expected_events);

        const std::vector<std::string> expected_log{"setClearColor(1,2,3,4)",
                                                    "clear(1,2,3,4)", "clear(1,2,3,4)",
                                                    "clear(1,2,3,4)", "shutdown"};
        GRACE_CHECK(backend.events() == expected_log);
        GRACE_CHECK_EQ(backend.violationCount(), 0u);

        // A second run or shutdown is rejected.
        GRACE_CHECK(throwsLogicError([&] { static_cast<void>(app.run(1)); }));
        GRACE_CHECK(throwsLogicError([&] { app.shutdown(); }));
    }

    // A full run produces a deterministic clear colour; two identical runs
    // produce identical call logs.
    {
        ApplicationConfig config;
        config.frame_ns = 25;
        config.step = FixedStepConfig{10, 8};
        config.clear_color = ClearColor{10, 20, 30, 40};

        NullRenderBackend first(640, 480);
        Application a(config, first);
        static_cast<void>(a.run(4));

        NullRenderBackend second(640, 480);
        Application b(config, second);
        static_cast<void>(b.run(4));

        GRACE_CHECK(first.events() == second.events());
        const ClearColor expected_color{10, 20, 30, 40};
        GRACE_CHECK(first.clearColor() == expected_color);
        GRACE_CHECK_EQ(first.violationCount(), 0u);
        GRACE_CHECK_EQ(first.width(), 640u);
        GRACE_CHECK_EQ(first.height(), 480u);
        GRACE_CHECK(first.describe() == std::string("null (headless)"));

        // frame_ns 25 with step 10: 100 ns over 4 frames is exactly 10 steps.
        GRACE_CHECK_EQ(a.clock().totalSteps(), 10u);
        GRACE_CHECK_EQ(a.clock().clampCount(), 0u);
        GRACE_CHECK_EQ(a.clock().remainderNs(), 0);
    }

    // The null backend counts protocol violations after shutdown.
    {
        NullRenderBackend backend(8, 8);
        backend.shutdown();
        backend.clear();
        backend.setClearColor(ClearColor{9, 9, 9, 9});
        backend.shutdown();
        GRACE_CHECK_EQ(backend.violationCount(), 3u);
        GRACE_CHECK_EQ(backend.events().size(), 4u);
    }

    return grace::test::summary("application");
}
