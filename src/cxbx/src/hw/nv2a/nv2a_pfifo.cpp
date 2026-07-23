#include "hw/nv2a_pfifo.h"

namespace cxbx::nv2a
{

namespace
{

constexpr std::uint32_t MethodTypeMask = 0x00000001u;
constexpr std::uint32_t MethodMask = 0x00001FFCu;
constexpr std::uint32_t SubchannelMask = 0x0000E000u;
constexpr std::uint32_t MethodCountMask = 0x1FFC0000u;
constexpr std::uint32_t ErrorMask = 0xE0000000u;
constexpr std::uint32_t MethodCountShift = 18;
constexpr std::uint32_t SubchannelShift = 13;
constexpr std::uint32_t DmaWordSize = sizeof(std::uint32_t);
constexpr std::uint32_t OldJumpMask = 0xE0000003u;
constexpr std::uint32_t OldJumpValue = 0x20000000u;
constexpr std::uint32_t JumpTargetMask = 0x1FFFFFFCu;
constexpr std::uint32_t ControlTypeMask = 0x00000003u;
constexpr std::uint32_t JumpType = 0x00000001u;
constexpr std::uint32_t CallType = 0x00000002u;
constexpr std::uint32_t ControlTargetMask = 0xFFFFFFFCu;
constexpr std::uint32_t ReturnCommand = 0x00020000u;
constexpr std::uint32_t MethodHeaderMask = 0xE0030003u;
constexpr std::uint32_t IncreasingHeader = 0x00000000u;
constexpr std::uint32_t NonIncreasingHeader = 0x40000000u;
constexpr std::uint32_t SubroutineActive = 0x00000001u;

} // namespace

bool IsPfifoDmaWordInRange(std::uint32_t offset,
                           std::uint32_t limit) noexcept
{
    return offset <= limit && limit - offset >= DmaWordSize - 1;
}

std::uint32_t ApplyPfifoPusherError(
    std::uint32_t methodState, PfifoPusherError error) noexcept
{
    return (methodState & ~ErrorMask) | static_cast<std::uint32_t>(error);
}

PfifoPusherStep StepPfifoPusher(PfifoPusherState& state,
                                std::uint32_t word) noexcept
{
    state.get += DmaWordSize;

    std::uint32_t count =
        (state.methodState & MethodCountMask) >> MethodCountShift;
    if(count != 0)
    {
        std::uint32_t method = state.methodState & MethodMask;
        const std::uint32_t subchannel =
            (state.methodState & SubchannelMask) >> SubchannelShift;
        const PfifoPusherStep step{
            PfifoPusherStepKind::Method,
            PfifoPusherError::None,
            subchannel,
            method,
            word,
        };

        if((state.methodState & MethodTypeMask) == 0)
        {
            method += DmaWordSize;
        }

        --count;
        state.methodState &= ~(MethodMask | MethodCountMask);
        state.methodState |= method & MethodMask;
        state.methodState |=
            (count << MethodCountShift) & MethodCountMask;
        ++state.dataCount;
        return step;
    }

    if((word & OldJumpMask) == OldJumpValue)
    {
        state.get = word & JumpTargetMask;
        return { PfifoPusherStepKind::Jump };
    }

    if((word & ControlTypeMask) == JumpType)
    {
        state.get = word & ControlTargetMask;
        return { PfifoPusherStepKind::Jump };
    }

    if((word & ControlTypeMask) == CallType)
    {
        if((state.subroutine & SubroutineActive) != 0)
        {
            return { PfifoPusherStepKind::Error,
                     PfifoPusherError::Call };
        }

        state.subroutine = (state.get & ControlTargetMask) | SubroutineActive;
        state.get = word & ControlTargetMask;
        return { PfifoPusherStepKind::Call };
    }

    if(word == ReturnCommand)
    {
        if((state.subroutine & SubroutineActive) == 0)
        {
            return { PfifoPusherStepKind::Error,
                     PfifoPusherError::Return };
        }

        state.get = state.subroutine & ControlTargetMask;
        state.subroutine = 0;
        return { PfifoPusherStepKind::Return };
    }

    const std::uint32_t headerType = word & MethodHeaderMask;
    if(headerType == IncreasingHeader ||
       headerType == NonIncreasingHeader)
    {
        state.methodState &= ErrorMask;
        state.methodState |=
            word & (MethodMask | SubchannelMask | MethodCountMask);
        if(headerType == NonIncreasingHeader)
        {
            state.methodState |= MethodTypeMask;
        }

        state.dataCount = 0;
        return { PfifoPusherStepKind::Header };
    }

    return { PfifoPusherStepKind::Error, PfifoPusherError::Reserved };
}

} // namespace cxbx::nv2a
