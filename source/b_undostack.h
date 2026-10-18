#pragma once

/**
 * 基于 entt::snapshot / entt::snapshot_loader 的整体式撤销/重做栈
 *
 * "整体二进制"归档器：entt::snapshot/snapshot_loader 只要求
 * 归档对象提供 `operator()(T&)`（读）或 `operator()(const T&)`（写），本类
 * 原样按字节追加/读回，不做任何格式化或压缩
 */

namespace bakuon::core {

} // namespace bakuon::core
