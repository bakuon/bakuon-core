#pragma once

/**
 * @file domain.h
 * @brief Public per-domain entity lifecycle interface.
 *
 * Ownership: a domain is owned by the host world. Plugins only receive
 * non-owning pointers (e.g. via IPluginContext::require/find) and must not
 * keep them past on_unload. The destructor is protected and non-virtual, so
 * the interface cannot be deleted through a pointer.
 *
 * ABI: pure virtual, by-value POD arguments only, no exceptions cross it.
 * Failures are reported through return values.
 *
 * Entities are only meaningful inside the domain that created them. Two
 * domains may hand out bit-identical Entity values; never use an entity
 * from one domain with another.
 */

#include <bakuon/core/api.h>
#include <bakuon/core/component.h>
#include <bakuon/core/entity.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

/**
 * 插件可见的世界接口。本头不包含、不前向声明任何 entt 类型。
 *
 * 组件必须是标准布局且可平凡复制的 POD（或空标签类型）。
 * 插件只传 Entity、组件名和对象字节；引擎按名字哈希把每一种组件放进
 * 独立的 entt::basic_registry<Entity> 存储，实体销毁时一并释放。
 *
 * 迭代顺序不稳定。each 的回调里禁止 create / destroy / emplace / remove /
 * unregister，只允许改组件字节。结构修改放到遍历结束后。
 */

namespace bakuon::core {

/**
 * @brief 域标识：命名域 = FNV-1a 64(name) | 最高位；匿名域分配的计数器值。
 * @note 最高位把两类 id 空间彻底隔开，命名域之间的哈希碰撞由 Workspace 在创建时
 *       比对名字检出。名字大小写敏感。0 为无效值。
 */
class BAKUON_CORE_API DomainId
{
public:
    using value_type                     = std::uint64_t;
    static constexpr value_type NamedBit = value_type{1} << 63;

    constexpr DomainId() noexcept = default;
    explicit constexpr DomainId(value_type raw) noexcept
        : m_value(raw)
    {
    }

    [[nodiscard]] constexpr value_type value() const noexcept { return m_value; }
    [[nodiscard]] constexpr bool valid() const noexcept { return m_value != 0; }
    [[nodiscard]] constexpr bool named() const noexcept { return (m_value & NamedBit) != 0; }

    friend constexpr auto operator<=>(const DomainId&, const DomainId&) = default;
    friend constexpr bool operator==(const DomainId&, const DomainId&)  = default;

    [[nodiscard]] static constexpr DomainId from_name(std::string_view name) noexcept
    {
        if (name.empty()) {
            return {};
        }
        value_type hash = 0xcbf29ce484222325ULL;
        for (const char c : name) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 0x100000001b3ULL;
        }
        return DomainId{hash | NamedBit};
    }

private:
    value_type m_value{0};
};

/// 弱句柄：id + serial。域被销毁（哪怕随后同名重建）后 resolve() 一律返回 nullptr。
class BAKUON_CORE_API DomainHandle
{
public:
    constexpr DomainHandle() noexcept = default;

    [[nodiscard]] constexpr DomainId id() const noexcept { return m_id; }
    [[nodiscard]] constexpr std::uint64_t serial() const noexcept { return m_serial; }
    [[nodiscard]] constexpr bool is_null() const noexcept { return m_serial == 0; }

    friend constexpr bool operator==(const DomainHandle&, const DomainHandle&) = default;

private:
    friend class IDomain;
    constexpr DomainHandle(DomainId id, std::uint64_t serial) noexcept
        : m_id(id)
        , m_serial(serial)
    {
    }

    DomainId m_id{};
    std::uint64_t m_serial{0};
};

/**
 * @brief 插件唯一的世界接口 ——— 插件和引擎必须用同一套编译器，因为边界是 C++ 虚函数
 * @details 一个相互隔离的领域/上下文,独占一个 Registry。
 */
class BAKUON_CORE_API IDomain
{
public:
    /// Bump when the vtable layout changes. Plugins compare their compiled
    /// constant with abi_version() before using the interface.
    static constexpr std::uint32_t abi_current = 1;

    [[nodiscard]] virtual std::uint32_t abi_version() const noexcept = 0;
    [[nodiscard]] virtual DomainId id() const noexcept               = 0;
    [[nodiscard]] virtual const char* name() const noexcept          = 0;
    [[nodiscard]] virtual std::uint64_t serial() const noexcept      = 0;
    [[nodiscard]] DomainHandle handle() const noexcept { return {id(), serial()}; };
    /// 正在被销毁（Domain Destroying 期间为 true）
    [[nodiscard]] virtual bool closing() const noexcept = 0;

    /// Creates an entity. Returns `null` on allocation failure.
    [[nodiscard]] virtual Entity create() noexcept = 0;

    /// Tries to create an entity with the requested value. The result may differ
    /// from @p hint if it is already alive, so always compare the result.
    /// A null hint behaves like create(). Returns `null` on failure.
    [[nodiscard]] virtual Entity create(Entity hint) noexcept = 0;

    /// Destroys an entity and everything attached to it.
    /// @return true if destroyed; false if @p entity was null or not alive.
    virtual bool destroy(Entity entity) noexcept = 0;

    /// True only if @p entity is currently alive in this domain (index AND version match).
    [[nodiscard]] virtual bool alive(Entity entity) const noexcept = 0;

    /// Number of entities currently alive.
    [[nodiscard]] virtual std::size_t alive_count() const noexcept = 0;

    /// Destroys every entity (and all attached data). All prior handles become stale.
    virtual void clear() noexcept = 0;

    /// ========================= component abi v2 ===============================

    /// Component access for this domain. Never null; lives as long as the domain.
    [[nodiscard]] virtual IComponents& components() noexcept = 0;

protected:
    IDomain()                          = default;
    IDomain(const IDomain&)            = default;
    IDomain& operator=(const IDomain&) = default;
    virtual ~IDomain()                 = default;
};

} // namespace bakuon::core

template<>
struct std::hash<bakuon::core::DomainId>
{
    std::size_t operator()(const bakuon::core::DomainId& d) const noexcept
    {
        return std::hash<bakuon::core::DomainId::value_type>{}(d.value());
    }
};
