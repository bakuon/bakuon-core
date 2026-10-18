#include "b_service.h"

namespace bakuon::core {

bool ServiceProvider::add(std::string_view name, void* iface, std::uint32_t abi)
{
    if (name.empty()) {
        fail("add: service name is empty");
        return false;
    }
    if (iface == nullptr) {
        fail("add: interface pointer is null");
        return false;
    }
    if (m_entries.find(std::string(name)) != m_entries.end()) {
        fail("add: duplicate service name");
        return false;
    }

    m_entries.emplace(std::string(name), Entry{iface, abi});
    clear_error();
    return true;
}

bool ServiceProvider::remove(std::string_view name)
{
    const auto it = m_entries.find(std::string(name));
    if (it == m_entries.end()) {
        fail("remove: unknown service");
        return false;
    }
    m_entries.erase(it);
    clear_error();
    return true;
}

void* ServiceProvider::get(std::string_view name, std::uint32_t expected_abi) const
{
    const auto it = m_entries.find(std::string(name));
    if (it == m_entries.end()) {
        m_error = "get: unknown service";
        return nullptr;
    }
    if (it->second.abi != expected_abi) {
        m_error = "get: abi_version mismatch";
        return nullptr;
    }
    m_error.clear();
    return it->second.iface;
}

bool ServiceProvider::contains(std::string_view name) const noexcept
{
    return m_entries.find(std::string(name)) != m_entries.end();
}

} // namespace bakuon::core
