#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "b_scheduler.h"
#include "b_world.h"

#include <bakuon/core/plugin.h>
#include <bakuon/core/system.h>

using namespace bakuon::core;

namespace {

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

/// 记录生命周期与 update 调用顺序的测试 System。
class ProbeSystem final : public ISystem
{
public:
    explicit ProbeSystem(std::string name, SystemPhase phase = SystemPhase::Update,
                         int priority = 0, std::vector<std::string> deps = {})
        : m_name(std::move(name))
        , m_phase(phase)
        , m_priority(priority)
        , m_deps(std::move(deps))
    {
    }

    std::uint32_t abi_version() const noexcept override { return system_abi_version; }
    std::string_view name() const noexcept override { return m_name; }
    SystemPhase phase() const noexcept override { return m_phase; }
    int priority() const noexcept override { return m_priority; }

    std::uint32_t dependency_count() const noexcept override
    {
        return static_cast<std::uint32_t>(m_deps.size());
    }

    std::uint32_t dependency_at(std::uint32_t index, char* out, std::uint32_t cap) const override
    {
        if (index >= m_deps.size()) {
            return 0;
        }
        const std::string& dep = m_deps[index];
        if (out != nullptr && cap > 0) {
            const std::size_t n = (std::min)(dep.size(), static_cast<std::size_t>(cap - 1));
            std::memcpy(out, dep.data(), n);
            out[n] = '\0';
        }
        return static_cast<std::uint32_t>(dep.size() + 1);
    }

    void on_attach(IDomain& domain) override
    {
        ++attach_count;
        last_attach_domain = domain.name();
    }

    void on_detach(IDomain& domain) override
    {
        ++detach_count;
        last_detach_domain = domain.name();
    }

    void on_enable() override { ++enable_count; }
    void on_disable() override { ++disable_count; }

    void update(IDomain& domain, float dt) override
    {
        ++update_count;
        last_dt     = dt;
        last_domain = domain.name();
        if (order_log != nullptr) {
            order_log->push_back(m_name);
        }
    }

    // 测试可观察状态
    int attach_count  = 0;
    int detach_count  = 0;
    int enable_count  = 0;
    int disable_count = 0;
    int update_count  = 0;
    float last_dt     = 0.f;
    std::string last_domain;
    std::string last_attach_domain;
    std::string last_detach_domain;
    std::vector<std::string>* order_log = nullptr;

private:
    std::string m_name;
    SystemPhase m_phase;
    int m_priority;
    std::vector<std::string> m_deps;
};

class MovementSystem final : public ISystem
{
public:
    std::uint32_t abi_version() const noexcept override { return system_abi_version; }
    std::string_view name() const noexcept override { return "game.Movement"; }
    SystemPhase phase() const noexcept override { return SystemPhase::Update; }

    void on_attach(IDomain& domain) override
    {
        m_pos = component<Position>(domain, "game.Position");
        m_vel = component<Velocity>(domain, "game.Velocity");
    }

    void update(IDomain& domain, float dt) override
    {
        if (!m_pos || !m_vel) {
            return;
        }
        view(domain, m_pos, m_vel).each([dt](Entity, Position& p, Velocity& v) {
            p.x += v.x * dt;
            p.y += v.y * dt;
        });
        ++ticks;
    }

    int ticks = 0;

private:
    Component<Position> m_pos;
    Component<Velocity> m_vel;
};

class DummyPlugin final : public IPlugin
{
public:
    std::uint32_t abi_version() const noexcept override { return plugin_abi_version; }
    std::string_view name() const noexcept override { return "test.Dummy"; }
    std::string_view version() const noexcept override { return "1.0.0"; }

    void on_load(IPluginContext& ctx) override
    {
        loaded = true;
        IDomain* domain = ctx.require_domain("game");
        ASSERT_NE(domain, nullptr);
        (void)component<Position>(*domain, "game.Position");
        (void)component<Velocity>(*domain, "game.Velocity");

        auto* sys = new MovementSystem{};
        movement  = sys;
        ASSERT_TRUE(ctx.scheduler().register_system(sys, domain->handle()));
    }

