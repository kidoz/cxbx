// Host-backend selection contract: P8 flipped the default to the native
// Vulkan backend. Null, empty, unrecognized, and "vulkan" (any case) select
// Vulkan; only the explicit string "d3d8" opts into the legacy host-d3d8
// presenter. A machine without a working Vulkan device still ends up on d3d8
// through the runtime bring-up fallback (HostBackendInitialize latches the
// presenter off), so a mistyped override can never disable rendering.
#include "host_backend.h"

#include <cstdio>

int main()
{
    struct Case
    {
        const char* value;
        cxbx::d3d8::HostBackendFlavor expected;
    };

    const Case cases[] = {
        { nullptr, cxbx::d3d8::HostBackendFlavor::Vulkan },
        { "", cxbx::d3d8::HostBackendFlavor::Vulkan },
        { "d3d8", cxbx::d3d8::HostBackendFlavor::D3D8 },
        { "D3D8", cxbx::d3d8::HostBackendFlavor::D3D8 },
        { "d3d81", cxbx::d3d8::HostBackendFlavor::Vulkan },
        { "ad3d8", cxbx::d3d8::HostBackendFlavor::Vulkan },
        { "vulkan", cxbx::d3d8::HostBackendFlavor::Vulkan },
        { "VULKAN", cxbx::d3d8::HostBackendFlavor::Vulkan },
        { "Vulkan", cxbx::d3d8::HostBackendFlavor::Vulkan },
        { " dxvk", cxbx::d3d8::HostBackendFlavor::Vulkan },
        { "dxvk", cxbx::d3d8::HostBackendFlavor::Vulkan },
        { "vulkanx", cxbx::d3d8::HostBackendFlavor::Vulkan },
        { "vulkan ", cxbx::d3d8::HostBackendFlavor::Vulkan },
    };

    for(const Case& c : cases)
    {
        if(cxbx::d3d8::HostBackendParseFlavor(c.value) != c.expected)
        {
            std::fprintf(stderr,
                         "CXBX_HOST_BACKEND value \"%s\" must parse as flavor %d\n",
                         c.value != nullptr ? c.value : "(null)",
                         static_cast<int>(c.expected));
            return 1;
        }
    }

    return 0;
}
