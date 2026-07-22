cxbx_core = static_library(
  'cxbx_core',
  'src/core/error.cpp',
  'src/core/exe.cpp',
  'src/core/open_xdk.cpp',
  'src/core/xbe.cpp',
  include_directories : [
    cxbx_public_inc,
    cxbx_win32_inc,
    cxbx_krnl_inc,
  ],
  cpp_args : [
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
)

cxbx_core_dep = declare_dependency(
  link_with : cxbx_core,
  include_directories : [
    cxbx_public_inc,
    cxbx_win32_inc,
  ],
)

cxbx_pcm = static_library(
  'cxbx_pcm',
  'src/core/pcm_converter.cpp',
  include_directories : cxbx_public_inc,
)

cxbx_pcm_dep = declare_dependency(
  link_with : cxbx_pcm,
  include_directories : cxbx_public_inc,
)

cxbx_xbox_adpcm = static_library(
  'cxbx_xbox_adpcm',
  'src/core/xbox_adpcm_decoder.cpp',
  include_directories : cxbx_public_inc,
)

cxbx_xbox_adpcm_dep = declare_dependency(
  link_with : cxbx_xbox_adpcm,
  include_directories : cxbx_public_inc,
)

cxbx_nv2a_capture = static_library(
  'cxbx_nv2a_capture',
  'src/core/nv2a_capture.cpp',
  include_directories : cxbx_public_inc,
)

cxbx_nv2a_capture_dep = declare_dependency(
  link_with : cxbx_nv2a_capture,
  include_directories : cxbx_public_inc,
)

cxbx_platform_win32_inc = [
  cxbx_krnl_inc,
  cxbx_dxsdk_inc,
]

cxbx_platform_win32 = static_library(
  'cxbx_platform_win32',
  'src/win32/mutex.cpp',
  'src/win32/xbox_controller.cpp',
  'src/win32/xbox_video.cpp',
  include_directories : cxbx_platform_win32_inc,
  dependencies : cxbx_core_dep,
  cpp_args : [
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
)

cxbx_platform_win32_dep = declare_dependency(
  link_with : cxbx_platform_win32,
  dependencies : cxbx_core_dep,
  link_args : [
    '-ldinput8',
    '-ldxguid',
  ],
  include_directories : cxbx_platform_win32_inc,
)

cxbx_host_input_win32 = static_library(
  'cxbx_host_input_win32',
  'src/win32/host_input.cpp',
  include_directories : cxbx_win32_inc,
)

cxbx_host_input_win32_dep = declare_dependency(
  link_with : cxbx_host_input_win32,
  include_directories : cxbx_win32_inc,
)

cxbx_runtime_win32 = static_library(
  'cxbx_runtime_win32',
  'src/platform/win32/shared_controller_config.cpp',
  'src/platform/win32/shared_runtime_state.cpp',
  'src/platform/win32/shared_video_config.cpp',
  'src/platform/win32/shared_runtime_storage.cpp',
  'src/platform/win32/ntdll_emulation.cpp',
  dependencies : cxbx_platform_win32_dep,
  cpp_args : [
    '-D_CXBXKRNL_INTERNAL',
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
)

cxbx_runtime_win32_dep = declare_dependency(
  link_whole : cxbx_runtime_win32,
  dependencies : cxbx_platform_win32_dep,
)

cxbx_guest_tls_win32 = static_library(
  'cxbx_guest_tls_win32',
  'src/platform/win32/guest_tls/fs_emulation.cpp',
  'src/platform/win32/guest_tls/ldt_emulation.cpp',
  include_directories : [
    openxdk_inc,
    cxbx_krnl_inc,
    cxbx_dxsdk_inc,
    cxbx_public_inc,
    cxbx_win32_inc,
  ],
  cpp_args : [
    '-D_CXBXKRNL_INTERNAL',
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
)

cxbx_guest_tls_win32_dep = declare_dependency(
  link_whole : cxbx_guest_tls_win32,
  dependencies : cxbx_runtime_win32_dep,
)

cxbx_hle_input = static_library(
  'cxbx_hle_input',
  'src/platform/win32/emulation_window_access.cpp',
  'src/platform/win32/host_input_lifecycle.cpp',
  'src/hle/input/dinput_emulation.cpp',
  dependencies : [
    cxbx_host_input_win32_dep,
    cxbx_platform_win32_dep,
  ],
  cpp_args : [
    '-D_CXBXKRNL_INTERNAL',
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
)

cxbx_hle_input_dep = declare_dependency(
  link_whole : cxbx_hle_input,
  dependencies : [
    cxbx_host_input_win32_dep,
    cxbx_runtime_win32_dep,
    cxbx_platform_win32_dep,
  ],
)

cxbx_trace = static_library(
  'cxbx_trace',
  'src/core/trace/trace.cpp',
  'src/platform/win32/trace_os.cpp',
  include_directories : cxbx_public_inc,
)

cxbx_trace_dep = declare_dependency(
  link_with : cxbx_trace,
  include_directories : cxbx_public_inc,
)

cxbx_hle_audio = static_library(
  'cxbx_hle_audio',
  'src/hle/audio/dsound_emulation.cpp',
  'src/hle/audio/xact_emulation.cpp',
  include_directories : openxdk_inc,
  dependencies : [
    cxbx_pcm_dep,
    cxbx_platform_win32_dep,
    cxbx_trace_dep,
    cxbx_xbox_adpcm_dep,
  ],
  cpp_args : [
    '-D_CXBXKRNL_INTERNAL',
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
)

cxbx_hle_audio_dep = declare_dependency(
  link_whole : cxbx_hle_audio,
  dependencies : [
    cxbx_guest_tls_win32_dep,
    cxbx_pcm_dep,
    cxbx_platform_win32_dep,
    cxbx_trace_dep,
    cxbx_xbox_adpcm_dep,
  ],
  link_args : '-ldsound',
)

cxbx_hle_kernel = static_library(
  'cxbx_hle_kernel',
  'src/hle/kernel/file_emulation.cpp',
  'src/hle/kernel/kernel_emulation.cpp',
  include_directories : [
    cxbx_win32_inc,
    cxbx_krnl_inc,
    openxdk_inc,
  ],
  dependencies : cxbx_trace_dep,
  cpp_args : [
    '-D_CXBXKRNL_INTERNAL',
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
)

cxbx_hle_kernel_dep = declare_dependency(
  link_whole : cxbx_hle_kernel,
  dependencies : [
    cxbx_guest_tls_win32_dep,
    cxbx_trace_dep,
  ],
)

cxbx_hle_xapi = static_library(
  'cxbx_hle_xapi',
  'src/hle/xapi/xapi_emulation.cpp',
  dependencies : [
    cxbx_host_input_win32_dep,
    cxbx_platform_win32_dep,
  ],
  cpp_args : [
    '-D_CXBXKRNL_INTERNAL',
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
)

cxbx_hle_xapi_dep = declare_dependency(
  link_whole : cxbx_hle_xapi,
  dependencies : [
    cxbx_guest_tls_win32_dep,
    cxbx_hle_input_dep,
    cxbx_platform_win32_dep,
  ],
)

cxbx_hle_xonline = static_library(
  'cxbx_hle_xonline',
  'src/hle/xonline/xonline_emulation.cpp',
  include_directories : [
    cxbx_public_inc,
    cxbx_win32_inc,
    cxbx_krnl_inc,
  ],
  cpp_args : [
    '-D_CXBXKRNL_INTERNAL',
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
)

cxbx_hle_xonline_dep = declare_dependency(
  link_whole : cxbx_hle_xonline,
  dependencies : cxbx_guest_tls_win32_dep.partial_dependency(link_args : true, links : true),
  link_args : '-lws2_32',
)

cxbx_hle_xg = static_library(
  'cxbx_hle_xg',
  'src/hle/xg/xg_emulation.cpp',
  include_directories : [
    cxbx_public_inc,
    cxbx_win32_inc,
    cxbx_krnl_inc,
    cxbx_dxsdk_inc,
  ],
  cpp_args : [
    '-D_CXBXKRNL_INTERNAL',
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
)

cxbx_hle_xg_dep = declare_dependency(
  link_whole : cxbx_hle_xg,
  dependencies : cxbx_guest_tls_win32_dep.partial_dependency(link_args : true, links : true),
)

cxbx_hle_dispatch = static_library(
  'cxbx_hle_dispatch',
  'src/hle/dispatch/hle_database.cpp',
  'src/hle/dispatch/kernel_thunk.cpp',
  include_directories : [
    cxbx_win32_inc,
    cxbx_krnl_inc,
    openxdk_inc,
    cxbx_dxsdk_inc,
  ],
  dependencies : cxbx_trace_dep,
  cpp_args : [
    '-D_CXBXKRNL_INTERNAL',
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
)

cxbx_hle_dispatch_dep = declare_dependency(
  link_whole : cxbx_hle_dispatch,
  dependencies : cxbx_trace_dep.partial_dependency(links : true),
)

cxbx_yuy2 = static_library(
  'cxbx_yuy2',
  'src/core/yuy2_converter.cpp',
  include_directories : cxbx_public_inc,
)

cxbx_yuy2_dep = declare_dependency(
  link_with : cxbx_yuy2,
  include_directories : cxbx_public_inc,
)

cxbx_launcher_src = files(
  'src/platform/win32/shared_controller_config.cpp',
  'src/platform/win32/shared_runtime_state_launcher.cpp',
  'src/platform/win32/shared_video_config.cpp',
  'src/frontend/win32_legacy/controller_config_dialog.cpp',
  'src/frontend/win32_legacy/video_config_dialog.cpp',
  'src/frontend/win32_legacy/xbe_to_pe_converter.cpp',
  'src/frontend/win32_legacy/executable_prolog.cpp',
  'src/frontend/win32_legacy/win_main.cpp',
  'src/frontend/win32_legacy/window.cpp',
  'src/frontend/win32_legacy/about_window.cpp',
  'src/frontend/win32_legacy/main_window.cpp',
)

cxbx_frontend_win32 = static_library(
  'cxbx_frontend_win32',
  cxbx_launcher_src,
  dependencies : [
    cxbx_platform_win32_dep,
    cxbx_core_dep,
  ],
  cpp_args : [
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
)

cxbx_frontend_win32_dep = declare_dependency(
  link_whole : cxbx_frontend_win32,
  dependencies : [
    cxbx_platform_win32_dep,
    cxbx_core_dep,
  ],
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

cxbx_vsh_private_inc = include_directories('src/hw/nv2a/vsh')

cxbx_vsh_components = static_library(
  'cxbx_vsh_components',
  files(
    'src/hw/nv2a/vsh/vsh_cpu_device_state.cpp',
    'src/hw/nv2a/vsh/vsh_shader_creation.cpp',
    'src/hw/nv2a/vsh/vsh_shader_registry.cpp',
  ),
  include_directories : cxbx_public_inc,
)

cxbx_vsh_components_dep = declare_dependency(
  link_with : cxbx_vsh_components,
  include_directories : cxbx_public_inc,
)

cxbx_vsh_runtime = static_library(
  'cxbx_vsh_runtime',
  files(
    'src/hw/nv2a/vsh/vsh_decoder.cpp',
    'src/hw/nv2a/vsh/vsh_decoder_legacy.cpp',
    'src/hw/nv2a/vsh/vsh_decoder_runtime.cpp',
  ),
  include_directories : [
    cxbx_public_inc,
    cxbx_win32_inc,
    cxbx_krnl_inc,
    openxdk_inc,
  ],
  cpp_args : [
    '-D_CXBXKRNL_INTERNAL',
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
)

cxbx_vsh_runtime_dep = declare_dependency(
  link_whole : cxbx_vsh_runtime,
  include_directories : cxbx_public_inc,
)

cxbx_vsh_host = static_library(
  'cxbx_vsh_host',
  files(
    'src/hw/nv2a/vsh/vsh_decoder.cpp',
    'src/hw/nv2a/vsh/vsh_decoder_legacy.cpp',
    'src/hw/nv2a/vsh/vsh_decoder_host.cpp',
  ),
  include_directories : cxbx_public_inc,
  cpp_args : '-DCXBX_VSH_HOST_TEST',
)

cxbx_vsh_host_dep = declare_dependency(
  link_with : cxbx_vsh_host,
  compile_args : '-DCXBX_VSH_HOST_TEST',
  dependencies : cxbx_vsh_components_dep,
  include_directories : [cxbx_public_inc, cxbx_vsh_private_inc],
)

cxbx_d3d8_public_inc = [
  cxbx_public_inc,
  cxbx_win32_inc,
  cxbx_krnl_inc,
  cxbx_dxsdk_inc,
]

cxbx_hle_d3d8 = static_library(
  'cxbx_hle_d3d8',
  'src/hle/d3d8/d3d8_emulation.cpp',
  'src/hle/d3d8/d3d8_conversion.cpp',
  include_directories : cxbx_d3d8_public_inc + [cxbx_vsh_private_inc, openxdk_inc],
  cpp_args : [
    '-D_CXBXKRNL_INTERNAL',
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
)

cxbx_hle_d3d8_dep = declare_dependency(
  link_whole : cxbx_hle_d3d8,
  link_with : d3d8_import_lib,
  dependencies : [
    cxbx_guest_tls_win32_dep.partial_dependency(link_args : true, links : true),
    cxbx_hle_input_dep.partial_dependency(link_args : true, links : true),
    cxbx_hle_xg_dep.partial_dependency(link_args : true, links : true),
    cxbx_platform_win32_dep.partial_dependency(link_args : true, links : true),
    cxbx_trace_dep.partial_dependency(link_args : true, links : true),
    cxbx_vsh_components_dep.partial_dependency(link_args : true, links : true),
    cxbx_vsh_runtime_dep.partial_dependency(link_args : true, links : true),
    cxbx_yuy2_dep.partial_dependency(link_args : true, links : true),
  ],
  link_args : [
    '-lddraw',
    '-lwinmm',
  ],
  include_directories : cxbx_d3d8_public_inc,
)

cxbx_hle_runtime = static_library(
  'cxbx_hle_runtime',
  'src/hle/runtime/emulation_runtime.cpp',
  include_directories : openxdk_inc,
  dependencies : [
    cxbx_core_dep,
    cxbx_host_input_win32_dep,
    cxbx_nv2a_capture_dep,
    cxbx_platform_win32_dep,
    cxbx_trace_dep,
  ],
  cpp_args : [
    '-D_CXBXKRNL_INTERNAL',
    '-DXBOXKRNL_NO_EXTERN_C',
    '-Wno-c++11-narrowing',
  ],
)

cxbx_hle_runtime_dep = declare_dependency(
  link_whole : cxbx_hle_runtime,
  dependencies : [
    cxbx_core_dep,
    cxbx_guest_tls_win32_dep,
    cxbx_hle_d3d8_dep,
    cxbx_hle_dispatch_dep,
    cxbx_hle_kernel_dep,
    cxbx_hle_xapi_dep,
    cxbx_host_input_win32_dep,
    cxbx_nv2a_capture_dep,
    cxbx_platform_win32_dep,
    cxbx_trace_dep,
    cxbx_vsh_runtime_dep,
  ],
)

resource_compiler = find_program('llvm-rc')
resource_root = join_paths(meson.project_source_root(), 'Resource')
branding_root = join_paths(meson.project_source_root(), 'docs', 'branding')
cxbx_runtime_resources = custom_target(
  'cxbx_runtime_resources',
  input : files('../../Resource/runtime_resources.rc'),
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
    '../../include/cxbx/include/win32/cxbxkrnl/runtime_resource_ids.gen.h',
  ),
)
cxbx_launcher_resources = custom_target(
  'cxbx_launcher_resources',
  input : files('../../Resource/launcher_resources.rc'),
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
    'src/frontend/win32_legacy/launcher_resource_ids.gen.h',
    '../../Resource/Splash.bmp',
  ),
)

cxbx_runtime = shared_library(
  'Cxbx',
  cxbx_runtime_resources,
  dependencies : [
    cxbx_hle_audio_dep,
    cxbx_hle_d3d8_dep,
    cxbx_hle_dispatch_dep,
    cxbx_hle_kernel_dep,
    cxbx_hle_runtime_dep,
    cxbx_hle_xapi_dep,
    cxbx_hle_xonline_dep,
    cxbx_hle_xg_dep,
    cxbx_hle_input_dep,
    cxbx_guest_tls_win32_dep,
    cxbx_platform_win32_dep,
    cxbx_core_dep,
    cxbx_nv2a_capture_dep,
    cxbx_trace_dep,
    cxbx_vsh_runtime_dep,
    cxbx_yuy2_dep,
  ],
  link_with : cxbx_link_libs,
  link_args : [
    '-Wl,/BASE:0x10000000',
    '-Wl,/DYNAMICBASE:NO',
  ],
  name_prefix : '',
)

executable(
  'cxbx',
  cxbx_launcher_resources,
  dependencies : cxbx_frontend_win32_dep,
  link_with : [
    cxbx_runtime,
    d3d8_import_lib,
    cxbx_link_libs,
  ],
  win_subsystem : 'windows',
)
