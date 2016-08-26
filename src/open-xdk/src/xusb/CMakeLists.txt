set(CMAKE_C_STANDARD 11)

include_directories("${CXBX_SOURCE_DIR}/include/open-xdk")

file(GLOB XUSB_SRC "src/core/*.c" "src/host/*.c" "src/sys/*.c" "src/*.c")

add_library(xusb ${XUSB_SRC})