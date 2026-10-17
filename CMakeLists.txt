cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(
    bakuon
    VERSION 1.0.0
    LANGUAGES CXX
)

# 记录仓库真正的根目录。一旦 bakuon 被上层工程通过 add_subdirectory() 集成， CMAKE_SOURCE_DIR
# 会指向上层工程的根目录而非本仓库根目录， 因此所有子模块的 PUBLIC include 路径都必须引用 BAKUON_ROOT_DIR，而不是
# CMAKE_SOURCE_DIR。
set(BAKUON_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(BAKUON_CMAKE_DIR "${BAKUON_ROOT_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${BAKUON_CMAKE_DIR}")

# 加载 bakuon 自身的 cmake 模块（函数 / 第三方模块 / 包管理）
include(BakuonCMake)

# ============================================================================
# 顶层可选项 当 bakuon 被作为子目录集成进其他工程时（PROJECT_IS_TOP_LEVEL 为 FALSE）， 默认关闭 tests /
# examples。
# ============================================================================
option(BAKUON_BUILD_TESTS "构建 bakuon 单元测试" ${PROJECT_IS_TOP_LEVEL})
option(BAKUON_BUILD_EXAMPLES "构建 bakuon 示例程序" ${PROJECT_IS_TOP_LEVEL})
option(BAKUON_ENABLE_WARNINGS "启用 -Wall -Wextra / W4 等编译警告" ON)
option(BAKUON_WARNINGS_AS_ERRORS "将编译警告视为错误" OFF)
option(BAKUON_INSTALL_SDK "安装头文件与导出 target(打包方案未定，默认关闭)" OFF)

add_definitions(-DBAKUON_BUILD_TESTS -DBAKUON_BUILD_EXAMPLES -DBAKUON_BUILD_SANDBOX_RUNTIME)

# ----------------------------------------------------------------------------
# C++ 标准通过 target_compile_features(cxx_std_20) 按目标下发（见
# cmake/BakuonUtils.cmake），刻意不设置全局 CMAKE_CXX_STANDARD， 以免连带影响 vendor 下第三方依赖（如
# googletest）的编译标准。
# ----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

# 框架内所有静态库都以位置无关代码编译， 以便未来被链接进以共享库/插件形式加载的模块（plugins/ 下的运行时插件）。
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ============================================================================
# 配置 ID_TYPE
#
# # 引入该库编译时生成的配置文件（可以通过环境变量或 CMake 变量传入路径）
# 例如：cmake -DBAKUON_BUILD_DIR=/path/to/bakuon/build ..
# include("${DBAKUON_BUILD_DIR}/BakuonConfig.cmake" OPTIONAL RESULT_VARIABLE BAKUON_CONFIG_FOUND)
#
# if(NOT BAKUON_CONFIG_FOUND)
#     message(FATAL_ERROR "找不到核心库配置文件！请指定 -DBAKUON_BUILD_BIT")
# endif()
#
# # 【严格安全校验】检查插件的位数是否与宿主完全一致
# if(NOT CMAKE_SIZEOF_VOID_P EQUAL BAKUON_BUILD_BIT)
#     message(FATAL_ERROR "架构不匹配！核心库是 ${BAKUON_BUILD_BIT} 位，而插件正尝试编译为 ${CMAKE_SIZEOF_VOID_P} 位！")
# endif()
# ============================================================================
# 平台检测
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BAKUON_ID_TYPE "std::uint64_t")
else()
    set(BAKUON_ID_TYPE "std::uint32_t")
endif()

# 给 EnTT 注入宏
add_compile_definitions(ENTT_ID_TYPE=${BAKUON_ID_TYPE})

# 生成供插件使用的 CMake 配置文件
file(
    WRITE "${CMAKE_BINARY_DIR}/BakuonConfig.cmake"
    "
    # 这是由核心库工程自动生成的配置文件，请勿手动修改
    set(BAKUON_BUILD_BIT ${CMAKE_SIZEOF_VOID_P})
    set(BAKUON_ID_TYPE_STR \"${BAKUON_ID_TYPE}\")
    "
)

# ============================================================================
# 统一产物输出目录
#
# 所有可执行文件和"运行期"产物统一收拢到同一个 bin/ 目录：
#
#   - Windows：SHARED 库的 .dll 和 .exe 都由 RUNTIME_OUTPUT_DIRECTORY 控制，
#     统一到 bin/ 后，Windows 默认 DLL 搜索顺序里的"应用程序所在目录"就能直接命中，
#     不需要额外拷贝或修改 PATH。
#   - Linux/macOS：.so 由 LIBRARY_OUTPUT_DIRECTORY 控制。可执行文件启动时如何定位
#     "构建目录里的" .so 是 CMake 的默认行为——只要没有显式设置 CMAKE_SKIP_BUILD_RPATH，
#     CMake 会自动为链接了该 .so 的目标写入指向其构建位置的 RPATH，构建树内直接运行
#     天然可用，这里不需要再手动摆弄 RPATH（真正 install() 打包时的 RPATH 策略留给
#     BAKUON_INSTALL_SDK 相关 TODO，见 cmake/BakuonUtils.cmake）。
# ============================================================================
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
if(NOT CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

# add_compile_options("/utf-8") 通常不会自动作用于通过 add_subdirectory
# 或 CPM 引入的第三方 target（因为编译选项有作用域限制）。
# 使用 link_libraries 强制将 /utf-8 注入到所有子目标，或者通过 CMake 全局变量：
if(MSVC)
    # 强制当前目标程序在运行时将进程代码页（ACP）视为 UTF-8
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/utf-8>")

    # 这会强制后续所有加入的项目（包括 CPM 引入的）在 MSVC 下都带上 /utf-8
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /utf-8")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
    # 同时针对 Windows 10/11 的 Manifest 注入
    # ...
endif()

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
