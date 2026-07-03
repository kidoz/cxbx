xhal_src = files(
  'src/xfile.c',
  'src/xnvidia.c',
  'src/xohci.c',
  'src/xohci_pci.c',
  'src/xstartup.c',
  'src/xsysfont.c',
  'src/xvga.c',
  'src/xvga_sys.c',
)

libxhal = static_library(
  'xhal',
  xhal_src,
  include_directories : openxdk_inc,
)
