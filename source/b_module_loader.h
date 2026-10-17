#pragma once
/** @file b_module_loader.h  INTERNAL. OS dynamic library wrapper. */

#include <bakuon/core/api.h>
#include <bakuon/core/status.h>

#include <string>

namespace bakuon::core {

class BAKUON_CORE_API ModuleLoader
{
public:
    ModuleLoader() = default;
    ~ModuleLoader() { close(); }

    ModuleLoader(const ModuleLoader&)            = delete;
    ModuleLoader& operator=(const ModuleLoader&) = delete;

    /// UTF-8 path. Closes any previously opened library first.
    /// invalid_argument: null/empty/invalid path. load_failed: OS refused (see error()).
    Status open(const char* utf8_path) noexcept;

    /// Null if not open, name is null, or the symbol does not exist.
    [[nodiscard]] void* symbol(const char* name) const noexcept;

    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept { return m_handle != nullptr; }
    [[nodiscard]] const std::string& error() const noexcept { return m_error; }

private:
    void* m_handle{nullptr};
    std::string m_error;
};

} // namespace bakuon::core
