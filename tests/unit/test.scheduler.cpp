#include "b_world.h"
#include "bakuon/core/system.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace bakuon::core;

namespace {

struct Shared
{
    std::vector<std::string> log;
    int released{0};
    double last_dt{0.0};
    std::uint64_t last_frame{0};
    IDomain* last_domain{nullptr};
};

class Probe final : public ISystem
{
public:
    Probe(Shared* shared, std::string name)
        : m_shared(shared)
        , m_name(std::move(name))
    {
    }
    std::uint32_t abi_version() const noexcept override { return ISystem::abi_current; }
    void update(const FrameContext& ctx, IDomain& domain) noexcept override
    {
        try {
            m_shared->log.push_back(m_name);
        } catch (...) {
        }
        m_shared->last_dt     = ctx.delta_seconds;
        m_shared->last_frame  = ctx.frame_index;
        m_shared->last_domain = &domain;
    }
    void release() noexcept override
    {
        ++m_shared->released;
        delete this;
    }

private:
    Shared* m_shared;
    std::string m_name;
};

class BadAbi final : public ISystem
{
public:
    explicit BadAbi(Shared* shared)
        : m_shared(shared)
    {
    }
    std::uint32_t abi_version() const noexcept override { return 999; }
    void update(const FrameContext&, IDomain&) noexcept override {}
    void release() noexcept override
    {
        ++m_shared->released;
        delete this;
    }

private:
    Shared* m_shared;
};

class Reentrant final : public ISystem
{
public:
    Reentrant(IScheduler* sched, Status* result)
        : m_sched(sched)
        , m_result(result)
    {
    }
    std::uint32_t abi_version() const noexcept override { return ISystem::abi_current; }
    void update(const FrameContext&, IDomain&) noexcept override
    {
        SystemDesc d{};
        d.struct_size = sizeof(d);
        d.name        = "x";
        d.domain      = "main";
        *m_result     = m_sched->add_system(d, nullptr);
    }
    void release() noexcept override { delete this; }

private:
    IScheduler* m_sched;
    Status* m_result;
};

class SchedulerTest : public ::testing::Test
{
protected:
    void SetUp() override { ASSERT_TRUE(m_world.require("main") != nullptr); }

    // Caller keeps ownership when the scheduler rejects the system.
    Status add(const char* name, Phase phase = Phase::update, int priority = 0,
               std::vector<const char*> deps = {}, const char* domain = "main")
    {
        SystemDesc d{};
        d.struct_size      = sizeof(d);
        d.name             = name;
        d.domain           = domain;
        d.phase            = phase;
        d.priority         = priority;
        d.depends_on       = deps.empty() ? nullptr : deps.data();
        d.depends_on_count = deps.size();
        auto* s            = new Probe(&m_shared, name);
        const Status st    = sched().add_system(d, s);
        if (st != Status::ok) {
            s->release();
        }
        return st;
    }

    IScheduler& sched() { return m_world.scheduler(); }

    std::vector<std::string> order()
    {
        std::vector<std::string> v;
        for (std::size_t i = 0;; ++i) {
            const char* n = sched().system_name_at(i);
            if (n == nullptr) {
                break;
            }
            v.emplace_back(n);
        }
        return v;
    }

    Shared m_shared; // declared first: must outlive the world (release() writes to it)
    World m_world;
};

using Names = std::vector<std::string>;

} // namespace

TEST_F(SchedulerTest, PhasesRunInOrder)
{
    ASSERT_EQ(add("r", Phase::render), Status::ok);
    ASSERT_EQ(add("u", Phase::update), Status::ok);
    ASSERT_EQ(add("pre", Phase::pre_update), Status::ok);
    ASSERT_EQ(add("post", Phase::post_update), Status::ok);
    ASSERT_EQ(m_world.run_frame(FrameContext{}), Status::ok);
    EXPECT_EQ(m_shared.log, (Names{"pre", "u", "post", "r"}));
}

TEST_F(SchedulerTest, PriorityThenRegistrationOrder)
{
    ASSERT_EQ(add("p5", Phase::update, 5), Status::ok);
    ASSERT_EQ(add("p1", Phase::update, 1), Status::ok);
    ASSERT_EQ(add("p3a", Phase::update, 3), Status::ok);
    ASSERT_EQ(add("p3b", Phase::update, 3), Status::ok);
    ASSERT_EQ(m_world.build_schedule(), Status::ok);
    EXPECT_EQ(order(), (Names{"p1", "p3a", "p3b", "p5"}));
}

TEST_F(SchedulerTest, DependencyOverridesPriority)
{
    ASSERT_EQ(add("b", Phase::update, 0, {"a"}), Status::ok);
    ASSERT_EQ(add("a", Phase::update, 10), Status::ok);
    ASSERT_EQ(m_world.build_schedule(), Status::ok);
    EXPECT_EQ(order(), (Names{"a", "b"}));
}

TEST_F(SchedulerTest, ForwardDependencyIsResolvedAtBuild)
{
    ASSERT_EQ(add("b", Phase::update, 0, {"a"}), Status::ok);
    EXPECT_EQ(m_world.build_schedule(), Status::dependency_missing);
    EXPECT_EQ(m_world.run_frame(FrameContext{}), Status::dependency_missing);
    EXPECT_TRUE(m_shared.log.empty());

    ASSERT_EQ(add("a"), Status::ok); // marks the schedule dirty again
    ASSERT_EQ(m_world.build_schedule(), Status::ok);
    EXPECT_EQ(order(), (Names{"a", "b"}));
}

