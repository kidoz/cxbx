// Windows XInput backend for original Xbox controller emulation.
#include "HostInput.h"

#define POINTER_64 __ptr64
#include <windows.h>

#include <array>
#include <mutex>

namespace
{
constexpr std::size_t AnalogA = 0;
constexpr std::size_t AnalogB = 1;
constexpr std::size_t AnalogX = 2;
constexpr std::size_t AnalogY = 3;
constexpr std::size_t AnalogBlack = 4;
constexpr std::size_t AnalogWhite = 5;
constexpr std::size_t AnalogLeftTrigger = 6;
constexpr std::size_t AnalogRightTrigger = 7;

struct NativeXInputState
{
    DWORD packetNumber;
    HostInput::XInputGamepad gamepad;
};

struct NativeXInputVibration
{
    WORD leftMotorSpeed;
    WORD rightMotorSpeed;
};

static_assert(sizeof(HostInput::XInputGamepad) == 12,
              "The host XInput gamepad ABI must remain 12 bytes.");
static_assert(sizeof(NativeXInputState) == 16,
              "The host XInput state ABI must remain 16 bytes.");
static_assert(sizeof(NativeXInputVibration) == 4,
              "The host XInput vibration ABI must remain 4 bytes.");

using XInputGetStateFunction = DWORD(WINAPI*)(DWORD, NativeXInputState*);
using XInputSetStateFunction = DWORD(WINAPI*)(DWORD, NativeXInputVibration*);

HMODULE g_xinputModule = nullptr;
XInputGetStateFunction g_xinputGetState = nullptr;
XInputSetStateFunction g_xinputSetState = nullptr;
std::array<HostInput::GamepadState, HostInput::MaxPorts> g_lastState{};
std::array<bool, HostInput::MaxPorts> g_hasLastState{};
std::array<std::uint32_t, HostInput::MaxPorts> g_packetNumber{};
std::mutex g_mutex;

constexpr bool HasButton(std::uint16_t buttons, HostInput::XInputButton button)
{
    return (buttons & static_cast<std::uint16_t>(button)) != 0;
}

bool SameGamepad(const HostInput::GamepadState& left,
                 const HostInput::GamepadState& right)
{
    return left.buttons == right.buttons &&
           left.analogButtons == right.analogButtons &&
           left.leftThumbX == right.leftThumbX &&
           left.leftThumbY == right.leftThumbY &&
           left.rightThumbX == right.rightThumbX &&
           left.rightThumbY == right.rightThumbY;
}

bool InitializeUnlocked()
{
    if(g_xinputGetState != nullptr && g_xinputSetState != nullptr)
    {
        return true;
    }

    constexpr std::array<const char*, 3> DllNames = {
        "xinput1_4.dll",
        "xinput1_3.dll",
        "xinput9_1_0.dll",
    };

    for(const char* dllName : DllNames)
    {
        HMODULE module = LoadLibraryA(dllName);
        if(module == nullptr)
        {
            continue;
        }

        auto getState = reinterpret_cast<XInputGetStateFunction>(
            GetProcAddress(module, "XInputGetState"));
        auto setState = reinterpret_cast<XInputSetStateFunction>(
            GetProcAddress(module, "XInputSetState"));
        if(getState != nullptr && setState != nullptr)
        {
            g_xinputModule = module;
            g_xinputGetState = getState;
            g_xinputSetState = setState;
            return true;
        }

        FreeLibrary(module);
    }

    return false;
}
} // namespace

bool HostInput::Initialize()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return InitializeUnlocked();
}

void HostInput::TranslateXInputGamepad(const XInputGamepad& host,
                                       GamepadState& guest)
{
    guest = {};
    guest.buttons = host.buttons & 0x00FFu;
    guest.analogButtons[AnalogA] = HasButton(host.buttons, XInputButton::A) ? 0xFFu : 0u;
    guest.analogButtons[AnalogB] = HasButton(host.buttons, XInputButton::B) ? 0xFFu : 0u;
    guest.analogButtons[AnalogX] = HasButton(host.buttons, XInputButton::X) ? 0xFFu : 0u;
    guest.analogButtons[AnalogY] = HasButton(host.buttons, XInputButton::Y) ? 0xFFu : 0u;
    guest.analogButtons[AnalogBlack] =
        HasButton(host.buttons, XInputButton::RightShoulder) ? 0xFFu : 0u;
    guest.analogButtons[AnalogWhite] =
        HasButton(host.buttons, XInputButton::LeftShoulder) ? 0xFFu : 0u;
    guest.analogButtons[AnalogLeftTrigger] = host.leftTrigger;
    guest.analogButtons[AnalogRightTrigger] = host.rightTrigger;
    guest.leftThumbX = host.leftThumbX;
    guest.leftThumbY = host.leftThumbY;
    guest.rightThumbX = host.rightThumbX;
    guest.rightThumbY = host.rightThumbY;
}

std::uint32_t HostInput::GetConnectedMask()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if(!InitializeUnlocked())
    {
        return 0;
    }

    std::uint32_t connectedMask = 0;
    for(std::uint32_t port = 0; port < MaxPorts; ++port)
    {
        NativeXInputState nativeState{};
        if(g_xinputGetState(port, &nativeState) == ERROR_SUCCESS)
        {
            connectedMask |= (1u << port);
        }
    }
    return connectedMask;
}

bool HostInput::Poll(std::uint32_t port, GamepadState& state)
{
    if(port >= MaxPorts)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if(!InitializeUnlocked())
    {
        return false;
    }

    NativeXInputState nativeState{};
    if(g_xinputGetState(port, &nativeState) != ERROR_SUCCESS)
    {
        g_hasLastState[port] = false;
        return false;
    }

    GamepadState translated{};
    TranslateXInputGamepad(nativeState.gamepad, translated);
    if(!g_hasLastState[port] || !SameGamepad(translated, g_lastState[port]))
    {
        ++g_packetNumber[port];
        g_lastState[port] = translated;
        g_hasLastState[port] = true;
    }

    translated.packetNumber = g_packetNumber[port];
    state = translated;
    return true;
}

std::uint32_t HostInput::SetRumble(std::uint32_t port,
                                   std::uint16_t leftMotorSpeed,
                                   std::uint16_t rightMotorSpeed)
{
    if(port >= MaxPorts)
    {
        return ERROR_BAD_ARGUMENTS;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if(!InitializeUnlocked())
    {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    NativeXInputVibration vibration{ leftMotorSpeed, rightMotorSpeed };
    return g_xinputSetState(port, &vibration);
}

void HostInput::Shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_xinputGetState = nullptr;
    g_xinputSetState = nullptr;
    g_hasLastState.fill(false);
    g_packetNumber.fill(0);

    if(g_xinputModule != nullptr)
    {
        FreeLibrary(g_xinputModule);
        g_xinputModule = nullptr;
    }
}
