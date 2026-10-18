#pragma once

/**
 * @file component.h
 * @brief Name-based, type-erased component API (ABI-stable, pure virtual).
 *
 * Rules for component types:
 *  - trivially copyable + standard layout (POD-like). The host copies them with memcpy.
 *  - 0 < size <= component_max_size, align <= component_max_align, size % align == 0.
 *
 * Ownership / lifetime:
 *  - Component storage is owned by the domain. Pointers returned by add/get_*
 *    and spans stay valid only until the next add/remove/clear/destroy on the
 *    SAME domain. Never cache them across such calls.
 *  - A component type that registers hook function pointers MUST be
 *    unregistered by the registering module before that module is unloaded.
 *
 * Threading: none. A domain must be used from one thread at a time.
 * Errors: reported via Status / null results, never via exceptions.
 */

#include <bakuon/core/entity.h>
#include <bakuon/core/status.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace bakuon::core {

/// 必须等于 ENTT_ID_TYPE（默认 uint32_t）
/// 当前根据平台配置来设置（见根 CMakeList.txt BAKUON_ID_TYPE）
// using ComponentId = std::size_t;

/// Stable id: 64-bit FNV-1a of the component name. 0 is invalid.
struct ComponentId final
{
    std::size_t value{0};

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }

    friend constexpr bool operator==(ComponentId a, ComponentId b) noexcept
    {
        return a.value == b.value;
    }
    friend constexpr bool operator!=(ComponentId a, ComponentId b) noexcept
    {
        return a.value != b.value;
    }
};

/// Pure function of the name; plugin and host compute the same value.
[[nodiscard]] constexpr ComponentId make_component_id(const char* name) noexcept
{
    if (name == nullptr) {
        return ComponentId{};
    }
    std::uint64_t h = 14695981039346656037ull;
    for (; *name != '\0'; ++name) {
        h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(*name));
        h *= 1099511628211ull;
    }
    return ComponentId{h};
}

inline constexpr std::uint32_t component_max_size      = 1u << 20;
inline constexpr std::uint32_t component_max_align     = 4096;
inline constexpr std::size_t component_max_name_length = 255;

enum ComponentFlags : std::uint32_t {
    component_flag_none           = 0,
    component_flag_editor_visible = 1u << 0, ///< stored only; consumed by editor plugins
    component_flag_serializable   = 1u << 1, ///< stored only; consumed by serializer plugins
};

// ---- Hook signatures (reserved; the host stores them but never calls them) ----
// Hooks must not throw. `user` is ComponentDesc::hook_user.

/// Writes up to `capacity` bytes into `out`; sets *out_size to the required size.
using ComponentSerializer = std::int32_t (*)(const void* component, void* out, std::size_t capacity,
                                             std::size_t* out_size, void* user);
using ComponentDeserializer = std::int32_t (*)(void* component, const void* data, std::size_t size,
                                               void* user);
/// `draw_ctx` is an opaque pointer defined by the editor plugin.
using ComponentEditorDraw   = void (*)(void* component, void* draw_ctx, void* user);

/// Registration descriptor. Strings and default_value are copied by the host.
struct ComponentDesc final
{
    std::uint32_t struct_size{0}; ///< must equal sizeof(ComponentDesc)
    const char* name{nullptr};    ///< unique within the domain, UTF-8, NUL-terminated
    std::uint32_t size{0};
    std::uint32_t align{0};
    std::uint32_t flags{0};
    const void* default_value{nullptr}; ///< `size` bytes, or null = zero-initialised

    ComponentSerializer serializer{nullptr};
    ComponentDeserializer deserializer{nullptr};
    ComponentEditorDraw editor_draw{nullptr};
    void* hook_user{nullptr};
};

/// Read-only description returned by IComponents::describe.
/// The caller sets struct_size = sizeof(ComponentInfo) before the call.
struct ComponentInfo final
{
    std::uint32_t struct_size{0};
    ComponentId id{};
    const char* name{nullptr}; ///< valid until the type is unregistered
    std::uint32_t size{0};
    std::uint32_t align{0};
    std::uint32_t flags{0};

