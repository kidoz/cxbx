// Builds the 20-byte XID (Xbox controller) input reports the OHCI
// transfer-descriptor engine's stub gamepad delivers on its interrupt-IN
// endpoint (and to XID GET_REPORT), from the host XInput backend or the
// injected-state environment variables.
#include "xid_input_report.h"

#include "host_input.h"

#define POINTER_64 __ptr64
#include <windows.h>

#include <cstdio>
#include <cstring>

namespace
{
// XID input report layout: bReportId 0, bLength 0x14, a 16-bit little-endian
// digital-button mask, eight 8-bit analog buttons in A, B, X, Y, Black, White,
// left-trigger, right-trigger order (exactly GamepadState::analogButtons), then
// four 16-bit little-endian stick axes (LX, LY, RX, RY).
void BuildXidReport(const HostInput::GamepadState& state,
                    unsigned char report[EmuXidInputReportSize])
{
    report[0] = 0;
    report[1] = EmuXidInputReportSize;
    report[2] = static_cast<unsigned char>(state.buttons & 0xFFu);
    report[3] = static_cast<unsigned char>((state.buttons >> 8) & 0xFFu);
    for(std::size_t i = 0; i < state.analogButtons.size(); ++i)
    {
        report[4 + i] = state.analogButtons[i];
    }
    const std::int16_t axes[4] = { state.leftThumbX, state.leftThumbY,
                                   state.rightThumbX, state.rightThumbY };
    for(std::size_t i = 0; i < 4; ++i)
    {
        const std::uint16_t value = static_cast<std::uint16_t>(axes[i]);
        report[12 + i * 2] = static_cast<unsigned char>(value & 0xFFu);
        report[13 + i * 2] = static_cast<unsigned char>(value >> 8);
    }
}

enum class InjectionMode
{
    Unchecked,
    Off,
    Static,
    Sequence,
};

// Single writer (the USB delivery thread refreshing) with readers on arbitrary
// threads; the lock serializes whole-report copies so a reader never sees a
// torn report.
SRWLOCK g_ReportLock = SRWLOCK_INIT;
unsigned char g_Reports[HostInput::MaxPorts][EmuXidInputReportSize];
bool g_ReportsSeeded = false;

// The injection state mirrors EmuXInputInjectState (xapi_emulation.cpp) so one
// CXBX_INPUT_STATE / CXBX_INPUT_STATE_SEQUENCE setting drives the HLE XInput
// path and the USB stub gamepad identically. Only the refresh thread touches
// these, so they need no locking.
InjectionMode g_InjectionMode = InjectionMode::Unchecked;
HostInput::GamepadState g_InjectedStatic{};
HostInput::InjectedGamepadSequence g_InjectedSequence;
DWORD g_SequenceStartTick = 0;

void SeedNeutralReportsLocked()
{
    if(g_ReportsSeeded)
    {
        return;
    }
    const HostInput::GamepadState neutral{};
    for(std::size_t port = 0; port < HostInput::MaxPorts; ++port)
    {
        BuildXidReport(neutral, g_Reports[port]);
    }
    g_ReportsSeeded = true;
}

void DetectInjectionMode()
{
    char sequenceText[4096] = {};
    const DWORD sequenceLength = GetEnvironmentVariableA(
        "CXBX_INPUT_STATE_SEQUENCE", sequenceText, sizeof(sequenceText));
    if(sequenceLength != 0 && sequenceLength < sizeof(sequenceText) &&
       g_InjectedSequence.Parse(sequenceText))
    {
        g_InjectionMode = InjectionMode::Sequence;
        g_SequenceStartTick = GetTickCount();
        printf("Emu (0x%lX): USB XID timed input injection active (%zu frames).\n",
               GetCurrentThreadId(), g_InjectedSequence.Size());
        fflush(stdout);
        return;
    }

    char stateText[256] = {};
    const DWORD stateLength =
        GetEnvironmentVariableA("CXBX_INPUT_STATE", stateText, sizeof(stateText));
    if(stateLength != 0 && stateLength < sizeof(stateText) &&
       HostInput::ParseInjectedGamepadState(stateText, g_InjectedStatic))
    {
        g_InjectionMode = InjectionMode::Static;
        printf("Emu (0x%lX): USB XID input injection active (wButtons=0x%04X).\n",
               GetCurrentThreadId(), g_InjectedStatic.buttons);
        fflush(stdout);
        return;
    }

    g_InjectionMode = InjectionMode::Off;
}
} // namespace

extern "C" void EmuXidRefreshInputReports(unsigned long PortCount)
{
    if(g_InjectionMode == InjectionMode::Unchecked)
    {
        DetectInjectionMode();
    }
    if(PortCount > HostInput::MaxPorts)
    {
        PortCount = HostInput::MaxPorts;
    }

    // Only Poll() ports the backend reports connected: XInputGetState on a
    // DISCONNECTED port re-enumerates devices and is slow enough that calling
    // it 125 times per second starves the whole process. The connection
    // snapshot probes disconnected ports at most once per second.
    std::uint32_t connectedMask = 0;
    if(g_InjectionMode == InjectionMode::Off && HostInput::IsInitialized())
    {
        connectedMask = HostInput::GetConnectionSnapshot(true, false).currentMask;
    }

    unsigned char reports[HostInput::MaxPorts][EmuXidInputReportSize];
    for(std::size_t port = 0; port < HostInput::MaxPorts; ++port)
    {
        HostInput::GamepadState state{};
        if(port < PortCount)
        {
            if(g_InjectionMode == InjectionMode::Static)
            {
                state = g_InjectedStatic;
            }
            else if(g_InjectionMode == InjectionMode::Sequence)
            {
                state = g_InjectedSequence.StateAt(GetTickCount() -
                                                   g_SequenceStartTick);
            }
            else if((connectedMask & (1u << port)) == 0 ||
                    !HostInput::Poll(static_cast<std::uint32_t>(port), state))
            {
                state = HostInput::GamepadState{}; // disconnected: neutral
            }
        }
        BuildXidReport(state, reports[port]);
    }

    AcquireSRWLockExclusive(&g_ReportLock);
    memcpy(g_Reports, reports, sizeof(g_Reports));
    g_ReportsSeeded = true;
    ReleaseSRWLockExclusive(&g_ReportLock);
}

extern "C" void EmuXidGetInputReport(unsigned long Port,
                                     unsigned char Report[EmuXidInputReportSize])
{
    if(Report == NULL)
    {
        return;
    }
    if(Port >= HostInput::MaxPorts)
    {
        Port = 0;
    }

    AcquireSRWLockExclusive(&g_ReportLock);
    SeedNeutralReportsLocked();
    memcpy(Report, g_Reports[Port], EmuXidInputReportSize);
    ReleaseSRWLockExclusive(&g_ReportLock);
}
