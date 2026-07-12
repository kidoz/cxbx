// Win32 services used by the platform-neutral trace core.
#include "trace_os.h"

#ifndef POINTER_64
#define POINTER_64 __ptr64
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <io.h>

namespace cxbx::trace
{
namespace
{

HANDLE g_EmergencyHandle = INVALID_HANDLE_VALUE;
HANDLE g_BackgroundThread = nullptr;
TraceOsThreadRoutine g_BackgroundRoutine = nullptr;
void* g_BackgroundContext = nullptr;

DWORD WINAPI BackgroundThreadEntry(void*) noexcept
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    if(g_BackgroundRoutine != nullptr)
    {
        g_BackgroundRoutine(g_BackgroundContext);
    }
    return 0;
}

} // namespace

std::uint32_t TraceOsThreadId() noexcept
{
    return static_cast<std::uint32_t>(GetCurrentThreadId());
}

std::uint64_t TraceOsNowTicks() noexcept
{
    LARGE_INTEGER counter{};
    if(QueryPerformanceCounter(&counter) == FALSE)
    {
        return 0;
    }
    return static_cast<std::uint64_t>(counter.QuadPart);
}

std::uint64_t TraceOsTickFrequency() noexcept
{
    LARGE_INTEGER frequency{};
    if(QueryPerformanceFrequency(&frequency) == FALSE || frequency.QuadPart <= 0)
    {
        return 1;
    }
    return static_cast<std::uint64_t>(frequency.QuadPart);
}

bool TraceOsEnvironmentExists(const char* name) noexcept
{
    if(name == nullptr)
    {
        return false;
    }
    SetLastError(ERROR_SUCCESS);
    const DWORD length = GetEnvironmentVariableA(name, nullptr, 0);
    return length != 0 || GetLastError() == ERROR_SUCCESS;
}

bool TraceOsReadEnvironment(const char* name, char* buffer, std::size_t capacity) noexcept
{
    if(name == nullptr || buffer == nullptr || capacity == 0 || capacity > MAXDWORD)
    {
        return false;
    }
    const DWORD length = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(capacity));
    return length != 0 && length < capacity;
}

void TraceOsEmergencyInitialize(std::FILE* output) noexcept
{
    g_EmergencyHandle = INVALID_HANDLE_VALUE;
    if(output == nullptr)
    {
        return;
    }
    const int descriptor = _fileno(output);
    if(descriptor < 0)
    {
        return;
    }
    const intptr_t handle = _get_osfhandle(descriptor);
    if(handle != -1)
    {
        g_EmergencyHandle = reinterpret_cast<HANDLE>(handle);
    }
}

void TraceOsEmergencyShutdown() noexcept
{
    g_EmergencyHandle = INVALID_HANDLE_VALUE;
}

void TraceOsEmergencyWrite(const char* data, std::size_t length) noexcept
{
    if(data == nullptr || length == 0 || length > MAXDWORD ||
       g_EmergencyHandle == INVALID_HANDLE_VALUE)
    {
        return;
    }
    DWORD written = 0;
    static_cast<void>(SetFilePointer(g_EmergencyHandle, 0, nullptr, FILE_END));
    static_cast<void>(WriteFile(g_EmergencyHandle, data, static_cast<DWORD>(length),
                                &written, nullptr));
}

bool TraceOsStartBackgroundThread(TraceOsThreadRoutine routine, void* context) noexcept
{
    if(routine == nullptr || g_BackgroundThread != nullptr)
    {
        return false;
    }
    g_BackgroundRoutine = routine;
    g_BackgroundContext = context;
    g_BackgroundThread = CreateThread(nullptr, 0, BackgroundThreadEntry, nullptr, 0, nullptr);
    if(g_BackgroundThread == nullptr)
    {
        g_BackgroundRoutine = nullptr;
        g_BackgroundContext = nullptr;
        return false;
    }
    return true;
}

void TraceOsJoinBackgroundThread() noexcept
{
    if(g_BackgroundThread == nullptr)
    {
        return;
    }
    static_cast<void>(WaitForSingleObject(g_BackgroundThread, INFINITE));
    CloseHandle(g_BackgroundThread);
    g_BackgroundThread = nullptr;
    g_BackgroundRoutine = nullptr;
    g_BackgroundContext = nullptr;
}

void TraceOsSleep(std::uint32_t milliseconds) noexcept
{
    Sleep(milliseconds);
}

} // namespace cxbx::trace
