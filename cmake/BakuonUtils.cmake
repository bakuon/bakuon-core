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
function(_get_targets_by_directory _result _dir)
    get_property(
        _subdirs
        DIRECTORY ${_dir}
        PROPERTY SUBDIRECTORIES
    )
    foreach(_subdir IN LISTS _subdirs)
        _get_targets_by_directory(${_result} ${_subdir})
    endforeach()

    get_directory_property(_sub_targets DIRECTORY ${_dir} BUILDSYSTEM_TARGETS)
    set(${_result} ${${_result}} ${_sub_targets} PARENT_SCOPE)
endfunction()

function(bakuon_apply_targets_folder _dir)
    cmake_parse_arguments(ARG "" "ALIAS" "" ${ARGN})

    _get_targets_by_directory(ALL_TARGETS ${_dir})

    foreach(target IN LISTS ALL_TARGETS)

        # 获取该 Target 所在的源目录
        get_target_property(TARGET_SOURCE_DIR ${target} SOURCE_DIR)

        # 计算相对路径
        if(TARGET_SOURCE_DIR)
            file(RELATIVE_PATH REL_DIR ${BAKUON_CORE_ROOT_DIR} ${TARGET_SOURCE_DIR})

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
