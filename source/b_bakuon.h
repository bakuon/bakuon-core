#pragma once

#include "b_component_registry.h" // IWYU pragma: keep
#include "b_component_storage.h"  // IWYU pragma: keep
#include "b_connection.h"         // IWYU pragma: keep
#include "b_domain.h"             // IWYU pragma: keep
#include "b_entity.h"             // IWYU pragma: keep
#include "b_host.h"               // IWYU pragma: keep
#include "b_module_loader.h"      // IWYU pragma: keep
#include "b_names.h"              // IWYU pragma: keep
#include "b_plugin_host.h"        // IWYU pragma: keep
#include "b_plugin_manager.h"     // IWYU pragma: keep
#include "b_scheduler.h"          // IWYU pragma: keep
#include "b_world.h"              // IWYU pragma: keep

#include <bakuon/core/bakuon.h>

namespace bakuon::core {

/**
 * @brief bakuon 模块版本号（人类可读），供诊断日志/关于对话框使用。
 *
 * @note 存在的意义有两个：
 *  1. bakuon_add_module() 要求每个模块至少有一个 .cpp（静态库不能是纯头文件集合，
 *     见 cmake/BakuonUtils.cmake 的 FATAL_ERROR 检查）；
 *  2. 未来出现更多非模板的辅助函数时，这里是自然的落脚点，不必因为
 *     "只有模板代码"就临时找个地方硬塞一个 .cpp。
 */

// std::string_view versionString() noexcept;
// std::size_t stringHashed(std::string_view str) noexcept;

} // namespace bakuon::core
