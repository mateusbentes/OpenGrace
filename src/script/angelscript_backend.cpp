#include "grace/script/angelscript_backend.hpp"

#include <angelscript.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace grace::script {

// -- Concrete handle types ---------------------------------------------------
//
// The adapter owns the asIScriptEngine and everything it produces: modules,
// contexts and functions. The wrapper handles are the contract's opaque
// handles; the SDK objects are the real lifetime carriers. The registration
// surface is engine-wide in the SDK, so the backend keeps its own lists and
// applies them into every engine created through the backend.

class AsScriptEngine final : public ScriptEngine
{
public:
    asIScriptEngine* engine = nullptr;
    std::string name;
    std::vector<AsModule*> modules;
    std::vector<AsContext*> contexts;
    std::string lastBuildMessage;
};

class AsModule final : public ScriptModule
{
public:
    AsScriptEngine* owner = nullptr;
    asIScriptModule* module = nullptr;
    std::string name;
    std::vector<AsFunction*> functions;
};

class AsFunction final : public ScriptFunction
{
public:
    asIScriptFunction* function = nullptr;
};

class AsContext final : public ScriptContext
{
public:
    AsScriptEngine* owner = nullptr;
    asIScriptContext* context = nullptr;
};

namespace {

// -- Contract to SDK mappings ------------------------------------------------

// The contract passes native function pointers as void*: the adapter turns
// them into the SDK's asSFuncPtr. asFunctionPtr<T> does the reinterpret_cast
// internally, so the void* is first cast to a plain function pointer type.
asSFuncPtr asPtr(void* functionPointer)
{
    return asFunctionPtr(reinterpret_cast<asFUNCTION_t>(functionPointer));
}

// Convention mapping. The contract does not carry thiscall: a native method
// registered with CdeclObjFirst/CdeclObjLast receives the object as the
// first/last argument.
asDWORD mapConvention(CallingConvention convention)
{
    switch (convention)
    {
    case CallingConvention::Cdecl:
        return asCALL_CDECL;
    case CallingConvention::CdeclObjLast:
        return asCALL_CDECL_OBJLAST;
    case CallingConvention::CdeclObjFirst:
        return asCALL_CDECL_OBJFIRST;
    }
    return asCALL_CDECL;
}

// TypeFlags mapping: a PlainValue type is a plain stack value with no
// reference counting and no constructor, which is asOBJ_VALUE | asOBJ_POD.
// A None flag set is a reference type the SDK owns.
asQWORD mapFlags(TypeFlags flags)
{
    switch (flags)
    {
    case TypeFlags::None:
        return asOBJ_REF;
    case TypeFlags::PlainValue:
        return asOBJ_VALUE | asOBJ_POD;
    }
    return asOBJ_REF;
}

// -- Message callback ----------------------------------------------------------
//
// The SDK reports build errors through a message callback. The callback is a
// free function taking the owning wrapper as the last argument
// (asCALL_CDECL_OBJLAST), capturing the first error into the engine's
// lastBuildMessage. It is installed once per engine.

void messageCallback(const asSMessageInfo* message, void* object)
{
    auto* engine = static_cast<AsScriptEngine*>(object);
    if (message == nullptr || engine == nullptr)
    {
        return;
    }
    if (message->type == asMSGTYPE_ERROR)
    {
        engine->lastBuildMessage = message->message != nullptr
                                       ? message->message : "";
    }
}

}  // namespace

// -- Backend lifetime ----------------------------------------------------------

std::string AngelScriptBackend::describe() const
{
    // decision 33: the script contract's real implementation.
    return "angelscript (AngelScript 2.39, engine-owned script host)";
}

void AngelScriptBackend::shutdown()
{
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    m_shutdown = true;
    for (auto* engine : m_engines)
    {
        engine->engine->ShutDownAndRelease();
        delete engine;
    }
    m_engines.clear();
    m_registeredTypes.clear();
    m_registeredFunctions.clear();
    m_registeredMethods.clear();
    m_globalProperties.clear();
}

std::uint64_t AngelScriptBackend::violationCount() const noexcept
{
    return m_violations;
}

void AngelScriptBackend::note(const std::string& message)
{
    m_error = message;
    ++m_violations;
}

// -- Handle validation ---------------------------------------------------------

// List membership, never dynamic_cast on a possibly-dangling pointer (the
// double-release lesson from the null backend's suite).
AsScriptEngine* AngelScriptBackend::engineOf(ScriptEngine* engine) const
{
    if (engine == nullptr)
    {
        return nullptr;
    }
    for (auto* candidate : m_engines)
    {
        if (candidate == engine)
        {
            return candidate;
        }
    }
    return nullptr;
}

