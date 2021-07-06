set(CMAKE_CXX_STANDARD 17)

include_directories(
        "${CXBX_SOURCE_DIR}/include/cxbx/include"
        "${CXBX_SOURCE_DIR}/include/cxbx/include/win32"
        "${CXBX_SOURCE_DIR}/include/cxbx/include/win32/cxbx"
        "${CXBX_SOURCE_DIR}/include/cxbx/include/win32/cxbxkrnl"
        "${CXBX_SOURCE_DIR}/include/open-xdk/include"
        "${CXBX_SOURCE_DIR}/3rdparty/dxsdk/Include"
)

file(GLOB CXBX_SRC "src/core/*.cpp" "src/standard/cxbe/*.cpp" "src/win32/cxbx/*.cpp" "src/win32/cxbxkrnl/*.cpp" "src/win32/*.cpp")

add_executable(cxbx ${CXBX_SRC})