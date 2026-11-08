#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "grace/script/script_backend.hpp"

namespace grace::script {

class NullScriptEngine;
class NullScriptModule;
class NullScriptContext;

/// Headless script backend: the contract's protocol reference.
///
/// The null backend records the registration surface and the execution
/// requests and counts protocol violations, but it cannot compile or run
/// scripts: it has no language. build() and execute() return false with a
/// deterministic lastError() message, and findFunction() returns null. The
/// registration round-trip (register -> globalProperty) is real, so the
/// contract's registration surface is proven here; the language semantics
/// are proven by the AngelScript adapter's suite.
///
/// Everything is deterministic: the same call sequence produces the same
/// records and the same violation counts.
class NullScriptBackend final : public ScriptBackend
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

    /// Recorded call sequence, in order.
    const std::vector<std::string>& events() const noexcept { return m_events; }

private:
    void record(std::string event);
    void note(const std::string& message);

    NullScriptModule* moduleOf(ScriptModule* module) const;
    NullScriptEngine* engineOf(ScriptEngine* engine) const;
    NullScriptContext* contextOf(ScriptContext* context) const;

    bool m_shutdown = false;
    std::uint64_t m_violations = 0;
    std::string m_error;

    std::vector<std::string> m_registeredTypes;
    std::vector<std::string> m_registeredFunctions;
    std::vector<std::string> m_registeredMethods;
    std::map<std::string, void*> m_globalProperties;

    std::vector<NullScriptEngine*> m_engines;
    bool m_built = false;
    std::vector<std::string> m_events;
};

}  // namespace grace::script
