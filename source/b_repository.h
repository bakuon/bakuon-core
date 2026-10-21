#pragma once

/** @file b_storage.h  INTERNAL. Runtime-typed dense pool over an EnTT sparse set. */

/// 当前文件未使用

#include "b_entity.h"
#include <bakuon/core/component.h>
#include <bakuon/core/entity.h>
#include <bakuon/core/status.h>

#include <entt/entity/sparse_set.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace bakuon::core {
/// usage:
/// Status span(Repository *api, ComponentArray<T>& out) noexcept
/// {
///     if (!valid()) {
///         return Status::invalid_argument;
///     }
///     ComponentSpan raw{};
///     const Status s = api->span(m_id, &raw);
///     if (s == Status::ok) {
///         out.entities = raw.entities;
///         out.data     = static_cast<T*>(raw.data);
///         out.count    = raw.count;
///     }
///     return s;
/// }
///
/// update useage:
/// void update(const FrameContext& ctx, IDomain&) noexcept override
/// {
///     ComponentArray<example::Velocity> vel;
///     if (m_velocity.span(vel) != Status::ok) {
///         return;
///     }
///     const float dt = static_cast<float>(ctx.delta_seconds);
///     for (std::size_t i = 0; i < vel.count; ++i) {
///         if (example::Position* p = m_position.get(vel.entities[i])) {
///             p->x += vel.data[i].x * dt;
///             p->y += vel.data[i].y * dt;
///             p->z += vel.data[i].z * dt;
///         }
///     }
/// }

/// Contiguous view of one component type. entities[i] owns the payload at
/// (char*)data + i * stride. Order is unspecified. Invalidated by any structural change.
struct ComponentSpan final
{
    const Entity* entities{nullptr};
    void* data{nullptr};
    std::size_t count{0};
    std::size_t stride{0};
};

template<class T>
struct ComponentArray
{
    const Entity* entities{nullptr};
    T* data{nullptr};
    std::size_t count{0};
};

// Component Registry
class Repository : public IComponents
{
public:
    explicit Repository(const Registry& registry);
    ~Repository();

    Repository(const Repository&)            = delete;
    Repository& operator=(const Repository&) = delete;

    Status inregister(const ComponentDesc& desc, ComponentId* out_id) noexcept override;
    Status unregister(ComponentId id) noexcept override;
    ComponentId find(const char* name) const noexcept override;
    Status describe(ComponentId id, ComponentInfo* out) const noexcept override;
    std::size_t type_count() const noexcept override;
    ComponentId type_at(std::size_t index) const noexcept override;

    Status emplace(Entity e, ComponentId id, const void* bytes, void** out) noexcept override;
    Status replace(Entity e, ComponentId id, const void* bytes) noexcept override
    {
        return emplace(e, id, bytes, nullptr);
    }

    Status remove(Entity e, ComponentId id) noexcept override;
    bool has(Entity e, ComponentId id) const noexcept override;
    void* get(Entity e, ComponentId id) noexcept override;
    const void* get_const(Entity e, ComponentId id) const noexcept override;

    std::size_t count(ComponentId id) const noexcept override;
    Status clear(ComponentId id) noexcept override;
    // 直接暴露稠密数组（entities[] + 连续 payload），让插件 System
    // 能缓存友好地遍历，但任何增删都会使 span 失效
    Status span(ComponentId id, ComponentSpan* out) noexcept;

    Status for_each(ComponentId id, void* user, EachFn fn) noexcept override
    {
        // 可能的实现:
        // ComponentSpan values;
        // auto s = span(id, &values);
        // for (std::size_t i = 0; i < values.count; ++i) {
        //     const Entity e = values.entities[i];
        //     void* data[1]  = {get(e, id)};
        //     fn(user, e, data);
        // }
        // return s;

        // TODO: Implement this pure virtual method.
        return Status::unsupported;
    }
    Status each(const ComponentId* ids, std::uint32_t count, void* user, EachFn fn) noexcept override
    {
        // TODO: Implement this pure virtual method.
        return Status::unsupported;
    }

    /// Domain hooks: called by Domain::destroy (before the entity dies) and Domain::clear.
    void erase_entity(Entity e) noexcept;
    void clear_all() noexcept;

private:
    struct TypeEntry;
    TypeEntry* find_entry(ComponentId id) const noexcept;

    std::vector<std::unique_ptr<TypeEntry>> m_types; // registration order, stable addresses
    std::unordered_map<std::uint32_t, TypeEntry*> m_by_id;
    const Registry& m_registry;
};

/// direct access through contiguous memory
/// Index = entt::basic_sparse_set<Entity> (paged sparse array, version checks,
/// swap-and-pop). Payload = aligned byte array kept in lock-step with the
/// sparse set's packed order: payload[i] belongs to entities()[i].
class Storage final
{
public:
    Storage(std::uint32_t size, std::uint32_t align);
    ~Storage();

    Storage(const Storage&)            = delete;
    Storage& operator=(const Storage&) = delete;

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
