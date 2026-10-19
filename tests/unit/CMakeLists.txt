set(CMAKE_CXX_STANDARD 20)

#==============================================================================
# A real plugin module built against public headers only (no link dependencies).
#==============================================================================
add_library(bakuon_test_plugin MODULE ${BAKUON_CORE_ROOT_DIR}/tests/plugins/test_plugin.cpp)
target_include_directories(bakuon_test_plugin PRIVATE ${BAKUON_CORE_ROOT_DIR}/include)
target_link_libraries(bakuon_test_plugin PRIVATE bakuon::core)
set_target_properties(
    bakuon_test_plugin
    PROPERTIES PREFIX ""
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN ON
)
bakuon_apply_warnings(bakuon_test_plugin)

#==============================================================================
# Internal tests: link the implementation objects directly.
#==============================================================================
add_executable(
    bakuon_core_tests
    test.bakuon.cpp
    test.components.cpp
    test.domain.cpp
    test.entity.cpp
    test.plugin_host.cpp
    test.plugin_loading.cpp
    test.plugin_ownership.cpp
    test.scheduler.cpp
    test.world.cpp
)
target_link_libraries(bakuon_core_tests PRIVATE bakuon_core_obj EnTT::EnTT GTest::gtest_main)
target_include_directories(bakuon_core_tests PRIVATE ${BAKUON_CORE_ROOT_DIR}/source)
target_compile_definitions(
    bakuon_core_tests PRIVATE "BAKUON_TEST_PLUGIN_PATH=\"$<TARGET_FILE:bakuon_test_plugin>\""
                              "BAKUON_CORE_LIB_PATH=\"$<TARGET_FILE:bakuon_core>\""
)
add_dependencies(bakuon_core_tests bakuon_test_plugin bakuon::core)
bakuon_apply_warnings(bakuon_core_tests)
gtest_discover_tests(bakuon_core_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS "unit")

#==============================================================================
# Public-API tests: link ONLY the shared host library (no EnTT include path).
#==============================================================================
add_executable(
    bakuon_host_api_tests
    test.host_api.cpp
)
target_link_libraries(bakuon_host_api_tests PRIVATE bakuon::core GTest::gtest_main)
target_compile_definitions(
    bakuon_host_api_tests PRIVATE "BAKUON_TEST_PLUGIN_PATH=\"$<TARGET_FILE:bakuon_test_plugin>\""
)
add_dependencies(bakuon_host_api_tests bakuon_test_plugin)
bakuon_apply_warnings(bakuon_host_api_tests)
gtest_discover_tests(bakuon_host_api_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS "plugin")
