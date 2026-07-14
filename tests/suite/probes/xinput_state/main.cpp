// xinput_state -- end-to-end XAPI input HLE with headless injection: the
// runner sets CXBX_INPUT_STATE (see probe.toml), the emulator's
// EmuXInputGetState returns that synthetic pad state instead of polling host
// the host controller backend, and the probe asserts the exact injected values arrive
// through the genuine XDK 5849 xapilib entry points.
//
// Injected state (probe.toml must match):
//   CXBX_INPUT_STATE = "0x0004,255,0,0,0,0,0,0,0"
//   -> wButtons = 0x0004 (D-pad left), analog A = 255.
// This is the same mechanism scripted menu navigation will use.
#include "xdk_xtrace.h"

#define EXPECT_BUTTONS 0x0004
#define EXPECT_A       255

void __cdecl main()
{
    xt_begin("xinput_state");

    // Device enumeration: the HLE reports one gamepad on port 0.
    DWORD devices = XGetDevices(XDEVICE_TYPE_GAMEPAD);
    xt_chk("xi.getdevices_port0", 1, (devices & 1) != 0);

    DWORD ins = 0xFFFFFFFF, rem = 0xFFFFFFFF;
    BOOL changed = XGetDeviceChanges(XDEVICE_TYPE_GAMEPAD, &ins, &rem);
    xt_chk("xi.changes_after_snapshot", 1,
           !changed && ins == 0 && rem == 0);

    HANDLE hPad = XInputOpen(XDEVICE_TYPE_GAMEPAD, XDEVICE_PORT0,
                             XDEVICE_NO_SLOT, NULL);
    xt_chk("xi.open_ok", 1, hPad != NULL);
    if(hPad == NULL)
    {
        xt_end_and_exit();
    }

    XINPUT_CAPABILITIES caps;
    ZeroMemory(&caps, sizeof(caps));
    xt_chk("xi.caps_ok", 1,
           XInputGetCapabilities(hPad, &caps) == ERROR_SUCCESS &&
               caps.SubType == XINPUT_DEVSUBTYPE_GC_GAMEPAD);
    xt_chk("xi.caps_analog", 1,
           caps.In.Gamepad.bAnalogButtons[0] == 255 &&
               caps.In.Gamepad.bAnalogButtons[6] == 255 &&
               caps.In.Gamepad.bAnalogButtons[7] == 255);

    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(state));
    DWORD ret = XInputGetState(hPad, &state);
    xt_chk("xi.getstate_ok", 1, ret == ERROR_SUCCESS);
    xt_emitf("EV   xi.state packet=%u wButtons=0x%04X A=%u",
             state.dwPacketNumber, state.Gamepad.wButtons,
             state.Gamepad.bAnalogButtons[0]);
    xt_chk("xi.buttons_injected", 1, state.Gamepad.wButtons == EXPECT_BUTTONS);
    xt_chk("xi.analog_a_injected", 1,
           state.Gamepad.bAnalogButtons[0] == EXPECT_A);

    // The packet number must advance between polls (titles use it to detect
    // fresh input).
    DWORD packet1 = state.dwPacketNumber;
    XInputGetState(hPad, &state);
    xt_chk("xi.packet_advances", 1, state.dwPacketNumber != packet1);

    ins = 0;
    rem = 0;
    changed = XGetDeviceChanges(XDEVICE_TYPE_GAMEPAD, &ins, &rem);
    xt_chk("xi.changes_removal", 1,
           changed && ins == 0 && (rem & 1) != 0);
    xt_chk("xi.removed_handle_disconnected", 1,
           XInputGetState(hPad, &state) == ERROR_DEVICE_NOT_CONNECTED);

    ins = 0;
    rem = 0;
    changed = XGetDeviceChanges(XDEVICE_TYPE_GAMEPAD, &ins, &rem);
    xt_chk("xi.changes_reinsertion", 1,
           changed && (ins & 1) != 0 && rem == 0);
    xt_chk("xi.stale_handle_disconnected", 1,
           XInputGetState(hPad, &state) == ERROR_DEVICE_NOT_CONNECTED);

    HANDLE hReconnectedPad = XInputOpen(XDEVICE_TYPE_GAMEPAD, XDEVICE_PORT0,
                                        XDEVICE_NO_SLOT, NULL);
    xt_chk("xi.reopen_ok", 1, hReconnectedPad != NULL);
    xt_chk("xi.reopen_new_generation", 1,
           hReconnectedPad != NULL && hReconnectedPad != hPad);
    xt_chk("xi.reconnected_state", 1,
           hReconnectedPad != NULL &&
               XInputGetState(hReconnectedPad, &state) == ERROR_SUCCESS);

    XINPUT_FEEDBACK feedback;
    ZeroMemory(&feedback, sizeof(feedback));
    feedback.Header.dwStatus = 0xFFFFFFFF;
    ret = XInputSetState(hReconnectedPad, &feedback);
    xt_chk("xi.rumble_zero", 1,
           ret == ERROR_SUCCESS && feedback.Header.dwStatus == ERROR_SUCCESS);

    XInputClose(hReconnectedPad);
    XInputClose(hPad);
    xt_chk("xi.close_survived", 1, 1);

    xt_end_and_exit();
}
