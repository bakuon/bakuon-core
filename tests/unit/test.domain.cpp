#include "b_domain.h"

#include <gtest/gtest.h>

using namespace bakuon::core;

TEST(DomainTest, CreateDestroy)
{
    Domain d(DomainId::from_name("game.test"), "test", 1);
    IDomain& api = d; // exercise through the public interface

    EXPECT_EQ(api.abi_version(), IDomain::abi_current);
    EXPECT_EQ(api.alive_count(), 0u);

    const Entity e = api.create();
    EXPECT_FALSE(e.is_null());
    EXPECT_TRUE(e == Entity{});
    EXPECT_TRUE(api.alive(e));
    EXPECT_EQ(api.alive_count(), 1u);

    EXPECT_TRUE(api.destroy(e));
    EXPECT_FALSE(api.alive(e));
    EXPECT_FALSE(api.destroy(e));
    EXPECT_EQ(api.alive_count(), 0u);
}

TEST(DomainTest, RecycleAndStaleHandle)
{
    Domain d(DomainId::from_name("game.test"), "test", 1);
    const Entity e1 = d.create();
    EXPECT_TRUE(d.destroy(e1));
    const Entity e2 = d.create();
    EXPECT_EQ(e2.index(), e1.index());
    EXPECT_EQ(e2.version(), e1.version() + 1u);
    EXPECT_FALSE(d.alive(e1));
    EXPECT_TRUE(d.alive(e2));
}

TEST(DomainTest, NullIsSafe)
{
    Domain d(DomainId::from_name("game.test"), "test", 1);
    EXPECT_FALSE(d.alive(null));
    EXPECT_FALSE(d.destroy(null));
    EXPECT_FALSE(d.alive(Entity::make(99, 0)));
}

TEST(DomainTest, Hint)
{
    Domain d(DomainId::from_name("game.test"), "test", 1);
    const Entity hint = Entity::make(5, 0);
    const Entity e    = d.create(hint);
    EXPECT_TRUE(e == hint);
    EXPECT_TRUE(d.alive(e));
    EXPECT_EQ(d.alive_count(), 1u);

    const Entity e2 = d.create(null);
    EXPECT_FALSE(e2.is_null());
    EXPECT_EQ(d.alive_count(), 2u);
}

TEST(DomainTest, Clear)
{
    Domain d(DomainId::from_name("game.test"), "test", 1);
    const Entity a = d.create();
    const Entity b = d.create();
    d.clear();
    EXPECT_FALSE(d.alive(a));
    EXPECT_FALSE(d.alive(b));
    EXPECT_EQ(d.alive_count(), 0u);
    EXPECT_FALSE(d.create().is_null());
}

TEST(DomainTest, Isolation)
{
    Domain a(DomainId::from_name("game.test.a"), "test a", 1);
    Domain b(DomainId::from_name("game.test.b"), "test b", 2);
    const Entity ea = a.create();
    EXPECT_TRUE(a.alive(ea));
    EXPECT_FALSE(b.alive(ea));
}

TEST(DomainTest, ExposesComponents)
{
    Domain d(DomainId::from_name("game.x"), "x", 1);
    IDomain& api = d;
    EXPECT_EQ(&api.components(), &d.components());
    EXPECT_EQ(api.components().count(), 0u);
    EXPECT_EQ(api.abi_version(), 1u);
}
