#include "b_plugin_host.h"
#include "bakuon/core/plugin.h"
#include <bakuon/core/component.h>

#include <gtest/gtest.h>

#include <cstdint>

using namespace bakuon::core;

namespace {

struct Thing
{
    std::int32_t v;
};

std::int32_t dummy_serialize(const void*, void*, std::size_t, std::size_t*, void*)
{
    return 0;
}

struct Observed
{
    Status foreign_unregister{Status::ok};
    bool same_view{false};
    bool sees_peer_entity{false};
    IDomain* view[2]{nullptr, nullptr};
};
Observed g_obs;
Entity g_entity = Entity::null(); // created by plugin 0

// Registers a hooked and a data-only type and then deliberately forgets to clean up.
template<int N>
class OwnerPlugin final : public IPlugin
{
public:
    std::uint32_t abi_version() const noexcept override { return abi_current; }
    const char* name() const noexcept override { return N == 0 ? "owner0" : "owner1"; }

    Status on_load(IPluginContext& ctx) noexcept override
    {
        IDomain* d = ctx.require_domain("main");
        if (d == nullptr) {
            return Status::load_failed;
        }
        g_obs.view[N]   = d;
        g_obs.same_view = (ctx.find_domain("main") == d);

        ComponentDesc hooked = make_component_desc<Thing>("Hooked");
        hooked.serializer    = &dummy_serialize;
        ComponentId hid{};
        Status s = d->components().inregister(hooked, &hid);
        if (s != Status::ok) {
            return s;
        }
        s = d->components().inregister(make_component_desc<Thing>("Plain"), nullptr);
        if (s != Status::ok) {
            return s;
        }

        if constexpr (N == 0) {
            TypedComponent<Thing> plain;
            s = TypedComponent<Thing>::bind(d->components(), "Plain", plain);
            if (s != Status::ok) {
                return s;
            }
            g_entity = d->create();
            return plain.add(g_entity, Thing{42});
        } else {
            g_obs.foreign_unregister = d->components().unregister(hid);
            g_obs.sees_peer_entity   = d->alive(g_entity);
            return Status::ok;
        }
    }

    void on_unload(IPluginContext&) noexcept override {} // deliberately forgets everything
};

class FailingPlugin final : public IPlugin
{
public:
    std::uint32_t abi_version() const noexcept override { return abi_current; }
    const char* name() const noexcept override { return "failing"; }
    Status on_load(IPluginContext& ctx) noexcept override
    {
        IDomain* d = ctx.require_domain("main");
        if (d == nullptr) {
            return Status::load_failed;
        }
        ComponentDesc hooked = make_component_desc<Thing>("HookedFail");
        hooked.serializer    = &dummy_serialize;
        const Status s       = d->components().inregister(hooked, nullptr);
        if (s != Status::ok) {
            return s;
        }
        return Status::load_failed; // fail after registering
    }
    void on_unload(IPluginContext&) noexcept override {}
};

template<class P>
std::uint32_t abi_ok()
{
    return IPlugin::abi_current;
}
template<class P>
IPlugin* create_plugin()
{
    return new P();
}
template<class P>
void destroy_plugin(IPlugin* p)
{
    delete static_cast<P*>(p);
}
template<class P>
PluginModule make_module()
{
    PluginModule m;
    m.abi_version = &abi_ok<P>;
    m.create      = &create_plugin<P>;
    m.destroy     = &destroy_plugin<P>;
    return m;
}

class OwnershipTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        g_obs    = Observed{};
        g_entity = Entity::null();
    }
    World m_world;
    PluginHost m_host{m_world};
};

} // namespace

TEST_F(OwnershipTest, HookedTypeIsRemovedOnUnloadEvenIfThePluginForgot)
{
    ASSERT_EQ(m_host.load(make_module<OwnerPlugin<0>>()), Status::ok);
    IDomain* main = m_world.find_by_name("main");
    ASSERT_TRUE(main != nullptr);
    EXPECT_TRUE(main->components().find("Hooked").valid());
    EXPECT_TRUE(main->components().find("Plain").valid());

    ASSERT_EQ(m_host.unload("owner0"), Status::ok);
    EXPECT_FALSE(
        main->components().find("Hooked").valid());        // code pointers into the module are gone
    EXPECT_TRUE(main->components().find("Plain").valid()); // data-only type persists
}

