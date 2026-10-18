#==============================================================================
# Internal tests: link the implementation objects directly.
#==============================================================================
add_executable(
    bakuon_core_tests
    test.bakuon.cpp test.component_storage.cpp test.components.cpp test.domain.cpp test.entity.cpp test.world.cpp
)
target_link_libraries(bakuon_core_tests PRIVATE bakuon_core_obj EnTT::EnTT GTest::gtest_main)
target_include_directories(bakuon_core_tests PRIVATE ${BAKUON_CORE_ROOT_DIR}/source)
bakuon_apply_warnings(bakuon_core_tests)
gtest_discover_tests(bakuon_core_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS "unit")

#==============================================================================
# Public-API tests: link ONLY the shared host library (no EnTT include path).
#==============================================================================
# add_executable(bakuon_host_api_tests test.host_api.cpp)
# target_link_libraries(bakuon_host_api_tests PRIVATE bakuon::core GTest::gtest_main)
# target_compile_definitions(
#     bakuon_host_api_tests PRIVATE "BAKUON_TEST_PLUGIN_PATH=\"$<TARGET_FILE:bakuon_test_plugin>\""
# )
# add_dependencies(bakuon_host_api_tests bakuon_test_plugin)
# bakuon_apply_warnings(bakuon_host_api_tests)
# gtest_discover_tests(bakuon_host_api_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS "plugin")
