#include "grace/script/null_script_backend.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace grace::script {

// -- Concrete handle types ---------------------------------------------------
//
// The null backend's engine is a record: the modules, contexts, functions and
// sources registered into it. The backend owns the objects and destroys them
// with the engine.

class NullScriptFunction final : public ScriptFunction
{
public:
    std::string decl;
};

class NullScriptModule final : public ScriptModule
{
public:
    NullScriptEngine* engine = nullptr;
    std::string name;
    std::vector<std::pair<std::string, std::string>> sections;
    bool built = false;
    std::vector<NullScriptFunction*> functions;
};

class NullScriptContext final : public ScriptContext
{
public:
    NullScriptEngine* engine = nullptr;
    bool prepared = false;
    std::vector<std::int32_t> intArgs;
    std::vector<float> floatArgs;
};

class NullScriptEngine final : public ScriptEngine
{
public:
    std::string name;
    std::uint32_t maxCallStackDepth = 0;
    std::vector<NullScriptModule*> modules;
    std::vector<NullScriptContext*> contexts;
};

// -- Backend lifetime ---------------------------------------------------------

std::string NullScriptBackend::describe() const
{
    return "null (headless, no language)";
}

void NullScriptBackend::shutdown()
{
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    m_shutdown = true;
    for (auto* engine : m_engines)
    {
        delete engine;
    }
    m_engines.clear();
    m_registeredTypes.clear();
    m_registeredFunctions.clear();
    m_registeredMethods.clear();
    m_globalProperties.clear();
}

std::uint64_t NullScriptBackend::violationCount() const noexcept
{
    return m_violations;
}

void NullScriptBackend::note(const std::string& message)
{
    ++m_violations;
    m_error = message;
}

void NullScriptBackend::record(std::string event)
{
    m_events.push_back(std::move(event));
}

// -- Engine and module lifetime ---------------------------------------------

ScriptEngine* NullScriptBackend::createEngine(const EngineDesc& desc)
{
    if (m_shutdown)
    {
        note("createEngine after shutdown");
        return nullptr;
    }
    std::string message;
    if (!desc.validate(message))
    {
        throw std::invalid_argument(message);
    }
    auto* engine = new NullScriptEngine;
    engine->name = desc.name;
    engine->maxCallStackDepth = desc.maxCallStackDepth;
    m_engines.push_back(engine);
    return engine;
}

void NullScriptBackend::releaseEngine(ScriptEngine* engine)
{
    if (m_shutdown)
    {
        note("releaseEngine after shutdown");
        return;
    }
    if (engine == nullptr)
    {
        note("releaseEngine(null)");
        return;
    }
    NullScriptEngine* e = nullptr;
    for (auto* candidate : m_engines)
    {
        if (candidate == engine)
        {
            e = candidate;
            break;
        }
    }
    if (e == nullptr)
    {
        // A pointer outside the engine list is either foreign or already
        // released: reading it further is undefined, so it is only noted.
        note("releaseEngine(foreign or double release)");
        return;
    }
    m_engines.erase(std::find(m_engines.begin(), m_engines.end(), e));
    for (auto* module : e->modules)
    {
        delete module;
    }
    for (auto* context : e->contexts)
    {
        delete context;
    }
    delete e;
}

ScriptModule* NullScriptBackend::createModule(ScriptEngine* engine,
                                              const std::string& name)
{
    if (m_shutdown)
    {
        note("createModule after shutdown");
        return nullptr;
    }
    auto* e = dynamic_cast<NullScriptEngine*>(engine);
    if (e == nullptr)
    {
        note("createModule(foreign engine)");
        return nullptr;
    }
    if (name.empty())
    {
        throw std::invalid_argument("module name: must not be empty");
    }
    for (auto* module : e->modules)
    {
        if (module->name == name)
        {
            throw std::invalid_argument("module name: already used");
        }
    }
    auto* module = new NullScriptModule;
    module->engine = e;
    module->name = name;
    e->modules.push_back(module);
    return module;
}

