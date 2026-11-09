#include "grace/visual/visual_types.hpp"

namespace grace::visual {

bool BlockDesc::validate(std::string& message) const
{
    if (name.empty())
    {
        message = "block name must not be empty";
        return false;
    }
    if (pins.empty())
    {
        message = "block '" + name + "': a block must have at least one pin";
        return false;
    }
    bool hasFlowInput = false;
    bool hasFlowOutput = false;
    for (const PinDesc& pin : pins)
    {
        if (pin.name.empty())
        {
            message = "block '" + name + "': pin name must not be empty";
            return false;
        }
        for (const PinDesc& other : pins)
        {
            if (&other != &pin && other.name == pin.name)
            {
                message = "block '" + name + "': duplicate pin name '"
                          + pin.name + "'";
                return false;
            }
        }
        if (pin.type == PinType::Flow)
        {
            if (pin.direction == PinDirection::Input)
            {
                hasFlowInput = true;
            }
            else
            {
                hasFlowOutput = true;
            }
        }
    }
    if (kind == BlockKind::Flow && !hasFlowInput && !hasFlowOutput)
    {
        message = "block '" + name + "': an execution block must have at "
                  "least one flow pin";
        return false;
    }
    if (kind == BlockKind::Expression && (hasFlowInput || hasFlowOutput))
    {
        message = "block '" + name + "': an expression block must not have "
                  "flow pins";
        return false;
    }
    return true;
}

bool GraphDesc::validate(std::string& message) const
{
    if (name.empty())
    {
        message = "graph name must not be empty";
        return false;
    }
    for (const NodeDesc& node : nodes)
    {
        if (node.name.empty())
        {
            message = "graph '" + name + "': node name must not be empty";
            return false;
        }
        for (const NodeDesc& other : nodes)
        {
            if (&other != &node && other.name == node.name)
            {
                message = "graph '" + name + "': duplicate node name '"
                          + node.name + "'";
                return false;
            }
        }
    }
    for (const ConnectionDesc& connection : connections)
    {
        bool fromNode = false;
        bool toNode = false;
        for (const NodeDesc& node : nodes)
        {
            fromNode = fromNode || (node.name == connection.fromNode);
            toNode = toNode || (node.name == connection.toNode);
        }
        if (!fromNode || !toNode)
        {
            message = "graph '" + name + "': connection references a node "
                      "that is not in the graph";
            return false;
        }
    }
    return true;
}

}  // namespace grace::visual
