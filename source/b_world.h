#pragma once

#include "b_connection.h"
#include "b_domain.h"
#include "b_scheduler.h"
#include <bakuon/core/world.h>

#include <cassert>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace bakuon::core {

namespace detail {
struct WorldHooks;
}

class WorldError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/**
 * @brief 域隔离的多路复用器：按需创建/持有/销毁多个相互独立的 Domain（各自一个 registry）。
 *
 * ## 线程模型
 * create/require/destroy/clear/on_domain_* 只能在创建 World 的线程调用（debug 下断言）。
 * find/resolve/each 在没有并发拓扑修改时可读。Domain 内部数据不加锁，线程亲和性由调用方决定。
 *
 * ## 引用有效期
 * 返回的 IDomain& / IDomain* 在该域被 destroy()/clear() 之前一直有效（地址稳定，与其它域的
 * 增删无关）。需要跨越"可能被销毁"的边界持有时，存 DomainHandle，用 resolve() 取回。
 *
 * ## 销毁时序
 * destroy(id)：标记 closing → 触发 on_domain_destroying（此时仍可 find）→ 从表中摘除 →
 * 逆序销毁服务 → 销毁 Container。~World()/clear() 按域创建的逆序销毁。
 * 钩子里可以创建其它域、销毁其它域；对正在销毁的域再次 destroy 返回 false。
 *
 */
class World : public IWorld
{
public:
    using Initializer    = IWorld::Initializer;
    using DomainCallback = IWorld::DomainCallback;

    // Bring the header-only lambda/function-pointer adapters from IWorld into this
    // overload set; the virtual overrides below still handle DomainCallback values.
    using IWorld::create;
    using IWorld::require;

    World() noexcept;
    ~World();

    World(const World&)            = delete;
    World& operator=(const World&) = delete;
    World(World&&)                 = delete;
    World& operator=(World&&)      = delete;

    [[nodiscard]] virtual std::uint32_t abi_version() const noexcept override
    {
        return IWorld::abi_current;
    }

    [[nodiscard]] bool contains(DomainId id) const noexcept override { return find(id) != nullptr; }
    [[nodiscard]] std::size_t count() const noexcept override { return m_domains.size(); }
    [[nodiscard]] IDomain* find(DomainId id) noexcept override;
    [[nodiscard]] const IDomain* find(DomainId id) const noexcept;
    [[nodiscard]] IDomain* find_by_name(const char* name) noexcept override;
    [[nodiscard]] IDomain* resolve(DomainHandle handle) noexcept override;
    [[nodiscard]] const IDomain* resolve(DomainHandle handle) const noexcept;

    /// 创建命名域；同名已存在返回 nullptr；名字为空或与另一个已存在域发生哈希碰撞抛 DomainError。
    /// init 在域插入表之前执行，抛异常时域被丢弃、Workspace 状态不变。
    [[nodiscard]] IDomain* create(const char* name, const Initializer& init = {}) override;
    /// 按需分配：存在则返回，否则创建（并执行 init）。域正在销毁时抛 DomainError。
    [[nodiscard]] IDomain* require(const char* name, const Initializer& init = {}) override;
    /// 不存在或已在销毁中返回 false
    Status destroy(DomainId id) override;
    Status destroy_by_name(const char* name) override;

    /// 创建匿名域（典型：每个打开的文档一个），id 由 Workspace 分配；label 仅用于诊断。
    IDomain* create_anonymous(std::string label = {}, const Initializer& init = {});

    /// 捕获 lambda / 普通函数指针的 create_anonymous 适配器。
    template<class F, std::enable_if_t<!std::is_same_v<std::decay_t<F>, Initializer>, int> = 0>
    [[nodiscard]] IDomain* create_anonymous(std::string label, F&& init)
    {
        detail::DomainInitializer<std::decay_t<F>> holder{std::forward<F>(init)};
        return insert(DomainId{m_nextAnonymous++}, std::move(label), holder.callback());
    }

    /// 逆创建序销毁全部域。
    void clear();

    IScheduler& scheduler() noexcept override { return *m_scheduler; }
    Status build_schedule() noexcept override { return m_scheduler->build(); }
    Status run_frame(const FrameContext& ctx) noexcept override { return m_scheduler->run(ctx); }

    /// True if `domain` is still owned by this world and was created with `serial`.
    [[nodiscard]] bool is_live(const Domain* domain, std::uint64_t serial) const noexcept;