TEST_F(OwnershipTest, DataOnlyTypesKeepTheirDataAndReloadIsIdempotent)
{
    ASSERT_EQ(m_host.load(make_module<OwnerPlugin<0>>()), Status::ok);
    const Entity first = g_entity;
    ASSERT_EQ(m_host.unload("owner0"), Status::ok);

    IDomain* main = m_world.find_by_name("main");
    ASSERT_TRUE(main != nullptr);
    TypedComponent<Thing> plain;
    ASSERT_EQ(TypedComponent<Thing>::bind(main->components(), "Plain", plain), Status::ok);
    const Thing* t = plain.get_const(first);
    ASSERT_TRUE(t != nullptr);
    EXPECT_EQ(t->v, 42);

    ASSERT_EQ(m_host.load(make_module<OwnerPlugin<0>>()), Status::ok); // same layouts: idempotent
    EXPECT_TRUE(main->components().find("Hooked").valid());
}

TEST_F(OwnershipTest, SecondRegistrarDoesNotOwnTheType)
{
    ASSERT_EQ(m_host.load(make_module<OwnerPlugin<0>>()), Status::ok);
    ASSERT_EQ(m_host.load(make_module<OwnerPlugin<1>>()), Status::ok);
    IDomain* main = m_world.find_by_name("main");
    ASSERT_TRUE(main != nullptr);

    ASSERT_EQ(m_host.unload("owner1"), Status::ok);
    EXPECT_TRUE(main->components().find("Hooked").valid()); // owner0 still owns it
    ASSERT_EQ(m_host.unload("owner0"), Status::ok);
    EXPECT_FALSE(main->components().find("Hooked").valid());
}

TEST_F(OwnershipTest, PluginCannotUnregisterForeignTypes)
{
    ASSERT_EQ(m_host.load(make_module<OwnerPlugin<0>>()), Status::ok);
    ASSERT_EQ(m_host.load(make_module<OwnerPlugin<1>>()), Status::ok);
    EXPECT_EQ(g_obs.foreign_unregister, Status::not_found);
    EXPECT_TRUE(m_world.find_by_name("main")->components().find("Hooked").valid());
}

TEST_F(OwnershipTest, PluginsGetDistinctViewsOfTheSameDomain)
{
    ASSERT_EQ(m_host.load(make_module<OwnerPlugin<0>>()), Status::ok);
    ASSERT_EQ(m_host.load(make_module<OwnerPlugin<1>>()), Status::ok);
    EXPECT_TRUE(g_obs.same_view);                           // find/require agree within one plugin
    EXPECT_NE(g_obs.view[0], g_obs.view[1]);                // one view per plugin
    EXPECT_NE(g_obs.view[0], m_world.find_by_name("main")); // never the raw domain
    EXPECT_STREQ(g_obs.view[0]->name(), "main");
    EXPECT_TRUE(g_obs.sees_peer_entity); // but they share the same data
}

TEST_F(OwnershipTest, UnloadAfterTheDomainWasDestroyedIsSafe)
{
    ASSERT_EQ(m_host.load(make_module<OwnerPlugin<0>>()), Status::ok);
    ASSERT_EQ(m_world.destroy_by_name("main"), Status::ok); // no systems: allowed
    EXPECT_EQ(m_host.unload("owner0"),
              Status::ok); // must not touch the dead domain (run under ASan)
}

TEST_F(OwnershipTest, FailedLoadRollsBackHookedTypes)
{
    EXPECT_EQ(m_host.load(make_module<FailingPlugin>()), Status::load_failed);
    IDomain* main = m_world.find_by_name("main");
    ASSERT_TRUE(main != nullptr);
    EXPECT_FALSE(main->components().find("HookedFail").valid());
}