AsScriptEngine* AngelScriptBackend::moduleOwner(ScriptModule* module) const
{
    if (module == nullptr)
    {
        return nullptr;
    }
    for (auto* engine : m_engines)
    {
        auto& modules = engine->modules;
        if (std::find(modules.begin(), modules.end(), module) != modules.end())
        {
            return engine;
        }
    }
    return nullptr;
}

AsScriptEngine* AngelScriptBackend::contextOwner(ScriptContext* context) const
{
    if (context == nullptr)
    {
        return nullptr;
    }
    for (auto* engine : m_engines)
    {
        auto& contexts = engine->contexts;
        if (std::find(contexts.begin(), contexts.end(), context) != contexts.end())
        {
            return engine;
        }
    }
    return nullptr;
}

AsContext* AngelScriptBackend::contextOf(ScriptContext* context) const
{
    AsScriptEngine* owner = contextOwner(context);
    if (owner == nullptr)
    {
        return nullptr;
    }
    return static_cast<AsContext*>(context);
}

AsFunction* AngelScriptBackend::functionOf(ScriptFunction* function) const
{
    if (function == nullptr)
    {
        return nullptr;
    }
    for (auto* engine : m_engines)
    {
        for (auto* module : engine->modules)
        {
            auto& functions = module->functions;
            if (std::find(functions.begin(), functions.end(), function) !=
                functions.end())
            {
                return static_cast<AsFunction*>(function);
            }
        }
    }
    return nullptr;
}

void AngelScriptBackend::applyRegistrations(AsScriptEngine* engine)
{
    for (const auto& registered : m_registeredTypes)
    {
        engine->engine->RegisterObjectType(
            registered.name.c_str(),
            static_cast<int>(registered.byteSize),
            mapFlags(registered.flags));
    }
    for (const auto& registered : m_registeredMethods)
    {
        engine->engine->RegisterObjectMethod(
            registered.typeName.c_str(), registered.decl.c_str(),
            asPtr(registered.function),
            mapConvention(registered.convention));
    }
    for (const auto& registered : m_registeredFunctions)
    {
        engine->engine->RegisterGlobalFunction(
            registered.decl.c_str(), asPtr(registered.function),
            mapConvention(registered.convention));
    }
    for (const auto& registered : m_globalProperties)
    {
        engine->engine->RegisterGlobalProperty(
            registered.decl.c_str(), registered.pointer);
    }
}

// -- Engine and module lifetime ---------------------------------------------

ScriptEngine* AngelScriptBackend::createEngine(const EngineDesc& desc)
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
    auto* asEngine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
    if (asEngine == nullptr)
    {
        throw std::runtime_error("angelscript: engine creation failed");
    }
    if (desc.maxCallStackDepth > 0)
    {
        asEngine->SetEngineProperty(asEP_MAX_CALL_STACK_SIZE,
                                    static_cast<asPWORD>(desc.maxCallStackDepth));
    }
    auto* engine = new AsScriptEngine;
    engine->engine = asEngine;
    engine->name = desc.name;
    asEngine->SetMessageCallback(asFunctionPtr(&messageCallback),
                                 engine, asCALL_CDECL_OBJLAST);
    applyRegistrations(engine);
    m_engines.push_back(engine);
    return engine;
}

void AngelScriptBackend::releaseEngine(ScriptEngine* engine)
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
    AsScriptEngine* e = engineOf(engine);
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
        module->module->Discard();
        delete module;
    }
    for (auto* context : e->contexts)
    {
        context->context->Release();
        delete context;
    }
    e->engine->ShutDownAndRelease();
    delete e;
}

ScriptModule* AngelScriptBackend::createModule(ScriptEngine* engine,
                                               const std::string& name)
{
    if (m_shutdown)
    {
        note("createModule after shutdown");
        return nullptr;
    }
    AsScriptEngine* e = engineOf(engine);
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
    auto* asModule = e->engine->GetModule(name.c_str(),
                                          asGM_CREATE_IF_NOT_EXISTS);
    if (asModule == nullptr)
    {
        throw std::runtime_error("angelscript: module creation failed");
    }
    auto* module = new AsModule;
    module->owner = e;
    module->module = asModule;
    module->name = name;
    e->modules.push_back(module);
    return module;
}

