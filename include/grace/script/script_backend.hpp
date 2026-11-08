#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "grace/script/script_types.hpp"

namespace grace::script {

class ScriptEngine;
class ScriptModule;
class ScriptFunction;
class ScriptContext;

/// Platform-neutral script-host contract.
///
/// This header must stay free of any scripting-language type: no as* type
/// may appear here or in any API-neutral header. Concrete backends (the
/// AngelScript adapter, and later the Tetragramaton visual layer) implement
/// this interface in their own modules. The contract is the engine-owned
/// script host the documented architecture describes: text scripts compile
/// to bytecode the engine owns, the engine registers application types into
/// the script engine, and the visual layer's graph compiles to a callable
/// entry point.
///
/// The registration API is decl-string based as data: registerType takes a
/// name, a size and flags; registerFunction takes a decl string and a native
/// function pointer; registerGlobalProperty takes a decl string and a void
/// pointer. The visual layer (milestone 10) registers its blocks through
/// this same surface, so the host must support type registration from the
/// visual layer from the start (architecture §4.2).
///
/// Protocol: mutating calls made after shutdown() are protocol violations
/// and are counted through violationCount(); a repeated shutdown() is a
/// violation too but must stay safe.
class ScriptBackend
{
public:
    virtual ~ScriptBackend() = default;

    /// Human-readable backend description, for example "null (headless)".
    virtual std::string describe() const = 0;

    /// Releases backend resources. Exactly one successful call per backend
    /// lifetime; calls after shutdown are protocol violations.
    virtual void shutdown() = 0;

    /// Number of protocol violations observed so far.
    virtual std::uint64_t violationCount() const noexcept = 0;

    // -- Engine and module lifetime -------------------------------------------
    //
    // The engine is explicit, not a singleton: the application creates a
    // script engine through the backend, and all modules live inside it.
    // The application owns the lifetime: releaseEngine() destroys the engine
    // and every module it owns. A module must be released before its engine.
    /// Creates a script engine. Throws std::invalid_argument on an invalid
    /// description.
    virtual ScriptEngine* createEngine(const EngineDesc& desc) = 0;
    /// Destroys an engine and every module it owns. Releasing a null pointer
    /// or a foreign engine is a protocol violation but stays safe.
    virtual void releaseEngine(ScriptEngine* engine) = 0;
    /// Creates a module inside an engine. Throws std::invalid_argument if
    /// the name is empty or already used.
    virtual ScriptModule* createModule(ScriptEngine* engine, const std::string& name) = 0;
    /// Destroys a module. Releasing a null pointer or a foreign module is a
    /// protocol violation but stays safe.
    virtual void releaseModule(ScriptModule* module) = 0;

    // -- Script source and compilation ----------------------------------------
    //
    // A module is a unit of compilation: sections are added to it, then
    // build() compiles the whole set. Errors are reported through
    // lastError() and never throw.
    /// Adds a script source section to a module. The module is not compiled
    /// until build() is called.
    virtual void addScriptSource(ScriptModule* module,
                                 const std::string& sectionName,
                                 const std::string& source) = 0;
    /// Compiles a module. Returns false and fills lastError() on failure.
    virtual bool build(ScriptModule* module) = 0;
    /// Finds a compiled function by declaration. Returns null if absent.
    virtual ScriptFunction* findFunction(ScriptModule* module,
                                         const std::string& decl) = 0;

    // -- Registration ----------------------------------------------------------
    //
    // The application registers its types, functions and globals into the
    // engine so scripts can call them. The visual layer (milestone 10)
    // registers its blocks through the same surface. Registration calls are
    // made before any module is built: a registration after a build is a
    // protocol violation.
    /// Registers a value type by name, size and flags. Throws
    /// std::invalid_argument if the name is empty or already used.
    virtual void registerType(const std::string& name,
                              std::uint32_t byteSize,
                              TypeFlags flags) = 0;
    /// Registers a method on a registered type. Throws std::invalid_argument
    /// if the type is not registered.
    virtual void registerMethod(const std::string& typeName,
                                const std::string& decl,
                                void* functionPointer,
                                CallingConvention convention) = 0;
    /// Registers a global function by decl string and native pointer.
    virtual void registerFunction(const std::string& decl,
                                  void* functionPointer,
                                  CallingConvention convention) = 0;
    /// Registers a global property by decl string and a pointer to the
    /// engine-owned storage. The engine never copies the storage: scripts
    /// read and write through the pointer.
    virtual void registerGlobalProperty(const std::string& decl,
                                        void* pointer) = 0;

    // -- Execution -------------------------------------------------------------
    //
    // A context is a single execution thread: prepare a function, execute,
    // read the state. The engine owns the contexts; the application acquires
    // and releases them.
    /// Creates a context for an engine. Throws std::invalid_argument on a
    /// null engine.
    virtual ScriptContext* createContext(ScriptEngine* engine) = 0;
    /// Destroys a context. Releasing a null pointer or a foreign context is
    /// a protocol violation but stays safe.
    virtual void releaseContext(ScriptContext* context) = 0;
    /// Prepares a function for execution in a context. Returns false and
    /// fills lastError() on failure.
    virtual bool prepare(ScriptContext* context, ScriptFunction* function) = 0;
    /// Passes an argument (int) to the prepared function. The argument
    /// index is 0-based.
    virtual void setArgumentInt(ScriptContext* context,
                                std::uint32_t index,
                                std::int32_t value) = 0;
    /// Passes an argument (float) to the prepared function.
    virtual void setArgumentFloat(ScriptContext* context,
                                  std::uint32_t index,
                                  float value) = 0;
    /// Executes the prepared function. Returns false and fills lastError()
    /// on failure.
    virtual bool execute(ScriptContext* context) = 0;
    /// Reads the return value (int) of a finished execution.
    virtual std::int32_t returnInt(ScriptContext* context) const = 0;
    /// Reads the return value (float) of a finished execution.
    virtual float returnFloat(ScriptContext* context) const = 0;
    /// Reads the engine-owned storage of a global property by decl string.
    /// Returns null if the property is not registered.
    virtual void* globalProperty(const std::string& decl) const = 0;

    // -- Errors ----------------------------------------------------------------
    /// The last error message, for example a build error. Cleared on the
    /// next successful call.
    virtual std::string lastError() const = 0;
};

}  // namespace grace::script
