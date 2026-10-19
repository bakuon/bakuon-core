#pragma once
/** @file b_component_registry.h  INTERNAL. Per-domain type table over named EnTT storages. */

#include "b_entity.h"
#include <bakuon/core/component.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace bakuon::core {

/**
 * @brief One instance per domain: no global state, so type tables and payloads of
 * different domains are fully independent.
 *
 * Every registered type owns one EnTT storage named by its ComponentId. The storage
 * element is a move-only handle to a separately allocated payload, which keeps the
 * payload of one instance stable while other instances are added/removed. Because the
 * storages live in the domain's Registry, registry.destroy()/clear()/reset() maintain
 * component lifetime automatically: there is no separate cleanup hook.
 *
 * @note The 32-bit name hash doubles as the EnTT storage id. inregister() rejects a
 * hash that is already owned by another storage in this registry (Status::id_collision),
 * so plugin components and engine-owned typed storages never alias each other.
 */
class ComponentRegistry final : public IComponents
{
public:
    explicit ComponentRegistry(Registry& registry);
    ~ComponentRegistry();

    ComponentRegistry(const ComponentRegistry&)            = delete;
    ComponentRegistry& operator=(const ComponentRegistry&) = delete;

    Status inregister(const ComponentDesc& desc, ComponentId* out_id) noexcept override;
    Status unregister(ComponentId id) noexcept override;
    ComponentId find(const char* name) const noexcept override;
    Status describe(ComponentId id, ComponentInfo* out) const noexcept override;
    std::size_t type_count() const noexcept override;
    ComponentId type_at(std::size_t index) const noexcept override;

    Status emplace(Entity e, ComponentId id, const void* bytes, void** out) noexcept override;
    Status replace(Entity e, ComponentId id, const void* bytes) noexcept override;
    Status remove(Entity e, ComponentId id) noexcept override;
    bool has(Entity e, ComponentId id) const noexcept override;
    void* get(Entity e, ComponentId id) noexcept override;
    const void* get_const(Entity e, ComponentId id) const noexcept override;

    std::size_t count(ComponentId id) const noexcept override;
    Status clear(ComponentId id) noexcept override;

    Status for_each(ComponentId id, void* user, EachFn fn) noexcept override;
    Status each(const ComponentId* ids, std::uint32_t count, void* user,
                EachFn fn) noexcept override;

    /// True while a for_each()/each() callback is running. Domain uses this to reject
    /// destructive operations that would invalidate the iteration (e.g. destroy).
    [[nodiscard]] bool iterating() const noexcept { return m_iteration_depth != 0; }

private:
    struct TypeEntry;
    TypeEntry* find_entry(ComponentId id) const noexcept;

    std::vector<std::unique_ptr<TypeEntry>> m_types; // registration order, stable addresses
    std::unordered_map<ComponentId, TypeEntry*> m_by_id;
    Registry& m_registry;
    std::uint32_t m_iteration_depth{0};
};

} // namespace bakuon::core
