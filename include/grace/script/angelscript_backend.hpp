#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "grace/script/script_backend.hpp"

namespace grace::script {

class AsScriptEngine;
class AsModule;
class AsFunction;
class AsContext;

/// AngelScript adapter: the script contract's real implementation.
///
/// The contract is decl-string based as data, which is exactly what
/// AngelScript's registration API is: registerType(name, size, flags) maps
/// onto asIScriptEngine::RegisterObjectType with asOBJ_VALUE | asOBJ_POD,
/// registerMethod/registerFunction map onto RegisterObjectMethod /
/// RegisterGlobalFunction with the matching asCALL_* convention, and
/// registerGlobalProperty maps onto RegisterGlobalProperty.
///
/// Protocol: mutating calls after shutdown() are violations and are counted
/// through violationCount(); a repeated shutdown() is a violation but stays
/// safe. Registration after a build is a violation (the contract's rule),
/// since AngelScript's registration is engine-wide and a module's build
/// compiles against it. Handle validation is by list membership, never by
/// reading a possibly-dangling pointer (the double-release lesson).
///
/// Everything is engine-owned: the adapter owns the asIScriptEngine and its
/// modules, contexts and functions. The application owns the lifetime:
/// releaseEngine() destroys the engine and every module it owns.
class AngelScriptBackend final : public ScriptBackend
{
public:
    std::string describe() const override;
    void shutdown() override;
    std::uint64_t violationCount() const noexcept override;

    ScriptEngine* createEngine(const EngineDesc& desc) override;
    void releaseEngine(ScriptEngine* engine) override;
    ScriptModule* createModule(ScriptEngine* engine, const std::string& name) override;
    void releaseModule(ScriptModule* module) override;

    void addScriptSource(ScriptModule* module,
                         const std::string& sectionName,
                         const std::string& source) override;
    bool build(ScriptModule* module) override;
    ScriptFunction* findFunction(ScriptModule* module,
                                 const std::string& decl) override;

    void registerType(const std::string& name,
                      std::uint32_t byteSize,
                      TypeFlags flags) override;
    void registerMethod(const std::string& typeName,
                        const std::string& decl,
                        void* functionPointer,
                        CallingConvention convention) override;
    void registerFunction(const std::string& decl,
                          void* functionPointer,
                          CallingConvention convention) override;
    void registerGlobalProperty(const std::string& decl,
                                void* pointer) override;

    ScriptContext* createContext(ScriptEngine* engine) override;
    void releaseContext(ScriptContext* context) override;
    bool prepare(ScriptContext* context, ScriptFunction* function) override;
    void setArgumentInt(ScriptContext* context,
                        std::uint32_t index,
                        std::int32_t value) override;
    void setArgumentFloat(ScriptContext* context,
                          std::uint32_t index,
                          float value) override;
    bool execute(ScriptContext* context) override;
    std::int32_t returnInt(ScriptContext* context) const override;
    float returnFloat(ScriptContext* context) const override;
    void* globalProperty(const std::string& decl) const override;

    std::string lastError() const override;

private:
    void note(const std::string& message);
    AsScriptEngine* engineOf(ScriptEngine* engine) const;
    AsScriptEngine* moduleOwner(ScriptModule* module) const;
    AsScriptEngine* contextOwner(ScriptContext* context) const;
    AsContext* contextOf(ScriptContext* context) const;
    AsFunction* functionOf(ScriptFunction* function) const;
    void applyRegistrations(AsScriptEngine* engine);

    /// A registration applied into every engine created through the backend.
    struct RegisteredType
    {
        std::string name;
        std::uint32_t byteSize = 0;
        TypeFlags flags = TypeFlags::None;
    };
    struct RegisteredMethod
    {
        std::string typeName;
        std::string decl;
        void* function = nullptr;
        CallingConvention convention = CallingConvention::Cdecl;
    };
    struct RegisteredFunction
    {
        std::string decl;
        void* function = nullptr;
        CallingConvention convention = CallingConvention::Cdecl;
    };
    struct RegisteredProperty
    {
        std::string decl;
        void* pointer = nullptr;
    };

    bool m_shutdown = false;
    std::uint64_t m_violations = 0;
    std::string m_error;

    std::vector<AsScriptEngine*> m_engines;
    bool m_built = false;
    std::vector<RegisteredType> m_registeredTypes;
    std::vector<RegisteredMethod> m_registeredMethods;
    std::vector<RegisteredFunction> m_registeredFunctions;
    std::vector<RegisteredProperty> m_globalProperties;
};

}  // namespace grace::script
