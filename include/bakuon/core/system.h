#pragma once

#include <cstdint>
#include <string_view>

#include <bakuon/core/domain.h>

/**
 * system.h — 插件可实现的 System 接口与调度器接口。
 *
 * 设计约束（与 Entity / IDomain 一致）：
 * - 本头不包含、不前向声明任何 entt 类型。
 * - 跨 DLL 边界只走虚函数 + 显式 abi_version。
 * - 插件实现 ISystem，由核心持有的 IScheduler 接管所有权。
 * - System::update 只拿 IDomain&，禁止持有 registry 裸指针。
 *
 * @code
 * class MovementSystem final : public bakuon::core::ISystem {
 * public:
 *     std::uint32_t abi_version() const noexcept override {
 *         return bakuon::core::system_abi_version;
 *     }
 *     std::string_view name() const noexcept override { return "game.Movement"; }
 *     bakuon::core::SystemPhase phase() const noexcept override {
 *         return bakuon::core::SystemPhase::Update;
 *     }
 *     void update(bakuon::core::IDomain& domain, float dt) override { ... }
 * };
 * @endcode
 */

namespace bakuon::core {

/// 必须与实现侧一致；插件注册前应校验。
inline constexpr std::uint32_t system_abi_version = 1;

/**
 * @brief 调度阶段。同阶段内再按 priority（升序）与依赖拓扑排序。
 * @note 插件自定义阶段建议从 1000 起，避免与内建阶段碰撞。
 */
enum class SystemPhase : std::uint32_t {
    PreUpdate  = 100,
    Update     = 200,
    PostUpdate = 300,
    PreRender  = 400,
    Render     = 500,
    PostRender = 600,
};

/**
 * @brief 插件实现的 System 接口。
 *
 * 生命周期顺序（典型）：
 *   构造 → on_attach(domain) → [on_enable] → update... → [on_disable] → on_detach → 析构
 *
 * 回调约束：
 * - update / on_* 内禁止 unregister 自己或销毁所属 Domain。
 * - 结构修改（create/destroy/emplace/remove）应放到遍历结束后，或仅在非 each 回调中进行。
 */
class ISystem
{
public:
    virtual ~ISystem() = default;

    ISystem(const ISystem&)            = delete;
    ISystem& operator=(const ISystem&) = delete;

    [[nodiscard]] virtual std::uint32_t abi_version() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept     = 0;
    [[nodiscard]] virtual SystemPhase phase() const noexcept         = 0;

    /// 同阶段内排序键，越小越先执行。默认 0。
    [[nodiscard]] virtual int priority() const noexcept { return 0; }

    /// 可选分组标签（例如 "physics"），调度器可按组过滤。默认空。
    [[nodiscard]] virtual std::string_view group() const noexcept { return {}; }

    /**
     * @brief 声明对本 System 的依赖（按名字）。
     * @details 调度器在同阶段内做拓扑排序；若出现环或未知依赖名，注册失败。
     *          默认无依赖。依赖名必须与被依赖 System 的 name() 完全一致。
     */
    [[nodiscard]] virtual std::uint32_t dependency_count() const noexcept { return 0; }

    /**
     * @brief 写出第 index 个依赖名到 out（以 '\\0' 结尾）。
     * @param out 调用方提供的缓冲区；cap 为容量（含终止符）。
     * @return 实际需要的字节数（含终止符）；若 cap 不足则截断并返回所需长度。
     */
    virtual std::uint32_t dependency_at(std::uint32_t /*index*/, char* /*out*/,
                                        std::uint32_t /*cap*/) const
    {
        return 0;
    }

    /// 被挂到某个 Domain 时调用（未绑定 Domain 的 System 不会收到）。
    virtual void on_attach(IDomain& /*domain*/) {}
    virtual void on_detach(IDomain& /*domain*/) {}

    virtual void on_enable() {}
    virtual void on_disable() {}

    /**
     * @brief 每帧主入口。
     * @param domain 绑定的 Domain；未绑定且走 tick() 时，由调度器对每个存活 Domain 各调一次。
     * @param dt     秒。
     */
    virtual void update(IDomain& domain, float dt) = 0;

protected:
    ISystem() = default;
};

/**
 * @brief 调度器接口：注册 / 卸载 / 按阶段执行 System。
 *
 * 所有权：register_system 成功后接管 system 指针；unregister / 析构时销毁。
 * 线程模型：与 World 一致，拓扑修改（register/unregister/enable）应在同一线程。
 */
class IScheduler
{
public:
    virtual ~IScheduler() = default;

    IScheduler(const IScheduler&)            = delete;
    IScheduler& operator=(const IScheduler&) = delete;

    [[nodiscard]] virtual std::uint32_t abi_version() const noexcept = 0;

    /**
     * @brief 注册 System。
     * @param system 非空；abi_version 必须匹配；name 不可为空且不可重复。
     * @param bind   非空时绑定到该 Domain（resolve 失败则跳过 update）；
     *               空句柄表示“全局”——tick() 时对每个存活 Domain 执行一次。
     * @return 成功 true；失败时 system 仍由调用方负责释放。
     */
    [[nodiscard]] virtual bool register_system(ISystem* system, DomainHandle bind = {}) = 0;

    /// 按 name 卸载；不存在返回 false。会先 on_disable → on_detach（若仍可 resolve）。
    virtual bool unregister_system(std::string_view name) = 0;

    [[nodiscard]] virtual bool contains(std::string_view name) const noexcept = 0;
    [[nodiscard]] virtual std::size_t count() const noexcept                  = 0;

    virtual bool set_enabled(std::string_view name, bool enabled) = 0;
    [[nodiscard]] virtual bool is_enabled(std::string_view name) const noexcept = 0;

    /// 按 phase → priority → 依赖拓扑 顺序执行全部已启用 System。
    virtual void tick(float dt) = 0;

    /// 只对绑定到该 Domain、或未绑定的全局 System 执行一次。
    virtual void tick_domain(IDomain& domain, float dt) = 0;

    /// 最近一次失败原因（register / unregister 等）；成功操作后清空。
    [[nodiscard]] virtual const char* last_error() const = 0;

protected:
    IScheduler() = default;
};

} // namespace bakuon::core
