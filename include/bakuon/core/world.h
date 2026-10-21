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
#include <type_traits>
#include <utility>

namespace bakuon::core {

/**
 * @brief ABI-safe domain callback: a C-style function pointer plus an opaque user pointer.
 *
 * This is a plain value type (two pointers). It is safe to pass by value or by
 * reference across the DLL boundary: it contains no std::function, no templates and
 * no ownership. The callee never destroys `user`; ownership stays with the caller
 * unless a higher-level API explicitly says otherwise.
 */
struct DomainCallback final
{
    void* user{nullptr};
    void (*invoke)(void*, IDomain&){nullptr};

    [[nodiscard]] explicit operator bool() const noexcept { return invoke != nullptr; }

    /// Invokes the callback if it is set. Exceptions from the callback propagate;
    /// World::insert relies on that to discard a domain whose initializer failed.
    void operator()(IDomain& domain) const
    {
        if (invoke != nullptr) {
            invoke(user, domain);
        }
    }
};

static_assert(std::is_standard_layout_v<DomainCallback>, "DomainCallback must be standard layout");
static_assert(std::is_trivially_copyable_v<DomainCallback>,
              "DomainCallback must be trivially copyable");

namespace detail {

/// Stack/owner-side adapter that turns any callable (capturing lambda or plain
/// function pointer) into a DomainCallback. The callback points at `this`, so the
/// holder must outlive every use of the callback.
template<class F>
struct DomainInitializer
{
    F fn;

    static void invoke(void* user, IDomain& domain) { (*static_cast<F*>(user))(domain); }

    [[nodiscard]] DomainCallback callback() noexcept { return DomainCallback{this, &invoke}; }
};

/// Function-pointer thunk used when the callback is owned elsewhere (e.g. a hook
/// entry holds a shared_ptr<F>).
template<class F>
void invoke_domain_callback(void* user, IDomain& domain)
{
    (*static_cast<F*>(user))(domain);
}

} // namespace detail

class BAKUON_CORE_API IWorld
{
public:
    /// v2: callbacks are C-style function pointers, not std::function.
    static constexpr std::uint32_t abi_current = 2;

    using DomainCallback = bakuon::core::DomainCallback;
    using Initializer    = DomainCallback;

    [[nodiscard]] virtual std::uint32_t abi_version() const noexcept = 0;

    [[nodiscard]] virtual bool contains(DomainId id) const noexcept        = 0;
    [[nodiscard]] virtual std::size_t count() const noexcept               = 0;
    [[nodiscard]] virtual IDomain* find(DomainId id) noexcept              = 0;
    [[nodiscard]] virtual IDomain* find_by_name(const char* name) noexcept = 0;
    [[nodiscard]] virtual IDomain* resolve(DomainHandle handle) noexcept   = 0;

    /// Create a named domain; if a domain with the same name already exists, return nullptr; if the
    /// name is empty or there is a hash collision, throw error.
    [[nodiscard]] virtual IDomain* create(const char* name, const Initializer& init = {})  = 0;
    /// Finds the domain or creates it. Null on invalid name / allocation failure.
    [[nodiscard]] virtual IDomain* require(const char* name, const Initializer& init = {}) = 0;
    /// Return false if it does not exist or is being destroyed.
    virtual Status destroy(DomainId id)                                                    = 0;
    virtual Status destroy_by_name(const char* name)                                       = 0;

    [[nodiscard]] virtual IScheduler& scheduler() noexcept     = 0;
    /// Sorts the systems. Also done lazily by run_frame when something changed.
    virtual Status build_schedule() noexcept                   = 0;
    /// Runs all phases once. Runs nothing and returns the build error if the schedule is invalid.
    virtual Status run_frame(const FrameContext& ctx) noexcept = 0;

    /**
     * @brief Convenience overload accepting a capturing lambda or a plain function pointer.
     *
     * The callback runs synchronously inside create(); the holder lives on the stack
     * for the duration of the call. Use the DomainCallback overload when the callback
     * must carry an externally owned user pointer.
     */
    template<class F, std::enable_if_t<!std::is_same_v<std::decay_t<F>, Initializer>, int> = 0>
    [[nodiscard]] IDomain* create(const char* name, F&& init)
    {
        detail::DomainInitializer<std::decay_t<F>> holder{std::forward<F>(init)};
        return create(name, holder.callback());
    }

    /// Same as create(); the callback is invoked synchronously while the holder is alive.
    template<class F, std::enable_if_t<!std::is_same_v<std::decay_t<F>, Initializer>, int> = 0>
    [[nodiscard]] IDomain* require(const char* name, F&& init)
    {
        detail::DomainInitializer<std::decay_t<F>> holder{std::forward<F>(init)};
        return require(name, holder.callback());
    }

protected:
    IWorld()                         = default;
    IWorld(const IWorld&)            = default;
    IWorld& operator=(const IWorld&) = default;
    ~IWorld()                        = default;
};

} // namespace bakuon::core
