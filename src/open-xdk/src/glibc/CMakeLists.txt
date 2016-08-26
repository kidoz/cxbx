set(CMAKE_C_STANDARD 11)

include_directories("${CXBX_SOURCE_DIR}/include/open-xdk")

file(GLOB GLIBC_SRC "src/math/ieee754/*.c" "src/math/*.c" "src/stdio/*.c" "src/string/*.c" "src/*.c")

add_library(glibc ${GLIBC_SRC})