# project(
#     core
#     VERSION 1.0.0
#     LANGUAGES CXX
# )

set(CMAKE_CXX_EXTENSIONS OFF)
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated_include")

#==============================================================================
# Host implementation objects (packaged into the shared host library in a later step).
#==============================================================================
set(OBJECT_TARGET "bakuon_core_obj")
add_library(
    ${OBJECT_TARGET} OBJECT
    b_bakuon.cpp
    b_domain.cpp
    b_world.cpp
    b_scheduler.cpp
    b_plugin_host.cpp
    b_module_loader.cpp
    b_plugin_manager.cpp
    b_host.cpp
    b_component_registry.cpp
)
target_include_directories(
    ${OBJECT_TARGET}
    PUBLIC ${BAKUON_CORE_ROOT_DIR}/include
    PRIVATE ${BAKUON_CORE_ROOT_DIR}/source
)
target_link_libraries(${OBJECT_TARGET} PRIVATE EnTT::EnTT)
target_compile_features(${OBJECT_TARGET} PUBLIC cxx_std_20)
if(CMAKE_DL_LIBS)
    target_link_libraries(${OBJECT_TARGET} PUBLIC ${CMAKE_DL_LIBS})
endif()
target_compile_definitions(${OBJECT_TARGET} PUBLIC BAKUON_CORE_BUILD)
set_target_properties(${OBJECT_TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON)

bakuon_apply_warnings(${OBJECT_TARGET})

#==============================================================================
# the host dynamic library: exports only the three C symbols of host.h
#==============================================================================
set(SHARED_TARGET "bakuon_core")
add_library(${SHARED_TARGET} SHARED $<TARGET_OBJECTS:${OBJECT_TARGET}> b_api.cpp)
add_library(bakuon::core ALIAS ${SHARED_TARGET})
target_include_directories(
    ${SHARED_TARGET}
    PUBLIC ${BAKUON_CORE_ROOT_DIR}/include
    PRIVATE ${BAKUON_CORE_ROOT_DIR}/source
)
target_link_libraries(${SHARED_TARGET} PRIVATE EnTT::EnTT)
target_compile_features(${SHARED_TARGET} PUBLIC cxx_std_20)
if(CMAKE_DL_LIBS)
    target_link_libraries(${SHARED_TARGET} PRIVATE ${CMAKE_DL_LIBS})
endif()

# PRIVATE（仅 b_api.cpp 自己编译时需要）绝不能 PUBLIC：会泄漏给 主程序/插件
target_compile_definitions(${SHARED_TARGET} PRIVATE BAKUON_CORE_BUILD)
set_target_properties(${SHARED_TARGET} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

bakuon_apply_warnings(${SHARED_TARGET})

# bakuon_apply_targets_folder(${CMAKE_CURRENT_LIST_DIR} ALIAS "core")