TEST_F(SchedulerTest, CycleIsDetected)
{
    ASSERT_EQ(add("a", Phase::update, 0, {"b"}), Status::ok);
    ASSERT_EQ(add("b", Phase::update, 0, {"a"}), Status::ok);
    EXPECT_EQ(m_world.build_schedule(), Status::dependency_cycle);
    EXPECT_EQ(m_world.run_frame(FrameContext{}), Status::dependency_cycle);
    EXPECT_TRUE(m_shared.log.empty());
    EXPECT_TRUE(sched().system_name_at(0) == nullptr);
}

TEST_F(SchedulerTest, CrossPhaseDependencies)
{
    ASSERT_EQ(add("pre", Phase::pre_update), Status::ok);
    ASSERT_EQ(add("u", Phase::update, 0, {"pre"}), Status::ok); // earlier phase: fine
    ASSERT_EQ(m_world.build_schedule(), Status::ok);
    EXPECT_EQ(order(), (Names{"pre", "u"}));

    ASSERT_EQ(add("pre2", Phase::pre_update, 0, {"u"}), Status::ok); // later phase: unsatisfiable
    EXPECT_EQ(m_world.build_schedule(), Status::invalid_argument);
}

TEST_F(SchedulerTest, OwnershipTransferAndRelease)
{
    ASSERT_EQ(add("a"), Status::ok);
    ASSERT_EQ(add("b"), Status::ok);
    EXPECT_TRUE(sched().has_system("a"));
    EXPECT_EQ(sched().system_count(), 2u);

    ASSERT_EQ(sched().remove_system("a"), Status::ok);
    EXPECT_EQ(m_shared.released, 1);
    EXPECT_FALSE(sched().has_system("a"));
    EXPECT_EQ(sched().remove_system("a"), Status::not_found);
}

TEST(SchedulerLifetimeTest, WorldReleasesRemainingSystems)
{
    Shared shared;
    {
        World w;
        ASSERT_TRUE(w.require("main") != nullptr);
        for (const char* n : {"a", "b", "c"}) {
            SystemDesc d{};
            d.struct_size = sizeof(d);
            d.name        = n;
            d.domain      = "main";
            ASSERT_EQ(w.scheduler().add_system(d, new Probe(&shared, n)), Status::ok);
        }
    }
    EXPECT_EQ(shared.released, 3);
}

TEST_F(SchedulerTest, RejectedSystemsStayWithTheCaller)
{
    ASSERT_EQ(add("a"), Status::ok);
    EXPECT_EQ(add("a"), Status::already_exists);
    EXPECT_EQ(add("n", Phase::update, 0, {}, "missing"), Status::not_found);
    EXPECT_EQ(add("self", Phase::update, 0, {"self"}), Status::invalid_argument);
    EXPECT_EQ(add("bad_phase", static_cast<Phase>(99)), Status::invalid_argument);
    EXPECT_EQ(add(""), Status::invalid_argument);
    EXPECT_EQ(sched().system_count(), 1u);
    EXPECT_EQ(m_shared.released, 5); // the 5 rejected probes were released by the test helper only
}

TEST_F(SchedulerTest, DescriptorValidation)
{
    SystemDesc d{};
    d.name   = "x";
    d.domain = "main";
    auto* p  = new Probe(&m_shared, "x");
    EXPECT_EQ(sched().add_system(d, p), Status::invalid_argument); // struct_size not set
    d.struct_size = sizeof(d);
    EXPECT_EQ(sched().add_system(d, nullptr), Status::invalid_argument);
    d.kind = SystemKind::script;
    EXPECT_EQ(sched().add_system(d, p), Status::unsupported);
    d.kind             = SystemKind::native;
    d.depends_on_count = 1; // count without array
    EXPECT_EQ(sched().add_system(d, p), Status::invalid_argument);
    p->release();

    auto* bad          = new BadAbi(&m_shared);
    d.depends_on_count = 0;
    EXPECT_EQ(sched().add_system(d, bad), Status::abi_mismatch);
    bad->release();
    EXPECT_EQ(sched().system_count(), 0u);
}

TEST_F(SchedulerTest, DomainInUseCannotBeDestroyed)
{
    ASSERT_EQ(add("a"), Status::ok);
    EXPECT_EQ(m_world.destroy("main"), Status::busy);
    ASSERT_EQ(sched().remove_system("a"), Status::ok);
    EXPECT_EQ(m_world.destroy("main"), Status::ok);
}

TEST_F(SchedulerTest, SystemReceivesDomainAndFrameContext)
{
    ASSERT_EQ(add("a"), Status::ok);
    ASSERT_EQ(m_world.run_frame(FrameContext{0.5, 7}), Status::ok);
    EXPECT_DOUBLE_EQ(m_shared.last_dt, 0.5);
    EXPECT_EQ(m_shared.last_frame, 7u);
    EXPECT_TRUE(m_shared.last_domain == m_world.find("main"));
}

TEST_F(SchedulerTest, SchedulerIsLockedWhileRunning)
{
    Status inner = Status::ok;
    SystemDesc d{};
    d.struct_size = sizeof(d);
    d.name        = "re";
    d.domain      = "main";
    ASSERT_EQ(sched().add_system(d, new Reentrant(&sched(), &inner)), Status::ok);
    ASSERT_EQ(m_world.run_frame(FrameContext{}), Status::ok);
    EXPECT_EQ(inner, Status::busy);
}
