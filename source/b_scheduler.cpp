#include "b_scheduler.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace bakuon::core {

namespace {

[[nodiscard]] std::vector<std::string> snapshot_dependencies(const ISystem& system)
{
    const std::uint32_t count = system.dependency_count();
    std::vector<std::string> deps;
    deps.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        char buffer[256]{};
        const std::uint32_t needed
            = system.dependency_at(i, buffer, static_cast<std::uint32_t>(sizeof(buffer)));
        if (needed == 0) {
            continue;
        }
        if (needed > sizeof(buffer)) {
            // 超长依赖名：截断仍可用于诊断，但注册阶段会因匹配失败而拒绝。
            buffer[sizeof(buffer) - 1] = '\0';
        }
        deps.emplace_back(buffer);
    }
    return deps;
}

[[nodiscard]] bool phase_less(SystemPhase a, SystemPhase b) noexcept
{
    return static_cast<std::uint32_t>(a) < static_cast<std::uint32_t>(b);
}

} // namespace

Scheduler::Scheduler(World& world) noexcept
    : m_world(&world)
{
}

Scheduler::~Scheduler()
{
    // 逆注册顺序销毁：后注册者可能依赖先注册者的副作用。
    while (!m_entries.empty()) {
        Entry entry = std::move(m_entries.back());
        m_entries.pop_back();
        if (entry.system) {
            if (entry.enabled) {
                entry.system->on_disable();
            }
            if (Domain* domain = m_world->resolve(entry.bind)) {
                entry.system->on_detach(*domain);
            }
        }
    }
}

bool Scheduler::register_system(ISystem* system, DomainHandle bind)
{
    if (system == nullptr) {
        fail("register_system: system is null");
        return false;
    }
    if (system->abi_version() != system_abi_version) {
        fail("register_system: abi_version mismatch");
        return false;
    }
    const std::string_view name = system->name();
    if (name.empty()) {
        fail("register_system: name is empty");
        return false;
    }
    if (find(name) != nullptr) {
        fail("register_system: duplicate system name");
        return false;
    }

    // 允许前向依赖：未知依赖名在后续 register 补齐后于 ensure_order 时再解析。
    // 仅当“当前已注册集合”内出现环或跨阶段依赖时立即拒绝并归还所有权。
    std::vector<std::string> deps = snapshot_dependencies(*system);

    Entry trial;
    trial.system       = std::unique_ptr<ISystem>(system);
    trial.bind         = bind;
    trial.enabled      = true;
    trial.dependencies = std::move(deps);

    m_entries.push_back(std::move(trial));
    m_order_dirty = true;

    if (!rebuild_order()) {
        // 仅环 / 跨阶段会导致失败；未知依赖不会在此处失败。
        Entry rolled = std::move(m_entries.back());
        m_entries.pop_back();
        m_order_dirty = true;
        rolled.system.release();
        return false;
    }

    Entry& entry = m_entries.back();
    if (Domain* domain = m_world->resolve(entry.bind)) {
        entry.system->on_attach(*domain);
    }
    entry.system->on_enable();
    clear_error();
    return true;
}

bool Scheduler::unregister_system(std::string_view name)
{
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [name](const Entry& e) {
        return e.system->name() == name;
    });
    if (it == m_entries.end()) {
        fail("unregister_system: unknown system");
        return false;
    }

    Entry entry = std::move(*it);
    m_entries.erase(it);
    m_order_dirty = true;

    if (entry.enabled) {
        entry.system->on_disable();
    }
    if (Domain* domain = m_world->resolve(entry.bind)) {
        entry.system->on_detach(*domain);
    }
    // unique_ptr 析构 ISystem
    clear_error();
    return true;
}

bool Scheduler::contains(std::string_view name) const noexcept
{
    return find(name) != nullptr;
}

bool Scheduler::set_enabled(std::string_view name, bool enabled)
{
    Entry* entry = find(name);
    if (entry == nullptr) {
        fail("set_enabled: unknown system");
        return false;
    }
    if (entry->enabled == enabled) {
        clear_error();
        return true;
    }
    entry->enabled = enabled;
    if (enabled) {
        entry->system->on_enable();
    } else {
        entry->system->on_disable();
    }
    clear_error();
    return true;
}

bool Scheduler::is_enabled(std::string_view name) const noexcept
{
    const Entry* entry = find(name);
    return entry != nullptr && entry->enabled;
}

void Scheduler::tick(float dt)
{
    ensure_order();
    for (const std::size_t index : m_order) {
        Entry& entry = m_entries[index];
        if (!entry.enabled) {
            continue;
        }

        if (entry.bind.isNull()) {
            // 全局：对每个存活 Domain 执行一次。
            m_world->each([&](Domain& domain) { invoke_update(entry, domain, dt); });
        } else if (Domain* domain = m_world->resolve(entry.bind)) {
            invoke_update(entry, *domain, dt);
        }
        // Domain 已销毁：静默跳过，保留注册（宿主可稍后重建同 serial 的域——实际上 serial
        // 不会复用，因此等价于永久跳过，直到 unregister）。
    }
}

