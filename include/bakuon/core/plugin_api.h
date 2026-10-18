#pragma once

/**
 * plugin_api.h — 插件 DLL 的 C 导出入口约定。
 *
 * 工厂函数必须是 extern "C"，避免 C++ 名字修饰差异。
 * 返回的 IPlugin* 仍是 C++ 虚接口：插件与宿主须使用同一编译器与 ABI 设置
 * （与 IDomain / ISystem 边界一致）。
 *
 * 宿主：只使用符号名字符串 + 函数指针类型，通过 dlsym / GetProcAddress 解析，
 *       不要链接插件 MODULE。
 * 插件：定义 BAKUON_PLUGIN_BUILD 后实现下方导出函数。
 */

#include <bakuon/core/plugin.h>

#if defined(_WIN32) || defined(_WIN64)
#    ifdef BAKUON_PLUGIN_BUILD
#        define BAKUON_PLUGIN_EXPORT __declspec(dllexport)
#    else
#        define BAKUON_PLUGIN_EXPORT
#    endif
#else
#    if defined(BAKUON_PLUGIN_BUILD)
#        define BAKUON_PLUGIN_EXPORT __attribute__((visibility("default")))
#    else
#        define BAKUON_PLUGIN_EXPORT
#    endif
#endif

namespace bakuon::core {

/// 导出符号名（宿主 dlsym / GetProcAddress 使用）
inline constexpr const char* plugin_create_symbol  = "bakuon_create_plugin";
inline constexpr const char* plugin_destroy_symbol = "bakuon_destroy_plugin";

using PluginCreateFn  = IPlugin* (*)();
using PluginDestroyFn = void (*)(IPlugin*);

} // namespace bakuon::core

#if defined(BAKUON_PLUGIN_BUILD)
extern "C" {

BAKUON_PLUGIN_EXPORT bakuon::core::IPlugin* bakuon_create_plugin();
BAKUON_PLUGIN_EXPORT void bakuon_destroy_plugin(bakuon::core::IPlugin* plugin);

}
#endif
