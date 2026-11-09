#pragma once
#include <cstdint>
#include <string>
#include "grace/visual/visual_types.hpp"

namespace grace::visual {

class VisualRegistry;
class VisualGraph;

/// Platform-neutral visual-scripting contract.
///
/// The documented architecture describes the visual scripting system as a
/// graph representation whose blocks are types registered into the script
/// engine; each graph compiles to a callable entry point (architecture
/// §4.2). The contract is the engine-owned vocabulary and the compile
/// operation; the adapter maps it to the script host. No host-language type
/// crosses here: the same rule the physics contract applies to Px*/bt*/b2*
/// and the script contract applies to as* types.
///
/// The registry is the vocabulary: a graph uses only blocks registered in
/// its registry. The contract records the vocabulary; the adapter maps it
/// to host types. The contract never interprets the semantics of a block.
///
/// Protocol: mutating calls made after shutdown() are protocol violations
/// and are counted through violationCount(); a repeated shutdown() is a
/// violation too but must stay safe.
class VisualBackend
{
public:
    virtual ~VisualBackend() = default;

    /// Human-readable backend description, for example "null (headless)".
    virtual std::string describe() const = 0;

    /// Releases backend resources. Exactly one successful call per backend
    /// lifetime; calls after shutdown are protocol violations.
    virtual void shutdown() = 0;

    /// Number of protocol violations observed so far.
    virtual std::uint64_t violationCount() const noexcept = 0;

    // -- Registry ---------------------------------------------------------------
    //
    // The registry is the vocabulary: a graph uses only blocks registered
    // in its registry. Blocks are types registered into the script engine
    // through the script contract (architecture §4.2). Registration calls
    // are made before any graph is compiled: a registration after a compile
    // is a protocol violation.
    /// Creates a block registry. Throws std::invalid_argument if the name
    /// is empty or already used.
    virtual VisualRegistry* createRegistry(const std::string& name) = 0;
    /// Destroys a registry and every graph it owns. Releasing a null
    /// pointer or a foreign registry is a protocol violation but stays
    /// safe.
    virtual void releaseRegistry(VisualRegistry* registry) = 0;

    /// Registers a block by description. Throws std::invalid_argument if
    /// the name is empty or already used, the description does not
    /// validate, or the registry is not owned by this backend.
    virtual void registerBlock(VisualRegistry* registry,
                               const BlockDesc& desc) = 0;
    /// Registers a pin on a registered block. Throws std::invalid_argument
    /// if the block is not registered.
    virtual void registerPin(VisualRegistry* registry,
                             const std::string& blockName,
                             const PinDesc& pin) = 0;

    // -- Graph ------------------------------------------------------------------
    //
    // A graph is a set of nodes (block instances) and edges (connections
    // between pins). The graph validates: node names unique, connections
    // reference existing nodes and pins.
    /// Creates a graph. Throws std::invalid_argument if the description
    /// does not validate, or the registry is not owned by this backend.
    virtual VisualGraph* createGraph(VisualRegistry* registry,
                                     const GraphDesc& desc) = 0;
    /// Destroys a graph. Releasing a null pointer or a foreign graph is a
    /// protocol violation but stays safe.
    virtual void releaseGraph(VisualGraph* graph) = 0;

    // -- Compilation ------------------------------------------------------------
    //
    // A graph compiles to a callable entry point through the script host:
    // the visual layer registers its blocks through the same surface, so
    // the graph compiles to a callable entry point (architecture §4.2).
    // Errors are reported through lastError() and never throw.
    virtual bool compile(VisualGraph* graph,
                         const std::string& entryPointDecl) = 0;

    // -- Errors -----------------------------------------------------------------
    /// The last error message, for example a compile error. Cleared on the
    /// next successful call.
    virtual std::string lastError() const = 0;
};

}  // namespace grace::visual