void NullScriptBackend::releaseModule(ScriptModule* module)
{
    if (m_shutdown)
    {
        note("releaseModule after shutdown");
        return;
    }
    if (module == nullptr)
    {
        note("releaseModule(null)");
        return;
    }
    NullScriptEngine* owner = nullptr;
    for (auto* engine : m_engines)
    {
        auto& modules = engine->modules;
        if (std::find(modules.begin(), modules.end(), module) != modules.end())
        {
            owner = engine;
            break;
        }
    }
    if (owner == nullptr)
    {
        note("releaseModule(foreign or double release)");
        return;
    }
    auto* m = static_cast<NullScriptModule*>(module);
    for (auto* function : m->functions)
    {
        delete function;
    }
    owner->modules.erase(std::find(owner->modules.begin(),
                                   owner->modules.end(), m));
    delete m;
}

// -- Script source and compilation ------------------------------------------

void NullScriptBackend::addScriptSource(ScriptModule* module,
                                        const std::string& sectionName,
                                        const std::string& source)
{
    if (m_shutdown)
    {
        note("addScriptSource after shutdown");
        return;
    }
    auto* m = dynamic_cast<NullScriptModule*>(module);
    if (m == nullptr)
    {
        note("addScriptSource(foreign module)");
        return;
    }
    m->sections.push_back({sectionName, source});
}

bool NullScriptBackend::build(ScriptModule* module)
{
    if (m_shutdown)
    {
        note("build after shutdown");
        return false;
    }
    auto* m = dynamic_cast<NullScriptModule*>(module);
    if (m == nullptr)
    {
        note("build(foreign module)");
        return false;
    }
    m_built = true;
    m_error = "null backend: no language, cannot compile scripts";
    return false;
}

ScriptFunction* NullScriptBackend::findFunction(ScriptModule* module,
                                                const std::string& decl)
{
    if (m_shutdown)
    {
        note("findFunction after shutdown");
        return nullptr;
    }
    auto* m = dynamic_cast<NullScriptModule*>(module);
    if (m == nullptr)
    {
        note("findFunction(foreign module)");
        return nullptr;
    }
    record("findFunction(" + decl + ")");
    return nullptr;
}

// -- Registration ------------------------------------------------------------

void NullScriptBackend::registerType(const std::string& name,
                                     std::uint32_t byteSize,
                                     TypeFlags flags)
{
    if (m_shutdown)
    {
        note("registerType after shutdown");
        return;
    }
    if (m_built)
    {
        note("registerType after build");
        return;
    }
    if (name.empty())
    {
        throw std::invalid_argument("type name: must not be empty");
    }
    for (const auto& existing : m_registeredTypes)
    {
        if (existing == name)
        {
            throw std::invalid_argument("type name: already used");
        }
    }
    m_registeredTypes.push_back(name);
    record("registerType(" + name + ", size=" + std::to_string(byteSize) +
           ", flags=" + std::to_string(static_cast<std::uint32_t>(flags)) + ")");
}

void NullScriptBackend::registerMethod(const std::string& typeName,
                                       const std::string& decl,
                                       void* functionPointer,
                                       CallingConvention convention)
{
    if (m_shutdown)
    {
        note("registerMethod after shutdown");
        return;
    }
    if (m_built)
    {
        note("registerMethod after build");
        return;
    }
    bool found = false;
    for (const auto& existing : m_registeredTypes)
    {
        if (existing == typeName)
        {
            found = true;
            break;
        }
    }
    if (!found)
    {
        throw std::invalid_argument("type name: not registered");
    }
    m_registeredMethods.push_back(typeName + " " + decl);
    record("registerMethod(" + typeName + ", " + decl + ", conv=" +
           std::to_string(static_cast<int>(convention)) + ", ptr=" +
           std::to_string(reinterpret_cast<std::uintptr_t>(functionPointer)) + ")");
}

void NullScriptBackend::registerFunction(const std::string& decl,
                                         void* functionPointer,
                                         CallingConvention convention)
{
    if (m_shutdown)
    {
        note("registerFunction after shutdown");
        return;
    }
    if (m_built)
    {
        note("registerFunction after build");
        return;
    }
    for (const auto& existing : m_registeredFunctions)
    {
        if (existing == decl)
        {
            throw std::invalid_argument("function decl: already used");
        }
    }
    m_registeredFunctions.push_back(decl);
    record("registerFunction(" + decl + ", conv=" +
           std::to_string(static_cast<int>(convention)) + ", ptr=" +
           std::to_string(reinterpret_cast<std::uintptr_t>(functionPointer)) + ")");
}

