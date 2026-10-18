#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

/**
 * service.h — 插件可见的服务提供者接口。
 *
 * ## 命名：IServiceProvider，而不是 ServiceLocator
 *
 * Service Locator 通常指「全局静态、任意处可 pull」的环境依赖，会隐藏调用链、
 * 妨碍测试与生命周期管理。本接口刻意避免那条路：
 *
 * - 由 World 持有，经 IPluginContext 注入，无全局单例
 * - 查找键是稳定字符串名 + abi_version，不是 C++ RTTI
 * - 接口指针的所有权仍在注册方；Provider 只保存裸指针，不负责 delete
 *
 * 与 .NET IServiceProvider 同精神：调用方「向已知容器要服务」，容器由宿主组装。
 *
 * @code
 * // 服务接口（可放在共享头，两端同 ABI）
 * class ILogger {
 * public:
 *     virtual ~ILogger() = default;
 *     static constexpr std::uint32_t abi = 1;
 *     static constexpr std::string_view id = "core.Logger";
 *     virtual void info(std::string_view) = 0;
 * };
 *
 * // 插件注册
 * ctx.services().add(ILogger::id, &my_logger, ILogger::abi);
 *
 * // 另一插件获取
 * if (auto* log = try_get<ILogger>(ctx.services())) {
 *     log->info("hello");
 * }
 * @endcode
 */

namespace bakuon::core {

inline constexpr std::uint32_t service_abi_version = 1;

/**
 * @brief 服务提供者：注册 / 解析 / 注销具名接口指针。
 *
 * 线程模型与 World 一致：拓扑修改（add/remove）应在同一线程。
 * get 在无并发 add/remove 时可读。
 */
class IServiceProvider
{
public:
    virtual ~IServiceProvider() = default;

    IServiceProvider(const IServiceProvider&)            = delete;
    IServiceProvider& operator=(const IServiceProvider&) = delete;

    [[nodiscard]] virtual std::uint32_t abi_version() const noexcept = 0;

    /**
     * @brief 注册服务。
     * @param name  非空，大小写敏感（建议 "module.Interface"）。
     * @param iface 非空接口指针；所有权仍归调用方。
     * @param abi   该接口的 ABI 版本；get 时必须匹配。
     * @return 成功 true；重名或参数非法 false，原因见 last_error()。
     */
    [[nodiscard]] virtual bool add(std::string_view name, void* iface, std::uint32_t abi) = 0;

    /// 按名注销；不存在返回 false。不释放 iface 指向的对象。
    virtual bool remove(std::string_view name) = 0;

    /**
     * @brief 解析服务。
     * @param expected_abi 调用方期望的 ABI；与注册值不同则返回 nullptr。
     * @return 注册时的 iface；未找到或 abi 不匹配返回 nullptr。
     */
    [[nodiscard]] virtual void* get(std::string_view name, std::uint32_t expected_abi) const = 0;

    [[nodiscard]] virtual bool contains(std::string_view name) const noexcept = 0;
    [[nodiscard]] virtual std::size_t count() const noexcept                  = 0;

    [[nodiscard]] virtual const char* last_error() const = 0;

protected:
    IServiceProvider() = default;
};

// ---------------------------------------------------------------------------
// 类型安全包装（服务接口须提供 static constexpr abi 与可选 id）
// ---------------------------------------------------------------------------

/**
 * @brief 注册实现了 T 的对象。T 须有 static constexpr std::uint32_t abi。
 */
template<typename T>
[[nodiscard]] bool add(IServiceProvider& provider, std::string_view name, T& instance)
{
    static_assert(std::is_class_v<T>, "service type must be a class/interface");
    return provider.add(name, static_cast<void*>(&instance), T::abi);
}

template<typename T>
[[nodiscard]] bool add(IServiceProvider& provider, T& instance)
{
    static_assert(std::is_class_v<T>, "service type must be a class/interface");
    return provider.add(T::id, static_cast<void*>(&instance), T::abi);
}

template<typename T>
[[nodiscard]] T* try_get(const IServiceProvider& provider, std::string_view name)
{
    return static_cast<T*>(provider.get(name, T::abi));
}

template<typename T>
[[nodiscard]] T* try_get(const IServiceProvider& provider)
{
    return static_cast<T*>(provider.get(T::id, T::abi));
}

} // namespace bakuon::core
