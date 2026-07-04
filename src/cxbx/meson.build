cxbx_common_src = files(
  'src/core/Error.cpp',
  'src/core/Exe.cpp',
  'src/core/OpenXDK.cpp',
  'src/core/Xbe.cpp',
  'src/win32/Mutex.cpp',
  'src/win32/XBController.cpp',
  'src/win32/XBVideo.cpp',
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
  'src/win32/EmuShared.cpp',
  'src/win32/CxbxKrnl/Emu.cpp',
  'src/win32/CxbxKrnl/EmuD3D8.cpp',
  'src/win32/CxbxKrnl/EmuD3D8Conv.cpp',
  'src/win32/CxbxKrnl/EmuDes.cpp',
  'src/win32/CxbxKrnl/EmuDInput.cpp',
  'src/win32/CxbxKrnl/EmuDSound.cpp',
  'src/win32/CxbxKrnl/EmuFS.cpp',
  'src/win32/CxbxKrnl/EmuFile.cpp',
  'src/win32/CxbxKrnl/EmuKrnl.cpp',
  'src/win32/CxbxKrnl/EmuLDT.cpp',
  'src/win32/CxbxKrnl/EmuNtDll.cpp',
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

cxbx_runtime = shared_library(
  'Cxbx',
  cxbx_common_src,
  cxbx_runtime_src,
  include_directories : cxbx_inc,
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
  cxbx_common_src,
  cxbx_launcher_src,
  include_directories : cxbx_inc,
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
