// Host-input-backed 20-byte XID (Xbox controller) input reports for the OHCI
// transfer-descriptor engine's stub gamepad.
#ifndef XID_INPUT_REPORT_H
#define XID_INPUT_REPORT_H

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        EmuXidInputReportSize = 20
    };

    // Rebuild the cached per-port reports from the host XInput backend or the
    // CXBX_INPUT_STATE / CXBX_INPUT_STATE_SEQUENCE injection variables. PortCount
    // limits how many root-hub ports are actively polled (the rest stay neutral).
    // Call only from a host-FS thread (the USB delivery thread): the backend takes
    // locks and calls into the host XInput DLL.
    void EmuXidRefreshInputReports(unsigned long PortCount);

    // Copy the latest cached report for one root-hub port. Safe from any thread,
    // including guest threads servicing MMIO inside the exception handler: it only
    // takes a lightweight lock and copies bytes.
    void EmuXidGetInputReport(unsigned long Port,
                              unsigned char Report[EmuXidInputReportSize]);

#ifdef __cplusplus
}
#endif

#endif
