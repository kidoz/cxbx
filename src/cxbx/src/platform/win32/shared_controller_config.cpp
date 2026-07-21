// Shared controller-configuration access for runtime consumers.
#include "shared_controller_config.h"

#include "EmuShared.h"

namespace cxbx::platform
{

void GetSharedControllerConfig(XBController& controller)
{
    g_EmuShared->GetXBController(&controller);
}

void SetSharedControllerConfig(const XBController& controller)
{
    g_EmuShared->SetXBController(&controller);
}

} // namespace cxbx::platform
