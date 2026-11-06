#pragma once

#include <cstdio>
#include <cstdlib>

namespace grace::core::math::detail {

/// Handler for degenerate math inputs. When installed, it is called with the
/// function name and a description of the invalid input instead of aborting;
/// tests use it to verify the messages. Install with setViolationHandler().
using ViolationHandler = void (*)(const char* function, const char* what);

inline ViolationHandler& violationHandlerSlot()
{
    static ViolationHandler handler = nullptr;
    return handler;
}

inline void setViolationHandler(ViolationHandler handler)
{
    violationHandlerSlot() = handler;
}

/// Reports a degenerate input. With a handler installed, the handler is
/// called. Without a handler: in Debug builds (NDEBUG not defined) this
/// prints a message naming the function and the invalid input and aborts; in
/// Release builds it is silent, preserving the documented Release behaviour
/// exactly (no dependency, no change to the math results).
inline void degenerateInput(const char* function, const char* what)
{
    if (ViolationHandler handler = violationHandlerSlot())
    {
        handler(function, what);
        return;
    }
#ifndef NDEBUG
    std::fprintf(stderr, "grace math contract violation in %s: %s\n", function, what);
    std::abort();
#else
    static_cast<void>(function);
    static_cast<void>(what);
#endif
}

}  // namespace grace::core::math::detail
