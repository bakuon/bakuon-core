#include <bakuon/core/component.h>
#include <bakuon/core/host.h>

#include <gtest/gtest.h>

#include <cstdint>

#ifndef BAKUON_TEST_PLUGIN_PATH
#    error "BAKUON_TEST_PLUGIN_PATH must be defined by the build"
#endif

using namespace bakuon::core;

namespace {
struct Counter
{
    std::int32_t value;
};
} // namespace

TEST(HostApiTest, VersionAndLifecycle)
{
    EXPECT_EQ(bakuon_host_abi_version(), IHost::abi_current);
    IHost* host = bakuon_host_create();
    ASSERT_TRUE(host != nullptr);
    EXPECT_EQ(host->abi_version(), IHost::abi_current);
    EXPECT_EQ(host->plugin_count(), 0u);
    EXPECT_EQ(host->world().abi_version(), IWorld::abi_current);
    bakuon_host_destroy(host);
}

TEST(HostApiTest, LoadRunUnloadThroughExportedApi)
{
    IHost* host = bakuon_host_create();
    ASSERT_TRUE(host != nullptr);

    ASSERT_EQ(host->load_plugin(BAKUON_TEST_PLUGIN_PATH), Status::ok);
    ASSERT_EQ(host->plugin_count(), 1u);
    EXPECT_STREQ(host->plugin_name_at(0), "test_plugin");

    for (int i = 0; i < 5; ++i) {
        ASSERT_EQ(host->world().run_frame(FrameContext{0.016, static_cast<std::uint64_t>(i)}),
                  Status::ok);
    }

    IDomain* d = host->world().find("main");
    ASSERT_TRUE(d != nullptr);
    TypedComponent<Counter> c;
    ASSERT_EQ(TypedComponent<Counter>::bind(d->components(), "TestCounter", c), Status::ok);
    ComponentArray<Counter> arr;
    ASSERT_EQ(c.span(arr), Status::ok);
    ASSERT_EQ(arr.count, 1u);
    EXPECT_EQ(arr.data[0].value, 5);

    ASSERT_EQ(host->unload_plugin("test_plugin"), Status::ok);
    EXPECT_EQ(d->components().find("TestCounter"), 0);
    bakuon_host_destroy(host);
}

TEST(HostApiTest, DestroyWithPluginStillLoadedIsClean)
{
    IHost* host = bakuon_host_create();
    ASSERT_TRUE(host != nullptr);
    ASSERT_EQ(host->load_plugin(BAKUON_TEST_PLUGIN_PATH), Status::ok);
    bakuon_host_destroy(host); // must unload the plugin before closing its library
    SUCCEED();
}

TEST(HostApiTest, ErrorsAreReported)
{
    IHost* host = bakuon_host_create();
    ASSERT_TRUE(host != nullptr);
    EXPECT_EQ(host->load_plugin("/nope/plugin.so"), Status::load_failed);
    EXPECT_STRNE(host->last_error(), "");
    EXPECT_EQ(host->unload_plugin("nope"), Status::not_found);
    bakuon_host_destroy(host);
}
