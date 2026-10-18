#include "b_plugin_host.h"
#include "bakuon/core/plugin.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace bakuon::core;

namespace {

std::vector<std::string>* g_events = nullptr;
void ev(const std::string& s)
{
    if (g_events != nullptr) {
        try {
            g_events->push_back(s);
        } catch (...) {
        }
    }
}

enum class Mode { normal, remove_in_unload, fail_load };

class PSystem final : public ISystem
{
public:
    explicit PSystem(std::string n)
        : m_name(std::move(n))
    {
    }
    std::uint32_t abi_version() const noexcept override { return abi_current; }
    void update(const FrameContext&, IDomain&) noexcept override { ev("update:" + m_name); }
    void release() noexcept override
    {
        ev("release:" + m_name);
        delete this;
    }

private:
    std::string m_name;
};

template<int N>
class TestPlugin final : public IPlugin
{
public:
    static inline Mode mode = Mode::normal;

    std::uint32_t abi_version() const noexcept override { return abi_current; }
    const char* name() const noexcept override { return N == 0 ? "p0" : "p1"; }

    Status on_load(IPluginContext& ctx) noexcept override
    {
        ev(std::string("load:") + name());
        if (ctx.require_domain("main") == nullptr) {
            return Status::load_failed;
        }
        SystemDesc sd{};
        sd.struct_size  = sizeof(sd);
        sd.name         = N == 0 ? "sys0" : "sys1";
        sd.domain       = "main";
        auto* s         = new PSystem(sd.name);
        const Status st = ctx.scheduler().add_system(sd, s);
        if (st != Status::ok) {
            s->release();
            return st;
        }
        if (N == 1) { // a plugin must not be able to remove another plugin's system
            const Status r = ctx.scheduler().remove_system("sys0");
            ev(r == Status::not_found ? "intrude:denied" : "intrude:allowed");
        }
        return mode == Mode::fail_load ? Status::load_failed : Status::ok;
    }

    void on_unload(IPluginContext& ctx) noexcept override
    {
        ev(std::string("unload:") + name());
        if (mode == Mode::remove_in_unload) {
            (void) ctx.scheduler().remove_system(N == 0 ? "sys0" : "sys1");
        }
    }
};

template<int N>
std::uint32_t abi_ok()
{
    return IPlugin::abi_current;
}
std::uint32_t abi_bad()
{
    return 999;
}
template<int N>
IPlugin* create_plugin()
{
    return new TestPlugin<N>();
}
template<int N>
void destroy_plugin(IPlugin* p)
{
    ev(std::string("destroy:") + p->name());
    delete static_cast<TestPlugin<N>*>(p);
}
template<int N>
PluginModule make_module()
{
    PluginModule m;
    m.abi_version = &abi_ok<N>;
    m.create      = &create_plugin<N>;
    m.destroy     = &destroy_plugin<N>;
    return m;
}

class PluginHostTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        g_events            = &m_events;
        TestPlugin<0>::mode = Mode::normal;
        TestPlugin<1>::mode = Mode::normal;
    }
    void TearDown() override { g_events = nullptr; }

    int index_of(const std::string& e) const
    {
        const auto it = std::find(m_events.begin(), m_events.end(), e);
        return it == m_events.end() ? -1 : static_cast<int>(it - m_events.begin());
    }
    int count_of(const std::string& e) const
    {
        return static_cast<int>(std::count(m_events.begin(), m_events.end(), e));
    }

    std::vector<std::string> m_events;
    World m_world;
    PluginHost m_host{m_world};
};

} // namespace

TEST_F(PluginHostTest, LoadRunUnloadInStrictOrder)
{
    ASSERT_EQ(m_host.load(make_module<0>()), Status::ok);
    EXPECT_TRUE(m_host.is_loaded("p0"));
    EXPECT_TRUE(m_world.scheduler().has_system("sys0"));

    ASSERT_EQ(m_world.run_frame(FrameContext{}), Status::ok);
    EXPECT_EQ(count_of("update:sys0"), 1);

    ASSERT_EQ(m_host.unload("p0"), Status::ok);
    EXPECT_FALSE(m_host.is_loaded("p0"));
    EXPECT_FALSE(m_world.scheduler().has_system("sys0"));

    const int unload  = index_of("unload:p0");
    const int release = index_of("release:sys0");
    const int destroy = index_of("destroy:p0");
    ASSERT_GE(unload, 0);
    EXPECT_LT(unload, release); // on_unload -> leftover systems -> destroy
    EXPECT_LT(release, destroy);
    EXPECT_EQ(count_of("release:sys0"), 1);
}

