// Host-input lifecycle access for runtime consumers.
#pragma once

namespace XTL
{

bool EmuDInputInit();
void EmuDInputCleanup();
void EmuDInputNotifyDeviceChange();

} // namespace XTL

namespace cxbx::platform
{

bool InitializeHostInput();
void ShutdownHostInput();
void NotifyHostInputDeviceChange();

} // namespace cxbx::platform
