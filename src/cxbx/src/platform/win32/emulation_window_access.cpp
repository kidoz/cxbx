// Access to the active emulation window without exposing D3D8 declarations.
#include "emulation_window_access.h"

#include "emulation_runtime.h"

namespace XTL
{

extern HWND g_hEmuWindow;

} // namespace XTL

namespace cxbx::platform
{

void* GetEmulationWindow()
{
    return XTL::g_hEmuWindow;
}

} // namespace cxbx::platform
