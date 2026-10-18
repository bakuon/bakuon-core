#include "b_plugin_manager.h"

namespace bakuon::core {

PluginManager::~PluginManager()
{
    // Reverse load order; each library is closed only after its plugin is destroyed.
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
        (void) m_host.unload((*it)->name.c_str());
        (*it)->lib.close();
    }
    m_entries.clear();
}

Status PluginManager::load(const char* path) noexcept
{
    try {
        m_error.clear();
        if (path == nullptr || *path == '\0') {
            m_error = "empty plugin path";
            return Status::invalid_argument;
        }
        m_entries.reserve(m_entries.size() + 1); // so the push_back below cannot throw
        auto entry  = std::make_unique<Entry>();
        entry->path = path;

        Status s = entry->lib.open(path);
        if (s != Status::ok) {
            m_error = entry->lib.error();
            return s;
        }

        PluginModule module;
        module.abi_version = reinterpret_cast<PluginAbiVersionFn>(
            entry->lib.symbol("bakuon_plugin_abi_version"));
        module.create = reinterpret_cast<PluginCreateFn>(entry->lib.symbol("bakuon_create_plugin"));
        module.destroy = reinterpret_cast<PluginDestroyFn>(
            entry->lib.symbol("bakuon_destroy_plugin"));
        if (module.abi_version == nullptr || module.create == nullptr || module.destroy == nullptr) {
            m_error = std::string("not a Bakuon plugin (missing exports): ") + path;
            return Status::load_failed; // entry's destructor closes the library
        }

        std::string name;
        s = m_host.load(module, &name);
        if (s != Status::ok) {
            m_error = "plugin rejected (status " + std::to_string(static_cast<int>(s))
                      + "): " + (name.empty() ? std::string(path) : name);
            return s; // PluginHost already destroyed the plugin object; library closes with entry
        }
        entry->name = std::move(name);
        m_entries.push_back(std::move(entry));
        return Status::ok;
    } catch (...) {
        m_error = "out of memory";
        return Status::out_of_memory;
    }
}

Status PluginManager::unload(const char* name) noexcept
{
    m_error.clear();
    if (name == nullptr) {
        return Status::invalid_argument;
    }
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if ((*it)->name == name) {
            const Status s = m_host.unload(name); // on_unload -> systems -> destroy
            (*it)->lib.close();                   // only now is it safe to drop the library
            m_entries.erase(it);
            return s;
        }
    }
    return Status::not_found;
}

Status PluginManager::adopt(IPlugin* plugin, PluginDisposeFn dispose, void* user) noexcept
{
    if (plugin == nullptr || dispose == nullptr) {
        return Status::invalid_argument;
    }
    bool handed_over = false;
    try {
        m_error.clear();
        m_entries.reserve(m_entries.size() + 1); // so the push_back below cannot throw
        auto entry                       = std::make_unique<Entry>();
        std::function<void(IPlugin*)> fn = [dispose, user](IPlugin* p) { dispose(p, user); };

        std::string name;
        handed_over    = true; // from here the host disposes the plugin on every failure
        const Status s = m_host.adopt(plugin, std::move(fn), &name);
        if (s != Status::ok) {
            m_error = "plugin rejected (status " + std::to_string(static_cast<int>(s)) + ")";
            return s;
        }
        entry->name = std::move(name);
        m_entries.push_back(std::move(entry));
        return Status::ok;
    } catch (...) {
        if (!handed_over) {
            dispose(plugin, user);
        }
        return Status::out_of_memory;
    }
}

} // namespace bakuon::core
