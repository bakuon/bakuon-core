#include "b_bakuon.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace bakuon::core;

namespace {

struct Position
{
    float x;
    float y;
    float z;
};
struct Health
{
    std::int32_t hp;
};
struct alignas(32) Wide
{
    float v[8];
};
struct DeadTag
{
};

std::int32_t dummy_serialize(const void*, void*, std::size_t, std::size_t*, void*)
{
    return 0;
}
std::int32_t dummy_deserialize(void*, const void*, std::size_t, void*)
{
    return 0;
}
void dummy_draw(void*, void*, void*)
{
}

class ComponentsTest : public ::testing::Test
{
protected:
    IComponents& comps() { return m_domain.components(); }
    Domain m_domain{Domain::from_name("game.test"), "test", 1};
};

} // namespace

TEST_F(ComponentsTest, RegisterFindDescribe)
{
    TypedComponent<Position> pos;
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", pos), Status::ok);
    EXPECT_TRUE(pos.valid());
    EXPECT_TRUE(pos.id() == make_component_id("Position"));
    EXPECT_TRUE(comps().find("Position") == pos.id());
    EXPECT_FALSE(comps().find("Nope").valid());
    EXPECT_EQ(comps().type_count(), 1u);
    EXPECT_TRUE(comps().type_at(0) == pos.id());
    EXPECT_FALSE(comps().type_at(1).valid());

    ComponentInfo info{};
    info.struct_size = sizeof(info);
    ASSERT_EQ(comps().describe(pos.id(), &info), Status::ok);
    EXPECT_STREQ(info.name, "Position");
    EXPECT_EQ(info.size, sizeof(Position));
    EXPECT_EQ(info.align, alignof(Position));
}

TEST_F(ComponentsTest, RegistrationIsIdempotentForSameLayout)
{
    TypedComponent<Position> a, b;
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", a), Status::ok);
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", b), Status::ok);
    EXPECT_TRUE(a.id() == b.id());
    EXPECT_EQ(comps().type_count(), 1u);

    TypedComponent<Health> wrong;
    EXPECT_EQ(TypedComponent<Health>::inregister(comps(), "Position", wrong), Status::type_mismatch);
    EXPECT_FALSE(wrong.valid());
}

TEST_F(ComponentsTest, InvalidDescriptorsAreRejected)
{
    ComponentId id{};
    ComponentDesc d = make_component_desc<Position>("");
    EXPECT_EQ(comps().inregister(d, &id), Status::invalid_argument);

    d = make_component_desc<Position>(nullptr);
    EXPECT_EQ(comps().inregister(d, &id), Status::invalid_argument);

    d             = make_component_desc<Position>("P");
    d.struct_size = 1;
    EXPECT_EQ(comps().inregister(d, &id), Status::invalid_argument);

    d       = make_component_desc<Position>("P");
    d.align = 0;
    EXPECT_EQ(comps().inregister(d, &id), Status::invalid_argument);

    d       = make_component_desc<Position>("P");
    d.align = 3;
    EXPECT_EQ(comps().inregister(d, &id), Status::invalid_argument);

    d      = make_component_desc<Position>("P");
    d.size = 13; // not a multiple of align
    EXPECT_EQ(comps().inregister(d, &id), Status::invalid_argument);

    d      = make_component_desc<Position>("P");
    d.size = component_max_size + 1u;
    EXPECT_EQ(comps().inregister(d, &id), Status::invalid_argument);

    EXPECT_EQ(comps().type_count(), 0u);
    EXPECT_EQ(comps().inregister(make_component_desc<Position>("Q"), nullptr), Status::ok);
}

TEST_F(ComponentsTest, HashCollisionWithBackendStorageIsRejected)
{
    // Simulate an engine-owned typed storage occupying the name hash. The component
    // table must reject the registration instead of aliasing that storage.
    struct EngineComponent
    {
        int value;
    };
    m_domain.registry().storage<EngineComponent>(make_component_id("Position").value);

    TypedComponent<Position> pos;
    EXPECT_EQ(TypedComponent<Position>::inregister(comps(), "Position", pos), Status::id_collision);
    EXPECT_FALSE(pos.valid());
    EXPECT_EQ(comps().type_count(), 0u);
}

