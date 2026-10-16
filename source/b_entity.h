#pragma once

#include <bakuon/core/entity.h>

#include <entt/entt.hpp>

/*
// ---------- 关键：为 Entity 提供 entt_traits ----------
struct EntityTraits
{
    using value_type   = bakuon::core::Entity; // 真正的 entity 类型
    using entity_type  = std::uint32_t;        // 底层数值类型
    using version_type = std::uint16_t;        // 版本类型（默认 12 位足够）

    // 默认 EnTT 布局（与 entt::entity 完全一致）：
    // 低 20 位 entity id，高 12 位 version
    static constexpr entity_type entity_mask  = 0xFFFFF; // 20 bits
    static constexpr entity_type version_mask = 0xFFF;   // 12 bits
};

/// EnTT 特性特化：告知 EnTT 如何使用 Entity
/// 帮助实现 to_integral、to_entity、to_version、construct、combine 等所有必要函数
template<>
struct entt::entt_traits<bakuon::core::Entity> : entt::basic_entt_traits<EntityTraits>
{
    // 自定义稀疏集合页大小
    static constexpr std::size_t page_size = 4096;
};
*/

namespace bakuon::core {

using Registry          = entt::basic_registry<Entity>;
using Snapshot          = entt::basic_snapshot<Registry>;
using SnapshotLoader    = entt::basic_snapshot_loader<Registry>;
using ContinuousLoader  = entt::basic_continuous_loader<Registry>;
using Dispatcher        = entt::dispatcher;
using SinkConnection    = entt::connection;
using ScopedConnection  = entt::scoped_connection;
using EntityHandle      = entt::basic_handle<Registry>;
using ConstEntityHandle = entt::basic_handle<const Registry>;

// 插件里写死的 null 位型一旦和这版 EnTT 不一致，这里直接编译失败。
static_assert(entt::entt_traits<Entity>::entity_mask == Entity::index_mask);
// 尝试匹配参数列表 "(const entt::null_t)" 失败
// static_assert(entt::entt_traits<Entity>::to_integral(Entity{entt::null}) == Entity::null_raw);
static_assert(entt::entt_traits<Entity>::to_integral(Entity{
                  static_cast<Entity::entity_type>(entt::null)})
              == Entity::null_raw);
} // namespace bakuon::core
