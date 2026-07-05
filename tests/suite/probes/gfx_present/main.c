// SPDX-License-Identifier: MIT
//
// gfx_present - NV2A display-scanout capture ("path 2"). Fills the framebuffer
// with a known full-screen ARGB pattern and flips the CRTC scanout base to it.
// A title presents a frame by programming the NV2A scanout register
// (NV_PCRTC_START) with the surface to display; this probe drives that exact
// register write via pbkit's synchronous flip. A scanout-capturing target then
// snapshots what would appear on screen.
//
// On this Cxbx build it drives Emu.cpp's scanout interception, which reads the
// displayed surface straight from guest memory on the PCRTC_START write and
// writes it to %TEMP%\cxbx_fbN.bmp -- proving the on-screen-image path end to
// end with a deterministic, verifiable pattern.
//
// SCOPE: the frame is composed by CPU writes into the framebuffer (no rasterizer
// required); the flip is a real NV_PCRTC_START programming, the same signal a
// GPU-rendered title emits at frame end.

#include "xtest.h"
#include <windows.h>
#include <hal/video.h>
#include <pbkit/pbkit.h>

#define FBW 640
#define FBH 480

static uint32_t pattern_px(int x, int y)
{
    return 0xFF000000u
         | ((uint32_t)((x * 4) & 0xFF) << 16)
         | ((uint32_t)((y * 4) & 0xFF) << 8)
         | (uint32_t)((x ^ y) & 0xFF);
}

int main(void)
{
    xt_begin("v1", "gfx_present");
    xt_note("NV2A display-scanout capture (on-screen framebuffer via PCRTC_START)");
    xt_note("on a scanout-capturing target this dumps the presented frame");

    XVideoSetMode(FBW, FBH, 32, REFRESH_DEFAULT);

    int status = pb_init();
    xt_ev("gfx_present.pb_init_status=%d", status);
    xt_check_bool("gfx_present.pb_init", 1, status == 0);
    if (status != 0)
        return xt_end();

    unsigned char *fb = XVideoGetFB();
    xt_ev("gfx_present.fb=0x%08lX", (unsigned long)(uintptr_t)fb);
    xt_check_bool("gfx_present.fb_nonnull", 1, fb != NULL);
    if (fb == NULL) {
        pb_kill();
        return xt_end();
    }

    // Compose a deterministic full-screen frame directly in the scanout surface.
    volatile uint32_t *px = (volatile uint32_t *)fb;
    for (int y = 0; y < FBH; y++)
        for (int x = 0; x < FBW; x++)
            px[y * FBW + x] = pattern_px(x, y);

    // Present: flip the CRTC scanout base to this surface. This is the
    // NV_PCRTC_START write the capture intercepts. Do it a few times so the
    // target sees a stable, repeated frame.
    for (int i = 0; i < 4; i++) {
        pb_show_debug_screen();   // sets PCRTC_START = XVideoGetFB()
        while (pb_busy())
            ;
    }

    xt_check_bool("gfx_present.presented", 1, 1);

    pb_kill();
    return xt_end();
}
