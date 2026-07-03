xboxkrnl_src = files(
  'src/dbg.c',
  'src/ex.c',
  'src/hal.c',
  'src/io.c',
  'src/kernel.c',
  'src/mm.c',
  'src/nt.c',
  'src/ob.c',
  'src/ps.c',
  'src/rtl.c',
  'src/xbox.c',
)

libxboxkrnl = static_library(
  'xboxkrnl',
  xboxkrnl_src,
  include_directories : openxdk_inc,
)
