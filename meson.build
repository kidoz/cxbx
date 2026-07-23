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

build_host_asan = get_option('build_host_asan')

if build_host_asan
  if meson.is_cross_build()
    error('build_host_asan requires a native build, not a cross build.')
  endif

  if host_machine.system() != 'windows' or host_machine.cpu_family() != 'x86_64'
    error('build_host_asan requires a native x86_64 Windows host.')
  endif

  cpp = meson.get_compiler('cpp')
  if cpp.get_id() != 'clang'
    error('build_host_asan requires Clang.')
  endif

  if get_option('b_sanitize') != 'address'
    error('build_host_asan requires -Db_sanitize=address without additional sanitizers.')
  endif

  if get_option('b_vscrt') != 'static_from_buildtype'
    error('build_host_asan requires -Db_vscrt=static_from_buildtype.')
  endif

  clang_resource_dir_result = run_command(
    cpp.cmd_array(),
    '-print-resource-dir',
    check : true,
  )
  clang_resource_dir = clang_resource_dir_result.stdout().strip()
  cxbx_host_asan_runtime_dir = join_paths(
    clang_resource_dir,
    'lib',
    'windows',
  )

  fs = import('fs')
  foreach runtime_file : [
    'clang_rt.asan_dynamic-x86_64.dll',
    'clang_rt.asan_dynamic-x86_64.lib',
    'clang_rt.asan_static_runtime_thunk-x86_64.lib',
  ]
    runtime_path = join_paths(cxbx_host_asan_runtime_dir, runtime_file)
    if not fs.is_file(runtime_path)
      error('build_host_asan requires ' + runtime_path)
    endif
  endforeach

  # Meson 1.11 instruments Windows objects for b_sanitize=address but omits the
  # corresponding compiler-rt libraries from clang++ executable link commands.
  # Reproduce Clang's Windows ASan link contract explicitly for this lane.
  cxbx_host_asan_import_library = join_paths(
    cxbx_host_asan_runtime_dir,
    'clang_rt.asan_dynamic-x86_64.lib',
  )
  cxbx_host_asan_runtime_thunk = join_paths(
    cxbx_host_asan_runtime_dir,
    'clang_rt.asan_static_runtime_thunk-x86_64.lib',
  )
  add_project_link_arguments(
    cxbx_host_asan_import_library,
    '-Wl,/wholearchive:' + cxbx_host_asan_runtime_thunk,
    language : 'cpp',
  )

  cxbx_host_asan_test_env = environment()
  cxbx_host_asan_test_env.prepend('PATH', cxbx_host_asan_runtime_dir)
  llvm_symbolizer = find_program('llvm-symbolizer', required : true)
  cxbx_host_asan_test_env.set(
    'ASAN_SYMBOLIZER_PATH',
    llvm_symbolizer.full_path(),
  )
  cxbx_host_asan_test_env.set(
    'ASAN_OPTIONS',
    'halt_on_error=1:abort_on_error=1:print_stacktrace=1:symbolize=1',
  )
endif

# Include directory objects, resolved relative to the source root so they can be
# handed to targets defined in the subdirectories below.
openxdk_root_inc = include_directories('include/open-xdk')
openxdk_inc = include_directories('include/open-xdk/include')
cxbx_public_inc = include_directories('include/cxbx/include')
cxbx_win32_inc = include_directories('include/cxbx/include/win32')
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

if build_host_asan
  # The sanitizer lane deliberately excludes the emulator, open-xdk, xiso, and
  # host tests that have not passed the native x86_64 eligibility gate.
  subdir('src/cxbx')
  subdir('tests/host')
else
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
endif
