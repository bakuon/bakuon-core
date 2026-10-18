#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <bakuon/core/event.h>
#include <bakuon/core/plugin.h>
#include <bakuon/core/system.h>

#include "b_world.h"

// 只给引擎编译单元包含。插件禁止包含本头。

namespace bakuon::core {

/**
 * @brief IScheduler 的具体实现，与 World 协作解析 DomainHandle。
 *
 * ## 排序规则
 * 1. 按 SystemPhase 升序
 * 2. 同阶段内按 priority 升序
 * 3. 同阶段 + 同 priority 时按依赖拓扑（Kahn）；无依赖时保持注册顺序稳定
 *
 * ## Domain 绑定
 * - 空 DomainHandle：全局 System，tick() 时对每个存活 Domain 各执行一次 update
 * - 非空：仅当 resolve(handle) 成功时执行；Domain 已销毁则跳过（不自动 unregister）
 *
 * ## 线程
 * register / unregister / set_enabled / tick 应在同一线程（与 World 拓扑操作一致）。
 */
class Scheduler final : public IScheduler
{
public:
    explicit Scheduler(World& world) noexcept;
    ~Scheduler() override;

    Scheduler(const Scheduler&)            = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&)                 = delete;
    Scheduler& operator=(Scheduler&&)      = delete;

    [[nodiscard]] std::uint32_t abi_version() const noexcept override { return system_abi_version; }

    [[nodiscard]] bool register_system(ISystem* system, DomainHandle bind = {}) override;
    bool unregister_system(std::string_view name) override;

    [[nodiscard]] bool contains(std::string_view name) const noexcept override;
    [[nodiscard]] std::size_t count() const noexcept override { return m_entries.size(); }

    bool set_enabled(std::string_view name, bool enabled) override;
    [[nodiscard]] bool is_enabled(std::string_view name) const noexcept override;

    void tick(float dt) override;
    void tick_domain(IDomain& domain, float dt) override;

    [[nodiscard]] const char* last_error() const override { return m_error.c_str(); }

    /// 按当前排序顺序遍历已注册 System 名字（调试 / 测试用）。
    template<typename Func>
    void each_ordered(Func&& func) const
    {
        ensure_order();
        for (const std::size_t index : m_order) {
            func(*m_entries[index].system);
        }
    }

private:
    struct Entry
    {
        std::unique_ptr<ISystem> system;
        DomainHandle bind{};
        bool enabled = true;
        std::vector<std::string> dependencies; // 注册时快照
    };

    [[nodiscard]] Entry* find(std::string_view name) noexcept;
    [[nodiscard]] const Entry* find(std::string_view name) const noexcept;

    void fail(const char* message);
    void clear_error() { m_error.clear(); }

    /// 惰性重建 m_order；依赖环或缺失依赖时 fail 并保持旧序。
    void ensure_order() const;
    [[nodiscard]] bool rebuild_order() const;

    void invoke_update(Entry& entry, IDomain& domain, float dt);

private:
    World* m_world = nullptr;
    std::vector<Entry> m_entries;

    // 可变：仅缓存排序结果与诊断字符串，不改变逻辑状态。
    mutable std::vector<std::size_t> m_order;
    mutable bool m_order_dirty = true;

    mutable std::string m_error;
};

/**
 * @brief 最小 IPluginContext 实现，供测试与宿主装配使用。
 *
 * 推荐：PluginContext(world) —— 直接使用 World 内嵌的 Scheduler / EventBus。
 * 三参数构造保留给需要注入替身的单元测试。
 */
class PluginContext final : public IPluginContext
{
public:
    explicit PluginContext(World& world) noexcept
        : m_world(&world)
        , m_scheduler(&world.scheduler())
        , m_events(&world.events())
        , m_services(&world.services())
    {
    }

    PluginContext(World& world, IScheduler& scheduler, IEventBus& events,
                  IServiceProvider& services) noexcept
        : m_world(&world)
        , m_scheduler(&scheduler)
        , m_events(&events)
        , m_services(&services)
    {
    }

    [[nodiscard]] std::uint32_t abi_version() const noexcept override { return plugin_abi_version; }

    [[nodiscard]] IDomain* require_domain(std::string_view name) override
    {
        return &m_world->require(name);
    }

    [[nodiscard]] IDomain* find_domain(std::string_view name) override
    {
        return m_world->find(name);
    }

    [[nodiscard]] IScheduler& scheduler() override { return *m_scheduler; }
    [[nodiscard]] IEventBus& events() override { return *m_events; }
    [[nodiscard]] IServiceProvider& services() override { return *m_services; }

private:
    World* m_world               = nullptr;
    IScheduler* m_scheduler      = nullptr;
    IEventBus* m_events          = nullptr;
    IServiceProvider* m_services = nullptr;
};

} // namespace bakuon::core
