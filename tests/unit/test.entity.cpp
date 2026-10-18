#include "b_bakuon.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <unordered_set>

using namespace bakuon::core;

static_assert(Entity{} != null, "Entity{} is not null");
static_assert(null == Entity::null());

TEST(EntityTest, NullSemantics)
{
    constexpr Entity zero{};
    EXPECT_FALSE(zero.is_null());
    EXPECT_TRUE(zero != null);
    EXPECT_TRUE(null != zero);

    Entity e = null;
    EXPECT_TRUE(e.is_null());
    EXPECT_TRUE(e == null);
    EXPECT_TRUE(null == e);
    EXPECT_FALSE(e != null);

    const Entity odd{0xABCFFFFFu}; // version ignored, like the backend's null
    EXPECT_TRUE(odd.is_null());
    EXPECT_TRUE(odd == null);
    EXPECT_TRUE(odd != Entity::null());
}

TEST(EntityTest, Parts)
{
    const Entity e = Entity::make(5, 3);
    EXPECT_EQ(e.index(), 5u);
    EXPECT_EQ(e.version(), 3u);
    EXPECT_EQ(e.value(), (3u << 20) | 5u);
    EXPECT_EQ(static_cast<std::uint32_t>(e), e.value());

    const Entity masked = Entity::make(0xFFFFFFFFu, 0xFFFFFFFFu);
    EXPECT_EQ(masked.index(), Entity::index_mask);
    EXPECT_EQ(masked.version(), Entity::version_mask);
}

TEST(EntityTest, TrivialCopy)
{
    const Entity a = Entity::make(42, 7);
    Entity b;
    std::memcpy(&b, &a, sizeof(Entity));
    EXPECT_TRUE(a == b);
}

TEST(EntityTest, Hash)
{
    std::unordered_set<Entity> set;
    set.insert(Entity::make(1, 0));
    set.insert(Entity::make(1, 0));
    set.insert(Entity::make(1, 1));
    EXPECT_EQ(set.size(), 2u);
    EXPECT_EQ(set.count(Entity::make(1, 1)), 1u);
}

TEST(EntityTest, BackendInterop)
{
    Registry r;
    const Entity e = r.create();
    EXPECT_TRUE(e == Entity{}); // first entity is the valid entity 0
    EXPECT_TRUE(r.valid(e));

    const Entity backend_null = entt::null;
    EXPECT_TRUE(backend_null.is_null());
    EXPECT_TRUE(backend_null == null);
    EXPECT_FALSE(r.valid(backend_null));

    r.emplace<int>(e, 42);
    EXPECT_EQ(r.get<int>(e), 42);

    EntityHandle h{r, e};
    EXPECT_TRUE(h.valid());

    r.destroy(e);
    EXPECT_FALSE(r.valid(e));
}
