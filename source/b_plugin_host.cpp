#include "b_plugin_host.h"
#include <string>

namespace bakuon::core {

PluginHost::~PluginHost()
{
    for (auto it = m_loaded.rbegin(); it != m_loaded.rend(); ++it) {
        teardown(**it);
    }
    m_loaded.clear();
}

bool PluginHost::is_loaded(const char* name) const noexcept
{
    if (name == nullptr) {
        return false;
    }
    for (const auto& l : m_loaded) {
        if (l->name == name) {
            return true;
        }
    }
    return false;
}

void PluginHost::teardown(Loaded& l) noexcept
{
    l.plugin->on_unload(*l.context);
    l.context->release_systems(); // safety net for anything the plugin left behind
    l.module.destroy(l.plugin);   // freed by the module that allocated it
    l.plugin = nullptr;
}

Status PluginHost::load(const PluginModule& m, std::string* out_name) noexcept
{
    if (m.abi_version == nullptr || m.create == nullptr || m.destroy == nullptr) {
        return Status::invalid_argument;
    }
    if (m.abi_version() != plugin_abi_version_current) {
        return Status::abi_mismatch; // refuse before running any plugin code
    }
    IPlugin* plugin = m.create();
    if (plugin == nullptr) {
        return Status::load_failed;
    }
    if (plugin->abi_version() != plugin_abi_version_current) {
        m.destroy(plugin);
        return Status::abi_mismatch;
    }
    const char* name = plugin->name();
    if (!is_valid_name(name)) {
        m.destroy(plugin);
        return Status::invalid_argument;
    }
    if (is_loaded(name)) {
        m.destroy(plugin);
        return Status::already_exists;
    }

    try {
        m_loaded.reserve(m_loaded.size() + 1); // so push_back below cannot throw
        auto l     = std::make_unique<Loaded>();
        l->name    = name;
        l->module  = m;
        l->plugin  = plugin;
        l->context = std::make_unique<PluginContext>(m_world);

        if (out_name != nullptr) {
            *out_name = name;
        }

        const Status s = plugin->on_load(*l->context);
        if (s != Status::ok) {
            l->context->release_systems(); // roll back; on_unload is NOT called
            m.destroy(plugin);
            return s;
        }
        m_loaded.push_back(std::move(l));
        return Status::ok;
    } catch (...) {
        m.destroy(plugin);
        return Status::out_of_memory;
    }
}

Status PluginHost::unload(const char* name) noexcept
{
    if (name == nullptr) {
        return Status::invalid_argument;
    }
    for (auto it = m_loaded.begin(); it != m_loaded.end(); ++it) {
        if ((*it)->name == name) {
            teardown(**it);
            m_loaded.erase(it);
            return Status::ok;
        }
    }
    return Status::not_found;
}

} // namespace bakuon::core
