#include <gtest/gtest.h>

#include "b_domain.h"
#include "b_world.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace bakuon::core;

struct Position
{
    float x;
    float y;
};

struct PositionBad
{
    float x;
    float y;
    float z;
};

struct Velocity
{
    float x;
    float y;
};

struct alignas(64) Wind
{
    float speed;
};

struct DeadTag
{
};

// ==================== 测试夹具 ====================

class DomainTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        world  = std::make_unique<World>();
        domain = world->create("sense");
        ASSERT_NE(domain, nullptr);

        // 注册组件
        position = component<Position>(*domain, "game.Position");
        velocity = component<Velocity>(*domain, "game.Velocity");
        wind     = component<Wind>(*domain, "game.Wind");
        dead     = component<DeadTag>(*domain, "game.DeadTag");
    }

    void TearDown() override
    {
        if (domain) {
            world->destroy(domain->id());
        }
    }

    std::unique_ptr<World> world;
    Domain* domain = nullptr;

    Component<Position> position;
    Component<Velocity> velocity;
    Component<Wind> wind;
    Component<DeadTag> dead;
};

// ==================== Entity 基础测试 ====================

TEST(EntityTest, NullAndDefaultConstruction)
{
    // 默认构造的实体不等于 null
    EXPECT_NE(Entity{}, null);
    // 默认构造的实体值为 0
    EXPECT_EQ((Entity{}).value(), 0);
    // null 实体判断
    EXPECT_TRUE(Entity::null().isNull());
    EXPECT_FALSE(Entity{}.isNull());
}

// ==================== Domain 基础测试 ====================

TEST_F(DomainTest, DomainCreationAndAbi)
{
    // ABI 版本检查
    EXPECT_EQ(domain->abi_version(), domain_abi_version);
    // 原生组件跟随实体
    EXPECT_TRUE(bakuon_native_components_follow_entity(*domain));
}

// ==================== 组件注册测试 ====================

TEST_F(DomainTest, ComponentRegistration)
{
    // 所有组件应成功注册
    EXPECT_TRUE(position);
    EXPECT_TRUE(velocity);
    EXPECT_TRUE(wind);
    EXPECT_TRUE(dead);

    // 相同名称获取相同组件句柄
    EXPECT_EQ(component<Position>(*domain, "game.Position"), position);
    // 不同名称获取不同组件但结构(size/align)相同(见 register_component注释)的组件句柄
    EXPECT_TRUE(component<Velocity>(*domain, "game.Position"));
    // 类型不匹配获取失败
    EXPECT_FALSE(component<PositionBad>(*domain, "game.Position"));
}

// ==================== 实体创建测试 ====================

TEST_F(DomainTest, EntityCreation)
{
    const Entity first  = domain->create();
    const Entity second = domain->create();
    const Entity third  = domain->create();

    // 创建的实体应有效
    EXPECT_TRUE(domain->valid(first));
    EXPECT_NE(first, null);

    // 不能给 null 实体放置组件
    EXPECT_FALSE(domain->emplace(null, position.id(), nullptr, nullptr));
}

// ==================== 组件 emplace/get 测试 ====================

TEST_F(DomainTest, ComponentEmplaceAndGet)
{
    const Entity entity = domain->create();

    // 放置 Position 组件
    Position* pos = emplace(*domain, entity, position, Position{1.f, 2.f});
    ASSERT_NE(pos, nullptr);
    EXPECT_FLOAT_EQ(pos->x, 1.f);

    // 修改后可通过 get 读到
    pos->x = 4.f;
    ASSERT_NE(get(*domain, entity, position), nullptr);
    EXPECT_FLOAT_EQ(get(*domain, entity, position)->x, 4.f);

    // 重复 emplace 返回 nullptr（已存在）
    EXPECT_EQ(emplace(*domain, entity, position, Position{}), nullptr);
}

// ==================== 组件 add/has/remove 测试 ====================

TEST_F(DomainTest, ComponentAddHasRemove)
{
    const Entity first  = domain->create();
    const Entity second = domain->create();

    // 给 first 添加 velocity
    EXPECT_TRUE(emplace(*domain, first, velocity, Velocity{3.f, 4.f}));
    // 给 second 添加 position 和 velocity
    EXPECT_TRUE(emplace(*domain, second, position, Position{8.f, 9.f}));
    EXPECT_TRUE(emplace(*domain, second, velocity, Velocity{-1.f, 0.f}));

    // 添加 Tag 组件
    EXPECT_TRUE(add(*domain, second, dead));
    EXPECT_TRUE(has(*domain, second, dead));
    EXPECT_NE(get(*domain, second, position), nullptr);
    EXPECT_FALSE(has(*domain, first, dead));

    // 移除组件
    EXPECT_TRUE(remove(*domain, first, velocity));
    EXPECT_FALSE(has(*domain, first, velocity));
}

// ==================== 组件内存对齐测试 ====================

