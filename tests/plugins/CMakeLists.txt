set(module_label "plugins")

# A real plugin module built against public headers only (no link dependencies).
add_library(bakuon_test_plugin MODULE test_plugin.cpp)
target_include_directories(bakuon_test_plugin PRIVATE ${PROJECT_SOURCE_DIR}/include)
set_target_properties(
    bakuon_test_plugin
    PROPERTIES PREFIX ""
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN ON
)
bakuon_set_warnings(bakuon_test_plugin)
