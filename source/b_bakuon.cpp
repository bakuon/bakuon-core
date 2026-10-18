#include "b_bakuon.h"

#include <entt/core/hashed_string.hpp>

namespace bakuon {

const char *versionString() noexcept
{
    return "0.1.0";
}

std::size_t stringHashed(const char *str, std::size_t size) noexcept
{
    return entt::hashed_string(str, size).value();
}

} // namespace bakuon
