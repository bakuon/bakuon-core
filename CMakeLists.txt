cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(
    bakuon-core
    VERSION 0.1.0
    LANGUAGES CXX
)

# 记录仓库真正的根目录。一旦 bakuon 被上层工程通过 add_subdirectory() 集成， CMAKE_SOURCE_DIR
# 会指向上层工程的根目录而非本仓库根目录， 因此所有子模块的 PUBLIC include 路径都必须引用
# BAKUON_CORE_ROOT_DIR 而不是 CMAKE_SOURCE_DIR。
set(BAKUON_CORE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(BAKUON_CORE_CMAKE_DIR "${BAKUON_CORE_ROOT_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${BAKUON_CORE_CMAKE_DIR}")

# 加载 bakuon 自身的 cmake 模块（函数 / 第三方模块 / 包管理）
include(BakuonCMake)

# ============================================================================
# 顶层可选项 当被作为子目录集成进其他工程时（PROJECT_IS_TOP_LEVEL 为 FALSE）
# 默认关闭 tests / examples。
# ============================================================================
option(BAKUON_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
option(BAKUON_BUILD_EXAMPLES "Build examples" ${PROJECT_IS_TOP_LEVEL})
option(BAKUON_ENABLE_WARNINGS "Enable -Wall -Wextra / W4 compilation warnings" ON)
option(BAKUON_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# ----------------------------------------------------------------------------
# C++ 标准通过 target_compile_features(cxx_std_20) 按目标下发（见
# cmake/BakuonUtils.cmake），刻意不设置全局 CMAKE_CXX_STANDARD， 以免连带影响 vendor 下第三方依赖（如
# googletest）的编译标准。
# ----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    # 强制当前目标程序在运行时将进程代码页（ACP）视为 UTF-8
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/utf-8>")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================================
# 子目录
# ============================================================================
add_subdirectory(vendor)
add_subdirectory(source)

if(BAKUON_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BAKUON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
