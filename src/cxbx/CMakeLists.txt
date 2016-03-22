set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

include_directories(
        "${CXBX_SOURCE_DIR}/include/cxbx/include"
        "${CXBX_SOURCE_DIR}/include/cxbx/include/core"
        "${CXBX_SOURCE_DIR}/include/cxbx/include/win32"
        "${CXBX_SOURCE_DIR}/include/cxbx/include/win32/cxbx"
        "${CXBX_SOURCE_DIR}/include/cxbx/include/win32/cxbxkrnl"
)

file(GLOB CXBX_SRC "src/core/*.cpp" "src/standard/cxbe/*.cpp" "src/win32/cxbx/*.cpp" "src/win32/cxbxkrnl/*.cpp" "src/win32/*.cpp")

add_executable(cxbx ${CXBX_SRC})