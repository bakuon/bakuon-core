cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(
    tests
    VERSION 1.0.0
    LANGUAGES CXX
)

# ============================================================================
# 公共头守卫（规则 1/2：include/bakuon/core/*.h 不含 entt，且对消费者自包含）
#
# 1) 编译检查：只链接 SHARED 的 bakuon::core（消费者视角：只有 include/ 与导出头，
#    没有 source/、没有 EnTT），把每个公共头各 #include 一遍。任何泄漏 EnTT 或缺少
#    自身依赖的公共头都会在构建期直接编译失败。
# 2) 文本检查：cmake/check_no_entt.cmake 扫描 entt:: / <entt/ / ENTT_ 等符号。
# ============================================================================
file(
    GLOB_RECURSE _public_headers
    CONFIGURE_DEPENDS
    RELATIVE "${BAKUON_ROOT_DIR}/include"
    "${BAKUON_ROOT_DIR}/include/*.h"
)
set(_public_headers_cpp_content "")
foreach(_header IN LISTS _public_headers)
    string(APPEND _public_headers_cpp_content "#include <${_header}>\n")
endforeach()
string(APPEND _public_headers_cpp_content "\nint bakuon_public_headers_check_anchor = 0;\n")
file(
    CONFIGURE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/public_headers_check.cpp"
    CONTENT "${_public_headers_cpp_content}"
    @ONLY
)
add_library(bakuon_public_headers_check OBJECT "${CMAKE_CURRENT_BINARY_DIR}/public_headers_check.cpp")
target_link_libraries(bakuon_public_headers_check PRIVATE bakuon::core)
bakuon_apply_warnings(bakuon_public_headers_check)

add_test(
    NAME public_headers_no_entt
    COMMAND ${CMAKE_COMMAND} -DPUBLIC_DIR=${BAKUON_ROOT_DIR}/include -P
            ${BAKUON_CMAKE_DIR}/check_no_entt.cmake
)
set_tests_properties(public_headers_no_entt PROPERTIES LABELS "bakuon_all;unit")

# 引入各个子模块测试
add_subdirectory(unit)
# add_subdirectory(manual)

# 为 IDE 组织项目结构
bakuon_apply_targets_folder(${CMAKE_CURRENT_LIST_DIR})