    void on_unload() override
    {
        unloaded = true;
        // 主动卸载；即使不写，Scheduler 析构也会安全销毁
        if (movement != nullptr) {
            // 名字已知
        }
    }

    bool loaded   = false;
    bool unloaded = false;
    MovementSystem* movement = nullptr; // 非拥有
};

} // namespace

// ==================== 基础注册 / 生命周期 ====================

TEST(SystemTest, RegisterAndLifecycle)
{
    World world;
    Domain& domain = world.require("sense");
    Scheduler scheduler(world);

    auto* probe = new ProbeSystem("test.Probe");
    ASSERT_TRUE(scheduler.register_system(probe, domain.handle()));
    EXPECT_TRUE(scheduler.contains("test.Probe"));
    EXPECT_EQ(scheduler.count(), 1u);
    EXPECT_EQ(probe->attach_count, 1);
    EXPECT_EQ(probe->enable_count, 1);
    EXPECT_EQ(probe->last_attach_domain, "sense");

    EXPECT_TRUE(scheduler.unregister_system("test.Probe"));
    EXPECT_FALSE(scheduler.contains("test.Probe"));
    EXPECT_EQ(scheduler.count(), 0u);
    // unregister 后 probe 已销毁，不能再访问
}

TEST(SystemTest, RejectDuplicateNameAndBadAbi)
{
    World world;
    Scheduler scheduler(world);

    ASSERT_TRUE(scheduler.register_system(new ProbeSystem("a")));
    auto* dup = new ProbeSystem("a");
    EXPECT_FALSE(scheduler.register_system(dup));
    EXPECT_NE(std::string_view{scheduler.last_error()}.find("duplicate"), std::string_view::npos);
    delete dup; // 失败时所有权归还

    class BadAbi final : public ISystem
    {
    public:
        std::uint32_t abi_version() const noexcept override { return 999; }
        std::string_view name() const noexcept override { return "bad"; }
        SystemPhase phase() const noexcept override { return SystemPhase::Update; }
        void update(IDomain&, float) override {}
    };
    auto* bad = new BadAbi{};
    EXPECT_FALSE(scheduler.register_system(bad));
    delete bad;
}

TEST(SystemTest, RejectEmptyNameAndNull)
{
    World world;
    Scheduler scheduler(world);
    EXPECT_FALSE(scheduler.register_system(nullptr));

    class EmptyName final : public ISystem
    {
    public:
        std::uint32_t abi_version() const noexcept override { return system_abi_version; }
        std::string_view name() const noexcept override { return {}; }
        SystemPhase phase() const noexcept override { return SystemPhase::Update; }
        void update(IDomain&, float) override {}
    };
    auto* empty = new EmptyName{};
    EXPECT_FALSE(scheduler.register_system(empty));
    delete empty;
}

// ==================== 排序：phase / priority / 依赖 ====================

TEST(SystemTest, OrderByPhaseAndPriority)
{
    World world;
    Domain& domain = world.require("d");
    Scheduler scheduler(world);

    std::vector<std::string> log;
    auto make = [&](const char* name, SystemPhase phase, int pri) {
        auto* s      = new ProbeSystem(name, phase, pri);
        s->order_log = &log;
        ASSERT_TRUE(scheduler.register_system(s, domain.handle()));
    };

    make("post", SystemPhase::PostUpdate, 0);
    make("pre", SystemPhase::PreUpdate, 0);
    make("upd_hi", SystemPhase::Update, 10);
    make("upd_lo", SystemPhase::Update, 0);

    scheduler.tick(0.016f);
    ASSERT_EQ(log.size(), 4u);
    EXPECT_EQ(log[0], "pre");
    EXPECT_EQ(log[1], "upd_lo");
    EXPECT_EQ(log[2], "upd_hi");
    EXPECT_EQ(log[3], "post");
}

