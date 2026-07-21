// Host-input lifecycle access for runtime consumers.
#include "host_input_lifecycle.h"

#include "XBController.h"

namespace cxbx::platform
{

bool InitializeHostInput()
{
    return XTL::EmuDInputInit();
}

void ShutdownHostInput()
{
    XTL::EmuDInputCleanup();
}

void NotifyHostInputDeviceChange()
{
    XTL::EmuDInputNotifyDeviceChange();
}

} // namespace cxbx::platform
