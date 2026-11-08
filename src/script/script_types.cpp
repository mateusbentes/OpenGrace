#include "grace/script/script_types.hpp"

namespace grace::script {
namespace {

std::string invalid(const char* what, const char* detail)
{
    return std::string(what) + ": " + detail;
}

}  // namespace

bool EngineDesc::validate(std::string& message) const
{
    if (name.empty())
    {
        message = invalid("engine desc", "name must not be empty");
        return false;
    }
    return true;
}

}  // namespace grace::script
