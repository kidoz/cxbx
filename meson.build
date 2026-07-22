project(
  'cxbx',
  ['c', 'cpp'],
  version : '0.1.0',
  meson_version : '>=1.4.0',
  default_options : [
    'c_std=c17',
    'cpp_std=c++20',
    'buildtype=debugoptimized',
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
cxbx_public_inc = include_directories('include/cxbx/include')
cxbx_win32_inc = include_directories('include/cxbx/include/win32')
cxbx_frontend_inc = include_directories('include/cxbx/include/win32/cxbx')
cxbx_krnl_inc = include_directories('include/cxbx/include/win32/cxbxkrnl')
cxbx_dxsdk_inc = include_directories('3rdparty/dxsdk/Include')

# The xusb library is a Linux-kernel-derived USB stack. Its sources use
# quote-includes rooted at the xusb/ and open-xdk/ header directories
# (e.g. "usb_wrapper.h", "linux/usb.h", "OpenXDK.h") rather than the
# xusb/-prefixed form the other libraries use, so it needs those directories
# on its include path directly.
openxdk_xusb_inc = include_directories(
  'include/open-xdk/include/xusb',
  'include/open-xdk/include/open-xdk',
)

# open-xdk support libraries. Each sets a `lib<name>` variable consumed by the
# cxbx executable below (meson subdir() shares the enclosing scope).
#
# NOTE: src/open-xdk/src/glibc is intentionally not built. Its `glibc` archive
# is never linked into the cxbx executable (xlibc supplies the libc/math/string
# routines that are actually used), and its sources expect a private GNU libc
# internal header tree that this build does not wire up. It is dead weight, so
# it is excluded rather than ported. Re-add the subdir() line to restore it.
subdir('src/open-xdk/src/xboxkrnl')
subdir('src/open-xdk/src/xgfx2d')
subdir('src/open-xdk/src/xhal')
subdir('src/open-xdk/src/xlibc')
if get_option('build_xusb')
  subdir('src/open-xdk/src/xusb')
endif
subdir('src/open-xdk/src/xvga')

# Emulator executable (links the libraries above).
subdir('src/cxbx')

# Deterministic host-side tests for input translation and other Win32 boundaries.
subdir('tests/host')

# XISO host tool (list/extract/create/verify Xbox disc images). Pure C++20,
# no emulator dependencies. Built on every machine -- portable host code.
if get_option('build_xiso')
  subdir('src/xiso')
endif