void Scheduler::tick_domain(IDomain& domain, float dt)
{
    ensure_order();
    const DomainHandle handle = domain.handle();
    for (const std::size_t index : m_order) {
        Entry& entry = m_entries[index];
        if (!entry.enabled) {
            continue;
        }
        // 全局 System 或显式绑定到本 Domain 的 System。
        if (entry.bind.isNull() || entry.bind == handle) {
            invoke_update(entry, domain, dt);
        }
    }
}

Scheduler::Entry* Scheduler::find(std::string_view name) noexcept
{
    for (Entry& entry : m_entries) {
        if (entry.system->name() == name) {
            return &entry;
        }
    }
    return nullptr;
}

const Scheduler::Entry* Scheduler::find(std::string_view name) const noexcept
{
    for (const Entry& entry : m_entries) {
        if (entry.system->name() == name) {
            return &entry;
        }
    }
    return nullptr;
}

void Scheduler::fail(const char* message)
{
    m_error = message;
}

void Scheduler::ensure_order() const
{
    if (m_order_dirty) {
        // 正常路径下 register 已保证可排序；若运行时被破坏则保持空序。
        (void) rebuild_order();
    }
}

bool Scheduler::rebuild_order() const
{
    const std::size_t n = m_entries.size();
    m_order.clear();
    m_order.reserve(n);

    if (n == 0) {
        m_order_dirty = false;
        return true;
    }

    // 名字 → 下标
    std::unordered_map<std::string, std::size_t> index_of;
    index_of.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        index_of.emplace(std::string(m_entries[i].system->name()), i);
    }

    // 按 phase 分组；组内再拓扑。
    // 先生成“候选全序”：phase 升序，同 phase 内 priority 升序，再注册顺序。
    std::vector<std::size_t> candidates(n);
    for (std::size_t i = 0; i < n; ++i) {
        candidates[i] = i;
    }
    std::stable_sort(candidates.begin(), candidates.end(), [this](std::size_t a, std::size_t b) {
        const ISystem& sa = *m_entries[a].system;
        const ISystem& sb = *m_entries[b].system;
        if (sa.phase() != sb.phase()) {
            return phase_less(sa.phase(), sb.phase());
        }
        if (sa.priority() != sb.priority()) {
            return sa.priority() < sb.priority();
        }
        return a < b; // 注册顺序稳定
    });

    // 构建同阶段邻接：依赖边 dep → self（dep 必须先于 self）
    std::vector<std::vector<std::size_t>> adj(n);
    std::vector<std::uint32_t> indegree(n, 0);

    for (std::size_t i = 0; i < n; ++i) {
        const SystemPhase phase = m_entries[i].system->phase();
        for (const std::string& dep_name : m_entries[i].dependencies) {
            const auto it = index_of.find(dep_name);
            if (it == index_of.end()) {
                // 前向依赖：暂不建边，等依赖方注册后再 dirty 重建。
                continue;
            }
            const std::size_t dep = it->second;
            if (m_entries[dep].system->phase() != phase) {
                m_error = "register_system: dependency '" + dep_name + "' is not in the same phase";
                m_order_dirty = true;
                return false;
            }
            adj[dep].push_back(i);
            ++indegree[i];
        }
    }

    // Kahn：按 candidates 顺序作为稳定优先队列
    std::vector<std::size_t> ready;
    ready.reserve(n);
    for (const std::size_t idx : candidates) {
        if (indegree[idx] == 0) {
            ready.push_back(idx);
        }
    }

    std::vector<std::size_t> ordered;
    ordered.reserve(n);

    // 用线性扫描模拟稳定队列：每次取 ready 中在 candidates 里最靠前的
    std::vector<bool> in_ready(n, false);
    for (const std::size_t idx : ready) {
        in_ready[idx] = true;
    }

    while (ordered.size() < n) {
        std::size_t next = n;
        for (const std::size_t idx : candidates) {
            if (in_ready[idx]) {
                next = idx;
                break;
            }
        }
        if (next == n) {
            m_error       = "register_system: dependency cycle detected";
            m_order_dirty = true;
            return false;
        }
        in_ready[next] = false;
        ordered.push_back(next);
        for (const std::size_t child : adj[next]) {
            if (--indegree[child] == 0) {
                in_ready[child] = true;
            }
        }
    }

    m_order       = std::move(ordered);
    m_order_dirty = false;
    return true;
}

void Scheduler::invoke_update(Entry& entry, IDomain& domain, float dt)
{
    entry.system->update(domain, dt);
}

} // namespace bakuon::core
