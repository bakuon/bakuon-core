#pragma once

/** @file b_names.h  INTERNAL. Shared name validation. */

#include <cstddef>
#include <cstring>

namespace bakuon::core {

inline bool is_valid_name(const char* s) noexcept
{
    if (s == nullptr) {
        return false;
    }
    const std::size_t n = std::strlen(s);
    return n > 0 && n <= 255;
}

} // namespace bakuon::core
