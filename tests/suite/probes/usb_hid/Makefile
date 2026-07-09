XBE_TITLE = xtest_usb_hid

# nxdk's USB host stack (libusbohci, incl. the HID class driver) lives in the
# prebuilt nxdk_usb.lib; link it and expose its headers.
NXDK_DIR ?= D:/projects/nxdk

XTEST_COMMON := $(CURDIR)/../../common
SRCS += $(CURDIR)/main.c
SRCS += $(XTEST_COMMON)/xtrace.c
SRCS += $(XTEST_COMMON)/xtest.c
CFLAGS += -I$(XTEST_COMMON)
CFLAGS += -I$(NXDK_DIR)/lib/usb/libusbohci/inc
CFLAGS += -I$(NXDK_DIR)/lib/usb/libusbohci_xbox

include $(NXDK_DIR)/Makefile

# Link the USB stack (its rule/target lives in lib/usb; the lib is prebuilt).
main.exe: $(NXDK_DIR)/lib/nxdk_usb.lib
