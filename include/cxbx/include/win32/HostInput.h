// Windows host gamepad input translated to the original Xbox controller model.
#ifndef HOSTINPUT_H
#define HOSTINPUT_H

#include <array>
#include <cstdint>

namespace HostInput
{
constexpr std::uint32_t MaxPorts = 4;

enum class XInputButton : std::uint16_t
{
    DPadUp = 0x0001,
    DPadDown = 0x0002,
    DPadLeft = 0x0004,
    DPadRight = 0x0008,
    Start = 0x0010,
    Back = 0x0020,
    LeftThumb = 0x0040,
    RightThumb = 0x0080,
    LeftShoulder = 0x0100,
    RightShoulder = 0x0200,
    A = 0x1000,
    B = 0x2000,
    X = 0x4000,
    Y = 0x8000,
};

struct XInputGamepad
{
    std::uint16_t buttons;
    std::uint8_t leftTrigger;
    std::uint8_t rightTrigger;
    std::int16_t leftThumbX;
    std::int16_t leftThumbY;
    std::int16_t rightThumbX;
    std::int16_t rightThumbY;
};

struct GamepadState
{
    std::uint32_t packetNumber;
    std::uint16_t buttons;
    std::array<std::uint8_t, 8> analogButtons;
    std::int16_t leftThumbX;
    std::int16_t leftThumbY;
    std::int16_t rightThumbX;
    std::int16_t rightThumbY;
};

void TranslateXInputGamepad(const XInputGamepad& host, GamepadState& guest);
std::uint32_t GetConnectedMask();
bool Poll(std::uint32_t port, GamepadState& state);
std::uint32_t SetRumble(std::uint32_t port, std::uint16_t leftMotorSpeed,
                        std::uint16_t rightMotorSpeed);
void Shutdown();
} // namespace HostInput

#endif
