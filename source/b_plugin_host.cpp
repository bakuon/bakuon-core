#include "b_plugin_host.h"
#include "bakuon/core/plugin.h"

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

void PluginHost::run_dispose(const std::function<void(IPlugin*)>& fn, IPlugin* plugin) noexcept
{
    try {
        fn(plugin);
    } catch (...) {
    }
}

void PluginHost::teardown(Loaded& l) noexcept
{
    l.plugin->on_unload(*l.context); // 1. the plugin cleans up after itself
    l.context->release_all();        // 2. safety net: leftover systems, then hooked component types
    run_dispose(l.dispose,
                l.plugin); // 3. object destroyed (and its module released) by its own module
    l.plugin = nullptr;
}

Status PluginHost::adopt(IPlugin* plugin, std::function<void(IPlugin*)> dispose,
                         std::string* out_name) noexcept
{
    if (plugin == nullptr || !dispose) {
        return Status::invalid_argument;
    }
    // From here on the plugin object belongs to us: every failure path must dispose it.
    const auto fail = [&](Status s) {
        run_dispose(dispose, plugin);
        return s;
    };
    if (plugin->abi_version() != IPlugin::abi_current) {
        return fail(Status::abi_mismatch);
    }
    const char* name = plugin->name();
    if (!is_valid_name(name)) {
        return fail(Status::invalid_argument);
    }
    if (is_loaded(name)) {
        return fail(Status::already_exists);
    }

    std::unique_ptr<Loaded> l;
    try { // everything that can throw happens before the plugin is handed over
        m_loaded.reserve(m_loaded.size() + 1);
        l          = std::make_unique<Loaded>();
        l->name    = name;
        l->context = std::make_unique<PluginContext>(m_world);
        if (out_name != nullptr) {
            *out_name = name;
        }
    } catch (...) {
        return fail(Status::out_of_memory);
    }
    l->plugin  = plugin;
    l->dispose = std::move(dispose);

    const Status s = plugin->on_load(*l->context);
    if (s != Status::ok) {
        l->context->release_all(); // roll back; on_unload is NOT called after a failed on_load
        run_dispose(l->dispose, plugin);
        return s;
    }
    m_loaded.push_back(std::move(l)); // reserved above: cannot throw
    return Status::ok;
}

Status PluginHost::load(const PluginModule& m, std::string* out_name) noexcept
{
    if (m.abi_version == nullptr || m.create == nullptr || m.destroy == nullptr) {
        return Status::invalid_argument;
    }
    if (m.abi_version() != IPlugin::abi_current) {
        return Status::abi_mismatch; // refuse before running any plugin code
    }
    IPlugin* plugin = m.create();
    if (plugin == nullptr) {
        return Status::load_failed;
    }
    const PluginDestroyFn destroy = m.destroy;
    try {
        return adopt(plugin, [destroy](IPlugin* p) { destroy(p); }, out_name);
    } catch (...) { // constructing the std::function failed; adopt was never entered
        destroy(plugin);
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
