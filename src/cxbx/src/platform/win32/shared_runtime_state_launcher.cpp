// Launcher-local shared-runtime access through the legacy exported storage ABI.
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

void SetSharedXbePath(const char (&path)[kSharedXbePathCapacity])
{
    g_EmuShared->SetXbePath(path);
}

void SetSharedContiguousWindow(std::uint32_t base, std::uint32_t size)
{
    g_EmuShared->SetContiguousWindow(base, size);
}

} // namespace cxbx::platform
