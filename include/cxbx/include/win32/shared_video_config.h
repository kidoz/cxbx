// Shared video-configuration access for runtime consumers.
#pragma once

class XBVideo;

namespace cxbx::platform
{

void GetSharedVideoConfig(XBVideo& video);
void SetSharedVideoConfig(const XBVideo& video);

} // namespace cxbx::platform
