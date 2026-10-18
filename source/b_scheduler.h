#pragma once

/** @file b_scheduler.h  INTERNAL. Phase + Kahn topological scheduler. */

#include <bakuon/core/system.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace bakuon::core {

class World;

class Scheduler final : public IScheduler
{
public:
    explicit Scheduler(World& world);
    ~Scheduler();

    Scheduler(const Scheduler&)            = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    Status add_system(const SystemDesc& desc, ISystem* system) noexcept override;
    Status remove_system(const char* name) noexcept override;
    bool has_system(const char* name) const noexcept override;
    std::size_t system_count() const noexcept override;
    const char* system_name_at(std::size_t index) const noexcept override;

    // ---- host only ----
    Status build() noexcept;
    Status run(const FrameContext& ctx) noexcept;
    bool uses_domain(const char* name) const noexcept;
    /// Releases every system (reverse registration order).
    void clear() noexcept;

private:
    struct Entry;
    Entry* find_entry(const char* name) const noexcept;
    Status fail_build(Status s) noexcept;

    World& m_world;
    std::vector<std::unique_ptr<Entry>> m_entries; // registration order
    std::vector<Entry*> m_order;                   // valid only when !m_dirty
    std::uint64_t m_next_seq{0};
    bool m_dirty{true};
    bool m_running{false};
    Status m_build_status{Status::ok};
};

} // namespace bakuon::core
