cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(
    core
    VERSION 1.0.0
    LANGUAGES CXX
)

# ----------------------------------------------------------------------------
# 后端依赖（仅内部可见）：EnTT + 必须在「core 与白盒测试」之间保持一致的编译宏。
# 不起 bakuon:: 别名，也不会出现在 SHARED 库的接口里（PRIVATE 链接）。
# ----------------------------------------------------------------------------
add_library(bakuon_core_backend INTERFACE)
target_link_libraries(bakuon_core_backend INTERFACE EnTT::EnTT)
target_compile_definitions(bakuon_core_backend INTERFACE ENTT_ID_TYPE=${BAKUON_ID_TYPE})

# 白盒测试用的 STATIC 变体只在构建测试时才创建（否则核心库不会被多编译一遍）。
set(_bakuon_core_with_static "")
if(BAKUON_BUILD_TESTS)
    set(_bakuon_core_with_static WITH_STATIC)
endif()

# 产出：
#   bakuon::core         SHARED  插件 / 主程序 / 示例链接；只暴露 include/ 与导出头
#   bakuon::core_static  STATIC  仅白盒测试链接；暴露 source/，导出宏为空
bakuon_add_module(
    NAME
    ${PROJECT_NAME}
    PATH
    ${CMAKE_CURRENT_SOURCE_DIR}
    EXPORT_MACRO
    "BAKUON_CORE_API" # 头文件中使用的导出宏
    DEPENDS
    bakuon_core_backend
    INCLUDE_DIRS
    ${BAKUON_ROOT_DIR}/source
    # 面向消费者的稳定门面头文件根目录：include/bakuon/*.h
    PUBLIC_INCLUDE_DIRS
    ${BAKUON_ROOT_DIR}/include
    ${_bakuon_core_with_static}
)

bakuon_apply_targets_folder(${CMAKE_CURRENT_LIST_DIR} ALIAS "core")
