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

void GetSharedXbePath(char (&path)[kSharedXbePathCapacity])
{
    g_EmuShared->GetXbePath(path);
}

void SetSharedXbePath(const char (&path)[kSharedXbePathCapacity])
{
    g_EmuShared->SetXbePath(path);
}

} // namespace cxbx::platform
