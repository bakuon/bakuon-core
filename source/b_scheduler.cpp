#include "b_scheduler.h"

#include "b_names.h"
#include "b_world.h"
#include "bakuon/core/system.h"

#include <algorithm>
#include <cstring>
#include <queue>
#include <string>
#include <unordered_map>

namespace bakuon::core {

struct Scheduler::Entry
{
    std::string name;
    std::string domain_name;
    Phase phase{Phase::update};
    std::int32_t priority{0};
    std::vector<std::string> deps;
    ISystem* system{nullptr};
    IDomain* domain{nullptr};
    std::uint64_t seq{0};
};

Scheduler::Scheduler(World& world)
    : m_world(world)
{
}
Scheduler::~Scheduler()
{
    clear();
}

Scheduler::Entry* Scheduler::find_entry(const char* name) const noexcept
{
    if (name == nullptr) {
        return nullptr;
    }
    for (const auto& e : m_entries) {
        if (e->name == name) {
            return e.get();
        }
    }
    return nullptr;
}

Status Scheduler::add_system(const SystemDesc& d, ISystem* system) noexcept
{
    if (m_running) {
        return Status::busy;
    }
    if (d.struct_size != sizeof(SystemDesc) || system == nullptr || !is_valid_name(d.name)
        || !is_valid_name(d.domain)) {
        return Status::invalid_argument;
    }
    const int phase = static_cast<int>(d.phase);
    if (phase < 0 || phase >= phase_count) {
        return Status::invalid_argument;
    }
    if (d.kind != SystemKind::native) {
        return Status::unsupported;
    }
    if (system->abi_version() != ISystem::abi_current) {
        return Status::abi_mismatch;
    }
    if (d.depends_on_count != 0 && d.depends_on == nullptr) {
        return Status::invalid_argument;
    }
    for (std::size_t i = 0; i < d.depends_on_count; ++i) {
        if (!is_valid_name(d.depends_on[i]) || std::strcmp(d.depends_on[i], d.name) == 0) {
            return Status::invalid_argument;
        }
    }
    if (find_entry(d.name) != nullptr) {
        return Status::already_exists;
    }
    IDomain* domain = m_world.find(d.domain);
    if (domain == nullptr) {
        return Status::not_found;
    }

    try {
        auto e         = std::make_unique<Entry>();
        e->name        = d.name;
        e->domain_name = d.domain;
        e->phase       = d.phase;
        e->priority    = d.priority;
        e->system      = system;
        e->domain      = domain;
        e->seq         = m_next_seq;
        for (std::size_t i = 0; i < d.depends_on_count; ++i) {
            e->deps.emplace_back(d.depends_on[i]);
        }
        m_entries.push_back(std::move(e));
        ++m_next_seq;
        m_order.clear();
        m_dirty = true;
        return Status::ok;
    } catch (...) {
        return Status::out_of_memory;
    }
}

Status Scheduler::remove_system(const char* name) noexcept
{
    if (m_running) {
        return Status::busy;
    }
    const Entry* target = find_entry(name);
    if (target == nullptr) {
        return Status::not_found;
    }
    ISystem* system = target->system;
    m_entries.erase(std::remove_if(m_entries.begin(),
                                   m_entries.end(),
                                   [target](const std::unique_ptr<Entry>& p) {
                                       return p.get() == target;
                                   }),
                    m_entries.end());
    m_order.clear();
    m_dirty = true;
    system->release();
    return Status::ok;
}

bool Scheduler::has_system(const char* name) const noexcept
{
    return find_entry(name) != nullptr;
}
std::size_t Scheduler::system_count() const noexcept
{
    return m_entries.size();
}

const char* Scheduler::system_name_at(std::size_t index) const noexcept
{
    if (m_dirty || index >= m_order.size()) {
        return nullptr;
    }
    return m_order[index]->name.c_str();
}

bool Scheduler::uses_domain(const char* name) const noexcept
{
    if (name == nullptr) {
        return false;
    }
    for (const auto& e : m_entries) {
        if (e->domain_name == name) {
            return true;
        }
    }
    return false;
}

Status Scheduler::fail_build(Status s) noexcept
{
    m_order.clear();
    m_dirty        = false; // cache the failure; any add/remove marks dirty again
    m_build_status = s;
    return s;
}

Status Scheduler::build() noexcept
{
    try {
        m_order.clear();
        std::unordered_map<const Entry*, int> indegree;
        std::unordered_map<const Entry*, std::vector<Entry*>> out_edges;

        for (const auto& up : m_entries) {
            indegree.emplace(up.get(), 0);
        }
        for (const auto& up : m_entries) {
            Entry* e = up.get();
            for (const std::string& dep : e->deps) {
                Entry* f = find_entry(dep.c_str());
                if (f == nullptr) {
                    return fail_build(Status::dependency_missing);
                }
                if (static_cast<int>(f->phase) > static_cast<int>(e->phase)) {
                    return fail_build(Status::invalid_argument); // can never be satisfied
                }
                if (f->phase != e->phase) {
                    continue; // earlier phase: already satisfied
                }
                out_edges[f].push_back(e);
                ++indegree[e];
            }
        }

        struct Later
        {
            bool operator()(const Entry* a, const Entry* b) const noexcept
            {
                if (a->priority != b->priority) {
                    return a->priority > b->priority;
                }
                return a->seq > b->seq;
            }
        };

        for (int p = 0; p < phase_count; ++p) {
            std::priority_queue<Entry*, std::vector<Entry*>, Later> ready;
            std::size_t total = 0;
            for (const auto& up : m_entries) {
                Entry* e = up.get();
                if (static_cast<int>(e->phase) != p) {
                    continue;
                }
                ++total;
                if (indegree[e] == 0) {
                    ready.push(e);
                }
            }
            std::size_t emitted = 0;
            while (!ready.empty()) {
                Entry* e = ready.top();
                ready.pop();
                m_order.push_back(e);
                ++emitted;
                const auto it = out_edges.find(e);
                if (it == out_edges.end()) {
                    continue;
                }
                for (Entry* next : it->second) {
                    if (--indegree[next] == 0) {
                        ready.push(next);
                    }
                }
            }
            if (emitted != total) {
                return fail_build(Status::dependency_cycle);
            }
        }
        m_dirty        = false;
        m_build_status = Status::ok;
        return Status::ok;
    } catch (...) {
        m_order.clear();
        m_dirty = true;
        return Status::out_of_memory;
    }
}

Status Scheduler::run(const FrameContext& ctx) noexcept
{
    if (m_running) {
        return Status::busy;
    }
    const Status s = m_dirty ? build() : m_build_status;
    if (s != Status::ok) {
        return s;
    }
    m_running = true;
    for (Entry* e : m_order) {
        e->system->update(ctx, *e->domain);
    }
    m_running = false;
    return Status::ok;
}

void Scheduler::clear() noexcept
{
    std::vector<ISystem*> systems;
    try {
        systems.reserve(m_entries.size());
    } catch (...) {
    }
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
        systems.push_back((*it)->system); // noexcept after reserve
    }
    m_entries.clear();
    m_order.clear();
    m_dirty = true;
    for (ISystem* s : systems) {
        s->release();
    }
}

} // namespace bakuon::core
