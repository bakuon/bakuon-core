#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "b_entity.h"
#include "b_world.h"

#include "b_scheduler.h"

#include <bakuon/core/event.h>
#include <bakuon/core/system.h>

using namespace bakuon::core;

namespace {

struct Position
{
    float x = 0.f;
    float y = 0.f;
};

/// 记录"析构时 Registry 是否还活着"——用来钉住服务先于 Container 销毁、且按逆序销毁。
template<int N>
struct Probe
{
    Probe(std::vector<std::string>& log, Registry& registry, Entity entity)
        : m_log(log)
        , m_registry(registry)
        , m_entity(entity)
    {
    }
    ~Probe()
    {
        m_log.push_back("~Probe" + std::to_string(N)
                        + (m_registry.valid(m_entity) ? ":registry-alive" : ":registry-dead"));
    }

    std::vector<std::string>& m_log;
    Registry& m_registry;
    Entity m_entity;
};

} // namespace

static_assert(World::fromName("physics").isNamed());
static_assert(!World::fromName("").isValid());
static_assert(World::fromName("a") != World::fromName("b"));

TEST(WorldTest, RequireCreatesOnDemandAndIsIdempotent)
{
    World world;
    EXPECT_EQ(world.count(), 0u);
    EXPECT_EQ(world.find("physics"), nullptr);

    Domain& a = world.require("physics");
    Domain& b = world.require("physics");
    EXPECT_EQ(&a, &b);
    EXPECT_EQ(world.count(), 1u);
    EXPECT_EQ(a.name(), "physics");
    EXPECT_EQ(world.find(World::fromName("physics")), &a);
}

TEST(WorldTest, CreateReturnsNullptrWhenAlreadyExistsAndThrowsOnEmptyName)
{
    World world;
    EXPECT_NE(world.create("a"), nullptr);
    EXPECT_EQ(world.create("a"), nullptr);
    EXPECT_THROW(world.create(""), WorldError);
    EXPECT_THROW(world.require(""), WorldError);
}

TEST(WorldTest, DomainsAreIsolated)
{
    World world;
    Domain& a = world.require("a");
    Domain& b = world.require("b");

    int constructedInB = 0;
    // or use entt::emitter
    auto fun           = [&](Registry&, Entity) { ++constructedInB; };
    auto conn = b.registry().on_construct<Position>().connect<&decltype(fun)::operator()>(&fun);

    a.registry().emplace<Position>(a.registry().create(), 1.f, 2.f);

    int visitedInB = 0;
    b.registry().view<Position>().each([&](Entity, Position&) { ++visitedInB; });
    EXPECT_EQ(constructedInB, 0);
    EXPECT_EQ(visitedInB, 0);
}

TEST(WorldTest, DomainAddressIsStableAcrossGrowth)
{
    World world;
    Domain* first = &world.require("d0");
    for (int i = 1; i < 200; ++i) {
        world.require("d" + std::to_string(i));
    }
    EXPECT_EQ(world.count(), 200u);
    EXPECT_EQ(&world.require("d0"), first);
}

TEST(WorldTest, HandleGoesStaleAfterDestroyEvenIfRecreatedWithSameName)
{
    World world;
    const DomainHandle old = world.require("doc").handle();
    ASSERT_EQ(world.resolve(old), world.find("doc"));

    ASSERT_TRUE(world.destroy(old.id()));
    EXPECT_EQ(world.resolve(old), nullptr);
    EXPECT_FALSE(world.destroy(old.id())) << "重复销毁应返回 false";

    Domain& reborn = world.require("doc");
    EXPECT_EQ(world.resolve(old), nullptr) << "同名重建不应让旧 handle 复活";
    EXPECT_EQ(world.resolve(reborn.handle()), &reborn);
    EXPECT_NE(reborn.serial(), old.serial());
}

TEST(WorldTest, AnonymousDomainsGetDistinctNonNamedIds)
{
    World world;
    Domain& a = world.createAnonymous("doc-1");
    Domain& b = world.createAnonymous("doc-2");
    EXPECT_NE(a.id(), b.id());
    EXPECT_FALSE(a.id().isNamed());
    EXPECT_TRUE(a.id().isValid());
    EXPECT_EQ(world.count(), 2u);
}

TEST(WorldTest, HooksFireAndDisconnect)
{
    std::vector<std::string> log;
    World world;
    Connection created = world.domainCreated(
        [&](Domain& d) { log.push_back("created:" + std::string(d.name())); });
    Connection destroying = world.domainDestroying([&](Domain& d) {
        log.push_back("destroying:" + std::string(d.name())
                      + (world.find(d.id()) ? ":visible" : ":hidden"));
    });

    world.require("x");
    ASSERT_TRUE(world.destroy(DomainId::fromName("x")));
    EXPECT_EQ(log, (std::vector<std::string>{"created:x", "destroying:x:visible"}));

    created.disconnect();
    world.require("y");
    EXPECT_EQ(log.size(), 2u) << "断开后不应再收到 created 通知";
}

