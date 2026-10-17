#include "b_module_loader.h"

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

namespace bakuon::core {

#if defined(_WIN32)

namespace {

std::string os_error_text(DWORD code)
{
    char* buf        = nullptr;
    const DWORD n    = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                          | FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr,
                                      code,
                                      0,
                                      reinterpret_cast<LPSTR>(&buf),
                                      0,
                                      nullptr);
    std::string text = n != 0 ? std::string(buf, n) : std::string("unknown error");
    if (buf != nullptr) {
        LocalFree(buf);
    }
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }
    return "error " + std::to_string(code) + ": " + text;
}

bool utf8_to_wide(const char* s, std::wstring& out)
{
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, nullptr, 0);
    if (n <= 0) {
        return false;
    }
    out.assign(static_cast<std::size_t>(n), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out.data(), n) <= 0) {
        return false;
    }
    out.resize(static_cast<std::size_t>(n) - 1); // drop the terminating NUL
    return true;
}

} // namespace

Status ModuleLoader::open(const char* utf8_path) noexcept
{
    close();
    try {
        m_error.clear();
        if (utf8_path == nullptr || *utf8_path == '\0') {
            m_error = "empty library path";
            return Status::invalid_argument;
        }
        std::wstring wide;
        if (!utf8_to_wide(utf8_path, wide)) {
            m_error = "library path is not valid UTF-8";
            return Status::invalid_argument;
        }
        // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR needs an absolute path.
        DWORD len = GetFullPathNameW(wide.c_str(), 0, nullptr, nullptr);
        if (len == 0) {
            m_error = "GetFullPathName failed, " + os_error_text(GetLastError());
            return Status::invalid_argument;
        }
        std::wstring full(static_cast<std::size_t>(len), L'\0');
        len = GetFullPathNameW(wide.c_str(), len, full.data(), nullptr);
        full.resize(static_cast<std::size_t>(len));

        HMODULE h = LoadLibraryExW(full.c_str(),
                                   nullptr,
                                   LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
                                       | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
        if (h == nullptr) {
            m_error = "LoadLibrary failed, " + os_error_text(GetLastError());
            return Status::load_failed;
        }
        m_handle = h;
        return Status::ok;
    } catch (...) {
        return Status::out_of_memory;
    }
}

void* ModuleLoader::symbol(const char* name) const noexcept
{
    if (m_handle == nullptr || name == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_handle), name));
}

void ModuleLoader::close() noexcept
{
    if (m_handle != nullptr) {
        FreeLibrary(static_cast<HMODULE>(m_handle));
        m_handle = nullptr;
    }
}

#else // POSIX

Status ModuleLoader::open(const char* utf8_path) noexcept
{
    close();
    try {
        m_error.clear();
        if (utf8_path == nullptr || *utf8_path == '\0') {
            m_error = "empty library path";
            return Status::invalid_argument;
        }
        void* h = ::dlopen(utf8_path, RTLD_NOW | RTLD_LOCAL);
        if (h == nullptr) {
            const char* e = ::dlerror();
            m_error       = e != nullptr ? e : "dlopen failed";
            return Status::load_failed;
        }
        m_handle = h;
        return Status::ok;
    } catch (...) {
        return Status::out_of_memory;
    }
}

void* ModuleLoader::symbol(const char* name) const noexcept
{
    if (m_handle == nullptr || name == nullptr) {
        return nullptr;
    }
    return ::dlsym(m_handle, name);
}

void ModuleLoader::close() noexcept
{
    if (m_handle != nullptr) {
        ::dlclose(m_handle);
        m_handle = nullptr;
    }
}

#endif

} // namespace bakuon::core
