set(CMAKE_C_STANDARD 11)

include_directories("${CXBX_SOURCE_DIR}/include/open-xdk/include")

file(GLOB XBOXKRNL_SRC "src/*.c")

add_library(xboxkrnl ${XBOXKRNL_SRC})