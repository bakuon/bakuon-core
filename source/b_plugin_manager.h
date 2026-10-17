#pragma once
/** @file b_plugin_manager.h  INTERNAL. Library loading + plugin lifecycle. */

#include "b_module_loader.h"
#include "b_plugin_host.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace bakuon::core {

class BAKUON_CORE_API PluginManager
{
public:
    explicit PluginManager(World& world) noexcept
        : m_host(world)
    {
    }
    ~PluginManager();

    PluginManager(const PluginManager&)            = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    Status load(const char* utf8_path) noexcept;
    Status unload(const char* name) noexcept;

    [[nodiscard]] std::size_t count() const noexcept { return m_entries.size(); }
    [[nodiscard]] const char* name_at(std::size_t index) const noexcept
    {
        return index < m_entries.size() ? m_entries[index]->name.c_str() : nullptr;
    }
    [[nodiscard]] const char* last_error() const noexcept { return m_error.c_str(); }

private:
    struct Entry
    {
        std::string name;
        std::string path;
        ModuleLoader lib;
    };

    // Declared before m_host: destroyed after it, so a library always outlives
    // the plugin object that lives inside it.
    std::vector<std::unique_ptr<Entry>> m_entries;
    PluginHost m_host;
    std::string m_error;
};

} // namespace bakuon::core
