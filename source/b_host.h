#pragma once
/** @file b_host.h  INTERNAL. Concrete host: one world + plugin manager. */

#include "b_plugin_manager.h"
#include "b_world.h"
#include <bakuon/core/host.h>

namespace bakuon::core {

class Host final : public IHost
{
public:
    Host()
        : m_plugins(m_world)
    {
    }

    Host(const Host&)            = delete;
    Host& operator=(const Host&) = delete;

    std::uint32_t abi_version() const noexcept override { return IHost::abi_version_current; }
    IWorld& world() noexcept override { return m_world; }
    Status load_plugin(const char* path) noexcept override { return m_plugins.load(path); }
    Status unload_plugin(const char* name) noexcept override { return m_plugins.unload(name); }
    std::size_t plugin_count() const noexcept override { return m_plugins.count(); }
    const char* plugin_name_at(std::size_t i) const noexcept override
    {
        return m_plugins.name_at(i);
    }
    const char* last_error() const noexcept override { return m_plugins.last_error(); }

private:
    World m_world;           // declared first: destroyed last
    PluginManager m_plugins; // destroyed first: plugins unload while the world is alive
};

} // namespace bakuon::core
