#include "b_world.h"
#include "bakuon/core/domain.h"
#include "bakuon/core/status.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bakuon::core {

namespace detail {

/// One registered domain hook. The callback is a plain C-style descriptor; `owner`
/// keeps a capturing lambda (or its heap copy) alive for as long as the hook exists.
struct HookEntry
{
    std::size_t id{0};
    DomainCallback callback;
    std::shared_ptr<void> owner;
};

struct WorldHooks
{
    std::vector<HookEntry> created;
    std::vector<HookEntry> destroying;
    std::size_t nextId = 1;
};

} // namespace detail

namespace {

using Hooks    = detail::WorldHooks;
using HookList = std::vector<detail::HookEntry> Hooks::*;

Connection add_hook(const std::shared_ptr<Hooks>& hooks, HookList list, DomainCallback callback,
                    std::shared_ptr<void> owner)
{
    if (!callback) {
        return Connection{};
    }

    const std::size_t id = hooks->nextId++;

    // Connection is an internal type: std::function is allowed here and can own the
    // weak_ptr capture. Build it before inserting so a throwing push_back cannot
    // leave a hook entry behind without a disconnector.
    Connection connection([weak = std::weak_ptr<Hooks>(hooks), list, id] {
        if (const auto locked = weak.lock()) {
            auto& entries = (*locked).*list;
            entries.erase(std::remove_if(entries.begin(),
                                         entries.end(),
                                         [id](const detail::HookEntry& entry) {
                                             return entry.id == id;
                                         }),
                          entries.end());
        }
    });

    // The callback descriptor is non-owning; `owner` keeps a capturing lambda alive
    // for as long as the hook entry exists (including notify() snapshots).
    (*hooks.*list).push_back(detail::HookEntry{id, callback, std::move(owner)});
    return connection;
}

void ensure_same_name(const IDomain& existing, std::string_view name)
{
    if (existing.name() != name) {
        throw WorldError("domain id collision: '" + std::string(name) + "' vs existing '"
                         + std::string(existing.name()) + "'");
    }
}

void notify(const Hooks& hooks, HookList list, IDomain& domain)
{
    // 拷贝快照：回调里允许 disconnect/新增钩子。owner 的 shared_ptr 副本保证
    // 正在执行的回调即使被 disconnect，其闭包也不会在本次调用期间被释放。
    const std::vector<detail::HookEntry> snapshot = hooks.*list;
    for (const auto& entry : snapshot) {
        if (entry.callback.invoke != nullptr) {
            entry.callback.invoke(entry.callback.user, domain);
        }
    }
}

} // namespace

World::World() noexcept
    : m_hooks(std::make_shared<detail::WorldHooks>())
    // , m_events(std::make_unique<EventBus>())
    // , m_services(std::make_unique<ServiceProvider>())
    , m_scheduler(std::make_unique<Scheduler>(*this))
{
}

World::~World()
{
    // clear() first releases all systems through Scheduler::clear() while the domains
    // are still alive, then destroys the domains. m_scheduler must stay valid for that
    // call: resetting it here first would make clear() dereference a null unique_ptr.
    try {
        clear();
    } catch (...) {
        // 析构中不能抛。钩子抛异常时放弃剩余通知，由 m_domains 的成员析构兜底回收。
    }

    m_scheduler.reset();

    // m_events 随成员析构自动释放。
}

IDomain* World::find(DomainId id) noexcept
{
    const auto it = m_domains.find(id);
    return it == m_domains.end() ? nullptr : it->second.get();
}

const IDomain* World::find(DomainId id) const noexcept
{
    const auto it = m_domains.find(id);
    return it == m_domains.end() ? nullptr : it->second.get();
}

IDomain* World::find_by_name(const char* name) noexcept
{
    IDomain* domain = find(Domain::from_name(name));
    return (domain && domain->name() == name) ? domain : nullptr;
}

IDomain* World::resolve(DomainHandle handle) noexcept
{
    IDomain* domain = find(handle.id());
    return (domain && domain->serial() == handle.serial()) ? domain : nullptr;
}

const IDomain* World::resolve(DomainHandle handle) const noexcept
{
    const IDomain* domain = find(handle.id());
    return (domain && domain->serial() == handle.serial()) ? domain : nullptr;
}

IDomain* World::create(const char* name, const Initializer& init)
{
    assertOwnerThread();
    const DomainId id = Domain::from_name(name);
    if (!id.valid()) {
        throw WorldError("World::create: domain name must not be empty");
    }
    if (const IDomain* existing = find(id)) {
        ensure_same_name(*existing, name);
        return nullptr;
    }
    return insert(id, std::string(name), init);
}

