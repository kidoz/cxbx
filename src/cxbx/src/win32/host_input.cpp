// Windows XInput backend for original Xbox controller emulation.
#include "host_input.h"

#define POINTER_64 __ptr64
#include <windows.h>

#include <array>
#include <limits>
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
constexpr DWORD DisconnectedProbeIntervalMs = 1000;
constexpr std::uint32_t ValidPortMask = (1u << HostInput::MaxPorts) - 1u;

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
std::array<DWORD, HostInput::MaxPorts> g_lastConnectionProbe{};
HostInput::ConnectionTracker g_connections;
HWND g_notificationWindow = nullptr;
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

void ObservePortConnectionUnlocked(std::uint32_t port, bool connected)
{
    HostInput::ConnectionSnapshot snapshot = g_connections.Snapshot();
    const std::uint32_t portMask = 1u << port;
    const std::uint32_t currentMask = connected
                                          ? snapshot.currentMask | portMask
                                          : snapshot.currentMask & ~portMask;
    g_connections.Observe(currentMask);
    g_lastConnectionProbe[port] = GetTickCount();
}

void RefreshConnectionsUnlocked(bool force)
{
    if(g_xinputGetState == nullptr)
    {
        return;
    }

    const DWORD now = GetTickCount();
    std::uint32_t observedMask = g_connections.Snapshot().currentMask;
    for(std::uint32_t port = 0; port < HostInput::MaxPorts; ++port)
    {
        const std::uint32_t portMask = 1u << port;
        const bool connected = (observedMask & portMask) != 0;
        const bool probeDisconnected =
            now - g_lastConnectionProbe[port] >= DisconnectedProbeIntervalMs;
        if(!force && !connected && !probeDisconnected)
        {
            continue;
        }

        NativeXInputState nativeState{};
        const DWORD result = g_xinputGetState(port, &nativeState);
        g_lastConnectionProbe[port] = now;
        if(result == ERROR_SUCCESS)
        {
            observedMask |= portMask;
        }
        else if(result == ERROR_DEVICE_NOT_CONNECTED)
        {
            observedMask &= ~portMask;
            g_hasLastState[port] = false;
        }
    }
    g_connections.Observe(observedMask);
}

std::array<RAWINPUTDEVICE, 3> MakeRawInputDevices(HWND window, DWORD flags)
{
    return { RAWINPUTDEVICE{ 0x01, 0x04, flags, window },
             RAWINPUTDEVICE{ 0x01, 0x05, flags, window },
             RAWINPUTDEVICE{ 0x01, 0x08, flags, window } };
}

void DetachWindowUnlocked()
{
    if(g_notificationWindow == nullptr)
    {
        return;
    }

    auto devices = MakeRawInputDevices(nullptr, RIDEV_REMOVE);
    RegisterRawInputDevices(devices.data(), static_cast<UINT>(devices.size()),
                            sizeof(RAWINPUTDEVICE));
    g_notificationWindow = nullptr;
}

std::string_view Trim(std::string_view text)
{
    while(!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    {
        text.remove_prefix(1);
    }
    while(!text.empty() && (text.back() == ' ' || text.back() == '\t'))
    {
        text.remove_suffix(1);
    }
    return text;
}

bool ParseUnsigned(std::string_view text, std::uint32_t maximum,
                   std::uint32_t& value)
{
    text = Trim(text);
    if(text.empty())
    {
        return false;
    }

    std::uint32_t base = 10;
    if(text.size() > 2 && text[0] == '0' &&
       (text[1] == 'x' || text[1] == 'X'))
    {
        base = 16;
        text.remove_prefix(2);
    }
    if(text.empty())
    {
        return false;
    }

    std::uint32_t result = 0;
    for(const char character : text)
    {
        std::uint32_t digit = 0;
        if(character >= '0' && character <= '9')
        {
            digit = static_cast<std::uint32_t>(character - '0');
        }
        else if(character >= 'a' && character <= 'f')
        {
            digit = static_cast<std::uint32_t>(character - 'a') + 10u;
        }
        else if(character >= 'A' && character <= 'F')
        {
            digit = static_cast<std::uint32_t>(character - 'A') + 10u;
        }
        else
        {
            return false;
        }
        if(digit >= base || digit > maximum ||
           result > (maximum - digit) / base)
        {
            return false;
        }
        result = result * base + digit;
    }
    value = result;
    return true;
}

bool ParseSigned16(std::string_view text, std::int16_t& value)
{
    text = Trim(text);
    const bool negative = !text.empty() && text.front() == '-';
    if(negative)
    {
        text.remove_prefix(1);
    }

    std::uint32_t magnitude = 0;
    const std::uint32_t maximum = negative ? 32768u : 32767u;
    if(!ParseUnsigned(text, maximum, magnitude))
    {
        return false;
    }
    const std::int32_t signedValue = negative
                                         ? -static_cast<std::int32_t>(magnitude)
                                         : static_cast<std::int32_t>(magnitude);
    value = static_cast<std::int16_t>(signedValue);
    return true;
}
} // namespace

