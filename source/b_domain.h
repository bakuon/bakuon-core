#pragma once

#include <string>
#include <unordered_map>

#include <bakuon/core/domain.h>

#include "b_entity.h"

// 只给引擎编译单元包含。插件禁止包含本头。

namespace bakuon::core {

// 引擎自己的 C++ 组件继续走 registry.emplace<T> / view<T>。
// 插件组件不要用这条路径，它们的真实类型在引擎编译期不可见。
[[nodiscard]] Registry& registry_of(IDomain& domain);
[[nodiscard]] const Registry& registry_of(const IDomain& domain);

// 引擎内部的类型安全事件总线（entt::dispatcher）。插件禁止使用本路径。
[[nodiscard]] Dispatcher& dispatcher_of(IDomain& domain);
[[nodiscard]] const Dispatcher& dispatcher_of(const IDomain& domain);

/// 重命名为 Domain
class Domain final : public IDomain
{
public:
    ~Domain();

    Domain(const Domain&)            = delete;
    Domain& operator=(const Domain&) = delete;
    Domain(Domain&&)                 = delete;
    Domain& operator=(Domain&&)      = delete;

    [[nodiscard]] std::uint32_t abi_version() const noexcept override { return domain_abi_version; }
    [[nodiscard]] DomainId id() const noexcept override { return m_id; }
    [[nodiscard]] std::string_view name() const noexcept override { return m_name; }
    [[nodiscard]] std::uint64_t serial() const noexcept override { return m_serial; }
    [[nodiscard]] bool closing() const noexcept override { return m_closing; }

    [[nodiscard]] Registry& registry() { return m_registry; }
    [[nodiscard]] const Registry& registry() const { return m_registry; }

    /// 本域专属的 entt::dispatcher。与 registry 同寿，域销毁时订阅一并失效。
    /// 仅引擎编译单元使用；跨 DLL 插件请走 World::events()（IEventBus）。
    [[nodiscard]] Dispatcher& dispatcher() noexcept { return m_dispatcher; }
    [[nodiscard]] const Dispatcher& dispatcher() const noexcept { return m_dispatcher; }

    [[nodiscard]] Entity create() override { return m_registry.create(); }
    bool destroy(Entity entity) override;
    [[nodiscard]] bool valid(Entity entity) const override { return m_registry.valid(entity); }
    [[nodiscard]] ComponentId register_component(std::string_view name, std::uint32_t size,
                                                 std::uint32_t align) override;
    bool unregister_component(ComponentId id) override;
    bool emplace(Entity entity, ComponentId id, const void* bytes, void** out) override;
    bool replace(Entity entity, ComponentId id, const void* bytes) override;
    bool remove(Entity entity, ComponentId id) override;

    [[nodiscard]] void* get(Entity entity, ComponentId id) override;
    [[nodiscard]] const void* get(Entity entity, ComponentId id) const override;
    [[nodiscard]] bool has(Entity entity, ComponentId id) const override;
    [[nodiscard]] std::uint32_t count(ComponentId id) const override;
    void each(const ComponentId* ids, std::uint32_t n, void* user, EachFn fn) override;

    [[nodiscard]] const char* last_error() const override { return m_error.c_str(); }

private:
    /**
    引擎为每个名字建一份 basic_registry<Entity> 里的 storage<PluginSlot>，POD
    字节放在槽位单独持有的内存里。registry.destroy 会拆掉所有池，槽位析构函数释放这些字节。
    */
    struct PluginSlot
    {
        void* data          = nullptr;
        std::uint32_t size  = 0;
        std::uint32_t align = 1;

        PluginSlot() = default;
        PluginSlot(void* data, std::uint32_t size, std::uint32_t align) noexcept
            : data(data)
            , size(size)
            , align(align)
        {
        }

        PluginSlot(const PluginSlot&)            = delete;
        PluginSlot& operator=(const PluginSlot&) = delete;

        PluginSlot(PluginSlot&& other) noexcept
            : data(other.data)
            , size(other.size)
            , align(other.align)
        {
            other.data = nullptr;
        }

        PluginSlot& operator=(PluginSlot&& other) noexcept
        {
            if (this != &other) {
                release();
                data       = other.data;
                size       = other.size;
                align      = other.align;
                other.data = nullptr;
            }
            return *this;
        }

        ~PluginSlot() { release(); }

        void release() noexcept
        {
            if (data == nullptr) {
                return;
            }
            if (align > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
                ::operator delete(data, std::align_val_t{static_cast<std::size_t>(align)});
            } else {
                ::operator delete(data);
            }
            data = nullptr;
        }
    };

    using Storage = Registry::storage_for_type<PluginSlot>;

    struct Record
    {
        std::string name;
        std::uint32_t size  = 0;
        std::uint32_t align = 1;
        Storage* pool       = nullptr;
    };

    [[nodiscard]] Record* find(ComponentId id)
    {
        const auto it = m_records.find(id);
        return it == m_records.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const Record* find(ComponentId id) const
    {
        const auto it = m_records.find(id);
        return it == m_records.end() ? nullptr : &it->second;
    }

    ComponentId fail(const char* message)
    {
        m_error = message;
        return 0;
    }

private:
    friend class World;
    Domain(DomainId id, std::string name, std::uint64_t serial);

private:
    DomainId m_id;
    std::string m_name;
    std::uint64_t m_serial;
    bool m_closing = false;

    // 声明顺序 = 构造顺序；析构时 m_records 先于 m_registry 销毁。
    // 不要为了"看起来整齐"调整这两个成员的相对顺序。
    Registry m_registry;
    Dispatcher m_dispatcher;
    std::unordered_map<ComponentId, Record> m_records;
    std::unordered_map<std::string, ComponentId> m_names;
    std::string m_error;
};

} // namespace bakuon::core