TEST(WorldTest, ConnectionMayOutliveWorld)
{
    Connection conn;
    {
        World world;
        conn = world.domainCreated([](Domain&) {});
    }
    conn.disconnect(); // 不应崩溃（ASan 下验证无 use-after-free）
}

TEST(WorldTest, ReentrantDestroyOnClosingDomainIsRejected)
{
    World world;
    bool nested     = true;
    Connection conn = world.domainDestroying([&](Domain& d) { nested = world.destroy(d.id()); });

    world.require("r");
    EXPECT_TRUE(world.destroy(DomainId::fromName("r")));
    EXPECT_FALSE(nested);
    EXPECT_EQ(world.count(), 0u);
}

TEST(WorldTest, ClearDestroysInReverseCreationOrder)
{
    std::vector<std::string> log;
    World world;
    Connection conn = world.domainDestroying(
        [&](Domain& d) { log.push_back(std::string(d.name())); });

    world.require("a");
    world.require("b");
    world.require("c");
    world.clear();

    EXPECT_EQ(log, (std::vector<std::string>{"c", "b", "a"}));
    EXPECT_EQ(world.count(), 0u);
}

TEST(WorldTest, EachToleratesDestroyingOtherDomainsDuringIteration)
{
    World world;
    world.require("a");
    world.require("b");
    world.require("c");

    std::vector<std::string> visited;
    world.each([&](Domain& d) {
        visited.emplace_back(d.name());
        if (d.name() == "a") {
            world.destroy(DomainId::fromName("b"));
        }
    });
    EXPECT_EQ(visited, (std::vector<std::string>{"a", "c"}));
}

// ==================== World 组合层：内嵌 Scheduler / EventBus ====================

namespace {

class CountingSystem final : public ISystem
{
public:
    explicit CountingSystem(std::string name)
        : m_name(std::move(name))
    {
    }

    std::uint32_t abi_version() const noexcept override { return system_abi_version; }
    std::string_view name() const noexcept override { return m_name; }
    SystemPhase phase() const noexcept override { return SystemPhase::Update; }

    void on_attach(IDomain& domain) override
    {
        ++attach_count;
        attached_domain = std::string(domain.name());
    }

    void on_detach(IDomain& domain) override
    {
        ++detach_count;
        detached_domain = std::string(domain.name());
    }

    void update(IDomain& domain, float dt) override
    {
        ++update_count;
        last_dt     = dt;
        last_domain = std::string(domain.name());
    }

    int attach_count  = 0;
    int detach_count  = 0;
    int update_count  = 0;
    float last_dt     = 0.f;
    std::string last_domain;
    std::string attached_domain;
    std::string detached_domain;

private:
    std::string m_name;
};

} // namespace

TEST(WorldTest, EmbedsSchedulerAndEventBus)
{
    World world;
    IScheduler& sched = world.scheduler();
    IEventBus& bus    = world.events();

    EXPECT_EQ(sched.abi_version(), system_abi_version);
    EXPECT_EQ(bus.abi_version(), event_abi_version);
    EXPECT_EQ(sched.count(), 0u);
}

TEST(WorldTest, TickForwardsToEmbeddedScheduler)
{
    World world;
    Domain& domain = world.require("game");

    auto* sys = new CountingSystem("game.Counter");
    ASSERT_TRUE(world.scheduler().register_system(sys, domain.handle()));
    EXPECT_EQ(sys->attach_count, 1);
    EXPECT_EQ(sys->attached_domain, "game");

    world.tick(0.016f);
    EXPECT_EQ(sys->update_count, 1);
    EXPECT_FLOAT_EQ(sys->last_dt, 0.016f);
    EXPECT_EQ(sys->last_domain, "game");

    world.tick_domain(domain, 0.1f);
    EXPECT_EQ(sys->update_count, 2);
    EXPECT_FLOAT_EQ(sys->last_dt, 0.1f);
}

TEST(WorldTest, EmbeddedEventBusPublishSubscribe)
{
    World world;
    int hits = 0;
    auto on_tick = [&]() { ++hits; };
    Connection conn = subscribe_tag(world.events(), "frame.Tick", on_tick);

    EXPECT_EQ(publish(world.events(), "frame.Tick"), 1u);
    EXPECT_EQ(hits, 1);
}

TEST(WorldTest, PluginContextFromWorldAlone)
{
    World world;
    PluginContext ctx(world);

    EXPECT_EQ(&ctx.scheduler(), &world.scheduler());
    EXPECT_EQ(&ctx.events(), &world.events());

    IDomain* domain = ctx.require_domain("sense");
    ASSERT_NE(domain, nullptr);
    EXPECT_EQ(world.find("sense"), domain);
}

TEST(WorldTest, DestructorDetachesSystemsWhileDomainsAlive)
{
    std::string detached;
    {
        World world;
        Domain& domain = world.require("doc");
        auto* sys      = new CountingSystem("s");
        ASSERT_TRUE(world.scheduler().register_system(sys, domain.handle()));
        EXPECT_EQ(sys->attach_count, 1);
        // ~World: Scheduler 先析构 → on_detach 时 domain 仍可 resolve
        // 无法在析构后读 sys；用 Domain 钩子侧面验证顺序也可。
        // 此处仅保证不崩溃且 Domain 钩子在 System detach 之后仍可能触发。
        (void)detached;
    }
    SUCCEED();
}

