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
