#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace grace::visual {

/// Opaque handle to a block registry created by a backend.
class VisualRegistry
{
public:
    virtual ~VisualRegistry() = default;
};

/// Opaque handle to a graph created by a backend.
class VisualGraph
{
public:
    virtual ~VisualGraph() = default;
};

/// Direction of a pin.
enum class PinDirection : std::uint8_t
{
    Input = 0,
    Output = 1,
};

/// Data type carried by a pin. The vocabulary is the block-level one: value
/// pins carry data between blocks, flow pins carry control. The names are
/// plain C++ names; no host-language type crosses here even for built-in
/// ones ("int", "float"), because the contract talks in decl strings, not
/// bytes.
enum class PinType : std::uint8_t
{
    Flow = 0,
    Int32,
    Float32,
    Bool,
    String,
};

/// How a block is invoked. Execution blocks drive the flow (they have flow
/// pins); expression blocks are evaluated as data (they have none).
enum class BlockKind : std::uint8_t
{
    Flow = 0,
    Expression,
};

/// A pin declaration: a name, a direction and a type.
struct PinDesc final
{
    std::string name;
    PinDirection direction = PinDirection::Input;
    PinType type = PinType::Flow;
};

/// A block declaration: a kind, a name and a list of pins. Blocks are types
/// registered into the script engine through the script contract
/// (architecture §4.2): the visual layer registers its blocks through the
/// same surface, so a graph compiles to a callable entry point. The
/// contract records the block vocabulary; the adapter maps it to host
/// types. The contract never interprets the semantics of a block.
struct BlockDesc final
{
    std::string name;
    BlockKind kind = BlockKind::Flow;
    std::vector<PinDesc> pins;

    /// Message-string style validation: empty name invalid, empty pin name
    /// invalid, duplicate pin names invalid, flow pins must be paired
    /// (exactly one input and one output flow pin per execution block).
    bool validate(std::string& message) const;
};

/// A node is an instance of a block from the registry: a block name and a
/// node name.
struct NodeDesc final
{
    std::string blockName;
    std::string name;
};

/// A connection is an edge between two pins: from (node, pin) to
/// (node, pin).
struct ConnectionDesc final
{
    std::string fromNode;
    std::string fromPin;
    std::string toNode;
    std::string toPin;
};

/// A graph description: a name, a list of nodes and a list of connections.
/// The registry is the vocabulary: a graph uses only blocks from the
/// registry. The contract records the vocabulary; the adapter maps it to
/// host types. The contract never interprets the semantics of a block.
struct GraphDesc final
{
    std::string name;
    std::vector<NodeDesc> nodes;
    std::vector<ConnectionDesc> connections;

    /// Message-string style validation: empty name invalid, empty node name
    /// invalid, duplicate node names invalid, connection endpoints must
    /// reference existing nodes and pins.
    bool validate(std::string& message) const;
};

}  // namespace grace::visual
