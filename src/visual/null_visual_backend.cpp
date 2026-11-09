#include "grace/visual/null_visual_backend.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace grace::visual {

/// A registry is the vocabulary: a set of blocks by name. A graph uses
/// only blocks from its registry.
class NullVisualRegistry final : public VisualRegistry
{
public:
    std::string name;
    std::vector<BlockDesc> blocks;
    std::vector<PinDesc> pins;
    std::vector<NullVisualGraph*> graphs;
};

/// A graph validates against the registry's vocabulary: node names unique,
/// connections reference existing nodes and pins.
class NullVisualGraph final : public VisualGraph
{
public:
    std::string name;
    GraphDesc desc;
};

std::string NullVisualBackend::describe() const
{
    return "null (headless)";
}

void NullVisualBackend::shutdown()
{
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    m_shutdown = true;
    for (NullVisualRegistry* registry : m_registries)
    {
        delete registry;
    }
    m_registries.clear();
}

std::uint64_t NullVisualBackend::violationCount() const noexcept
{
    return m_violations;
}

NullVisualRegistry* NullVisualBackend::registryOf(VisualRegistry* registry) const
{
    if (registry == nullptr)
    {
        return nullptr;
    }
    for (NullVisualRegistry* existing : m_registries)
    {
        if (existing == registry)
        {
            return existing;
        }
    }
    return nullptr;
}

NullVisualGraph* NullVisualBackend::graphOf(VisualGraph* graph) const
{
    if (graph == nullptr)
    {
        return nullptr;
    }
    for (NullVisualRegistry* registry : m_registries)
    {
        for (NullVisualGraph* candidate : registry->graphs)
        {
            if (candidate == graph)
            {
                return candidate;
            }
        }
    }
    return nullptr;
}

void NullVisualBackend::record(std::string event)
{
    m_events.push_back(std::move(event));
}

void NullVisualBackend::note(const std::string& message)
{
    m_error = message;
}

VisualRegistry* NullVisualBackend::createRegistry(const std::string& name)
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
    for (NullVisualRegistry* existing : m_registries)
    {
        if (existing->name == name)
        {
            throw std::invalid_argument("visual: registry name '" + name
                                        + "' already used");
        }
    }
    auto* registry = new NullVisualRegistry();
    registry->name = name;
    m_registries.push_back(registry);
    record("createRegistry(name=" + name + ")");
    return registry;
}

void NullVisualBackend::releaseRegistry(VisualRegistry* registry)
{
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    NullVisualRegistry* owned = registryOf(registry);
    if (owned == nullptr)
    {
        ++m_violations;
        return;
    }
    for (NullVisualGraph* graph : owned->graphs)
    {
        delete graph;
    }
    m_registries.erase(std::remove(m_registries.begin(), m_registries.end(),
                                   owned),
                       m_registries.end());
    delete owned;
}

void NullVisualBackend::registerBlock(VisualRegistry* registry,
                                      const BlockDesc& desc)
{
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    NullVisualRegistry* owned = registryOf(registry);
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
    record("registerBlock(registry=" + owned->name + ", block=" + desc.name
           + ", pins=" + std::to_string(desc.pins.size()) + ")");
}

void NullVisualBackend::registerPin(VisualRegistry* registry,
                                    const std::string& blockName,
                                    const PinDesc& pin)
{
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    NullVisualRegistry* owned = registryOf(registry);
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
                throw std::invalid_argument("visual: pin name must not be empty");
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
            record("registerPin(registry=" + owned->name + ", block="
                   + blockName + ", pin=" + pin.name + ")");
            return;
        }
    }
    throw std::invalid_argument("visual: block '" + blockName
                                + "' is not registered");
}

VisualGraph* NullVisualBackend::createGraph(VisualRegistry* registry,
                                            const GraphDesc& desc)
{
    if (m_shutdown)
    {
        ++m_violations;
        return nullptr;
    }
    NullVisualRegistry* owned = registryOf(registry);
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
    auto* graph = new NullVisualGraph();
    graph->name = desc.name;
    graph->desc = desc;
    owned->graphs.push_back(graph);
    record("createGraph(registry=" + owned->name + ", graph=" + desc.name
           + ", nodes=" + std::to_string(desc.nodes.size()) + ")");
    return graph;
}

void NullVisualBackend::releaseGraph(VisualGraph* graph)
{
    if (m_shutdown)
    {
        ++m_violations;
        return;
    }
    NullVisualGraph* owned = graphOf(graph);
    if (owned == nullptr)
    {
        ++m_violations;
        return;
    }
    for (NullVisualRegistry* registry : m_registries)
    {
        auto& graphs = registry->graphs;
        graphs.erase(std::remove(graphs.begin(), graphs.end(), owned),
                     graphs.end());
    }
    delete owned;
}

bool NullVisualBackend::compile(VisualGraph* graph,
                                const std::string& entryPointDecl)
{
    if (m_shutdown)
    {
        ++m_violations;
        return false;
    }
    NullVisualGraph* owned = graphOf(graph);
    if (owned == nullptr)
    {
        ++m_violations;
        return false;
    }
    record("compile(graph=" + owned->name + ", entryPoint=" + entryPointDecl
           + ")");
    m_error = "null visual backend: no language, cannot compile graphs";
    return false;
}

std::string NullVisualBackend::lastError() const
{
    return m_error;
}

}  // namespace grace::visual
