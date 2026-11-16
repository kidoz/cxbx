// Process-level shared-runtime access without exposing stored configuration types.
#include "shared_runtime_state.h"

#include "shared_runtime_storage.h"

namespace cxbx::platform
{

void InitializeSharedRuntime()
{
    EmuShared::Init();
}

void ShutdownSharedRuntime()
{
    EmuShared::Cleanup();
}

void DisableSharedRuntimePersist()
{
    EmuShared::DisablePersist();
}

void GetSharedXbePath(char (&path)[kSharedXbePathCapacity])
{
    g_EmuShared->GetXbePath(path);
}

void SetSharedXbePath(const char (&path)[kSharedXbePathCapacity])
{
    g_EmuShared->SetXbePath(path);
}

void GetSharedContiguousWindow(std::uint32_t& base, std::uint32_t& size)
{
    ULONG Base = 0;
    ULONG Size = 0;
    g_EmuShared->GetContiguousWindow(&Base, &Size);
    base = Base;
    size = Size;
}

} // namespace cxbx::platform
