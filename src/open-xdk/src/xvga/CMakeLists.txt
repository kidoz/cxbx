set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11")

include_directories(
        "${CXBX_SOURCE_DIR}/include/open-xdk/include"
        "${CXBX_SOURCE_DIR}/include/open-xdk/include/xvga/include"
)

file(GLOB XVGA_SRC "src/*.c")

add_library (xvga ${XVGA_SRC})

target_include_directories (xvga PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})