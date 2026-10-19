#include "b_component_registry.h"

#include <entt/core/hashed_string.hpp>
#include <entt/entity/storage.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace bakuon::core {

namespace {

/// The payload allocation of one component instance. Move-only and RAII: constructing
/// it allocates and copies, emplacing it into the storage transfers ownership, and its
/// destructor releases the payload. This makes both success and failure paths safe.
class ComponentSlot final
{
public:
    ComponentSlot() noexcept = default;

    ComponentSlot(std::uint32_t size, std::uint32_t align, const void* source)
        : m_data(allocate(size, align))
        , m_size(size)
        , m_align(align)
    {
        if (m_data == nullptr) {
            return;
        }
        if (source != nullptr) {
            std::memcpy(m_data, source, m_size);
        } else {
            std::memset(m_data, 0, m_size);
        }
    }

    ~ComponentSlot() { release(); }

    ComponentSlot(const ComponentSlot&)            = delete;
    ComponentSlot& operator=(const ComponentSlot&) = delete;

    ComponentSlot(ComponentSlot&& other) noexcept
        : m_data(other.m_data)
        , m_size(other.m_size)
        , m_align(other.m_align)
    {
        other.m_data  = nullptr;
        other.m_size  = 0;
        other.m_align = 1;
    }

    ComponentSlot& operator=(ComponentSlot&& other) noexcept
    {
        if (this != &other) {
            release();
            m_data        = other.m_data;
            m_size        = other.m_size;
            m_align       = other.m_align;
            other.m_data  = nullptr;
            other.m_size  = 0;
            other.m_align = 1;
        }
        return *this;
    }

    [[nodiscard]] void* data() const noexcept { return m_data; }

private:
    static constexpr std::size_t default_alignment = alignof(std::max_align_t);

    [[nodiscard]] static void* allocate(std::uint32_t size, std::uint32_t align)
    {
        if (size == 0) {
            return nullptr;
        }
        if (align > default_alignment) {
            return ::operator new(size, std::align_val_t{static_cast<std::size_t>(align)});
        }
        return ::operator new(size);
    }

    void release() noexcept
    {
        if (m_data == nullptr) {
            return;
        }
        if (m_align > default_alignment) {
            ::operator delete(m_data, std::align_val_t{static_cast<std::size_t>(m_align)});
        } else {
            ::operator delete(m_data);
        }
        m_data = nullptr;
    }

    void* m_data{nullptr};
    std::uint32_t m_size{0};
    std::uint32_t m_align{1};
};

} // namespace

// The public id is a 32-bit FNV-1a value and doubles as the EnTT storage id. Both the
// host and every plugin/target must therefore see the same 32-bit ENTT_ID_TYPE.
static_assert(std::is_same_v<entt::id_type, std::uint32_t>,
              "ComponentId requires ENTT_ID_TYPE=std::uint32_t");
static_assert(entt::hashed_string::value("Position") == make_component_id("Position").value,
              "make_component_id must agree with entt::hashed_string");
static_assert(entt::hashed_string::value("game.Position")
                  == make_component_id("game.Position").value,
              "make_component_id must agree with entt::hashed_string");

struct ComponentRegistry::TypeEntry
{
    using Storage = Registry::storage_for_type<ComponentSlot>;

    TypeEntry(Registry& registry, ComponentId id_, const ComponentDesc& d, Storage& pool_)
        : m_registry(registry)
        , pool(pool_)
        , id(id_)
        , name(d.name)
        , size(d.size)
        , align(d.size == 0 ? 1u : d.align)
        , flags(d.flags)
        , serializer(d.serializer)
        , deserializer(d.deserializer)
        , editor_draw(d.editor_draw)
        , hook_user(d.hook_user)
    {
        if (d.size != 0 && d.default_value != nullptr) {
            const auto* p = static_cast<const unsigned char*>(d.default_value);
            default_bytes.assign(p, p + d.size);
        }
    }

    /// The storage is owned by the Registry. Removing it destroys every ComponentSlot
    /// (and therefore every payload) and frees the storage id for re-registration.
    ~TypeEntry() { m_registry.reset(static_cast<entt::id_type>(id.value)); }

    TypeEntry(const TypeEntry&)            = delete;
    TypeEntry& operator=(const TypeEntry&) = delete;

    [[nodiscard]] const void* default_ptr() const noexcept
    {
        return default_bytes.empty() ? nullptr : default_bytes.data();
    }

    Registry& m_registry;
    Storage& pool;

    ComponentId id;
    std::string name;
    std::uint32_t size;
    std::uint32_t align;
    std::uint32_t flags;
    ComponentSerializer serializer;
    ComponentDeserializer deserializer;
    ComponentEditorDraw editor_draw;
    void* hook_user;
    std::vector<unsigned char> default_bytes;
};

namespace {

class IterationGuard
{
public:
    explicit IterationGuard(std::uint32_t& depth) noexcept
        : m_depth(depth)
    {
        ++m_depth;
    }
    ~IterationGuard() { --m_depth; }

    IterationGuard(const IterationGuard&)            = delete;
    IterationGuard& operator=(const IterationGuard&) = delete;

private:
    std::uint32_t& m_depth;
};

} // namespace

