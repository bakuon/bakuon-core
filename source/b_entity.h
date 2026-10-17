#pragma once

/**
 * @file b_entity.h
 * @brief INTERNAL. Backend aliases and traits verification.
 * Must never be installed or included by plugins/applications.
 */

#include <bakuon/core/entity.h>

#include <entt/entity/handle.hpp>
#include <entt/entity/registry.hpp>
#include <entt/entity/snapshot.hpp>
#include <entt/signal/dispatcher.hpp>
#include <entt/signal/sigh.hpp>

#include <cstdint>
#include <type_traits>

namespace bakuon::core {

using Registry          = entt::basic_registry<Entity>;
using Snapshot          = entt::basic_snapshot<Registry>;
using SnapshotLoader    = entt::basic_snapshot_loader<Registry>;
using ContinuousLoader  = entt::basic_continuous_loader<Registry>;
using Dispatcher        = entt::dispatcher;
using SinkConnection    = entt::connection;
using ScopedConnection  = entt::scoped_connection;
using EntityHandle      = entt::basic_handle<Registry>;
using ConstEntityHandle = entt::basic_handle<const Registry>;

namespace detail {
using entity_traits                        = entt::entt_traits<Entity>;
inline constexpr Entity backend_null_probe = entt::null; // copy-init: converts to Entity
} // namespace detail

// ---- traits must match Entity's own layout ---------------------------------
static_assert(std::is_same_v<Registry::entity_type, Entity>,
              "Registry must be keyed on bakuon::core::Entity");
static_assert(std::is_same_v<detail::entity_traits::entity_type, std::uint32_t>,
              "backend must pick the 32-bit layout");
static_assert(detail::entity_traits::entity_mask == Entity::index_mask,
              "index mask mismatch with backend traits");
static_assert(detail::entity_traits::version_mask == Entity::version_mask,
              "version mask mismatch with backend traits");

static_assert(detail::entity_traits::to_integral(Entity::make(5, 3)) == ((3u << 20) | 5u),
              "to_integral mismatch");
static_assert(detail::entity_traits::to_entity(Entity::make(5, 3)) == 5u,
              "to_entity (index) mismatch");
static_assert(detail::entity_traits::to_version(Entity::make(5, 3)) == 3u, "to_version mismatch");

// ---- null must be bit-identical -------------------------------------------
static_assert(detail::backend_null_probe.value() == Entity::null_raw,
              "backend null bit pattern differs from Entity::null_raw");
static_assert(detail::backend_null_probe.is_null(), "backend null must be null for Entity");
static_assert(!Entity{}.is_null(), "Entity{} must stay a valid entity 0");

} // namespace bakuon::core
