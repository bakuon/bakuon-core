#pragma once

/** @file status.h  Error codes returned across the ABI. Append-only. */

#include <cstdint>

namespace bakuon::core {

enum class [[nodiscard]] Status : std::int32_t {
    ok                 = 0,
    invalid_argument   = 1, ///< null/empty name, bad size/align, wrong struct_size, ...
    not_alive          = 2, ///< entity is null, destroyed or stale
    not_registered     = 3, ///< unknown component id/name
    already_present    = 4, ///< entity already has this component
    not_present        = 5, ///< entity does not have this component
    type_mismatch      = 6, ///< same name registered with different layout
    id_collision       = 7, ///< different name hashes to an existing id
    out_of_memory      = 8,
    // ---- appended with world/scheduler/plugin ----
    not_found          = 9,  ///< unknown domain / system / plugin
    already_exists     = 10, ///< duplicate domain/system/plugin name
    dependency_missing = 11, ///< a declared system dependency is not registered
    dependency_cycle   = 12,
    busy               = 13, ///< operation not allowed right now (domain in use, scheduler running)
    unsupported        = 14, ///< e.g. non-native SystemKind
    abi_mismatch       = 15,
    load_failed        = 16,
};

[[nodiscard]] constexpr bool succeeded(Status s) noexcept
{
    return s == Status::ok;
}

} // namespace bakuon::core
