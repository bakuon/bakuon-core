cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(
    bakuon
    VERSION 1.0.0
    LANGUAGES CXX
)

bakuon_add_module(
    NAME
    ${PROJECT_NAME}
    PATH
    ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS
    EnTT::EnTT
    INCLUDE_DIRS
    ${BAKUON_ROOT_DIR}/include
    # 面向消费者的稳定门面头文件根目录：include/bakuon/*.h
    PUBLIC_INCLUDE_DIRS
    ${BAKUON_ROOT_DIR}/include
)

bakuon_apply_targets_folder(${CMAKE_CURRENT_LIST_DIR} ALIAS "core")