void NullScriptBackend::registerGlobalProperty(const std::string& decl,
                                               void* pointer)
{
    if (m_shutdown)
    {
        note("registerGlobalProperty after shutdown");
        return;
    }
    if (m_built)
    {
        note("registerGlobalProperty after build");
        return;
    }
    m_globalProperties[decl] = pointer;
    record("registerGlobalProperty(" + decl + ")");
}

// -- Execution ---------------------------------------------------------------

ScriptContext* NullScriptBackend::createContext(ScriptEngine* engine)
{
    if (m_shutdown)
    {
        note("createContext after shutdown");
        return nullptr;
    }
    auto* e = dynamic_cast<NullScriptEngine*>(engine);
    if (e == nullptr)
    {
        note("createContext(foreign engine)");
        return nullptr;
    }
    auto* context = new NullScriptContext;
    context->engine = e;
    e->contexts.push_back(context);
    return context;
}

void NullScriptBackend::releaseContext(ScriptContext* context)
{
    if (m_shutdown)
    {
        note("releaseContext after shutdown");
        return;
    }
    if (context == nullptr)
    {
        note("releaseContext(null)");
        return;
    }
    NullScriptEngine* owner = nullptr;
    for (auto* engine : m_engines)
    {
        auto& contexts = engine->contexts;
        if (std::find(contexts.begin(), contexts.end(), context) != contexts.end())
        {
            owner = engine;
            break;
        }
    }
    if (owner == nullptr)
    {
        note("releaseContext(foreign or double release)");
        return;
    }
    auto* c = static_cast<NullScriptContext*>(context);
    owner->contexts.erase(std::find(owner->contexts.begin(),
                                    owner->contexts.end(), c));
    delete c;
}

bool NullScriptBackend::prepare(ScriptContext* context,
                                ScriptFunction* function)
{
    if (m_shutdown)
    {
        note("prepare after shutdown");
        return false;
    }
    auto* c = dynamic_cast<NullScriptContext*>(context);
    if (c == nullptr)
    {
        note("prepare(foreign context)");
        return false;
    }
    record("prepare(funct=" + std::to_string(reinterpret_cast<std::uintptr_t>(function)) + ")");
    m_error = "null backend: no language, cannot execute scripts";
    return false;
}

void NullScriptBackend::setArgumentInt(ScriptContext* context,
                                       std::uint32_t index,
                                       std::int32_t value)
{
    if (m_shutdown)
    {
        note("setArgumentInt after shutdown");
        return;
    }
    auto* c = dynamic_cast<NullScriptContext*>(context);
    if (c == nullptr)
    {
        note("setArgumentInt(foreign context)");
        return;
    }
    if (c->intArgs.size() <= index)
    {
        c->intArgs.resize(index + 1);
    }
    c->intArgs[index] = value;
    record("setArgumentInt(" + std::to_string(index) + ", " + std::to_string(value) + ")");
}

void NullScriptBackend::setArgumentFloat(ScriptContext* context,
                                         std::uint32_t index,
                                         float value)
{
    if (m_shutdown)
    {
        note("setArgumentFloat after shutdown");
        return;
    }
    auto* c = dynamic_cast<NullScriptContext*>(context);
    if (c == nullptr)
    {
        note("setArgumentFloat(foreign context)");
        return;
    }
    if (c->floatArgs.size() <= index)
    {
        c->floatArgs.resize(index + 1);
    }
    c->floatArgs[index] = value;
    record("setArgumentFloat(" + std::to_string(index) + ", " + std::to_string(value) + ")");
}

bool NullScriptBackend::execute(ScriptContext* context)
{
    if (m_shutdown)
    {
        note("execute after shutdown");
        return false;
    }
    auto* c = dynamic_cast<NullScriptContext*>(context);
    if (c == nullptr)
    {
        note("execute(foreign context)");
        return false;
    }
    m_error = "null backend: no language, cannot execute scripts";
    return false;
}

std::int32_t NullScriptBackend::returnInt(ScriptContext* context) const
{
    (void)context;
    return 0;
}

float NullScriptBackend::returnFloat(ScriptContext* context) const
{
    (void)context;
    return 0.0f;
}

void* NullScriptBackend::globalProperty(const std::string& decl) const
{
    auto it = m_globalProperties.find(decl);
    return it != m_globalProperties.end() ? it->second : nullptr;
}

std::string NullScriptBackend::lastError() const
{
    return m_error;
}

}  // namespace grace::script
