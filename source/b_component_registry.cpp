#include "b_component_registry.h"
#include "b_component_storage.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace bakuon::core {

struct ComponentRegistry::TypeEntry
{
    TypeEntry(ComponentId id_, const ComponentDesc& d)
        : id(id_)
        , name(d.name)
        , size(d.size)
        , align(d.align)
        , flags(d.flags)
        , serializer(d.serializer)
        , deserializer(d.deserializer)
        , editor_draw(d.editor_draw)
        , hook_user(d.hook_user)
        , pool(d.size, d.align)
    {
        if (d.default_value != nullptr) {
            const auto* p = static_cast<const unsigned char*>(d.default_value);
            default_bytes.assign(p, p + d.size);
        }
    }

    const void* default_ptr() const noexcept
    {
        return default_bytes.empty() ? nullptr : default_bytes.data();
    }

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
    ComponentStorage pool; // use Registry.storage<PluginSlot>(id);
};

ComponentRegistry::ComponentRegistry(const Registry& registry)
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
    if (d.size == 0 || d.size > component_max_size) {
        return Status::invalid_argument;
    }
    if (d.align == 0 || (d.align & (d.align - 1)) != 0 || d.align > component_max_align
        || d.size % d.align != 0) {
        return Status::invalid_argument;
    }
    const ComponentId id = make_component_id(d.name);
    if (id == 0) { // !id.valid()
        return Status::invalid_argument;
    }

    try {
        if (const TypeEntry* existing = find_entry(id)) {
            if (existing->name != d.name) {
                return Status::id_collision;
            }
            if (existing->size != d.size || existing->align != d.align
                || existing->flags != d.flags) {
                return Status::type_mismatch;
            }
            if (out_id != nullptr) {
                *out_id = id;
            }
            return Status::ok;
        }

        auto entry     = std::make_unique<TypeEntry>(id, d);
        TypeEntry* raw = entry.get();
        m_types.push_back(std::move(entry));
        try {
            m_by_id.emplace(id, raw);
        } catch (...) {
            m_types.pop_back();
            throw;
        }
        if (out_id != nullptr) {
            *out_id = id;
        }
        return Status::ok;
    } catch (...) {
        return Status::out_of_memory;
    }
}

Status ComponentRegistry::unregister(ComponentId id) noexcept
{
    const auto it = m_by_id.find(id);
    if (it == m_by_id.end()) {
        return Status::not_registered;
    }
    const TypeEntry* raw = it->second;
    m_by_id.erase(it);
    m_types.erase(std::remove_if(m_types.begin(),
                                 m_types.end(),
                                 [raw](const std::unique_ptr<TypeEntry>& p) {
                                     return p.get() == raw;
                                 }),
                  m_types.end());
    return Status::ok;
}

ComponentId ComponentRegistry::find(const char* name) const noexcept
{
    const ComponentId id = make_component_id(name);
    if (id == 0) { // !id.valid()
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

std::size_t ComponentRegistry::count() const noexcept
{
    return m_types.size();
}

ComponentId ComponentRegistry::at(std::size_t index) const noexcept
{
    return index < m_types.size() ? m_types[index]->id : ComponentId{};
}

Status ComponentRegistry::emplace(Entity e, ComponentId id, const void* init,
                                  void** out_ptr) noexcept
{
    if (out_ptr != nullptr) {
        *out_ptr = nullptr;
    }
    TypeEntry* t = find_entry(id);
    if (t == nullptr) {
        return Status::not_registered;
    }
    if (e.is_null() || !m_registry.valid(e)) {
        return Status::not_alive;
    }
    if (void* existing = t->pool.get(e)) {
        if (out_ptr != nullptr) {
            *out_ptr = existing;
        }
        return Status::already_present;
    }
    try {
        void* p = t->pool.emplace(e, init, t->default_ptr());
        if (out_ptr != nullptr) {
            *out_ptr = p;
        }
        return Status::ok;
    } catch (...) {
        return Status::out_of_memory;
    }
}

Status ComponentRegistry::remove(Entity e, ComponentId id) noexcept
{
    TypeEntry* t = find_entry(id);
    if (t == nullptr) {
        return Status::not_registered;
    }
    return t->pool.erase(e) ? Status::ok : Status::not_present;
}

bool ComponentRegistry::has(Entity e, ComponentId id) const noexcept
{
    const TypeEntry* t = find_entry(id);
    return t != nullptr && t->pool.contains(e);
}

void* ComponentRegistry::get(Entity e, ComponentId id) noexcept
{
    TypeEntry* t = find_entry(id);
    return t != nullptr ? t->pool.get(e) : nullptr;
}

const void* ComponentRegistry::get(Entity e, ComponentId id) const noexcept
{
    const TypeEntry* t = find_entry(id);
    return t != nullptr ? t->pool.get(e) : nullptr;
}

std::size_t ComponentRegistry::count(ComponentId id) const noexcept
{
    const TypeEntry* t = find_entry(id);
    return t != nullptr ? t->pool.size() : 0;
}

Status ComponentRegistry::clear(ComponentId id) noexcept
{
    TypeEntry* t = find_entry(id);
    if (t == nullptr) {
        return Status::not_registered;
    }
    t->pool.clear();
    return Status::ok;
}

Status ComponentRegistry::span(ComponentId id, ComponentSpan* out) noexcept
{
    if (out == nullptr) {
        return Status::invalid_argument;
    }
    TypeEntry* t = find_entry(id);
    if (t == nullptr) {
        return Status::not_registered;
    }
    out->entities = t->pool.entities();
    out->data     = t->pool.data();
    out->count    = t->pool.size();
    out->stride   = t->pool.stride();
    return Status::ok;
}

void ComponentRegistry::erase_entity(Entity e) noexcept
{
    for (const auto& t : m_types) {
        t->pool.erase(e);
    }
}

void ComponentRegistry::clear_all() noexcept
{
    for (const auto& t : m_types) {
        t->pool.clear();
    }
}

} // namespace bakuon::core
