#pragma once
/**
 * @file b_plugin_view.h
 * @brief INTERNAL. Per-plugin view of a domain: forwards everything to the real
 * domain and records which component types this plugin created.
 */

#include "b_world.h"
#include <bakuon/core/domain.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bakuon::core {

class PluginComponents final : public IComponents
{
public:
    explicit PluginComponents(IComponents& inner) noexcept
        : m_inner(inner)
    {
    }

    Status inregister(const ComponentDesc& desc, ComponentId* out_id) noexcept override
    {
        try {
            // An identical re-registration of an existing type is idempotent and does NOT transfer
            // ownership.
            const bool existed = m_inner.find(desc.name).valid();
            m_owned.reserve(m_owned.size() + 1); // so push_back below cannot throw
            ComponentId id{};
            const Status s = m_inner.inregister(desc, &id);
            if (s == Status::ok) {
                if (!existed) {
                    m_owned.push_back(id);
                }
                if (out_id != nullptr) {
                    *out_id = id;
                }
            }
            return s;
        } catch (...) {
            return Status::out_of_memory;
        }
    }

    /// A plugin can only unregister types it created; others are reported as not_found.
    Status unregister(ComponentId id) noexcept override
    {
        const auto it = std::find(m_owned.begin(), m_owned.end(), id);
        if (it == m_owned.end()) {
            return Status::not_found;
        }
        const Status s = m_inner.unregister(id);
        if (s == Status::ok || s == Status::not_registered) {
            m_owned.erase(it);
        }
        return s;
    }

    ComponentId find(const char* name) const noexcept override { return m_inner.find(name); }
    Status describe(ComponentId id, ComponentInfo* out) const noexcept override
    {
        return m_inner.describe(id, out);
    }
    std::size_t type_count() const noexcept override { return m_inner.type_count(); }
    ComponentId type_at(std::size_t index) const noexcept override
    {
        return m_inner.type_at(index);
    }

    Status emplace(Entity e, ComponentId id, const void* init, void** out_ptr) noexcept override
    {
        return m_inner.emplace(e, id, init, out_ptr);
    }
    Status replace(Entity e, ComponentId id, const void* bytes) noexcept override
    {
        return m_inner.replace(e, id, bytes);
    }
    Status remove(Entity e, ComponentId id) noexcept override { return m_inner.remove(e, id); }
    bool has(Entity e, ComponentId id) const noexcept override { return m_inner.has(e, id); }
    void* get(Entity e, ComponentId id) noexcept override { return m_inner.get(e, id); }
    const void* get_const(Entity e, ComponentId id) const noexcept override
    {
        return m_inner.get_const(e, id);
    }
    std::size_t count(ComponentId id) const noexcept override { return m_inner.count(id); }
    Status clear(ComponentId id) noexcept override { return m_inner.clear(id); }
    Status for_each(ComponentId id, void* user, EachFn fn) noexcept override
    {
        return m_inner.for_each(id, user, fn);
    }
    Status each(const ComponentId* ids, std::uint32_t count, void* user, EachFn fn) noexcept override
    {
        return m_inner.each(ids, count, user, fn);
    }

    /// Unregisters the types this plugin created that carry code pointers into its module.
    /// Data-only types stay registered (with their data); ownership of them is dropped.
    void release_hooked_types() noexcept
    {
        for (auto it = m_owned.rbegin(); it != m_owned.rend(); ++it) {
            ComponentInfo info{};
            info.struct_size = static_cast<std::uint32_t>(sizeof(ComponentInfo));
            if (m_inner.describe(*it, &info) != Status::ok) {
                continue;
            }
            if (info.serializer != nullptr || info.deserializer != nullptr
                || info.editor_draw != nullptr) {
                (void) m_inner.unregister(*it);
            }
        }
        m_owned.clear();
    }

private:
    IComponents& m_inner;
    std::vector<ComponentId> m_owned;
};

class PluginDomainView final : public IDomain
{
public:
    PluginDomainView(World& world, Domain& domain)
        : m_world(world)
        , m_domain(&domain)
        , m_serial(domain.serial())
        , m_components(domain.components())
    {
    }

    std::uint32_t abi_version() const noexcept override { return IDomain::abi_current; }
    DomainId id() const noexcept override { return m_domain->id(); }
    const char* name() const noexcept override { return m_domain->name(); }
    bool closing() const noexcept override { return m_domain->closing(); }
    Entity create() noexcept override { return m_domain->create(); }
    Entity create_hint(Entity hint) noexcept override { return m_domain->create_hint(hint); }
    bool destroy(Entity e) noexcept override { return m_domain->destroy(e); }
    bool alive(Entity e) const noexcept override { return m_domain->alive(e); }
    std::size_t alive_count() const noexcept override { return m_domain->alive_count(); }
    void clear() noexcept override { m_domain->clear(); }
    IComponents& components() noexcept override { return m_components; }

    [[nodiscard]] const Domain* domain() const noexcept { return m_domain; }
    [[nodiscard]] std::uint64_t serial() const noexcept override { return m_serial; }

    /// Safe even if the domain was destroyed in the meantime.
    void release() noexcept
    {
        if (m_world.is_live(m_domain, m_serial)) {
            m_components.release_hooked_types();
        }
    }

private:
    World& m_world;
    Domain* m_domain;
    std::uint64_t m_serial;
    PluginComponents m_components;
};

} // namespace bakuon::core
