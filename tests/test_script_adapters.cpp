#include "grace/script/angelscript_backend.hpp"
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

// -- Native functions and types registered into the script engine ------------

int g_counter = 0;

void nativeTick()
{
    ++g_counter;
}

int nativeAdd(int a, int b)
{
    return a + b;
}

struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

void Vec2Init(Vec2* self, float x, float y)
{
    self->x = x;
    self->y = y;
}

float Vec2GetX(const Vec2* self)
{
    return self->x;
}

void Vec2SetY(Vec2* self, float y)
{
    self->y = y;
}

// -- The registration surface ------------------------------------------------

/// Registers the native surface into the backend: one value type, three
/// methods on it, two global functions and one global property.
void registerSurface(grace::script::AngelScriptBackend& backend)
{
    backend.registerType("Vec2", sizeof(Vec2),
                         grace::script::TypeFlags::PlainValue);
    backend.registerMethod("Vec2", "void init(float, float)",
                           reinterpret_cast<void*>(&Vec2Init),
                           grace::script::CallingConvention::CdeclObjFirst);
    backend.registerMethod("Vec2", "float get_x() const",
                           reinterpret_cast<void*>(&Vec2GetX),
                           grace::script::CallingConvention::CdeclObjFirst);
    backend.registerMethod("Vec2", "void set_y(float)",
                           reinterpret_cast<void*>(&Vec2SetY),
                           grace::script::CallingConvention::CdeclObjFirst);
    backend.registerFunction("void nativeTick()",
                             reinterpret_cast<void*>(&nativeTick),
                             grace::script::CallingConvention::Cdecl);
    backend.registerFunction("int nativeAdd(int, int)",
                             reinterpret_cast<void*>(&nativeAdd),
                             grace::script::CallingConvention::Cdecl);
    backend.registerGlobalProperty("int g_counter",
                                   reinterpret_cast<void*>(&g_counter));
}

const char* surfaceScript =
    "void run() {\n"
    "  Vec2 v; v.init(3.0, 4.0);\n"
    "  nativeTick();\n"
    "  nativeTick();\n"
    "  g_counter = nativeAdd(g_counter, 10);\n"
    "  g_counter = v.get_x() > 0.0 ? g_counter : 0;\n"
    "}\n";

const char* argumentScript =
    "int twice(int a) { return a * 2; }\n"
    "float sum(float a, float b) { return a + b; }\n";

}  // namespace

using grace::script::AngelScriptBackend;
using grace::script::EngineDesc;
using grace::script::ScriptContext;
using grace::script::ScriptEngine;
using grace::script::ScriptFunction;
using grace::script::ScriptModule;

