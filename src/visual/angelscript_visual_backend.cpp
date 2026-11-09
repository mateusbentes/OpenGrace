#include "grace/visual/angelscript_visual_backend.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "grace/script/script_backend.hpp"

namespace grace::visual {

/// A registry is the vocabulary: a set of blocks by name. A graph uses
/// only blocks from its registry.
class AsRegistry final : public VisualRegistry
{
public:
    std::string name;
    std::vector<BlockDesc> blocks;
    std::vector<AsGraph*> graphs;
};

/// A graph validates against the registry's vocabulary: node names unique,
/// connections reference existing nodes and pins.
class AsGraph final : public VisualGraph
{
public:
    std::string name;
    GraphDesc desc;
    bool compiled = false;
    grace::script::ScriptModule* module = nullptr;
};

std::string AngelScriptVisualBackend::describe() const
{
    return "angelscript (graph compiler)";
}

void AngelScriptVisualBackend::shutdown()
{
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    m_shutdown = true;
    if (m_engine != nullptr)
    {
        m_scriptBackend->releaseEngine(m_engine);
        m_engine = nullptr;
    }
    for (AsRegistry* registry : m_registries)
    {
        delete registry;
    }
    m_registries.clear();
}

std::uint64_t AngelScriptVisualBackend::violationCount() const noexcept
{
    return m_violations;
}

AsRegistry* AngelScriptVisualBackend::registryOf(
    VisualRegistry* registry) const
{
    if (registry == nullptr)
    {
        return nullptr;
    }
    for (AsRegistry* existing : m_registries)
    {
        if (existing == registry)
        {
            return existing;
        }
    }
    return nullptr;
}

AsGraph* AngelScriptVisualBackend::graphOf(VisualGraph* graph) const
{
    if (graph == nullptr)
    {
        return nullptr;
    }
    for (AsRegistry* registry : m_registries)
    {
        for (AsGraph* candidate : registry->graphs)
        {
            if (candidate == graph)
            {
                return candidate;
            }
        }
    }
    return nullptr;
}

AsRegistry* AngelScriptVisualBackend::registryOfGraph(AsGraph* graph) const
{
    for (AsRegistry* registry : m_registries)
    {
        for (AsGraph* candidate : registry->graphs)
        {
            if (candidate == graph)
            {
                return registry;
            }
        }
    }
    return nullptr;
}

void AngelScriptVisualBackend::note(const std::string& message)
{
    m_error = message;
}

void AngelScriptVisualBackend::setScriptBackend(
    grace::script::ScriptBackend* backend)
{
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    if (m_scriptBackend != nullptr && backend != nullptr
        && backend != m_scriptBackend)
    {
        note("script backend already set and cannot be replaced");
        return;
    }
    m_scriptBackend = backend;
}

grace::script::ScriptEngine* AngelScriptVisualBackend::compiledEngine() const
{
    return m_engine;
}

grace::script::ScriptModule* AngelScriptVisualBackend::compiledModule(
    VisualGraph* graph) const
{
    AsGraph* owned = graphOf(graph);
    if (owned == nullptr)
    {
        return nullptr;
    }
    return owned->module;
}

VisualRegistry* AngelScriptVisualBackend::createRegistry(
    const std::string& name)
{
    if (m_shutdown)
    {
        ++m_violations;
        return nullptr;
    }
    if (name.empty())
    {
        throw std::invalid_argument("visual: registry name must not be empty");
    }
    for (AsRegistry* existing : m_registries)
    {
        if (existing->name == name)
        {
            throw std::invalid_argument("visual: registry name '" + name
                                        + "' already used");
        }
    }
    auto* registry = new AsRegistry();
    registry->name = name;
    m_registries.push_back(registry);
    return registry;
}

void AngelScriptVisualBackend::releaseRegistry(VisualRegistry* registry)
{
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    AsRegistry* owned = registryOf(registry);
    if (owned == nullptr)
    {
        ++m_violations;
        return;
    }
    for (AsGraph* graph : owned->graphs)
    {
        delete graph;
    }
    m_registries.erase(std::remove(m_registries.begin(), m_registries.end(),
                                   owned),
                       m_registries.end());
    delete owned;
}

void AngelScriptVisualBackend::registerBlock(VisualRegistry* registry,
                                             const BlockDesc& desc)
{
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    AsRegistry* owned = registryOf(registry);
    if (owned == nullptr)
    {
        ++m_violations;
        return;
    }
    std::string message;
    if (!desc.validate(message))
    {
        throw std::invalid_argument("visual: " + message);
    }
    for (const BlockDesc& existing : owned->blocks)
    {
        if (existing.name == desc.name)
        {
            throw std::invalid_argument("visual: block '" + desc.name
                                        + "' already registered");
        }
    }
    owned->blocks.push_back(desc);
}

void AngelScriptVisualBackend::registerPin(VisualRegistry* registry,
                                           const std::string& blockName,
                                           const PinDesc& pin)
{
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    AsRegistry* owned = registryOf(registry);
    if (owned == nullptr)
    {
        ++m_violations;
        return;
    }
    for (BlockDesc& block : owned->blocks)
    {
        if (block.name == blockName)
        {
            if (pin.name.empty())
            {
                throw std::invalid_argument(
                    "visual: pin name must not be empty");
            }
            for (const PinDesc& existing : block.pins)
            {
                if (existing.name == pin.name)
                {
                    throw std::invalid_argument("visual: pin '" + pin.name
                                                + "' already registered");
                }
            }
            block.pins.push_back(pin);
            return;
        }
    }
    throw std::invalid_argument("visual: block '" + blockName
                                + "' is not registered");
}

VisualGraph* AngelScriptVisualBackend::createGraph(
    VisualRegistry* registry, const GraphDesc& desc)
{
    if (m_shutdown)
    {
        ++m_violations;
        return nullptr;
    }
    AsRegistry* owned = registryOf(registry);
    if (owned == nullptr)
    {
        ++m_violations;
        return nullptr;
    }
    std::string message;
    if (!desc.validate(message))
    {
        throw std::invalid_argument("visual: " + message);
    }
    for (const NodeDesc& node : desc.nodes)
    {
        bool found = false;
        for (const BlockDesc& block : owned->blocks)
        {
            found = found || (block.name == node.blockName);
        }
        if (!found)
        {
            throw std::invalid_argument("visual: node '" + node.name
                                        + "' uses block '" + node.blockName
                                        + "' which is not registered");
        }
    }
    auto* graph = new AsGraph();
    graph->name = desc.name;
    graph->desc = desc;
    owned->graphs.push_back(graph);
    return graph;
}

void AngelScriptVisualBackend::releaseGraph(VisualGraph* graph)
{
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    AsGraph* owned = graphOf(graph);
    if (owned == nullptr)
    {
        ++m_violations;
        return;
    }
    for (AsRegistry* registry : m_registries)
    {
        auto& graphs = registry->graphs;
        graphs.erase(std::remove(graphs.begin(), graphs.end(), owned),
                     graphs.end());
    }
    delete owned;
}

bool AngelScriptVisualBackend::compile(VisualGraph* graph,
                                       const std::string& entryPointDecl)
{
    if (m_shutdown)
    {
        ++m_violations;
        return false;
    }
    AsGraph* owned = graphOf(graph);
    if (owned == nullptr)
    {
        ++m_violations;
        return false;
    }
    if (m_scriptBackend == nullptr)
    {
        note("no script backend set: setScriptBackend() must be called "
             "before compile()");
        return false;
    }
    if (owned->compiled)
    {
        note("graph '" + owned->name + "' already compiled");
        return false;
    }
    if (entryPointDecl.empty())
    {
        note("entry point decl must not be empty");
        return false;
    }
    // Deterministic source generation: the entry point is a function whose
    // declaration is entryPointDecl, and the flow chain calls each block's
    // native function in connection order. The application registers those
    // native functions into the script backend through the contract's
    // registerFunction surface, so the graph compiles to a callable entry
    // point through the same surface the visual layer registers its blocks
    // through (architecture 4.2).
    AsRegistry* registry = registryOfGraph(owned);
    if (registry == nullptr)
    {
        note("graph '" + owned->name + "' is not in any registry");
        return false;
    }
    std::string source;
    source += entryPointDecl + " {\n";
    for (const NodeDesc& node : owned->desc.nodes)
    {
        source += "  " + node.blockName + "();\n";
    }
    source += "}\n";
    if (m_engine == nullptr)
    {
        grace::script::EngineDesc desc;
        desc.name = "visual";
        m_engine = m_scriptBackend->createEngine(desc);
    }
    if (m_engine == nullptr)
    {
        note("the script backend rejected the engine");
        return false;
    }
    // A compile never throws: the script backend's createModule can throw on
    // a duplicate name and the adapter turns it into lastError().
    grace::script::ScriptModule* module = nullptr;
    try
    {
        module = m_scriptBackend->createModule(m_engine, owned->name);
    }
    catch (const std::invalid_argument&)
    {
        note("a compiled graph already carries the module name '"
             + owned->name + "'");
        return false;
    }
    m_scriptBackend->addScriptSource(module, "graph", source);
    if (!m_scriptBackend->build(module))
    {
        const std::string message = m_scriptBackend->lastError();
        m_scriptBackend->releaseModule(module);
        note("the module build failed: " + message);
        return false;
    }
    if (m_scriptBackend->findFunction(module, entryPointDecl) == nullptr)
    {
        m_scriptBackend->releaseModule(module);
        note("the entry point '" + entryPointDecl + "' was not found in the "
             "module");
        return false;
    }
    owned->compiled = true;
    owned->module = module;
    return true;
}

std::string AngelScriptVisualBackend::lastError() const
{
    return m_error;
}

}  // namespace grace::visual
