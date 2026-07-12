host_input_test = executable(
  'host_input_test',
  'input_translation.cpp',
  '../../src/cxbx/src/win32/HostInput.cpp',
  include_directories : cxbx_inc,
)

test('host-input-translation', host_input_test)

host_yuy2_test = executable(
  'host_yuy2_test',
  'yuy2_conversion.cpp',
  '../../src/cxbx/src/core/Yuy2Converter.cpp',
  include_directories : cxbx_inc,
)

test('host-yuy2-conversion', host_yuy2_test)

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
  include_directories : cxbx_inc,
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
