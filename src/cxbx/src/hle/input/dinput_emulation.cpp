// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbxkrnl->dinput_emulation.cpp
// *
// *  This file is part of the cxbx project.
// *
// *  cxbx and cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file LICENSE.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *
// *  All rights reserved
// *
// ******************************************************************
#define _CXBXKRNL_INTERNAL
#define _XBOXKRNL_LOCAL_

#include "host_input.h"
#include "emulation_runtime.h"
#include "xbox_controller.h"
#include "emulation_window_access.h"
#include "host_input_lifecycle.h"
#include "shared_controller_config.h"

#include <cstring>
#include <mutex>

// ******************************************************************
// * Static Variable(s)
// ******************************************************************
static XBController g_XBController;
static bool g_DirectInputReady = false;
static XTL::XINPUT_STATE g_LastDirectInputState = {};
static DWORD g_DirectInputPacketNumber = 0;
static bool g_HasLastDirectInputState = false;
static bool g_DirectInputConnected = false;
static HostInput::ConnectionTracker g_EffectiveConnections;
static std::mutex g_DirectInputMutex;

static void EmuDInputStartLegacyUnlocked()
{
    cxbx::platform::GetSharedControllerConfig(g_XBController);
    g_XBController.ListenBegin(static_cast<HWND>(cxbx::platform::GetEmulationWindow()));

    if (g_XBController.GetError()) {
        printf("EmuDInput: legacy DirectInput initialization failed: %s\n",
               g_XBController.GetError());
        g_XBController.ListenEnd();
        g_DirectInputReady = false;
        g_DirectInputConnected = false;
        return;
    }

    g_DirectInputReady = true;
    g_DirectInputConnected = g_XBController.HasInputDevice();
    g_HasLastDirectInputState = false;
}

// ******************************************************************
// * func: XTL::EmuDInputInit
// ******************************************************************
bool XTL::EmuDInputInit()
{
    bool directInputReady = false;
    {
        std::lock_guard<std::mutex> lock(g_DirectInputMutex);
        EmuDInputStartLegacyUnlocked();
        directInputReady = g_DirectInputReady;
    }

    if (!HostInput::AttachWindow(cxbx::platform::GetEmulationWindow())) {
        printf("EmuDInput: host gamepad device notifications are unavailable; "
               "using polling fallback.\n");
    }
    return directInputReady || HostInput::IsInitialized();
}

// ******************************************************************
// * func: XTL::EmuDInputCleanup
// ******************************************************************
void XTL::EmuDInputCleanup()
{
    {
        std::lock_guard<std::mutex> lock(g_DirectInputMutex);
        if (g_DirectInputReady) {
            g_XBController.ListenEnd();
        }
        g_DirectInputReady = false;
        g_DirectInputConnected = false;
        g_HasLastDirectInputState = false;
        g_EffectiveConnections.Reset();
    }
    HostInput::Shutdown();
}

// ******************************************************************
// * func: XTL::EmuDInputNotifyDeviceChange
// ******************************************************************
void XTL::EmuDInputNotifyDeviceChange()
{
    HostInput::NotifyDeviceChange();

    std::lock_guard<std::mutex> lock(g_DirectInputMutex);
    if (g_DirectInputReady) {
        g_XBController.ListenEnd();
    }
    EmuDInputStartLegacyUnlocked();
}

// ******************************************************************
// * func: XTL::EmuDInputGetConnectionSnapshot
// ******************************************************************
void XTL::EmuDInputGetConnectionSnapshot(BOOL Refresh, BOOL ConsumeChanges,
                                         PDWORD CurrentMask, PDWORD ChangedMask,
                                         PDWORD Generations)
{
    const HostInput::ConnectionSnapshot host =
        HostInput::GetConnectionSnapshot(Refresh != FALSE, true);
    std::lock_guard<std::mutex> lock(g_DirectInputMutex);
    const DWORD fallbackMask = g_DirectInputConnected ? 1u : 0u;
    const DWORD effectiveMask = host.currentMask | fallbackMask;
    const DWORD visibleHostChanges = host.changedMask & ~fallbackMask;
    g_EffectiveConnections.Observe(effectiveMask, visibleHostChanges);

    const HostInput::ConnectionSnapshot effective = ConsumeChanges != FALSE
                                                        ? g_EffectiveConnections.Consume()
                                                        : g_EffectiveConnections.Snapshot();
    if (CurrentMask != nullptr) {
        *CurrentMask = effective.currentMask;
    }
    if (ChangedMask != nullptr) {
        *ChangedMask = effective.changedMask;
    }
    if (Generations != nullptr) {
        for (DWORD port = 0; port < HostInput::MaxPorts; ++port) {
            Generations[port] = effective.generations[port];
        }
    }
}

// ******************************************************************
// * func: XTL::EmuDInputGetConnectedMask
// ******************************************************************
DWORD XTL::EmuDInputGetConnectedMask()
{
    DWORD connectedMask = 0;
    EmuDInputGetConnectionSnapshot(TRUE, FALSE, &connectedMask, nullptr, nullptr);
    return connectedMask;
}

// ******************************************************************
// * func: XTL::EmuDInputPoll
// ******************************************************************
bool XTL::EmuDInputPoll(DWORD Port, XTL::PXINPUT_STATE Controller)
{
    if (Controller == nullptr || Port >= HostInput::MaxPorts) {
        return false;
    }

    HostInput::GamepadState hostState{};
    if (HostInput::Poll(Port, hostState)) {
        ZeroMemory(Controller, sizeof(*Controller));
        Controller->dwPacketNumber = hostState.packetNumber;
        Controller->Gamepad.wButtons = hostState.buttons;
        memcpy(Controller->Gamepad.bAnalogButtons, hostState.analogButtons.data(),
               hostState.analogButtons.size());
        Controller->Gamepad.sThumbLX = hostState.leftThumbX;
        Controller->Gamepad.sThumbLY = hostState.leftThumbY;
        Controller->Gamepad.sThumbRX = hostState.rightThumbX;
        Controller->Gamepad.sThumbRY = hostState.rightThumbY;
        return true;
    }

    std::lock_guard<std::mutex> lock(g_DirectInputMutex);
    if (Port != 0 || !g_DirectInputConnected) {
        return false;
    }

    XTL::XINPUT_STATE directInputState{};
    if (!g_XBController.ListenPoll(&directInputState)) {
        g_HasLastDirectInputState = false;
        return false;
    }

    if (g_XBController.GetError()) {
        MessageBox(NULL, g_XBController.GetError(), "cxbx [*UNHANDLED!*]", MB_OK);  // TODO: Handle this!
        return false;
    }

    if (!g_HasLastDirectInputState ||
        memcmp(&directInputState.Gamepad, &g_LastDirectInputState.Gamepad,
               sizeof(directInputState.Gamepad)) != 0) {
        ++g_DirectInputPacketNumber;
        g_LastDirectInputState = directInputState;
        g_HasLastDirectInputState = true;
    }

    directInputState.dwPacketNumber = g_DirectInputPacketNumber;
    *Controller = directInputState;
    return true;
}

DWORD XTL::EmuDInputSetState(DWORD Port, WORD LeftMotorSpeed, WORD RightMotorSpeed)
{
    DWORD result = HostInput::SetRumble(Port, LeftMotorSpeed, RightMotorSpeed);
    std::lock_guard<std::mutex> lock(g_DirectInputMutex);
    if (result == ERROR_DEVICE_NOT_CONNECTED && Port == 0 &&
        g_DirectInputConnected) {
        return ERROR_SUCCESS;
    }
    return result;
}
