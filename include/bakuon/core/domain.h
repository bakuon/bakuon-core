#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

#include <bakuon/core/entity.h>

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

/// 必须等于 ENTT_ID_TYPE（默认 uint32_t）
/// 当前根据平台配置来设置（见根 CMakeList.txt BAKUON_ID_TYPE）
using ComponentId = std::size_t;

inline constexpr std::uint32_t domain_abi_version = 1;

/**
 * @brief 域标识：命名域 = FNV-1a 64(name) | 最高位；匿名域 = Workspace 分配的计数器值。
 * @note 最高位把两类 id 空间彻底隔开，命名域之间的哈希碰撞由 Workspace 在创建时
 *       比对名字检出。名字大小写敏感。0 为无效值。
 */
class DomainId
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
    [[nodiscard]] constexpr bool isValid() const noexcept { return m_value != 0; }
    [[nodiscard]] constexpr bool isNamed() const noexcept { return (m_value & NamedBit) != 0; }

    friend constexpr auto operator<=>(const DomainId&, const DomainId&) = default;
    friend constexpr bool operator==(const DomainId&, const DomainId&)  = default;

    [[nodiscard]] static constexpr DomainId fromName(std::string_view name) noexcept
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
class DomainHandle
{
public:
    constexpr DomainHandle() noexcept = default;

    [[nodiscard]] constexpr DomainId id() const noexcept { return m_id; }
    [[nodiscard]] constexpr std::uint64_t serial() const noexcept { return m_serial; }
    [[nodiscard]] constexpr bool isNull() const noexcept { return m_serial == 0; }

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
class IDomain
{
public:
    virtual ~IDomain() = default;

    IDomain(const IDomain&)            = delete;
    IDomain& operator=(const IDomain&) = delete;

    [[nodiscard]] virtual std::uint32_t abi_version() const noexcept = 0;
    [[nodiscard]] virtual DomainId id() const noexcept               = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept     = 0;
    [[nodiscard]] virtual std::uint64_t serial() const noexcept      = 0;
    [[nodiscard]] DomainHandle handle() const noexcept { return {id(), serial()}; };
    /// 正在被销毁（Domain Destroying 期间为 true）
    [[nodiscard]] virtual bool closing() const noexcept = 0;

    [[nodiscard]] virtual Entity create()                 = 0;
    virtual bool destroy(Entity entity)                   = 0;
    [[nodiscard]] virtual bool valid(Entity entity) const = 0;

    // 同名且 size/align 相同则返回已有 id。失败返回 0，原因见 last_error()。
    // 空类型用 size == 0 注册成标签，不分配载荷。
    [[nodiscard]] virtual ComponentId register_component(std::string_view name, std::uint32_t size,
                                                         std::uint32_t align) = 0;
    virtual bool unregister_component(ComponentId id)                         = 0;

    // out 可为 nullptr。成功时 *out 是载荷地址；标签成功时 *out 为 nullptr。
    // 指针一直有效，直到这个组件实例被 remove、实体被 destroy，或组件类型被 unregister。
    // 同类型其它实例的增删不会让它失效。
    virtual bool emplace(Entity entity, ComponentId id, const void* bytes, void** out) = 0;
    virtual bool replace(Entity entity, ComponentId id, const void* bytes)             = 0;
    virtual bool remove(Entity entity, ComponentId id)                                 = 0;
    [[nodiscard]] virtual void* get(Entity entity, ComponentId id)                     = 0;
    [[nodiscard]] virtual const void* get(Entity entity, ComponentId id) const         = 0;
    [[nodiscard]] virtual bool has(Entity entity, ComponentId id) const                = 0;
    [[nodiscard]] virtual std::uint32_t count(ComponentId id) const                    = 0;

    // ids 在调用期间有效即可。count == 0 或任一 id 未注册时不调用 fn。
    // data[i] 与 ids[i] 对齐，标签组件的 data[i] 为 nullptr。
    using EachFn = void (*)(void* user, Entity entity, void* const* data);
    virtual void each(const ComponentId* ids, std::uint32_t count, void* user, EachFn fn) = 0;

    [[nodiscard]] virtual const char* last_error() const = 0;

protected:
    IDomain() = default;
};

// Component Handle
template<typename T>
class Component
{
public:
    constexpr Component() = default;
    explicit constexpr Component(ComponentId id) noexcept
        : m_id(id)
    {
    }

    [[nodiscard]] constexpr ComponentId id() const noexcept { return m_id; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_id != 0; }