void AngelScriptBackend::releaseModule(ScriptModule* module)
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
    AsScriptEngine* owner = moduleOwner(module);
    if (owner == nullptr)
    {
        note("releaseModule(foreign or double release)");
        return;
    }
    auto* m = static_cast<AsModule*>(module);
    for (auto* function : m->functions)
    {
        delete function;
    }
    owner->modules.erase(std::find(owner->modules.begin(),
                                   owner->modules.end(), m));
    m->module->Discard();
    delete m;
}

// -- Script source and compilation -------------------------------------------

void AngelScriptBackend::addScriptSource(ScriptModule* module,
                                         const std::string& sectionName,
                                         const std::string& source)
{
    if (m_shutdown)
    {
        note("addScriptSource after shutdown");
        return;
    }
    AsScriptEngine* owner = moduleOwner(module);
    if (owner == nullptr)
    {
        note("addScriptSource(foreign module)");
        return;
    }
    auto* m = static_cast<AsModule*>(module);
    const int result = m->module->AddScriptSection(
        sectionName.c_str(), source.c_str(), source.size());
    if (result < 0)
    {
        m_error = "angelscript: AddScriptSection failed";
    }
}

bool AngelScriptBackend::build(ScriptModule* module)
{
    if (m_shutdown)
    {
        note("build after shutdown");
        return false;
    }
    AsScriptEngine* owner = moduleOwner(module);
    if (owner == nullptr)
    {
        note("build(foreign module)");
        return false;
    }
    auto* m = static_cast<AsModule*>(module);
    const int result = m->module->Build();
    if (result < 0)
    {
        // The message callback captured the first error into the engine's
        // lastBuildMessage; fall back to a generic message when the SDK
        // reported nothing.
        m_error = owner->lastBuildMessage.empty()
                      ? "angelscript: build failed" : owner->lastBuildMessage;
        return false;
    }
    m_error.clear();
    m_built = true;
    return true;
}

ScriptFunction* AngelScriptBackend::findFunction(ScriptModule* module,
                                                 const std::string& decl)
{
    if (m_shutdown)
    {
        note("findFunction after shutdown");
        return nullptr;
    }
    if (moduleOwner(module) == nullptr)
    {
        note("findFunction(foreign module)");
        return nullptr;
    }
    auto* m = static_cast<AsModule*>(module);
    auto* asFunction = m->module->GetFunctionByDecl(decl.c_str());
    if (asFunction == nullptr)
    {
        return nullptr;
    }
    auto* f = new AsFunction;
    f->function = asFunction;
    m->functions.push_back(f);
    return f;
}

// -- Registration ------------------------------------------------------------

void AngelScriptBackend::registerType(const std::string& name,
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
        if (existing.name == name)
        {
            throw std::invalid_argument("type name: already used");
        }
    }
    m_registeredTypes.push_back({name, byteSize, flags});
    for (auto* engine : m_engines)
    {
        const int result = engine->engine->RegisterObjectType(
            name.c_str(), static_cast<int>(byteSize), mapFlags(flags));
        if (result < 0)
        {
            throw std::invalid_argument("angelscript: RegisterObjectType failed");
        }
    }
}

void AngelScriptBackend::registerMethod(const std::string& typeName,
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
        if (existing.name == typeName)
        {
            found = true;
            break;
        }
    }
    if (!found)
    {
        throw std::invalid_argument("type name: not registered");
    }
    m_registeredMethods.push_back({typeName, decl, functionPointer,
                                   convention});
    for (auto* engine : m_engines)
    {
        const int result = engine->engine->RegisterObjectMethod(
            typeName.c_str(), decl.c_str(), asPtr(functionPointer),
            mapConvention(convention));
        if (result < 0)
        {
            throw std::invalid_argument(
                "angelscript: RegisterObjectMethod failed");
        }
    }
}

void AngelScriptBackend::registerFunction(const std::string& decl,
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
    if (decl.empty())
    {
        throw std::invalid_argument("function decl: must not be empty");
    }
    for (const auto& existing : m_registeredFunctions)
    {
        if (existing.decl == decl)
        {
            throw std::invalid_argument("function decl: already used");
        }
    }
    m_registeredFunctions.push_back({decl, functionPointer, convention});
    for (auto* engine : m_engines)
    {
        const int result = engine->engine->RegisterGlobalFunction(
            decl.c_str(), asPtr(functionPointer), mapConvention(convention));
        if (result < 0)
        {
            throw std::invalid_argument(
                "angelscript: RegisterGlobalFunction failed");
        }
    }
}

void AngelScriptBackend::registerGlobalProperty(const std::string& decl,
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
    m_globalProperties.push_back({decl, pointer});
    for (auto* engine : m_engines)
    {
        const int result = engine->engine->RegisterGlobalProperty(
            decl.c_str(), pointer);
        if (result < 0)
        {
            throw std::invalid_argument(
                "angelscript: RegisterGlobalProperty failed");
        }
    }
}

