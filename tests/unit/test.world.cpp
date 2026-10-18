#include "b_bakuon.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

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

static_assert(Domain::from_name("physics").named());
static_assert(!Domain::from_name("").valid());
static_assert(Domain::from_name("a") != Domain::from_name("b"));

TEST(WorldTest, RequireCreatesOnDemandAndIsIdempotent)
{
    World world;
    EXPECT_EQ(world.count(), 0u);
    EXPECT_EQ(world.find("physics"), nullptr);

    IDomain* a = world.require("physics");
    IDomain* b = world.require("physics");
    EXPECT_EQ(a, b);
    EXPECT_EQ(world.count(), 1u);
    EXPECT_STREQ(a->name(), "physics");
    EXPECT_EQ(world.find(Domain::from_name("physics")), a);
}

TEST(WorldTest, CreateReturnsNullptrWhenAlreadyExistsAndThrowsOnEmptyName)
{
    World world;
    EXPECT_NE(world.create("a"), nullptr);
    EXPECT_EQ(world.create("a"), nullptr);
    EXPECT_THROW((void) world.create(""), WorldError);
    EXPECT_THROW((void) world.require(""), WorldError);
}

TEST(WorldTest, DomainAddressIsStableAcrossGrowth)
{
    World world;
    IDomain* first = world.require("d0");
    for (int i = 1; i < 200; ++i) {
        (void) world.require("d" + std::to_string(i));
    }
    EXPECT_EQ(world.count(), 200u);
    EXPECT_EQ(world.require("d0"), first);
}

TEST(WorldTest, HandleGoesStaleAfterDestroyEvenIfRecreatedWithSameName)
{
    World world;
    const DomainHandle old = world.require("doc")->handle();
    ASSERT_EQ(world.resolve(old), world.find("doc"));

    ASSERT_TRUE(world.destroy(old.id()) == Status::ok);
    EXPECT_EQ(world.resolve(old), nullptr);
    EXPECT_FALSE(world.destroy(old.id()) == Status::ok) << "重复销毁应返回 false";

    IDomain* reborn = world.require("doc");
    EXPECT_EQ(world.resolve(old), nullptr) << "同名重建不应让旧 handle 复活";
    EXPECT_EQ(world.resolve(reborn->handle()), reborn);
    EXPECT_NE(reborn->serial(), old.serial());
}

TEST(WorldTest, AnonymousDomainsGetDistinctNonNamedIds)
{
    World world;
    IDomain* a = world.create_anonymous("doc-1");
    IDomain* b = world.create_anonymous("doc-2");
    EXPECT_NE(a->id(), b->id());
    EXPECT_FALSE(a->id().named());
    EXPECT_TRUE(a->id().valid());
    EXPECT_EQ(world.count(), 2u);
}

TEST(WorldTest, HooksFireAndDisconnect)
{
    std::vector<std::string> log;
    World world;
    Connection created = world.on_domain_created(
        [&](IDomain& d) { log.push_back("created:" + std::string(d.name())); });
    Connection destroying = world.on_domain_destroying([&](IDomain& d) {
        log.push_back("destroying:" + std::string(d.name())
                      + (world.find(d.id()) ? ":visible" : ":hidden"));
    });

    (void) world.require("x");
    ASSERT_TRUE(world.destroy(Domain::from_name("x")) == Status::ok);
    EXPECT_EQ(log, (std::vector<std::string>{"created:x", "destroying:x:visible"}));

    created.disconnect();
    (void) world.require("y");
    EXPECT_EQ(log.size(), 2u) << "断开后不应再收到 created 通知";
}

TEST(WorldTest, ConnectionMayOutliveWorld)
{
    Connection conn;
    {
        World world;
        conn = world.on_domain_created([](IDomain&) {});
    }
    conn.disconnect(); // 不应崩溃（ASan 下验证无 use-after-free）
}

TEST(WorldTest, ReentrantDestroyOnClosingDomainIsRejected)
{
    World world;
    bool nested     = true;
    Connection conn = world.on_domain_destroying(
        [&](IDomain& d) { nested = world.destroy(d.id()) == Status::ok; });

    (void) world.require("r");
    EXPECT_EQ(world.destroy(Domain::from_name("r")), Status::ok);
    EXPECT_FALSE(nested);
    EXPECT_EQ(world.count(), 0u);
}

TEST(WorldTest, ClearDestroysInReverseCreationOrder)
{
    std::vector<std::string> log;
    World world;
    Connection conn = world.on_domain_destroying(
        [&](IDomain& d) { log.push_back(std::string(d.name())); });

    (void) world.require("a");
    (void) world.require("b");
    (void) world.require("c");
    world.clear();

    EXPECT_EQ(log, (std::vector<std::string>{"c", "b", "a"}));
    EXPECT_EQ(world.count(), 0u);
}

TEST(WorldTest, EachToleratesDestroyingOtherDomainsDuringIteration)
{
    World world;
    (void) world.require("a");
    (void) world.require("b");
    (void) world.require("c");

    std::vector<std::string> visited;
    world.each([&](IDomain* d) {
        visited.emplace_back(d->name());
        if (std::strcmp(d->name(), "a") == 0) {
            (void) world.destroy(Domain::from_name("b"));
        }
    });
    EXPECT_EQ(visited, (std::vector<std::string>{"a", "c"}));
}
