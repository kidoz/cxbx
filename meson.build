project(
  'cxbx',
  ['c', 'cpp'],
  version : '0.1.0',
  meson_version : '>=0.56.0',
  default_options : [
    'c_std=c11',
    'cpp_std=c++17',
  ],
)

# The tree is MSVC/Win32-targeted and relies pervasively on __declspec and other
# Microsoft language extensions. Enable clang's MS-extension support globally so
# the Xbox/open-xdk headers parse when building with a non-MSVC toolchain.
add_project_arguments('-fms-extensions', language : ['c', 'cpp'])

# Include directory objects, resolved relative to the source root so they can be
# handed to targets defined in the subdirectories below.
openxdk_root_inc = include_directories('include/open-xdk')
openxdk_inc = include_directories('include/open-xdk/include')

cxbx_inc = include_directories(
  'include/cxbx/include',
  'include/cxbx/include/win32',
  'include/cxbx/include/win32/cxbx',
  'include/cxbx/include/win32/cxbxkrnl',
  'include/open-xdk/include',
  # Bundled DirectX SDK headers used directly by the Meson build.
  '3rdparty/dxsdk/Include',
)

# open-xdk support libraries. Each sets a `lib<name>` variable consumed by the
# cxbx executable below (meson subdir() shares the enclosing scope).
subdir('src/open-xdk/src/glibc')
subdir('src/open-xdk/src/xboxkrnl')
subdir('src/open-xdk/src/xgfx2d')
subdir('src/open-xdk/src/xhal')
subdir('src/open-xdk/src/xlibc')
subdir('src/open-xdk/src/xusb')
subdir('src/open-xdk/src/xvga')

# Emulator executable (links the libraries above).
subdir('src/cxbx')
