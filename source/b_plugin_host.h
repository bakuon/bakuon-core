#pragma once
/** @file b_plugin_host.h  INTERNAL. Plugin lifecycle: tracked scheduler/domain views +
 * adopt/load/unload. */

#include "b_names.h"
#include "b_plugin_view.h"
#include "b_world.h"
#include <bakuon/core/plugin.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bakuon::core {

/// Tracks the systems one plugin registered; a plugin may only remove its own.
class PluginScheduler final : public IScheduler
{
public:
    explicit PluginScheduler(IScheduler& inner) noexcept
        : m_inner(inner)
    {
    }

    Status add_system(const SystemDesc& desc, ISystem* system) noexcept override
    {
        try {
            std::string name = desc.name != nullptr ? desc.name : "";
            m_owned.reserve(m_owned.size() + 1); // so the push below cannot throw
            const Status s = m_inner.add_system(desc, system);
            if (s == Status::ok) {
                m_owned.push_back(std::move(name));
            }
            return s;
        } catch (...) {
            return Status::out_of_memory; // nothing was handed over yet
        }
    }

    Status remove_system(const char* name) noexcept override
    {
        const auto it = find_owned(name);
        if (it == m_owned.end()) {
            return Status::not_found;
        }
        const Status s = m_inner.remove_system(name);
        if (s == Status::ok) {
            m_owned.erase(it);
        }
        return s;
    }

    bool has_system(const char* name) const noexcept override { return m_inner.has_system(name); }
    std::size_t system_count() const noexcept override { return m_inner.system_count(); }
    const char* system_name_at(std::size_t i) const noexcept override
    {
        return m_inner.system_name_at(i);
    }

    /// Removes (and releases) everything this plugin still owns.
    void release_all() noexcept
    {
        while (!m_owned.empty()) {
            const std::string name = m_owned.back();
            m_owned.pop_back();
            (void) m_inner.remove_system(name.c_str());
        }
    }

private:
    std::vector<std::string>::iterator find_owned(const char* name) noexcept
    {
        if (name == nullptr) {
            return m_owned.end();
        }
        return std::find(m_owned.begin(), m_owned.end(), name);
    }

    IScheduler& m_inner;
    std::vector<std::string> m_owned;
};

class PluginContext final : public IPluginContext
{
public:
    explicit PluginContext(World& world)
        : m_world(world)
        , m_scheduler(world.scheduler())
    {
    }

    std::uint32_t abi_version() const noexcept override { return IPlugin::abi_current; }

    IDomain* find_domain(const char* name) noexcept override
    {
        return view_for(m_world.find_internal(name));
    }
    IDomain* require_domain(const char* name) noexcept override
    {
        return view_for(static_cast<Domain*>(m_world.require(name)));
    }
    IScheduler& scheduler() noexcept override { return m_scheduler; }

    /// Teardown safety net, in this order: leftover systems, then hooked component types.
    void release_all() noexcept
    {
        m_scheduler.release_all();
        for (auto it = m_views.rbegin(); it != m_views.rend(); ++it) {
            (*it)->release();
        }
    }

private:
    IDomain* view_for(Domain* d) noexcept
    {
        if (d == nullptr) {
            return nullptr;
        }
        for (const auto& v : m_views) {
            if (v->domain() == d && v->serial() == d->serial()) {
                return v.get();
            }
        }
        try {
            m_views.push_back(std::make_unique<PluginDomainView>(m_world, *d));
            return m_views.back().get();
        } catch (...) {
            return nullptr;
        }
    }

    World& m_world;
    PluginScheduler m_scheduler;
    std::vector<std::unique_ptr<PluginDomainView>> m_views;
};

class PluginHost
{
public:
    explicit PluginHost(World& world) noexcept
        : m_world(world)
    {
    }
    ~PluginHost(); // unloads everything in reverse load order

    PluginHost(const PluginHost&)            = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    /// Function-pointer module path (dlopen/LoadLibrary). Destroys the plugin object on failure.
    Status load(const PluginModule& module, std::string* out_name = nullptr) noexcept;

    /// Takes an already created plugin. `dispose` runs exactly once: after unload, or
    /// immediately on any failure. It must destroy the object and release its module.
    Status adopt(IPlugin* plugin, std::function<void(IPlugin*)> dispose,
                 std::string* out_name = nullptr) noexcept;

    Status unload(const char* name) noexcept;
    bool is_loaded(const char* name) const noexcept;
    std::size_t count() const noexcept { return m_loaded.size(); }

private:
    struct Loaded
    {
        std::string name;
        std::function<void(IPlugin*)> dispose;
        IPlugin* plugin{nullptr};
        std::unique_ptr<PluginContext> context;
    };
    static void run_dispose(const std::function<void(IPlugin*)>& fn, IPlugin* plugin) noexcept;
    static void teardown(Loaded& l) noexcept;

    World& m_world;
    std::vector<std::unique_ptr<Loaded>> m_loaded;
};

} // namespace bakuon::core
