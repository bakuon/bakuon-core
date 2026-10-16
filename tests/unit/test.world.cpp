#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "b_entity.h"
#include "b_world.h"

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