TEST_F(PluginHostTest, PluginRemovingItsOwnSystemInUnloadIsNotReleasedTwice)
{
    TestPlugin<0>::mode = Mode::remove_in_unload;
    ASSERT_EQ(m_host.load(make_module<0>()), Status::ok);
    ASSERT_EQ(m_host.unload("p0"), Status::ok);
    EXPECT_EQ(count_of("release:sys0"), 1);
    EXPECT_LT(index_of("release:sys0"), index_of("destroy:p0"));
}

TEST_F(PluginHostTest, FailedLoadRollsBack)
{
    TestPlugin<0>::mode = Mode::fail_load;
    EXPECT_EQ(m_host.load(make_module<0>()), Status::load_failed);
    EXPECT_FALSE(m_host.is_loaded("p0"));
    EXPECT_FALSE(m_world.scheduler().has_system("sys0"));
    EXPECT_EQ(count_of("release:sys0"), 1);
    EXPECT_EQ(count_of("destroy:p0"), 1);
    EXPECT_EQ(count_of("unload:p0"), 0); // on_unload is not called after a failed on_load
}

TEST_F(PluginHostTest, AbiMismatchIsRejectedBeforeCreate)
{
    PluginModule m = make_module<0>();
    m.abi_version  = &abi_bad;
    EXPECT_EQ(m_host.load(m), Status::abi_mismatch);
    EXPECT_TRUE(m_events.empty()); // no plugin code ran

    PluginModule incomplete;
    EXPECT_EQ(m_host.load(incomplete), Status::invalid_argument);
}

TEST_F(PluginHostTest, DuplicateNameIsRejected)
{
    ASSERT_EQ(m_host.load(make_module<0>()), Status::ok);
    EXPECT_EQ(m_host.load(make_module<0>()), Status::already_exists);
    EXPECT_EQ(m_host.count(), 1u);
    EXPECT_EQ(count_of("destroy:p0"), 1); // the rejected second instance
    EXPECT_EQ(m_host.unload("nope"), Status::not_found);
}

TEST_F(PluginHostTest, PluginCannotRemoveForeignSystems)
{
    ASSERT_EQ(m_host.load(make_module<0>()), Status::ok);
    ASSERT_EQ(m_host.load(make_module<1>()), Status::ok);
    EXPECT_EQ(count_of("intrude:denied"), 1);
    EXPECT_TRUE(m_world.scheduler().has_system("sys0"));
}

TEST(PluginHostLifetimeTest, HostUnloadsInReverseOrderOnDestruction)
{
    std::vector<std::string> events;
    g_events            = &events;
    TestPlugin<0>::mode = Mode::normal;
    TestPlugin<1>::mode = Mode::normal;
    {
        World w;
        PluginHost h(w);
        ASSERT_EQ(h.load(make_module<0>()), Status::ok);
        ASSERT_EQ(h.load(make_module<1>()), Status::ok);
    }
    g_events = nullptr;

    const auto pos = [&](const std::string& e) {
        return std::find(events.begin(), events.end(), e) - events.begin();
    };
    EXPECT_LT(pos("unload:p1"), pos("unload:p0"));
    EXPECT_LT(pos("destroy:p1"), pos("destroy:p0"));
}

// ---- exported C entry points via the macro ----
namespace {
class MacroPlugin final : public IPlugin
{
public:
    std::uint32_t abi_version() const noexcept override { return abi_current; }
    const char* name() const noexcept override { return "macro"; }
    Status on_load(IPluginContext&) noexcept override { return Status::ok; }
    void on_unload(IPluginContext&) noexcept override {}
};
} // namespace

BAKUON_DEFINE_PLUGIN(MacroPlugin)

TEST(PluginMacroTest, ExportedEntryPointsWork)
{
    PluginModule m;
    m.abi_version = &bakuon_plugin_abi_version;
    m.create      = &bakuon_create_plugin;
    m.destroy     = &bakuon_destroy_plugin;
    EXPECT_EQ(m.abi_version(), IPlugin::abi_current);

    World w;
    PluginHost h(w);
    ASSERT_EQ(h.load(m), Status::ok);
    EXPECT_TRUE(h.is_loaded("macro"));
    EXPECT_EQ(h.unload("macro"), Status::ok);
}
