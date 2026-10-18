#pragma once

#include <cstdint>
#include <string_view>

#include <bakuon/core/domain.h>
#include <bakuon/core/event.h>
#include <bakuon/core/service.h>
#include <bakuon/core/system.h>

/**
 * plugin.h — 插件入口接口。
 *
 * 插件 DLL 通过 C 导出工厂创建 IPlugin 实例，核心在加载时调用 on_load，
 * 卸载时调用 on_unload 再销毁。所有跨边界类型均为虚接口 + 版本号。
 *
 * 推荐导出符号（插件侧）：
 * @code
 * extern "C" BAKUON_PLUGIN_API bakuon::core::IPlugin* bakuon_create_plugin();
 * extern "C" BAKUON_PLUGIN_API void bakuon_destroy_plugin(bakuon::core::IPlugin*);
 * @endcode
 */

namespace bakuon::core {

inline constexpr std::uint32_t plugin_abi_version = 1;

/**
 * @brief 插件可访问的宿主上下文（由核心在 on_load 时注入）。
 *
 * 仅暴露稳定虚接口，避免插件直接依赖 World / Scheduler 具体类型。
 */
class IPluginContext
{
public:
    virtual ~IPluginContext() = default;

    IPluginContext(const IPluginContext&)            = delete;
    IPluginContext& operator=(const IPluginContext&) = delete;

    [[nodiscard]] virtual std::uint32_t abi_version() const noexcept = 0;

    /// 按名查找或创建 Domain（语义同 World::require，由宿主决定是否允许创建）。
    [[nodiscard]] virtual IDomain* require_domain(std::string_view name) = 0;
    [[nodiscard]] virtual IDomain* find_domain(std::string_view name)    = 0;

    [[nodiscard]] virtual IScheduler& scheduler()       = 0;
    [[nodiscard]] virtual IEventBus& events()           = 0;
    [[nodiscard]] virtual IServiceProvider& services()  = 0;

protected:
    IPluginContext() = default;
};

/**
 * @brief 插件主接口。
 *
 * on_load 中典型工作：
 * 1. 注册组件（component<T>(domain, "name")）
 * 2. 创建并 register_system
 * 3. 订阅事件 / 注册服务（若宿主提供）
 *
 * on_unload 中应主动 unregister_system；若未做，宿主析构 Scheduler 时仍会安全销毁。
 */
class IPlugin
{
public:
    virtual ~IPlugin() = default;

    IPlugin(const IPlugin&)            = delete;
    IPlugin& operator=(const IPlugin&) = delete;

    [[nodiscard]] virtual std::uint32_t abi_version() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept     = 0;
    [[nodiscard]] virtual std::string_view version() const noexcept  = 0;

    virtual void on_load(IPluginContext& context) = 0;
    virtual void on_unload()                      = 0;

protected:
    IPlugin() = default;
};

} // namespace bakuon::core
