set(CMAKE_C_STANDARD 11)

include_directories(
        "${CXBX_SOURCE_DIR}/include/open-xdk/include"
)

file(GLOB XVGA_SRC "src/*.c")

add_library (xvga ${XVGA_SRC})