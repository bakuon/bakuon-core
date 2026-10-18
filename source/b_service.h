#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include <bakuon/core/service.h>

// 只给引擎编译单元包含。插件禁止包含本头。

namespace bakuon::core {

/**
 * @brief IServiceProvider 的具体实现。
 *
 * 不拥有服务对象：add 只存裸指针，remove / 析构都不 delete。
 * 注册方必须保证：在 remove 或 World 销毁之前，iface 一直有效。
 */
class ServiceProvider final : public IServiceProvider
{
public:
    ServiceProvider() noexcept = default;
    ~ServiceProvider() override = default;

    ServiceProvider(const ServiceProvider&)            = delete;
    ServiceProvider& operator=(const ServiceProvider&) = delete;
    ServiceProvider(ServiceProvider&&)                 = delete;
    ServiceProvider& operator=(ServiceProvider&&)      = delete;

    [[nodiscard]] std::uint32_t abi_version() const noexcept override
    {
        return service_abi_version;
    }

    [[nodiscard]] bool add(std::string_view name, void* iface, std::uint32_t abi) override;
    bool remove(std::string_view name) override;

    [[nodiscard]] void* get(std::string_view name, std::uint32_t expected_abi) const override;

    [[nodiscard]] bool contains(std::string_view name) const noexcept override;
    [[nodiscard]] std::size_t count() const noexcept override { return m_entries.size(); }

    [[nodiscard]] const char* last_error() const override { return m_error.c_str(); }

private:
    struct Entry
    {
        void* iface        = nullptr;
        std::uint32_t abi  = 0;
    };

    void fail(const char* message) { m_error = message; }
    void clear_error() { m_error.clear(); }

private:
    std::unordered_map<std::string, Entry> m_entries;
    mutable std::string m_error;
};

} // namespace bakuon::core
