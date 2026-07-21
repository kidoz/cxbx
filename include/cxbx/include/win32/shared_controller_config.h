// Shared controller-configuration access for runtime consumers.
#pragma once

class XBController;

namespace cxbx::platform
{

void GetSharedControllerConfig(XBController& controller);

} // namespace cxbx::platform
