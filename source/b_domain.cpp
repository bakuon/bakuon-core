#include "b_domain.h"

#include <utility>

namespace bakuon::core {

Domain::Domain(DomainId id, std::string name, std::uint64_t serial)
    : m_id(id)
    , m_name(std::move(name))
    , m_serial(serial)
    , m_components(m_registry)
{
}

Domain::~Domain()
{
    // m_components is destroyed before m_registry (member order): every TypeEntry
    // resets its named storage while the Registry is still alive. Registry's own
    // destructor then releases the engine-owned storages.
}

std::uint32_t Domain::abi_version() const noexcept
{
    return IDomain::abi_current;
}

Entity Domain::create() noexcept
{
    if (m_components.iterating()) {
        return Entity::null();
    }
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
    if (m_components.iterating()) {
        return Entity::null();
    }
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
    // Structural edits invalidate an active for_each()/each() iteration.
    if (m_components.iterating()) {
        return false;
    }
    // Guard first: destroying an invalid entity is undefined in the backend.
    if (entity.is_null() || !m_registry.valid(entity)) {
        return false;
    }
    try {
        // The Registry owns the component storages: destroy() erases this entity
        // from every one of them, running each ComponentSlot destructor.
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
    if (m_components.iterating()) {
        return;
    }
    try {
        // Destroys every entity and all attached component instances. Registrations
        // and their (now empty) named storages survive.
        m_registry.clear();
        m_alive_count = 0;
    } catch (...) {
    }
}

} // namespace bakuon::core
