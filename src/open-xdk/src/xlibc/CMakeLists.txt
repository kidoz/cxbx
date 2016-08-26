set(CMAKE_C_STANDARD 11)

include_directories("${CXBX_SOURCE_DIR}/include/open-xdk")

file(GLOB XLIBC_SRC "src/math/stdio/*.c" "src/math/string/*.c" "src/math/*.c")

add_library(xlibc ${XLIBC_SRC})