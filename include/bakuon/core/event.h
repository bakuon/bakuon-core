#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>


#include <bakuon/core/connection.h>

/**
 * event.h — 插件可见的事件总线接口。
 *
 * 设计约束（与 Entity / IDomain / ISystem 一致）：
 * - 本头不包含、不前向声明任何 entt 类型。
 * - 事件按名字（string_view）标识，载荷必须是标准布局且可平凡复制的 POD。
 * - 跨 DLL 只走虚函数 + 显式 abi_version；回调是 C 函数指针 + user 指针。
 * - publish 同步调用订阅者；回调内允许 subscribe / unsubscribe（快照隔离）。
 *
 * @code
 * struct DamageEvent { Entity target; float amount; };
 * static_assert(std::is_trivially_copyable_v<DamageEvent>);
 *
 * auto conn = bus.subscribe("game.Damage", &user, [](void* u, const void* data, std::uint32_t) {
 *     auto& e = *static_cast<const DamageEvent*>(data);
 *     // ...
 * });
 * bus.publish("game.Damage", DamageEvent{entity, 10.f});
 * @endcode
 */

namespace bakuon::core {

inline constexpr std::uint32_t event_abi_version = 1;

/**
 * @brief 事件总线接口。
 *
 * 所有权：subscribe 返回的 Connection 析构时自动取消订阅。
 * Connection 可以比 IEventBus 活得更久：总线先销毁时断开是安全空操作。
 */
class IEventBus
{
public:
    /// C 风格处理器：user 为订阅时传入的上下文；data 指向事件载荷；size 为字节数。
    /// 标签事件（无载荷）时 data == nullptr 且 size == 0。
    using Handler = void (*)(void* user, const void* data, std::uint32_t size);

    virtual ~IEventBus() = default;

    IEventBus(const IEventBus&)            = delete;
    IEventBus& operator=(const IEventBus&) = delete;

    [[nodiscard]] virtual std::uint32_t abi_version() const noexcept = 0;

    /**
     * @brief 订阅命名事件。
     * @param event_name 非空；大小写敏感。
     * @param user       透传给 handler 的上下文（可为 nullptr）。
     * @param handler    非空。
     * @return 有效 Connection；失败时返回默认构造的空 Connection，原因见 last_error()。
     */
    [[nodiscard]] virtual std::shared_ptr<IConnection> subscribe(std::string_view event_name,
                                                                 void* user, Handler handler) = 0;

    /**
     * @brief 发布事件（同步）。
     * @param event_name 非空。
     * @param data       载荷首地址；size == 0 时可为 nullptr。
     * @param size       载荷字节数。
     * @return 实际调用的订阅者数量；事件名无效时返回 0。
     */
    virtual std::uint32_t publish(std::string_view event_name, const void* data,
                                  std::uint32_t size) = 0;

    /// 当前某事件的订阅者数量（调试用）。
    [[nodiscard]] virtual std::uint32_t subscriber_count(std::string_view event_name) const = 0;

    [[nodiscard]] virtual const char* last_error() const = 0;

protected:
    IEventBus() = default;
};

// ---------------------------------------------------------------------------
// 类型安全的薄包装（插件侧推荐使用）
// ---------------------------------------------------------------------------

/**
 * @brief 发布 POD 事件。T 必须是 trivially copyable；空类型视为标签事件。
 */
template<typename T>
std::uint32_t publish(IEventBus& bus, std::string_view event_name, const T& value)
{
    static_assert(std::is_trivially_copyable_v<T>, "event payload must be trivially copyable");
    static_assert(std::is_standard_layout_v<T>, "event payload must be standard layout");
    if constexpr (std::is_empty_v<T>) {
        return bus.publish(event_name, nullptr, 0);
    } else {
        return bus.publish(event_name, &value, static_cast<std::uint32_t>(sizeof(T)));
    }
}

/// 无载荷标签事件。
inline std::uint32_t publish(IEventBus& bus, std::string_view event_name)
{
    return bus.publish(event_name, nullptr, 0);
}

/**
 * @brief 订阅并自动把载荷转成 const T&。
 * @param fn 形如 void(const T&) 或可调用对象；通过 user 传递时请自行保证生命周期。
 *
 * 注意：此重载把可调用对象的地址作为 user 传入，调用方必须保证 fn 在 Connection
 * 存活期间一直有效（典型：成员函数 + 对象，或静态/全局函数）。
 */
template<typename T, typename Fn>
[[nodiscard]] IConnection& subscribe(IEventBus& bus, std::string_view event_name, Fn& fn)
{
    static_assert(std::is_trivially_copyable_v<T>, "event payload must be trivially copyable");
    static_assert(!std::is_empty_v<T>, "use subscribe_tag for empty events");
    return bus.subscribe(event_name, &fn, [](void* user, const void* data, std::uint32_t size) {
        if (data == nullptr || size < sizeof(T)) {
            return;
        }
        auto& callback = *static_cast<Fn*>(user);
        callback(*static_cast<const T*>(data));
    });
}

template<typename Fn>
[[nodiscard]] IConnection& subscribe_tag(IEventBus& bus, std::string_view event_name, Fn& fn)
{
    return bus.subscribe(event_name, &fn, [](void* user, const void*, std::uint32_t) {
        auto& callback = *static_cast<Fn*>(user);
        callback();
    });
}

} // namespace bakuon::core
