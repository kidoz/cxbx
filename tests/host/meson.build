if not build_host_asan
  launcher_session_test = executable(
    'launcher_session_test',
    'launcher_session.cpp',
    dependencies : [cxbx_launcher_service_dep, cxbx_core_dep,
      cxbx_platform_win32_dep.partial_dependency(includes : true)],
    link_with : [cxbx_runtime, d3d8_import_lib],
  )
  launcher_test_env = environment()
  launcher_test_env.prepend('PATH', meson.project_build_root() / 'src/cxbx')
  test('host-launcher-session', import('python').find_installation(),
    args : [files('run_launcher_session.py'), launcher_session_test],
    env : launcher_test_env,
  )
  test('launcher-ui-smoke', cxbx_launcher, args : ['--ui-smoke-test'],
    suite : 'ui', timeout : 20, is_parallel : false)
  test('launcher-ui-document', import('python').find_installation(),
    args : [files('run_launcher_session.py'), launcher_session_test, cxbx_launcher],
    env : launcher_test_env, suite : 'ui', timeout : 30, is_parallel : false)

  host_input_test = executable(
    'host_input_test',
    'input_translation.cpp',
    dependencies : cxbx_host_input_win32_dep,
  )

  test('host-input-translation', host_input_test)
endif

host_yuy2_test = executable(
  'host_yuy2_test',
  'yuy2_conversion.cpp',
  dependencies : cxbx_yuy2_dep,
)

host_nv2a_device_state_test = executable(
  'host_nv2a_device_state_test',
  'nv2a_device_state.cpp',
  dependencies : cxbx_nv2a_device_dep,
  install : false,
)

host_nv2a_pfifo_test = executable(
  'host_nv2a_pfifo_test',
  'nv2a_pfifo.cpp',
  dependencies : cxbx_nv2a_pfifo_dep,
  install : false,
)

host_nv2a_pgraph_surface_test = executable(
  'host_nv2a_pgraph_surface_test',
  'nv2a_pgraph_surface.cpp',
  dependencies : cxbx_nv2a_pgraph_state_dep,
  install : false,
)

host_nv2a_pgraph_render_state_test = executable(
  'host_nv2a_pgraph_render_state_test',
  'nv2a_pgraph_render_state.cpp',
  dependencies : cxbx_nv2a_pgraph_state_dep,
  install : false,
)

host_nv2a_pgraph_combiner_state_test = executable(
  'host_nv2a_pgraph_combiner_state_test',
  'nv2a_pgraph_combiner_state.cpp',
  dependencies : cxbx_nv2a_pgraph_state_dep,
  install : false,
)

host_nv2a_pgraph_draw_state_test = executable(
  'host_nv2a_pgraph_draw_state_test',
  'nv2a_pgraph_draw_state.cpp',
  dependencies : cxbx_nv2a_pgraph_state_dep,
  install : false,
)

host_nv2a_pgraph_vertex_submission_state_test = executable(
  'host_nv2a_pgraph_vertex_submission_state_test',
  'nv2a_pgraph_vertex_submission_state.cpp',
  dependencies : cxbx_nv2a_pgraph_state_dep,
  install : false,
)

host_nv2a_pgraph_texture_state_test = executable(
  'host_nv2a_pgraph_texture_state_test',
  'nv2a_pgraph_texture_state.cpp',
  dependencies : cxbx_nv2a_pgraph_state_dep,
  install : false,
)

host_nv2a_pgraph_vertex_state_test = executable(
  'host_nv2a_pgraph_vertex_state_test',
  'nv2a_pgraph_vertex_state.cpp',
  dependencies : cxbx_nv2a_pgraph_state_dep,
  install : false,
)

host_nv2a_pgraph_transform_state_test = executable(
  'host_nv2a_pgraph_transform_state_test',
  'nv2a_pgraph_transform_state.cpp',
  dependencies : cxbx_nv2a_pgraph_state_dep,
  install : false,
)

host_nv2a_pgraph_semaphore_state_test = executable(
  'host_nv2a_pgraph_semaphore_state_test',
  'nv2a_pgraph_semaphore_state.cpp',
  dependencies : cxbx_nv2a_pgraph_state_dep,
  install : false,
)

host_nv2a_pgraph_flip_state_test = executable(
  'host_nv2a_pgraph_flip_state_test',
  'nv2a_pgraph_flip_state.cpp',
  dependencies : cxbx_nv2a_pgraph_state_dep,
  install : false,
)

