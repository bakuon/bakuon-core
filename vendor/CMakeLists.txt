# ============================================================================
# vendor —— 第三方依赖的统一入口
# ============================================================================

# set(CPM_SOURCE_CACHE "${CMAKE_SOURCE_DIR}/vendor")

# 仅在需要构建测试时才拉取 googletest，避免非测试场景下的无谓下载/编译开销。
if(BAKUON_BUILD_TESTS)
    cpmaddpackage(
        NAME googletest
        GITHUB_REPOSITORY google/googletest
        GIT_TAG release-1.12.1 # 建议指定具体版本标签，如 v1.14.0 或 release-1.12.1
        VERSION 1.12.1
        OPTIONS "INSTALL_GTEST OFF" # 禁止安装 GTest 到系统目录，加速构建
                "gtest_force_shared_crt ON" # Windows MSVC 下强制使用动态运行时库（可选，视项目需求而定）
        EXCLUDE_FROM_ALL YES
    )
endif()

cpmaddpackage(
    NAME EnTT
    GITHUB_REPOSITORY skypjack/entt
    GIT_TAG v3.16.0 # 钉 tag，不要追 main
    VERSION 3.16.0
    OPTIONS "ENTT_INSTALL OFF" "ENTT_BUILD_TESTING OFF"
    EXCLUDE_FROM_ALL YES
)

bakuon_apply_targets_folder(${CMAKE_CURRENT_LIST_DIR} ALIAS "vendor")
