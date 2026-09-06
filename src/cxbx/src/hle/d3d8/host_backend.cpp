#include "host_backend.h"

#include "vulkan/vulkan_backend.h"

// The vendored DirectX SDK basetsd.h shadows the Windows SDK one and does
// not define POINTER_64, which winnt.h requires (same workaround as the
// other windows.h consumers in this tree).
#define POINTER_64 __ptr64

#include <windows.h>

#include <cstdio>

namespace cxbx
{
namespace d3d8
{

namespace
{
HostBackendFlavor g_ActiveFlavor = HostBackendFlavor::D3D8;

bool EnvironmentFlagEnabled(const char* name)
{
    char value[8] = {};
    const DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
    return length > 0 && length < sizeof(value) && value[0] == '1';
}
} // namespace

HostBackendFlavor HostBackendFlavorFromEnvironment()
{
    char value[16] = {};
    const DWORD length =
        GetEnvironmentVariableA("CXBX_HOST_BACKEND", value, sizeof(value));
    if(length == 0 || length >= sizeof(value))
    {
        return HostBackendFlavor::D3D8;
    }
    return HostBackendParseFlavor(value);
}

void HostBackendInitialize(const void* nativeWindow)
{
    const HostBackendFlavor flavor = HostBackendFlavorFromEnvironment();
    g_ActiveFlavor = flavor;

    if(flavor != HostBackendFlavor::Vulkan)
    {
        return;
    }

    const bool validate = EnvironmentFlagEnabled("CXBX_VULKAN_VALIDATE");
    printf("VULKAN| CXBX_HOST_BACKEND=vulkan: smoke bootstrap (validation=%d)\n",
           validate ? 1 : 0);
    if(!vulkan::SmokeBootstrap(nativeWindow, validate))
    {
        printf("VULKAN| smoke bootstrap failed; d3d8 remains the presenter\n");
        return;
    }
    printf("VULKAN| smoke bootstrap ok; d3d8 remains the presenter (P0)\n");
}

void HostBackendShutdown()
{
    // The P0 smoke bootstrap owns no cross-call state; nothing to release.
}

HostBackendFlavor HostBackendActive()
{
    return g_ActiveFlavor;
}

} // namespace d3d8
} // namespace cxbx
