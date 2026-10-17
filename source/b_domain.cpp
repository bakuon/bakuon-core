#include "b_domain.h"

#include <utility>

namespace bakuon::core {

static_assert(std::is_same_v<entt::id_type, ComponentId>, "ComponentId must match ENTT_ID_TYPE");

Domain::Domain(DomainId id, std::string name, std::uint64_t serial)
    : m_id(id)
    , m_name(std::move(name))
    , m_serial(serial)
    , m_components(m_registry)
{
}

Domain::~Domain()
{
    // 先销毁其他
    // ...
    // 随后 Registry 才被销毁
}

std::uint32_t Domain::abi_version() const noexcept
{
    return IDomain::abi_current;
}

Entity Domain::create() noexcept
{
    try {
        const Entity e = m_registry.create();
        ++m_alive_count;
        return e;
    } catch (...) {
        return Entity::null();
    }
}

Entity Domain::create(Entity hint) noexcept
{
    if (hint.is_null()) {
        return create();
    }
    try {
        const Entity e = m_registry.create(hint);
        ++m_alive_count;
        return e;
    } catch (...) {
        return Entity::null();
    }
}

bool Domain::destroy(Entity entity) noexcept
{
    // Guard first: destroying an invalid entity is undefined in the backend.
    if (entity.is_null() || !m_registry.valid(entity)) {
        return false;
    }
    try {
        m_registry.destroy(entity);
    } catch (...) {
        return false;
    }
    --m_alive_count;
    return true;
}

bool Domain::alive(Entity entity) const noexcept
{
    return !entity.is_null() && m_registry.valid(entity);
}

void Domain::clear() noexcept
{
    try {
        m_registry.clear();
        m_alive_count = 0;
    } catch (...) {
    }
}

} // namespace bakuon::core
