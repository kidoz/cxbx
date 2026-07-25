// XAPI dashboard video configuration regression coverage.

#include "xdk_xtrace.h"

void __cdecl main()
{
    xt_begin("xapi_video_config");

    xt_chk("xapi.av_pack_standard",
           XC_AV_PACK_STANDARD,
           XGetAVPack());
    xt_chk("xapi.video_standard_ntsc_m",
           XC_VIDEO_STANDARD_NTSC_M,
           XGetVideoStandard());
    xt_chk("xapi.video_flags_default",
           0,
           XGetVideoFlags());

    xt_end_and_exit();
}
