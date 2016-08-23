set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11")

include_directories("${CXBX_SOURCE_DIR}/include/open-xdk/include")

file(GLOB XBOXKRNL_SRC "src/*.c")

add_library(xboxkrnl ${XBOXKRNL_SRC})