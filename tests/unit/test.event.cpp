#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "b_event.h"

#include <bakuon/core/event.h>

using namespace bakuon::core;

namespace {

struct DamageEvent
{
    std::uint32_t target = 0;
    float amount         = 0.f;
};

struct EmptyTag
{
};

} // namespace

// ==================== 基础订阅 / 发布 ====================

TEST(EventTest, SubscribeAndPublish)
{
    EventBus bus;

    int hits           = 0;
    DamageEvent last{};
    auto handler = [](void* user, const void* data, std::uint32_t size) {
        auto* ctx = static_cast<std::pair<int*, DamageEvent*>*>(user);
        if (data != nullptr && size >= sizeof(DamageEvent)) {
            *ctx->second = *static_cast<const DamageEvent*>(data);
        }
        ++(*ctx->first);
    };
    std::pair<int*, DamageEvent*> ctx{&hits, &last};

    Connection conn = bus.subscribe("game.Damage", &ctx, handler);
    EXPECT_TRUE(conn.isConnected());
    EXPECT_EQ(bus.subscriber_count("game.Damage"), 1u);

    EXPECT_EQ(publish(bus, "game.Damage", DamageEvent{42, 9.5f}), 1u);
    EXPECT_EQ(hits, 1);
    EXPECT_EQ(last.target, 42u);
    EXPECT_FLOAT_EQ(last.amount, 9.5f);
}

TEST(EventTest, TypedSubscribeHelper)
{
    EventBus bus;
    std::vector<float> amounts;

    auto on_damage = [&](const DamageEvent& e) { amounts.push_back(e.amount); };
    Connection conn = subscribe<DamageEvent>(bus, "game.Damage", on_damage);

    EXPECT_EQ(publish(bus, "game.Damage", DamageEvent{1, 3.f}), 1u);
    EXPECT_EQ(publish(bus, "game.Damage", DamageEvent{2, 7.f}), 1u);
    ASSERT_EQ(amounts.size(), 2u);
    EXPECT_FLOAT_EQ(amounts[0], 3.f);
    EXPECT_FLOAT_EQ(amounts[1], 7.f);
}

TEST(EventTest, TagEvent)
{
    EventBus bus;
    int hits = 0;
    auto on_tick = [&]() { ++hits; };
    Connection conn = subscribe_tag(bus, "frame.Tick", on_tick);

    EXPECT_EQ(publish(bus, "frame.Tick"), 1u);
    EXPECT_EQ(publish(bus, "frame.Tick", EmptyTag{}), 1u);
    EXPECT_EQ(hits, 2);
}

// ==================== 断开与生命周期 ====================

TEST(EventTest, ConnectionDisconnectStopsDelivery)
{
    EventBus bus;
    int hits = 0;
    auto fn  = [&](const DamageEvent&) { ++hits; };
    {
        Connection conn = subscribe<DamageEvent>(bus, "e", fn);
        EXPECT_EQ(publish(bus, "e", DamageEvent{}), 1u);
        EXPECT_EQ(hits, 1);
    } // conn 析构
    EXPECT_EQ(bus.subscriber_count("e"), 0u);
    EXPECT_EQ(publish(bus, "e", DamageEvent{}), 0u);
    EXPECT_EQ(hits, 1);
}

TEST(EventTest, ExplicitDisconnect)
{
    EventBus bus;
    int hits = 0;
    auto fn  = [&]() { ++hits; };
    Connection conn = subscribe_tag(bus, "t", fn);
    conn.disconnect();
    EXPECT_FALSE(conn.isConnected());
    EXPECT_EQ(publish(bus, "t"), 0u);
    EXPECT_EQ(hits, 0);
}

TEST(EventTest, ConnectionOutlivesBus)
{
    Connection conn;
    {
        EventBus bus;
        int hits = 0;
        auto fn  = [&]() { ++hits; };
        conn     = subscribe_tag(bus, "x", fn);
        EXPECT_EQ(publish(bus, "x"), 1u);
        EXPECT_EQ(hits, 1);
    } // bus 销毁
    // 断开必须是安全空操作
    conn.disconnect();
    EXPECT_FALSE(conn.isConnected());
}

// ==================== 重入安全 ====================

TEST(EventTest, SubscribeDuringPublish)
{
    EventBus bus;
    int a_hits = 0;
    int b_hits = 0;

    auto on_b = [&]() { ++b_hits; };
    Connection late;

    auto on_a = [&]() {
        ++a_hits;
        // 本次 publish 的快照不应包含 late
        late = subscribe_tag(bus, "reentrant", on_b);
    };
    Connection early = subscribe_tag(bus, "reentrant", on_a);

    EXPECT_EQ(publish(bus, "reentrant"), 1u);
    EXPECT_EQ(a_hits, 1);
    EXPECT_EQ(b_hits, 0);

    EXPECT_EQ(publish(bus, "reentrant"), 2u);
    EXPECT_EQ(a_hits, 2);
    EXPECT_EQ(b_hits, 1);
}

TEST(EventTest, UnsubscribeDuringPublish)
{
    EventBus bus;
    int hits = 0;
    Connection self;

    auto on_event = [&]() {
        ++hits;
        self.disconnect(); // 从快照中移除不影响本次后续（本例只有一个）
    };
    self = subscribe_tag(bus, "u", on_event);

    EXPECT_EQ(publish(bus, "u"), 1u);
    EXPECT_EQ(hits, 1);
    EXPECT_EQ(bus.subscriber_count("u"), 0u);
    EXPECT_EQ(publish(bus, "u"), 0u);
}

// ==================== 多订阅者与隔离 ====================

TEST(EventTest, MultipleSubscribersIndependentChannels)
{
    EventBus bus;
    int damage_hits = 0;
    int heal_hits   = 0;

    auto on_d = [&](const DamageEvent&) { ++damage_hits; };
    auto on_h = [&]() { ++heal_hits; };

    Connection c1 = subscribe<DamageEvent>(bus, "game.Damage", on_d);
    Connection c2 = subscribe_tag(bus, "game.Heal", on_h);
    Connection c3 = subscribe<DamageEvent>(bus, "game.Damage", on_d);

    EXPECT_EQ(bus.subscriber_count("game.Damage"), 2u);
    EXPECT_EQ(publish(bus, "game.Damage", DamageEvent{1, 1.f}), 2u);
    EXPECT_EQ(damage_hits, 2);
    EXPECT_EQ(heal_hits, 0);

    EXPECT_EQ(publish(bus, "game.Heal"), 1u);
    EXPECT_EQ(heal_hits, 1);
}

// ==================== 错误路径 ====================

TEST(EventTest, RejectEmptyNameAndNullHandler)
{
    EventBus bus;
    Connection empty = bus.subscribe("", nullptr, [](void*, const void*, std::uint32_t) {});
    EXPECT_FALSE(empty.isConnected());
    EXPECT_NE(std::string_view{bus.last_error()}.find("empty"), std::string_view::npos);

    Connection null_h = bus.subscribe("ok", nullptr, nullptr);
    EXPECT_FALSE(null_h.isConnected());
    EXPECT_NE(std::string_view{bus.last_error()}.find("null"), std::string_view::npos);

    EXPECT_EQ(bus.publish("", nullptr, 0), 0u);
    EXPECT_EQ(bus.publish("ok", nullptr, 4), 0u); // size>0 but data null
}

TEST(EventTest, PublishUnknownEventReturnsZero)
{
    EventBus bus;
    EXPECT_EQ(publish(bus, "never.subscribed", DamageEvent{}), 0u);
    EXPECT_EQ(bus.subscriber_count("never.subscribed"), 0u);
}
