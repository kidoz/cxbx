xlibc_src = files(
  'src/math/abort.c',
  'src/math/abs.c',
  'src/math/acos.c',
  'src/math/asin.c',
  'src/math/atan.c',
  'src/math/ceil.c',
  'src/math/div.c',
  'src/math/exp.c',
  'src/math/fabs.c',
  'src/math/floor.c',
  'src/math/fmod.c',
  'src/math/ftol.c',
  'src/math/hypot.c',
  'src/math/itoa.c',
  'src/math/ldiv.c',
  'src/math/log.c',
  'src/math/ltoa.c',
  # Use the host CRT allocator in the emulator build. xlibc's malloc/free
  # collide with the MSVC runtime that the C++ executable links.
  'src/math/misc_vars.c',
  'src/math/pow.c',
  'src/math/rand.c',
  'src/math/sincos.c',
  'src/math/sqrt.c',
  'src/math/stdio/conio.c',
  'src/math/stdio/file_io.c',
  # Use the host CRT string/printf routines in the emulator build. Several
  # legacy xlibc replacements use Xbox assumptions and corrupt host memory.
  'src/math/tan.c',
)

libxlibc = static_library(
  'xlibc',
  xlibc_src,
  include_directories : openxdk_inc,
)
