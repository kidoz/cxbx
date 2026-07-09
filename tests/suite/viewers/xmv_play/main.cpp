// xmv_play -- minimal XMV video player. Decodes the SDK's Test.xmv frame by
// frame through the genuine XDK xmv.lib (XMVDecoder_GetNextFrame), software
// converts each YUY2 frame to RGB, blits it to the lockable back buffer and
// Presents, so the movie plays in the Cxbx window. It also writes one decoded
// frame to D:\xmvframe.bmp so the result can be verified without a display.
//
// This is the visible end of the XMV work: the xmv_decode probe proved frames
// decode; this shows them on screen.
#include "xdk_xtrace.h"
#include <xmv.h>

// Converted BGRA scratch for one frame (sized for the largest plausible XMV).
static unsigned char g_frame[720 * 576 * 4];
static unsigned char g_row[720 * 3 + 4];

static int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// One YUV sample -> 0x00RRGGBB (BT.601, studio-swing).
static unsigned long yuv2rgb(int Y, int U, int V)
{
    int C = Y - 16, D = U - 128, E = V - 128;
    int R = (298 * C + 409 * E + 128) >> 8;
    int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
    int B = (298 * C + 516 * D + 128) >> 8;
    return ((unsigned long)clamp255(R) << 16) |
           ((unsigned long)clamp255(G) << 8)  |
           (unsigned long)clamp255(B);
}

// Write a 24-bit bottom-up BMP of a w*h BGRA buffer to a D:\ path.
static void dump_bmp(const char *path, int w, int h, const unsigned char *bgra)
{
    HANDLE f = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;

    int rowsz = w * 3;
    int pad = (4 - (rowsz & 3)) & 3;
    int imgsz = (rowsz + pad) * h;

    unsigned char hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    *(unsigned long *)&hdr[2]  = 54 + imgsz;
    *(unsigned long *)&hdr[10] = 54;
    *(unsigned long *)&hdr[14] = 40;
    *(long *)&hdr[18] = w;
    *(long *)&hdr[22] = h;
    *(unsigned short *)&hdr[26] = 1;
    *(unsigned short *)&hdr[28] = 24;
    *(unsigned long *)&hdr[34] = imgsz;

    DWORD cb;
    WriteFile(f, hdr, 54, &cb, NULL);
    for (int y = h - 1; y >= 0; y--) {
        const unsigned char *s = bgra + y * w * 4;
        for (int x = 0; x < w; x++) {
            g_row[x * 3 + 0] = s[x * 4 + 0];
            g_row[x * 3 + 1] = s[x * 4 + 1];
            g_row[x * 3 + 2] = s[x * 4 + 2];
        }
        for (int p = 0; p < pad; p++)
            g_row[rowsz + p] = 0;
        WriteFile(f, g_row, rowsz + pad, &cb, NULL);
    }
    CloseHandle(f);
}

