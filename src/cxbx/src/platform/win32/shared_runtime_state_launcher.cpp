// Launcher-local shared-runtime access through the legacy exported storage ABI.
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

void SetSharedXbePath(const char (&path)[kSharedXbePathCapacity])
{
    g_EmuShared->SetXbePath(path);
}

} // namespace cxbx::platform
