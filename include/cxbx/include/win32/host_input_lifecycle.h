// Host-input lifecycle access for runtime consumers.
#pragma once

namespace cxbx::platform
{

bool InitializeHostInput();
void ShutdownHostInput();
void NotifyHostInputDeviceChange();

} // namespace cxbx::platform
