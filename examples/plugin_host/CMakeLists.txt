cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(
    plugin_host
    VERSION 1.0.0
    LANGUAGES CXX
)

add_executable(plugin_host main.cpp)

target_link_libraries(plugin_host PRIVATE bakuon::bakuon)
target_compile_features(plugin_host PRIVATE cxx_std_20)

# 非 Windows 需要 libdl
if(UNIX AND NOT APPLE)
    target_link_libraries(plugin_host PRIVATE dl)
endif()

# 编译期注入插件产物路径，便于直接运行
if(TARGET plugin_movement)
    target_compile_definitions(
        plugin_host PRIVATE BAKUON_EXAMPLE_PLUGIN_PATH="$<TARGET_FILE:plugin_movement>"
    )
    add_dependencies(plugin_host plugin_movement)
endif()
