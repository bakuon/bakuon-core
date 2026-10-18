#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "b_connection.h"
#include "b_domain.h"

namespace bakuon::core {

namespace detail {
struct WorldHooks;
}

class Scheduler;
class EventBus;
class ServiceProvider;
class IScheduler;
class IEventBus;
class IServiceProvider;

class WorldError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief 应用程序级入口：域隔离多路复用器 + 内嵌 Scheduler / EventBus。
 *
 * ## 职责
 * - Domain 的创建 / 查找 / 销毁（与原先一致）
 * - 拥有唯一的 Scheduler 与 EventBus，作为插件与主循环的统一入口
 * - tick(dt) 转发到内嵌调度器
 *
 * ## 线程模型
 * create/require/destroy/clear/onDomain* / tick / scheduler 拓扑修改
 * 只能在创建 World 的线程调用（debug 下断言）。
 * find/resolve/each 在没有并发拓扑修改时可读。Domain 内部数据不加锁。
 *
 * ## 引用有效期
 * 返回的 Domain& / Domain* 在该域被 destroy()/clear() 之前一直有效。
 * scheduler() / events() 的引用与 World 同寿。
 *
 * ## 销毁时序
 * ~World：先销毁 Scheduler（on_disable / on_detach，此时 Domain 仍可用）→
 * clear() 逆序销毁全部 Domain → 销毁 EventBus。
 *
 * destroy(id)：标记 closing → onDomainDestroying → 从表中摘除 → 销毁 Domain。
 * 钩子里可以创建/销毁其它域；对正在销毁的域再次 destroy 返回 false。
 */
class World
{
public:
    using Initializer    = std::function<void(Domain&)>;
    using DomainCallback = std::function<void(Domain&)>;

    World() noexcept;
    ~World();

    World(const World&)            = delete;
    World& operator=(const World&) = delete;
    World(World&&)                 = delete;
    World& operator=(World&&)      = delete;

    // ------------------------------------------------------------------ Domain
    [[nodiscard]] bool contains(DomainId id) const noexcept { return find(id) != nullptr; }
    [[nodiscard]] std::size_t count() const noexcept { return m_domains.size(); }
    [[nodiscard]] Domain* find(DomainId id) noexcept;
    [[nodiscard]] const Domain* find(DomainId id) const noexcept;
    [[nodiscard]] Domain* find(std::string_view name) noexcept;
    [[nodiscard]] const Domain* find(std::string_view name) const noexcept;
    [[nodiscard]] Domain* resolve(DomainHandle handle) noexcept;
    [[nodiscard]] const Domain* resolve(DomainHandle handle) const noexcept;

    /// 创建命名域；同名已存在返回 nullptr；名字为空或哈希碰撞抛 WorldError。
    Domain* create(std::string_view name, const Initializer& init = {});

    /// 按需分配：存在则返回，否则创建。域正在销毁时抛 WorldError。
    Domain& require(std::string_view name, const Initializer& init = {});

    /// 创建匿名域；label 仅用于诊断。
    Domain& createAnonymous(std::string label = {}, const Initializer& init = {});

    /// 不存在或已在销毁中返回 false。
    bool destroy(DomainId id);

    /// 逆创建序销毁全部域（不影响 Scheduler / EventBus 本身）。
    void clear();

    /// 按创建顺序遍历。回调里销毁其它域安全；新建域不在本次遍历中。
    template<typename Func>
    void each(Func&& func)
    {
        for (const DomainHandle& handle : snapshot()) {
            if (Domain* domain = resolve(handle)) {
                func(*domain);
            }
        }
    }

    [[nodiscard]] Connection domainCreated(DomainCallback callback);
    [[nodiscard]] Connection domainDestroying(DomainCallback callback);

    // ---------------------------------------------------------- Scheduler / Events
    /// 内嵌调度器（与 World 同寿）。
    [[nodiscard]] IScheduler& scheduler() noexcept;
    [[nodiscard]] const IScheduler& scheduler() const noexcept;

    /// 内嵌插件事件总线（字符串名 + POD，跨 DLL）。与 World 同寿。
    [[nodiscard]] IEventBus& events() noexcept;
    [[nodiscard]] const IEventBus& events() const noexcept;

    /// 内嵌服务提供者（具名接口指针 + abi，跨 DLL）。与 World 同寿。
    /// 非全局 Service Locator：仅通过 World / IPluginContext 注入使用。
    [[nodiscard]] IServiceProvider& services() noexcept;
    [[nodiscard]] const IServiceProvider& services() const noexcept;

    /// 应用级原生 dispatcher（entt::dispatcher，仅引擎编译单元）。
    /// 用于帧开始/插件生命周期等与具体 Domain 无关的类型安全事件。
    /// 域内玩法事件请用 Domain::dispatcher() / dispatcher_of()。
    [[nodiscard]] Dispatcher& dispatcher() noexcept;
    [[nodiscard]] const Dispatcher& dispatcher() const noexcept;

    /// 主循环入口：等价于 scheduler().tick(dt)。
    void tick(float dt);

    /// 只跑绑定到该 Domain（或全局）的 System。
    void tick_domain(IDomain& domain, float dt);

    // ------------------------------------------------------------------ util
    [[nodiscard]] static constexpr DomainId fromName(std::string_view name) noexcept
    {
        if (name.empty()) {
            return {};
        }
        std::uint64_t hash = 0xcbf29ce484222325ULL;
        for (const char c : name) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 0x100000001b3ULL;
        }
        return DomainId{hash | DomainId::NamedBit};
    }

private:
    Domain& insert(DomainId id, std::string name, const Initializer& init);
    [[nodiscard]] std::vector<DomainHandle> snapshot() const;
    void assertOwnerThread() const noexcept
    {
        assert(m_ownerThread == std::this_thread::get_id()
               && "World 的拓扑操作只能在创建它的线程调用");
    }

private:
    // 声明顺序 = 构造顺序；析构逆序。
    // Scheduler 必须在 Domain 表之后声明，以便 ~World 先拆 Scheduler（此时 resolve 仍可用），
    // 再 clear Domain，最后拆 EventBus。
    entt::dense_map<DomainId, std::unique_ptr<Domain>> m_domains;
    std::shared_ptr<detail::WorldHooks> m_hooks;
    std::uint64_t m_nextSerial           = 1;
    DomainId::value_type m_nextAnonymous = 1;
    std::thread::id m_ownerThread        = std::this_thread::get_id();

    // 应用级原生事件 / 插件总线 / 服务表 / 调度器。
    Dispatcher m_dispatcher;
    std::unique_ptr<EventBus> m_events;
    std::unique_ptr<ServiceProvider> m_services;
    std::unique_ptr<Scheduler> m_scheduler;
};

} // namespace bakuon::core
