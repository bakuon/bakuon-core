#include "b_component_storage.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

namespace bakuon::core {

ComponentStorage::ComponentStorage(std::uint32_t size, std::uint32_t align)
    : m_size(size)
    , m_align(align)
{
}

ComponentStorage::~ComponentStorage()
{
    release(m_buffer);
}

void ComponentStorage::release(unsigned char* p) noexcept
{
    if (p != nullptr) {
        ::operator delete(p, std::align_val_t{m_align});
    }
}

void ComponentStorage::reserve_one()
{
    const std::size_t needed = m_index.size() + 1;
    if (needed <= m_capacity) {
        return;
    }
    std::size_t new_cap = m_capacity != 0 ? m_capacity * 2 : 8;
    if (new_cap < needed) {
        new_cap = needed;
    }
    if (new_cap > std::numeric_limits<std::size_t>::max() / m_size) {
        throw std::bad_alloc();
    }
    auto* fresh = static_cast<unsigned char*>(
        ::operator new(new_cap * m_size, std::align_val_t{m_align}));
    if (m_index.size() != 0) {
        std::memcpy(fresh, m_buffer, m_index.size() * m_size);
    }
    release(m_buffer);
    m_buffer   = fresh;
    m_capacity = new_cap;
}

bool ComponentStorage::contains(Entity e) const noexcept
{
    return !e.is_null() && m_index.contains(e);
}

void* ComponentStorage::get(Entity e) noexcept
{
    if (!contains(e)) {
        return nullptr;
    }
    return m_buffer + m_index.index(e) * m_size;
}

const void* ComponentStorage::get(Entity e) const noexcept
{
    if (!contains(e)) {
        return nullptr;
    }
    return m_buffer + m_index.index(e) * m_size;
}

void* ComponentStorage::emplace(Entity e, const void* init, const void* fallback)
{
    reserve_one();   // may throw; nothing changed yet
    m_index.push(e); // may throw; payload untouched, size() unchanged
    void* slot      = m_buffer + (m_index.size() - 1) * m_size;
    const void* src = init != nullptr ? init : fallback;
    if (src != nullptr) {
        std::memcpy(slot, src, m_size);
    } else {
        std::memset(slot, 0, m_size);
    }
    return slot;
}

bool ComponentStorage::erase(Entity e) noexcept
{
    if (!contains(e)) {
        return false;
    }
    const std::size_t pos  = m_index.index(e);
    const std::size_t last = m_index.size() - 1;
    if (pos != last) {
        // Mirror the sparse set's swap-and-pop: the last element fills the hole.
        std::memcpy(m_buffer + pos * m_size, m_buffer + last * m_size, m_size);
    }
    m_index.erase(e);
    return true;
}

void ComponentStorage::clear() noexcept
{
    m_index.clear();
}

} // namespace bakuon::core
