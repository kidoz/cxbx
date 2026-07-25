#pragma once

#include <cstdint>

namespace cxbx::nv2a
{

enum class PfifoPusherStepKind
{
    Method,
    Jump,
    Call,
    Return,
    Header,
    Error,
};

enum class PfifoPusherError : std::uint32_t
{
    None = 0x00000000u,
    Call = 0x20000000u,
    Return = 0x60000000u,
    Reserved = 0x80000000u,
    Protection = 0xC0000000u,
};

struct PfifoPusherState
{
    std::uint32_t get = 0;
    std::uint32_t methodState = 0;
    std::uint32_t dataCount = 0;
    std::uint32_t subroutine = 0;
};

struct PfifoPusherStep
{
    PfifoPusherStepKind kind = PfifoPusherStepKind::Header;
    PfifoPusherError error = PfifoPusherError::None;
    std::uint32_t subchannel = 0;
    std::uint32_t method = 0;
    std::uint32_t data = 0;
};

enum class PfifoInsertedCallbackPhase
{
    Idle,
    Address,
    Context,
    Notify,
};

struct PfifoInsertedCallbackState
{
    PfifoInsertedCallbackPhase phase = PfifoInsertedCallbackPhase::Idle;
    std::uint32_t address = 0;
    std::uint32_t context = 0;
};

struct PfifoInsertedCallback
{
    bool ready = false;
    std::uint32_t address = 0;
    std::uint32_t context = 0;
};

[[nodiscard]] bool IsPfifoDmaWordInRange(
    std::uint32_t offset, std::uint32_t limit) noexcept;
[[nodiscard]] std::uint32_t ApplyPfifoPusherError(
    std::uint32_t methodState, PfifoPusherError error) noexcept;
[[nodiscard]] PfifoPusherStep StepPfifoPusher(
    PfifoPusherState& state, std::uint32_t word) noexcept;
[[nodiscard]] PfifoInsertedCallback ApplyPfifoInsertedCallbackMethod(
    PfifoInsertedCallbackState& state, std::uint32_t method,
    std::uint32_t data) noexcept;

} // namespace cxbx::nv2a
