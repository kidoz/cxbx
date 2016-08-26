set(CMAKE_C_STANDARD 11)

include_directories("${CXBX_SOURCE_DIR}/include/open-xdk/include")

file(GLOB XHAL_SRC "src/*.c")

add_library (xhal ${XHAL_SRC})