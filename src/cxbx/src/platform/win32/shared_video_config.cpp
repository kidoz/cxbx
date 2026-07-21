// Shared video-configuration access for runtime consumers.
#include "shared_video_config.h"

#include "EmuShared.h"

namespace cxbx::platform
{

void GetSharedVideoConfig(XBVideo& video)
{
    g_EmuShared->GetXBVideo(&video);
}

void SetSharedVideoConfig(const XBVideo& video)
{
    g_EmuShared->SetXBVideo(&video);
}

} // namespace cxbx::platform