ComponentRegistry::ComponentRegistry(Registry& registry)
    : m_registry(registry)
{
}

ComponentRegistry::~ComponentRegistry() = default;

ComponentRegistry::TypeEntry* ComponentRegistry::find_entry(ComponentId id) const noexcept
{
    const auto it = m_by_id.find(id);
    return it == m_by_id.end() ? nullptr : it->second;
}

Status ComponentRegistry::inregister(const ComponentDesc& d, ComponentId* out_id) noexcept
{
    if (d.struct_size != sizeof(ComponentDesc) || d.name == nullptr) {
        return Status::invalid_argument;
    }
    const std::size_t len = std::strlen(d.name);
    if (len == 0 || len > component_max_name_length) {
        return Status::invalid_argument;
    }
    if (d.size > component_max_size) {
        return Status::invalid_argument;
    }
    if (d.align == 0 || (d.align & (d.align - 1u)) != 0u || d.align > component_max_align) {
        return Status::invalid_argument;
    }
    const std::uint32_t size  = d.size;
    const std::uint32_t align = size == 0 ? 1u : d.align;
    if (size != 0 && size % align != 0) {
        return Status::invalid_argument;
    }

    const ComponentId id = make_component_id(d.name);
    if (!id.valid()) {
        return Status::invalid_argument;
    }

    if (const TypeEntry* existing = find_entry(id)) {
        if (existing->name != d.name) {
            return Status::id_collision;
        }
        if (existing->size != size || existing->align != align || existing->flags != d.flags) {
            return Status::type_mismatch;
        }
        if (out_id != nullptr) {
            *out_id = id;
        }
        return Status::ok;
    }

    const auto storage_id = static_cast<entt::id_type>(id.value);
    if (m_registry.storage(storage_id) != nullptr) {
        // The 32-bit name hash collides with an existing storage (another component
        // type or an engine-owned typed storage). Reject instead of aliasing it.
        return Status::id_collision;
    }

    try {
        auto& pool = m_registry.storage<ComponentSlot>(storage_id);
        auto entry = std::make_unique<TypeEntry>(m_registry, id, d, pool);
        m_types.push_back(std::move(entry));
        try {
            m_by_id.emplace(id, m_types.back().get());
        } catch (...) {
            m_types.pop_back(); // ~TypeEntry removes the storage again
            throw;
        }
    } catch (...) {
        // Covers every failure path: TypeEntry ctor, vector growth, map growth, and
        // the storage allocation itself. reset() is a no-op if the storage is gone.
        m_registry.reset(storage_id);
        return Status::out_of_memory;
    }

    if (out_id != nullptr) {
        *out_id = id;
    }
    return Status::ok;
}

Status ComponentRegistry::unregister(ComponentId id) noexcept
{
    if (m_iteration_depth != 0) {
        return Status::busy;
    }
    const auto it = m_by_id.find(id);
    if (it == m_by_id.end()) {
        return Status::not_registered;
    }
    const TypeEntry* raw = it->second;
    m_by_id.erase(it);
    m_types.erase(std::remove_if(m_types.begin(),
                                 m_types.end(),
                                 [raw](const std::unique_ptr<TypeEntry>& entry) {
                                     return entry.get() == raw;
                                 }),
                  m_types.end());
    return Status::ok;
}

ComponentId ComponentRegistry::find(const char* name) const noexcept
{
    const ComponentId id = make_component_id(name);
    if (!id.valid()) {
        return ComponentId{};
    }
    const TypeEntry* t = find_entry(id);
    return (t != nullptr && t->name == name) ? id : ComponentId{};
}

Status ComponentRegistry::describe(ComponentId id, ComponentInfo* out) const noexcept
{
    if (out == nullptr || out->struct_size != sizeof(ComponentInfo)) {
        return Status::invalid_argument;
    }
    const TypeEntry* t = find_entry(id);
    if (t == nullptr) {
        return Status::not_registered;
    }
    out->id           = t->id;
    out->name         = t->name.c_str();
    out->size         = t->size;
    out->align        = t->align;
    out->flags        = t->flags;
    out->serializer   = t->serializer;
    out->deserializer = t->deserializer;
    out->editor_draw  = t->editor_draw;
    out->hook_user    = t->hook_user;
    return Status::ok;
}

std::size_t ComponentRegistry::type_count() const noexcept
{
    return m_types.size();
}

ComponentId ComponentRegistry::type_at(std::size_t index) const noexcept
{
    return index < m_types.size() ? m_types[index]->id : ComponentId{};
}