TEST_F(ComponentsTest, AddGetHasRemove)
{
    TypedComponent<Position> pos;
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", pos), Status::ok);
    const Entity e = m_domain.create();

    Position* p = nullptr;
    ASSERT_EQ(pos.add(e, Position{1.f, 2.f, 3.f}, &p), Status::ok);
    ASSERT_TRUE(p != nullptr);
    EXPECT_FLOAT_EQ(p->y, 2.f);
    EXPECT_TRUE(pos.has(e));
    EXPECT_TRUE(pos.get(e) == p);
    EXPECT_TRUE(pos.get_const(e) == p);

    Position* again = nullptr;
    EXPECT_EQ(pos.add(e, Position{9.f, 9.f, 9.f}, &again), Status::already_present);
    EXPECT_TRUE(again == p);
    EXPECT_FLOAT_EQ(p->x, 1.f); // untouched

    EXPECT_EQ(pos.remove(e), Status::ok);
    EXPECT_FALSE(pos.has(e));
    EXPECT_TRUE(pos.get(e) == nullptr);
    EXPECT_EQ(pos.remove(e), Status::not_present);
}

TEST_F(ComponentsTest, ReplaceUpdatesInPlace)
{
    TypedComponent<Position> pos;
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", pos), Status::ok);
    const Entity e = m_domain.create();

    Position* p = nullptr;
    ASSERT_EQ(pos.add(e, Position{1.f, 2.f, 3.f}, &p), Status::ok);
    ASSERT_EQ(pos.replace(e, Position{4.f, 5.f, 6.f}), Status::ok);
    EXPECT_TRUE(pos.get(e) == p); // no reallocation, no pointer invalidation
    EXPECT_FLOAT_EQ(p->x, 4.f);
    EXPECT_FLOAT_EQ(p->y, 5.f);
    EXPECT_FLOAT_EQ(p->z, 6.f);

    const Entity missing = m_domain.create();
    EXPECT_EQ(pos.replace(missing, Position{}), Status::not_present);
    EXPECT_EQ(comps().replace(e, pos.id(), nullptr), Status::invalid_argument);
}

TEST_F(ComponentsTest, DefaultValueAndZeroInit)
{
    const Health def{100};
    TypedComponent<Health> hp;
    TypedComponent<Position> pos;
    ASSERT_EQ(TypedComponent<Health>::inregister(comps(), "Health", hp, &def), Status::ok);
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", pos), Status::ok);

    const Entity e = m_domain.create();
    Health* h      = nullptr;
    Position* p    = nullptr;
    ASSERT_EQ(hp.add_default(e, &h), Status::ok);
    ASSERT_EQ(pos.add_default(e, &p), Status::ok);
    EXPECT_EQ(h->hp, 100);
    EXPECT_FLOAT_EQ(p->x, 0.f);
    EXPECT_FLOAT_EQ(p->z, 0.f);
}

TEST_F(ComponentsTest, TagComponentsHaveNoPayload)
{
    TypedComponent<DeadTag> dead;
    ASSERT_EQ(TypedComponent<DeadTag>::inregister(comps(), "DeadTag", dead), Status::ok);

    ComponentInfo info{};
    info.struct_size = sizeof(info);
    ASSERT_EQ(comps().describe(dead.id(), &info), Status::ok);
    EXPECT_EQ(info.size, 0u);
    EXPECT_EQ(info.align, 1u);

    const Entity e = m_domain.create();
    ASSERT_EQ(dead.add_default(e), Status::ok);
    EXPECT_TRUE(dead.has(e));
    EXPECT_TRUE(comps().get(e, dead.id()) == nullptr);
    EXPECT_TRUE(comps().get_const(e, dead.id()) == nullptr);
    EXPECT_EQ(comps().replace(e, dead.id(), nullptr), Status::ok);

    std::size_t visited = 0;
    ASSERT_EQ(dead.for_each([&](Entity entity) {
        ++visited;
        EXPECT_TRUE(entity == e);
    }),
              Status::ok);
    EXPECT_EQ(visited, 1u);
    EXPECT_EQ(dead.count(), 1u);

    EXPECT_EQ(dead.remove(e), Status::ok);
    EXPECT_FALSE(dead.has(e));
}

