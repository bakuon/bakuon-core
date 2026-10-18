#pragma once

/** @file b_component_storage.h  INTERNAL. Runtime-typed dense pool over an EnTT sparse set. */

#include "b_entity.h"

#include <entt/entity/sparse_set.hpp>

#include <cstddef>
#include <cstdint>

namespace bakuon::core {

/// Component Pool: use Registry::storage_for_type<Entity> instead of

/// Index = entt::basic_sparse_set<Entity> (paged sparse array, version checks,
/// swap-and-pop). Payload = aligned byte array kept in lock-step with the
/// sparse set's packed order: payload[i] belongs to entities()[i].
class ComponentStorage final
{
public:
    ComponentStorage(std::uint32_t size, std::uint32_t align);
    ~ComponentStorage();

    ComponentStorage(const ComponentStorage&)            = delete;
    ComponentStorage& operator=(const ComponentStorage&) = delete;

    [[nodiscard]] std::size_t size() const noexcept { return m_index.size(); }
    [[nodiscard]] std::uint32_t stride() const noexcept { return m_size; }

    [[nodiscard]] bool contains(Entity e) const noexcept;
    [[nodiscard]] void* get(Entity e) noexcept;
    [[nodiscard]] const void* get(Entity e) const noexcept;

    /// Precondition: !contains(e). Throws std::bad_alloc; state is unchanged on throw.
    /// Initial bytes: `init`, else `fallback`, else zeros.
    void* emplace(Entity e, const void* init, const void* fallback);

    bool erase(Entity e) noexcept;
    void clear() noexcept;

    [[nodiscard]] const Entity* entities() const noexcept { return m_index.data(); }
    [[nodiscard]] void* data() noexcept { return m_buffer; }
    [[nodiscard]] const void* data() const noexcept { return m_buffer; }

private:
    void reserve_one();
    void release(unsigned char* p) noexcept;

    entt::basic_sparse_set<Entity> m_index;
    unsigned char* m_buffer{nullptr};
    std::size_t m_capacity{0}; // in elements
    std::uint32_t m_size;
    std::uint32_t m_align;
};

} // namespace bakuon::core
