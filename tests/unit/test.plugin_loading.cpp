#include "b_host.h"
#include "b_module_loader.h"
#include <bakuon/core/component.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#ifndef BAKUON_TEST_PLUGIN_PATH
#    error "BAKUON_TEST_PLUGIN_PATH must be defined by the build"
#endif

using namespace bakuon::core;

namespace {

struct Counter
{
    std::int32_t value; // same layout as the test plugin's component
};

// Reads the plugin's counter through the public API; -1 if unavailable.
std::int32_t read_counter(IWorld& world)
{
    IDomain* d = world.find("main");
    if (d == nullptr) {
        return -1;
    }
    TypedComponent<Counter> c;
    if (TypedComponent<Counter>::bind(d->components(), "TestCounter", c) != Status::ok) {
        return -1;
    }
    std::int32_t value = -1;
    std::size_t seen   = 0;
    if (c.for_each([&](Entity, Counter& counter) {
            value = counter.value;
            ++seen;
        }) != Status::ok
        || seen != 1) {
        return -1;
    }
    return value;
}

class PluginLoadingTest : public ::testing::Test
{
protected:
    Host m_host;
};

} // namespace

TEST(ModuleLoaderTest, RejectsEmptyAndMissingPaths)
{
    ModuleLoader l;
    EXPECT_EQ(l.open(nullptr), Status::invalid_argument);
    EXPECT_EQ(l.open(""), Status::invalid_argument);
    EXPECT_EQ(l.open("/definitely/not/a/real/plugin.so"), Status::load_failed);
    EXPECT_FALSE(l.is_open());
    EXPECT_FALSE(l.error().empty());
}

TEST(ModuleLoaderTest, ResolvesExportsOfARealPlugin)
{
    ModuleLoader l;
    ASSERT_EQ(l.open(BAKUON_TEST_PLUGIN_PATH), Status::ok);
    EXPECT_TRUE(l.is_open());
    EXPECT_TRUE(l.symbol("bakuon_plugin_abi_version") != nullptr);
    EXPECT_TRUE(l.symbol("bakuon_create_plugin") != nullptr);
    EXPECT_TRUE(l.symbol("bakuon_destroy_plugin") != nullptr);
    EXPECT_TRUE(l.symbol("no_such_symbol") == nullptr);
    EXPECT_TRUE(l.symbol(nullptr) == nullptr);

    l.close();
    EXPECT_FALSE(l.is_open());
    EXPECT_TRUE(l.symbol("bakuon_create_plugin") == nullptr);
}

TEST_F(PluginLoadingTest, LoadRunUnload)
{
    ASSERT_EQ(m_host.load_plugin(BAKUON_TEST_PLUGIN_PATH), Status::ok);
    EXPECT_EQ(m_host.plugin_count(), 1u);
    EXPECT_STREQ(m_host.plugin_name_at(0), "test_plugin");
    EXPECT_TRUE(m_host.plugin_name_at(1) == nullptr);
    EXPECT_TRUE(m_host.world().scheduler().has_system("test_plugin.counter"));
    EXPECT_EQ(read_counter(m_host.world()), 0);

    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(m_host.world().run_frame(FrameContext{static_cast<std::uint64_t>(i), 0.016}),
                  Status::ok);
    }
    EXPECT_EQ(read_counter(m_host.world()), 3);

    ASSERT_EQ(m_host.unload_plugin("test_plugin"), Status::ok);
    EXPECT_EQ(m_host.plugin_count(), 0u);
    EXPECT_FALSE(m_host.world().scheduler().has_system("test_plugin.counter"));
    IDomain* d = m_host.world().find("main");
    ASSERT_TRUE(d != nullptr);
    EXPECT_FALSE(d->components().find("TestCounter").valid()); // plugin cleaned up its hooked type
    EXPECT_EQ(d->alive_count(), 0u);
}

TEST_F(PluginLoadingTest, ReloadCycleStartsFresh)
{
    for (int round = 0; round < 3; ++round) {
        ASSERT_EQ(m_host.load_plugin(BAKUON_TEST_PLUGIN_PATH), Status::ok);
        EXPECT_EQ(read_counter(m_host.world()), 0);
        ASSERT_EQ(m_host.world().run_frame(FrameContext{}), Status::ok);
        EXPECT_EQ(read_counter(m_host.world()), 1);
        ASSERT_EQ(m_host.unload_plugin("test_plugin"), Status::ok);
    }
}

TEST_F(PluginLoadingTest, DuplicateLoadIsRejectedAndFirstKeepsWorking)
{
    ASSERT_EQ(m_host.load_plugin(BAKUON_TEST_PLUGIN_PATH), Status::ok);
    EXPECT_EQ(m_host.load_plugin(BAKUON_TEST_PLUGIN_PATH), Status::already_exists);
    EXPECT_STRNE(m_host.last_error(), "");
    EXPECT_EQ(m_host.plugin_count(), 1u);
    ASSERT_EQ(m_host.world().run_frame(FrameContext{}), Status::ok);
    EXPECT_EQ(read_counter(m_host.world()), 1);
}

TEST_F(PluginLoadingTest, UnknownAndInvalidUnload)
{
    EXPECT_EQ(m_host.unload_plugin("nope"), Status::not_found);
    EXPECT_EQ(m_host.unload_plugin(nullptr), Status::invalid_argument);
}

TEST_F(PluginLoadingTest, MissingLibraryIsReported)
{
    EXPECT_EQ(m_host.load_plugin("/nope/plugin.so"), Status::load_failed);
    EXPECT_STRNE(m_host.last_error(), "");
    EXPECT_EQ(m_host.plugin_count(), 0u);
    EXPECT_EQ(m_host.load_plugin(""), Status::invalid_argument);
}

#ifdef BAKUON_CORE_LIB_PATH
TEST_F(PluginLoadingTest, LibraryWithoutPluginExportsIsRejected)
{
    EXPECT_EQ(m_host.load_plugin(BAKUON_CORE_LIB_PATH), Status::load_failed);
    EXPECT_NE(std::string(m_host.last_error()).find("missing exports"), std::string::npos);
    EXPECT_EQ(m_host.plugin_count(), 0u);
}
#endif

TEST(PluginLoadingLifetimeTest, HostDestructionUnloadsLoadedPlugins)
{
    {
        Host host;
        ASSERT_EQ(host.load_plugin(BAKUON_TEST_PLUGIN_PATH), Status::ok);
        ASSERT_EQ(host.world().run_frame(FrameContext{}), Status::ok);
    } // plugin unloaded, then library closed, then world destroyed; run under ASan to verify
    SUCCEED();
}