    /// 应用级原生 dispatcher（entt::dispatcher，仅引擎编译单元）。
    /// 用于帧开始/插件生命周期等与具体 Domain 无关的类型安全事件。
    /// 域内玩法事件请用 Domain::dispatcher()。
    [[nodiscard]] Dispatcher& dispatcher() noexcept { return m_dispatcher; }
    [[nodiscard]] const Dispatcher& dispatcher() const noexcept { return m_dispatcher; }

    Domain* find_internal(std::string_view name) const noexcept;

    /// 按创建顺序遍历。先对 handle 做快照再逐个 resolve()，回调里销毁其它域是安全的
    /// （已销毁的域会被跳过）；回调里新建的域不会被本次遍历访问。
    template<typename Func>
    void each(Func&& func)
    {
        for (const DomainHandle& handle : snapshot()) {
            if (IDomain* domain = resolve(handle)) {
                func(domain);
            }
        }
    }

    /// 域已完整构造（含 init）并插入表之后触发。
    /// 低层重载：回调为 C 风格函数指针 + user，context 的生命周期由调用方负责。
    [[nodiscard]] Connection on_domain_created(DomainCallback callback);
    /// 域即将销毁时触发，此时域仍完整可用；回调抛出的异常会中止本次销毁并向上传播。
    /// 返回的 Connection 可以比 Workspace 活得更久。
    [[nodiscard]] Connection on_domain_destroying(DomainCallback callback);

    /**
     * @brief 捕获 lambda / 普通函数指针的 on_domain_created 适配器。
     *
     * 可调用对象被复制到一个 shared_ptr 中，由 hook 表持有；连接断开或 World 销毁
     * 时自动释放。该对象及其函数指针 thunk 位于调用方模块，因此插件必须在卸载前
     * disconnect 自己注册的 hook（与原先 std::function 版本的生命周期约束一致）。
     */
    template<class F, std::enable_if_t<!std::is_same_v<std::decay_t<F>, DomainCallback>, int> = 0>
    [[nodiscard]] Connection on_domain_created(F&& callback)
    {
        using fn_type = std::decay_t<F>;
        auto owner    = std::make_shared<fn_type>(std::forward<F>(callback));
        const DomainCallback cb{owner.get(), &detail::invoke_domain_callback<fn_type>};
        return add_created_hook(cb, std::move(owner));
    }

    /// 捕获 lambda / 普通函数指针的 on_domain_destroying 适配器。
    template<class F, std::enable_if_t<!std::is_same_v<std::decay_t<F>, DomainCallback>, int> = 0>
    [[nodiscard]] Connection on_domain_destroying(F&& callback)
    {
        using fn_type = std::decay_t<F>;
        auto owner    = std::make_shared<fn_type>(std::forward<F>(callback));
        const DomainCallback cb{owner.get(), &detail::invoke_domain_callback<fn_type>};
        return add_destroying_hook(cb, std::move(owner));
    }

private:
    IDomain* insert(DomainId id, std::string name, const Initializer& init);
    [[nodiscard]] Connection add_created_hook(DomainCallback callback, std::shared_ptr<void> owner);
    [[nodiscard]] Connection add_destroying_hook(DomainCallback callback,
                                                 std::shared_ptr<void> owner);
    [[nodiscard]] std::vector<DomainHandle> snapshot() const;
    void assertOwnerThread() const noexcept
    {
        assert(m_ownerThread == std::this_thread::get_id()
               && "World 的拓扑操作只能在创建它的线程调用");
    }

private:
    /// 声明顺序 = 构造顺序；析构逆序。
    /// Scheduler 必须在 Domain 表之后声明，以便 ~World 先拆 Scheduler（此时 resolve 仍可用），
    /// 再 clear Domain，最后拆 EventBus。
    entt::dense_map<DomainId, std::unique_ptr<Domain>> m_domains;
    std::shared_ptr<detail::WorldHooks> m_hooks;
    std::uint64_t m_nextSerial           = 1;
    DomainId::value_type m_nextAnonymous = 1;
    std::thread::id m_ownerThread        = std::this_thread::get_id();

    // 应用级原生事件 / 插件总线 / 服务表 / 调度器 ...
    Dispatcher m_dispatcher;
    /// 为以后扩展占位
    // std::unique_ptr<EventBus> m_events;
    // std::unique_ptr<ServiceProvider> m_services;
    std::unique_ptr<Scheduler> m_scheduler;
};

} // namespace bakuon::core
