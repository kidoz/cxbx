#include "host_input.h"

#include <cstdint>
#include <cstdio>

namespace
{
int g_failures = 0;

template <typename Actual, typename Expected>
void Check(const char* name, Actual actual, Expected expected)
{
    if(actual != expected)
    {
        std::fprintf(stderr, "FAIL %s expected=%lld actual=%lld\n", name,
                     static_cast<long long>(expected),
                     static_cast<long long>(actual));
        ++g_failures;
    }
}

constexpr std::uint16_t Button(HostInput::XInputButton button)
{
    return static_cast<std::uint16_t>(button);
}
} // namespace

int main()
{
    HostInput::ConnectionTracker connections;
    HostInput::ConnectionSnapshot connection = connections.Snapshot();
    Check("connections initial current", connection.currentMask,
          static_cast<std::uint32_t>(0));
    Check("connections initial changed", connection.changedMask,
          static_cast<std::uint32_t>(0));

    connections.Observe(1u);
    connection = connections.Consume();
    Check("connections insertion current", connection.currentMask,
          static_cast<std::uint32_t>(1));
    Check("connections insertion changed", connection.changedMask,
          static_cast<std::uint32_t>(1));
    Check("connections insertion generation", connection.generations[0],
          static_cast<std::uint32_t>(1));

    connections.Observe(0u);
    connections.Observe(1u);
    connection = connections.Consume();
    Check("connections rapid reconnect current", connection.currentMask,
          static_cast<std::uint32_t>(1));
    Check("connections rapid reconnect latched", connection.changedMask,
          static_cast<std::uint32_t>(1));
    Check("connections rapid reconnect generation", connection.generations[0],
          static_cast<std::uint32_t>(3));

    connections.Observe(1u, 1u);
    connection = connections.Consume();
    Check("connections notified reconnect current", connection.currentMask,
          static_cast<std::uint32_t>(1));
    Check("connections notified reconnect latched", connection.changedMask,
          static_cast<std::uint32_t>(1));
    Check("connections notified reconnect generation", connection.generations[0],
          static_cast<std::uint32_t>(4));

    connections.Observe(0x0Au);
    connection = connections.Consume();
    Check("connections multi-port current", connection.currentMask,
          static_cast<std::uint32_t>(0x0A));
    Check("connections multi-port changed", connection.changedMask,
          static_cast<std::uint32_t>(0x0B));

    connections.Reset();
    connections.Observe(1u);
    connections.Consume();
    connections.Observe(0u);
    connection = connections.Consume();
    Check("fallback removal current", connection.currentMask,
          static_cast<std::uint32_t>(0));
    Check("fallback removal changed", connection.changedMask,
          static_cast<std::uint32_t>(1));
    Check("fallback removal generation", connection.generations[0],
          static_cast<std::uint32_t>(2));

    connections.Observe(1u);
    connection = connections.Consume();
    Check("fallback reinsertion current", connection.currentMask,
          static_cast<std::uint32_t>(1));
    Check("fallback reinsertion changed", connection.changedMask,
          static_cast<std::uint32_t>(1));
    Check("fallback reinsertion generation", connection.generations[0],
          static_cast<std::uint32_t>(3));

    HostInput::XInputGamepad host{};
    host.buttons = Button(HostInput::XInputButton::DPadUp) |
                   Button(HostInput::XInputButton::Start) |
                   Button(HostInput::XInputButton::LeftThumb) |
                   Button(HostInput::XInputButton::A) |
                   Button(HostInput::XInputButton::Y) |
                   Button(HostInput::XInputButton::LeftShoulder) |
                   Button(HostInput::XInputButton::RightShoulder);
    host.leftTrigger = 37;
    host.rightTrigger = 219;
    host.leftThumbX = -12345;
    host.leftThumbY = 23456;
    host.rightThumbX = 32767;
    host.rightThumbY = -32768;

    HostInput::GamepadState guest{};
    HostInput::TranslateXInputGamepad(host, guest);

    Check("digital buttons", guest.buttons, static_cast<std::uint16_t>(0x0051));
    Check("analog A", guest.analogButtons[0], static_cast<std::uint8_t>(0xFF));
    Check("analog B", guest.analogButtons[1], static_cast<std::uint8_t>(0));
    Check("analog X", guest.analogButtons[2], static_cast<std::uint8_t>(0));
    Check("analog Y", guest.analogButtons[3], static_cast<std::uint8_t>(0xFF));
    Check("black from right shoulder", guest.analogButtons[4],
          static_cast<std::uint8_t>(0xFF));
    Check("white from left shoulder", guest.analogButtons[5],
          static_cast<std::uint8_t>(0xFF));
    Check("left trigger", guest.analogButtons[6], static_cast<std::uint8_t>(37));
    Check("right trigger", guest.analogButtons[7], static_cast<std::uint8_t>(219));
    Check("left thumb X", guest.leftThumbX, static_cast<std::int16_t>(-12345));
    Check("left thumb Y", guest.leftThumbY, static_cast<std::int16_t>(23456));
    Check("right thumb X", guest.rightThumbX, static_cast<std::int16_t>(32767));
    Check("right thumb Y", guest.rightThumbY, static_cast<std::int16_t>(-32768));
    Check("translation does not invent packet", guest.packetNumber,
          static_cast<std::uint32_t>(0));

    HostInput::TranslateXInputGamepad({}, guest);
    Check("neutral buttons", guest.buttons, static_cast<std::uint16_t>(0));
    Check("neutral left trigger", guest.analogButtons[6], static_cast<std::uint8_t>(0));
    Check("neutral right trigger", guest.analogButtons[7], static_cast<std::uint8_t>(0));

    HostInput::GamepadState injected{};
    Check("parse injected state",
          HostInput::ParseInjectedGamepadState(
              "0x0010,255,1,2,3,4,5,6,7,-32768,32767,-123,456",
              injected),
          true);
    Check("parsed buttons", injected.buttons, static_cast<std::uint16_t>(0x0010));
    Check("parsed analog A", injected.analogButtons[0],
          static_cast<std::uint8_t>(255));
    Check("parsed analog right trigger", injected.analogButtons[7],
          static_cast<std::uint8_t>(7));
    Check("parsed left thumb X", injected.leftThumbX,
          static_cast<std::int16_t>(-32768));
    Check("parsed left thumb Y", injected.leftThumbY,
          static_cast<std::int16_t>(32767));
    Check("parsed right thumb X", injected.rightThumbX,
          static_cast<std::int16_t>(-123));
    Check("parsed right thumb Y", injected.rightThumbY,
          static_cast<std::int16_t>(456));
    Check("reject button overflow",
          HostInput::ParseInjectedGamepadState("0x10000", injected), false);
    Check("reject analog overflow",
          HostInput::ParseInjectedGamepadState("0,256", injected), false);
    Check("reject positive axis overflow",
          HostInput::ParseInjectedGamepadState(
              "0,0,0,0,0,0,0,0,0,32768", injected),
          false);
    Check("reject negative axis overflow",
          HostInput::ParseInjectedGamepadState(
              "0,0,0,0,0,0,0,0,0,-32769", injected),
          false);
    Check("reject malformed injected state",
          HostInput::ParseInjectedGamepadState("0,12x", injected), false);

    HostInput::InjectedGamepadSequence sequence;
    Check("parse injected sequence",
          sequence.Parse("100@0x0;200@0x10,255;250@0x0"), true);
    Check("injected sequence size", sequence.Size(), static_cast<std::size_t>(3));
    Check("sequence before first frame", sequence.FrameIndexAt(99), sequence.Size());
    Check("sequence before first buttons", sequence.StateAt(99).buttons,
          static_cast<std::uint16_t>(0));
    Check("sequence first frame", sequence.FrameIndexAt(100),
          static_cast<std::size_t>(0));
    Check("sequence first frame persists", sequence.FrameIndexAt(199),
          static_cast<std::size_t>(0));
    Check("sequence second buttons", sequence.StateAt(200).buttons,
          static_cast<std::uint16_t>(0x0010));
    Check("sequence second analog A", sequence.StateAt(249).analogButtons[0],
          static_cast<std::uint8_t>(255));
    Check("sequence last frame", sequence.FrameIndexAt(250),
          static_cast<std::size_t>(2));
    Check("sequence last frame persists", sequence.FrameIndexAt(999),
          static_cast<std::size_t>(2));
    Check("sequence last buttons", sequence.StateAt(999).buttons,
          static_cast<std::uint16_t>(0));
    Check("reject equal timestamps", sequence.Parse("100@0;100@1"), false);
    Check("reject decreasing timestamps", sequence.Parse("100@0;99@1"), false);
    Check("reject trailing separator", sequence.Parse("100@0;"), false);
    Check("reject malformed frame", sequence.Parse("100@@0"), false);
    Check("failed parse preserves sequence", sequence.Size(),
          static_cast<std::size_t>(3));
    Check("failed parse preserves state", sequence.StateAt(200).buttons,
          static_cast<std::uint16_t>(0x0010));

    Check("host input initially stopped", HostInput::IsInitialized(), false);
    Check("host input initialize", HostInput::Initialize(), true);
    Check("host input initialized", HostInput::IsInitialized(), true);
    const std::uint32_t connectedMask = HostInput::GetConnectedMask();
    std::printf("INFO connected XInput mask=0x%X\n", connectedMask);
    if((connectedMask & 1u) != 0)
    {
        HostInput::GamepadState physicalState{};
        const bool polled = HostInput::Poll(0, physicalState);
        std::printf("INFO port0 poll=%d packet=%u buttons=0x%04X LT=%u RT=%u\n",
                    polled ? 1 : 0, physicalState.packetNumber,
                    physicalState.buttons, physicalState.analogButtons[6],
                    physicalState.analogButtons[7]);
        Check("physical port0 zero rumble", HostInput::SetRumble(0, 0, 0),
              static_cast<std::uint32_t>(0));
    }
    HostInput::Shutdown();
    Check("host input shutdown", HostInput::IsInitialized(), false);

    if(g_failures == 0)
    {
        std::puts("PASS host input translation");
    }
    return g_failures;
}
