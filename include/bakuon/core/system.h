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

/// Phases run in this order every frame. `fixed_update` is scheduled by a fixed timestep accumulator.
enum class Phase : std::int32_t {
    pre_update   = 0,
    fixed_update = 1,
    update       = 2,
    post_update  = 3,
    flush        = 4, // render
    post_flush   = 5,
    count
};
inline constexpr std::int32_t phase_count = static_cast<std::int32_t>(Phase::count);

/// Extension hook for future data-driven / scripted systems. Only `native` is accepted today.
enum class SystemKind : std::int32_t { native = 0, data_driven = 1, script = 2 };

struct SystemHandle final
{
    std::uint32_t index      = 0;
    std::uint32_t generation = 0; ///< 0 = invalid
    constexpr bool isValid() const noexcept { return generation != 0; }
    friend constexpr bool operator==(SystemHandle a, SystemHandle b) noexcept
    {
        return a.index == b.index && a.generation == b.generation;
    }
};

struct FrameContext final
{
    std::uint64_t frame_index{0};
    double delta_seconds{0.0}; /// fixed_update phase is equivalent to a fixed step size

    /// TODO: For future expansion
    // double total_seconds{0.0};
    // double fixed_delta_seconds{0.0};
    // std::uint32_t struct_size = sizeof(FrameContext);
    // Phase phase               = Phase::update;
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

    /// TODO:  For future expansion
    // SystemHandle handle{};
    // const char* group = "";
    // bool enabled      = true;
    // bool faulted      = false;
    // bool blocked = false; ///< Dependency missing/circular reference. Currently, it is not running.
};

class BAKUON_CORE_API ISystem
{
public:
    static constexpr std::uint32_t abi_current = 1;

    /// The default setting is to compile the plugin module in-line, so the reported ABI version is
    /// that of the plugin during compilation.
    [[nodiscard]] virtual std::uint32_t abi_version() const noexcept = 0;
    virtual void describe(SystemDesc*) const {}; // = 0
    virtual void update(const FrameContext& ctx, IDomain& domain) noexcept = 0;
    /// Called once by the host when ownership ends. Implementations `delete this`.
    virtual void release() noexcept                                        = 0;

    /// It is called when bound to a certain Domain (where component types can be registered).
    /// Returning false indicates rejection of that Domain.
    virtual bool on_attach(IDomain& /*domain*/) { return true; }
    virtual void on_detach(IDomain& /*domain*/) noexcept {}

    virtual void on_enable() noexcept {}
    virtual void on_disable() noexcept {}

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
    /// TODO: populate @p desc by ISystem::describe().
    /// TODO: add @p out SystemHandle *
    virtual Status add_system(const SystemDesc& desc, ISystem* system) noexcept        = 0;
    /// Removes and releases the system. Plugins can only remove their own.
    /// TODO: use @p handle SystemHandle
    virtual Status remove_system(const char* name) noexcept                            = 0;
    [[nodiscard]] virtual bool has_system(const char* name) const noexcept             = 0;
    [[nodiscard]] virtual std::size_t system_count() const noexcept                    = 0;
    /// Execution order after a successful build; nullptr if out of range or not built.
    [[nodiscard]] virtual const char* system_name_at(std::size_t index) const noexcept = 0;

protected:
    IScheduler()                             = default;
    IScheduler(const IScheduler&)            = default;
    IScheduler& operator=(const IScheduler&) = default;
    virtual ~IScheduler()                    = default;
};

} // namespace bakuon::core
