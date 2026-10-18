# 基础模块标签
set(module_label "unit")

set(UNIT_TESTS
    "test.bakuon.cpp"
    "test.entity.cpp"
    "test.domain.cpp"
    "test.world.cpp"
    "test.components.cpp"
    "test.component_storage.cpp"
)

foreach(file_name ${UNIT_TESTS})
    # 从文件名提取 TestName（例如：BakuonTest -> test_unit_bakuon）
    string(REGEX REPLACE "^test\\.(.*)\\.cpp$" "\\1" base_name ${file_name})
    string(TOLOWER ${base_name} lower_name)
    set(target_name "test_unit_${lower_name}")

    bakuon_add_test(
        NAME
        ${target_name}
        SOURCES
        ${file_name}
        DEPENDS
        # 白盒：链接 STATIC 变体（导出宏为空，PUBLIC 带来私有 source/ 与 EnTT），
        # 不能链接 SHARED 的 bakuon::core（bakuon_add_test 会直接报错）。
        bakuon::core_static
        # bakuon_add_test 已包含 gtest, gtest_main 加在这可能希望自定义的 main 入口
        gtest_main
        LABELS
        "${module_label}"
    )
endforeach()
