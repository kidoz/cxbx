xusb_src = files(
  'src/core/buffer_simple.c',
  'src/core/config.c',
  'src/core/hcd-pci.c',
  'src/core/hcd.c',
  'src/core/hub.c',
  'src/core/message.c',
  'src/core/urb.c',
  'src/core/usb-debug.c',
  'src/core/usb.c',
  'src/host/ohci-dbg.c',
  'src/host/ohci-hcd.c',
  'src/host/ohci-hub.c',
  'src/host/ohci-mem.c',
  'src/host/ohci-pci.c',
  'src/host/ohci-q.c',
  'src/sys/BootUSB.c',
  'src/sys/linuxwrapper.c',
  'src/sys/usbwrapper.c',
  'src/sys/xpad.c',
  'src/usb_module.c',
)

libxusb = static_library(
  'xusb',
  xusb_src,
  include_directories : openxdk_inc,
)