    ComponentSerializer serializer{nullptr};
    ComponentDeserializer deserializer{nullptr};
    ComponentEditorDraw editor_draw{nullptr};
    void* hook_user{nullptr};
};

/// Contiguous view of one component type. entities[i] owns the payload at
/// (char*)data + i * stride. Order is unspecified. Invalidated by any structural change.
struct ComponentSpan final
{
    const Entity* entities{nullptr};
    void* data{nullptr};
    std::size_t count{0};
    std::size_t stride{0};
};

/**
 * @brief Component access of one domain. Obtained via IDomain::components().
 *
 * Non-owning; valid for the lifetime of the domain. Do not delete.
 * vtable policy: append-only, no overloaded virtuals. Any change bumps
 * IDomain::abi_version_current.
 */
class BAKUON_CORE_API IComponents
{
public:
    // ---- type registry ----
    /// Registers a type. Registering the same name with an identical layout
    /// (size, align, flags) is idempotent: returns ok and the existing id
    /// (hooks of the first registration win). A different layout -> type_mismatch.
    /// `out_id` may be null.
    virtual Status inregister(const ComponentDesc& desc, ComponentId* out_id) noexcept = 0;

    /// Removes the type and destroys all its instances.
    virtual Status unregister(ComponentId id) noexcept = 0;

    /// Returns an invalid id if the name is unknown.
    [[nodiscard]] virtual ComponentId find(const char* name) const noexcept    = 0;
    virtual Status describe(ComponentId id, ComponentInfo* out) const noexcept = 0;

    /// Enumeration in registration order (for editors/serializers).
    [[nodiscard]] virtual std::size_t count() const noexcept               = 0;
    [[nodiscard]] virtual ComponentId at(std::size_t index) const noexcept = 0;

    // ---- per-entity access ----
    /// Adds a component. `init` (size bytes) may be null -> default value or zeros.
    /// `init` must not point into component storage of this domain.
    /// `out_ptr` (optional) receives the instance; on already_present it
    /// receives the EXISTING instance, which is left untouched.
    virtual Status emplace(Entity e, ComponentId id, const void* bytes, void** out) noexcept = 0;
    // virtual Status replace(Entity e, ComponentId id, const void* bytes)                      = 0;
    virtual Status remove(Entity e, ComponentId id) noexcept                                 = 0;
    [[nodiscard]] virtual bool has(Entity e, ComponentId id) const noexcept                  = 0;

    /// Null if the entity lacks the component or the id is unknown.
    [[nodiscard]] virtual void* get(Entity e, ComponentId id) noexcept             = 0;
    [[nodiscard]] virtual const void* get(Entity e, ComponentId id) const noexcept = 0;

    // ---- bulk ----
    /// Number of instances; 0 for unknown ids.
    [[nodiscard]] virtual std::size_t count(ComponentId id) const noexcept = 0;
    /// Removes all instances of one type, keeping the registration.
    virtual Status clear(ComponentId id) noexcept                          = 0;
    virtual Status span(ComponentId id, ComponentSpan* out) noexcept       = 0;

protected:
    IComponents()                              = default;
    IComponents(const IComponents&)            = default;
    IComponents& operator=(const IComponents&) = default;
    ~IComponents()                             = default;
};

template<class T>
[[nodiscard]] ComponentDesc make_component_desc(const char* name, const T* default_value = nullptr,
                                                std::uint32_t flags = component_flag_none) noexcept
{
    static_assert(std::is_trivially_copyable_v<T>, "component must be trivially copyable");
    static_assert(std::is_standard_layout_v<T>, "component must be standard layout");
    static_assert(sizeof(T) <= component_max_size, "component too large");
    static_assert(alignof(T) <= component_max_align, "component over-aligned");

    ComponentDesc d{};
    d.struct_size   = static_cast<std::uint32_t>(sizeof(ComponentDesc));
    d.name          = name;
    d.size          = static_cast<std::uint32_t>(sizeof(T));
    d.align         = static_cast<std::uint32_t>(alignof(T));
    d.flags         = flags;
    d.default_value = default_value;
    return d;
}

template<class T>
struct ComponentArray
{
    const Entity* entities{nullptr};
    T* data{nullptr};
    std::size_t count{0};
};

