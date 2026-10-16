#include "b_domain.h"

#include <cstring>
#include <new>
#include <utility>

namespace bakuon::core {

namespace {

[[nodiscard]] void* allocate_pod(std::uint32_t size, std::uint32_t align)
{
    if (size == 0) {
        return nullptr;
    }
    if (align > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        return ::operator new(size, std::align_val_t{static_cast<std::size_t>(align)});
    }
    return ::operator new(size);
}

[[nodiscard]] ComponentId hash_name(std::string_view name) noexcept
{
    std::uint32_t hash = 2166136261u;
    for (const unsigned char byte : name) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

} // namespace

static_assert(std::is_same_v<entt::id_type, ComponentId>, "ComponentId must match ENTT_ID_TYPE");
static_assert(entt::entt_traits<Entity>::entity_mask == Entity::index_mask);
static_assert(entt::entt_traits<Entity>::to_integral(Entity{
                  static_cast<Entity::entity_type>(entt::null)})
              == Entity::null_raw);
static_assert(Entity{} != null);

Registry& registry_of(IDomain& domain)
{
    return static_cast<Domain&>(domain).registry();
}

const Registry& registry_of(const IDomain& domain)
{
    return static_cast<const Domain&>(domain).registry();
}

bool bakuon_native_components_follow_entity(IDomain& domain)
{
    struct Armor
    {
        int value;
    };

    auto& impl          = static_cast<Domain&>(domain);
    const Entity entity = impl.registry().create();
    impl.registry().emplace<Armor>(entity, Armor{9});
    const bool alive = impl.registry().valid(entity) && impl.registry().all_of<Armor>(entity);
    impl.registry().destroy(entity);
    const bool gone = !impl.registry().valid(entity)
                      && !impl.registry().storage<Armor>().contains(entity);
    return alive && gone;
}

Domain::Domain(DomainId id, std::string name, std::uint64_t serial)
    : m_id(id)
    , m_name(std::move(name))
    , m_serial(serial)
{
}

Domain::~Domain()
{
    // 先销毁其他
    // ...
    // 随后 Registry 才被销毁
}

bool Domain::destroy(Entity entity)
{
    if (!m_registry.valid(entity)) {
        return false;
    }
    m_registry.destroy(entity);
    return true;
}
ComponentId Domain::register_component(std::string_view name, std::uint32_t size,
                                       std::uint32_t align)
{
    if (name.empty()) {
        return fail("component name is empty");
    }
    if (align == 0 || (align & (align - 1u)) != 0u) {
        return fail("component alignment must be a power of two");
    }
    if (size == 0) {
        align = 1;
    }

    if (const auto known = m_names.find(std::string{name}); known != m_names.end()) {
        const Record& record = m_records.at(known->second);
        if (record.size != size || record.align != align) {
            return fail("component reregistered with a different size or alignment");
        }
        return known->second;
    }

    // TODO: 使用 entt::hashed_string 代替 hash_name
    const ComponentId id = hash_name(name);
    if (id == 0) {
        return fail("component name hashes to 0; rename it");
    }
    if (m_records.find(id) != m_records.end() || m_registry.storage(id) != nullptr) {
        return fail("component id collides with an existing storage; rename it");
    }

    Storage& pool = m_registry.storage<PluginSlot>(id);
    m_records.emplace(id, Record{std::string{name}, size, align, &pool});
    m_names.emplace(std::string{name}, id);
    m_error.clear();
    return id;
}

bool Domain::unregister_component(ComponentId id)
{
    const auto it = m_records.find(id);
    if (it == m_records.end()) {
        fail("unknown component");
        return false;
    }
    const std::string name = it->second.name;
    m_records.erase(it);
    m_names.erase(name);
    m_registry.reset(id);
    m_error.clear();
    return true;
}

bool Domain::emplace(Entity entity, ComponentId id, const void* bytes, void** out)
{
    if (out != nullptr) {
        *out = nullptr;
    }
    Record* record = find(id);
    if (record == nullptr) {
        fail("unknown component");
        return false;
    }
    if (!m_registry.valid(entity)) {
        fail("invalid entity");
        return false;
    }
    if (record->pool->contains(entity)) {
        fail("component already present");
        return false;
    }
    if (record->size != 0 && bytes == nullptr) {
        fail("missing component bytes");
        return false;
    }

    void* memory = nullptr;
    try {
        memory = allocate_pod(record->size, record->align);
    } catch (const std::bad_alloc&) {
        fail("out of memory");
        return false;
    }
    if (record->size != 0) {
        std::memcpy(memory, bytes, record->size);
    }

    record->pool->emplace(entity, memory, record->size, record->align);
    if (out != nullptr) {
        *out = memory;
    }
    m_error.clear();
    return true;
}

bool Domain::replace(Entity entity, ComponentId id, const void* bytes)
{
    Record* record = find(id);
    if (record == nullptr || !m_registry.valid(entity) || !record->pool->contains(entity)) {
        fail("component is not present");
        return false;
    }
    if (record->size == 0) {
        m_error.clear();
        return true;
    }
    if (bytes == nullptr) {
        fail("missing component bytes");
        return false;
    }
    std::memcpy(record->pool->get(entity).data, bytes, record->size);
    m_error.clear();
    return true;
}

bool Domain::remove(Entity entity, ComponentId id)
{
    Record* record = find(id);
    if (record == nullptr || !m_registry.valid(entity)) {
        return false;
    }
    const bool removed = record->pool->remove(entity);
    if (removed) {
        m_error.clear();
    }
    return removed;
}

void* Domain::get(Entity entity, ComponentId id)
{
    return const_cast<void*>(std::as_const(*this).get(entity, id));
}

const void* Domain::get(Entity entity, ComponentId id) const
{
    const Record* record = find(id);
    if (record == nullptr || record->size == 0 || !m_registry.valid(entity)
        || !record->pool->contains(entity)) {
        return nullptr;
    }
    return record->pool->get(entity).data;
}

[[nodiscard]] bool Domain::has(Entity entity, ComponentId id) const
{
    const Record* record = find(id);
    return record != nullptr && m_registry.valid(entity) && record->pool->contains(entity);
}

[[nodiscard]] std::uint32_t Domain::count(ComponentId id) const
{
    const Record* record = find(id);
    return record == nullptr ? 0u : static_cast<std::uint32_t>(record->pool->size());
}

void Domain::each(const ComponentId* ids, std::uint32_t n, void* user, EachFn fn)
{
    if (ids == nullptr || fn == nullptr || n == 0) {
        return;
    }
    if (n > 16u) {
        fail("each() supports at most 16 components");
        return;
    }

    struct Item
    {
        const Record* record = nullptr;
    };
    Item items[16];
    std::uint32_t smallest     = 0;
    std::size_t smallest_count = static_cast<std::size_t>(-1);
    for (std::uint32_t index = 0; index < n; ++index) {
        items[index].record = find(ids[index]);
        if (items[index].record == nullptr) {
            return;
        }
        const std::size_t pool_count = items[index].record->pool->size();
        if (pool_count < smallest_count) {
            smallest_count = pool_count;
            smallest       = index;
        }
    }
    if (smallest_count == 0) {
        return;
    }

    auto& storage = *items[smallest].record->pool;
    for (auto [entity, slot] : storage.each()) {
        void* data[16] = {};
        bool match     = true;
        for (std::uint32_t index = 0; index < n; ++index) {
            Storage* pool = items[index].record->pool;
            if (index != smallest && !pool->contains(entity)) {
                match = false;
                break;
            }
            data[index] = items[index].record->size == 0 ? nullptr : pool->get(entity).data;
        }
        if (match) {
            fn(user, entity, data);
        }
    }
}

} // namespace bakuon::core
