#pragma once

#include <string_view>

namespace bakuon {

/**
 * @brief bakuon 模块版本号（人类可读），供诊断日志/关于对话框使用。
 *
 * @note 存在的意义有两个：
 *  1. bakuon_add_module() 要求每个模块至少有一个 .cpp（静态库不能是纯头文件集合，
 *     见 cmake/BakuonUtils.cmake 的 FATAL_ERROR 检查）；
 *  2. 未来出现更多非模板的辅助函数时，这里是自然的落脚点，不必因为
 *     "只有模板代码"就临时找个地方硬塞一个 .cpp。
 */
[[nodiscard]] std::string_view versionString() noexcept;

[[nodiscard]] std::size_t stringHashed(std::string_view str) noexcept;

} // namespace bakuon
