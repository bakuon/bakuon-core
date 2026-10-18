cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# 示例插件：产出 MODULE（.so / .dll / .dylib），只能被动态加载，不能被静态链接进宿主。
bakuon_add_plugin(
    NAME
    plugin_movement
    PATH
    ${CMAKE_CURRENT_SOURCE_DIR}
    CATEGORY
    example
    DEPENDS
    bakuon::bakuon
)

# 编译插件时打开导出宏（Windows dllexport）
target_compile_definitions(plugin_movement PRIVATE BAKUON_PLUGIN_BUILD)
