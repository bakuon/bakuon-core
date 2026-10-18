/**
 * plugin_movement — 示例游戏逻辑插件（MODULE / 共享库）。
 *
 * 演示：
 * 1. C 导出工厂（plugin_api.h）
 * 2. 注册 POD 组件
 * 3. 实现 ISystem 并挂到 Domain
 * 4. 通过 IServiceProvider 使用宿主日志（若已注册）
 * 5. 订阅 / 发布 IEventBus 事件
 */

#include <bakuon/core/domain.h>
#include <bakuon/core/event.h>
#include <bakuon/core/plugin.h>
#include <bakuon/core/plugin_api.h>
#include <bakuon/core/service.h>
#include <bakuon/core/system.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace {

// ---------------------------------------------------------------------------
// 组件（POD，与引擎约定：standard layout + trivially copyable）
// ---------------------------------------------------------------------------

struct Position
{
    float x = 0.f;
    float y = 0.f;
};

struct Velocity
{
    float x = 0.f;
    float y = 0.f;
};

struct MovementTickEvent
{
    float dt = 0.f;
    std::uint32_t entity_count = 0;
};

// ---------------------------------------------------------------------------
// 可选：宿主若注册了同名日志服务则使用
// ---------------------------------------------------------------------------

class ILogger
{
public:
    static constexpr std::uint32_t abi   = 1;
    static constexpr std::string_view id = "core.Logger";

    virtual ~ILogger()                      = default;
    virtual void info(std::string_view msg) = 0;
};

// ---------------------------------------------------------------------------
// MovementSystem
// ---------------------------------------------------------------------------

class MovementSystem final : public bakuon::core::ISystem
{
public:
    std::uint32_t abi_version() const noexcept override
    {
        return bakuon::core::system_abi_version;
    }

    std::string_view name() const noexcept override { return "game.Movement"; }

    bakuon::core::SystemPhase phase() const noexcept override
    {
        return bakuon::core::SystemPhase::Update;
    }

    void on_attach(bakuon::core::IDomain& domain) override
    {
        m_pos = bakuon::core::component<Position>(domain, "game.Position");
        m_vel = bakuon::core::component<Velocity>(domain, "game.Velocity");
    }

    void update(bakuon::core::IDomain& domain, float dt) override
    {
        if (!m_pos || !m_vel) {
            return;
        }

        std::uint32_t count = 0;
        bakuon::core::view(domain, m_pos, m_vel)
            .each([dt, &count](bakuon::core::Entity, Position& p, Velocity& v) {
                p.x += v.x * dt;
                p.y += v.y * dt;
                ++count;
            });

        // 通过插件上下文注入的总线发事件（在 on_load 里保存指针）
        if (m_events != nullptr) {
            bakuon::core::publish(*m_events, "game.MovementTick",
                                  MovementTickEvent{dt, count});
        }
    }

    void set_events(bakuon::core::IEventBus* bus) noexcept { m_events = bus; }

private:
    bakuon::core::Component<Position> m_pos;
    bakuon::core::Component<Velocity> m_vel;
    bakuon::core::IEventBus* m_events = nullptr;
};

// ---------------------------------------------------------------------------
// Plugin
// ---------------------------------------------------------------------------

class MovementPlugin final : public bakuon::core::IPlugin
{
public:
    std::uint32_t abi_version() const noexcept override
    {
        return bakuon::core::plugin_abi_version;
    }

    std::string_view name() const noexcept override { return "example.Movement"; }
    std::string_view version() const noexcept override { return "1.0.0"; }

    void on_load(bakuon::core::IPluginContext& ctx) override
    {
        // 仅探测服务是否存在，避免跨 DLL 对静态库内虚接口做 try_get 虚调用。
        (void)ctx.services().contains("core.Logger");

        bakuon::core::IDomain* domain = ctx.require_domain("game");
        if (domain == nullptr) {
            return;
        }

        (void)bakuon::core::component<Position>(*domain, "game.Position");
        (void)bakuon::core::component<Velocity>(*domain, "game.Velocity");

        auto* system = new MovementSystem{};
        system->set_events(&ctx.events());
        m_system = system;

        if (!ctx.scheduler().register_system(system, domain->handle())) {
            delete system;
            m_system = nullptr;
            return;
        }

        m_loaded = true;
    }

    void on_unload() override
    {
        // System 由 Scheduler 接管所有权；此处只需清本地观察指针。
        // 若宿主先 unload 插件再销毁 World，应在此主动 unregister（需要保存 context 引用）。
        // 本示例依赖宿主在卸载插件前仍持有 World，由 ~Scheduler 统一销毁 System。
        m_system = nullptr;
        m_loaded = false;
    }

    [[nodiscard]] bool loaded() const noexcept { return m_loaded; }

private:
    MovementSystem* m_system = nullptr; // 非拥有
    bool m_loaded            = false;
};

} // namespace

// ---------------------------------------------------------------------------
// C 导出
// ---------------------------------------------------------------------------

extern "C" BAKUON_PLUGIN_EXPORT bakuon::core::IPlugin* bakuon_create_plugin()
{
    return new MovementPlugin{};
}

extern "C" BAKUON_PLUGIN_EXPORT void bakuon_destroy_plugin(bakuon::core::IPlugin* plugin)
{
    delete plugin;
}
