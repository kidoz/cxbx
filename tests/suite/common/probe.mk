# Shared nxdk build fragment for xtest conformance probes.
#
# A probe's Makefile sets XBE_TITLE and then includes this file:
#     XBE_TITLE = xtest_smoke
#     include ../../common/probe.mk
#
# NXDK_DIR may be overridden on the make command line (the runner does this).

NXDK_DIR     ?= D:/projects/nxdk
XTEST_COMMON := $(CURDIR)/../../common

SRCS += $(CURDIR)/main.c
SRCS += $(XTEST_COMMON)/xtrace.c
SRCS += $(XTEST_COMMON)/xtest.c

# Make the harness headers visible to the probe and the harness sources.
CFLAGS += -I$(XTEST_COMMON)

include $(NXDK_DIR)/Makefile