TEST(SystemTest, DependencyTopology)
{
    World world;
    Domain& domain = world.require("d");
    Scheduler scheduler(world);

    std::vector<std::string> log;
    auto make = [&](const char* name, std::vector<std::string> deps) {
        auto* s      = new ProbeSystem(name, SystemPhase::Update, 0, std::move(deps));
        s->order_log = &log;
        ASSERT_TRUE(scheduler.register_system(s, domain.handle()));
    };

    // 故意乱序注册：C 依赖 B，B 依赖 A
    make("C", {"B"});
    make("A", {});
    make("B", {"A"});

    scheduler.tick(0.f);
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], "A");
    EXPECT_EQ(log[1], "B");
    EXPECT_EQ(log[2], "C");
}

TEST(SystemTest, RejectDependencyCycle)
{
    World world;
    Scheduler scheduler(world);

    ASSERT_TRUE(scheduler.register_system(new ProbeSystem("A", SystemPhase::Update, 0, {"B"})));
    auto* b = new ProbeSystem("B", SystemPhase::Update, 0, {"A"});
    EXPECT_FALSE(scheduler.register_system(b));
    EXPECT_NE(std::string_view{scheduler.last_error()}.find("cycle"), std::string_view::npos);
    delete b;
}

TEST(SystemTest, RejectCrossPhaseDependencyAllowForwardRef)
{
    World world;
    Scheduler scheduler(world);

    // 前向依赖允许：B 依赖尚未注册的 DoesNotExist，注册成功。
    ASSERT_TRUE(scheduler.register_system(new ProbeSystem("B", SystemPhase::Update, 0, {"DoesNotExist"})));
    EXPECT_TRUE(scheduler.contains("B"));

    ASSERT_TRUE(scheduler.register_system(new ProbeSystem("A")));

    // 跨阶段依赖在依赖已存在时立即拒绝。
    auto* cross = new ProbeSystem("C", SystemPhase::PostUpdate, 0, {"A"});
    EXPECT_FALSE(scheduler.register_system(cross));
    EXPECT_NE(std::string_view{scheduler.last_error()}.find("same phase"), std::string_view::npos);
    delete cross;
}

// ==================== Domain 绑定与 tick ====================

TEST(SystemTest, BoundSystemSkipsDestroyedDomain)
{
    World world;
    Domain& domain = world.require("doc");
    const DomainHandle handle = domain.handle();
    Scheduler scheduler(world);

    auto* probe = new ProbeSystem("bound");
    ASSERT_TRUE(scheduler.register_system(probe, handle));
    // probe 指针在 register 后仍有效（所有权在 scheduler，但对象未销毁）
    EXPECT_EQ(probe->update_count, 0);

    scheduler.tick(0.1f);
    EXPECT_EQ(probe->update_count, 1);

    ASSERT_TRUE(world.destroy(handle.id()));
    scheduler.tick(0.1f);
    EXPECT_EQ(probe->update_count, 1); // 未再增加
}

TEST(SystemTest, GlobalSystemRunsOnEveryDomain)
{
    World world;
    world.require("a");
    world.require("b");
    Scheduler scheduler(world);

    auto* probe = new ProbeSystem("global");
    ASSERT_TRUE(scheduler.register_system(probe)); // 无绑定

    scheduler.tick(0.f);
    EXPECT_EQ(probe->update_count, 2);
}

TEST(SystemTest, TickDomainFilters)
{
    World world;
    Domain& a = world.require("a");
    Domain& b = world.require("b");
    Scheduler scheduler(world);

    auto* onlyA = new ProbeSystem("onlyA");
    auto* global = new ProbeSystem("global");
    ASSERT_TRUE(scheduler.register_system(onlyA, a.handle()));
    ASSERT_TRUE(scheduler.register_system(global));

    scheduler.tick_domain(a, 0.f);
    EXPECT_EQ(onlyA->update_count, 1);
    EXPECT_EQ(global->update_count, 1);

    scheduler.tick_domain(b, 0.f);
    EXPECT_EQ(onlyA->update_count, 1); // 未绑定到 b
    EXPECT_EQ(global->update_count, 2);
}

