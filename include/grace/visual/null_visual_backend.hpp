#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "grace/visual/visual_backend.hpp"

namespace grace::visual {

class NullVisualRegistry;
class NullVisualGraph;

/// Headless visual-scripting backend: the contract's protocol reference.
///
/// The null backend records the registration surface and the compile
/// requests and counts protocol violations, but it cannot compile graphs:
/// it has no language, no script host behind it. compile() fails with a
/// deterministic lastError() message, and everything is deterministic: the
/// same call sequence produces the same records and the same violation
/// counts.
///
/// The null backend is the exact reference implementation of the contract's
/// protocol: the registry is the vocabulary, the graph validates against
/// the registry's vocabulary, and compile() is the only language-dependent
/// operation.
class NullVisualBackend final : public VisualBackend
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

    /// Recorded call sequence, in order.
    const std::vector<std::string>& events() const noexcept { return m_events; }

private:
    void record(std::string event);
    void note(const std::string& message);

    NullVisualRegistry* registryOf(VisualRegistry* registry) const;
    NullVisualGraph* graphOf(VisualGraph* graph) const;

    bool m_shutdown = false;
    std::uint64_t m_violations = 0;
    std::string m_error;

    std::vector<NullVisualRegistry*> m_registries;
    std::vector<std::string> m_events;
};

}  // namespace grace::visual
