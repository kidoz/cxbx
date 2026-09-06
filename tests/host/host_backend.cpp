// Host-backend selection contract: CXBX_HOST_BACKEND parsing must map every
// unrecognized value back to the d3d8 default so a mistyped override can
// never disable the working presenter.
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
        { nullptr, cxbx::d3d8::HostBackendFlavor::D3D8 },
        { "", cxbx::d3d8::HostBackendFlavor::D3D8 },
        { "d3d8", cxbx::d3d8::HostBackendFlavor::D3D8 },
        { "D3D8", cxbx::d3d8::HostBackendFlavor::D3D8 },
        { "vulkan", cxbx::d3d8::HostBackendFlavor::Vulkan },
        { "VULKAN", cxbx::d3d8::HostBackendFlavor::Vulkan },
        { "Vulkan", cxbx::d3d8::HostBackendFlavor::Vulkan },
        { " dxvk", cxbx::d3d8::HostBackendFlavor::D3D8 },
        { "dxvk", cxbx::d3d8::HostBackendFlavor::D3D8 },
        { "vulkanx", cxbx::d3d8::HostBackendFlavor::D3D8 },
        { "vulkan ", cxbx::d3d8::HostBackendFlavor::D3D8 },
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
