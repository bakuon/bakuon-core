#pragma once

#include <cstdint>
#include <functional>

/**
 *************************************************************
 * @brief entity.h — 插件唯一需要包含的头，禁止 include <entt/...>
 * Entity 保持标准布局、可平凡复制，按值穿过 DLL 是安全的。不要给它虚函数，
 * 也不要在这个头里前向声明任何 entt:: 类型。
 *
 * @code
 * Entity entity = null;
 * if (entity == null || entity.is_null()) { ///处理无效/// }
 *
 * Entity zero{};          // 合法的 0 号实体，不是 null
 *
 * @endcode
 *
 * @details Entity zero{}; 为什么默认是 0 号位而不是 null：
 * 1. registry.create() 发出的第一个标识符就是索引 0、版本 0，位型是 0x00000000。entt::null
 * 占的是索引段全 1（0x000FFFFF），再配上版本段全 1，整体才是 0xFFFFFFFF。官方约定就是
 * entt::entity{} != entt::null，而且有测试钉死这一点。 索引段全 1 不是随便挑的哨兵。
 * sparse set 用索引直接算页号，entity_mask 算出来的页永远落在已分配范围之外，所以 valid(null)
 * 不用单独判断“是不是 0”就会失败。0 做不到这一点：它就是第 0 页的第 0 槽。版本段另有一个保留值
 * tombstone（版本位全 1），用来标记空洞；版本 0 因此仍然可用。
 *
 * 2. 空的 sparse 页是用 uninitialized_fill(..., null) 显式填成 entt::null 的，不是靠
 * Entity{}。标识符则走 Entity{raw} 这条构造函数。默认构造只是整数的零初始化，EnTT 自己的 enum class
 * entity 也是如此。 如果把默认构造改成 m_raw{null_raw}，会出现两个问题：
 *
 * - 这个构造函数不再是平凡的。memset(0)、静态存储、calloc 仍然得到 0
 * 号实体，只有真正调用构造函数才得到 null。同一种“没写初始值”会变成两种实体。
 * - 插件里 Entity parent{} 和引擎里 entt::entity{} 含义不同，以后按整数互转会对不上。
 *
 * @warning 成员忘记初始化时，应写成 Entity parent = null;，不要让 Entity{} 兼任 null。
 *
 * @note 不需要再特化 entt::entt_traits<Entity>。类里已经有公开的
 * entity_type，EnTT 会自己生成 to_integral、to_entity、construct。
 * 再特化一层反而会和这份自动 traits 冲突。
 * EnTT 对这个类只有两个硬性要求，上面的定义都满足了：
 * 1. to_integral -> static_cast<std::uint32_t>(entity)
 * 2. construct -> Entity{uint32_t}
 *
 * @note entt::registry 是 basic_registry<entt::entity>，换不了实体类型。
 * 内部自己起别名，这个头不能被插件包含：
 * @code
 * // Registry.hpp — 仅内部
 * #include <bakuon/core/entity.h>
 * #include <entt/entity/registry.hpp>
 *
 * using Registry = entt::basic_registry<Entity>;
 *
 * // 插件里写死的 null 位型一旦和这版 EnTT 不一致，这里直接编译失败。
 * static_assert(entt::entt_traits<Entity>::entity_mask == Entity::index_mask);
 * static_assert(entt::entt_traits<Entity>::to_integral(Entity{entt::null})
 *               == Entity::null_raw);
 * @endcode
 *
 *边界：
 * - 插件 API 只传 Entity 和你自己的组件 POD。不要在插件头里出现
 *   entt::registry、entt::view、entt::entity、entt::null。
 * - 销毁、查询、挂组件都留在内部，例如 bool alive(Entity)、void destroy(Entity)。
 * - == null 忽略版本；Entity == Entity 比较整个 32 位。不要把 is_null() 写成 value() == 0。
 * - 若改用 std::uint64_t 作为 entity_type，EnTT 的掩码是低 32 位索引、高 32 位版本，null 仍是全
 *   1。index_mask / null_raw 和内部的 static_assert 要一起改。
 *
 *************************************************************
 */

namespace bakuon::core {

class Entity final
{
public:
    // 名字必须是 entity_type，且必须 public。
    // EnTT 靠它识别该类，并套用 uint32_t 的 20/12 位布局。
    using entity_type = std::uint32_t;

    static constexpr entity_type index_mask = 0x000FFFFFu;
    static constexpr entity_type null_raw   = 0xFFFFFFFFu;

    constexpr Entity() noexcept = default;

    // construct() 写的是 Entity{raw}，explicit 构造即可。
    explicit constexpr Entity(entity_type raw) noexcept
        : m_raw(raw)
    {
    }

    [[nodiscard]] constexpr entity_type value() const noexcept { return m_raw; }
    [[nodiscard]] constexpr entity_type index() const noexcept { return m_raw & index_mask; }

    // 与 entt::null 同一语义：只看索引段，不看版本。
    [[nodiscard]] constexpr bool isNull() const noexcept { return index() == index_mask; }
    [[nodiscard]] static constexpr Entity null() noexcept { return Entity{null_raw}; }

    // to_integral() 使用 static_cast，explicit 转换运算符即可。
    [[nodiscard]] explicit constexpr operator entity_type() const noexcept { return m_raw; }

    // 两个实体之间是全字比较，版本不同就是不同实体。
    friend constexpr bool operator==(Entity a, Entity b) noexcept { return a.m_raw == b.m_raw; }
    friend constexpr bool operator!=(Entity a, Entity b) noexcept { return !(a == b); }

private:
    // EnTT 官方默认约定就是 entt::entity{} != entt::null
    entity_type m_raw{};
};

// 插件侧的 null。只比较索引段，版本不参与，和 entt::null 的比较规则一致。
// Entity{} 是 0 号实体，不是 null。
struct Null
{
    constexpr operator Entity() const noexcept { return Entity::null(); }

    friend constexpr bool operator==(Null, Entity entity) noexcept { return entity.isNull(); }
    friend constexpr bool operator==(Entity entity, Null null) noexcept { return null == entity; }
    friend constexpr bool operator!=(Null null, Entity entity) noexcept
    {
        return !(null == entity);
    }
    friend constexpr bool operator!=(Entity entity, Null null) noexcept
    {
        return !(null == entity);
    }
};

inline constexpr Null null{};

} // namespace bakuon::core

template<>
struct std::hash<bakuon::core::Entity>
{
    [[nodiscard]] std::size_t operator()(bakuon::core::Entity entity) const noexcept
    {
        return static_cast<std::size_t>(entity.value());
    }
};
