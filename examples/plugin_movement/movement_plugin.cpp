// Example plugin: public headers only. Registers Position/Velocity and a system
// that integrates position += velocity * dt.
#include "components.h"

#include <bakuon/core/component.h>
#include <bakuon/core/plugin.h>

#include <cstdint>
#include <cstring>
#include <new>

using namespace bakuon::core;

namespace {

const example::Position kDefaultPosition{0.f, 0.f, 0.f};

// A hook living in THIS module. Because Position carries it, the host removes the
// type automatically when this module unloads. Return codes: 0 ok, 1 buffer too small.
std::int32_t position_serialize(const void* component, void* out, std::size_t capacity,
                                std::size_t* out_size, void*)
{
    if (out_size != nullptr) {
        *out_size = sizeof(example::Position);
    }
    if (out == nullptr || capacity < sizeof(example::Position)) {
        return 1;
    }
    std::memcpy(out, component, sizeof(example::Position));
    return 0;
}

class IntegrateSystem final : public ISystem
{
public:
    IntegrateSystem(TypedComponent<example::Position> pos, TypedComponent<example::Velocity> vel)
        : m_position(pos)
        , m_velocity(vel)
    {
    }

    std::uint32_t abi_version() const noexcept override { return abi_current; }

    void update(const FrameContext& ctx, IDomain&) noexcept override
    {
        const float dt = static_cast<float>(ctx.delta_seconds);
        // Looking up other components while iterating is fine; adding/removing is not.
        // TODO: m_velocity.span(ComponentArray<example::Velocity> &out);
    }

    void release() noexcept override { delete this; }

private:
    TypedComponent<example::Position> m_position;
    TypedComponent<example::Velocity> m_velocity;
};

class MovementPlugin final : public IPlugin
{
public:
    std::uint32_t abi_version() const noexcept override { return abi_current; }
    const char* name() const noexcept override { return "movement"; }

    Status on_load(IPluginContext& ctx) noexcept override
    {
        IDomain* domain = ctx.require_domain("main");
        if (domain == nullptr) {
            return Status::load_failed;
        }
        IComponents& comps = domain->components();

        ComponentDesc pd = make_component_desc<example::Position>(example::position_name,
                                                                  &kDefaultPosition,
                                                                  component_flag_editor_visible
                                                                      | component_flag_serializable);
        pd.serializer = &position_serialize;
        Status s      = comps.inregister(pd, nullptr);
        if (s != Status::ok) {
            return s;
        }
        s = TypedComponent<example::Position>::bind(comps, example::position_name, m_position);
        if (s != Status::ok) {
            return s;
        }
        s = TypedComponent<example::Velocity>::inregister(comps, example::velocity_name, m_velocity);
        if (s != Status::ok) {
            return s;
        }

        auto* system = new (std::nothrow) IntegrateSystem(m_position, m_velocity);
        if (system == nullptr) {
            return Status::out_of_memory;
        }
        SystemDesc sd{};
        sd.struct_size = sizeof(sd);
        sd.name        = "movement.integrate";
        sd.domain      = "main";
        sd.phase       = Phase::update;
        s              = ctx.scheduler().add_system(sd, system);
        if (s != Status::ok) {
            system->release();
        }
        // On failure the host removes this plugin's systems and hooked types: nothing to undo here.
        return s;
    }

    void on_unload(IPluginContext&) noexcept override
    {
        // Nothing to do. The host removes our system and, because Position carries a hook into
        // this module, the Position type. Velocity (data only) stays registered with its data.
    }

private:
    TypedComponent<example::Position> m_position;
    TypedComponent<example::Velocity> m_velocity;
};

} // namespace

BAKUON_DEFINE_PLUGIN(MovementPlugin)
