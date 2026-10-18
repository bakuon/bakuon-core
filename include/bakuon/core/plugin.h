#pragma once

/**
 * @file plugin.h
 * @brief Plugin interface and the exported C entry points.
 *
 * A plugin module exports (see BAKUON_DEFINE_PLUGIN):
 *   extern "C" BAKUON_PLUGIN_API uint32_t bakuon_plugin_abi_version();
 *   extern "C" BAKUON_PLUGIN_API bakuon::core::IPlugin* bakuon_create_plugin();
 *   extern "C" BAKUON_PLUGIN_API void bakuon_destroy_plugin(bakuon::core::IPlugin*);
 *
 * The host never deletes a plugin itself; it always calls bakuon_destroy_plugin
 * so the object is freed by the module that allocated it.
 *
 * Unload order (enforced by the host):
 *   on_unload(ctx)  ->  leftover systems of this plugin are removed
 *   ->  bakuon_destroy_plugin  ->  the library may then be closed.
 * Plugins should unregister everything they registered (systems, component
 * types that carry hook function pointers) inside on_unload.
 */

#include <bakuon/core/api.h>
#include <bakuon/core/domain.h>
#include <bakuon/core/status.h>
#include <bakuon/core/system.h>

#include <cstdint>
#include <new>

namespace bakuon::core {

/// What a plugin sees of the host. Valid between on_load and on_unload.
class BAKUON_CORE_API IPluginContext
{
public:
    [[nodiscard]] virtual std::uint32_t abi_version() const noexcept         = 0;
    /// Null if the domain does not exist.
    [[nodiscard]] virtual IDomain* find_domain(const char* name) noexcept    = 0;
    /// Finds or creates the domain (never destroys). Null on failure.
    [[nodiscard]] virtual IDomain* require_domain(const char* name) noexcept = 0;
    /// Per-plugin view: systems added through it are tracked and can be
    /// removed only by this plugin; leftovers are removed on unload.
    [[nodiscard]] virtual IScheduler& scheduler() noexcept                   = 0;

protected:
    IPluginContext()                                 = default;
    IPluginContext(const IPluginContext&)            = default;
    IPluginContext& operator=(const IPluginContext&) = default;
    ~IPluginContext()                                = default;
};

class BAKUON_CORE_API IPlugin
{
public:
    static constexpr std::uint32_t abi_current = 1;

    [[nodiscard]] virtual std::uint32_t abi_version() const noexcept = 0;
    /// Unique, non-empty, stable for the plugin's lifetime.
    [[nodiscard]] virtual const char* name() const noexcept          = 0;
    /// Return ok on success. On failure the host rolls back (removes systems
    /// added through ctx) and does NOT call on_unload.
    virtual Status on_load(IPluginContext& ctx) noexcept             = 0;
    // extensions initialized
    virtual void on_ready(IPluginContext& ctx) noexcept              = 0;
    virtual void on_unload(IPluginContext& ctx) noexcept             = 0;

protected:
    IPlugin()                          = default;
    IPlugin(const IPlugin&)            = default;
    IPlugin& operator=(const IPlugin&) = default;
    ~IPlugin()                         = default;
};

using PluginAbiVersionFn = std::uint32_t (*)();
using PluginCreateFn     = IPlugin* (*)();
using PluginDestroyFn    = void (*)(IPlugin*);

/// The three entry points of a loaded module (obtained via dlsym/GetProcAddress).
struct PluginModule final
{
    PluginAbiVersionFn abi_version{nullptr};
    PluginCreateFn create{nullptr};
    PluginDestroyFn destroy{nullptr};
};

} // namespace bakuon::core

/// Place once at global scope in the plugin. PluginClass must be a `final`
/// class deriving from IPlugin with a public destructor and default constructor.
#define BAKUON_DEFINE_PLUGIN(PluginClass) \
    extern "C" BAKUON_PLUGIN_API std::uint32_t bakuon_plugin_abi_version() \
    { \
        return ::bakuon::core::plugin_abi_version_current; \
    } \
    extern "C" BAKUON_PLUGIN_API ::bakuon::core::IPlugin* bakuon_create_plugin() \
    { \
        return new (std::nothrow) PluginClass(); \
    } \
    extern "C" BAKUON_PLUGIN_API void bakuon_destroy_plugin(::bakuon::core::IPlugin* p) \
    { \
        delete static_cast<PluginClass*>(p); \
    }
