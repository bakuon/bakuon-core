#pragma once
/**
 * @file host.h
 * @brief What an application sees of the host library.
 *
 * Usage:
 *   if (bakuon_host_abi_version() != IHost::abi_version_current) { ... }
 *   IHost* host = bakuon_host_create();
 *   host->load_plugin("/abs/path/plugin.dll");
 *   host->world().run_frame(...);
 *   bakuon_host_destroy(host);   // unloads all plugins, then closes their libraries
 *
 * The host is created and destroyed only through the C functions below, so
 * it is always freed by the module that allocated it.
 */

#include <bakuon/core/api.h>
#include <bakuon/core/status.h>
#include <bakuon/core/world.h>

#include <cstddef>
#include <cstdint>

namespace bakuon::core {

class BAKUON_CORE_API IHost
{
public:
    static constexpr std::uint32_t abi_current = 1;

    [[nodiscard]] virtual std::uint32_t abi_version() const noexcept = 0;
    [[nodiscard]] virtual IWorld& world() noexcept                   = 0;

    /// Loads a plugin library from a UTF-8 path (made absolute on Windows).
    /// On failure see last_error().
    virtual Status load_plugin(const char* path) noexcept                              = 0;
    /// on_unload -> systems removed -> plugin destroyed -> library closed.
    virtual Status unload_plugin(const char* name) noexcept                            = 0;
    [[nodiscard]] virtual std::size_t plugin_count() const noexcept                    = 0;
    [[nodiscard]] virtual const char* plugin_name_at(std::size_t index) const noexcept = 0;

    /// Text of the last failed load/unload; valid until the next load/unload call.
    [[nodiscard]] virtual const char* last_error() const noexcept = 0;

protected:
    IHost()                        = default;
    IHost(const IHost&)            = default;
    IHost& operator=(const IHost&) = default;
    ~IHost()                       = default;
};

} // namespace bakuon::core

extern "C" {
BAKUON_CORE_API std::uint32_t bakuon_host_abi_version();
/// Returns null on failure.
BAKUON_CORE_API bakuon::core::IHost* bakuon_host_create();
BAKUON_CORE_API void bakuon_host_destroy(bakuon::core::IHost* host);
}
