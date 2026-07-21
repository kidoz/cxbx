// Access to the active emulation window without exposing D3D8 declarations.
#pragma once

namespace cxbx::platform
{

[[nodiscard]] void* GetEmulationWindow();

} // namespace cxbx::platform
