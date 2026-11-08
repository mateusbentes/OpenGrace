#include "grace/script/null_script_backend.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* message)
{
    ++g_checks;
    if (!condition)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

bool near(float a, float b, float tolerance)
{
    return (a - b) < tolerance && (b - a) < tolerance;
}

}  // namespace

using grace::script::CallingConvention;
using grace::script::EngineDesc;
using grace::script::NullScriptBackend;
using grace::script::ScriptContext;
using grace::script::ScriptEngine;
using grace::script::ScriptFunction;
using grace::script::ScriptModule;
using grace::script::TypeFlags;

/// Protocol-lifetime: createEngine, createModule, release, shutdown.
static void testEngineLifetime()
{
    NullScriptBackend backend;
    EngineDesc desc;
    desc.name = "host";

    // An invalid description throws.
    bool threw = false;
    try
    {
        EngineDesc invalid;
        (void)backend.createEngine(invalid);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "engine desc: invalid throws");

    // A valid engine is created.
    ScriptEngine* engine = backend.createEngine(desc);
    check(engine != nullptr, "engine created");
    check(backend.violationCount() == 0u, "violations after createEngine");

    // A module inside it is created.
    ScriptModule* module = backend.createModule(engine, "main");
    check(module != nullptr, "module created");
    check(backend.violationCount() == 0u, "violations after createModule");

    // A second module with the same name throws.
    bool threw2 = false;
    try
    {
        (void)backend.createModule(engine, "main");
    }
    catch (const std::invalid_argument&)
    {
        threw2 = true;
    }
    check(threw2, "module name: duplicate throws");

    // A module with an empty name throws.
    bool threw3 = false;
    try
    {
        (void)backend.createModule(engine, "");
    }
    catch (const std::invalid_argument&)
    {
        threw3 = true;
    }
    check(threw3, "module name: empty throws");

    // The module is released before its engine.
    backend.releaseModule(module);
    check(backend.violationCount() == 0u, "violations after releaseModule");

    // The engine is released.
    backend.releaseEngine(engine);
    check(backend.violationCount() == 0u, "violations after releaseEngine");

    // Double release is a violation but stays safe.
    backend.releaseEngine(engine);
    check(backend.violationCount() == 1u, "double release counts");

    // Release null is a violation but stays safe.
    backend.releaseEngine(nullptr);
    check(backend.violationCount() == 2u, "release null counts");

    // Double shutdown is a violation but stays safe.
    backend.shutdown();
    backend.shutdown();
    check(backend.violationCount() == 3u, "double shutdown counts");
}