bool HostInput::ParseInjectedGamepadState(std::string_view text,
                                          GamepadState& state)
{
    GamepadState parsed{};
    std::size_t fieldIndex = 0;
    while(true)
    {
        const std::size_t separator = text.find(',');
        const std::string_view field = text.substr(0, separator);
        if(fieldIndex == 0)
        {
            std::uint32_t buttons = 0;
            if(!ParseUnsigned(field, 0xFFFFu, buttons))
            {
                return false;
            }
            parsed.buttons = static_cast<std::uint16_t>(buttons);
        }
        else if(fieldIndex <= parsed.analogButtons.size())
        {
            std::uint32_t analog = 0;
            if(!ParseUnsigned(field, 0xFFu, analog))
            {
                return false;
            }
            parsed.analogButtons[fieldIndex - 1] =
                static_cast<std::uint8_t>(analog);
        }
        else if(fieldIndex <= parsed.analogButtons.size() + 4)
        {
            std::int16_t axis = 0;
            if(!ParseSigned16(field, axis))
            {
                return false;
            }
            switch(fieldIndex - parsed.analogButtons.size())
            {
                case 1: parsed.leftThumbX = axis; break;
                case 2: parsed.leftThumbY = axis; break;
                case 3: parsed.rightThumbX = axis; break;
                case 4: parsed.rightThumbY = axis; break;
            }
        }
        else
        {
            return false;
        }

        ++fieldIndex;
        if(separator == std::string_view::npos)
        {
            break;
        }
        text.remove_prefix(separator + 1);
    }

    state = parsed;
    return true;
}

bool HostInput::InjectedGamepadSequence::Parse(std::string_view text)
{
    std::array<Frame, MaxInjectedGamepadFrames> parsedFrames{};
    std::size_t parsedSize = 0;
    while(!text.empty())
    {
        if(parsedSize == parsedFrames.size())
        {
            return false;
        }

        const std::size_t frameSeparator = text.find(';');
        const std::string_view frameText = Trim(text.substr(0, frameSeparator));
        const std::size_t stateSeparator = frameText.find('@');
        if(stateSeparator == std::string_view::npos ||
           frameText.find('@', stateSeparator + 1) != std::string_view::npos)
        {
            return false;
        }

        std::uint32_t elapsedMs = 0;
        GamepadState state{};
        if(!ParseUnsigned(frameText.substr(0, stateSeparator),
                          (std::numeric_limits<std::uint32_t>::max)(), elapsedMs) ||
           !ParseInjectedGamepadState(frameText.substr(stateSeparator + 1), state) ||
           (parsedSize != 0 && elapsedMs <= parsedFrames[parsedSize - 1].elapsedMs))
        {
            return false;
        }
        parsedFrames[parsedSize++] = { elapsedMs, state };

        if(frameSeparator == std::string_view::npos)
        {
            break;
        }
        text.remove_prefix(frameSeparator + 1);
        if(text.empty())
        {
            return false;
        }
    }

    if(parsedSize == 0)
    {
        return false;
    }
    m_frames = parsedFrames;
    m_size = parsedSize;
    return true;
}

std::size_t HostInput::InjectedGamepadSequence::Size() const
{
    return m_size;
}

std::size_t HostInput::InjectedGamepadSequence::FrameIndexAt(
    std::uint32_t elapsedMs) const
{
    std::size_t first = 0;
    std::size_t last = m_size;
    while(first < last)
    {
        const std::size_t middle = first + (last - first) / 2;
        if(m_frames[middle].elapsedMs <= elapsedMs)
        {
            first = middle + 1;
        }
        else
        {
            last = middle;
        }
    }
    return first == 0 ? m_size : first - 1;
}