IDomain* World::require(const char* name, const Initializer& init)
{
    assertOwnerThread();
    const DomainId id = Domain::from_name(name);
    if (!id.valid()) {
        throw WorldError("World::require: domain name must not be empty");
    }
    if (IDomain* existing = find(id)) {
        ensure_same_name(*existing, name);
        if (existing->closing()) {
            throw WorldError("World::require: domain '" + std::string(name)
                             + "' is being destroyed");
        }
        return existing;
    }
    return insert(id, std::string(name), init);
}

Status World::destroy_by_name(const char* name)
{
    if (const auto* domain = find_by_name(name))
        return destroy(domain->id());
    return Status::not_found;
}

Status World::destroy(DomainId id)
{
    assertOwnerThread();
    const auto it = m_domains.find(id);
    if (it == m_domains.end() || it->second->m_closing) {
        return Status::not_found;
    }

    Domain& domain = *it->second;
    if (m_scheduler->uses_domain(domain.name())) {
        return Status::busy;
    }
    domain.m_closing = true; // mark closing
    try {
        notify(*m_hooks, &Hooks::destroying, domain);
    } catch (...) {
        domain.m_closing = false; // 钩子否决/失败：域保持可用
        throw;
    }

    // 回调期间 m_domains 可能被修改（rehash 会使 it 失效），重新定位。
    const auto current = m_domains.find(id);
    assert(current != m_domains.end() && current->second.get() == &domain);
    std::unique_ptr<Domain> owned = std::move(current->second);
    m_domains.erase(current);
    return Status::ok; // owned 析构：服务逆序 → Registry
}

IDomain* World::create_anonymous(std::string label, const Initializer& init)
{
    assertOwnerThread();
    return insert(DomainId{m_nextAnonymous++}, std::move(label), init);
}

void World::clear()
{
    assertOwnerThread();
    // Systems keep their owning domain alive (destroy() reports busy). Release them
    // first: their release() hooks only delete themselves, while the domains are still
    // valid. Without this, the loop below would keep picking the same in-use domain.
    m_scheduler->clear();
    // 逆创建序：后创建的域可能观察/依赖先创建的域。
    // 每轮重新挑 serial 最大者，钩子里新建/销毁域也不会让遍历失效。
    for (;;) {
        IDomain* last = nullptr;
        for (const auto entry : m_domains) {
            IDomain* candidate = entry.second.get();
            if (!candidate->closing() && (!last || candidate->serial() > last->serial())) {
                last = candidate;
            }
        }
        if (!last) {
            break;
        }
        (void) destroy(last->id());
    }
}

bool World::is_live(const Domain* domain, std::uint64_t serial) const noexcept
{
    for (const auto& d : m_domains) {
        if (d.second.get() == domain && d.second->serial() == serial) {
            return true;
        }
    }
    return false;
}

Domain* World::find_internal(std::string_view name) const noexcept
{
    if (name.empty()) {
        return nullptr;
    }
    for (const auto& d : m_domains) {
        if (d.second->name() == name) {
            return d.second.get();
        }
    }
    return nullptr;
}

Connection World::on_domain_created(DomainCallback callback)
{
    return add_created_hook(callback, {});
}

Connection World::on_domain_destroying(DomainCallback callback)
{
    return add_destroying_hook(callback, {});
}

Connection World::add_created_hook(DomainCallback callback, std::shared_ptr<void> owner)
{
    return add_hook(m_hooks, &Hooks::created, callback, std::move(owner));
}

Connection World::add_destroying_hook(DomainCallback callback, std::shared_ptr<void> owner)
{
    return add_hook(m_hooks, &Hooks::destroying, callback, std::move(owner));
}

IDomain* World::insert(DomainId id, std::string name, const Initializer& init)
{
    auto domain = std::unique_ptr<Domain>(new Domain(id, std::move(name), m_nextSerial++));
    if (init) {
        init(*domain); // 抛异常：domain 随 unique_ptr 析构，服务逆序销毁，Workspace 未被修改
    }
    IDomain& ref = *domain;
    m_domains.emplace(id, std::move(domain));
    notify(*m_hooks, &Hooks::created, ref);
    return &ref;
}

std::vector<DomainHandle> World::snapshot() const
{
    std::vector<DomainHandle> handles;
    handles.reserve(m_domains.size());
    for (const auto entry : m_domains) {
        handles.push_back(entry.second->handle());
    }
    std::sort(handles.begin(), handles.end(), [](const DomainHandle& a, const DomainHandle& b) {
        return a.serial() < b.serial();
    });
    return handles;
}

} // namespace bakuon::core
