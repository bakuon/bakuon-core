#pragma once

/// 复制粘贴是编辑器最基础的交互，且能验证 Reference 设计是否合理

/**
 * 克隆/深拷贝子树
 *
 *
 * entt 本身不提供"复制一个实体及其全部组件，生成一批新 Stable Id"的能力，
 * hierarchy:: 也只有摘除/挂接，没有"整棵子树连带组件一起复制"。
 * 复制/粘贴、预制体实例化都需要它，而且要和 Ref（b_reference.h）联动
 * ——克隆子树内部的相互引用要重定向到新 id，指向子树外部的引用要保持不变。
 */

namespace bakuon::core {

} // namespace bakuon::core
