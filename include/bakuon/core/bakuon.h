#pragma once

#include <cstddef>
#include <stdint.h>

namespace bakuon {

[[nodiscard]] const char* versionString() noexcept;

[[nodiscard]] std::size_t stringHashed(const char*, std::size_t) noexcept;

} // namespace bakuon
