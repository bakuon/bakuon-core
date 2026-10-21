#pragma once

/**
 * @file component.h
 * @brief Name-based, type-erased component API (ABI-stable, pure virtual).
 *
 * Rules for component types:
 *  - trivially copyable + standard layout (POD-like). The host copies them with memcpy.
 *  - 0 <= size <= component_max_size, align <= component_max_align, size % align == 0.
 *  - A type with `std::is_empty_v<T>` (a tag) is registered with size 0 and has no
 *    payload: emplace()/has() work, get() returns null.
 *
 * Ownership / lifetime:
 *  - Component storage is owned by the domain (one named EnTT storage per registered
 *    type inside the domain's Registry). Registrations, entities and payloads all die
 *    with the domain.
 *  - Each payload lives in its own allocation, so a pointer returned by get()/emplace()
 *    stays valid until THAT instance is removed, its entity is destroyed, the component
 *    type is unregistered, or the domain dies. Adding/removing other instances of the
 *    same type never invalidates it.
 *  - A component type that registers hook function pointers MUST be unregistered by
 *    the registering module before that module is unloaded.
 *
 * Iteration: use for_each() for one type and each() for the intersection of up to
 * component_max_each_components types. The callback must only read/write component
 * payloads: structural edits (create/destroy/emplace/remove/clear/unregister) are
 * rejected with Status::busy while an iteration is active. A callback that throws is
 * reported as Status::callback_failed; the exception never crosses the ABI.
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
#include <utility>

namespace bakuon::core {

/**
 * @brief Stable 32-bit component id: 32-bit FNV-1a of the canonical name.
 *
 * The value is a pure function of the UTF-8 name. It doubles as the EnTT storage id
 * inside the owning domain, which is why the backend is pinned to a 32-bit
 * ENTT_ID_TYPE (see the static assertions in b_component_registry.cpp).
 * 0 is invalid. Different names colliding is detected at registration time
 * (Status::id_collision).
 */
struct ComponentId final
{
    using value_type = std::uint32_t;

    value_type value{0};

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

static_assert(sizeof(ComponentId) == 4, "ComponentId must stay a 4-byte ABI value");
static_assert(std::is_standard_layout_v<ComponentId>, "ComponentId must be standard layout");
static_assert(std::is_trivially_copyable_v<ComponentId>, "ComponentId must be trivially copyable");

/**
 * @brief Computes the id from a NUL-terminated UTF-8 name.
 *
 * 32-bit FNV-1a. It is a pure function: plugin and host compute identical values
 * without sharing any state. nullptr and "" are invalid (return ComponentId{}).
 */
[[nodiscard]] constexpr ComponentId make_component_id(const char* name) noexcept
{
    if (name == nullptr || *name == '\0') {
        return ComponentId{};
    }
    std::uint32_t hash = 2166136261u;
    for (; *name != '\0'; ++name) {
        hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(*name));
        hash *= 16777619u;
    }
    return ComponentId{hash};
}

inline constexpr std::uint32_t component_max_size      = 1u << 20;
inline constexpr std::uint32_t component_max_align     = 4096;
inline constexpr std::size_t component_max_name_length = 255;

/// Hard limit of IComponents::each(). Keeps the host-side callback frame bounded.
inline constexpr std::uint32_t component_max_each_components = 16;

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
    std::uint32_t size{0};        ///< 0 = empty tag (no payload)
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

/**
 * @brief Component access of one domain. Obtained via IDomain::components().
 *
 * Non-owning; valid for the lifetime of the domain. Do not delete.
 * vtable policy: append-only, no overloaded virtuals. Any change bumps
 * IDomain::abi_current.
 */
class BAKUON_CORE_API IComponents
{
public:
    /**
     * @brief Callback used by for_each()/each().
     *
     * @param user  Opaque pointer supplied to for_each()/each().
     * @param e     Entity currently being visited.
     * @param data  Up to component_max_each_components payload pointers, aligned with
     *              the ids passed to each(). data[i] is null for an empty tag.
     *
     * Valid only for the duration of the call. Do not store it.
     */
    using EachFn = void (*)(void* user, Entity e, void* const* data);