/// Lightweight handle (api pointer + id). Copyable. Does not own anything.
template<class T>
class TypedComponent
{
public:
    constexpr TypedComponent() noexcept = default;

    /// Registers T under `name` (idempotent for identical layout).
    [[nodiscard]] static Status inregister(IComponents& api, const char* name, TypedComponent& out,
                                           const T* default_value = nullptr,
                                           std::uint32_t flags    = component_flag_none) noexcept
    {
        const ComponentDesc d = make_component_desc<T>(name, default_value, flags);
        ComponentId id{};
        const Status s = api.inregister(d, &id);
        if (s == Status::ok) {
            out.m_api = &api;
            out.m_id  = id;
        }
        return s;
    }

    /// Call from on_unload for types registered with hooks.
    Status unregister() noexcept
    {
        if (!valid()) {
            return Status::invalid_argument;
        }
        const Status s = m_api->unregister(m_id);
        if (s == Status::ok) {
            m_api = nullptr;
            m_id  = ComponentId{};
        }
        return s;
    }

    /// Binds to a type registered by another module; verifies size/align against T.
    [[nodiscard]] static Status bind(IComponents& api, const char* name,
                                     TypedComponent& out) noexcept
    {
        const ComponentId id = api.find(name);
        if (!id.valid()) {
            return Status::not_registered;
        }
        ComponentInfo info{};
        info.struct_size = static_cast<std::uint32_t>(sizeof(ComponentInfo));
        const Status s   = api.describe(id, &info);
        if (s != Status::ok) {
            return s;
        }
        if (info.size != sizeof(T) || info.align != alignof(T)) {
            return Status::type_mismatch;
        }
        out.m_api = &api;
        out.m_id  = id;
        return Status::ok;
    }

    [[nodiscard]] bool valid() const noexcept { return m_api != nullptr && m_id.valid(); }
    [[nodiscard]] ComponentId id() const noexcept { return m_id; }

    Status add(Entity e, const T& value, T** out = nullptr) const noexcept
    {
        return emplace_impl(e, &value, out);
    }
    Status add_default(Entity e, T** out = nullptr) const noexcept
    {
        return emplace_impl(e, nullptr, out);
    }

    Status remove(Entity e) const noexcept
    {
        return valid() ? m_api->remove(e, m_id) : Status::invalid_argument;
    }
    [[nodiscard]] bool has(Entity e) const noexcept { return valid() && m_api->has(e, m_id); }

    [[nodiscard]] T* get(Entity e) const noexcept
    {
        return valid() ? static_cast<T*>(m_api->get(e, m_id)) : nullptr;
    }
    [[nodiscard]] const T* get_const(Entity e) const noexcept
    {
        return valid() ? static_cast<const T*>(m_api->get(e, m_id)) : nullptr;
    }

    [[nodiscard]] std::size_t count() const noexcept { return valid() ? m_api->count(m_id) : 0; }

    Status clear() const noexcept
    {
        return valid() ? m_api->clear(m_id) : Status::invalid_argument;
    }

    Status span(ComponentArray<T>& out) const noexcept
    {
        if (!valid()) {
            return Status::invalid_argument;
        }
        ComponentSpan raw{};
        const Status s = m_api->span(m_id, &raw);
        if (s == Status::ok) {
            out.entities = raw.entities;
            out.data     = static_cast<T*>(raw.data);
            out.count    = raw.count;
        }
        return s;
    }

private:
    Status emplace_impl(Entity e, const T* init, T** out) const noexcept
    {
        if (!valid()) {
            return Status::invalid_argument;
        }
        void* p        = nullptr;
        const Status s = m_api->emplace(e, m_id, init, &p);
        if (out != nullptr) {
            *out = static_cast<T*>(p);
        }
        return s;
    }

    IComponents* m_api{nullptr};
    ComponentId m_id{};
};

} // namespace bakuon::core

template<>
struct std::hash<bakuon::core::ComponentId>
{
    std::size_t operator()(const bakuon::core::ComponentId& id) const noexcept
    {
        return std::hash<std::size_t>{}(id.value);
    }
};