// -- Execution ---------------------------------------------------------------

ScriptContext* AngelScriptBackend::createContext(ScriptEngine* engine)
{
    if (m_shutdown)
    {
        note("createContext after shutdown");
        return nullptr;
    }
    AsScriptEngine* e = engineOf(engine);
    if (e == nullptr)
    {
        note("createContext(foreign engine)");
        return nullptr;
    }
    auto* asContext = e->engine->CreateContext();
    if (asContext == nullptr)
    {
        throw std::runtime_error("angelscript: context creation failed");
    }
    auto* context = new AsContext;
    context->owner = e;
    context->context = asContext;
    e->contexts.push_back(context);
    return context;
}

void AngelScriptBackend::releaseContext(ScriptContext* context)
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
    AsScriptEngine* owner = contextOwner(context);
    if (owner == nullptr)
    {
        note("releaseContext(foreign or double release)");
        return;
    }
    auto* c = static_cast<AsContext*>(context);
    owner->contexts.erase(std::find(owner->contexts.begin(),
                                    owner->contexts.end(), c));
    c->context->Release();
    delete c;
}

bool AngelScriptBackend::prepare(ScriptContext* context,
                                 ScriptFunction* function)
{
    if (m_shutdown)
    {
        note("prepare after shutdown");
        return false;
    }
    AsContext* c = contextOf(context);
    if (c == nullptr)
    {
        note("prepare(foreign context)");
        return false;
    }
    if (function == nullptr)
    {
        note("prepare(null function)");
        return false;
    }
    AsFunction* f = functionOf(function);
    if (f == nullptr)
    {
        note("prepare(foreign function)");
        return false;
    }
    const int result = c->context->Prepare(f->function);
    if (result < 0)
    {
        m_error = "angelscript: Prepare failed";
        return false;
    }
    return true;
}

void AngelScriptBackend::setArgumentInt(ScriptContext* context,
                                        std::uint32_t index,
                                        std::int32_t value)
{
    if (m_shutdown)
    {
        note("setArgumentInt after shutdown");
        return;
    }
    AsContext* c = contextOf(context);
    if (c == nullptr)
    {
        note("setArgumentInt(foreign context)");
        return;
    }
    const int result = c->context->SetArgDWord(
        index, static_cast<asDWORD>(value));
    if (result < 0)
    {
        note("setArgumentInt: SDK error");
    }
}

void AngelScriptBackend::setArgumentFloat(ScriptContext* context,
                                          std::uint32_t index,
                                          float value)
{
    if (m_shutdown)
    {
        note("setArgumentFloat after shutdown");
        return;
    }
    AsContext* c = contextOf(context);
    if (c == nullptr)
    {
        note("setArgumentFloat(foreign context)");
        return;
    }
    const int result = c->context->SetArgFloat(index, value);
    if (result < 0)
    {
        note("setArgumentFloat: SDK error");
    }
}

bool AngelScriptBackend::execute(ScriptContext* context)
{
    if (m_shutdown)
    {
        note("execute after shutdown");
        return false;
    }
    AsContext* c = contextOf(context);
    if (c == nullptr)
    {
        note("execute(foreign context)");
        return false;
    }
    const int result = c->context->Execute();
    if (result == asEXECUTION_EXCEPTION)
    {
        const char* text = c->context->GetExceptionString();
        m_error = "angelscript: script exception: ";
        m_error += text != nullptr ? text : "";
        return false;
    }
    if (result != asEXECUTION_FINISHED)
    {
        m_error = "angelscript: Execute did not finish";
        return false;
    }
    m_error.clear();
    return true;
}

std::int32_t AngelScriptBackend::returnInt(ScriptContext* context) const
{
    AsContext* c = contextOf(context);
    if (c == nullptr)
    {
        return 0;
    }
    return static_cast<std::int32_t>(c->context->GetReturnDWord());
}

float AngelScriptBackend::returnFloat(ScriptContext* context) const
{
    AsContext* c = contextOf(context);
    if (c == nullptr)
    {
        return 0.0f;
    }
    return c->context->GetReturnFloat();
}

void* AngelScriptBackend::globalProperty(const std::string& decl) const
{
    for (const auto& registered : m_globalProperties)
    {
        if (registered.decl == decl)
        {
            return registered.pointer;
        }
    }
    return nullptr;
}

std::string AngelScriptBackend::lastError() const
{
    return m_error;
}

}  // namespace grace::script