    // ---- type registry ----
    /// Registers a type. Registering the same name with an identical layout
    /// (size, align, flags) is idempotent: returns ok and the existing id
    /// (hooks of the first registration win). A different layout -> type_mismatch.
    /// A name hash already used by a different name or by another registry storage
    /// -> id_collision.
    /// `out_id` may be null.
    virtual Status inregister(const ComponentDesc& desc, ComponentId* out_id) noexcept = 0;

    /// Removes the type and destroys all its instances.
    virtual Status unregister(ComponentId id) noexcept = 0;

    /// Returns an invalid id if the name is unknown.
    [[nodiscard]] virtual ComponentId find(const char* name) const noexcept    = 0;
    virtual Status describe(ComponentId id, ComponentInfo* out) const noexcept = 0;

    /// Number of registered types, in registration order (for editors/serializers).
    [[nodiscard]] virtual std::size_t type_count() const noexcept               = 0;
    [[nodiscard]] virtual ComponentId type_at(std::size_t index) const noexcept = 0;

    // ---- per-entity access ----
    /// Adds a component. `bytes` (size bytes) may be null -> default value or zeros.
    /// `bytes` must not point into component storage of this domain.
    /// `out` (optional) receives the instance; on already_present it
    /// receives the EXISTING instance, which is left untouched.
    virtual Status emplace(Entity e, ComponentId id, const void* bytes, void** out) noexcept = 0;

    /// Overwrites an existing instance in place. `bytes` may be null only for a tag.
    /// The payload address does not change. Returns not_present if the entity lacks it.
    virtual Status replace(Entity e, ComponentId id, const void* bytes) noexcept = 0;

    virtual Status remove(Entity e, ComponentId id) noexcept                = 0;
    [[nodiscard]] virtual bool has(Entity e, ComponentId id) const noexcept = 0;

    /// Null if the entity lacks the component, the id is unknown, or the type is a tag.
    [[nodiscard]] virtual void* get(Entity e, ComponentId id) noexcept                   = 0;
    /// Const twin of get(). Named distinctly: no overloaded virtuals across the ABI.
    [[nodiscard]] virtual const void* get_const(Entity e, ComponentId id) const noexcept = 0;

    // ---- bulk ----
    /// Number of instances; 0 for unknown ids. Tags are counted too.
    [[nodiscard]] virtual std::size_t count(ComponentId id) const noexcept = 0;
    /// Removes all instances of one type, keeping the registration.
    virtual Status clear(ComponentId id) noexcept                          = 0;

    /// Visits every instance of one type. data[0] is the payload (null for a tag).
    virtual Status for_each(ComponentId id, void* user, EachFn fn) noexcept = 0;

    /**
     * @brief Visits entities that own ALL of the given components (intersection).
     *
     * At most component_max_each_components ids. data[i] corresponds to ids[i] and is
     * null for a tag. Structural edits inside the callback are rejected with
     * Status::busy. Returns not_registered if any id is unknown.
     */
    virtual Status each(const ComponentId* ids, std::uint32_t count, void* user,
                        EachFn fn) noexcept = 0;

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
    static_assert(!std::is_pointer_v<T>, "do not store raw pointers in a POD component");
    static_assert(sizeof(T) <= component_max_size, "component too large");
    static_assert(alignof(T) <= component_max_align, "component over-aligned");

    ComponentDesc d{};
    d.struct_size   = static_cast<std::uint32_t>(sizeof(ComponentDesc));
    d.name          = name;
    d.size          = std::is_empty_v<T> ? 0u : static_cast<std::uint32_t>(sizeof(T));
    d.align         = std::is_empty_v<T> ? 1u : static_cast<std::uint32_t>(alignof(T));
    d.flags         = flags;
    d.default_value = default_value;
    return d;
}

/// Lightweight handle (api pointer + id). Copyable. Does not own anything.
template<class T>
class TypedComponent
{
    static_assert(std::is_trivially_copyable_v<T>, "component must be trivially copyable");
    static_assert(std::is_standard_layout_v<T>, "component must be standard layout");
    static_assert(!std::is_pointer_v<T>, "do not store raw pointers in a POD component");

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
        if (info.size != component_size || info.align != component_align) {
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
        return emplace_impl(e, is_tag ? nullptr : static_cast<const void*>(&value), out);
    }
    Status add_default(Entity e, T** out = nullptr) const noexcept
    {
        return emplace_impl(e, nullptr, out);
    }

