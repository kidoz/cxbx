// SPDX-License-Identifier: MIT
//
// gfx - graphics/video-memory path. Sets a video mode, gets the framebuffer,
// writes a deterministic ARGB pattern into a block, reads it back, and hashes
// it (FNV-1a) for a stable fingerprint.
//
// SCOPE: this exercises the framebuffer *memory* path (allocation, CPU
// read/write), which nxdk backs with contiguous memory and which works even on
// an HLE emulator. It does NOT test NV2A GPU command submission / rendering --
// that requires a GPU-emulating target (xemu, Cxbx-Reloaded) or real hardware.
// See tests/suite/README.md.

#include "xtest.h"
#include <windows.h>
#include <hal/video.h>

#define BW 64
#define BH 64

static uint32_t pattern_px(int x, int y)
{
    return 0xFF000000u
         | ((uint32_t)((x * 4) & 0xFF) << 16)
         | ((uint32_t)((y * 4) & 0xFF) << 8)
         | (uint32_t)((x ^ y) & 0xFF);
}

int main(void)
{
    xt_begin("v1", "gfx");
    xt_note("framebuffer memory path (write/readback/hash)");
    xt_note("NV2A rendering conformance needs a GPU-emulating target - see README");

    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
    const int stride = 640;   // pixels per row at 640x480x32

    unsigned char *fb = XVideoGetFB();
    xt_ev("XVideoGetFB -> 0x%08lX", (unsigned long)(uintptr_t)fb);
    xt_check_bool("gfx.fb_nonnull", 1, fb != NULL);

    if (fb != NULL) {
        volatile uint32_t *px = (volatile uint32_t *)fb;

        // Write the pattern.
        for (int y = 0; y < BH; y++)
            for (int x = 0; x < BW; x++)
                px[y * stride + x] = pattern_px(x, y);

        // Read back and verify + hash (FNV-1a over the block, row-packed).
        int ok = 1;
        uint32_t h = 2166136261u;
        for (int y = 0; y < BH; y++) {
            for (int x = 0; x < BW; x++) {
                uint32_t v = px[y * stride + x];
                if (v != pattern_px(x, y)) ok = 0;
                for (int b = 0; b < 4; b++) {
                    h ^= (v >> (b * 8)) & 0xFF;
                    h *= 16777619u;
                }
            }
        }
        xt_check_bool("gfx.fb_readback", 1, ok);
        // Deterministic fingerprint of the rendered block. Golden-tracked per
        // emulator; a change flags a framebuffer-format/stride divergence.
        xt_ev("gfx.block_fnv1a=0x%08lX", (unsigned long)h);
        xt_check_u32("gfx.block_hash", 0x700999C5u, h);
    }

    return xt_end();
}