if build_host_asan
  test(
    'host-yuy2-conversion',
    host_yuy2_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  test(
    'host-nv2a-device-state',
    host_nv2a_device_state_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  test(
    'host-nv2a-pfifo',
    host_nv2a_pfifo_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  test(
    'host-nv2a-pgraph-surface',
    host_nv2a_pgraph_surface_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  test(
    'host-nv2a-pgraph-render-state',
    host_nv2a_pgraph_render_state_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  test(
    'host-nv2a-pgraph-combiner-state',
    host_nv2a_pgraph_combiner_state_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  test(
    'host-nv2a-pgraph-draw-state',
    host_nv2a_pgraph_draw_state_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  test(
    'host-nv2a-pgraph-vertex-submission-state',
    host_nv2a_pgraph_vertex_submission_state_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  test(
    'host-nv2a-pgraph-texture-state',
    host_nv2a_pgraph_texture_state_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  test(
    'host-nv2a-pgraph-vertex-state',
    host_nv2a_pgraph_vertex_state_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  test(
    'host-nv2a-pgraph-transform-state',
    host_nv2a_pgraph_transform_state_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  test(
    'host-nv2a-pgraph-semaphore-state',
    host_nv2a_pgraph_semaphore_state_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  test(
    'host-nv2a-pgraph-flip-state',
    host_nv2a_pgraph_flip_state_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  host_pcm_test = executable(
    'host_pcm_test',
    'pcm_conversion.cpp',
    dependencies : cxbx_pcm_dep,
  )

  test(
    'host-pcm-conversion',
    host_pcm_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  host_nv2a_capture_test = executable(
    'host_nv2a_capture_test',
    'nv2a_capture.cpp',
    dependencies : cxbx_nv2a_capture_dep,
    install : false,
  )

  test(
    'host-nv2a-capture',
    host_nv2a_capture_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  host_d3d_present_test = executable(
    'host_d3d_present_test',
    'd3d_present.cpp',
    include_directories : cxbx_public_inc,
    install : false,
  )

  test(
    'host-d3d-present',
    host_d3d_present_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  host_d3d_push_buffer_test = executable(
    'host_d3d_push_buffer_test',
    'd3d_push_buffer.cpp',
    include_directories : cxbx_public_inc,
    install : false,
  )

  test(
    'host-d3d-push-buffer',
    host_d3d_push_buffer_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  host_nv2a_raster_test = executable(
    'host_nv2a_raster_test',
    'nv2a_raster.cpp',
    include_directories : cxbx_public_inc,
    install : false,
  )

  test(
    'host-nv2a-raster',
    host_nv2a_raster_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  host_nvnet_test = executable(
    'host_nvnet_test',
    'nvnet.cpp',
    include_directories : cxbx_public_inc,
    install : false,
  )

  test(
    'host-nvnet',
    host_nvnet_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  host_psh_translate_test = executable(
    'host_psh_translate_test',
    'psh_translate.cpp',
    include_directories : cxbx_public_inc,
    install : false,
  )

  test(
    'host-psh-translate',
    host_psh_translate_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  host_xbox_adpcm_test = executable(
    'host_xbox_adpcm_test',
    'xbox_adpcm.cpp',
    dependencies : cxbx_xbox_adpcm_dep,
  )

  test(
    'host-xbox-adpcm',
    host_xbox_adpcm_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  host_vsh_recompiler_test = executable(
    'host_vsh_recompiler_test',
    'vsh_recompiler.cpp',
    dependencies : cxbx_vsh_host_dep,
  )

  test(
    'host-vsh-recompiler',
    host_vsh_recompiler_test,
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
  )

  host_asan_runtime_probe = executable(
    'host_asan_runtime_probe',
    'asan_runtime_probe.cpp',
    install : false,
  )

  python = import('python').find_installation()
  test(
    'host-asan-runtime',
    python,
    args : [
      files('asan_runtime_probe.py'),
      host_asan_runtime_probe,
    ],
    env : cxbx_host_asan_test_env,
    suite : 'sanitizer',
    timeout : 30,
  )

  subdir_done()
else
  test('host-yuy2-conversion', host_yuy2_test)
  test('host-nv2a-device-state', host_nv2a_device_state_test)
  test('host-nv2a-pfifo', host_nv2a_pfifo_test)
  test('host-nv2a-pgraph-surface', host_nv2a_pgraph_surface_test)
  test('host-nv2a-pgraph-render-state', host_nv2a_pgraph_render_state_test)
  test('host-nv2a-pgraph-combiner-state', host_nv2a_pgraph_combiner_state_test)
  test('host-nv2a-pgraph-draw-state', host_nv2a_pgraph_draw_state_test)
  test(
    'host-nv2a-pgraph-vertex-submission-state',
    host_nv2a_pgraph_vertex_submission_state_test,
  )
  test('host-nv2a-pgraph-texture-state', host_nv2a_pgraph_texture_state_test)
  test('host-nv2a-pgraph-vertex-state', host_nv2a_pgraph_vertex_state_test)
  test('host-nv2a-pgraph-transform-state', host_nv2a_pgraph_transform_state_test)
  test('host-nv2a-pgraph-semaphore-state', host_nv2a_pgraph_semaphore_state_test)
  test('host-nv2a-pgraph-flip-state', host_nv2a_pgraph_flip_state_test)
endif

host_d3d_present_test = executable(
  'host_d3d_present_test',
  'd3d_present.cpp',
  include_directories : cxbx_public_inc,
  install : false,
)

test('host-d3d-present', host_d3d_present_test)

host_d3d_push_buffer_test = executable(
  'host_d3d_push_buffer_test',
  'd3d_push_buffer.cpp',
  include_directories : cxbx_public_inc,
  install : false,
)

test('host-d3d-push-buffer', host_d3d_push_buffer_test)

host_nv2a_raster_test = executable(
  'host_nv2a_raster_test',
  'nv2a_raster.cpp',
  include_directories : cxbx_public_inc,
  install : false,
)

test('host-nv2a-raster', host_nv2a_raster_test)

host_nv2a_capture_test = executable(
  'host_nv2a_capture_test',
  'nv2a_capture.cpp',
  dependencies : cxbx_nv2a_capture_dep,
  install : false,
)

test('host-nv2a-capture', host_nv2a_capture_test)

host_nvnet_test = executable(
  'host_nvnet_test',
  'nvnet.cpp',
  include_directories : cxbx_public_inc,
  install : false,
)

test('host-nvnet', host_nvnet_test)

host_psh_translate_test = executable(
  'host_psh_translate_test',
  'psh_translate.cpp',
  include_directories : cxbx_public_inc,
  install : false,
)

test('host-psh-translate', host_psh_translate_test)

host_pcm_test = executable(
  'host_pcm_test',
  'pcm_conversion.cpp',
  dependencies : cxbx_pcm_dep,
)

test('host-pcm-conversion', host_pcm_test)

host_xbox_adpcm_test = executable(
  'host_xbox_adpcm_test',
  'xbox_adpcm.cpp',
  dependencies : cxbx_xbox_adpcm_dep,
)

test('host-xbox-adpcm', host_xbox_adpcm_test)

host_vsh_recompiler_test = executable(
  'host_vsh_recompiler_test',
  'vsh_recompiler.cpp',
  dependencies : cxbx_vsh_host_dep,
)

test('host-vsh-recompiler', host_vsh_recompiler_test)

host_trace_core_test = executable(
  'host_trace_core_test',
  'trace_core.cpp',
  dependencies : cxbx_trace_dep,
)

test('host-trace-core', host_trace_core_test)

host_trace_ring_test = executable(
  'host_trace_ring_test',
  'trace_ring.cpp',
  dependencies : cxbx_trace_dep,
)

test('host-trace-ring', host_trace_ring_test)

host_trace_benchmark = executable(
  'host_trace_benchmark',
  'trace_benchmark.cpp',
  '../../src/cxbx/src/core/trace/trace.cpp',
  '../../src/cxbx/src/platform/win32/trace_os.cpp',
  include_directories : cxbx_public_inc,
  cpp_args : '-DCXBX_TRACE_BENCH_OPTIMIZATION="3"',
  override_options : ['optimization=3', 'debug=false'],
)

python = import('python').find_installation()
test(
  'host-trace-benchmark',
  python,
  args : [
    files('../../tools/tracebench.py'),
    host_trace_benchmark,
    '--profile',
    files('../../tools/trace/benchmark_profile.toml'),
  ],
  suite : 'performance',
  timeout : 120,
)

host_trace_durability_helper = executable(
  'host_trace_durability_helper',
  'trace_durability_helper.cpp',
  dependencies : cxbx_trace_dep,
)

test(
  'host-trace-durability',
  python,
  args : [files('trace_durability.py'), host_trace_durability_helper],
  timeout : 30,
)

host_stack_precommit_test = executable(
  'host_stack_precommit_test',
  'stack_precommit.cpp',
  include_directories : cxbx_krnl_inc,
  install : false,
)

test('host-stack-precommit', host_stack_precommit_test)

if not build_host_asan
  host_fs_content_swap_policy_test = executable(
    'host_fs_content_swap_policy_test',
    'fs_content_swap_policy.cpp',
    include_directories : cxbx_krnl_inc,
    install : false,
  )

  test('host-fs-content-swap-policy', host_fs_content_swap_policy_test)
endif

# Host-backend selection contract (native-Vulkan migration P0). The parser
# lives header-inline in the d3d8 HLE so this test compiles the header
# directly without linking any backend code.
host_backend_test = executable(
  'host_backend_test',
  'host_backend.cpp',
  include_directories : [cxbx_public_inc,
                         include_directories('../../src/cxbx/src/hle/d3d8')],
  install : false,
)

test('host-host-backend', host_backend_test)

if not build_host_asan
  # Production GPU integration, opt-in through CXBX_TEST_VULKAN=1. No guest
  # runtime or D3D8 dependency; disabled runs report Meson's skip code 77.
  host_vulkan_renderer_test = executable(
    'host_vulkan_renderer_test',
    'vulkan_renderer.cpp',
    include_directories : include_directories('../../src/cxbx/src/hle/d3d8'),
    dependencies : [cxbx_hle_d3d8_vulkan_dep, meson.get_compiler('cpp').find_library('user32')],
  )
  foreach scenario : ['vertex', 'index', 'target', 'combiner', 'combiner_logic', 'depth', 'sampling', 'descriptors', 'raster', 'present']
    test('host-vulkan-' + scenario, host_vulkan_renderer_test,
      args : [scenario], suite : 'vulkan-gpu', timeout : 30, is_parallel : false)
  endforeach
endif
