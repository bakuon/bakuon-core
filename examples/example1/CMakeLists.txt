cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(
    example1
    VERSION 1.0.0
    LANGUAGES CXX
)

add_executable(${PROJECT_NAME} main.cpp)

# 主程序 / 示例：只链接 SHARED 的 bakuon::core，仅依赖公共头 + 导出宏（dllimport），
# 看不到 source/ 与 EnTT。
target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE bakuon::core
)
bakuon_apply_warnings(${PROJECT_NAME})