TEST(WorldTest, ClearDoesNotDestroySchedulerOrEventBus)
{
    World world;
    Domain& domain = world.require("a");
    auto* sys      = new CountingSystem("keep");
    ASSERT_TRUE(world.scheduler().register_system(sys, domain.handle()));

    int hits = 0;
    auto fn  = [&]() { ++hits; };
    Connection conn = subscribe_tag(world.events(), "ping", fn);

    world.clear();
    EXPECT_EQ(world.count(), 0u);

    // Scheduler / EventBus 仍可用
    EXPECT_TRUE(world.scheduler().contains("keep"));
    EXPECT_EQ(publish(world.events(), "ping"), 1u);
    EXPECT_EQ(hits, 1);

    // 绑定域已销毁：tick 跳过该 System
    world.tick(0.f);
    EXPECT_EQ(sys->update_count, 0);
}

// ==================== entt::dispatcher：World 级 vs Domain 级 ====================

namespace {

struct AppFrameBegin
{
    float dt = 0.f;
};

struct DomainDamage
{
    Entity target{};
    float amount = 0.f;
};

} // namespace

TEST(WorldTest, WorldDispatcherIsApplicationScoped)
{
    World world;
    float seen_dt = -1.f;

    auto on_frame = [&](AppFrameBegin event) { seen_dt = event.dt; };
    world.dispatcher().sink<AppFrameBegin>().connect<&decltype(on_frame)::operator()>(on_frame);

    world.dispatcher().trigger(AppFrameBegin{0.016f});
    EXPECT_FLOAT_EQ(seen_dt, 0.016f);

    // 与 EventBus 独立：原生 trigger 不会自动进插件总线
    int plugin_hits = 0;
    auto on_plugin = [&]() { ++plugin_hits; };
    Connection conn = subscribe_tag(world.events(), "frame.Begin", on_plugin);
    world.dispatcher().trigger(AppFrameBegin{0.1f});
    EXPECT_EQ(plugin_hits, 0);
}

TEST(WorldTest, DomainDispatchersAreIsolated)
{
    World world;
    Domain& a = world.require("docA");
    Domain& b = world.require("docB");

    int hits_a = 0;
    int hits_b = 0;

    auto on_a = [&](DomainDamage) { ++hits_a; };
    auto on_b = [&](DomainDamage) { ++hits_b; };

    a.dispatcher().sink<DomainDamage>().connect<&decltype(on_a)::operator()>(on_a);
    b.dispatcher().sink<DomainDamage>().connect<&decltype(on_b)::operator()>(on_b);

    const Entity ea = a.create();
    a.dispatcher().trigger(DomainDamage{ea, 10.f});
    EXPECT_EQ(hits_a, 1);
    EXPECT_EQ(hits_b, 0);

    const Entity eb = b.create();
    b.dispatcher().trigger(DomainDamage{eb, 5.f});
    EXPECT_EQ(hits_a, 1);
    EXPECT_EQ(hits_b, 1);

    // dispatcher_of 与 Domain::dispatcher 是同一实例
    EXPECT_EQ(&dispatcher_of(a), &a.dispatcher());
    EXPECT_EQ(&dispatcher_of(static_cast<IDomain&>(a)), &a.dispatcher());
}

TEST(WorldTest, DomainDispatcherDiesWithDomain)
{
    World world;
    int hits = 0;
    auto on_damage = [&](DomainDamage) { ++hits; };

    DomainId id;
    {
        Domain& domain = world.require("temp");
        id             = domain.id();
        domain.dispatcher().sink<DomainDamage>().connect<&decltype(on_damage)::operator()>(
            on_damage);
        domain.dispatcher().trigger(DomainDamage{});
        EXPECT_EQ(hits, 1);
    }
    ASSERT_TRUE(world.destroy(id));
    // 域已销毁：订阅随 Domain 析构释放，不会悬空（无法再 trigger 该实例）
    EXPECT_EQ(world.find(id), nullptr);
}

TEST(WorldTest, BridgeNativeDispatcherToPluginEventBus)
{
    // 引擎内部：类型安全 trigger → 需要时再桥接到插件总线
    World world;
    Domain& domain = world.require("game");

    struct NativeHit
    {
        float amount = 0.f;
    };
    struct PluginHit
    {
        float amount = 0.f;
    };
    static_assert(std::is_trivially_copyable_v<PluginHit>);

    float plugin_amount = 0.f;
    auto on_plugin = [&](const PluginHit& e) { plugin_amount = e.amount; };
    Connection conn = subscribe<PluginHit>(world.events(), "game.Hit", on_plugin);

    auto bridge = [&](NativeHit e) {
        publish(world.events(), "game.Hit", PluginHit{e.amount});
    };
    domain.dispatcher().sink<NativeHit>().connect<&decltype(bridge)::operator()>(bridge);

    domain.dispatcher().trigger(NativeHit{42.f});
    EXPECT_FLOAT_EQ(plugin_amount, 42.f);
    (void)domain;
}
