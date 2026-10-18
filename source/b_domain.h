#pragma once

/** @file b_domain.h  INTERNAL. Concrete domain; owns one Registry. */

#include "b_component_registry.h"
#include "b_entity.h"
#include <bakuon/core/domain.h>

#include <cstddef>
#include <string>
#include <utility>

// 只给引擎编译单元包含。插件禁止包含本头。

namespace bakuon::core {

/// 重命名为 Domain
class Domain final : public IDomain
{
public:
    Domain(DomainId id, std::string name, std::uint64_t serial);
    ~Domain();

    Domain(const Domain&)            = delete;
    Domain& operator=(const Domain&) = delete;
    Domain(Domain&&)                 = delete;
    Domain& operator=(Domain&&)      = delete;

    [[nodiscard]] std::uint32_t abi_version() const noexcept override;
    [[nodiscard]] DomainId id() const noexcept override { return m_id; }
    [[nodiscard]] const char* name() const noexcept override { return m_name.c_str(); }
    [[nodiscard]] std::uint64_t serial() const noexcept override { return m_serial; }
    [[nodiscard]] bool closing() const noexcept override { return m_closing; }

    [[nodiscard]] Entity create() noexcept override;
    [[nodiscard]] Entity create(Entity hint) noexcept override;
    bool destroy(Entity entity) noexcept override;
    [[nodiscard]] bool alive(Entity entity) const noexcept override;
    std::size_t alive_count() const noexcept override { return m_alive_count; }
    void clear() noexcept override;

    /// ========================= abi v2 ===============================
    IComponents& components() noexcept override { return m_components; }

    /// Internal access only. Never expose across the ABI.
    [[nodiscard]] Registry& registry() { return m_registry; }
    [[nodiscard]] const Registry& registry() const { return m_registry; }

private:
    friend class World;

private:
    DomainId m_id;
    std::string m_name;
    std::uint64_t m_serial;
    bool m_closing = false;

    // 声明顺序 = 构造顺序；析构时 m_records 先于 m_registry 销毁。
    // 不要为了"看起来整齐"调整这两个成员的相对顺序。
    Registry m_registry;
    ComponentRegistry m_components;
    std::size_t m_alive_count{0};
};

} // namespace bakuon::core
