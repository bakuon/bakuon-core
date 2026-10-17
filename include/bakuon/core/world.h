#pragma once

/**
 * @file world.h
 * @brief Host-side world: isolated domains + one scheduler.
 *
 * Domain pointers are non-owning and valid until destroy_domain / world shutdown.
 * Domains with systems bound to them cannot be destroyed (Status::busy).
 */

#include <bakuon/core/api.h>
#include <bakuon/core/domain.h>
#include <bakuon/core/status.h>
#include <bakuon/core/system.h>

#include <cstddef>
#include <cstdint>

namespace bakuon::core {

class BAKUON_CORE_API IWorld
{
public:
    static constexpr std::uint32_t abi_current = 1;

    using Initializer    = std::function<void(IDomain&)>;
    using DomainCallback = std::function<void(IDomain&)>;

    [[nodiscard]] virtual std::uint32_t abi_version() const noexcept = 0;

    [[nodiscard]] virtual bool contains(DomainId id) const noexcept                  = 0;
    [[nodiscard]] virtual std::size_t count() const noexcept                         = 0;
    [[nodiscard]] virtual IDomain* find(DomainId id) noexcept                        = 0;
    [[nodiscard]] virtual const IDomain* find(DomainId id) const noexcept            = 0;
    [[nodiscard]] virtual IDomain* find(std::string_view name) noexcept              = 0;
    [[nodiscard]] virtual const IDomain* find(std::string_view name) const noexcept  = 0;
    [[nodiscard]] virtual IDomain* resolve(DomainHandle handle) noexcept             = 0;
    [[nodiscard]] virtual const IDomain* resolve(DomainHandle handle) const noexcept = 0;

    [[nodiscard]] virtual IDomain* create(std::string_view name, const Initializer& init = {})  = 0;
    /// Finds the domain or creates it. Null on invalid name / allocation failure.
    [[nodiscard]] virtual IDomain* require(std::string_view name, const Initializer& init = {}) = 0;
    virtual Status destroy(std::string_view name)                                               = 0;
    virtual Status destroy(DomainId id)                                                         = 0;

    [[nodiscard]] virtual IScheduler& scheduler() noexcept     = 0;
    /// Sorts the systems. Also done lazily by run_frame when something changed.
    virtual Status build_schedule() noexcept                   = 0;
    /// Runs all phases once. Runs nothing and returns the build error if the schedule is invalid.
    virtual Status run_frame(const FrameContext& ctx) noexcept = 0;

protected:
    IWorld()                         = default;
    IWorld(const IWorld&)            = default;
    IWorld& operator=(const IWorld&) = default;
    ~IWorld()                        = default;
};

} // namespace bakuon::core
