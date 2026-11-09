#include "grace/script/angelscript_backend.hpp"
#include "grace/visual/angelscript_visual_backend.hpp"

#include <cstdio>

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

// -- Native functions the visual layer registers through the script contract
// -- (architecture 4.2): a block's native function name is the block's name.

int g_ticks = 0;

void Start()
{
    ++g_ticks;
}

/// Registers the block natives into the script backend through the
/// contract's registerFunction surface.
void registerBlockNatives(grace::script::AngelScriptBackend& backend)
{
    backend.registerFunction("void Start()",
                             reinterpret_cast<void*>(&Start),
                             grace::script::CallingConvention::Cdecl);
}

/// Registers the block into the visual registry and builds a one-node graph.
void registerStartBlock(grace::visual::AngelScriptVisualBackend& backend,
                        grace::visual::VisualRegistry* registry)
{
    grace::visual::BlockDesc start;
    start.name = "Start";
    start.kind = grace::visual::BlockKind::Flow;
    start.pins.push_back({"out", grace::visual::PinDirection::Output,
                          grace::visual::PinType::Flow});
    backend.registerBlock(registry, start);
}

}  // namespace

/// Compilation: a graph compiles to a callable entry point through the
/// script contract's surface, and the block native runs once on execute.
static void testCompileAndExecute()
{
    grace::script::AngelScriptBackend script;
    grace::visual::AngelScriptVisualBackend visual;
    visual.setScriptBackend(&script);
    registerBlockNatives(script);

    grace::visual::VisualRegistry* registry = visual.createRegistry("blocks");
    registerStartBlock(visual, registry);

    grace::visual::GraphDesc desc;
    desc.name = "main";
    desc.nodes.push_back({"Start", "begin"});
    grace::visual::GraphDesc descRef = desc;
    grace::visual::VisualGraph* graph = visual.createGraph(registry, descRef);
    check(graph != nullptr, "graph created");

    check(visual.compile(graph, "void graphEntry()"),
          "compile() compiles the graph to a callable entry point");
    check(visual.compiledModule(graph) != nullptr,
          "the compiled graph lives in a module");
    grace::script::ScriptEngine* engine = visual.compiledEngine();
    check(engine != nullptr, "the adapter's engine exists");
    grace::script::ScriptFunction* entry =
        script.findFunction(visual.compiledModule(graph), "void graphEntry()");
    check(entry != nullptr, "the entry point resolves by declaration");

    g_ticks = 0;
    grace::script::ScriptContext* context = script.createContext(engine);
    check(script.prepare(context, entry), "prepare the entry point");
    check(script.execute(context), "execute the entry point");
    check(g_ticks == 1, "the block native ran once");
    check(visual.violationCount() == 0u, "violations after compile");

    script.releaseContext(context);
    visual.shutdown();
    script.shutdown();
    check(visual.violationCount() == 0u, "violations after full lifetime");
    check(script.violationCount() == 0u, "script violations after lifetime");
}

/// A compile without a script backend fails with a deterministic message;
/// a second compile of the same graph is refused.
static void testCompileProtocol()
{
    grace::script::AngelScriptBackend script;
    grace::visual::AngelScriptVisualBackend visual;
    registerBlockNatives(script);

    grace::visual::VisualRegistry* registry = visual.createRegistry("blocks");
    registerStartBlock(visual, registry);
    grace::visual::GraphDesc desc;
    desc.name = "main";
    desc.nodes.push_back({"Start", "begin"});
    grace::visual::VisualGraph* graph = visual.createGraph(registry, desc);

    check(!visual.compile(graph, "void graphEntry()"),
          "compile() without a script backend fails");
    check(visual.lastError().find("no script backend set")
              != std::string::npos,
          "lastError() carries the deterministic message");
    check(visual.violationCount() == 0u,
          "a missing script backend is not a violation");

    visual.setScriptBackend(&script);
    check(visual.compile(graph, "void graphEntry()"),
          "compile() succeeds once the script backend is set");
    check(!visual.compile(graph, "void graphEntry()"),
          "a second compile of the same graph is refused");
    check(visual.lastError().find("already compiled") != std::string::npos,
          "lastError() carries the refused message");
    check(visual.violationCount() == 0u,
          "a refused compile is not a violation");

    visual.shutdown();
    script.shutdown();
    check(visual.violationCount() == 0u, "violations after full lifetime");
}

/// A block native that is not registered into the script backend fails the
/// module build, and the module is released on the failure.
static void testCompileFailureReleases()
{
    grace::script::AngelScriptBackend script;
    grace::visual::AngelScriptVisualBackend visual;
    visual.setScriptBackend(&script);

    grace::visual::VisualRegistry* registry = visual.createRegistry("blocks");
    registerStartBlock(visual, registry);
    grace::visual::GraphDesc desc;
    desc.name = "main";
    desc.nodes.push_back({"Start", "begin"});
    grace::visual::VisualGraph* graph = visual.createGraph(registry, desc);

    check(!visual.compile(graph, "void graphEntry()"),
          "compile() fails when the block native is not registered");
    check(visual.lastError().find("the module build failed")
              != std::string::npos,
          "lastError() carries the script backend's message");
    check(visual.compiledModule(graph) == nullptr,
          "the failed compile leaves no module");
    check(visual.compiledEngine() != nullptr,
          "the engine stays alive after a failed compile");
    check(visual.violationCount() == 0u,
          "a failed compile is not a violation");

    visual.shutdown();
    script.shutdown();
    check(visual.violationCount() == 0u, "violations after full lifetime");
}

/// A second graph with the same name is refused: the module name is the
/// graph's name, and a compile never throws.
static void testDuplicateGraphName()
{
    grace::script::AngelScriptBackend script;
    grace::visual::AngelScriptVisualBackend visual;
    visual.setScriptBackend(&script);
    registerBlockNatives(script);

    grace::visual::VisualRegistry* registry = visual.createRegistry("blocks");
    registerStartBlock(visual, registry);

    grace::visual::GraphDesc desc;
    desc.name = "main";
    desc.nodes.push_back({"Start", "begin"});
    grace::visual::VisualGraph* graph = visual.createGraph(registry, desc);
    check(visual.compile(graph, "void graphEntry()"),
          "the first compile succeeds");

    grace::visual::GraphDesc second;
    second.name = "main";
    second.nodes.push_back({"Start", "begin2"});
    grace::visual::VisualGraph* graph2 = visual.createGraph(registry, second);
    check(graph2 != nullptr, "the second graph is created");
    check(!visual.compile(graph2, "void graphEntry()"),
          "a second compile with the same graph name fails");
    check(visual.lastError().find("module name") != std::string::npos,
          "lastError() carries the module-name message");
    check(visual.violationCount() == 0u,
          "a refused compile is not a violation");

    visual.shutdown();
    script.shutdown();
    check(visual.violationCount() == 0u, "violations after full lifetime");
}

int main()
{
    testCompileAndExecute();
    testCompileProtocol();
    testCompileFailureReleases();
    testDuplicateGraphName();

    std::printf("visual_adapters: %d checks, %d failures\n", g_checks,
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