TEST_F(ComponentsTest, ErrorPaths)
{
    TypedComponent<Position> pos;
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", pos), Status::ok);

    EXPECT_EQ(pos.add_default(null), Status::not_alive);

    const Entity dead = m_domain.create();
    ASSERT_TRUE(m_domain.destroy(dead));
    EXPECT_EQ(pos.add_default(dead), Status::not_alive);

    const Entity e = m_domain.create();
    EXPECT_EQ(comps().emplace(e, ComponentId{12345}, nullptr, nullptr), Status::not_registered);
    EXPECT_EQ(comps().replace(e, ComponentId{12345}, nullptr), Status::not_registered);
    EXPECT_EQ(comps().remove(e, ComponentId{12345}), Status::not_registered);
    EXPECT_FALSE(comps().has(e, ComponentId{12345}));
    EXPECT_TRUE(comps().get(e, ComponentId{12345}) == nullptr);
    EXPECT_TRUE(comps().get_const(e, ComponentId{12345}) == nullptr);
    EXPECT_EQ(comps().count(ComponentId{12345}), 0u);
    EXPECT_EQ(comps().for_each(ComponentId{12345}, nullptr, nullptr), Status::invalid_argument);
    EXPECT_EQ(comps().for_each(ComponentId{12345}, nullptr, [](void*, Entity, void* const*) {}),
              Status::not_registered);

    TypedComponent<Position> unbound;
    EXPECT_EQ(unbound.add_default(e), Status::invalid_argument);
    EXPECT_FALSE(unbound.has(e));
    EXPECT_TRUE(unbound.get(e) == nullptr);
    EXPECT_EQ(unbound.for_each([](Entity, Position&) {}), Status::invalid_argument);
}

TEST_F(ComponentsTest, GrowthKeepsValues)
{
    TypedComponent<Position> pos;
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", pos), Status::ok);

    std::vector<Entity> es;
    for (int i = 0; i < 1000; ++i) {
        const Entity e = m_domain.create();
        es.push_back(e);
        ASSERT_EQ(pos.add(e, Position{static_cast<float>(i), 0.f, 0.f}), Status::ok);
    }
    EXPECT_EQ(pos.count(), 1000u);
    for (int i = 0; i < 1000; ++i) {
        ASSERT_TRUE(pos.get(es[i]) != nullptr);
        EXPECT_FLOAT_EQ(pos.get(es[i])->x, static_cast<float>(i));
    }
}

TEST_F(ComponentsTest, OverAlignedComponent)
{
    TypedComponent<Wide> wide;
    ASSERT_EQ(TypedComponent<Wide>::inregister(comps(), "Wide", wide), Status::ok);

    std::vector<Entity> es;
    for (int i = 0; i < 100; ++i) {
        const Entity e = m_domain.create();
        es.push_back(e);
        Wide w{};
        w.v[0] = static_cast<float>(i);
        ASSERT_EQ(wide.add(e, w), Status::ok);
    }
    for (int i = 0; i < 100; ++i) {
        const Wide* w = wide.get_const(es[i]);
        ASSERT_TRUE(w != nullptr);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(w) % 32u, 0u);
        EXPECT_FLOAT_EQ(w->v[0], static_cast<float>(i));
    }
}

TEST_F(ComponentsTest, ForEachVisitsOnlyLiveInstances)
{
    TypedComponent<Position> pos;
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", pos), Status::ok);

    std::vector<Entity> es;
    for (int i = 0; i < 5; ++i) {
        const Entity e = m_domain.create();
        es.push_back(e);
        ASSERT_EQ(pos.add(e, Position{static_cast<float>(i), 0.f, 0.f}), Status::ok);
    }
    ASSERT_EQ(pos.remove(es[1]), Status::ok);

    std::size_t seen = 0;
    const Status s   = pos.for_each([&](Entity e, Position& p) {
        ++seen;
        EXPECT_TRUE(pos.get(e) == &p);
        EXPECT_FLOAT_EQ(p.x, static_cast<float>(e.index()));
    });
    EXPECT_EQ(s, Status::ok);
    EXPECT_EQ(seen, 4u);
    EXPECT_EQ(pos.count(), 4u);
}

