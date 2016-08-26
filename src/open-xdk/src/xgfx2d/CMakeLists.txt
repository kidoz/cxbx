set(CMAKE_C_STANDARD 11)

include_directories("${CXBX_SOURCE_DIR}/include/open-xdk/include")

file(GLOB XGFX2D_SRC "src/*.c")

add_library (xgfx2d ${XGFX2D_SRC})