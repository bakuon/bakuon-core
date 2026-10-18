#include <bakuon/core/bakuon.h>

#include <entt/core/hashed_string.hpp>

#include <string>

namespace bakuon {

BAKUON_CORE_API std::string_view versionString() noexcept
{
    return "0.1.0";
}

BAKUON_CORE_API std::size_t stringHashed(std::string_view str) noexcept
{
    return entt::hashed_string(std::string(str).c_str(), str.size()).value();
}

} // namespace bakuon
