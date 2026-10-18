#pragma once

/**
 * 选择集：Selected 标签为唯一真相源；Selection 类额外维护用户选择顺序与 primary。
 *
 * 不做成"当前选择存在 registry.ctx() 里的一份 vector"——那样会和标签组件
 * 变成双份事实来源。选择就是"带 Selected 的实体"，查询走 each<Selected>()。
 * Selection 的 ordered() 只是顺序视图，任何写操作必须同步标签。
 */

namespace bakuon::core {

} // namespace bakuon::core
