# ============================================================================
# 内部辅助：收集模块目录下的全部 .h / .cpp（不做任何排除）。
# CONFIGURE_DEPENDS：新增/删除源文件时自动触发 cmake 重新配置，
# 早期孵化阶段文件变动频繁，这个便利性优先于极少数场景下的重新配置开销。
# ============================================================================
function(bakuon_glob_module_files MODULE_PATH OUT_HEADERS OUT_SOURCES)
    file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "${MODULE_PATH}/*.h")
    file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${MODULE_PATH}/*.cpp")

    set(${OUT_HEADERS} "${HEADERS}" PARENT_SCOPE)
    set(${OUT_SOURCES} "${SOURCES}" PARENT_SCOPE)
endfunction()

# ============================================================================
# IDE 项目分组
# ============================================================================

# Collect all currently added targets in all subdirectories
# Parameters:
# - _result the list containing all found targets
# - _dir root directory to start looking from
function(_bakuon_get_targets_by_directory _result _dir)
    get_property(
        _subdirs
        DIRECTORY ${_dir}
        PROPERTY SUBDIRECTORIES
    )
    foreach(_subdir IN LISTS _subdirs)
        _bakuon_get_targets_by_directory(${_result} ${_subdir})
    endforeach()

    get_directory_property(_sub_targets DIRECTORY ${_dir} BUILDSYSTEM_TARGETS)
    set(${_result} ${${_result}} ${_sub_targets} PARENT_SCOPE)
endfunction()

function(bakuon_apply_targets_folder _dir)
    cmake_parse_arguments(
        ARG
        ""
        "ALIAS"
        ""
        ${ARGN}
    )

    _bakuon_get_targets_by_directory(ALL_TARGETS ${_dir})

    foreach(target IN LISTS ALL_TARGETS)

        # 获取该 Target 所在的源目录
        get_target_property(TARGET_SOURCE_DIR ${target} SOURCE_DIR)

        # 计算相对路径
        if(TARGET_SOURCE_DIR)
            file(RELATIVE_PATH REL_DIR ${BAKUON_ROOT_DIR} ${TARGET_SOURCE_DIR})

            get_target_property(TARGET_TYPE ${target} TYPE)
            # message(STATUS "[${PROJECT_NAME}] Found target: (${TARGET_TYPE}, ${target}, ${TARGET_SOURCE_DIR})")

            # 排除根目录下的 Target
            if(REL_DIR AND NOT REL_DIR STREQUAL ".")
                # 别名
                if(ARG_ALIAS)
                    set(REL_DIR ${ARG_ALIAS})
                endif()
                # 设置 FOLDER 属性
                set_target_properties(${target} PROPERTIES FOLDER "${REL_DIR}")
            endif()
        endif()
    endforeach()
endfunction()

# ============================================================================
# 内部辅助：目标的公共基础设置（C++ 标准 / 警告开关）
# 统一入口，避免 bakuon_add_module 与 bakuon_add_amalgamated_module 各写一份、逐渐漂移。
# ============================================================================
function(_bakuon_apply_common_target_settings TARGET_NAME)
    # PUBLIC：公共头里已经用了 C++20（domain.h 的 defaulted operator<=>），
    # 所以链接 bakuon::core / bakuon::core_static 的目标必须以 >= C++20 编译。
    target_compile_features(${TARGET_NAME} PUBLIC cxx_std_20)
    set_target_properties(${TARGET_NAME} PROPERTIES CXX_EXTENSIONS OFF)
    bakuon_apply_warnings(${TARGET_NAME})
endfunction()

# ============================================================================
# 公共函数：为目标启用统一的编译警告策略
# 独立导出，方便 standalone / examples / tests 下的可执行目标也复用同一套警告策略。
# ============================================================================
function(bakuon_apply_warnings TARGET_NAME)
    if(NOT BAKUON_ENABLE_WARNINGS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W4 $<$<BOOL:${BAKUON_WARNINGS_AS_ERRORS}>:/WX>)
    else()
        target_compile_options(
            ${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic $<$<BOOL:${BAKUON_WARNINGS_AS_ERRORS}>:-Werror>
        )
    endif()
endfunction()

# ============================================================================
# 公共函数：按需为一个库目标安装头文件 + 导出 target（装进 SDK / devkit 包）
#
# 默认关闭（BAKUON_INSTALL_SDK=OFF）：项目目前还在孵化期，没有正式的发布/打包流程，
# 贸然打开 install() 只会带来一堆没人验证过的坑。这里先把“将来要不要装、装哪些东西”的
# 骨架搭好、语义正确（BUILD_INTERFACE/INSTALL_INTERFACE 已经在 target_include_directories
# 里配好了），真正决定打包方案时只需要把 BAKUON_INSTALL_SDK 打开，
# 并按需在根目录补一份 install(EXPORT ...) + Config.cmake.in（见下方 TODO）。
#
# TODO（不确定，需要产品化打包方案确定后再完善）：
#  - 是否需要生成 bakuonConfig.cmake / bakuonConfigVersion.cmake 供
#    find_package(bakuon) 使用？如果需要，要另外写一份 cmake/bakuonConfig.cmake.in
#    并在根 CMakeLists.txt 里调 configure_package_config_file() + write_basic_package_version_file()。
#  - 版本兼容策略（SameMajorVersion？ExactVersion？）目前完全没有讨论过。
# ============================================================================
function(bakuon_install_module TARGET_NAME PUBLIC_HEADER_DIR)
    if(NOT BAKUON_INSTALL_SDK)
        return()
    endif()

    install(
        TARGETS ${TARGET_NAME}
        EXPORT bakuonTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    if(PUBLIC_HEADER_DIR AND EXISTS "${PUBLIC_HEADER_DIR}")
        install(DIRECTORY "${PUBLIC_HEADER_DIR}/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    endif()
endfunction()

# ============================================================================
# bakuon_add_module —— 注册宿主库（host library），同一份源码产出两个目标：
#
#   bakuon_<NAME>          SHARED  bakuon::<NAME>
#       给「插件 / 主程序 / 示例」用。消费者只能看到 PUBLIC_INCLUDE_DIRS（公共头）和
#       生成的导出头，看不到 INCLUDE_DIRS（私有 source/）和 DEPENDS（EnTT）。
#       编译本库时 DEFINE_SYMBOL（BAKUON_<NAME>_BUILD）生效 -> 导出宏展开为 dllexport；
#       消费者不带该宏 -> 展开为 dllimport。
#
#   bakuon_<NAME>_static   STATIC  bakuon::<NAME>_static   （仅 WITH_STATIC 时创建）
#       给「白盒单元测试」用。测试要直接 #include 私有头、调用未导出的内部符号，
#       这些符号在 DLL 里是 hidden / 没有 dllexport，链 SHARED 必然找不到符号。
#       这里把同一批 .cpp 重新编译一遍，并 PUBLIC 定义 BAKUON_<NAME>_STATIC_DEFINE，
#       导出宏展开为空（无 dllimport/dllexport）；私有 source/ 与后端依赖也是 PUBLIC，
#       传递给测试。
#
# 为什么不再用「一份 OBJECT 对象库同时喂给 SHARED 和 STATIC」：
#   导出宏是编译期决定的（dllexport / dllimport / 空）。SHARED 需要 dllexport，STATIC
#   需要空，同一份 .obj 不可能同时满足，必须按目标各编译一次。代价：开启测试时核心库
#   会编译两遍；BAKUON_BUILD_TESTS=OFF 时不创建 static 目标，没有额外开销。
#
# 参数：
#   NAME                模块名（core -> bakuon_core / bakuon::core）
#   PATH                私有实现目录（递归 GLOB .h/.cpp）
#   PUBLIC_INCLUDE_DIRS 公共头根目录（include/），消费者可见
#   INCLUDE_DIRS        私有头目录（source/），仅本库内部；static 目标 PUBLIC 暴露给白盒测试
#   DEPENDS             实现依赖（EnTT 等），shared: PRIVATE；static: PUBLIC
#   EXPORT_MACRO        导出宏名，默认 BAKUON_<NAME>_API
#   WITH_STATIC         同时创建白盒测试用的 static 目标
# ============================================================================
function(bakuon_add_module)
    cmake_parse_arguments(
        ARG
        "WITH_STATIC"
        "NAME;PATH;EXPORT_MACRO"
        "DEPENDS;INCLUDE_DIRS;PUBLIC_INCLUDE_DIRS"
        ${ARGN}
    )

    foreach(_required NAME PATH PUBLIC_INCLUDE_DIRS)
        if(NOT ARG_${_required})
            message(FATAL_ERROR "bakuon_add_module: ${_required} 是必填参数")
        endif()
    endforeach()

    set(MODULE_NAME ${ARG_NAME})
    set(SHARED_TARGET "bakuon_${MODULE_NAME}")
    set(STATIC_TARGET "bakuon_${MODULE_NAME}_static")
    string(TOUPPER ${MODULE_NAME} UPPER_NAME)

    if(NOT ARG_EXPORT_MACRO)
        set(ARG_EXPORT_MACRO "BAKUON_${UPPER_NAME}_API")
    endif()

    bakuon_glob_module_files("${ARG_PATH}" ALL_HEADERS ALL_SOURCES)
    if(NOT ALL_SOURCES)
        message(FATAL_ERROR "bakuon_add_module(${MODULE_NAME}): 在 ${ARG_PATH} 下没有找到任何 .cpp 文件，"
                            "无法创建库；该模块尚无实现代码前不要 add_subdirectory() 它。"
        )
    endif()

    # 公共头目录：仅在构建树内有效（装 SDK 包的路径等 BAKUON_INSTALL_SDK 落地时再处理）
    set(_public_dirs ${ARG_PUBLIC_INCLUDE_DIRS})
    list(TRANSFORM _public_dirs PREPEND "$<BUILD_INTERFACE:")
    list(TRANSFORM _public_dirs APPEND ">")

    # ── 1. SHARED：给插件 / 主程序 ───────────────────────────────────────────
    add_library(${SHARED_TARGET} SHARED ${ALL_SOURCES} ${ALL_HEADERS})
    add_library(bakuon::${MODULE_NAME} ALIAS ${SHARED_TARGET})

    # DEFINE_SYMBOL 必须在 generate_export_header 之前设置。
    # CMake 会只对「本 SHARED 目标自己的编译」加 -DBAKUON_<NAME>_BUILD，
    # 绝不能再做 target_compile_definitions(PUBLIC BAKUON_<NAME>_BUILD)：那会泄漏给消费者，
    # 让插件/主程序也把导入符号当成 dllexport。
    set_target_properties(${SHARED_TARGET} PROPERTIES DEFINE_SYMBOL BAKUON_${UPPER_NAME}_BUILD)

    # 导出头是公共头 api.h 所依赖的生成文件：api.h 里写的是 #include "core/b_core_export.h"，
    # 因此目录布局必须保持为 <EXPORT_DIR>/<NAME>/b_<NAME>_export.h。
    set(EXPORT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_include")
    set(EXPORT_HEADER "${EXPORT_DIR}/${MODULE_NAME}/b_${MODULE_NAME}_export.h")
    generate_export_header(
        ${SHARED_TARGET}
        BASE_NAME
        BAKUON_${UPPER_NAME}
        EXPORT_MACRO_NAME
        ${ARG_EXPORT_MACRO}
        NO_EXPORT_MACRO_NAME
        BAKUON_${UPPER_NAME}_NO_EXPORT
        DEPRECATED_MACRO_NAME
        BAKUON_${UPPER_NAME}_DEPRECATED
        STATIC_DEFINE
        BAKUON_${UPPER_NAME}_STATIC_DEFINE
        EXPORT_FILE_NAME
        "${EXPORT_HEADER}"
    )

    target_include_directories(
        ${SHARED_TARGET}
        PUBLIC ${_public_dirs} "$<BUILD_INTERFACE:${EXPORT_DIR}>"
        PRIVATE ${ARG_INCLUDE_DIRS}
    )
    # 实现依赖（EnTT 等）PRIVATE：不传递给消费者，保证插件/主程序编译期根本看不到 EnTT。
    target_link_libraries(${SHARED_TARGET} PRIVATE ${ARG_DEPENDS} ${CMAKE_DL_LIBS})

    set_target_properties(
        ${SHARED_TARGET}
        PROPERTIES OUTPUT_NAME ${SHARED_TARGET}
                   CXX_VISIBILITY_PRESET hidden
                   VISIBILITY_INLINES_HIDDEN ON
                   # SOVERSION 用于 Linux/macOS 的 SONAME；早期版本号还会频繁变动，
                   # 先固定为主版本号。
                   SOVERSION ${PROJECT_VERSION_MAJOR}
    )
    _bakuon_apply_common_target_settings(${SHARED_TARGET})
    bakuon_install_module(${SHARED_TARGET} "${ARG_PUBLIC_INCLUDE_DIRS}")

    message(STATUS "[Build] ${SHARED_TARGET} SHARED (${CMAKE_CURRENT_LIST_FILE})")

    # ── 2. STATIC：给白盒单元测试 ────────────────────────────────────────────
    if(ARG_WITH_STATIC)
        add_library(${STATIC_TARGET} STATIC ${ALL_SOURCES} ${ALL_HEADERS})
        add_library(bakuon::${MODULE_NAME}_static ALIAS ${STATIC_TARGET})

        # 导出宏展开为空；PUBLIC：所有链接本目标的测试也必须带上，否则测试 TU 里会
        # 重新出现 dllimport，与 static 库里的无修饰符号对不上。
        target_compile_definitions(${STATIC_TARGET} PUBLIC BAKUON_${UPPER_NAME}_STATIC_DEFINE)

        # 白盒：私有头目录和实现依赖都是 PUBLIC，测试可以直接 #include "b_xxx.h"。
        target_include_directories(
            ${STATIC_TARGET} PUBLIC ${ARG_PUBLIC_INCLUDE_DIRS} "${EXPORT_DIR}" ${ARG_INCLUDE_DIRS}
        )
        target_link_libraries(${STATIC_TARGET} PUBLIC ${ARG_DEPENDS} ${CMAKE_DL_LIBS})

        # 与 SHARED 的 import library（Windows 下 bakuon_<NAME>.lib）区分开，避免重名。
        set_target_properties(${STATIC_TARGET} PROPERTIES OUTPUT_NAME ${STATIC_TARGET})
        _bakuon_apply_common_target_settings(${STATIC_TARGET})

        message(STATUS "[Build] ${STATIC_TARGET} STATIC, white-box tests only (${CMAKE_CURRENT_LIST_FILE})")
    endif()
endfunction()

# ============================================================================
# 定义聚合模块注册函数（Unity Build）
# 单一编译单元换取更快的全量构建速度；同时生成一个 EXCLUDE_FROM_ALL 的
# OBJECT IDE 目标，只为让各子 .cpp 在 compile_commands.json / clangd 中有独立的编译条目，
# 不参与实际链接产物。
# ============================================================================
function(bakuon_add_amalgamated_module)
    cmake_parse_arguments(
        ARG
        ""
        "NAME;PATH"
        "DEPENDS;INCLUDE_DIRS"
        ${ARGN}
    )

    set(MODULE_NAME ${ARG_NAME})
    set(MODULE_PATH ${ARG_PATH})
    set(AMALGAM_H "${MODULE_PATH}/${MODULE_NAME}.h")
    set(AMALGAM_CPP "${MODULE_PATH}/${MODULE_NAME}.cpp")

    bakuon_glob_module_files("${MODULE_PATH}" ALL_HEADERS ALL_SOURCES)

    # Unity Build 下 MODULE_NAME.h / MODULE_NAME.cpp 是硬性约定的聚合入口，必须存在，
    # 因此从常规列表中剔除，分别按“唯一公共头”和“唯一编译单元”特殊处理。
    list(REMOVE_ITEM ALL_SOURCES "${AMALGAM_CPP}")
    list(REMOVE_ITEM ALL_HEADERS "${AMALGAM_H}")

    # ── 1. 真正的构建目标（Unity Build）────────────────
    add_library(${MODULE_NAME} STATIC)
    add_library(bakuon::${MODULE_NAME} ALIAS ${MODULE_NAME})

    target_sources(
        ${MODULE_NAME} PRIVATE ${AMALGAM_CPP} # 唯一编译单元
                               ${ALL_HEADERS} # 头文件加入 IDE 项目树（不编译）
    )

    # 子 .cpp 加入项目树但不编译
    foreach(sub_src ${ALL_SOURCES})
        target_sources(${MODULE_NAME} PRIVATE ${sub_src})
        set_source_files_properties(${sub_src} PROPERTIES HEADER_FILE_ONLY TRUE)
    endforeach()

    target_include_directories(
        ${MODULE_NAME}
        PUBLIC $<BUILD_INTERFACE:${BAKUON_ROOT_DIR}/source> # 外部用 <MODULE/${MODULE_NAME}.h>
        PRIVATE ${ARG_INCLUDE_DIRS}
    )

    if(ARG_DEPENDS)
        target_link_libraries(${MODULE_NAME} PUBLIC ${ARG_DEPENDS})
    endif()

    _bakuon_apply_common_target_settings(${MODULE_NAME})

    # ── 2. IDE 专用 OBJECT 目标（生成 compile_commands 条目）──
    #    EXCLUDE_FROM_ALL：不参与默认构建，但 CMake 仍会为其生成编译命令
    if(CMAKE_EXPORT_COMPILE_COMMANDS)
        set(IDE_TARGET "${MODULE_NAME}_ide")

        add_library(${IDE_TARGET} OBJECT EXCLUDE_FROM_ALL)

        target_sources(${IDE_TARGET} PRIVATE ${ALL_SOURCES})

        # 关键：通过 -include 强制注入聚合头，模拟 unity build 的上下文
        target_compile_options(${IDE_TARGET} PRIVATE "SHELL:-include ${AMALGAM_H}")

        # 复制与主目标相同的 include 路径
        target_include_directories(${IDE_TARGET} PRIVATE ${BAKUON_ROOT_DIR}/source ${ARG_INCLUDE_DIRS})

        if(ARG_DEPENDS)
            target_link_libraries(${IDE_TARGET} PRIVATE ${ARG_DEPENDS})
        endif()

        message(STATUS "[IDE]  ${IDE_TARGET} compile_commands entries generated")
    endif()

    message(STATUS "[Build] ${MODULE_NAME} (unity build, ${CMAKE_CURRENT_LIST_FILE})")
endfunction()

# ============================================================================
# 注册一个运行时插件（plugins/ 目录下的具体插件实现，例如 plugins/gui/example）
#
# 与 bakuon_add_module()/bakuon_add_amalgamated_module() 的本质区别：
#   - bakuon_add_module 系列产出 STATIC 库，给别的目标 target_link_libraries() 静态链接；
#   - bakuon_add_plugin 产出 MODULE 库（不能被链接，只能被 QPluginLoader/dlopen 在运行时加载），
#     对应 source/gui/b_plugin.h 里 Plugin 接口 + Q_DECLARE_INTERFACE 描述的插件架构。
#
# 调用方需要自行在其 CMakeLists.txt 里于调用本函数之前设置：
#   set(CMAKE_AUTOMOC ON)   # 插件类通常带 Q_OBJECT / Q_PLUGIN_METADATA，需要 moc
#
# 参数：
#   NAME            插件目标名，同时也是输出文件名（不含平台前后缀，见下方 PREFIX 设置）
#   PATH            插件实现代码所在目录，递归 GLOB 其下 .h / .cpp
#   CATEGORY        插件分类子目录（如 gui / core），决定输出到
#                   ${CMAKE_BINARY_DIR}/plugins/<CATEGORY>/ 下；不传则归入 "misc"
#   DEPENDS         除 bakuon::core 以外该插件还需要链接的库（bakuon::core 自动链接）
#   INCLUDE_DIRS    额外的 PRIVATE 头文件搜索路径
#   METADATA        可选，插件 JSON 元数据文件路径（对应 b_plugin.h 顶部注释里的
#                   MetaData 示例格式）；若提供，会在插件构建后原样拷贝一份到
#                   输出目录旁（具体是否还需要这份旁置 json，见下方 TODO 2）
#
# 用法示例（plugins/gui/example/CMakeLists.txt）：
#   set(CMAKE_AUTOMOC ON)
#   bakuon_add_plugin(
#       NAME example_plugin
#       PATH ${CMAKE_CURRENT_SOURCE_DIR}
#       CATEGORY gui
#       DEPENDS Qt6::Core
#       METADATA ${CMAKE_CURRENT_SOURCE_DIR}/example_plugin.json)
#
# ----------------------------------------------------------------------------
# TODO（以下几点目前还没有最终拍板，先按最保守的方式实现，不要在没确认前当成定论）：
#  1. 插件发现机制未定：宿主（standalone）是固定扫描某个编译期已知目录，
#     还是运行时可配置路径 + QCoreApplication::addLibraryPath()？
#     这决定了插件产物最终应该躺在哪——当前先输出到
#     ${CMAKE_BINARY_DIR}/plugins/<CATEGORY>/，方便调试期直接找到，
#     是否需要 POST_BUILD 再拷贝一份到 standalone 可执行文件同级目录，
#     等 standalone 里真的写了 QPluginLoader 加载逻辑后再决定。
#  2. JSON 元数据：是通过 Q_PLUGIN_METADATA(FILE "xxx.json") 在编译期直接嵌入插件二进制
#     （Qt 官方推荐、无需额外部署文件），还是同时也需要在 plugins/ 目录下保留一份
#     可读 .json（比如给非 Qt 的启动脚本 / 打包工具解析插件列表用）？
#     如果确认只用 Q_PLUGIN_METADATA 嵌入，下面 METADATA 相关的 add_custom_command
#     可以整段删掉。
#  3. 插件 ABI / CompatVersion 兼容性检查（b_plugin.h 顶部 JSON 示例里的 CompatVersion 字段）
#     目前完全没有落地代码，无论是 host 侧运行时校验还是 CMake 层面的版本号注入，
#     这里都还没有涉及。
#  4. 是否需要 install(TARGETS ...) 把插件也装进最终发布包，取决于产品打包方式，
#     目前跟 bakuon_install_module() 一样按 BAKUON_INSTALL_SDK 关闭，先不处理。
# ============================================================================
function(bakuon_add_plugin)
    cmake_parse_arguments(
        ARG
        ""
        "NAME;PATH;CATEGORY;METADATA"
        "DEPENDS;INCLUDE_DIRS"
        ${ARGN}
    )

    set(PLUGIN_NAME ${ARG_NAME})
    set(PLUGIN_PATH ${ARG_PATH})

    if(NOT ARG_CATEGORY)
        set(ARG_CATEGORY "misc")
    endif()

    bakuon_glob_module_files("${PLUGIN_PATH}" ALL_HEADERS ALL_SOURCES)

    if(NOT ALL_SOURCES)
        message(FATAL_ERROR "bakuon_add_plugin(${PLUGIN_NAME}): 在 ${PLUGIN_PATH} 下没有找到任何 .cpp 文件。")
    endif()

    # MODULE：只能被动态加载，不能被 target_link_libraries() 静态链接，
    # 这是与 bakuon_add_module()/bakuon_add_amalgamated_module() 的核心区别。
    add_library(${PLUGIN_NAME} MODULE)

    target_sources(${PLUGIN_NAME} PRIVATE ${ALL_SOURCES} ${ALL_HEADERS})

    target_include_directories(${PLUGIN_NAME} PRIVATE ${PLUGIN_PATH} ${ARG_INCLUDE_DIRS})

    # 插件只依赖宿主 SHARED 库（bakuon::core）：由它带来公共头目录和导出头，
    # 插件看到的 BAKUON_CORE_API 是 dllimport；私有 source/ 与 EnTT 对插件不可见。
    # 白盒的 bakuon::core_static 不允许出现在插件里（会把第二份宿主状态链进插件）。
    target_link_libraries(${PLUGIN_NAME} PRIVATE bakuon::core ${ARG_DEPENDS})

    _bakuon_apply_common_target_settings(${PLUGIN_NAME})

    # 默认隐藏全部符号，只有 BAKUON_PLUGIN_API 标注的三个 C 入口对外可见。
    set_target_properties(${PLUGIN_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

    # 按分类输出到统一的 plugins/ 目录下，与源码里 plugins/<CATEGORY>/ 的物理布局对应；
    # LIBRARY_OUTPUT_DIRECTORY 对应 Linux/macOS 的 .so/.dylib，RUNTIME_OUTPUT_DIRECTORY
    # 对应 Windows 的 .dll（Windows 上 MODULE 产物走 RUNTIME 而不是 LIBRARY 属性）。
    set_target_properties(
        ${PLUGIN_NAME}
        PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${ARG_CATEGORY}"
                   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${ARG_CATEGORY}"
                   PREFIX "" # 不加平台默认的 lib 前缀，插件文件名与 NAME 保持一致
    )

    if(ARG_METADATA AND EXISTS "${ARG_METADATA}")
        # 见上方 TODO 2：这里先原样拷贝一份 json 到插件产物旁，是否真的需要、
        # 是否应改为纯 Q_PLUGIN_METADATA 编译期嵌入，等确认后再调整/删除本段。
        add_custom_command(
            TARGET ${PLUGIN_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ARG_METADATA}"
                    "$<TARGET_FILE_DIR:${PLUGIN_NAME}>/$<TARGET_FILE_BASE_NAME:${PLUGIN_NAME}>.json"
        )
    endif()

    message(STATUS "[Plugin] ${PLUGIN_NAME} (${ARG_CATEGORY}, ${CMAKE_CURRENT_LIST_FILE})")
endfunction()

function(bakuon_add_test)
    cmake_parse_arguments(
        ARG
        "USE_QT_GUI;USE_QT_CORE"
        "NAME"
        "SOURCES;DEPENDS;INCLUDE_DIRS;LABELS"
        ${ARGN}
    )

    if(NOT ARG_NAME)
        message(FATAL_ERROR "bakuon_add_test: NAME parameter is required")
    endif()

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "bakuon_add_test: SOURCES parameter is required")
    endif()

    # 处理 Qt 自动化编译与链接
    if(ARG_USE_QT_GUI)
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTORCC ON)
        set(CMAKE_AUTOUIC ON)
    elseif(ARG_USE_QT_CORE)
        set(CMAKE_AUTOMOC ON)
    endif()

    # 创建可执行文件
    add_executable(${ARG_NAME} ${ARG_SOURCES})

    # 注入对应的宏和链接 Qt::Test
    if(ARG_USE_QT_CORE OR ARG_USE_QT_GUI)
        find_package(
            QT NAMES Qt6 Qt5
            COMPONENTS Test
            REQUIRED
        )
        find_package(
            Qt${QT_VERSION_MAJOR}
            COMPONENTS Test
            REQUIRED
        )
    endif()

    if(ARG_USE_QT_GUI)
        target_compile_definitions(${ARG_NAME} PRIVATE USE_QT_GUI_APP)
        target_link_libraries(${ARG_NAME} PRIVATE Qt::Test) # 自动追加测试模块
    elseif(ARG_USE_QT_CORE)
        target_compile_definitions(${ARG_NAME} PRIVATE USE_QT_CORE_APP)
        target_link_libraries(${ARG_NAME} PRIVATE Qt::Test) # 自动追加测试模块
    endif()

    # 白盒测试必须链接 STATIC 变体：SHARED 里私有符号是 hidden / 未导出的，会链接失败。
    foreach(_dep IN LISTS ARG_DEPENDS)
        if(TARGET ${_dep})
            get_target_property(_aliased ${_dep} ALIASED_TARGET)
            if(_aliased)
                set(_real ${_aliased})
            else()
                set(_real ${_dep})
            endif()
            get_target_property(_type ${_real} TYPE)
            if(_type STREQUAL "SHARED_LIBRARY")
                message(FATAL_ERROR "bakuon_add_test(${ARG_NAME}): 不能链接动态库 ${_dep}，" "白盒测试请链接 bakuon::core_static")
            endif()
        endif()
    endforeach()

    target_link_libraries(${ARG_NAME} PRIVATE gtest ${ARG_DEPENDS})
    if(ARG_INCLUDE_DIRS)
        target_include_directories(${ARG_NAME} PRIVATE ${ARG_INCLUDE_DIRS})
    endif()

    if(COMMAND bakuon_apply_warnings)
        bakuon_apply_warnings(${ARG_NAME})
    endif()

    # 利用 gtest_discover_tests 优化测试过滤标签
    set(test_labels "bakuon_all") # 所有测试的通标签
    if(ARG_LABELS)
        list(APPEND test_labels ${ARG_LABELS}) # 用户自定义标签（如 core, gui, p0 等）
    endif()
    if(ARG_USE_QT_GUI OR ARG_USE_QT_CORE)
        list(APPEND test_labels "qt") # 自动为依赖 Qt 的测试加上 qt 标签
    endif()

    # 利用 gtest_discover_tests 注入 CTest 属性
    # PRE_TEST：在 ctest 运行时才枚举用例，而不是构建后立刻运行 exe
    # （交叉编译、或 exe 依赖的 DLL 此刻还不在搜索路径上时，构建期枚举会失败）。
    gtest_discover_tests(${ARG_NAME} DISCOVERY_MODE PRE_TEST PROPERTIES LABELS "${test_labels}")
endfunction()
