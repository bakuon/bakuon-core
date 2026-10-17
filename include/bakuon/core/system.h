#pragma once
/**
 * @file system.h
 * @brief Systems and the registration-side scheduler interface.
 *
 * Ownership: after add_system() returns ok the scheduler owns the system and
 * will call release() exactly once (on remove_system, on plugin unload, or at
 * world shutdown). On any other status the caller keeps ownership.
 * release() must destroy the object in the module that created it
 * (typically `delete this`) and must not call back into the scheduler.
 *
 * Systems must not throw from update(). Running is single-threaded.
 */

#include <bakuon/core/api.h>
#include <bakuon/core/domain.h>
#include <bakuon/core/status.h>

#include <cstddef>
#include <cstdint>

namespace bakuon::core {

/// Phases run in this order every frame.
enum class Phase : std::int32_t { pre_update = 0, update = 1, post_update = 2, render = 3, count };
inline constexpr std::int32_t phase_count = static_cast<std::int32_t>(Phase::count);

/// Extension hook for future data-driven / scripted systems. Only `native` is accepted today.
enum class SystemKind : std::int32_t { native = 0, data_driven = 1, script = 2 };

struct FrameContext final
{
    double delta_seconds{0.0};
    std::uint64_t frame_index{0};
};

struct SystemDesc final
{
    std::uint32_t struct_size{0}; ///< must equal sizeof(SystemDesc)
    std::int32_t priority{0};     ///< lower runs earlier among systems with no ordering constraint
    const char* name{nullptr};    ///< unique within the world
    const char* domain{nullptr};  ///< domain the system runs on; must already exist
    const char* const* depends_on{nullptr}; ///< names of systems that must run before this one
    std::size_t depends_on_count{0};
    Phase phase{Phase::update};
    SystemKind kind{SystemKind::native};
};

class BAKUON_CORE_API ISystem
{
public:
    static constexpr std::uint32_t abi_current = 1;

    [[nodiscard]] virtual std::uint32_t abi_version() const noexcept       = 0;
    virtual void update(const FrameContext& ctx, IDomain& domain) noexcept = 0;
    /// Called once by the host when ownership ends. Implementations `delete this`.
    virtual void release() noexcept                                        = 0;

protected:
    ISystem()                          = default;
    ISystem(const ISystem&)            = default;
    ISystem& operator=(const ISystem&) = default;
    ~ISystem()                         = default;
};

/// Registration and introspection. Running the schedule is host-only (IWorld).
class BAKUON_CORE_API IScheduler
{
public:
    virtual Status add_system(const SystemDesc& desc, ISystem* system) noexcept        = 0;
    /// Removes and releases the system. Plugins can only remove their own.
    virtual Status remove_system(const char* name) noexcept                            = 0;
    [[nodiscard]] virtual bool has_system(const char* name) const noexcept             = 0;
    [[nodiscard]] virtual std::size_t system_count() const noexcept                    = 0;
    /// Execution order after a successful build; nullptr if out of range or not built.
    [[nodiscard]] virtual const char* system_name_at(std::size_t index) const noexcept = 0;

protected:
    IScheduler()                             = default;
    IScheduler(const IScheduler&)            = default;
    IScheduler& operator=(const IScheduler&) = default;
    ~IScheduler()                            = default;
};

} // namespace bakuon::core
