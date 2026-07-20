cxbx_core = static_library(
  'cxbx_core',
  'src/core/Error.cpp',
  'src/core/Exe.cpp',
  'src/core/OpenXDK.cpp',
  'src/core/Xbe.cpp',
  include_directories : cxbx_inc,
  cpp_args : [
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
)

cxbx_core_dep = declare_dependency(
  link_with : cxbx_core,
  include_directories : cxbx_inc,
)

cxbx_shared_win32_src = files(
  'src/win32/Mutex.cpp',
  'src/win32/XBController.cpp',
  'src/win32/XBVideo.cpp',
)

cxbx_trace = static_library(
  'cxbx_trace',
  'src/core/trace/trace.cpp',
  'src/platform/win32/trace_os.cpp',
  include_directories : cxbx_inc,
)

cxbx_trace_dep = declare_dependency(
  link_with : cxbx_trace,
  include_directories : cxbx_inc,
)

cxbx_yuy2 = static_library(
  'cxbx_yuy2',
  'src/core/Yuy2Converter.cpp',
  include_directories : cxbx_inc,
)

cxbx_yuy2_dep = declare_dependency(
  link_with : cxbx_yuy2,
  include_directories : cxbx_inc,
)

cxbx_launcher_src = files(
  'src/win32/Cxbx/DlgControllerConfig.cpp',
  'src/win32/Cxbx/DlgVideoConfig.cpp',
  'src/win32/Cxbx/EmuExe.cpp',
  'src/win32/Cxbx/Prolog.cpp',
  'src/win32/Cxbx/WinMain.cpp',
  'src/win32/Cxbx/Wnd.cpp',
  'src/win32/Cxbx/WndAbout.cpp',
  'src/win32/Cxbx/WndMain.cpp',
)

cxbx_runtime_src = files(
  'src/core/nv2a_capture.cpp',
  'src/core/PcmConverter.cpp',
  'src/core/XboxAdpcmDecoder.cpp',
  'src/win32/EmuShared.cpp',
  'src/win32/HostInput.cpp',
  'src/win32/CxbxKrnl/Emu.cpp',
  'src/win32/CxbxKrnl/EmuD3D8.cpp',
  'src/win32/CxbxKrnl/EmuD3D8Conv.cpp',
  'src/win32/CxbxKrnl/EmuDes.cpp',
  'src/win32/CxbxKrnl/EmuDInput.cpp',
  'src/win32/CxbxKrnl/EmuDSound.cpp',
  'src/win32/CxbxKrnl/EmuXact.cpp',
  'src/win32/CxbxKrnl/EmuFS.cpp',
  'src/win32/CxbxKrnl/EmuFile.cpp',
  'src/win32/CxbxKrnl/EmuKrnl.cpp',
  'src/win32/CxbxKrnl/EmuLDT.cpp',
  'src/win32/CxbxKrnl/EmuNtDll.cpp',
  'src/win32/CxbxKrnl/EmuVshDecoder.cpp',
  'src/win32/CxbxKrnl/EmuXG.cpp',
  'src/win32/CxbxKrnl/EmuXOnline.cpp',
  'src/win32/CxbxKrnl/EmuXapi.cpp',
  'src/win32/CxbxKrnl/HLEDataBase.cpp',
  'src/win32/CxbxKrnl/KernelThunk.cpp',
)

cxbx_link_libs = [
  libxboxkrnl,
  libxgfx2d,
  libxhal,
  libxlibc,
  libxvga,
]

if get_option('build_xusb')
  cxbx_link_libs += [libxusb]
endif

dlltool = find_program('llvm-dlltool')
d3d8_import_lib = custom_target(
  'd3d8_import_lib',
  input : 'd3d8.def',
  output : 'd3d8.lib',
  command : [dlltool, '-m', 'i386', '-k', '-d', '@INPUT@', '-l', '@OUTPUT@'],
)
cxbx_link_libs += [d3d8_import_lib]

cxbx_link_args = [
  '-lddraw',
  '-ldinput8',
  '-ldsound',
  '-ldxguid',
  '-lwinmm',
  '-lws2_32',
]

resource_compiler = find_program('llvm-rc')
resource_root = join_paths(meson.project_source_root(), 'Resource')
branding_root = join_paths(meson.project_source_root(), 'docs', 'branding')
cxbx_runtime_resources = custom_target(
  'cxbx_runtime_resources',
  input : files('../../Resource/CxbxDll.rc'),
  output : 'CxbxDll.res',
  command : [
    resource_compiler,
    '/nologo',
    '/fo@OUTPUT@',
    '/I' + resource_root,
    '/I' + branding_root,
    '@INPUT@',
  ],
  depend_files : files(
    '../../docs/branding/cxbx.ico',
    '../../include/cxbx/include/win32/cxbxkrnl/ResCxbxDll.h',
  ),
)
cxbx_launcher_resources = custom_target(
  'cxbx_launcher_resources',
  input : files('../../Resource/Cxbx.rc'),
  output : 'Cxbx.res',
  command : [
    resource_compiler,
    '/nologo',
    '/fo@OUTPUT@',
    '/I' + resource_root,
    '/I' + branding_root,
    '@INPUT@',
  ],
  depend_files : files(
    '../../docs/branding/cxbx.ico',
    '../../include/cxbx/include/win32/cxbx/ResCxbx.h',
    '../../Resource/Splash.bmp',
  ),
)

cxbx_runtime = shared_library(
  'Cxbx',
  cxbx_shared_win32_src,
  cxbx_runtime_src,
  cxbx_runtime_resources,
  include_directories : cxbx_inc,
  dependencies : [
    cxbx_core_dep,
    cxbx_trace_dep,
    cxbx_yuy2_dep,
  ],
  link_with : cxbx_link_libs,
  link_args : cxbx_link_args + [
    '-Wl,/BASE:0x10000000',
    '-Wl,/DYNAMICBASE:NO',
  ],
  cpp_args : [
    '-D_CXBXKRNL_INTERNAL',
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
  name_prefix : '',
)

executable(
  'cxbx',
  cxbx_shared_win32_src,
  cxbx_launcher_src,
  cxbx_launcher_resources,
  include_directories : cxbx_inc,
  dependencies : cxbx_core_dep,
  link_with : [
    cxbx_runtime,
    cxbx_link_libs,
  ],
  link_args : cxbx_link_args,
  cpp_args : [
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
  win_subsystem : 'windows',
)