    /// Overwrites an existing instance in place. Tags have no payload.
    Status replace(Entity e, const T& value) const noexcept
    {
        static_assert(!is_tag, "empty tags have no payload; add/remove is enough");
        return valid() ? m_api->replace(e, m_id, &value) : Status::invalid_argument;
    }

    Status remove(Entity e) const noexcept
    {
        return valid() ? m_api->remove(e, m_id) : Status::invalid_argument;
    }
    [[nodiscard]] bool has(Entity e) const noexcept { return valid() && m_api->has(e, m_id); }

    [[nodiscard]] T* get(Entity e) const noexcept
    {
        static_assert(!is_tag, "empty tags have no payload; use has()");
        return valid() ? static_cast<T*>(m_api->get(e, m_id)) : nullptr;
    }
    [[nodiscard]] const T* get_const(Entity e) const noexcept
    {
        static_assert(!is_tag, "empty tags have no payload; use has()");
        return valid() ? static_cast<const T*>(m_api->get_const(e, m_id)) : nullptr;
    }

    [[nodiscard]] std::size_t count() const noexcept { return valid() ? m_api->count(m_id) : 0; }

    Status clear() const noexcept
    {
        return valid() ? m_api->clear(m_id) : Status::invalid_argument;
    }

    /**
     * @brief Visits every instance of T.
     *
     * For a non-empty T the callback is `void(Entity, T&)`. For an empty tag it is
     * `void(Entity)`. Structural edits inside the callback are rejected.
     */
    template<class Fn>
    Status for_each(Fn&& fn) const noexcept
    {
        if (!valid()) {
            return Status::invalid_argument;
        }
        using fn_type = std::remove_reference_t<Fn>;
        return m_api->for_each(m_id,
                               &fn,
                               [](void* user, Entity e, [[maybe_unused]] void* const* data) {
                                   auto& callback = *static_cast<fn_type*>(user);
                                   if constexpr (is_tag) {
                                       callback(e);
                                   } else {
                                       callback(e, *static_cast<T*>(data[0]));
                                   }
                               });
    }

private:
    static constexpr bool is_tag                   = std::is_empty_v<T>;
    static constexpr std::uint32_t component_size  = is_tag ? 0u
                                                            : static_cast<std::uint32_t>(sizeof(T));
    static constexpr std::uint32_t component_align = is_tag
                                                         ? 1u
                                                         : static_cast<std::uint32_t>(alignof(T));

    Status emplace_impl(Entity e, const void* init, T** out) const noexcept
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

namespace detail {

template<typename Fn, typename... T, std::size_t... Index>
void invoke_each(Fn& fn, Entity entity, void* const* data, std::index_sequence<Index...>)
{
    fn(entity, *static_cast<T*>(data[Index])...);
}

} // namespace detail

/**
 * @brief Typed intersection view. Created by view(api, typed_component...).
 *
 * Only non-empty components can be joined: filter tag components with has().
 */
template<class... T>
class View
{
    static_assert(sizeof...(T) > 0, "view needs at least one component");
    static_assert((!std::is_empty_v<T> && ...), "tag types cannot be joined; filter with has()");

public:
    View(IComponents& api, TypedComponent<T>... components) noexcept
        : m_api(&api)
        , m_ids{components.id()...}
    {
    }

    /// Callback form: void(Entity, T&...). Order is unspecified.
    template<class Fn>
    Status each(Fn&& fn) const noexcept
    {
        using fn_type = std::remove_reference_t<Fn>;
        return m_api->each(m_ids,
                           static_cast<std::uint32_t>(sizeof...(T)),
                           &fn,
                           [](void* user, Entity entity, void* const* data) {
                               auto& callback = *static_cast<fn_type*>(user);
                               detail::invoke_each<fn_type, T...>(callback,
                                                                  entity,
                                                                  data,
                                                                  std::index_sequence_for<T...>{});
                           });
    }

private:
    IComponents* m_api;
    ComponentId m_ids[sizeof...(T)]{};
};

template<class... T>
[[nodiscard]] View<T...> view(IComponents& api, TypedComponent<T>... components) noexcept
{
    return View<T...>{api, components...};
}

} // namespace bakuon::core

template<>
struct std::hash<bakuon::core::ComponentId>
{
    std::size_t operator()(const bakuon::core::ComponentId& id) const noexcept
    {
        return std::hash<bakuon::core::ComponentId::value_type>{}(id.value);
    }
};
