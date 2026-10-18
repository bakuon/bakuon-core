#include "b_world.h"
#include "bakuon/core/domain.h"
#include "bakuon/core/status.h"

#include <algorithm>
#include <string>

namespace bakuon::core {

namespace detail {
struct WorldHooks
{
    using Entry = std::pair<std::size_t, World::DomainCallback>;
    std::vector<Entry> created;
    std::vector<Entry> destroying;
    std::size_t nextId = 1;
};

} // namespace detail

namespace {

using Hooks    = detail::WorldHooks;
using HookList = std::vector<Hooks::Entry> Hooks::*;

Connection add_hook(const std::shared_ptr<Hooks>& hooks, HookList list,
                    World::DomainCallback callback)
{
    const std::size_t id = hooks->nextId++;
    (*hooks.*list).emplace_back(id, std::move(callback));
    // 只持 weak_ptr：Workspace 先于 Connection 销毁时，断开是安全的空操作。
    return Connection([weak = std::weak_ptr<Hooks>(hooks), list, id] {
        if (const auto locked = weak.lock()) {
            auto& entries = (*locked).*list;
            entries.erase(std::remove_if(entries.begin(),
                                         entries.end(),
                                         [id](const Hooks::Entry& e) { return e.first == id; }),
                          entries.end());
        }
    });
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
    // 拷贝快照：回调里允许 disconnect/新增钩子。
    const std::vector<Hooks::Entry> snapshot = hooks.*list;
    for (const auto& entry : snapshot) {
        if (entry.second) {
            entry.second(domain);
        }
    }
}

} // namespace

World::World() noexcept
    : m_scheduler(*this)
    , m_hooks(std::make_shared<detail::WorldHooks>())
{
}

World::~World()
{
    try {
        clear();
    } catch (...) {
        // 析构中不能抛。钩子抛异常时放弃剩余通知，由 m_domains 的成员析构兜底回收。
    }
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

IDomain* World::find(std::string_view name) noexcept
{
    IDomain* domain = find(DomainId::from_name(name));
    return (domain && domain->name() == name) ? domain : nullptr;
}

const IDomain* World::find(std::string_view name) const noexcept
{
    const IDomain* domain = find(DomainId::from_name(name));
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

IDomain* World::create(std::string_view name, const Initializer& init)
{
    assertOwnerThread();
    const DomainId id = DomainId::from_name(name);
    if (!id.valid()) {
        throw WorldError("World::create: domain name must not be empty");
    }
    if (const IDomain* existing = find(id)) {
        ensure_same_name(*existing, name);
        return nullptr;
    }
    return insert(id, std::string(name), init);
}

IDomain* World::require(std::string_view name, const Initializer& init)
{
    assertOwnerThread();
    const DomainId id = DomainId::from_name(name);
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

Status World::destroy(std::string_view name)
{
    if (const auto* domain = find(name))
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

    Domain& domain   = *it->second;
    domain.m_closing = true; // mark busy
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
    return add_hook(m_hooks, &Hooks::created, std::move(callback));
}

Connection World::on_domain_destroying(DomainCallback callback)
{
    return add_hook(m_hooks, &Hooks::destroying, std::move(callback));
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