TEST_F(ComponentsTest, JointEachVisitsIntersection)
{
    TypedComponent<Position> pos;
    TypedComponent<Health> hp;
    TypedComponent<DeadTag> dead;
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", pos), Status::ok);
    ASSERT_EQ(TypedComponent<Health>::inregister(comps(), "Health", hp), Status::ok);
    ASSERT_EQ(TypedComponent<DeadTag>::inregister(comps(), "DeadTag", dead), Status::ok);

    const Entity both1    = m_domain.create();
    const Entity both2    = m_domain.create();
    const Entity only_pos = m_domain.create();
    const Entity only_hp  = m_domain.create();
    ASSERT_EQ(pos.add_default(both1), Status::ok);
    ASSERT_EQ(hp.add_default(both1), Status::ok);
    ASSERT_EQ(dead.add_default(both1), Status::ok);
    ASSERT_EQ(pos.add_default(both2), Status::ok);
    ASSERT_EQ(hp.add_default(both2), Status::ok);
    ASSERT_EQ(pos.add_default(only_pos), Status::ok);
    ASSERT_EQ(hp.add_default(only_hp), Status::ok);

    std::size_t pairs = 0;
    const Status s    = view(comps(), pos, hp).each([&](Entity e, Position&, Health&) {
        ++pairs;
        EXPECT_TRUE(e == both1 || e == both2);
    });
    EXPECT_EQ(s, Status::ok);
    EXPECT_EQ(pairs, 2u);

    const ComponentId ids[] = {pos.id(), dead.id()};
    std::size_t tagged      = 0;
    EXPECT_EQ(comps().each(ids,
                           2,
                           &tagged,
                           [](void* user, Entity, void* const* data) {
                               ++*static_cast<std::size_t*>(user);
                               EXPECT_TRUE(data[0] != nullptr);
                               EXPECT_TRUE(data[1] == nullptr);
                           }),
              Status::ok);
    EXPECT_EQ(tagged, 1u);

    ComponentId too_many[component_max_each_components + 1]{};
    EXPECT_EQ(comps().each(too_many, component_max_each_components + 1, nullptr, nullptr),
              Status::invalid_argument);
}

TEST_F(ComponentsTest, IterationRejectsStructuralEdits)
{
    TypedComponent<Position> pos;
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", pos), Status::ok);
    const Entity e = m_domain.create();
    ASSERT_EQ(pos.add_default(e), Status::ok);
    const Entity spare = m_domain.create();

    std::size_t calls = 0;
    const Status s    = pos.for_each([&](Entity entity, Position&) {
        ++calls;
        EXPECT_EQ(pos.remove(entity), Status::busy);
        EXPECT_EQ(pos.add_default(spare), Status::busy);
        EXPECT_EQ(pos.clear(), Status::busy);
        EXPECT_EQ(pos.unregister(), Status::busy);
        EXPECT_FALSE(m_domain.destroy(entity));
        EXPECT_TRUE(m_domain.create().is_null());
    });
    EXPECT_EQ(s, Status::ok);
    EXPECT_EQ(calls, 1u);

    EXPECT_TRUE(pos.valid());
    EXPECT_TRUE(pos.has(e));
    EXPECT_TRUE(m_domain.alive(e));
    EXPECT_EQ(pos.count(), 1u);
    EXPECT_EQ(pos.remove(e), Status::ok);
}

TEST_F(ComponentsTest, IterationReportsCallbackFailure)
{
    TypedComponent<Position> pos;
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", pos), Status::ok);
    const Entity e = m_domain.create();
    ASSERT_EQ(pos.add_default(e), Status::ok);

    EXPECT_EQ(pos.for_each([](Entity, Position&) { throw 42; }), Status::callback_failed);
    EXPECT_TRUE(pos.has(e));              // the aborted iteration left the data intact
    EXPECT_EQ(pos.remove(e), Status::ok); // and released the structural-edit guard
}

TEST_F(ComponentsTest, DestroyingEntityRemovesItsComponents)
{
    TypedComponent<Position> pos;
    TypedComponent<Health> hp;
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", pos), Status::ok);
    ASSERT_EQ(TypedComponent<Health>::inregister(comps(), "Health", hp), Status::ok);

    const Entity e = m_domain.create();
    ASSERT_EQ(pos.add_default(e), Status::ok);
    ASSERT_EQ(hp.add_default(e), Status::ok);
    ASSERT_TRUE(m_domain.destroy(e));

    EXPECT_EQ(pos.count(), 0u);
    EXPECT_EQ(hp.count(), 0u);
    EXPECT_TRUE(pos.get(e) == nullptr);

    const Entity e2 = m_domain.create(); // recycled slot, new version
    EXPECT_EQ(e2.index(), e.index());
    EXPECT_FALSE(pos.has(e2));
    EXPECT_FALSE(hp.has(e2));
}

TEST_F(ComponentsTest, UnregisterDropsDataAndAllowsReRegistration)
{
    TypedComponent<Position> pos;
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", pos), Status::ok);
    const Entity e = m_domain.create();
    ASSERT_EQ(pos.add_default(e), Status::ok);
    const ComponentId old_id = pos.id();

    ASSERT_EQ(pos.unregister(), Status::ok);
    EXPECT_FALSE(pos.valid());
    EXPECT_FALSE(comps().find("Position").valid());
    EXPECT_EQ(comps().type_count(), 0u);
    EXPECT_EQ(comps().count(old_id), 0u);
    EXPECT_FALSE(comps().has(e, old_id));
    EXPECT_EQ(comps().unregister(old_id), Status::not_registered);

    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", pos), Status::ok);
    EXPECT_EQ(pos.count(), 0u); // fresh, empty pool
}

