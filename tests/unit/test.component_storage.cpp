#include "b_bakuon.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <random>
#include <vector>

using namespace bakuon::core;

TEST(PoolTest, EmplaceGetErase)
{
    Registry reg;
    ComponentStorage pool(sizeof(std::uint32_t), alignof(std::uint32_t));
    const Entity a         = reg.create();
    const Entity b         = reg.create();
    const std::uint32_t va = 7, vb = 9;

    ASSERT_TRUE(pool.emplace(a, &va, nullptr) != nullptr);
    ASSERT_TRUE(pool.emplace(b, &vb, nullptr) != nullptr);
    EXPECT_EQ(pool.size(), 2u);
    EXPECT_EQ(*static_cast<const std::uint32_t*>(pool.get(a)), 7u);

    EXPECT_TRUE(pool.erase(a)); // b is swapped into a's slot
    EXPECT_FALSE(pool.contains(a));
    EXPECT_EQ(*static_cast<const std::uint32_t*>(pool.get(b)), 9u);
    EXPECT_EQ(pool.entities()[0], b);

    EXPECT_FALSE(pool.erase(a));
    EXPECT_FALSE(pool.erase(Entity::null()));
    EXPECT_TRUE(pool.get(a) == nullptr);
}

TEST(PoolTest, FallbackAndZeroInit)
{
    Registry reg;
    ComponentStorage pool(sizeof(std::uint32_t), alignof(std::uint32_t));
    const std::uint32_t def = 55;
    const Entity a          = reg.create();
    const Entity b          = reg.create();
    pool.emplace(a, nullptr, &def);
    pool.emplace(b, nullptr, nullptr);
    EXPECT_EQ(*static_cast<const std::uint32_t*>(pool.get(a)), 55u);
    EXPECT_EQ(*static_cast<const std::uint32_t*>(pool.get(b)), 0u);
}

TEST(PoolTest, OverAlignedPayload)
{
    Registry reg;
    ComponentStorage pool(32, 32);
    for (int i = 0; i < 50; ++i) { // forces several reallocations
        const Entity e = reg.create();
        void* p        = pool.emplace(e, nullptr, nullptr);
        ASSERT_TRUE(p != nullptr);
    }
    for (std::size_t i = 0; i < pool.size(); ++i) {
        const auto addr = reinterpret_cast<std::uintptr_t>(pool.get(pool.entities()[i]));
        EXPECT_EQ(addr % 32u, 0u);
    }
}

TEST(PoolTest, Clear)
{
    Registry reg;
    ComponentStorage pool(4, 4);
    const Entity e = reg.create();
    pool.emplace(e, nullptr, nullptr);
    pool.clear();
    EXPECT_EQ(pool.size(), 0u);
    EXPECT_FALSE(pool.contains(e));
}

// Guards the central assumption: payload order mirrors the sparse set's packed order.
TEST(PoolTest, RandomOpsKeepIndexAndPayloadInSync)
{
    Registry reg;
    ComponentStorage pool(sizeof(std::uint64_t), alignof(std::uint64_t));
    std::mt19937 rng(1234);
    std::vector<Entity> live;
    std::map<std::uint32_t, std::uint64_t> expected; // entity value -> payload

    auto verify = [&] {
        ASSERT_EQ(pool.size(), expected.size());
        const auto* base = static_cast<const unsigned char*>(pool.data());
        for (std::size_t i = 0; i < pool.size(); ++i) {
            std::uint64_t v = 0;
            std::memcpy(&v, base + i * sizeof(v), sizeof(v));
            const auto it = expected.find(pool.entities()[i].value());
            ASSERT_TRUE(it != expected.end());
            ASSERT_EQ(it->second, v);
        }
    };

    for (int step = 0; step < 5000; ++step) {
        if (live.empty() || rng() % 3 != 0) {
            const Entity e        = reg.create();
            const std::uint64_t v = (static_cast<std::uint64_t>(rng()) << 32) | rng();
            pool.emplace(e, &v, nullptr);
            expected[e.value()] = v;
            live.push_back(e);
        } else {
            const std::size_t k = rng() % live.size();
            const Entity e      = live[k];
            live[k]             = live.back();
            live.pop_back();
            ASSERT_TRUE(pool.erase(e));
            expected.erase(e.value());
            ASSERT_FALSE(pool.contains(e));
        }
        if (step % 97 == 0) {
            verify();
        }
    }
    verify();
}