Status ComponentRegistry::emplace(Entity e, ComponentId id, const void* init,
                                  void** out_ptr) noexcept
{
    if (out_ptr != nullptr) {
        *out_ptr = nullptr;
    }
    if (m_iteration_depth != 0) {
        return Status::busy;
    }
    TypeEntry* t = find_entry(id);
    if (t == nullptr) {
        return Status::not_registered;
    }
    if (e.is_null() || !m_registry.valid(e)) {
        return Status::not_alive;
    }
    if (t->pool.contains(e)) {
        if (out_ptr != nullptr) {
            *out_ptr = t->pool.get(e).data();
        }
        return Status::already_present;
    }

    try {
        if (t->size == 0) {
            t->pool.emplace(e, ComponentSlot{});
        } else {
            ComponentSlot slot(t->size, t->align, init != nullptr ? init : t->default_ptr());
            t->pool.emplace(e, std::move(slot));
        }
        if (out_ptr != nullptr && t->size != 0) {
            *out_ptr = t->pool.get(e).data();
        }
        return Status::ok;
    } catch (...) {
        return Status::out_of_memory;
    }
}

Status ComponentRegistry::replace(Entity e, ComponentId id, const void* bytes) noexcept
{
    TypeEntry* t = find_entry(id);
    if (t == nullptr) {
        return Status::not_registered;
    }
    if (e.is_null() || !m_registry.valid(e)) {
        return Status::not_alive;
    }
    if (!t->pool.contains(e)) {
        return Status::not_present;
    }
    if (t->size == 0) {
        return Status::ok;
    }
    if (bytes == nullptr) {
        return Status::invalid_argument;
    }
    std::memcpy(t->pool.get(e).data(), bytes, t->size);
    return Status::ok;
}

Status ComponentRegistry::remove(Entity e, ComponentId id) noexcept
{
    if (m_iteration_depth != 0) {
        return Status::busy;
    }
    TypeEntry* t = find_entry(id);
    if (t == nullptr) {
        return Status::not_registered;
    }
    if (e.is_null() || !m_registry.valid(e)) {
        return Status::not_alive;
    }
    return t->pool.remove(e) ? Status::ok : Status::not_present;
}

bool ComponentRegistry::has(Entity e, ComponentId id) const noexcept
{
    const TypeEntry* t = find_entry(id);
    return t != nullptr && !e.is_null() && m_registry.valid(e) && t->pool.contains(e);
}

void* ComponentRegistry::get(Entity e, ComponentId id) noexcept
{
    TypeEntry* t = find_entry(id);
    if (t == nullptr || e.is_null() || !m_registry.valid(e) || !t->pool.contains(e)) {
        return nullptr;
    }
    return t->pool.get(e).data();
}

const void* ComponentRegistry::get_const(Entity e, ComponentId id) const noexcept
{
    const TypeEntry* t = find_entry(id);
    if (t == nullptr || e.is_null() || !m_registry.valid(e) || !t->pool.contains(e)) {
        return nullptr;
    }
    return t->pool.get(e).data();
}

std::size_t ComponentRegistry::count(ComponentId id) const noexcept
{
    const TypeEntry* t = find_entry(id);
    return t != nullptr ? t->pool.size() : 0;
}

Status ComponentRegistry::clear(ComponentId id) noexcept
{
    if (m_iteration_depth != 0) {
        return Status::busy;
    }
    TypeEntry* t = find_entry(id);
    if (t == nullptr) {
        return Status::not_registered;
    }
    t->pool.clear();
    return Status::ok;
}

Status ComponentRegistry::for_each(ComponentId id, void* user, EachFn fn) noexcept
{
    if (fn == nullptr) {
        return Status::invalid_argument;
    }
    TypeEntry* t = find_entry(id);
    if (t == nullptr) {
        return Status::not_registered;
    }

    IterationGuard guard(m_iteration_depth);
    try {
        for (auto [entity, slot] : t->pool.each()) {
            void* data[1] = {slot.data()};
            fn(user, entity, data);
        }
    } catch (...) {
        return Status::callback_failed;
    }
    return Status::ok;
}

Status ComponentRegistry::each(const ComponentId* ids, std::uint32_t count, void* user,
                               EachFn fn) noexcept
{
    if (ids == nullptr || fn == nullptr || count == 0 || count > component_max_each_components) {
        return Status::invalid_argument;
    }

    TypeEntry* entries[component_max_each_components]{};
    std::uint32_t smallest    = 0;
    std::size_t smallest_size = std::numeric_limits<std::size_t>::max();
    for (std::uint32_t index = 0; index < count; ++index) {
        entries[index] = find_entry(ids[index]);
        if (entries[index] == nullptr) {
            return Status::not_registered;
        }
        const std::size_t pool_size = entries[index]->pool.size();
        if (pool_size < smallest_size) {
            smallest_size = pool_size;
            smallest      = index;
        }
    }
    if (smallest_size == 0) {
        return Status::ok;
    }

    IterationGuard guard(m_iteration_depth);
    try {
        for (auto [entity, slot] : entries[smallest]->pool.each()) {
            void* data[component_max_each_components]{};
            bool match = true;
            for (std::uint32_t index = 0; index < count; ++index) {
                if (index == smallest) {
                    data[index] = slot.data();
                    continue;
                }
                if (!entries[index]->pool.contains(entity)) {
                    match = false;
                    break;
                }
                data[index] = entries[index]->pool.get(entity).data();
            }
            if (match) {
                fn(user, entity, data);
            }
        }
    } catch (...) {
        return Status::callback_failed;
    }
    return Status::ok;
}

} // namespace bakuon::core
