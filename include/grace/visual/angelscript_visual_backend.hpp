#pragma once
#include <cstdint>
#include <string>
#include "grace/visual/visual_backend.hpp"

namespace grace::script {
class ScriptBackend;
class ScriptEngine;
class ScriptModule;
}

namespace grace::visual {

/// Opaque handle to a block registry created by a backend.
class VisualRegistry;
/// Opaque handle to a graph created by a backend.
class VisualGraph;

class AsRegistry;
class AsGraph;

/// The 10b adapter: compiles visual-script graphs to AngelScript through the
/// script contract's registration surface (decision 34). The adapter owns
/// its registry and graph handles (real instances of the opaque classes,
/// the 9a lesson) and never owns the script backend: the application owns
/// it, so the visual layer registers its blocks through the same surface
/// the application registers its types through (architecture 4.2).
///
/// The mapping: the application registers each block's native function into
/// the script backend through the contract's registerFunction surface, with
/// the declaration "void BlockName()" (the block's native function name is
/// the block's name). compile() generates deterministic AngelScript source
/// for the graph: the entry point is a function whose declaration is
/// entryPointDecl, and the flow chain calls each block's native function in
/// connection order. The source is handed to the script backend as a module
/// named after the graph, built, and the entry point resolved by decl.
///
/// A graph compiles once: a second compile() of the same graph is refused
/// with a note in lastError(). The adapter creates one script engine on the
/// first compile and releases it on shutdown.
class AngelScriptVisualBackend final : public VisualBackend
{
public:
    std::string describe() const override;
    void shutdown() override;
    std::uint64_t violationCount() const noexcept override;

    VisualRegistry* createRegistry(const std::string& name) override;
    void releaseRegistry(VisualRegistry* registry) override;
    void registerBlock(VisualRegistry* registry,
                       const BlockDesc& desc) override;
    void registerPin(VisualRegistry* registry,
                     const std::string& blockName,
                     const PinDesc& pin) override;

    VisualGraph* createGraph(VisualRegistry* registry,
                             const GraphDesc& desc) override;
    void releaseGraph(VisualGraph* graph) override;
    bool compile(VisualGraph* graph,
                 const std::string& entryPointDecl) override;

    std::string lastError() const override;

    /// Sets the script backend the adapter compiles graphs through. The
    /// adapter never owns it: registration happens before any compile, so
    /// setScriptBackend is only accepted while nothing was compiled yet; a
    /// later change is refused with a note in lastError().
    void setScriptBackend(grace::script::ScriptBackend* backend);

    /// The script engine the adapter compiled graphs into. Null before the
    /// first compile() and after shutdown(). The adapter owns the engine;
    /// the application must not release it, but it uses the handle for
    /// createContext() on the script backend.
    grace::script::ScriptEngine* compiledEngine() const;

    /// The script module a compiled graph lives in. Null before the graph's
    /// first compile() and after shutdown(). The adapter owns the module;
    /// the application must not release it, but it uses the handle for
    /// findFunction() on the script backend.
    grace::script::ScriptModule* compiledModule(VisualGraph* graph) const;

private:
    void note(const std::string& message);
    AsRegistry* registryOf(VisualRegistry* registry) const;
    AsGraph* graphOf(VisualGraph* graph) const;
    AsRegistry* registryOfGraph(AsGraph* graph) const;

    bool m_shutdown = false;
    std::uint64_t m_violations = 0;
    std::string m_error;

    grace::script::ScriptBackend* m_scriptBackend = nullptr;
    grace::script::ScriptEngine* m_engine = nullptr;
    grace::script::ScriptModule* m_module = nullptr;
    std::vector<AsRegistry*> m_registries;
};

}  // namespace grace::visual
