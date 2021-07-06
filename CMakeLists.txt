cmake_minimum_required(VERSION 3.15)
project(CXBX)

add_subdirectory(src/cxbx)
add_subdirectory(src/open-xdk/src/glibc)
add_subdirectory(src/open-xdk/src/xboxkrnl)
add_subdirectory(src/open-xdk/src/xgfx2d)
add_subdirectory(src/open-xdk/src/xhal)
add_subdirectory(src/open-xdk/src/xlibc)
add_subdirectory(src/open-xdk/src/xusb)
add_subdirectory(src/open-xdk/src/xvga)

list (APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

find_package (DirectX)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    function(add_clang_static_analysis target)
        get_target_property(SRCs ${target} SOURCES)
        add_library(${target}_analyze OBJECT EXCLUDE_FROM_ALL ${SRCs})
        set_target_properties(${target}_analyze PROPERTIES
                COMPILE_OPTIONS "--analyze"
                EXCLUDE_FROM_DEFAULT_BUILD true)
    endfunction()
endif ()