/// Registration surface: registerType, registerMethod, registerFunction,
/// registerGlobalProperty, and the round trip through globalProperty().
static void testRegistration()
{
    NullScriptBackend backend;
    EngineDesc desc;
    desc.name = "host";
    ScriptEngine* engine = backend.createEngine(desc);
    ScriptModule* module = backend.createModule(engine, "main");

    // An empty type name throws.
    bool threw = false;
    try
    {
        backend.registerType("", 4u, TypeFlags::PlainValue);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "type name: empty throws");

    // A valid type is registered.
    backend.registerType("Vec3", 12u, TypeFlags::PlainValue);
    check(backend.violationCount() == 0u, "violations after registerType");

    // A duplicate type name throws.
    bool threw2 = false;
    try
    {
        backend.registerType("Vec3", 12u, TypeFlags::PlainValue);
    }
    catch (const std::invalid_argument&)
    {
        threw2 = true;
    }
    check(threw2, "type name: duplicate throws");

    // A method on an unregistered type throws.
    bool threw3 = false;
    try
    {
        backend.registerMethod("Vec4", "void f()", nullptr, CallingConvention::Cdecl);
    }
    catch (const std::invalid_argument&)
    {
        threw3 = true;
    }
    check(threw3, "method: type not registered throws");

    // A method on a registered type is recorded.
    static int counter = 0;
    backend.registerMethod("Vec3", "void f()", &counter, CallingConvention::Cdecl);
    check(backend.violationCount() == 0u, "violations after registerMethod");

    // A global function is registered.
    backend.registerFunction("void f(int)", &counter, CallingConvention::Cdecl);
    check(backend.violationCount() == 0u, "violations after registerFunction");

    // A duplicate function decl throws.
    bool threw4 = false;
    try
    {
        backend.registerFunction("void f(int)", &counter, CallingConvention::Cdecl);
    }
    catch (const std::invalid_argument&)
    {
        threw4 = true;
    }
    check(threw4, "function decl: duplicate throws");

    // A global property round-trips through the storage pointer.
    static float timeSec = 0.0f;
    backend.registerGlobalProperty("float timeSec", &timeSec);
    check(backend.globalProperty("float timeSec") == &timeSec,
          "property round-trip");
    check(backend.globalProperty("float absent") == nullptr,
          "absent property is null");

    // The registration surface is recorded in order.
    const auto& events = backend.events();
    bool sawType = false;
    bool sawMethod = false;
    bool sawFunction = false;
    bool sawProperty = false;
    for (const auto& event : events)
    {
        if (event.find("registerType(Vec3") == 0u)
        {
            sawType = true;
        }
        if (event.find("registerMethod(Vec3") == 0u)
        {
            sawMethod = true;
        }
        if (event.find("registerFunction(void f(int)") == 0u)
        {
            sawFunction = true;
        }
        if (event.find("registerGlobalProperty(float timeSec") == 0u)
        {
            sawProperty = true;
        }
    }
    check(sawType, "type registered");
    check(sawMethod, "method registered");
    check(sawFunction, "function registered");
    check(sawProperty, "property registered");

    // The null backend cannot compile or run scripts.
    backend.addScriptSource(module, "main", "void f() {}");
    check(!backend.build(module), "null backend: build fails");
    check(backend.lastError().find("no language") != std::string::npos,
          "null backend: build error message");
    check(backend.findFunction(module, "void f()") == nullptr,
          "null backend: findFunction returns null");

    // The build error message is deterministic.
    check(backend.lastError() == "null backend: no language, cannot compile scripts",
          "null backend: deterministic error message");

    // The engine is released, and the module lives inside it.
    backend.releaseModule(module);
    backend.releaseEngine(engine);
}

/// Execution protocol: the null backend cannot execute scripts.
static void testExecution()
{
    NullScriptBackend backend;
    EngineDesc desc;
    desc.name = "host";
    ScriptEngine* engine = backend.createEngine(desc);

    // A context is created.
    ScriptContext* context = backend.createContext(engine);
    check(context != nullptr, "context created");

    // The null backend cannot execute scripts.
    check(!backend.execute(context), "null backend: execute fails");
    check(backend.lastError().find("no language") != std::string::npos,
          "null backend: execute error message");

    // The null backend returns 0 for a finished execution: it cannot
    // produce one.
    check(backend.returnInt(context) == 0, "null backend: returnInt 0");
    check(near(backend.returnFloat(context), 0.0f, 1e-6f),
          "null backend: returnFloat 0");

    // The context is released before the engine.
    backend.releaseContext(context);
    check(backend.violationCount() == 0u, "violations after releaseContext");

    backend.releaseEngine(engine);
}

/// The no-language rule: the null backend cannot compile or run anything.
static void testNoLanguage()
{
    NullScriptBackend backend;
    EngineDesc desc;
    desc.name = "host";
    ScriptEngine* engine = backend.createEngine(desc);
    ScriptModule* module = backend.createModule(engine, "main");

    // The null backend cannot execute scripts.
    check(!backend.execute(nullptr), "null backend: execute fails");
    check(backend.violationCount() == 1u, "execute(null) counts");

    // The module is not compiled by the null backend: the source is
    // recorded and the build fails with the no-language message.
    backend.addScriptSource(module, "main", "void f() {}");
    check(!backend.build(module), "null backend: build fails");
    check(backend.lastError().find("no language") != std::string::npos,
          "null backend: build error message");

    // The context is released before the engine.
    ScriptContext* context = backend.createContext(engine);
    check(context != nullptr, "context created");
    backend.releaseContext(context);
    check(backend.violationCount() == 1u, "violations after releaseContext");

    backend.releaseEngine(engine);
}

int main()
{
    testEngineLifetime();
    testRegistration();
    testExecution();
    testNoLanguage();

    std::printf("script: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
