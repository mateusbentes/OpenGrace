// The game module's data types: the validation of the agent and timer
// descriptions.
#include "grace/game/game_types.hpp"
namespace grace {
namespace game {
bool AgentDesc::validate(std::string& message) const
{
    if (name.empty())
    {
        message = "an agent's name is empty";
        return false;
    }
    if (speed < 0.0f)
    {
        message = "an agent's speed is negative";
        return false;
    }
    if (behaviour == BehaviourKind::Patrol && waypoints.size() < 2u)
    {
        message = "a patrol agent's waypoint list has fewer than two "
                  "waypoints";
        return false;
    }
    return true;
}
bool TimerDesc::validate(std::string& message) const
{
    if (name.empty())
    {
        message = "a timer's name is empty";
        return false;
    }
    if (duration <= 0.0f)
    {
        message = "a timer's duration is not positive";
        return false;
    }
    return true;
}
}  // namespace game
}  // namespace grace