TEST_F(ComponentsTest, HooksAreStoredAndExposed)
{
    int user        = 0;
    ComponentDesc d = make_component_desc<Position>("Position",
                                                    nullptr,
                                                    component_flag_editor_visible
                                                        | component_flag_serializable);
    d.serializer    = &dummy_serialize;
    d.deserializer  = &dummy_deserialize;
    d.editor_draw   = &dummy_draw;
    d.hook_user     = &user;

    ComponentId id{};
    ASSERT_EQ(comps().inregister(d, &id), Status::ok);

    ComponentInfo info{};
    info.struct_size = sizeof(info);
    ASSERT_EQ(comps().describe(id, &info), Status::ok);
    EXPECT_TRUE(info.serializer == &dummy_serialize);
    EXPECT_TRUE(info.deserializer == &dummy_deserialize);
    EXPECT_TRUE(info.editor_draw == &dummy_draw);
    EXPECT_TRUE(info.hook_user == &user);
    EXPECT_EQ(info.flags, component_flag_editor_visible | component_flag_serializable);

    ComponentInfo bad{}; // struct_size not set
    EXPECT_EQ(comps().describe(id, &bad), Status::invalid_argument);
}

TEST_F(ComponentsTest, BindSharesAndVerifiesLayout)
{
    TypedComponent<Position> a;
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", a), Status::ok);

    TypedComponent<Position> b;
    ASSERT_EQ(TypedComponent<Position>::bind(comps(), "Position", b), Status::ok);
    EXPECT_TRUE(a.id() == b.id());

    const Entity e = m_domain.create();
    ASSERT_EQ(a.add(e, Position{4.f, 5.f, 6.f}), Status::ok);
    EXPECT_TRUE(a.get(e) == b.get(e));

    TypedComponent<Health> wrong;
    EXPECT_EQ(TypedComponent<Health>::bind(comps(), "Position", wrong), Status::type_mismatch);
    EXPECT_EQ(TypedComponent<Position>::bind(comps(), "Missing", wrong.valid() ? b : b),
              Status::not_registered);

    TypedComponent<DeadTag> tag;
    ASSERT_EQ(TypedComponent<DeadTag>::inregister(comps(), "DeadTag", tag), Status::ok);
    TypedComponent<DeadTag> tag_bound;
    EXPECT_EQ(TypedComponent<DeadTag>::bind(comps(), "DeadTag", tag_bound), Status::ok);
}

TEST_F(ComponentsTest, DomainClearKeepsRegistrationsButDropsData)
{
    TypedComponent<Position> pos;
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", pos), Status::ok);
    const Entity e = m_domain.create();
    ASSERT_EQ(pos.add_default(e), Status::ok);

    m_domain.clear();
    EXPECT_EQ(pos.count(), 0u);
    EXPECT_EQ(comps().type_count(), 1u);

    const Entity e2 = m_domain.create();
    EXPECT_EQ(pos.add_default(e2), Status::ok);
}

TEST_F(ComponentsTest, ClearComponentOnlyAffectsOneType)
{
    TypedComponent<Position> pos;
    TypedComponent<Health> hp;
    ASSERT_EQ(TypedComponent<Position>::inregister(comps(), "Position", pos), Status::ok);
    ASSERT_EQ(TypedComponent<Health>::inregister(comps(), "Health", hp), Status::ok);
    const Entity e = m_domain.create();
    ASSERT_EQ(pos.add_default(e), Status::ok);
    ASSERT_EQ(hp.add_default(e), Status::ok);

    ASSERT_EQ(pos.clear(), Status::ok);
    EXPECT_EQ(pos.count(), 0u);
    EXPECT_EQ(hp.count(), 1u);
}

TEST(ComponentsIsolationTest, TypeTablesAreIndependentPerDomain)
{
    Domain a{Domain::from_name("game.a"), "a", 1};
    Domain b{Domain::from_name("game.b"), "b", 2};
    TypedComponent<Position> pos;
    ASSERT_EQ(TypedComponent<Position>::inregister(a.components(), "Position", pos), Status::ok);
    EXPECT_FALSE(b.components().find("Position").valid());
    EXPECT_EQ(b.components().type_count(), 0u);
}
