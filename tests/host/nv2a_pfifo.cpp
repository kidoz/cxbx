#include "hw/nv2a_pfifo.h"

#include <cstdio>
#include <cstdint>

namespace
{

bool ExpectEqual(std::uint32_t actual, std::uint32_t expected,
                 const char* message) noexcept
{
    if(actual == expected)
    {
        return true;
    }

    std::fprintf(stderr, "%s: expected 0x%08X, got 0x%08X\n", message,
                 expected, actual);
    return false;
}

bool ExpectKind(cxbx::nv2a::PfifoPusherStepKind actual,
                cxbx::nv2a::PfifoPusherStepKind expected,
                const char* message) noexcept
{
    if(actual == expected)
    {
        return true;
    }

    std::fprintf(stderr, "%s: unexpected step kind\n", message);
    return false;
}

} // namespace

int main() noexcept
{
    using cxbx::nv2a::PfifoPusherError;
    using cxbx::nv2a::PfifoPusherState;
    using cxbx::nv2a::PfifoPusherStepKind;

    if(!cxbx::nv2a::IsPfifoDmaWordInRange(0, 3) ||
       !cxbx::nv2a::IsPfifoDmaWordInRange(4, 7) ||
       cxbx::nv2a::IsPfifoDmaWordInRange(4, 6) ||
       cxbx::nv2a::IsPfifoDmaWordInRange(8, 7))
    {
        std::fputs("PFIFO DMA word range validation is invalid\n", stderr);
        return 1;
    }

    PfifoPusherState state{};
    auto step = cxbx::nv2a::StepPfifoPusher(state, 0x00080100u);
    if(!ExpectKind(step.kind, PfifoPusherStepKind::Header,
                   "increasing header") ||
       !ExpectEqual(state.get, 4, "header GET") ||
       !ExpectEqual(state.methodState, 0x00080100u,
                    "increasing header state") ||
       !ExpectEqual(state.dataCount, 0, "header DCOUNT"))
    {
        return 1;
    }

    step = cxbx::nv2a::StepPfifoPusher(state, 0x11111111u);
    if(!ExpectKind(step.kind, PfifoPusherStepKind::Method,
                   "first increasing data") ||
       !ExpectEqual(step.subchannel, 0, "first method subchannel") ||
       !ExpectEqual(step.method, 0x100u, "first increasing method") ||
       !ExpectEqual(step.data, 0x11111111u, "first method data") ||
       !ExpectEqual(state.methodState, 0x00040104u,
                    "first increasing state") ||
       !ExpectEqual(state.dataCount, 1, "first method DCOUNT"))
    {
        return 1;
    }

    step = cxbx::nv2a::StepPfifoPusher(state, 0x22222222u);
    if(!ExpectKind(step.kind, PfifoPusherStepKind::Method,
                   "second increasing data") ||
       !ExpectEqual(step.method, 0x104u, "second increasing method") ||
       !ExpectEqual(state.methodState, 0x00000108u,
                    "completed increasing state") ||
       !ExpectEqual(state.dataCount, 2, "completed method DCOUNT"))
    {
        return 1;
    }

    state = {};
    step = cxbx::nv2a::StepPfifoPusher(state, 0x40086080u);
    step = cxbx::nv2a::StepPfifoPusher(state, 0x33333333u);
    if(!ExpectKind(step.kind, PfifoPusherStepKind::Method,
                   "non-increasing data") ||
       !ExpectEqual(step.subchannel, 3, "non-increasing subchannel") ||
       !ExpectEqual(step.method, 0x80u, "non-increasing method") ||
       !ExpectEqual(state.methodState, 0x00046081u,
                    "non-increasing method state"))
    {
        return 1;
    }

    state = {};
    step = cxbx::nv2a::StepPfifoPusher(state, 0x20000020u);
    if(!ExpectKind(step.kind, PfifoPusherStepKind::Jump, "old jump") ||
       !ExpectEqual(state.get, 0x20u, "old jump target"))
    {
        return 1;
    }

    state = {};
    step = cxbx::nv2a::StepPfifoPusher(state, 0x00000021u);
    if(!ExpectKind(step.kind, PfifoPusherStepKind::Jump, "jump") ||
       !ExpectEqual(state.get, 0x20u, "jump target"))
    {
        return 1;
    }

    state = {};
    step = cxbx::nv2a::StepPfifoPusher(state, 0x00000022u);
    if(!ExpectKind(step.kind, PfifoPusherStepKind::Call, "call") ||
       !ExpectEqual(state.get, 0x20u, "call target") ||
       !ExpectEqual(state.subroutine, 5, "call return address"))
    {
        return 1;
    }

    step = cxbx::nv2a::StepPfifoPusher(state, 0x00000042u);
    if(!ExpectKind(step.kind, PfifoPusherStepKind::Error, "nested call") ||
       step.error != PfifoPusherError::Call ||
       !ExpectEqual(state.get, 0x24u, "failed call GET") ||
       !ExpectEqual(state.subroutine, 5, "failed call subroutine"))
    {
        return 1;
    }

    state.get = 0x20u;
    step = cxbx::nv2a::StepPfifoPusher(state, 0x00020000u);
    if(!ExpectKind(step.kind, PfifoPusherStepKind::Return, "return") ||
       !ExpectEqual(state.get, 4, "return target") ||
       !ExpectEqual(state.subroutine, 0, "cleared subroutine"))
    {
        return 1;
    }

    state = {};
    step = cxbx::nv2a::StepPfifoPusher(state, 0x00020000u);
    if(!ExpectKind(step.kind, PfifoPusherStepKind::Error,
                   "return without call") ||
       step.error != PfifoPusherError::Return ||
       !ExpectEqual(state.get, 4, "failed return GET"))
    {
        return 1;
    }

    state = {};
    step = cxbx::nv2a::StepPfifoPusher(state, 0xDEADBEEFu);
    if(!ExpectKind(step.kind, PfifoPusherStepKind::Error,
                   "reserved command") ||
       step.error != PfifoPusherError::Reserved ||
       !ExpectEqual(state.get, 4, "reserved command GET"))
    {
        return 1;
    }

    if(!ExpectEqual(
           cxbx::nv2a::ApplyPfifoPusherError(
               0xF2345678u, PfifoPusherError::Protection),
           0xD2345678u, "protection error state") ||
       !ExpectEqual(
           cxbx::nv2a::ApplyPfifoPusherError(
               0xF2345678u, PfifoPusherError::Reserved),
           0x92345678u, "reserved error state"))
    {
        return 1;
    }

    return 0;
}