    friend constexpr bool operator==(Component lhs, Component rhs) noexcept
    {
        return lhs.m_id == rhs.m_id;
    }
    friend constexpr bool operator!=(Component lhs, Component rhs) noexcept
    {
        return !(lhs == rhs);
    }

private:
    ComponentId m_id = 0;
};

/**
component<T>(domain, "game.Velocity") 只把 sizeof / alignof 和名字交给引擎。同名且布局相同则复用
id；布局不同，或哈希撞上已有 storage，返回空 Component，原因在
last_error()。空类型是标签，没有载荷，用 add / has
*/
template<typename T>
[[nodiscard]] Component<T> component(IDomain& domain, std::string_view name)
{
    static_assert(std::is_standard_layout_v<T>, "component must be standard layout");
    static_assert(std::is_trivially_copyable_v<T>, "component must be trivially copyable");
    static_assert(!std::is_pointer_v<T>, "do not store raw pointers in a POD component");
    const auto size  = std::is_empty_v<T> ? 0u : static_cast<std::uint32_t>(sizeof(T));
    const auto align = std::is_empty_v<T> ? 1u : static_cast<std::uint32_t>(alignof(T));
    return Component<T>{domain.register_component(name, size, align)};
}

template<typename T>
[[nodiscard]] T* emplace(IDomain& domain, Entity entity, Component<T> component, const T& value = {})
{
    static_assert(!std::is_empty_v<T>, "empty tags have no payload; use add()");
    void* out = nullptr;
    if (!domain.emplace(entity, component.id(), &value, &out)) {
        return nullptr;
    }
    return static_cast<T*>(out);
}

template<typename T>
[[nodiscard]] bool add(IDomain& domain, Entity entity, Component<T> component)
{
    static_assert(std::is_empty_v<T>, "add() is only for empty tag types");
    return domain.emplace(entity, component.id(), nullptr, nullptr);
}
/**
get 返回的指针在这个实例被 remove、实体被 destroy、或这个组件类型被 unregister
之前一直有效。同类型其它实例的增删不会让它失效
*/
template<typename T>
[[nodiscard]] T* get(IDomain& domain, Entity entity, Component<T> component)
{
    static_assert(!std::is_empty_v<T>, "empty tags have no payload; use has()");
    return static_cast<T*>(domain.get(entity, component.id()));
}

template<typename T>
[[nodiscard]] const T* get(const IDomain& domain, Entity entity, Component<T> component)
{
    static_assert(!std::is_empty_v<T>, "empty tags have no payload; use has()");
    return static_cast<const T*>(domain.get(entity, component.id()));
}

template<typename T>
[[nodiscard]] bool has(const IDomain& domain, Entity entity, Component<T> component)
{
    return domain.has(entity, component.id());
}

template<typename T>
[[nodiscard]] bool remove(IDomain& domain, Entity entity, Component<T> component)
{
    return domain.remove(entity, component.id());
}

template<typename T>
[[nodiscard]] T* replace(IDomain& domain, Entity entity, Component<T> component, const T& value)
{
    static_assert(!std::is_empty_v<T>, "empty tags have no payload");
    if (!domain.replace(entity, component.id(), &value)) {
        return nullptr;
    }
    return get(domain, entity, component);
}

// 已有则覆盖，没有则挂上。
template<typename T>
[[nodiscard]] T* insert(IDomain& domain, Entity entity, Component<T> component, const T& value = {})
{
    if (auto* existing = get(domain, entity, component)) {
        *existing = value;
        return existing;
    }
    return emplace(domain, entity, component, value);
}

namespace detail {

template<typename Fn, typename... T, std::size_t... Index>
void call_each(Fn& fn, Entity entity, void* const* data, std::index_sequence<Index...>)
{
    fn(entity, *static_cast<T*>(data[Index])...);
}

} // namespace detail

// 交集查询。T 都不能是空标签。回调形如 void(Entity, T&...)。
template<typename... T>
class View
{
public:
    View(IDomain& domain, Component<T>... components) noexcept
        : m_domain(&domain)
        , m_ids{components.id()...}
    {
    }

    // view(...).each 的顺序不稳定，回调里只能改字节，不能 create / destroy / emplace / remove
    template<typename Fn>
    void each(Fn&& fn) const
    {
        static_assert(sizeof...(T) > 0, "view needs at least one component");
        static_assert((!std::is_empty_v<T> && ...), "tag types cannot be joined; filter with has()");
        m_domain->each(m_ids,
                       static_cast<std::uint32_t>(sizeof...(T)),
                       &fn,
                       [](void* user, Entity entity, void* const* data) {
                           auto& callback = *static_cast<std::remove_reference_t<Fn>*>(user);
                           detail::call_each<std::remove_reference_t<Fn>, T...>(
                               callback, entity, data, std::index_sequence_for<T...>{});
                       });
    }

private:
    IDomain* m_domain = nullptr;
    ComponentId m_ids[sizeof...(T)]{};
};

template<typename... T>
[[nodiscard]] View<T...> view(IDomain& domain, Component<T>... components)
{
    static_assert(sizeof...(T) > 0, "view needs at least one component");
    return View<T...>{domain, components...};
}

// 内部自测，实现在 IDomain.cpp，插件不必调用。
[[nodiscard]] bool bakuon_native_components_follow_entity(IDomain& domain);

} // namespace bakuon::core

template<>
struct std::hash<bakuon::core::DomainId>
{
    [[nodiscard]] std::size_t operator()(bakuon::core::DomainId id) const noexcept
    {
        return std::hash<bakuon::core::DomainId::value_type>{}(id.value());
    }
};
