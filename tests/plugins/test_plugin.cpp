// A real plugin module: public headers only, no link dependency on the host library.
#include <bakuon/core/component.h>
#include <bakuon/core/plugin.h>

#include <cstdint>
#include <new>

using namespace bakuon::core;

namespace {

struct Counter
{
    std::int32_t value;
};

// A hook pointing into THIS module: the type must be unregistered before unload.
std::int32_t counter_serialize(const void*, void*, std::size_t, std::size_t* out_size, void*)
{
    if (out_size != nullptr) {
        *out_size = sizeof(Counter);
    }
    return 0;
}

class CounterSystem final : public ISystem
{
public:
    explicit CounterSystem(TypedComponent<Counter> counter)
        : m_counter(counter)
    {
    }

    std::uint32_t abi_version() const noexcept override { return abi_current; }

    void update(const FrameContext&, IDomain&) noexcept override
    {
        (void) m_counter.for_each([](Entity, Counter& counter) { counter.value += 1; });
    }

    void release() noexcept override { delete this; }

private:
    TypedComponent<Counter> m_counter;
};

class TestPlugin final : public IPlugin
{
public:
    std::uint32_t abi_version() const noexcept override { return abi_current; }
    const char* name() const noexcept override { return "test_plugin"; }

    Status on_load(IPluginContext& ctx) noexcept override
    {
        IDomain* domain = ctx.require_domain("main");
        if (domain == nullptr) {
            return Status::load_failed;
        }

        ComponentDesc desc = make_component_desc<Counter>("TestCounter",
                                                          nullptr,
                                                          component_flag_serializable);
        desc.serializer    = &counter_serialize;
        ComponentId id{};
        Status s = domain->components().inregister(desc, &id);
        if (s != Status::ok) {
            return s;
        }
        s = TypedComponent<Counter>::bind(domain->components(), "TestCounter", m_counter);
        if (s != Status::ok) {
            (void) domain->components().unregister(id);
            return s;
        }

        m_domain = domain;
        m_entity = domain->create();
        s        = m_counter.add(m_entity, Counter{0});
        if (s == Status::ok) {
            auto* system = new (std::nothrow) CounterSystem(m_counter);
            if (system == nullptr) {
                s = Status::out_of_memory;
            } else {
                SystemDesc sd{};
                sd.struct_size = sizeof(sd);
                sd.name        = "test_plugin.counter";
                sd.domain      = "main";
                s              = ctx.scheduler().add_system(sd, system);
                if (s != Status::ok) {
                    system->release();
                }
            }
        }
        if (s != Status::ok) {
            teardown(ctx); // the host does not call on_unload after a failed on_load
        }
        return s;
    }

    void on_unload(IPluginContext& ctx) noexcept override { teardown(ctx); }

private:
    void teardown(IPluginContext& ctx) noexcept
    {
        (void) ctx.scheduler().remove_system("test_plugin.counter");
        if (m_domain != nullptr) {
            if (m_domain->alive(m_entity)) {
                m_domain->destroy(m_entity);
            }
            (void) m_counter.unregister();
        }
        m_domain = nullptr;
        m_entity = Entity::null();
    }

    IDomain* m_domain{nullptr};
    Entity m_entity{Entity::null()};
    TypedComponent<Counter> m_counter;
};

} // namespace

BAKUON_DEFINE_PLUGIN(TestPlugin)
