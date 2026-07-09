// SPDX-License-Identifier: MIT
//
// usb_hid - USB HID enumeration over the raw OHCI stack (nxdk libusbohci), the
// exact path NestopiaX 1.0 nxdk uses for its gamepad. It brings up the host
// controller, connects the OHCI ISR, and pumps usbh_pooling_hubs() -- the app-
// side hub poll that resets a connected port and runs the enumeration control
// transfers -- watching for a HID device to appear on the root hub.
//
// A target that models a USB device on the root hub plus OHCI transfer
// execution enumerates one and usbh_hid_get_device_list() returns it; a target
// without that (this Cxbx has OHCI register semantics + a synthesized SOF
// interrupt, but no device/transfer engine) enumerates nothing, so the check
// records the gap. Tagged "usbhid" so targets lacking it skip it.

#include "xtest.h"
#include <windows.h>
#include <stdint.h>
#include <usbh_lib.h>
#include <usbh_hid.h>

static volatile int g_connects = 0;

static void on_connect(struct udev_t *udev, int param)    { (void)udev; (void)param; g_connects++; }
static void on_disconnect(struct udev_t *udev, int param) { (void)udev; (void)param; }

int main(void)
{
    xt_begin("v1", "usb_hid");
    xt_note("USB HID enumeration via libusbohci (raw OHCI, NestopiaX 1.0 path)");
    xt_note("requires a modeled root-hub device + OHCI transfer execution");

    // Bring up the host controller + connect the OHCI interrupt (the ISR that
    // queues the DPC running the frame/done-queue processing).
    usbh_core_init();
    usbh_install_conn_callback((CONN_FUNC *)on_connect, (CONN_FUNC *)on_disconnect);
    usbh_ohci_irq_init();
    xt_check_bool("usb_hid.core_init", 1, 1);   // reaching here = the stack initialized

    // Pump the hub poll (~2 s worth). This is where a connected port is reset
    // and the device is enumerated over control transfers.
    struct usbhid_dev *hid = NULL;
    int polls = 0;
    for (int i = 0; i < 500 && hid == NULL; i++)
    {
        usbh_pooling_hubs();
        polls++;
        hid = usbh_hid_get_device_list();
        Sleep(4);
    }

    xt_ev("usb_hid.polls=%d connects=%d hid_list=0x%08lX",
          polls, g_connects, (unsigned long)(uintptr_t)hid);
    xt_check_bool("usb_hid.device_enumerated", 1, hid != NULL);

    usbh_core_deinit();
    return xt_end();
}
