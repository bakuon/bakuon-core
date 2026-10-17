#pragma once
/**
 * @file entity.h
 * @brief Public entity handle. The only entity header plugins need to include.
 *
 * ABI: Entity is a standard-layout, trivially copyable, 4-byte value type.
 * It may be passed/returned by value across DLL boundaries.
 * It has no virtual functions and no dependency on the host's ECS backend.
 *
 * Bit layout (identical to the backend's default 32-bit layout):
 *   [31..20] version (12 bits)   [19..0] index (20 bits)
 *
 * The backend recognizes this class automatically through the public
 * `entity_type` alias, the explicit raw constructor and the explicit
 * conversion operator below. Do not rename or remove them.
 */

#include <bakuon/core/api.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace bakuon::core {

class BAKUON_CORE_API Entity final
{
public:
    /// Must be named `entity_type` and be public (backend detection contract).
    using entity_type = std::uint32_t;

    static constexpr unsigned index_bits      = 20;
    static constexpr unsigned version_bits    = 12;
    static constexpr entity_type index_mask   = 0x000FFFFFu;
    static constexpr entity_type version_mask = 0x00000FFFu;
    static constexpr entity_type null_raw     = 0xFFFFFFFFu;

    /// Produces entity 0 (index 0, version 0). This is a VALID value, NOT null.
    /// Use `Entity e = null;` for an explicit null handle.
    constexpr Entity() noexcept = default;

    explicit constexpr Entity(entity_type raw) noexcept
        : m_raw(raw)
    {
    }

    /// Builds an entity from parts. Out-of-range bits are masked off.
    [[nodiscard]] static constexpr Entity make(entity_type index, entity_type version = 0) noexcept
    {
        return Entity{(index & index_mask) | ((version & version_mask) << index_bits)};
    }

    [[nodiscard]] constexpr entity_type value() const noexcept { return m_raw; }
    [[nodiscard]] constexpr entity_type index() const noexcept { return m_raw & index_mask; }
    [[nodiscard]] constexpr entity_type version() const noexcept { return m_raw >> index_bits; }

    /// Same semantics as the backend's null: only the index segment is inspected,
    /// the version bits are ignored.
    [[nodiscard]] constexpr bool is_null() const noexcept { return index() == index_mask; }

    [[nodiscard]] static constexpr Entity null() noexcept { return Entity{null_raw}; }

    [[nodiscard]] explicit constexpr operator entity_type() const noexcept { return m_raw; }

    /// Raw bitwise comparison. To test for null use `e == null` or `e.is_null()`.
    friend constexpr bool operator==(Entity a, Entity b) noexcept { return a.m_raw == b.m_raw; }
    friend constexpr bool operator!=(Entity a, Entity b) noexcept { return !(a == b); }

private:
    entity_type m_raw{};
};

/// Tag type for the null entity. Converts implicitly to Entity.
struct Null
{
    constexpr operator Entity() const noexcept { return Entity::null(); }

    friend constexpr bool operator==(Null, Null) noexcept { return true; }
    friend constexpr bool operator!=(Null, Null) noexcept { return false; }

    // Null-ness is decided by the index segment only (version ignored).
    friend constexpr bool operator==(Entity e, Null) noexcept { return e.is_null(); }
    friend constexpr bool operator==(Null, Entity e) noexcept { return e.is_null(); }
    friend constexpr bool operator!=(Entity e, Null) noexcept { return !e.is_null(); }
    friend constexpr bool operator!=(Null, Entity e) noexcept { return !e.is_null(); }
};

inline constexpr Null null{};

// ---- ABI contract ----------------------------------------------------------
static_assert(sizeof(Entity) == 4, "Entity must be 4 bytes");
static_assert(alignof(Entity) == 4, "Entity must be 4-byte aligned");
static_assert(std::is_standard_layout_v<Entity>, "Entity must be standard layout");
static_assert(std::is_trivially_copyable_v<Entity>, "Entity must be trivially copyable");
static_assert(!std::is_polymorphic_v<Entity>, "Entity must not have virtual functions");
static_assert(!std::is_convertible_v<std::uint32_t, Entity>, "raw ctor must be explicit");
static_assert(!std::is_convertible_v<Entity, std::uint32_t>, "raw conversion must be explicit");
static_assert(!Entity{}.is_null(), "Entity{} is entity 0, not null");
static_assert(Entity::null().is_null(), "null() must be null");
static_assert(Entity::index_bits + Entity::version_bits == 32, "bit layout must fill 32 bits");

} // namespace bakuon::core

template<>
struct std::hash<bakuon::core::Entity>
{
    std::size_t operator()(const bakuon::core::Entity& e) const noexcept
    {
        return std::hash<bakuon::core::Entity::entity_type>{}(e.value());
    }
};