TEST_F(DomainTest, ComponentAlignment)
{
    const Entity entity = domain->create();

    Wind* gust = emplace(*domain, entity, wind, Wind{12.f});
    ASSERT_NE(gust, nullptr);
    // 验证 64 字节对齐
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(gust) % 64u, 0u);
}

// ==================== 组件稳定性测试 ====================

TEST_F(DomainTest, ComponentPointerStability)
{
    const Entity first = domain->create();
    const Entity third = domain->create();

    ASSERT_NE(emplace(*domain, first, position, Position{4.f, 2.f}), nullptr);
    const Position* stable = get(*domain, first, position);
    ASSERT_NE(stable, nullptr);

    // 给另一个实体添加组件不应使现有指针失效
    EXPECT_TRUE(emplace(*domain, third, position, Position{0.f, 1.f}));
    EXPECT_FLOAT_EQ(stable->x, 4.f);
}

// ==================== View 迭代测试 ====================

TEST_F(DomainTest, ViewIteration)
{
    const Entity first  = domain->create();
    const Entity second = domain->create();

    ASSERT_TRUE(emplace(*domain, first, position, Position{4.f, 2.f}));
    ASSERT_TRUE(emplace(*domain, first, velocity, Velocity{3.f, 4.f}));
    ASSERT_TRUE(emplace(*domain, second, position, Position{8.f, 9.f}));
    ASSERT_TRUE(emplace(*domain, second, velocity, Velocity{-1.f, 0.f}));

    int pairs = 0;
    view(*domain, position, velocity).each([&](Entity entity, Position& p, Velocity& v) {
        EXPECT_TRUE(entity == first || entity == second);
        if (entity == first) {
            EXPECT_FLOAT_EQ(p.x, 4.f);
            EXPECT_FLOAT_EQ(v.x, 3.f);
        }
        ++pairs;
    });
    EXPECT_EQ(pairs, 2);

    // 组件计数
    EXPECT_EQ(domain->count(position.id()), 2);

    // 移除 first 的 velocity 后，view 只能匹配到 second
    ASSERT_TRUE(remove(*domain, first, velocity));
    pairs = 0;
    view(*domain, position, velocity).each([&](Entity, Position&, Velocity&) { ++pairs; });
    EXPECT_EQ(pairs, 1);
}

// ==================== 实体销毁测试 ====================

TEST_F(DomainTest, EntityDestruction)
{
    const Entity second = domain->create();
    ASSERT_TRUE(emplace(*domain, second, position, Position{8.f, 9.f}));
    ASSERT_TRUE(add(*domain, second, dead));

    const Entity stale = second;
    EXPECT_TRUE(domain->destroy(second));

    // 销毁后实体失效
    EXPECT_FALSE(domain->valid(stale));
    EXPECT_EQ(get(*domain, stale, position), nullptr);
    EXPECT_FALSE(has(*domain, stale, dead));

    // 重复销毁返回 false
    EXPECT_FALSE(domain->destroy(stale));
}

// ==================== 批量操作测试 ====================

TEST_F(DomainTest, BatchCreateAndRemove)
{
    // 先准备基础数据
    const Entity first = domain->create();
    const Entity third = domain->create();
    ASSERT_TRUE(emplace(*domain, first, position, Position{4.f, 2.f}));
    ASSERT_TRUE(emplace(*domain, third, wind, Wind{12.f}));
    ASSERT_TRUE(emplace(*domain, third, position, Position{0.f, 1.f}));

    // 批量创建 8 个实体
    Entity entities[8];
    for (int i = 0; i < 8; ++i) {
        entities[i] = domain->create();
        EXPECT_TRUE(emplace(*domain, entities[i], velocity, Velocity{static_cast<float>(i), 0.f}));
    }

    // view 遍历的实体都应有效
    view(*domain, velocity).each([&](Entity entity, Velocity&) {
        EXPECT_TRUE(domain->valid(entity));
    });

    // 隔一个移除一个
    std::uint32_t removed = 0;
    for (int i = 0; i < 8; i += 2) {
        removed += domain->remove(entities[i], velocity.id()) ? 1u : 0u;
    }
    EXPECT_EQ(removed, 4u);
}

// ==================== 组件注销测试 ====================

TEST_F(DomainTest, ComponentUnregistration)
{
    const Entity third = domain->create();
    ASSERT_TRUE(emplace(*domain, third, wind, Wind{12.f}));

    // 注销组件
    EXPECT_TRUE(domain->unregister_component(wind.id()));
    EXPECT_FALSE(has(*domain, third, wind));
    EXPECT_EQ(get(*domain, third, wind), nullptr);
}

// ==================== World 销毁测试 ====================

TEST_F(DomainTest, WorldDestruction)
{
    // TearDown 中已经会调用 world->destroy，这里额外测试显式销毁场景
    Domain* local_domain = world->create("local_test");
    ASSERT_NE(local_domain, nullptr);

    DomainId id = local_domain->id();
    EXPECT_TRUE(world->destroy(id));
}