// ==================== enable / disable ====================

TEST(SystemTest, EnableDisable)
{
    World world;
    Domain& domain = world.require("d");
    Scheduler scheduler(world);

    auto* probe = new ProbeSystem("p");
    ASSERT_TRUE(scheduler.register_system(probe, domain.handle()));
    EXPECT_TRUE(scheduler.is_enabled("p"));

    ASSERT_TRUE(scheduler.set_enabled("p", false));
    EXPECT_FALSE(scheduler.is_enabled("p"));
    EXPECT_EQ(probe->disable_count, 1);

    scheduler.tick(0.f);
    EXPECT_EQ(probe->update_count, 0);

    ASSERT_TRUE(scheduler.set_enabled("p", true));
    EXPECT_EQ(probe->enable_count, 2); // register 时一次 + 本次
    scheduler.tick(0.f);
    EXPECT_EQ(probe->update_count, 1);
}

// ==================== 与组件协作（端到端） ====================

TEST(SystemTest, MovementSystemIntegratesWithDomain)
{
    World world;
    Domain& domain = world.require("game");
    Scheduler scheduler(world);

    auto* movement = new MovementSystem{};
    ASSERT_TRUE(scheduler.register_system(movement, domain.handle()));

    const Entity e = domain.create();
    auto pos = component<Position>(domain, "game.Position");
    auto vel = component<Velocity>(domain, "game.Velocity");
    ASSERT_TRUE(pos);
    ASSERT_TRUE(vel);
    ASSERT_NE(emplace(domain, e, pos, Position{1.f, 2.f}), nullptr);
    ASSERT_NE(emplace(domain, e, vel, Velocity{3.f, 4.f}), nullptr);

    scheduler.tick(0.5f);
    EXPECT_EQ(movement->ticks, 1);

    const Position* p = get(domain, e, pos);
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->x, 1.f + 3.f * 0.5f);
    EXPECT_FLOAT_EQ(p->y, 2.f + 4.f * 0.5f);
}

// ==================== Plugin 入口 ====================

TEST(SystemTest, PluginLoadUnload)
{
    World world;
    PluginContext ctx(world);

    DummyPlugin plugin;
    plugin.on_load(ctx);
    EXPECT_TRUE(plugin.loaded);
    EXPECT_NE(world.find("game"), nullptr);
    EXPECT_TRUE(world.scheduler().contains("game.Movement"));

    // 跑一帧确认 System 工作
    Domain* domain = world.find("game");
    ASSERT_NE(domain, nullptr);
    const Entity e = domain->create();
    auto pos = component<Position>(*domain, "game.Position");
    auto vel = component<Velocity>(*domain, "game.Velocity");
    ASSERT_NE(emplace(*domain, e, pos, Position{0.f, 0.f}), nullptr);
    ASSERT_NE(emplace(*domain, e, vel, Velocity{10.f, 0.f}), nullptr);
    world.tick(1.f);
    ASSERT_NE(plugin.movement, nullptr);
    EXPECT_EQ(plugin.movement->ticks, 1);

    plugin.on_unload();
    EXPECT_TRUE(plugin.unloaded);
}

// ==================== 析构安全 ====================

TEST(SystemTest, DestructorDetachesSafely)
{
    World world;
    Domain& domain = world.require("d");
    {
        Scheduler scheduler(world);
        auto* probe = new ProbeSystem("x");
        ASSERT_TRUE(scheduler.register_system(probe, domain.handle()));
        EXPECT_EQ(probe->attach_count, 1);
        // scheduler 析构应 on_disable + on_detach
    }
    // Domain 仍可用
    EXPECT_TRUE(domain.valid(domain.create()));
}