void __cdecl main()
{
    xt_begin("xmv_play");

    LPDIRECT3D8 pD3D = Direct3DCreate8(D3D_SDK_VERSION);
    xt_chk("xmv.d3d", 1, pD3D != NULL);
    if (pD3D == NULL) xt_end_and_exit();

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth  = 640;
    pp.BackBufferHeight = 480;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount  = 1;
    pp.SwapEffect       = D3DSWAPEFFECT_DISCARD;
    pp.Flags            = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

    D3DDevice *pDev = NULL;
    HRESULT hr = pD3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                                    D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &pDev);
    xt_chk("xmv.device", 1, SUCCEEDED(hr) && pDev != NULL);
    if (FAILED(hr) || pDev == NULL) xt_end_and_exit();

    XMVDecoder *pDec = NULL;
    hr = XMVDecoder_CreateDecoderForFile(0, "D:\\Media\\Videos\\Test.xmv", &pDec);
    xt_chk("xmv.create", 1, SUCCEEDED(hr) && pDec != NULL);
    if (FAILED(hr) || pDec == NULL) xt_end_and_exit();

    XMVVIDEO_DESC desc;
    memset(&desc, 0, sizeof(desc));
    XMVDecoder_GetVideoDescriptor(pDec, &desc);
    int vw = (int)desc.Width, vh = (int)desc.Height;
    xt_chk("xmv.size", 1, vw > 0 && vw <= 720 && vh > 0 && vh <= 576);
    if (vw <= 0 || vw > 720 || vh <= 0 || vh > 576) xt_end_and_exit();

    IDirect3DTexture8 *pTex = NULL;
    pDev->CreateTexture(vw, vh, 1, 0, D3DFMT_YUY2, 0, &pTex);
    IDirect3DSurface8 *pSurf = NULL;
    if (pTex != NULL)
        pTex->GetSurfaceLevel(0, &pSurf);
    xt_chk("xmv.surface", 1, pSurf != NULL);

    IDirect3DSurface8 *pBack = NULL;
    pDev->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBack);

    int ox = (640 - vw) / 2, oy = (480 - vh) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    int frames = 0, locked = 0;
    unsigned long chksum = 0;

    for (int i = 0; i < 4000; i++) {
        XMVRESULT xr = XMV_NOFRAME;
        __try {
            XMVDecoder_GetNextFrame(pDec, pSurf, &xr, NULL);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }

        if (xr == XMV_NEWFRAME && pSurf != NULL) {
            D3DLOCKED_RECT s;
            if (SUCCEEDED(pSurf->LockRect(&s, NULL, 0))) {
                locked = 1;
                for (int y = 0; y < vh; y++) {
                    unsigned char *srow = (unsigned char *)s.pBits + y * s.Pitch;
                    unsigned char *drow = g_frame + (y * vw) * 4;
                    for (int x = 0; x + 1 < vw; x += 2) {
                        int Y0 = srow[x * 2 + 0], U = srow[x * 2 + 1];
                        int Y1 = srow[x * 2 + 2], V = srow[x * 2 + 3];
                        unsigned long p0 = yuv2rgb(Y0, U, V);
                        unsigned long p1 = yuv2rgb(Y1, U, V);
                        drow[x * 4 + 0] = (unsigned char)(p0 & 0xFF);
                        drow[x * 4 + 1] = (unsigned char)((p0 >> 8) & 0xFF);
                        drow[x * 4 + 2] = (unsigned char)((p0 >> 16) & 0xFF);
                        drow[x * 4 + 3] = 0xFF;
                        drow[(x + 1) * 4 + 0] = (unsigned char)(p1 & 0xFF);
                        drow[(x + 1) * 4 + 1] = (unsigned char)((p1 >> 8) & 0xFF);
                        drow[(x + 1) * 4 + 2] = (unsigned char)((p1 >> 16) & 0xFF);
                        drow[(x + 1) * 4 + 3] = 0xFF;
                        chksum += p0 + p1;
                    }
                }
                pSurf->UnlockRect();

                if (pBack != NULL) {
                    D3DLOCKED_RECT bd;
                    if (SUCCEEDED(pBack->LockRect(&bd, NULL, 0))) {
                        for (int y = 0; y < vh && (y + oy) < 480; y++) {
                            unsigned char *srow = g_frame + (y * vw) * 4;
                            unsigned char *drow = (unsigned char *)bd.pBits +
                                                  (y + oy) * bd.Pitch + ox * 4;
                            memcpy(drow, srow, vw * 4);
                        }
                        pBack->UnlockRect();
                    }
                }
                pDev->Present(NULL, NULL, NULL, NULL);

                frames++;
                if (frames == 30)
                    dump_bmp("D:\\xmvframe.bmp", vw, vh, g_frame);
                if (frames == 150)
                    dump_bmp("D:\\xmvframe2.bmp", vw, vh, g_frame);
            }
        }
        Sleep(10);
    }

    xt_chk("xmv.surface_lockable", 1, locked);
    xt_chk("xmv.frames_played", 1, frames > 0);
    xt_emitf("EV  xmv.play frames=%d size=%dx%d chksum=0x%08lX",
             frames, vw, vh, (unsigned long)chksum);

    XMVDecoder_CloseDecoder(pDec);
    xt_end_and_exit();
}