/// Registration surface: registerType, registerMethod, registerFunction and
/// registerGlobalProperty all succeed, and globalProperty() round-trips the
/// registered pointer.
static void testRegistration()
{
    AngelScriptBackend backend;
    EngineDesc desc;
    desc.name = "adapter";
    ScriptEngine* engine = backend.createEngine(desc);
    check(engine != nullptr, "engine created");
    ScriptModule* module = backend.createModule(engine, "main");
    check(module != nullptr, "module created");

    registerSurface(backend);
    check(backend.violationCount() == 0u, "violations after registration");

    void* property = backend.globalProperty("int g_counter");
    check(property == reinterpret_cast<void*>(&g_counter),
          "globalProperty round-trips the registered pointer");

    // A duplicate registration throws.
    bool threw = false;
    try
    {
        backend.registerFunction("void nativeTick()",
                                 reinterpret_cast<void*>(&nativeTick),
                                 grace::script::CallingConvention::Cdecl);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    check(threw, "function decl: duplicate throws");

    // A method on an unregistered type throws.
    bool threw2 = false;
    try
    {
        backend.registerMethod("Unknown", "void f()",
                               reinterpret_cast<void*>(&Vec2Init),
                               grace::script::CallingConvention::CdeclObjFirst);
    }
    catch (const std::invalid_argument&)
    {
        threw2 = true;
    }
    check(threw2, "type name: not registered throws");

    backend.releaseModule(module);
    backend.releaseEngine(engine);
    backend.shutdown();
    check(backend.violationCount() == 0u, "violations after registration test");
}

/// Compilation: a real script builds, functions are found by declaration,
/// and a broken script fails with the SDK's message through lastError().
static void testBuildAndExecute()
{
    AngelScriptBackend backend;
    EngineDesc desc;
    desc.name = "adapter";
    ScriptEngine* engine = backend.createEngine(desc);
    ScriptModule* module = backend.createModule(engine, "main");
    registerSurface(backend);

    backend.addScriptSource(module, "main", surfaceScript);
    check(backend.build(module), "script builds");
    check(backend.violationCount() == 0u, "violations after build");

    ScriptFunction* run = backend.findFunction(module, "void run()");
    check(run != nullptr, "void run() found by declaration");

    g_counter = 0;
    ScriptContext* context = backend.createContext(engine);
    check(context != nullptr, "context created");
    check(backend.prepare(context, run), "prepare run");
    check(backend.execute(context), "execute run");
    check(g_counter == 12, "script called nativeTick twice and nativeAdd(0,10)");
    check(backend.violationCount() == 0u, "violations after execute");

    // Argument passing and return value through the contract.
    ScriptFunction* twice = backend.findFunction(module, "int twice(int)");
    check(twice == nullptr, "absent function not found");

    backend.releaseContext(context);
    backend.releaseModule(module);
    backend.releaseEngine(engine);
    backend.shutdown();
    check(backend.violationCount() == 0u, "violations after execute test");
}

/// Argument passing and return values through the contract: setArgumentInt
/// and setArgumentFloat feed the prepared function, returnInt and
/// returnFloat read the result.
static void testArgumentPassing()
{
    AngelScriptBackend backend;
    EngineDesc desc;
    desc.name = "adapter";
    ScriptEngine* engine = backend.createEngine(desc);
    ScriptModule* module = backend.createModule(engine, "main");
    registerSurface(backend);

    backend.addScriptSource(module, "main", argumentScript);
    check(backend.build(module), "script builds");
    ScriptFunction* twice = backend.findFunction(module, "int twice(int)");
    check(twice != nullptr, "int twice(int) found by declaration");
    ScriptFunction* sum = backend.findFunction(module, "float sum(float, float)");
    check(sum != nullptr, "float sum(float, float) found by declaration");

    ScriptContext* context = backend.createContext(engine);
    check(context != nullptr, "context created");
    check(backend.prepare(context, twice), "prepare twice");
    backend.setArgumentInt(context, 0, 21);
    check(backend.execute(context), "execute twice");
    check(backend.returnInt(context) == 42, "twice(21) == 42");

    check(backend.prepare(context, sum), "prepare sum");
    backend.setArgumentFloat(context, 0, 1.5f);
    backend.setArgumentFloat(context, 1, 2.25f);
    check(backend.execute(context), "execute sum");
    check(near(backend.returnFloat(context), 3.75f, 1e-5f),
          "sum(1.5, 2.25) == 3.75");
    check(backend.violationCount() == 0u, "violations after argument passing");

    backend.releaseContext(context);
    backend.releaseModule(module);
    backend.releaseEngine(engine);
    backend.shutdown();
    check(backend.violationCount() == 0u,
          "violations after argument passing test");
}

/// Build errors: a broken script fails with the SDK's message and the
/// execution is never run.
static void testBuildError()
{
    AngelScriptBackend backend;
    EngineDesc desc;
    desc.name = "adapter";
    ScriptEngine* engine = backend.createEngine(desc);
    ScriptModule* module = backend.createModule(engine, "main");
    registerSurface(backend);

    backend.addScriptSource(module, "main", "int x = ;\n");
    check(!backend.build(module), "broken script fails to build");
    std::string message = backend.lastError();
    check(!message.empty(), "build error message is not empty");
    check(backend.violationCount() == 0u, "violations after build error");

    backend.releaseModule(module);
    backend.releaseEngine(engine);
    backend.shutdown();
    check(backend.violationCount() == 0u, "violations after build error test");
}

/// Script exception: a script that throws fails execute() and lastError()
/// carries the exception message.
static void testScriptException()
{
    AngelScriptBackend backend;
    EngineDesc desc;
    desc.name = "adapter";
    ScriptEngine* engine = backend.createEngine(desc);
    ScriptModule* module = backend.createModule(engine, "main");

    backend.addScriptSource(module, "main",
        "void boom() {\n"
        "  int x = 0;\n"
        "  int y = 1 / x;\n"
        "}\n");
    check(backend.build(module), "script builds");
    ScriptFunction* boom = backend.findFunction(module, "void boom()");
    check(boom != nullptr, "void boom() found by declaration");
    ScriptContext* context = backend.createContext(engine);
    check(backend.prepare(context, boom), "prepare boom");
    check(!backend.execute(context), "script exception fails execute");
    check(backend.lastError().find("exception") != std::string::npos,
          "lastError carries the exception message");
    check(backend.violationCount() == 0u, "violations after exception");

    backend.releaseContext(context);
    backend.releaseModule(module);
    backend.releaseEngine(engine);
    backend.shutdown();
    check(backend.violationCount() == 0u, "violations after exception test");
}

int main()
{
    testRegistration();
    testBuildAndExecute();
    testArgumentPassing();
    testBuildError();
    testScriptException();
    std::printf("script_adapters: %d checks, %d failures\n",
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
