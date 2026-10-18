set(CMAKE_CXX_STANDARD 17)

add_library(plugin_movement MODULE plugin_movement/movement_plugin.cpp)
target_link_libraries(plugin_movement PRIVATE bakuon::core)
target_include_directories(plugin_movement PRIVATE ${BAKUON_CORE_CMAKE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/common)
set_target_properties(
    plugin_movement
    PROPERTIES PREFIX ""
               CXX_VISIBILITY_PRESET hidden
               VISIBILITY_INLINES_HIDDEN ON
)
bakuon_apply_warnings(plugin_movement)

add_executable(
    plugin_host
    plugin_host/main.cpp
)
target_include_directories(plugin_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(plugin_host PRIVATE bakuon::core)
add_dependencies(plugin_host plugin_movement)
bakuon_apply_warnings(plugin_host)

if(BAKUON_BUILD_TESTS)
    # The example is its own smoke test: exit code 0 only if positions, the hook and the unload rules hold.
    add_test(NAME example_plugin_host COMMAND plugin_host $<TARGET_FILE:plugin_movement>)
endif()

# bakuon_apply_targets_folder(${CMAKE_CURRENT_LIST_DIR})
