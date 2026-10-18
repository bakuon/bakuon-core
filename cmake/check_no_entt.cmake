# Usage: cmake -DPUBLIC_DIR=<include dir> -P check_no_entt.cmake
#
# 扫描公共头里的「代码」是否出现 EnTT 符号 / 头文件 / 宏。
# 注释会先被剥掉再匹配：注释里提到 entt 不构成对 EnTT 的依赖；真正的依赖
# （#include <entt/...>、entt:: 类型、ENTT_ 宏）一定出现在代码里。
# 编译层面的强检查见 tests/CMakeLists.txt 的 bakuon_public_headers_check。
file(GLOB_RECURSE _headers "${PUBLIC_DIR}/*.h" "${PUBLIC_DIR}/*.hpp")
set(_bad "")
foreach(_h IN LISTS _headers)
    file(READ "${_h}" _content)
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" _content "${_content}") # /* ... */
    string(REGEX REPLACE "//[^\n]*" "" _content "${_content}") # // ...
    if(_content MATCHES "entt::|<entt/|ENTT_|entt_traits")
        list(APPEND _bad "${_h}")
    endif()
endforeach()
if(_bad)
    message(FATAL_ERROR "Backend symbols leaked into public headers:\n  ${_bad}")
endif()
