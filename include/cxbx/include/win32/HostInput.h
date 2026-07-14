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

struct ConnectionSnapshot
{
    std::uint32_t currentMask;
    std::uint32_t changedMask;
    std::array<std::uint32_t, MaxPorts> generations;
};

class ConnectionTracker
{
  public:
    void Reset();
    void Observe(std::uint32_t currentMask,
                 std::uint32_t additionalChangedMask = 0);
    ConnectionSnapshot Snapshot() const;
    ConnectionSnapshot Consume();

  private:
    std::uint32_t m_currentMask = 0;
    std::uint32_t m_changedMask = 0;
    std::array<std::uint32_t, MaxPorts> m_generations{};
};

void TranslateXInputGamepad(const XInputGamepad& host, GamepadState& guest);

// Initialize the host XInput backend. Safe to call from the launcher thread
// before the FS-swap is active; subsequent GetConnectedMask/Poll/SetRumble
// calls from the guest thread then avoid the LoadLibrary/GetProcAddress path
// whose CRT throws cross the FS boundary.
bool Initialize();
bool IsInitialized();
bool AttachWindow(void* nativeWindow);
void NotifyDeviceChange();
ConnectionSnapshot GetConnectionSnapshot(bool refresh, bool consumeChanges);
std::uint32_t GetConnectedMask();
bool Poll(std::uint32_t port, GamepadState& state);
std::uint32_t SetRumble(std::uint32_t port, std::uint16_t leftMotorSpeed,
                        std::uint16_t rightMotorSpeed);
void Shutdown();
} // namespace HostInput

#endif
