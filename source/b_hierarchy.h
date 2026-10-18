#pragma once

/**
 * Hierarchy 十字链表实现
 *
 * 在 EnTT 中用 Parent / FirstChild / LastChild / PrevSibling / NextSibling
 * 表达有序树。组件固定大小、无堆分配，适配稀疏集布局与缓存友好访问。
 *
 * 劣势：按索引随机访问第 N 个子节点是 O(N)；GUI 场景通常顺序遍历，可接受。
 * 所有改树操作必须走本命名空间的函数，禁止直接 emplace/remove Hierarchy。
 */

namespace bakuon::core {

} // namespace bakuon::core