HostInput::GamepadState HostInput::InjectedGamepadSequence::StateAt(
    std::uint32_t elapsedMs) const
{
    const std::size_t index = FrameIndexAt(elapsedMs);
    return index < m_size ? m_frames[index].state : GamepadState{};
}

void HostInput::ConnectionTracker::Reset()
{
    m_currentMask = 0;
    m_changedMask = 0;
    m_generations.fill(0);
}

void HostInput::ConnectionTracker::Observe(std::uint32_t currentMask,
                                           std::uint32_t additionalChangedMask)
{
    currentMask &= ValidPortMask;
    const std::uint32_t transitionMask =
        (m_currentMask ^ currentMask) | (additionalChangedMask & ValidPortMask);
    m_changedMask |= transitionMask;
    m_currentMask = currentMask;

    for(std::uint32_t port = 0; port < MaxPorts; ++port)
    {
        if((transitionMask & (1u << port)) == 0)
        {
            continue;
        }

        ++m_generations[port];
        if(m_generations[port] == 0)
        {
            ++m_generations[port];
        }
    }
}

HostInput::ConnectionSnapshot HostInput::ConnectionTracker::Snapshot() const
{
    return { m_currentMask, m_changedMask, m_generations };
}

HostInput::ConnectionSnapshot HostInput::ConnectionTracker::Consume()
{
    ConnectionSnapshot snapshot = Snapshot();
    m_changedMask = 0;
    return snapshot;
}

bool HostInput::Initialize()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if(!InitializeUnlocked())
    {
        return false;
    }

    RefreshConnectionsUnlocked(true);
    return true;
}

bool HostInput::IsInitialized()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_xinputGetState != nullptr && g_xinputSetState != nullptr;
}

bool HostInput::AttachWindow(void* nativeWindow)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if(g_xinputGetState == nullptr || nativeWindow == nullptr)
    {
        return false;
    }

    HWND window = static_cast<HWND>(nativeWindow);
    if(g_notificationWindow == window)
    {
        return true;
    }

    DetachWindowUnlocked();
    auto devices = MakeRawInputDevices(window, RIDEV_DEVNOTIFY);
    if(!RegisterRawInputDevices(devices.data(), static_cast<UINT>(devices.size()),
                                sizeof(RAWINPUTDEVICE)))
    {
        return false;
    }

    g_notificationWindow = window;
    RefreshConnectionsUnlocked(true);
    return true;
}

void HostInput::NotifyDeviceChange()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    RefreshConnectionsUnlocked(true);
}

HostInput::ConnectionSnapshot HostInput::GetConnectionSnapshot(
    bool refresh, bool consumeChanges)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if(refresh)
    {
        RefreshConnectionsUnlocked(false);
    }
    return consumeChanges ? g_connections.Consume() : g_connections.Snapshot();
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
    return GetConnectionSnapshot(true, false).currentMask;
}

bool HostInput::Poll(std::uint32_t port, GamepadState& state)
{
    if(port >= MaxPorts)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if(g_xinputGetState == nullptr)
    {
        return false;
    }

    NativeXInputState nativeState{};
    const DWORD result = g_xinputGetState(port, &nativeState);
    if(result != ERROR_SUCCESS)
    {
        if(result == ERROR_DEVICE_NOT_CONNECTED)
        {
            ObservePortConnectionUnlocked(port, false);
            g_hasLastState[port] = false;
        }
        return false;
    }
    ObservePortConnectionUnlocked(port, true);

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
    if(g_xinputSetState == nullptr)
    {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    NativeXInputVibration vibration{ leftMotorSpeed, rightMotorSpeed };
    const DWORD result = g_xinputSetState(port, &vibration);
    if(result == ERROR_SUCCESS)
    {
        ObservePortConnectionUnlocked(port, true);
    }
    else if(result == ERROR_DEVICE_NOT_CONNECTED)
    {
        ObservePortConnectionUnlocked(port, false);
        g_hasLastState[port] = false;
    }
    return result;
}

void HostInput::Shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    DetachWindowUnlocked();
    g_xinputGetState = nullptr;
    g_xinputSetState = nullptr;
    g_hasLastState.fill(false);
    g_packetNumber.fill(0);
    g_lastConnectionProbe.fill(0);
    g_connections.Reset();

    if(g_xinputModule != nullptr)
    {
        FreeLibrary(g_xinputModule);
        g_xinputModule = nullptr;
    }
}
