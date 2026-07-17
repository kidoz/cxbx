// Host regression test for EmuStackPrecommit.h: guest-role threads must never
// depend on kernel stack growth, because while Xbox code runs the TEB stack
// fields hold KPCR/TLS content and the kernel will not grow the stack on a
// guard fault (Turok Evolution's intermittent early 0xC0000005 during CPU
// vertex-shader fallback setup). The test creates a thread with a small
// committed stack inside a larger reservation, pre-commits it, verifies the
// whole reservation is committed and guard-free, then corrupts the TIB
// StackBase exactly the way the Xbox FS role does and recurses far past the
// originally committed region -- which only survives when no growth is needed.

#include "EmuStackPrecommit.h"

#include <cstdio>
#include <cstring>

namespace
{

constexpr SIZE_T kReservationBytes = 256 * 1024;
constexpr int kBurnFrames = 40; // ~40 x 4 KiB frames = 160 KiB of stack

#if defined(_MSC_VER)
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif

// One ~4 KiB stack frame per recursion level, with every page touched so the
// consumption is real (and would have required guard growth without the
// pre-commit).
TEST_NOINLINE int BurnStack(int depth)
{
    volatile unsigned char frame[4096];
    std::memset(const_cast<unsigned char*>(frame), depth & 0xFF, sizeof(frame));
    if(depth <= 0)
    {
        return frame[0];
    }
    return BurnStack(depth - 1) + frame[1];
}

bool RangeCommittedGuardFree(BYTE* begin, BYTE* end)
{
    BYTE* cursor = begin;
    while(cursor < end)
    {
        MEMORY_BASIC_INFORMATION info;
        if(VirtualQuery(cursor, &info, sizeof(info)) != sizeof(info))
        {
            std::fprintf(stderr, "VirtualQuery failed at %p\n", cursor);
            return false;
        }
        if(info.State != MEM_COMMIT)
        {
            std::fprintf(stderr, "page %p not committed (state=0x%lX)\n",
                         cursor, info.State);
            return false;
        }
        if((info.Protect & PAGE_GUARD) != 0)
        {
            std::fprintf(stderr, "page %p still PAGE_GUARD\n", cursor);
            return false;
        }
        cursor = (BYTE*)info.BaseAddress + info.RegionSize;
    }
    return true;
}

DWORD WINAPI TestThread(LPVOID)
{
    NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
    BYTE* deallocation = *(BYTE**)((BYTE*)tib + 0xE0C);
    BYTE* originalLimit = (BYTE*)tib->StackLimit;
    if(deallocation == NULL || originalLimit <= deallocation)
    {
        std::fprintf(stderr, "could not resolve thread stack bounds\n");
        return 1;
    }

    // The reservation must be materially larger than the initial commit, or
    // the scenario under test (growth needed while in guest role) is absent.
    const SIZE_T uncommitted = (SIZE_T)(originalLimit - deallocation);
    if(uncommitted < 64 * 1024)
    {
        std::fprintf(stderr,
                     "initial commit too close to reservation base (0x%lX)\n",
                     (unsigned long)uncommitted);
        return 1;
    }

    const SIZE_T precommitted = EmuPrecommitThreadStack();
    if(precommitted == 0)
    {
        std::fprintf(stderr, "EmuPrecommitThreadStack committed nothing\n");
        return 1;
    }

    SYSTEM_INFO systemInfo;
    GetSystemInfo(&systemInfo);
    BYTE* fence = deallocation + systemInfo.dwPageSize;
    if((BYTE*)tib->StackLimit != fence)
    {
        std::fprintf(stderr, "StackLimit not lowered to the fence page\n");
        return 1;
    }
    if(!RangeCommittedGuardFree(fence, (BYTE*)tib->StackBase))
    {
        return 1;
    }

    // Second call must be a no-op.
    if(EmuPrecommitThreadStack() != 0)
    {
        std::fprintf(stderr, "second pre-commit was not idempotent\n");
        return 1;
    }

    // Now corrupt the TIB the way the Xbox FS role does (StackBase becomes a
    // TLS pointer -- i.e. garbage as far as stack growth is concerned) and
    // consume far more stack than was originally committed. Without the
    // pre-commit this recursion needs kernel growth, which the corrupted TIB
    // prevents: the thread would die with an access violation below the old
    // committed floor. With the pre-commit it must complete.
    PVOID savedStackBase = tib->StackBase;
    tib->StackBase = (PVOID)(ULONG_PTR)0x00263024; // typical guest TLS pointer
    const int burned = BurnStack(kBurnFrames);
    tib->StackBase = savedStackBase;
    if(burned < 0)
    {
        return 1;
    }

    return 0;
}

} // namespace

int main()
{
    // Small committed stack inside a larger reservation: the shape guest
    // threads get (XBE-specified commit inside the generated exe's reserve).
    HANDLE thread = CreateThread(NULL, kReservationBytes, TestThread, NULL,
                                 STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if(thread == NULL)
    {
        std::fprintf(stderr, "CreateThread failed (%lu)\n", GetLastError());
        return 1;
    }
    WaitForSingleObject(thread, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    if(exitCode != 0)
    {
        std::fprintf(stderr, "stack pre-commit test FAILED (%lu)\n", exitCode);
        return 1;
    }
    std::puts("stack pre-commit: guest-role deep recursion survives without kernel growth");
    return 0;
}
