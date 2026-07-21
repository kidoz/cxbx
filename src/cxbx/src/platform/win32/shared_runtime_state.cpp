// Process-level shared-runtime access without exposing stored configuration types.
#include "shared_runtime_state.h"

#include "EmuShared.h"

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

} // namespace cxbx::platform
