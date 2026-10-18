#pragma once
/** @file b_component_registry.h  INTERNAL. Per-domain type table + pools. */

#include "b_entity.h"
#include <bakuon/core/component.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace bakuon::core {

/// One instance per domain: no global state, so type tables of different
/// domains are fully independent.
class ComponentRegistry final : public IComponents
{
public:
    explicit ComponentRegistry(const Registry& registry);
    ~ComponentRegistry();

    ComponentRegistry(const ComponentRegistry&)            = delete;
    ComponentRegistry& operator=(const ComponentRegistry&) = delete;

    Status inregister(const ComponentDesc& desc, ComponentId* out_id) noexcept override;
    Status unregister(ComponentId id) noexcept override;
    ComponentId find(const char* name) const noexcept override;
    Status describe(ComponentId id, ComponentInfo* out) const noexcept override;
    std::size_t count() const noexcept override;
    ComponentId at(std::size_t index) const noexcept override;

    Status emplace(Entity e, ComponentId id, const void* bytes, void** out) noexcept override;
    Status remove(Entity e, ComponentId id) noexcept override;
    bool has(Entity e, ComponentId id) const noexcept override;
    void* get(Entity e, ComponentId id) noexcept override;
    const void* get(Entity e, ComponentId id) const noexcept override;

    std::size_t count(ComponentId id) const noexcept override;
    Status clear(ComponentId id) noexcept override;
    Status span(ComponentId id, ComponentSpan* out) noexcept override;

    /// Domain hooks: called by Domain::destroy (before the entity dies) and Domain::clear.
    void erase_entity(Entity e) noexcept;
    void clear_all() noexcept;

private:
    struct TypeEntry;
    TypeEntry* find_entry(ComponentId id) const noexcept;

    std::vector<std::unique_ptr<TypeEntry>> m_types; // registration order, stable addresses
    std::unordered_map<std::uint64_t, TypeEntry*> m_by_id;
    const Registry& m_registry;
};

} // namespace bakuon::core
