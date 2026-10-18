#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "b_scheduler.h"
#include "b_service.h"
#include "b_world.h"

#include <bakuon/core/service.h>

using namespace bakuon::core;

namespace {

/// 示例服务接口：共享头中声明 abi + id，两端同编译约定即可跨 DLL。
class ILogger
{
public:
    static constexpr std::uint32_t abi       = 1;
    static constexpr std::string_view id     = "core.Logger";

    virtual ~ILogger()                       = default;
    virtual void info(std::string_view msg)  = 0;
    virtual int call_count() const           = 0;
};

class Logger final : public ILogger
{
public:
    void info(std::string_view msg) override
    {
        last = std::string(msg);
        ++count;
    }
    int call_count() const override { return count; }

    std::string last;
    int count = 0;
};

class IRenderer
{
public:
    static constexpr std::uint32_t abi   = 2;
    static constexpr std::string_view id = "gfx.Renderer";

    virtual ~IRenderer()                 = default;
    virtual int width() const            = 0;
};

class Renderer final : public IRenderer
{
public:
    explicit Renderer(int w)
        : w(w)
    {
    }
    int width() const override { return w; }
    int w = 0;
};

} // namespace

TEST(ServiceTest, AddGetRemove)
{
    ServiceProvider provider;
    Logger logger;

    ASSERT_TRUE(add(provider, logger)); // 使用 T::id
    EXPECT_TRUE(provider.contains(ILogger::id));
    EXPECT_EQ(provider.count(), 1u);

    ILogger* got = try_get<ILogger>(provider);
    ASSERT_NE(got, nullptr);
    got->info("hello");
    EXPECT_EQ(logger.call_count(), 1);
    EXPECT_EQ(logger.last, "hello");

    ASSERT_TRUE(provider.remove(ILogger::id));
    EXPECT_FALSE(provider.contains(ILogger::id));
    EXPECT_EQ(try_get<ILogger>(provider), nullptr);
    EXPECT_EQ(logger.call_count(), 1); // remove 不销毁对象
}

TEST(ServiceTest, RejectDuplicateNullAndEmpty)
{
    ServiceProvider provider;
    Logger logger;

    ASSERT_TRUE(provider.add("core.Logger", &logger, ILogger::abi));
    EXPECT_FALSE(provider.add("core.Logger", &logger, ILogger::abi));
    EXPECT_NE(std::string_view{provider.last_error()}.find("duplicate"), std::string_view::npos);

    EXPECT_FALSE(provider.add("", &logger, 1));
    EXPECT_FALSE(provider.add("x", nullptr, 1));
}

TEST(ServiceTest, AbiMismatchReturnsNull)
{
    ServiceProvider provider;
    Logger logger;
    ASSERT_TRUE(provider.add(ILogger::id, &logger, /*abi=*/1));

    EXPECT_EQ(provider.get(ILogger::id, /*expected_abi=*/99), nullptr);
    EXPECT_NE(std::string_view{provider.last_error()}.find("abi"), std::string_view::npos);

    // 正确 abi 仍可用
    EXPECT_NE(provider.get(ILogger::id, 1), nullptr);
}

TEST(ServiceTest, MultipleServicesIndependent)
{
    ServiceProvider provider;
    Logger logger;
    Renderer renderer{1920};

    ASSERT_TRUE(add(provider, logger));
    ASSERT_TRUE(add(provider, renderer));
    EXPECT_EQ(provider.count(), 2u);

    EXPECT_EQ(try_get<ILogger>(provider)->call_count(), 0);
    EXPECT_EQ(try_get<IRenderer>(provider)->width(), 1920);
}

TEST(ServiceTest, WorldEmbedsServiceProvider)
{
    World world;
    Logger logger;
    ASSERT_TRUE(add(world.services(), logger));

    PluginContext ctx(world);
    ILogger* log = try_get<ILogger>(ctx.services());
    ASSERT_NE(log, nullptr);
    log->info("from plugin context");
    EXPECT_EQ(logger.last, "from plugin context");
}

TEST(ServiceTest, ProviderDoesNotOwnInstance)
{
    ServiceProvider provider;
    int destroyed = 0;
    {
        struct Guard : ILogger
        {
            explicit Guard(int& d)
                : d(d)
            {
            }
            ~Guard() override { ++d; }
            void info(std::string_view) override {}
            int call_count() const override { return 0; }
            int& d;
        };
        Guard guard(destroyed);
        ASSERT_TRUE(add(provider, static_cast<ILogger&>(guard)));
        EXPECT_EQ(destroyed, 0);
        ASSERT_TRUE(provider.remove(ILogger::id));
        EXPECT_EQ(destroyed, 0); // remove 不 delete
    }
    EXPECT_EQ(destroyed, 1); // 离开作用域才析构
}
