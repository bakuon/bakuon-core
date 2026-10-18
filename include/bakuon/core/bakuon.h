#pragma once

/**
 ****************************************************************************
 * bakuon 门面头文件（facade）
 *
 * 保留这一层转发是为了让消费者始终写：
 *   #include "bakuon/bakuon.h"
 * 而不是：
 *   #include "b_bakuon.h"
 * 一旦将来内部实现分层策略发生变化（例如引入非模板的 PIMPL 部分），
 * 只需要调整这一层转发，消费者的 #include 路径不受影响。
 ******************************************************************************
 */

#include <bakuon/core/api.h>

#include <string_view>

namespace bakuon {

BAKUON_CORE_API std::string_view versionString() noexcept;
BAKUON_CORE_API std::size_t stringHashed(std::string_view str) noexcept;

} // namespace bakuon
