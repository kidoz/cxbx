set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11")

include_directories("${CXBX_SOURCE_DIR}/include/open-xdk")

file(GLOB XGFX2D_SRC "src/*.c")

add_library (xgfx2d ${XGFX2D_SRC})