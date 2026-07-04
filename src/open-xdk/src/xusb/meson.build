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
  # ohci-hcd.c is the only real translation unit here; it textually #includes
  # ohci-hub.c, ohci-dbg.c, ohci-mem.c, ohci-q.c and ohci-pci.c. Those must NOT
  # be compiled standalone (they have no includes of their own and reference the
  # complete struct ohci_hcd only visible inside ohci-hcd.c).
  'src/host/ohci-hcd.c',
  'src/sys/BootUSB.c',
  'src/sys/linuxwrapper.c',
  'src/sys/usbwrapper.c',
  'src/sys/xpad.c',
  # NOTE: src/usb_module.c is intentionally excluded. It is a Linux-kernel test
  # module (module_init/MODULE_LICENSE, pci_find_device, kernel_thread, and
  # <linux/module.h> etc. that do not exist in this tree) and it duplicates the
  # zxmalloc/zxprintf/zxsprintf definitions that usbwrapper.c already provides.
)

libxusb = static_library(
  'xusb',
  xusb_src,
  include_directories : [openxdk_inc, openxdk_xusb_inc],
  # This is a Linux-kernel-derived OHCI driver: it uses GNU C idioms (bare
  # `typeof` in list_entry/container_of, statement-expressions, named variadic
  # macros). Build it in a GNU dialect so clang accepts them.
  override_options : ['c_std=gnu17'],
)
