// Shared video-configuration access for runtime consumers.
#include "shared_video_config.h"

#include "EmuShared.h"

namespace cxbx::platform
{

void GetSharedVideoConfig(XBVideo& video)
{
    g_EmuShared->GetXBVideo(&video);
}

} // namespace cxbx::platform
