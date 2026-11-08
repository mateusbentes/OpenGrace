#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace grace::script {

/// Opaque handle to a script engine created by a backend.
class ScriptEngine
{
public:
    virtual ~ScriptEngine() = default;
};

/// Opaque handle to a module inside an engine.
class ScriptModule
{
public:
    virtual ~ScriptModule() = default;
};

/// Opaque handle to a compiled function inside a module.
class ScriptFunction
{
public:
    virtual ~ScriptFunction() = default;
};

/// Opaque handle to an execution context.
class ScriptContext
{
public:
    virtual ~ScriptContext() = default;
};

/// Flags for a registered value type.
enum class TypeFlags : std::uint32_t
{
    None = 0,
    /// A plain value type scripts can construct on the stack, with no
    /// reference-counting and no constructor.
    PlainValue = 1u << 0,
};

/// Bitwise OR of two flag sets.
constexpr TypeFlags operator|(TypeFlags a, TypeFlags b) noexcept
{
    return static_cast<TypeFlags>(static_cast<std::uint32_t>(a) |
                                  static_cast<std::uint32_t>(b));
}

/// Calling convention for a registered native function.
enum class CallingConvention
{
    /// A plain C function: `ret f(args...)`.
    Cdecl = 0,
    /// A plain C function that takes the object as the last argument.
    CdeclObjLast = 1,
    /// A plain C function that takes the object as the first argument.
    CdeclObjFirst = 2,
};

/// Description of a script engine.
struct EngineDesc
{
    /// The engine's name, for diagnostics.
    std::string name;
    /// The maximum number of call-stack levels.
    std::uint32_t maxCallStackDepth = 0;

    /// Validates the description. Returns false and fills the message on
    /// an invalid description.
    bool validate(std::string& message) const;
};

}  // namespace grace::script
