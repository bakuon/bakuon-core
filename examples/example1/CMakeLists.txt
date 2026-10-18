cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(
    example1
    VERSION 1.0.0
    LANGUAGES CXX
)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE bakuon::core
)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
