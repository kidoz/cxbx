// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbxkrnl->EmuD3D8.cpp
// *
// *  This file is part of the cxbx project.
// *
// *  cxbx and cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file LICENSE.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *
// *  All rights reserved
// *
// ******************************************************************
#define _CXBXKRNL_INTERNAL
#define _XBOXKRNL_LOCAL_

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace xboxkrnl
{
    #include <xboxkrnl/xboxkrnl.h>
};

#include "Emu.h"
#include "EmuVshDecoder.h"
#include "EmuFS.h"
#include "EmuShared.h"
#include "core/d3d_push_buffer.h"
#include "core/d3d_pixel_shader.h"
#include "core/d3d_pixel_shader_translate.h"
#include "core/d3d_present.h"
#include "core/d3d_texture.h"
#include "core/trace.h"
#include "core/Yuy2Converter.h"

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace XTL
{
    #include "EmuXTL.h"
};

#include "ResCxbxDll.h"

#include <process.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <clocale>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#include <vector>

extern "C" bool EmuNv2aExecutePushBuffer(const DWORD* Buffer, DWORD Size);
extern "C" bool EmuNv2aExecuteGuestPushBuffer(DWORD GuestAddress, DWORD Size);
extern "C" void EmuNv2aEnableHleRaster();

// ******************************************************************
// * Global(s)
// ******************************************************************
HWND XTL::g_hEmuWindow = NULL;   // Rendering Window

// ******************************************************************
// * Static Function(s)
// ******************************************************************
static DWORD WINAPI   EmuRenderWindow(LPVOID);
static DWORD WINAPI   EmuCreateDeviceProxy(LPVOID);
static LRESULT WINAPI EmuMsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static DWORD WINAPI   EmuUpdateTickCount(LPVOID);
static DWORD          EmuCheckAllocationSize(LPVOID);
static inline void    EmuVerifyResourceIsRegistered(XTL::X_D3DResource *pResource);
static void           EmuAdjustPower2(UINT *dwWidth, UINT *dwHeight);
static void           EmuFlushTiledSurfaceLock(XTL::X_D3DResource *pResource);
static void           EmuFlushTiledSurfaceLocks();
static HRESULT        EmuLockTiledSurface(XTL::X_D3DResource *pResource, XTL::D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect, DWORD Flags);

// ******************************************************************
// * Static Variable(s)
// ******************************************************************
static XTL::LPDIRECT3D8             g_pD3D8         = NULL; // Direct3D8
static XTL::LPDIRECT3DDEVICE8       g_pD3DDevice8   = NULL; // Direct3D8 Device
static BOOL                         g_bSupportsYUY2 = FALSE;// Does device support YUY2 overlays?
static BOOL                         g_bYuvEnable    = FALSE;// D3DRS_YUVENABLE: sample bound textures as YUY2 and convert to RGB
static BOOL                         g_bStage0ConvertedYuv = FALSE;
static XTL::LPDIRECTDRAW7           g_pDD7          = NULL; // DirectDraw7
static XTL::LPDIRECTDRAWSURFACE7    g_pDDSPrimary   = NULL; // DirectDraw7 Primary Surface
static XTL::LPDIRECTDRAWSURFACE7    g_pDDSOverlay7  = NULL; // DirectDraw7 Overlay Surface
static DWORD                        g_dwOverlayW    = 640;  // Cached Overlay Width
static DWORD                        g_dwOverlayH    = 480;  // Cached Overlay Height

// Converted RGB copy of the latest UpdateOverlay frame (owned by the YUY2
// conversion cache, do not Release); composited over the backbuffer at Swap
// while the title keeps the overlay enabled.
static XTL::IDirect3DTexture8      *g_pOverlayFrameTexture = NULL;
static std::atomic<std::uint32_t> g_OverlayFrameGeneration{ 0 };
static LARGE_INTEGER              g_NextOverlayUpdate = {};
static LARGE_INTEGER              g_NextSwapPresent = {};
static Xbe::Header                 *g_XbeHeader     = NULL; // XbeHeader
static uint32                       g_XbeHeaderSize = 0;    // XbeHeaderSize
static XTL::D3DCAPS8                g_D3DCaps;              // Direct3D8 Caps
static HBRUSH                       g_hBgBrush      = NULL; // Background Brush
static std::atomic_bool             g_bRenderWindowActive{ false };
static XBVideo                      g_XBVideo;
static DWORD                        g_D3DDebugMarker = 0;
static uint08                      *g_pD3DPerfStatistics = NULL;
static volatile DWORD              *g_pD3DSingleStepPusher = NULL;
static bool                         g_bD3DDebugGlobalsScanned = false;

// Last D3D wrapper entered -- a lightweight (single store) entry trace so the
// vectored-exception dump can name the HLE wrapper that was on the call path
// when host d3d8 throws (e.g. the 0xE06D7363/D3DERR_NOTAVAILABLE storm on the
// Turok Evolution render path). Printed by the vectored handler in Emu.cpp via
// EmuGetLastD3DCall(). The store is now unconditional (it earned its keep on
// every Turok crash triage); CXBX_D3D_CALL_STATS additionally counts the
// calls per wrapper and dumps the table every 128 Swaps, which shows what a
// title's frame is actually made of (e.g. a black screen: are draws even
// running?).
static volatile const char        *g_LastD3DCall = NULL;

#define EMU_D3D_CALL_STATS_SLOTS 96
struct EmuD3DCallStat { const char *Name; DWORD Count; };
static EmuD3DCallStat g_D3DCallStats[EMU_D3D_CALL_STATS_SLOTS] = {0};
static int g_D3DCallStatsEnabled = -1;

static void EmuD3DTraceEntry(const char *Name)
{
    g_LastD3DCall = Name;

    if(g_D3DCallStatsEnabled < 0)
        g_D3DCallStatsEnabled = (getenv("CXBX_D3D_CALL_STATS") != NULL) ? 1 : 0;

    if(!g_D3DCallStatsEnabled)
        return;

    // Names are string literals, so pointer identity suffices.
    for(int i = 0; i < EMU_D3D_CALL_STATS_SLOTS; i++)
    {
        if(g_D3DCallStats[i].Name == Name)
        {
            g_D3DCallStats[i].Count++;
            return;
        }

        if(g_D3DCallStats[i].Name == NULL)
        {
            g_D3DCallStats[i].Name = Name;
            g_D3DCallStats[i].Count = 1;
            return;
        }
    }
}

static void EmuD3DDumpCallStats(void)
{
    printf("D3DSTAT| ---- calls since last dump ----\n");
    for(int i = 0; i < EMU_D3D_CALL_STATS_SLOTS && g_D3DCallStats[i].Name != NULL; i++)
    {
        printf("D3DSTAT| %-32s %u\n", g_D3DCallStats[i].Name, g_D3DCallStats[i].Count);
        g_D3DCallStats[i].Count = 0;
    }
}

template <size_t Size>
constexpr uint32 EmuD3DTraceId(const char (&Name)[Size])
{
    uint32 hash = 2166136261u;
    for(size_t i = 0; i + 1 < Size; ++i)
    {
        hash ^= static_cast<std::uint8_t>(Name[i]);
        hash *= 16777619u;
    }
    return hash;
}

#define D3D_TRACE(name)                                                                  \
    do                                                                                   \
    {                                                                                    \
        cxbx::trace::RecordFlight(cxbx::trace::Event::D3dBoundary, EmuD3DTraceId(name)); \
        EmuD3DTraceEntry(name);                                                          \
    } while(0)
extern "C" const char *EmuGetLastD3DCall(void) { return (const char *)g_LastD3DCall; }

static HRESULT EmuHostEndScene()
{
    __try
    {
        return g_pD3DDevice8->EndScene();
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return D3DERR_INVALIDCALL;
    }
}

static HRESULT EmuHostPresent(const RECT* sourceRect, const RECT* destinationRect,
                              HWND destinationWindow, const RGNDATA* dirtyRegion)
{
    __try
    {
        return g_pD3DDevice8->Present(sourceRect, destinationRect, destinationWindow,
                                      dirtyRegion);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return D3DERR_INVALIDCALL;
    }
}

static HRESULT EmuHostBeginScene()
{
    __try
    {
        return g_pD3DDevice8->BeginScene();
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return D3DERR_INVALIDCALL;
    }
}

static bool EmuReadHostBackbufferPixel(UINT x, UINT y, DWORD& pixel)
{
    XTL::IDirect3DSurface8* backBuffer = nullptr;
    bool read = false;
    __try
    {
        if(SUCCEEDED(g_pD3DDevice8->GetBackBuffer(
               0, XTL::D3DBACKBUFFER_TYPE_MONO, &backBuffer)) &&
           backBuffer != nullptr)
        {
            XTL::D3DSURFACE_DESC description = {
                XTL::D3DFMT_UNKNOWN, XTL::D3DRTYPE_SURFACE, 0, XTL::D3DPOOL_DEFAULT,
                0, XTL::D3DMULTISAMPLE_NONE, 0, 0
            };
            XTL::D3DLOCKED_RECT locked{};
            if(SUCCEEDED(backBuffer->GetDesc(&description)) && x < description.Width &&
               y < description.Height &&
               SUCCEEDED(backBuffer->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
            {
                const BYTE* row = static_cast<const BYTE*>(locked.pBits) +
                                  static_cast<std::size_t>(y) * locked.Pitch;
                std::memcpy(&pixel, row + static_cast<std::size_t>(x) * sizeof(pixel),
                            sizeof(pixel));
                backBuffer->UnlockRect();
                read = true;
            }
            backBuffer->Release();
            backBuffer = nullptr;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        if(backBuffer != nullptr)
        {
            backBuffer->Release();
        }
        read = false;
    }
    return read;
}

static void EmuDrawHostControlTriangles(HRESULT& simpleResult, HRESULT& complexResult)
{
    struct ControlVertex
    {
        float x;
        float y;
        float z;
        float rhw;
        DWORD diffuse;
    };
    struct ComplexControlVertex
    {
        float x;
        float y;
        float z;
        float rhw;
        float pointSize;
        DWORD diffuse;
        DWORD specular;
        float texCoords[4][2];
    };
    const ControlVertex vertices[3] = {
        { 16.0f, 16.0f, 0.0f, 1.0f, 0xFFFF00FFu },
        { 96.0f, 16.0f, 0.0f, 1.0f, 0xFFFF00FFu },
        { 16.0f, 96.0f, 0.0f, 1.0f, 0xFFFF00FFu },
    };
    const ComplexControlVertex complexVertices[3] = {
        { 128.0f, 16.0f, 0.0f, 1.0f, 1.0f, 0xFF00FFFFu, 0, {} },
        { 208.0f, 16.0f, 0.0f, 1.0f, 1.0f, 0xFF00FFFFu, 0, {} },
        { 128.0f, 96.0f, 0.0f, 1.0f, 1.0f, 0xFF00FFFFu, 0, {} },
    };
    DWORD savedState = 0;
    simpleResult = D3DERR_INVALIDCALL;
    complexResult = D3DERR_INVALIDCALL;
    __try
    {
        const HRESULT stateResult =
            g_pD3DDevice8->CreateStateBlock(XTL::D3DSBT_ALL, &savedState);
        if(SUCCEEDED(stateResult))
        {
            g_pD3DDevice8->SetPixelShader(0);
            g_pD3DDevice8->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
            g_pD3DDevice8->SetRenderState(XTL::D3DRS_ZENABLE, FALSE);
            g_pD3DDevice8->SetRenderState(XTL::D3DRS_CULLMODE, XTL::D3DCULL_NONE);
            g_pD3DDevice8->SetRenderState(XTL::D3DRS_ALPHABLENDENABLE, FALSE);
            g_pD3DDevice8->SetRenderState(XTL::D3DRS_ALPHATESTENABLE, FALSE);
            g_pD3DDevice8->SetRenderState(XTL::D3DRS_COLORWRITEENABLE, 0xFu);
            g_pD3DDevice8->SetTextureStageState(0, XTL::D3DTSS_COLOROP,
                                                XTL::D3DTOP_SELECTARG1);
            g_pD3DDevice8->SetTextureStageState(0, XTL::D3DTSS_COLORARG1,
                                                D3DTA_DIFFUSE);
            g_pD3DDevice8->SetTextureStageState(1, XTL::D3DTSS_COLOROP,
                                                XTL::D3DTOP_DISABLE);
            simpleResult = g_pD3DDevice8->DrawPrimitiveUP(
                XTL::D3DPT_TRIANGLELIST, 1, vertices, sizeof(ControlVertex));
            g_pD3DDevice8->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE |
                                           D3DFVF_SPECULAR | D3DFVF_PSIZE |
                                           D3DFVF_TEX4);
            complexResult = g_pD3DDevice8->DrawPrimitiveUP(
                XTL::D3DPT_TRIANGLELIST, 1, complexVertices,
                sizeof(ComplexControlVertex));
            g_pD3DDevice8->ApplyStateBlock(savedState);
            g_pD3DDevice8->DeleteStateBlock(savedState);
            savedState = 0;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        simpleResult = D3DERR_INVALIDCALL;
        complexResult = D3DERR_INVALIDCALL;
    }
}

static void EmuRunHostControlProbe()
{
    static bool attempted = false;
    if(attempted ||
       !cxbx::d3d::ControlProbeEnabled(getenv("CXBX_D3D_CONTROL_PROBE")))
    {
        return;
    }
    attempted = true;

    DWORD beforePixel = 0;
    DWORD afterPixel = 0;
    DWORD beforeComplexPixel = 0;
    DWORD afterComplexPixel = 0;
    const bool beforeRead = EmuReadHostBackbufferPixel(32, 32, beforePixel);
    const bool beforeComplexRead =
        EmuReadHostBackbufferPixel(144, 32, beforeComplexPixel);
    const HRESULT beginResult = EmuHostBeginScene();
    HRESULT simpleDrawResult = D3DERR_INVALIDCALL;
    HRESULT complexDrawResult = D3DERR_INVALIDCALL;
    HRESULT endResult = D3DERR_INVALIDCALL;
    if(SUCCEEDED(beginResult))
    {
        EmuDrawHostControlTriangles(simpleDrawResult, complexDrawResult);
        endResult = EmuHostEndScene();
    }
    const bool afterRead = EmuReadHostBackbufferPixel(32, 32, afterPixel);
    const bool afterComplexRead =
        EmuReadHostBackbufferPixel(144, 32, afterComplexPixel);
    printf("D3DPROBE| control_triangle begin=%08lX simple_draw=%08lX "
           "complex_draw=%08lX end=%08lX simple_before=%08lX simple_after=%08lX "
           "simple_changed=%u complex_before=%08lX complex_after=%08lX complex_changed=%u\n",
           beginResult, simpleDrawResult, complexDrawResult, endResult,
           beforeRead ? beforePixel : 0u, afterRead ? afterPixel : 0u,
           beforeRead && afterRead && beforePixel != afterPixel ? 1u : 0u,
           beforeComplexRead ? beforeComplexPixel : 0u,
           afterComplexRead ? afterComplexPixel : 0u,
           beforeComplexRead && afterComplexRead && beforeComplexPixel != afterComplexPixel
               ? 1u
               : 0u);
    fflush(stdout);
}

static void EmuCapturePresentMirror();
static void EmuBlitPresentMirror();

static HRESULT EmuPresentHostDevice(const RECT* sourceRect, const RECT* destinationRect,
                                    HWND destinationWindow, const RGNDATA* dirtyRegion,
                                    bool runControlProbe)
{
    HRESULT endSceneResult = D3D_OK;
    HRESULT presentResult = D3D_OK;
    HRESULT beginSceneResult = D3D_OK;
    for(const cxbx::d3d::PresentSceneStep step : cxbx::d3d::PresentSceneSteps())
    {
        switch(step)
        {
            case cxbx::d3d::PresentSceneStep::EndScene:
                endSceneResult = EmuHostEndScene();
                if(SUCCEEDED(endSceneResult) && runControlProbe)
                {
                    EmuRunHostControlProbe();
                }
                break;
            case cxbx::d3d::PresentSceneStep::CaptureMirror:
                if(SUCCEEDED(endSceneResult))
                {
                    EmuCapturePresentMirror();
                }
                break;
            case cxbx::d3d::PresentSceneStep::Present:
                presentResult = EmuHostPresent(sourceRect, destinationRect, destinationWindow,
                                               dirtyRegion);
                break;
            case cxbx::d3d::PresentSceneStep::BlitMirror:
                EmuBlitPresentMirror();
                break;
            case cxbx::d3d::PresentSceneStep::BeginScene:
                beginSceneResult = EmuHostBeginScene();
                break;
        }
    }

    if(FAILED(endSceneResult))
    {
        static LONG warningCount = 0;
        if(InterlockedIncrement(&warningCount) <= 4)
        {
            EmuWarning("D3D8 host EndScene failed before Present (hr=0x%.08lX)",
                       endSceneResult);
        }
    }
    if(FAILED(presentResult))
    {
        return presentResult;
    }
    return beginSceneResult;
}

// XDK 5849 D3DPERF_APICounters values used by the HLE debug bridge.
enum EmuD3DPerfApiCounter
{
    EMU_API_D3DDEVICE_BLOCKUNTILIDLE = 18,
    EMU_API_D3DDEVICE_CLEAR = 21,
    EMU_D3DAPI_MAX = 255
};

enum EmuD3DPerfLayout
{
    EMU_D3DPERF_WAIT_COUNTER_OFFSET = 0x33A50,
    EMU_D3DPERF_WAIT_COUNTER_DWORDS = 120,
    EMU_D3DPERF_API_COUNTER_OFFSET = 0x33C30,
    EMU_D3DPERF_RENDER_COUNTER_OFFSET = 0x3402C,
    EMU_D3DPERF_RENDER_COUNTER_DWORDS = 166,
    EMU_D3DPERF_TEXTURE_COUNTER_OFFSET = 0x342C4,
    EMU_D3DPERF_TEXTURE_COUNTER_DWORDS = 32,
    EMU_D3DPERF_REQUIRED_SIZE = 0x34344
};

static DWORD *EmuD3DPerfApiCounters()
{
    return g_pD3DPerfStatistics == NULL ? NULL :
        (DWORD*)(g_pD3DPerfStatistics + EMU_D3DPERF_API_COUNTER_OFFSET);
}

// Locate the two debug-runtime globals without depending on title link
// addresses. D3DPERF_GetStatistics is a stable aligned accessor
// (mov eax, <static D3DPERF>; ret), while the single-step reference is
// identified by the surrounding pusher validation sequence. Relocated
// operands are deliberately decoded rather than embedded in OOVPAs.
static void EmuLocateD3DDebugGlobals()
{
    if(g_bD3DDebugGlobalsScanned || g_XbeHeader == NULL)
        return;

    g_bD3DDebugGlobalsScanned = true;

    const uint32 ImageBase = g_XbeHeader->dwBaseAddr;
    const uint32 ImageEnd = ImageBase + g_XbeHeader->dwSizeofImage;
    const uint32 SectionOffset = g_XbeHeader->dwSectionHeadersAddr - ImageBase;
    if(ImageEnd < ImageBase || SectionOffset >= g_XbeHeaderSize ||
       g_XbeHeader->dwSections >
           (g_XbeHeaderSize - SectionOffset) / sizeof(Xbe::SectionHeader))
        return;

    Xbe::SectionHeader *pSections =
        (Xbe::SectionHeader*)((uint08*)g_XbeHeader + SectionOffset);

    for(uint32 SectionIndex = 0; SectionIndex < g_XbeHeader->dwSections; SectionIndex++)
    {
        Xbe::SectionHeader *pSection = &pSections[SectionIndex];
        if(!pSection->dwFlags.bExecutable || pSection->dwVirtualSize < 20 ||
           pSection->dwVirtualAddr < ImageBase ||
           pSection->dwVirtualAddr > ImageEnd ||
           pSection->dwVirtualSize > ImageEnd - pSection->dwVirtualAddr)
            continue;

        uint08 *pCode = (uint08*)pSection->dwVirtualAddr;
        const uint32 ScanSize = pSection->dwVirtualSize - 19;
        for(uint32 Offset = 0; Offset < ScanSize; Offset++)
        {
            uint08 *p = pCode + Offset;

            if(g_pD3DPerfStatistics == NULL &&
               p[0] == 0xB8 && p[5] == 0xC3 &&
               p[6] == 0xCC && p[7] == 0xCC && p[8] == 0xCC &&
               p[9] == 0xCC && p[10] == 0xCC && p[11] == 0xCC &&
               p[12] == 0xCC && p[13] == 0xCC && p[14] == 0xCC &&
               p[15] == 0xCC && p[16] == 0x55 && p[17] == 0x8B && p[18] == 0xEC)
            {
                const uint32 Statistics = *(uint32*)(p + 1);
                if(Statistics >= ImageBase &&
                   Statistics <= ImageEnd &&
                   EMU_D3DPERF_REQUIRED_SIZE <= ImageEnd - Statistics)
                    g_pD3DPerfStatistics = (uint08*)Statistics;
            }

            if(g_pD3DSingleStepPusher == NULL &&
               p[0] == 0xF6 && p[1] == 0x05 && p[6] == 0x01 && p[7] == 0x74 &&
               p[9] == 0xF6 && p[10] == 0x86 && p[11] == 0xD8 &&
               p[12] == 0x08 && p[13] == 0x00 && p[14] == 0x00 && p[15] == 0xC0)
            {
                const uint32 SingleStep = *(uint32*)(p + 2);
                if(SingleStep >= ImageBase && SingleStep + sizeof(DWORD) <= ImageEnd)
                    g_pD3DSingleStepPusher = (volatile DWORD*)SingleStep;
            }

            if(g_pD3DPerfStatistics != NULL && g_pD3DSingleStepPusher != NULL)
                return;
        }
    }
}

#define EMU_D3DLOCK_TILED    0x40
#define EMU_D3DLOCK_READONLY 0x80

struct EmuTiledSurfaceLock
{
    XTL::X_D3DResource      *pResource;
    XTL::IDirect3DSurface8  *pSurface8;
};

static EmuTiledSurfaceLock g_TiledSurfaceLocks[8] = {};

#define EMU_YUY2_TEXTURE_SLOTS 16

struct EmuYuy2TextureInfo
{
    XTL::X_D3DResource *pHandle;
    uint08             *pPixels;
    DWORD               Width;
    DWORD               Height;
    DWORD               Pitch;
    ULONG               RefCount;
    DWORD               DataSize;   // Pitch * Height (start of the canary tail)
    DWORD               ContentHash;
    bool                HashValid;
};

// The video decoder writes frames into pPixels through LockRect; a decoder
// that assumes Xbox pitch/size padding writes PAST Pitch*Height and, on a
// plain new[] block, corrupts the host heap (detected only much later by an
// unrelated free). Give every buffer a pattern-filled slop tail: moderate
// overwrites land harmlessly, and the Release path measures and reports how
// far the title actually wrote.
#define EMU_YUY2_TAIL_SLOP  0x4000
#define EMU_YUY2_TAIL_FILL  0xC5

static EmuYuy2TextureInfo g_Yuy2Textures[EMU_YUY2_TEXTURE_SLOTS] = {};

static EmuYuy2TextureInfo *EmuFindYuy2Texture(const XTL::X_D3DResource *pHandle)
{
    for(int i = 0; i < EMU_YUY2_TEXTURE_SLOTS; i++)
    {
        if(g_Yuy2Textures[i].pHandle == pHandle)
            return &g_Yuy2Textures[i];
    }

    return NULL;
}

static XTL::X_D3DTexture *EmuCreateYuy2Texture(DWORD Width, DWORD Height)
{
    for(int i = 0; i < EMU_YUY2_TEXTURE_SLOTS; i++)
    {
        EmuYuy2TextureInfo *pInfo = &g_Yuy2Textures[i];
        if(pInfo->pHandle != NULL)
            continue;

        DWORD Pitch = Width * 2;
        DWORD DataSize = (DWORD)((size_t)Pitch * Height);
        uint08 *pPixels = new uint08[(size_t)DataSize + EMU_YUY2_TAIL_SLOP];
        memset(pPixels, 0, DataSize);
        memset(pPixels + DataSize, EMU_YUY2_TAIL_FILL, EMU_YUY2_TAIL_SLOP);

        pInfo->pHandle = (XTL::X_D3DResource*)((uint32)pPixels | 0x80000000);
        pInfo->pPixels = pPixels;
        pInfo->Width = Width;
        pInfo->Height = Height;
        pInfo->Pitch = Pitch;
        pInfo->RefCount = 1;
        pInfo->DataSize = DataSize;
        pInfo->ContentHash = 0;
        pInfo->HashValid = false;

        return (XTL::X_D3DTexture*)pInfo->pHandle;
    }

    return NULL;
}

static HRESULT EmuLockYuy2Texture(EmuYuy2TextureInfo *pInfo,
                                  XTL::D3DLOCKED_RECT *pLockedRect,
                                  CONST RECT *pRect)
{
    if(pInfo == NULL || pLockedRect == NULL)
        return D3DERR_INVALIDCALL;

    DWORD Left = 0;
    DWORD Top = 0;
    if(pRect != NULL)
    {
        if(pRect->left < 0 || pRect->top < 0 || pRect->right > (LONG)pInfo->Width ||
           pRect->bottom > (LONG)pInfo->Height || pRect->left >= pRect->right ||
           pRect->top >= pRect->bottom)
            return D3DERR_INVALIDCALL;

        Left = (DWORD)pRect->left;
        Top = (DWORD)pRect->top;

    }

    pLockedRect->Pitch = pInfo->Pitch;
    pLockedRect->pBits = pInfo->pPixels + (size_t)Top * pInfo->Pitch + (size_t)Left * 2;

    static int s_YuvLockTraceEnabled = -1;
    static DWORD s_YuvLockSequence = 0;
    if(s_YuvLockTraceEnabled < 0)
    {
        s_YuvLockTraceEnabled = getenv("CXBX_YUV_TRACE") != NULL ? 1 : 0;
    }
    if(s_YuvLockTraceEnabled == 1)
    {
        const DWORD sequence = ++s_YuvLockSequence;
        if(sequence <= 8 || sequence % 60 == 0)
        {
            printf("YUV| lock seq=%lu handle=0x%.08lX bits=0x%.08lX pitch=%lu\n",
                   static_cast<unsigned long>(sequence),
                   reinterpret_cast<unsigned long>(pInfo->pHandle),
                   reinterpret_cast<unsigned long>(pLockedRect->pBits),
                   static_cast<unsigned long>(pLockedRect->Pitch));
        }
    }
    return D3D_OK;
}

// ******************************************************************
// * Cached Direct3D State Variable(s)
// ******************************************************************
static XTL::X_D3DSurface      *g_pCachedRenderTarget = NULL;
static XTL::X_D3DSurface      *g_pCachedZStencilSurface = NULL;
static XTL::X_D3DPushBuffer   *g_pRecordingPushBuffer = NULL;
static DWORD                   g_dwVertexShaderUsage = 0;

struct EmuRecordedPushBufferDraw
{
    XTL::D3DPRIMITIVETYPE primitiveType = XTL::D3DPT_POINTLIST;
    UINT primitiveCount = 0;
    UINT stride = 0;
    DWORD stateBlock = 0;
    std::vector<BYTE> vertices;
};

struct EmuRecordedPushBuffer
{
    XTL::X_D3DPushBuffer* pushBuffer = NULL;
    const DWORD* registeredData = nullptr;
    DWORD registeredBytes = 0;
    std::vector<EmuRecordedPushBufferDraw> draws;
    std::size_t byteCount = 0;
};

struct EmuRegisteredVertexBuffer
{
    XTL::X_D3DVertexBuffer* resource = nullptr;
    const BYTE* guestData = nullptr;
    DWORD byteSize = 0;
};

static std::vector<EmuRecordedPushBuffer> g_EmuRecordedPushBuffers;
static std::vector<EmuRegisteredVertexBuffer> g_EmuRegisteredVertexBuffers;
constexpr std::size_t EMU_RECORDED_DRAW_LIMIT = 256;
constexpr std::size_t EMU_RECORDED_BYTE_LIMIT = 16 * 1024 * 1024;

static DWORD EmuCaptureD3DStateBlock()
{
    DWORD stateBlock = 0;
    __try
    {
        g_pD3DDevice8->CreateStateBlock(XTL::D3DSBT_ALL, &stateBlock);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        stateBlock = 0;
    }
    return stateBlock;
}

static void EmuDeleteD3DStateBlock(DWORD stateBlock)
{
    if(stateBlock == 0 || g_pD3DDevice8 == NULL)
    {
        return;
    }
    __try
    {
        g_pD3DDevice8->DeleteStateBlock(stateBlock);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static EmuRecordedPushBuffer* EmuFindRecordedPushBuffer(XTL::X_D3DPushBuffer* pushBuffer)
{
    for(EmuRecordedPushBuffer& recording : g_EmuRecordedPushBuffers)
    {
        if(recording.pushBuffer == pushBuffer)
        {
            return &recording;
        }
    }
    return NULL;
}

static EmuRegisteredVertexBuffer* EmuFindRegisteredVertexBuffer(
    XTL::X_D3DVertexBuffer* vertexBuffer)
{
    for(EmuRegisteredVertexBuffer& registration : g_EmuRegisteredVertexBuffers)
    {
        if(registration.resource == vertexBuffer)
        {
            return &registration;
        }
    }
    return nullptr;
}

static void EmuDiscardRegisteredVertexBuffer(XTL::X_D3DVertexBuffer* vertexBuffer)
{
    for(std::vector<EmuRegisteredVertexBuffer>::iterator registration =
            g_EmuRegisteredVertexBuffers.begin();
        registration != g_EmuRegisteredVertexBuffers.end(); ++registration)
    {
        if(registration->resource == vertexBuffer)
        {
            g_EmuRegisteredVertexBuffers.erase(registration);
            return;
        }
    }
}

static void EmuResetRegisteredVertexBuffer(XTL::X_D3DVertexBuffer* vertexBuffer,
                                           const void* guestData, DWORD byteSize)
{
    EmuRegisteredVertexBuffer* registration = EmuFindRegisteredVertexBuffer(vertexBuffer);
    if(registration != nullptr)
    {
        registration->guestData = static_cast<const BYTE*>(guestData);
        registration->byteSize = byteSize;
        return;
    }

    try
    {
        g_EmuRegisteredVertexBuffers.push_back(
            { vertexBuffer, static_cast<const BYTE*>(guestData), byteSize });
    }
    catch(...)
    {
    }
}

static void EmuReleaseRecordedPushBufferState(EmuRecordedPushBuffer& recording)
{
    if(g_pD3DDevice8 != NULL)
    {
        for(EmuRecordedPushBufferDraw& draw : recording.draws)
        {
            if(draw.stateBlock != 0)
            {
                EmuDeleteD3DStateBlock(draw.stateBlock);
                draw.stateBlock = 0;
            }
        }
    }
}

static void EmuDiscardRecordedPushBuffer(XTL::X_D3DPushBuffer* pushBuffer)
{
    for(std::vector<EmuRecordedPushBuffer>::iterator recording = g_EmuRecordedPushBuffers.begin();
        recording != g_EmuRecordedPushBuffers.end(); ++recording)
    {
        if(recording->pushBuffer == pushBuffer)
        {
            EmuReleaseRecordedPushBufferState(*recording);
            g_EmuRecordedPushBuffers.erase(recording);
            return;
        }
    }
}

static EmuRecordedPushBuffer* EmuResetRecordedPushBuffer(XTL::X_D3DPushBuffer* pushBuffer)
{
    EmuRecordedPushBuffer* recording = EmuFindRecordedPushBuffer(pushBuffer);
    if(recording != NULL)
    {
        EmuReleaseRecordedPushBufferState(*recording);
        recording->draws.clear();
        recording->byteCount = 0;
        return recording;
    }

    try
    {
        g_EmuRecordedPushBuffers.push_back({ pushBuffer });
        return &g_EmuRecordedPushBuffers.back();
    }
    catch(...)
    {
        return NULL;
    }
}

static UINT EmuRecordedDrawVertexCount(XTL::D3DPRIMITIVETYPE primitiveType, UINT primitiveCount)
{
    switch(primitiveType)
    {
        case XTL::D3DPT_POINTLIST: return primitiveCount;
        case XTL::D3DPT_LINELIST: return primitiveCount * 2;
        case XTL::D3DPT_LINESTRIP: return primitiveCount + 1;
        case XTL::D3DPT_TRIANGLELIST: return primitiveCount * 3;
        case XTL::D3DPT_TRIANGLESTRIP:
        case XTL::D3DPT_TRIANGLEFAN: return primitiveCount + 2;
        default: return 0;
    }
}

static void EmuAppendRecordedDraw(EmuRecordedPushBuffer& recording,
                                  XTL::D3DPRIMITIVETYPE primitiveType, UINT primitiveCount,
                                  const void* vertices, UINT stride)
{
    if(vertices == NULL || stride == 0 || recording.draws.size() >= EMU_RECORDED_DRAW_LIMIT)
    {
        return;
    }

    const UINT vertexCount = EmuRecordedDrawVertexCount(primitiveType, primitiveCount);
    if(vertexCount == 0 ||
       vertexCount > (std::numeric_limits<std::size_t>::max)() / stride)
    {
        return;
    }
    const std::size_t byteCount = static_cast<std::size_t>(vertexCount) * stride;
    if(byteCount > EMU_RECORDED_BYTE_LIMIT - recording.byteCount)
    {
        return;
    }

    EmuRecordedPushBufferDraw draw;
    draw.primitiveType = primitiveType;
    draw.primitiveCount = primitiveCount;
    draw.stride = stride;
    draw.stateBlock = EmuCaptureD3DStateBlock();

    try
    {
        const BYTE* vertexBytes = static_cast<const BYTE*>(vertices);
        draw.vertices.assign(vertexBytes, vertexBytes + byteCount);
        recording.draws.push_back(std::move(draw));
        recording.byteCount += byteCount;
    }
    catch(...)
    {
        if(draw.stateBlock != 0)
        {
            EmuDeleteD3DStateBlock(draw.stateBlock);
        }
    }
}

static void EmuRecordPushBufferDraw(XTL::D3DPRIMITIVETYPE primitiveType, UINT primitiveCount,
                                    const void* vertices, UINT stride)
{
    if(g_pRecordingPushBuffer == NULL)
    {
        return;
    }
    EmuRecordedPushBuffer* recording = EmuFindRecordedPushBuffer(g_pRecordingPushBuffer);
    if(recording != NULL)
    {
        EmuAppendRecordedDraw(*recording, primitiveType, primitiveCount, vertices, stride);
    }
}

static bool EmuReplayRecordedDraws(const EmuRecordedPushBuffer& recording)
{
    if(recording.draws.empty())
    {
        return true;
    }

    HRESULT result = D3D_OK;
    __try
    {
        for(const EmuRecordedPushBufferDraw& draw : recording.draws)
        {
            if(FAILED(result))
            {
                break;
            }
            if(draw.stateBlock != 0)
            {
                result = g_pD3DDevice8->ApplyStateBlock(draw.stateBlock);
            }
            if(SUCCEEDED(result))
            {
                result = g_pD3DDevice8->DrawPrimitiveUP(
                    draw.primitiveType, draw.primitiveCount, draw.vertices.data(), draw.stride);
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        result = D3DERR_INVALIDCALL;
    }
    return SUCCEEDED(result);
}

static bool EmuReplayRecordedPushBuffer(XTL::X_D3DPushBuffer* pushBuffer)
{
    EmuRecordedPushBuffer* recording = EmuFindRecordedPushBuffer(pushBuffer);
    return recording == NULL || EmuReplayRecordedDraws(*recording);
}

// ******************************************************************
// * EmuD3DTiles (8 Tiles Max)
// ******************************************************************
XTL::X_D3DTILE XTL::EmuD3DTileCache[0x08] = {0};

// ******************************************************************
// * EmuD3DDeferredRenderState
// ******************************************************************
DWORD *XTL::EmuD3DDeferredRenderState;

// ******************************************************************
// * EmuD3DDeferredTextureState
// ******************************************************************
DWORD *XTL::EmuD3DDeferredTextureState;

// ******************************************************************
// * EmuD3D8CreateDeviceProxyData
// ******************************************************************
struct EmuD3D8CreateDeviceProxyData
{
    XTL::UINT                        Adapter;
    XTL::D3DDEVTYPE                  DeviceType;
    HWND                             hFocusWindow;
    XTL::DWORD                       BehaviorFlags;
    XTL::X_D3DPRESENT_PARAMETERS    *pPresentationParameters;
    XTL::IDirect3DDevice8          **ppReturnedDeviceInterface;
    volatile bool                    bReady;
    volatile HRESULT                 hRet;
}
g_EmuD3D8CreateDeviceProxyData = {0};

// ******************************************************************
// * func: XTL::EmuD3DInit
// ******************************************************************
VOID XTL::EmuD3DInit(Xbe::Header *XbeHeader, uint32 XbeHeaderSize)
{
    g_EmuShared->GetXBVideo(&g_XBVideo);

    // ******************************************************************
    // * store XbeHeader and XbeHeaderSize for further use
    // ******************************************************************
    g_XbeHeader     = XbeHeader;
    g_XbeHeaderSize = XbeHeaderSize;

    // ******************************************************************
    // * create a thread dedicated to timing
    // ******************************************************************
    {
        DWORD dwThreadId;

        CreateThread(NULL, NULL, EmuUpdateTickCount, NULL, NULL, &dwThreadId);
    }

    // ******************************************************************
    // * create a thread dedicated to creating devices
    // ******************************************************************
    {
        DWORD dwThreadId;

        CreateThread(NULL, NULL, EmuCreateDeviceProxy, NULL, NULL, &dwThreadId);
    }

    // ******************************************************************
    // * spark up a new thread to handle window message processing
    // ******************************************************************
    {
        DWORD dwThreadId;

        g_bRenderWindowActive.store(false, std::memory_order_relaxed);

        CreateThread(NULL, NULL, EmuRenderWindow, NULL, NULL, &dwThreadId);

        while(!g_bRenderWindowActive.load(std::memory_order_acquire))
        {
            Sleep(10);
        }

        Sleep(50);
    }

    // ******************************************************************
    // * create Direct3D8 and retrieve caps
    // ******************************************************************
    {
        using namespace XTL;

        // xbox Direct3DCreate8 returns "1" always, so we need our own ptr
        g_pD3D8 = Direct3DCreate8(D3D_SDK_VERSION);

        if(g_pD3D8 == NULL)
            EmuCleanup("Could not initialize Direct3D8!");

        D3DDEVTYPE DevType = (g_XBVideo.GetDirect3DDevice() == 0) ? D3DDEVTYPE_HAL : D3DDEVTYPE_REF;

        g_pD3D8->GetDeviceCaps(g_XBVideo.GetDisplayAdapter(), DevType, &g_D3DCaps);
    }

    // ******************************************************************
    // * create DirectDraw7
    // ******************************************************************
    {
        using namespace XTL;

        HRESULT hRet = DirectDrawCreateEx(NULL, (void**)&g_pDD7, IID_IDirectDraw7, NULL);

        if(FAILED(hRet))
            EmuCleanup("Could not initialize DirectDraw7");

        hRet = g_pDD7->SetCooperativeLevel(0, DDSCL_NORMAL);

        if(FAILED(hRet))
            EmuCleanup("Could not set cooperative level");

    }

    // ******************************************************************
    // * create default device
    // ******************************************************************
    {
        XTL::X_D3DPRESENT_PARAMETERS PresParam;

        ZeroMemory(&PresParam, sizeof(PresParam));

        PresParam.BackBufferWidth  = 640;
        PresParam.BackBufferHeight = 480;
        PresParam.BackBufferFormat = 6; /* X_D3DFMT_A8R8G8B8 */
        PresParam.BackBufferCount  = 1;
        PresParam.EnableAutoDepthStencil = TRUE;
        PresParam.AutoDepthStencilFormat = 0x2A; /* X_D3DFMT_D24S8 */
        PresParam.SwapEffect = XTL::D3DSWAPEFFECT_DISCARD;

        EmuSwapFS();    // XBox FS
        XTL::EmuIDirect3D8_CreateDevice(0, XTL::D3DDEVTYPE_HAL, 0, 0x00000040, &PresParam, &g_pD3DDevice8);
        EmuSwapFS();    // Win2k/XP FS
    }
}

// ******************************************************************
// * func: XTL::EmuD3DCleanup
// ******************************************************************
VOID XTL::EmuD3DCleanup()
{
    XTL::EmuDInputCleanup();

    return;
}

// ******************************************************************
// * func: EmuRenderWindow
// ******************************************************************
static DWORD WINAPI EmuRenderWindow(LPVOID)
{
    // ******************************************************************
    // * register window class
    // ******************************************************************
    {
        #ifdef _DEBUG
        HMODULE hCxbxDll = GetModuleHandle("cxbxkrnl.dll");
        #else
        HMODULE hCxbxDll = GetModuleHandle("cxbx.dll");
        #endif

        LOGBRUSH logBrush = {BS_SOLID, RGB(0,0,0)};

        g_hBgBrush = CreateBrushIndirect(&logBrush);

        WNDCLASSEX wc =
        {
            sizeof(WNDCLASSEX),
            CS_CLASSDC,
            EmuMsgProc,
            0, 0, GetModuleHandle(NULL),
            LoadIcon(hCxbxDll, MAKEINTRESOURCE(IDI_CXBX)),
            LoadCursor(NULL, IDC_ARROW), 
            (HBRUSH)(g_hBgBrush), NULL,
            "CxbxRender",
            NULL
        };

        RegisterClassEx(&wc);
    }

    // ******************************************************************
    // * create the window
    // ******************************************************************
    {
        char AsciiTitle[50];

        // ******************************************************************
        // * retrieve xbe title (if possible)
        // ******************************************************************
        {
            char tAsciiTitle[40] = "Unknown";

            uint32 CertAddr = g_XbeHeader->dwCertificateAddr - g_XbeHeader->dwBaseAddr;

            if(CertAddr + 0x0C + 40 < g_XbeHeaderSize)
            {
                Xbe::Certificate *XbeCert = (Xbe::Certificate*)((uint32)g_XbeHeader + CertAddr);

                setlocale( LC_ALL, "English" );

                wcstombs(tAsciiTitle, XbeCert->wszTitleName, 40);
            }

            sprintf(AsciiTitle, "cxbx : Emulating %s", tAsciiTitle);
        }

        // ******************************************************************
        // * Create Window
        // ******************************************************************
        {
            DWORD dwStyle = WS_OVERLAPPEDWINDOW;

            int nTitleHeight  = GetSystemMetrics(SM_CYCAPTION);
            int nBorderWidth  = GetSystemMetrics(SM_CXSIZEFRAME);
            int nBorderHeight = GetSystemMetrics(SM_CYSIZEFRAME);

            int x = 100, y = 100, nWidth = 640, nHeight = 480;

            nWidth  += nBorderWidth*2;
            nHeight += nBorderHeight*2 + nTitleHeight;

            sscanf(g_XBVideo.GetVideoResolution(), "%d x %d", &nWidth, &nHeight);

            if(g_XBVideo.GetFullscreen())
            {
                x = y = nWidth = nHeight = 0;
                dwStyle = WS_POPUP;
            }

            XTL::g_hEmuWindow = CreateWindow
            (
                "CxbxRender", AsciiTitle,
                dwStyle, x, y, nWidth, nHeight,
                GetDesktopWindow(), NULL, GetModuleHandle(NULL), NULL
            );
        }
    }

    ShowWindow(XTL::g_hEmuWindow, SW_SHOWDEFAULT);
    UpdateWindow(XTL::g_hEmuWindow);

    // ******************************************************************
    // * initialize direct input
    // ******************************************************************
    if(!XTL::EmuDInputInit())
        EmuCleanup("Could not initialize DirectInput!");

    printf("EmuD3D8 (0x%X): Message-Pump thread is running.\n", GetCurrentThreadId());

    // ******************************************************************
    // * message processing loop
    // ******************************************************************
    {
        MSG msg;

        ZeroMemory(&msg, sizeof(msg));
        g_bRenderWindowActive.store(true, std::memory_order_release);

        while(msg.message != WM_QUIT)
        {
            if(PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else
            {
                Sleep(10);
            }
        }

        g_bRenderWindowActive.store(false, std::memory_order_release);

        EmuCleanup(NULL);
    }

    return 0;
}

// ******************************************************************
// * func: EmuMsgProc
// ******************************************************************
static LRESULT WINAPI EmuMsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static DWORD dwRestoreSleepRate = EmuAutoSleepRate;

    switch(msg)
    {
        case WM_DESTROY:
            DeleteObject(g_hBgBrush);
            PostQuitMessage(0);
            return 0;

        case WM_INPUT_DEVICE_CHANGE:
            XTL::EmuDInputNotifyDeviceChange();
            return 0;

        case WM_KEYDOWN:
            if(wParam == VK_ESCAPE)
                PostMessage(hWnd, WM_CLOSE, 0, 0);
            break;

        case WM_SETFOCUS:
            EmuAutoSleepRate = dwRestoreSleepRate;
            break;

        case WM_KILLFOCUS:
            dwRestoreSleepRate = EmuAutoSleepRate;
            EmuAutoSleepRate = 0;
            break;

        case WM_CLOSE:
            DestroyWindow(hWnd);
            break;

        case WM_SETCURSOR:
            if(g_XBVideo.GetFullscreen())
            {
                SetCursor(NULL);
                return 0;
            }
            return DefWindowProc(hWnd, msg, wParam, lParam);

        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    return 0;
}

// ******************************************************************
// * func: EmuUpdateTickCount
// ******************************************************************
static DWORD WINAPI EmuUpdateTickCount(LPVOID)
{
    printf("EmuD3D8 (0x%X): Timing thread is running.\n", GetCurrentThreadId());

    timeBeginPeriod(1);

    while(true)
    {
        xboxkrnl::KeTickCount = timeGetTime();
        Sleep(1);
    }

    timeEndPeriod(1);
}

// ******************************************************************
// * func: EmuCreateDeviceProxy
// ******************************************************************
static DWORD WINAPI EmuCreateDeviceProxy(LPVOID)
{
    printf("EmuD3D8 (0x%X): CreateDevice proxy thread is running.\n", GetCurrentThreadId());

    while(true)
    {
        // if we have been signalled, create the device with cached parameters
        if(g_EmuD3D8CreateDeviceProxyData.bReady)
        {
            printf("EmuD3D8 (0x%X): CreateDevice proxy thread recieved request.\n", GetCurrentThreadId());

            // only one device should be created at once
            // TODO: ensure all surfaces are somehow cleaned up?
            if(g_pD3DDevice8 != 0)
            {
                printf("EmuD3D8 (0x%X): CreateDevice proxy thread releasing old Device.\n", GetCurrentThreadId());

                g_pD3DDevice8->EndScene();

                while(g_pD3DDevice8->Release() != 0);

                g_pD3DDevice8 = 0;
            }

            // ******************************************************************
            // * verify no ugly circumstances
            // ******************************************************************
            if(g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->BufferSurfaces[0] != NULL || g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->DepthStencilSurface != NULL)
                EmuWarning("DepthStencilSurface != NULL and/or BufferSurfaces[0] != NULL");

            // ******************************************************************
            // * make adjustments to parameters to make sense with windows d3d
            // ******************************************************************
            {
                g_EmuD3D8CreateDeviceProxyData.DeviceType =(g_XBVideo.GetDirect3DDevice() == 0) ? XTL::D3DDEVTYPE_HAL : XTL::D3DDEVTYPE_REF;
                g_EmuD3D8CreateDeviceProxyData.Adapter    = g_XBVideo.GetDisplayAdapter();

                g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->Windowed = !g_XBVideo.GetFullscreen();

                if(g_XBVideo.GetVSync())
                    g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->SwapEffect = XTL::D3DSWAPEFFECT_COPY_VSYNC;

                g_EmuD3D8CreateDeviceProxyData.hFocusWindow = XTL::g_hEmuWindow;

                g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->BackBufferFormat       = XTL::EmuXB2PC_D3DFormat(g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->BackBufferFormat);
                g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->AutoDepthStencilFormat = XTL::EmuXB2PC_D3DFormat(g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->AutoDepthStencilFormat);

                if(!g_XBVideo.GetVSync() && (g_D3DCaps.PresentationIntervals & D3DPRESENT_INTERVAL_IMMEDIATE) && g_XBVideo.GetFullscreen())
                    g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
                else
                {
                    if(g_D3DCaps.PresentationIntervals & D3DPRESENT_INTERVAL_ONE && g_XBVideo.GetFullscreen())
                        g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE;
                    else
                        g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
                }

                // TODO: Support Xbox extensions if possible
                if(g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->MultiSampleType != 0)
                {
                    EmuWarning("MultiSampleType 0x%.08X is not supported!", g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->MultiSampleType);

                    g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->MultiSampleType = XTL::D3DMULTISAMPLE_NONE;

                    // TODO: Check card for multisampling abilities
        //            if(pPresentationParameters->MultiSampleType == 0x00001121)
        //                pPresentationParameters->MultiSampleType = D3DMULTISAMPLE_2_SAMPLES;
        //            else
        //                EmuCleanup("Unknown MultiSampleType (0x%.08X)", pPresentationParameters->MultiSampleType);
                }

                // The Xbox Flags field carries Xbox-only bits (field/interlace
                // scanout hints, e.g. 0x20 from z26x) that the PC runtime
                // rejects as an invalid call; LOCKABLE_BACKBUFFER (0x1) is the
                // only flag DX8 defines, so replace rather than OR.
                g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
        
                // ******************************************************************
                // * Retrieve Resolution from Configuration
                // ******************************************************************
                if(g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->Windowed)
                {
                    sscanf(g_XBVideo.GetVideoResolution(), "%d x %d", &g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->BackBufferWidth, &g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->BackBufferHeight);

                    XTL::D3DDISPLAYMODE D3DDisplayMode;

                    g_pD3D8->GetAdapterDisplayMode(g_XBVideo.GetDisplayAdapter(), &D3DDisplayMode);

                    g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->BackBufferFormat = D3DDisplayMode.Format;
                    g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->FullScreen_RefreshRateInHz = 0;
                }
                else
                {
                    char szBackBufferFormat[16];

                    sscanf(g_XBVideo.GetVideoResolution(), "%d x %d %*dbit %s (%d hz)", 
                        &g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->BackBufferWidth, 
                        &g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->BackBufferHeight,
                        szBackBufferFormat,
                        &g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->FullScreen_RefreshRateInHz);

                    if(strcmp(szBackBufferFormat, "x1r5g5b5") == 0)
                        g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->BackBufferFormat = XTL::D3DFMT_X1R5G5B5;
                    else if(strcmp(szBackBufferFormat, "r5g6r5") == 0)
                        g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->BackBufferFormat = XTL::D3DFMT_R5G6B5;
                    else if(strcmp(szBackBufferFormat, "x8r8g8b8") == 0)
                        g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->BackBufferFormat = XTL::D3DFMT_X8R8G8B8;
                    else if(strcmp(szBackBufferFormat, "a8r8g8b8") == 0)
                        g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->BackBufferFormat = XTL::D3DFMT_A8R8G8B8;
                }
            }

            // ******************************************************************
            // * Detect vertex processing capabilities
            // ******************************************************************
            if((g_D3DCaps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) && g_EmuD3D8CreateDeviceProxyData.DeviceType == XTL::D3DDEVTYPE_HAL)
            {
                #ifdef _DEBUG_TRACE
                printf("EmuD3D8 (0x%X): Using hardware vertex processing\n", GetCurrentThreadId());
                #endif
                g_EmuD3D8CreateDeviceProxyData.BehaviorFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING;
                g_dwVertexShaderUsage = 0;
            }
            else
            {
                #ifdef _DEBUG_TRACE
                printf("EmuD3D8 (0x%X): Using software vertex processing\n", GetCurrentThreadId());
                #endif
                g_EmuD3D8CreateDeviceProxyData.BehaviorFlags = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
                g_dwVertexShaderUsage = D3DUSAGE_SOFTWAREPROCESSING;
            }

            // ******************************************************************
            // * redirect to windows d3d
            // ******************************************************************
            g_EmuD3D8CreateDeviceProxyData.hRet = g_pD3D8->CreateDevice
            (
                g_EmuD3D8CreateDeviceProxyData.Adapter,
                g_EmuD3D8CreateDeviceProxyData.DeviceType,
                g_EmuD3D8CreateDeviceProxyData.hFocusWindow,
                g_EmuD3D8CreateDeviceProxyData.BehaviorFlags,
                (XTL::D3DPRESENT_PARAMETERS*)g_EmuD3D8CreateDeviceProxyData.pPresentationParameters,
                g_EmuD3D8CreateDeviceProxyData.ppReturnedDeviceInterface
            );

            // ******************************************************************
            // * report error
            // ******************************************************************
            if(FAILED(g_EmuD3D8CreateDeviceProxyData.hRet))
            {
                XTL::X_D3DPRESENT_PARAMETERS *pp = g_EmuD3D8CreateDeviceProxyData.pPresentationParameters;
                printf("EmuD3D8 (0x%X): CreateDevice params: %ux%u fmt=%u count=%u swap=%u windowed=%u "
                       "zenable=%u zfmt=%u flags=0x%X msaa=0x%X hz=%u interval=0x%X\n",
                       GetCurrentThreadId(), pp->BackBufferWidth, pp->BackBufferHeight,
                       (UINT)pp->BackBufferFormat, pp->BackBufferCount, (UINT)pp->SwapEffect,
                       pp->Windowed, pp->EnableAutoDepthStencil, (UINT)pp->AutoDepthStencilFormat,
                       pp->Flags, (UINT)pp->MultiSampleType, pp->FullScreen_RefreshRateInHz,
                       pp->FullScreen_PresentationInterval);
                fflush(stdout);

                if(g_EmuD3D8CreateDeviceProxyData.hRet == D3DERR_INVALIDCALL)
                    EmuCleanup("IDirect3D8::CreateDevice failed (Invalid Call)");
                else if(g_EmuD3D8CreateDeviceProxyData.hRet == D3DERR_NOTAVAILABLE)
                    EmuCleanup("IDirect3D8::CreateDevice failed (Not Available)");
                else if(g_EmuD3D8CreateDeviceProxyData.hRet == D3DERR_OUTOFVIDEOMEMORY)
                    EmuCleanup("IDirect3D8::CreateDevice failed (Out of Video Memory)");

                EmuCleanup("IDirect3D8::CreateDevice failed (Unknown)");
            }

            // ******************************************************************
            // * it is necessary to store this pointer globally for emulation
            // ******************************************************************
            g_pD3DDevice8 = *g_EmuD3D8CreateDeviceProxyData.ppReturnedDeviceInterface;

            // D3D8 YUY2 texture sampling support does not imply that the
            // separate DirectDraw hardware-overlay path is available or can
            // target this window. Modern drivers can report the texture format
            // while presenting the DirectDraw overlay as a black surface. Keep
            // XMV presentation on the deterministic software conversion path.
            g_bSupportsYUY2 = FALSE;

            // ******************************************************************
            // * Update Caches
            // ******************************************************************
            {
                g_pCachedRenderTarget = new XTL::X_D3DSurface();
                g_pD3DDevice8->GetRenderTarget(&g_pCachedRenderTarget->EmuSurface8);

                g_pCachedZStencilSurface = new XTL::X_D3DSurface();
                g_pD3DDevice8->GetDepthStencilSurface(&g_pCachedZStencilSurface->EmuSurface8);
            }

            // ******************************************************************
            // * Begin Scene
            // ******************************************************************
            g_pD3DDevice8->BeginScene();

            // ******************************************************************
            // * Initially, show a black screen
            // ******************************************************************
            g_pD3DDevice8->Clear(0, 0, D3DCLEAR_TARGET, 0, 0, 0);
            EmuPresentHostDevice(nullptr, nullptr, nullptr, nullptr, false);

            // signal completion
            g_EmuD3D8CreateDeviceProxyData.bReady = false;
        }

        Sleep(1);
    }
}

// ******************************************************************
// * func: EmuCheckAllocationSize
// ******************************************************************
static DWORD EmuCheckAllocationSize(PVOID pBase)
{
    MEMORY_BASIC_INFORMATION MemoryBasicInfo;

    DWORD dwRet = VirtualQuery(pBase, &MemoryBasicInfo, sizeof(MemoryBasicInfo));

    if(dwRet == 0)
        return 0;

    if(MemoryBasicInfo.State != MEM_COMMIT)
        return 0;

    return MemoryBasicInfo.RegionSize - ((DWORD)pBase - (DWORD)MemoryBasicInfo.BaseAddress);
}

static bool EmuD3DIsReadableRange(const void* base, DWORD bytes)
{
    if(base == NULL || bytes == 0)
    {
        return false;
    }

    const uintptr_t begin = reinterpret_cast<uintptr_t>(base);
    const uintptr_t end = begin + bytes;
    if(end < begin)
    {
        return false;
    }

    uintptr_t current = begin;
    while(current < end)
    {
        MEMORY_BASIC_INFORMATION memory = {};
        if(VirtualQuery(reinterpret_cast<const void*>(current),
                        &memory,
                        sizeof(memory)) != sizeof(memory) ||
           memory.State != MEM_COMMIT ||
           (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        {
            return false;
        }

        const uintptr_t regionEnd =
            reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize;
        if(regionEnd <= current)
        {
            return false;
        }
        current = regionEnd;
    }

    return true;
}

extern "C" ULONG EmuContiguousBlockBase(ULONG HostAddress, ULONG* BlockSize);
extern "C" ULONG EmuContiguousHostFromPhysical(ULONG PhysicalAddress);
extern "C" ULONG EmuSystemHostFromPhysical(ULONG PhysicalAddress, ULONG Size);

static bool EmuD3DCopyReadableRange(const void* source, DWORD bytes,
                                    std::vector<BYTE>& copy)
{
    if(source == nullptr || bytes == 0)
    {
        return false;
    }

    try
    {
        copy.resize(bytes);
    }
    catch(...)
    {
        return false;
    }

    SIZE_T bytesRead = 0;
    if(ReadProcessMemory(GetCurrentProcess(), source, copy.data(), bytes, &bytesRead) &&
       bytesRead == bytes)
    {
        return true;
    }

    // Xbox resources may carry an identity-map alias or another pointer-shaped
    // value whose low 26 bits are the physical RAM address.  Reverse-map that
    // address through the contiguous-allocation tracker before treating a texture
    // as missing.  The tracker returns zero unless the complete address belongs
    // to a live guest allocation, so arbitrary unreadable host pointers fail closed.
    const ULONG physicalAddress = cxbx::d3d::XboxPhysicalAddress(
        reinterpret_cast<std::uintptr_t>(source));
    ULONG hostAddress = EmuContiguousHostFromPhysical(physicalAddress);
    if(hostAddress == 0)
    {
        hostAddress = EmuSystemHostFromPhysical(physicalAddress, bytes);
    }
    bytesRead = 0;
    if(hostAddress == 0 ||
       !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(hostAddress),
                          copy.data(), bytes, &bytesRead) ||
       bytesRead != bytes)
    {
        copy.clear();
        return false;
    }
    return true;
}

// ******************************************************************
// * func: EmuVerifyResourceIsRegistered
// ******************************************************************
static inline void EmuVerifyResourceIsRegistered(XTL::X_D3DResource *pResource)
{
    if(pResource->Lock == 0)
    {
        EmuSwapFS();    // XBox FS;
        XTL::EmuIDirect3DResource8_Register(pResource, 0/*(PVOID)pResource->Data*/);
        EmuSwapFS();    // Win2k/XP FS
    }
}

// ******************************************************************
// * func: EmuAdjustPower2
// ******************************************************************
static void EmuAdjustPower2(UINT *dwWidth, UINT *dwHeight)
{
    UINT NewWidth=0, NewHeight=0;

    int v;

    for(v=0;v<32;v++)
    {
        int mask = 1 << v;

        if(*dwWidth & mask)
            NewWidth = mask;

        if(*dwHeight & mask)
            NewHeight = mask;
    }

    if(*dwWidth != NewWidth)
    {
        NewWidth <<= 1;
        printf("*Warning* needed to resize width (%d->%d)\n", *dwWidth, NewWidth);
    }

    if(*dwHeight != NewHeight)
    {
        NewHeight <<= 1;
        printf("*Warning* needed to resize height (%d->%d)\n", *dwHeight, NewHeight);
    }

    *dwWidth = NewWidth;
    *dwHeight = NewHeight;
}

// ******************************************************************
// * func: EmuFindTiledSurfaceLock
// ******************************************************************
static EmuTiledSurfaceLock *EmuFindTiledSurfaceLock(XTL::X_D3DResource *pResource)
{
    for(unsigned v = 0; v < sizeof(g_TiledSurfaceLocks) / sizeof(g_TiledSurfaceLocks[0]); v++)
    {
        if(g_TiledSurfaceLocks[v].pResource == pResource)
            return &g_TiledSurfaceLocks[v];
    }

    return NULL;
}

// ******************************************************************
// * func: EmuFindFreeTiledSurfaceLock
// ******************************************************************
static EmuTiledSurfaceLock *EmuFindFreeTiledSurfaceLock()
{
    for(unsigned v = 0; v < sizeof(g_TiledSurfaceLocks) / sizeof(g_TiledSurfaceLocks[0]); v++)
    {
        if(g_TiledSurfaceLocks[v].pResource == NULL)
            return &g_TiledSurfaceLocks[v];
    }

    return &g_TiledSurfaceLocks[0];
}

// ******************************************************************
// * func: EmuCommitTiledSurfaceLock
// ******************************************************************
static void EmuCommitTiledSurfaceLock(EmuTiledSurfaceLock *pLock)
{
    // The guest UnlockRect is not HLE-patched in this tree. Commit the host
    // surface before presenting; otherwise D3D8 can display stale/partial data.
    pLock->pSurface8->UnlockRect();
}

// ******************************************************************
// * func: EmuFlushTiledSurfaceLock
// ******************************************************************
static void EmuFlushTiledSurfaceLock(XTL::X_D3DResource *pResource)
{
    EmuTiledSurfaceLock *pLock = EmuFindTiledSurfaceLock(pResource);
    if(pLock == NULL)
        return;

    EmuCommitTiledSurfaceLock(pLock);

    if(pLock->pSurface8 != NULL)
        pLock->pSurface8->Release();

    pLock->pResource = NULL;
    pLock->pSurface8 = NULL;
}

// ******************************************************************
// * func: EmuFlushTiledSurfaceLocks
// ******************************************************************
static void EmuFlushTiledSurfaceLocks()
{
    for(unsigned v = 0; v < sizeof(g_TiledSurfaceLocks) / sizeof(g_TiledSurfaceLocks[0]); v++)
    {
        if(g_TiledSurfaceLocks[v].pResource != NULL)
            EmuFlushTiledSurfaceLock(g_TiledSurfaceLocks[v].pResource);
    }
}

// ******************************************************************
// * func: EmuLockTiledSurface
// ******************************************************************
static HRESULT EmuLockTiledSurface(XTL::X_D3DResource *pResource, XTL::D3DLOCKED_RECT *pLockedRect, CONST RECT *pRect, DWORD Flags)
{
    if(pLockedRect == NULL)
        return D3DERR_INVALIDCALL;

    XTL::IDirect3DSurface8 *pSurface8 = pResource->EmuSurface8;
    if(pSurface8 == NULL)
        return D3DERR_INVALIDCALL;

    EmuFlushTiledSurfaceLock(pResource);

    EmuTiledSurfaceLock *pLock = EmuFindFreeTiledSurfaceLock();
    if(pLock->pResource != NULL)
        EmuFlushTiledSurfaceLock(pLock->pResource);

    pSurface8->UnlockRect();

    DWORD NewFlags = 0;
    if(Flags & EMU_D3DLOCK_READONLY)
        NewFlags |= D3DLOCK_READONLY;

    HRESULT hRet = pSurface8->LockRect(pLockedRect, pRect, NewFlags);
    if(FAILED(hRet))
        return hRet;

    pLock->pResource = pResource;
    pLock->pSurface8 = pSurface8;
    pLock->pSurface8->AddRef();

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3D8_CreateDevice
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3D8_CreateDevice
(
    UINT                        Adapter,
    D3DDEVTYPE                  DeviceType,
    HWND                        hFocusWindow,
    DWORD                       BehaviorFlags,
    X_D3DPRESENT_PARAMETERS    *pPresentationParameters,
    IDirect3DDevice8          **ppReturnedDeviceInterface
)
{
    D3D_TRACE("CreateDevice");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3D8_CreateDevice\n"
               "(\n"
               "   Adapter                   : 0x%.08X\n"
               "   DeviceType                : 0x%.08X\n"
               "   hFocusWindow              : 0x%.08X\n"
               "   BehaviorFlags             : 0x%.08X\n"
               "   pPresentationParameters   : 0x%.08X\n"
               "   ppReturnedDeviceInterface : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Adapter, DeviceType, hFocusWindow,
               BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
    }
    #endif

    // Cache parameters
    g_EmuD3D8CreateDeviceProxyData.Adapter = Adapter;
    g_EmuD3D8CreateDeviceProxyData.DeviceType = DeviceType;
    g_EmuD3D8CreateDeviceProxyData.hFocusWindow = hFocusWindow;
    g_EmuD3D8CreateDeviceProxyData.pPresentationParameters = pPresentationParameters;
    g_EmuD3D8CreateDeviceProxyData.ppReturnedDeviceInterface = ppReturnedDeviceInterface;

    // Signal proxy thread, and wait for completion
    g_EmuD3D8CreateDeviceProxyData.bReady = true;

    while(g_EmuD3D8CreateDeviceProxyData.bReady)
        Sleep(10);

    EmuSwapFS();   // XBox FS

    return g_EmuD3D8CreateDeviceProxyData.hRet;
}

// ******************************************************************
// * func: EmuIDirect3D8_GetAdapterModeCount
// ******************************************************************
UINT WINAPI XTL::EmuIDirect3D8_GetAdapterModeCount
(
    UINT                        Adapter
)
{
    D3D_TRACE("GetAdapterModeCount");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3D8_GetAdapterModeCount\n"
               "(\n"
               "   Adapter                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Adapter);
    }
    #endif

    // NOTE: WARNING: We should return only modes that should exist on a real
    // Xbox. This could even be configurable, if desirable.

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    UINT ret = g_pD3D8->GetAdapterModeCount(g_XBVideo.GetDisplayAdapter());

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3D8_GetAdapterDisplayMode
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3D8_GetAdapterDisplayMode
(
    UINT                        Adapter,
    X_D3DDISPLAYMODE           *pMode
)
{
    D3D_TRACE("GetAdapterDisplayMode");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3D8_GetAdapterDisplayMode\n"
               "(\n"
               "   Adapter                   : 0x%.08X\n"
               "   pMode                     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Adapter, pMode);
    }
    #endif

    // NOTE: WARNING: We should cache the "Emulated" display mode and return
    // This value. We can initialize the cache with the default Xbox mode data.

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    HRESULT hRet = g_pD3D8->GetAdapterDisplayMode
    (
        g_XBVideo.GetDisplayAdapter(),
        (D3DDISPLAYMODE*)pMode
    );

    // ******************************************************************
    // * make adjustments to parameters to make sense with windows d3d
    // ******************************************************************
    {
        D3DDISPLAYMODE *pPCMode = (D3DDISPLAYMODE*)pMode;

        // Convert Format (PC->Xbox)
        pMode->Format = EmuPC2XB_D3DFormat(pPCMode->Format);

        // TODO: Make this configurable in the future?
        // D3DPRESENTFLAG_FIELD | D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_LOCKABLE_BACKBUFFER
        pMode->Flags  = 0x000000A1;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3D8_EnumAdapterModes
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3D8_EnumAdapterModes
(
    UINT                        Adapter,
    UINT                        Mode,
    X_D3DDISPLAYMODE           *pMode
)
{
    D3D_TRACE("EnumAdapterModes");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3D8_EnumAdapterModes\n"
               "(\n"
               "   Adapter                   : 0x%.08X\n"
               "   Mode                      : 0x%.08X\n"
               "   pMode                     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Adapter, Mode, pMode);
    }
    #endif

    // NOTE: WARNING: We should probably only return valid xbox display modes,
    // this should be coordinated with GetAdapterModeCount, etc.

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    HRESULT hRet = g_pD3D8->EnumAdapterModes(g_XBVideo.GetDisplayAdapter(), Mode, (D3DDISPLAYMODE*)pMode);

    // ******************************************************************
    // * make adjustments to parameters to make sense with windows d3d
    // ******************************************************************
    {
        D3DDISPLAYMODE *pPCMode = (D3DDISPLAYMODE*)pMode;

        // Convert Format (PC->Xbox)
        pMode->Format = EmuPC2XB_D3DFormat(pPCMode->Format);

        // TODO: Make this configurable in the future?
        // D3DPRESENTFLAG_FIELD | D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_LOCKABLE_BACKBUFFER
        pMode->Flags  = 0x000000A1;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3D8_KickOffAndWaitForIdle
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3D8_KickOffAndWaitForIdle()
{
    D3D_TRACE("KickOffAndWaitForIdle");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3D8_KickOffAndWaitForIdle()\n", GetCurrentThreadId());
    }
    #endif

    // TODO: Actually do something here?

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_AddRef
// ******************************************************************
ULONG WINAPI XTL::EmuIDirect3DDevice8_AddRef()
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("AddRef");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_AddRef()\n", GetCurrentThreadId());
    }
    #endif

    ULONG ret = g_pD3DDevice8->AddRef();

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BeginStateBlock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_BeginStateBlock()
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("BeginStateBlock");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_BeginStateBlock()\n", GetCurrentThreadId());
    }
    #endif

    ULONG ret = g_pD3DDevice8->BeginStateBlock();

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CaptureStateBlock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CaptureStateBlock(DWORD Token)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("CaptureStateBlock");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CaptureStateBlock\n"
               "(\n"
               "   Token               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Token);
    }
    #endif

    ULONG ret = g_pD3DDevice8->CaptureStateBlock(Token);

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_ApplyStateBlock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_ApplyStateBlock(DWORD Token)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("ApplyStateBlock");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_ApplyStateBlock\n"
               "(\n"
               "   Token               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Token);
    }
    #endif

    ULONG ret = g_pD3DDevice8->ApplyStateBlock(Token);

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_EndStateBlock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_EndStateBlock(DWORD *pToken)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("EndStateBlock");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_EndStateBlock\n"
               "(\n"
               "   pToken              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pToken);
    }
    #endif

    ULONG ret = g_pD3DDevice8->EndStateBlock(pToken);

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CopyRects
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CopyRects
(
    X_D3DSurface       *pSourceSurface,
    CONST RECT         *pSourceRectsArray,
    UINT                cRects,
    X_D3DSurface       *pDestinationSurface,
    CONST POINT        *pDestPointsArray
)
{
    D3D_TRACE("CopyRects");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CopyRects\n"
               "(\n"
               "   pSourceSurface      : 0x%.08X\n"
               "   pSourceRectsArray   : 0x%.08X\n"
               "   cRects              : 0x%.08X\n"
               "   pDestinationSurface : 0x%.08X\n"
               "   pDestPointsArray    : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pSourceSurface, pSourceRectsArray, cRects,
               pDestinationSurface, pDestPointsArray);
    }
    #endif

    // ******************************************************************
    // * Redirect to PC D3D
    // ******************************************************************
    HRESULT hRet = D3D_OK;
    __try
    {
        hRet = g_pD3DDevice8->CopyRects
        (
            pSourceSurface->EmuSurface8,
            pSourceRectsArray,
            cRects,
            pDestinationSurface->EmuSurface8,
            pDestPointsArray
        );
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        hRet = D3DERR_INVALIDCALL;
    }

    EmuSwapFS();   // Xbox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateImageSurface
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateImageSurface
(
    UINT                Width,
    UINT                Height,
    X_D3DFORMAT         Format,
    X_D3DSurface      **ppBackBuffer
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("CreateImageSurface");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateImageSurface\n"
               "(\n"
               "   Width               : 0x%.08X\n"
               "   Height              : 0x%.08X\n"
               "   Format              : 0x%.08X\n"
               "   ppBackBuffer        : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Width, Height, Format, ppBackBuffer);
    }
    #endif

    *ppBackBuffer = new X_D3DSurface();

    D3DFORMAT PCFormat = EmuXB2PC_D3DFormat(Format);

    HRESULT hRet = g_pD3DDevice8->CreateImageSurface(Width, Height, PCFormat, &((*ppBackBuffer)->EmuSurface8));

    EmuSwapFS();   // Xbox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetBackBuffer2
// ******************************************************************
XTL::X_D3DSurface* WINAPI XTL::EmuIDirect3DDevice8_GetBackBuffer2
(
    INT                 BackBuffer
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("GetBackBuffer2");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetBackBuffer2\n"
               "(\n"
               "   BackBuffer          : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), BackBuffer);
    }
    #endif

    /* Temporarily? removed
    X_D3DSurface *pBackBuffer = new X_D3DSurface();

    if(BackBuffer == -1)
    {
        static IDirect3DSurface8 *pCachedPrimarySurface = 0;

        if(pCachedPrimarySurface == 0)
        {
            // create a buffer to return
            // TODO: Verify the surface is always 640x480
            g_pD3DDevice8->CreateImageSurface(640, 480, D3DFMT_A8R8G8B8, &pBackBuffer->EmuSurface8);
        } 
        else
            pBackBuffer->EmuSurface8 = pCachedPrimarySurface;

        HRESULT hRet = g_pD3DDevice8->GetFrontBuffer(pBackBuffer->EmuSurface8);

        if(FAILED(hRet))
        {
            EmuWarning("Could not retrieve primary surface, using backbuffer");
            pBackBuffer->EmuSurface8->Release();
            pBackBuffer->EmuSurface8 = 0;
            BackBuffer = 0;
        }

        // Debug: Save this image temporarily
        //D3DXSaveSurfaceToFile("C:\\Aaron\\Textures\\FrontBuffer.bmp", D3DXIFF_BMP, pBackBuffer->EmuSurface8, NULL, NULL);
    }

    if(BackBuffer != -1)
        g_pD3DDevice8->GetBackBuffer(BackBuffer, D3DBACKBUFFER_TYPE_MONO, &(pBackBuffer->EmuSurface8));
    */

    X_D3DSurface *pBackBuffer = new X_D3DSurface();

    if(BackBuffer == -1)
        BackBuffer = 0;

    HRESULT hRet = g_pD3DDevice8->GetBackBuffer(BackBuffer, D3DBACKBUFFER_TYPE_MONO, &(pBackBuffer->EmuSurface8));

    if(FAILED(hRet))
        EmuCleanup("Unable to retrieve back buffer");

    EmuSwapFS();   // Xbox FS

    return pBackBuffer;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetBackBuffer
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetBackBuffer
(
    INT                 BackBuffer,
    D3DBACKBUFFER_TYPE  Type,
    X_D3DSurface      **ppBackBuffer
)
{
    D3D_TRACE("GetBackBuffer");
    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetBackBuffer\n"
               "(\n"
               "   BackBuffer          : 0x%.08X\n"
               "   Type                : 0x%.08X\n"
               "   ppBackBuffer        : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), BackBuffer, Type, ppBackBuffer);
        EmuSwapFS();   // Xbox FS
    }
    #endif

    *ppBackBuffer = EmuIDirect3DDevice8_GetBackBuffer2(BackBuffer);

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetViewport
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetViewport
(
    CONST D3DVIEWPORT8 *pViewport
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetViewport");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetViewport\n"
               "(\n"
               "   pViewport           : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pViewport);
    }
    #endif

    // On Xbox the NV2A imposes no relationship between the viewport and the
    // render target: a viewport may extend past the framebuffer (it just
    // clips), and MinZ/MaxZ may lie outside [0,1]. Host d3d8 validates
    // SetViewport strictly -- X+Width and Y+Height must fit the bound render
    // target and the depth range must be within [0,1] -- and rejects a
    // violation by throwing the HRESULT internally (a bare C++ `throw`), which
    // does not unwind across our FS content-swap: the process dies
    // 0xE06D7363/D3DERR_NOTAVAILABLE. Turok Evolution's render-to-texture path
    // sets a 512x512 viewport against a 640x480 backbuffer and hits this on
    // every frame. Clamp the viewport into the host-valid range and forward
    // the clamped copy; report success, as real hardware would have accepted
    // the call. (Same guard discipline as SetRenderTarget / Clear.)
    D3DVIEWPORT8 vp = *pViewport;

    bool Clamped = false;

    // Depth range: host requires MinZ/MaxZ within [0,1].
    if(vp.MinZ < 0.0f) { vp.MinZ = 0.0f; Clamped = true; }
    if(vp.MinZ > 1.0f) { vp.MinZ = 1.0f; Clamped = true; }
    if(vp.MaxZ < 0.0f) { vp.MaxZ = 0.0f; Clamped = true; }
    if(vp.MaxZ > 1.0f) { vp.MaxZ = 1.0f; Clamped = true; }

    // Extents: host requires X+Width <= RT width and Y+Height <= RT height.
    IDirect3DSurface8 *pRT = NULL;
    if(SUCCEEDED(g_pD3DDevice8->GetRenderTarget(&pRT)) && pRT != NULL)
    {
        D3DSURFACE_DESC RTDesc;
        if(SUCCEEDED(pRT->GetDesc(&RTDesc)))
        {
            if((DWORD)vp.X > RTDesc.Width) { vp.X = RTDesc.Width; Clamped = true; }
            if((DWORD)vp.Y > RTDesc.Height) { vp.Y = RTDesc.Height; Clamped = true; }
            DWORD MaxW = RTDesc.Width - vp.X;
            DWORD MaxH = RTDesc.Height - vp.Y;
            if(vp.Width > MaxW) { vp.Width = MaxW; Clamped = true; }
            if(vp.Height > MaxH) { vp.Height = MaxH; Clamped = true; }
        }
        pRT->Release();
    }

    if(Clamped)
    {
        static LONG WarnCount = 0;
        if(InterlockedIncrement(&WarnCount) <= 5)
            EmuWarning("SetViewport clamped to host bounds: in{x=%lu y=%lu w=%lu h=%lu minz=%g maxz=%g} -> out{x=%lu y=%lu w=%lu h=%lu minz=%g maxz=%g}",
                       pViewport->X, pViewport->Y, pViewport->Width, pViewport->Height, pViewport->MinZ, pViewport->MaxZ,
                       vp.X, vp.Y, vp.Width, vp.Height, vp.MinZ, vp.MaxZ);
    }

    HRESULT hRet = g_pD3DDevice8->SetViewport(&vp);

    EmuSwapFS();   // Xbox FS

    // The title's call would have succeeded on hardware; do not feed it an
    // error path it never takes there.
    if(FAILED(hRet))
        hRet = D3D_OK;

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetShaderConstantMode
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetShaderConstantMode
(
    DWORD               dwMode    // TODO: Fill out enumeration
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetShaderConstantMode");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetShaderConstantMode\n"
               "(\n"
               "   dwMode              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), dwMode);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // Xbox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetRenderTarget
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetRenderTarget
(
    X_D3DSurface  **ppRenderTarget
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("GetRenderTarget");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetRenderTarget\n"
               "(\n"
               "   ppRenderTarget      : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), ppRenderTarget);
    }
    #endif

    IDirect3DSurface8 *pSurface8 = g_pCachedRenderTarget->EmuSurface8;

    pSurface8->AddRef();

    *ppRenderTarget = g_pCachedRenderTarget;

    EmuSwapFS();   // Xbox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetRenderTarget2
// ******************************************************************
XTL::X_D3DSurface * WINAPI XTL::EmuIDirect3DDevice8_GetRenderTarget2()
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("GetRenderTarget2");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetRenderTarget2()\n",
               GetCurrentThreadId());
    }
    #endif

    IDirect3DSurface8 *pSurface8 = g_pCachedRenderTarget->EmuSurface8;

    pSurface8->AddRef();

    EmuSwapFS();   // Xbox FS

    return g_pCachedRenderTarget;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetDepthStencilSurface
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetDepthStencilSurface
(
    X_D3DSurface  **ppZStencilSurface
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("GetDepthStencilSurface");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetDepthStencilSurface\n"
               "(\n"
               "   ppZStencilSurface   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), ppZStencilSurface);
    }
    #endif

    IDirect3DSurface8 *pSurface8 = g_pCachedZStencilSurface->EmuSurface8;

    if(pSurface8 != 0)
        pSurface8->AddRef();

    *ppZStencilSurface = g_pCachedZStencilSurface;

    EmuSwapFS();   // Xbox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetDepthStencilSurface2
// ******************************************************************
XTL::X_D3DSurface * WINAPI XTL::EmuIDirect3DDevice8_GetDepthStencilSurface2()
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("GetDepthStencilSurface2");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetDepthStencilSurface2()\n",
               GetCurrentThreadId());
    }
    #endif

    if(g_pCachedZStencilSurface == NULL)
    {
        EmuSwapFS();   // Xbox FS
        return NULL;
    }

    IDirect3DSurface8 *pSurface8 = g_pCachedZStencilSurface->EmuSurface8;

    if(pSurface8 != 0)
        pSurface8->AddRef();

    EmuSwapFS();   // Xbox FS

    return g_pCachedZStencilSurface;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetTile
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetTile
(
    DWORD           Index,
    X_D3DTILE      *pTile
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("GetTile");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetTile\n"
               "(\n"
               "   Index               : 0x%.08X\n"
               "   pTile               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Index, pTile);
    }
    #endif

    if(pTile != NULL)
        memcpy(pTile, &EmuD3DTileCache[Index], sizeof(X_D3DTILE));

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTileNoWait
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetTileNoWait
(
    DWORD               Index,
    CONST X_D3DTILE    *pTile
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetTileNoWait");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetTileNoWait\n"
               "(\n"
               "   Index               : 0x%.08X\n"
               "   pTile               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Index, pTile);
    }
    #endif

    if(pTile != NULL)
        memcpy(&EmuD3DTileCache[Index], pTile, sizeof(X_D3DTILE));

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * EmuLiveVertexShaders
// ******************************************************************
// Live X_D3DVertexShader wrappers handed out by CreateVertexShader. Titles
// can delete a shader handle twice or pass a stale one; IsBadReadPtr cannot
// detect freed heap memory, so an unregistered-pointer check is the only
// reliable guard against a double delete corrupting the host heap.
constexpr std::size_t EMU_VSH_LIVE_CAPACITY = 256;
constexpr std::size_t EMU_VSH_MAX_INSTRUCTIONS = 136;
constexpr std::size_t EMU_VSH_MAX_DECLARATION_TOKENS = 128;

struct EmuVshCpuFallback
{
    bool enabled = false;
    bool bindLogged = false;
    bool unbindLogged = false;
    bool drawLogged = false;
    bool geometryLogged = false;
    bool materialLogged = false;
    bool collapseCaptureLogged = false;
    bool rejectionLogged = false;
    std::uint32_t hash = 0;
    std::size_t instructionCount = 0;
    std::size_t declarationTokenCount = 0;
    std::array<DWORD, 1 + EMU_VSH_MAX_INSTRUCTIONS * 4> function{};
    std::array<DWORD, EMU_VSH_MAX_DECLARATION_TOKENS> declaration{};
};

static XTL::X_D3DVertexShader* g_EmuLiveVertexShaders[EMU_VSH_LIVE_CAPACITY] = { 0 };
static EmuVshCpuFallback g_EmuCpuVertexShaders[EMU_VSH_LIVE_CAPACITY] = {};
static EmuVshCpuFallback* g_EmuCurrentCpuVertexShader = nullptr;
static float g_EmuVshCpuConstants[192 * 4] = {};
static XTL::X_D3DVertexBuffer* g_EmuVshCpuStreams[16] = {};
static UINT g_EmuVshCpuStreamStrides[16] = {};
static XTL::X_D3DIndexBuffer* g_EmuVshCpuIndexBuffer = nullptr;
static UINT g_EmuVshCpuBaseVertexIndex = 0;

static EmuVshCpuFallback* EmuVshFindLive(XTL::X_D3DVertexShader* pShader)
{
    for(std::size_t index = 0; index < EMU_VSH_LIVE_CAPACITY; ++index)
    {
        if(g_EmuLiveVertexShaders[index] == pShader)
        {
            return &g_EmuCpuVertexShaders[index];
        }
    }
    return nullptr;
}

static void EmuVshLogCpuBinding(EmuVshCpuFallback* metadata, const char* api, bool bound)
{
    if(metadata == nullptr || !metadata->enabled)
    {
        return;
    }
    bool& logged = bound ? metadata->bindLogged : metadata->unbindLogged;
    if(logged)
    {
        return;
    }
    printf("VSH| cpu_bind hash=%08X api=%s state=%s\n",
           static_cast<unsigned int>(metadata->hash), api, bound ? "bound" : "unbound");
    fflush(stdout);
    logged = true;
}

static void EmuVshSetCurrentCpuShader(EmuVshCpuFallback* metadata, const char* api)
{
    if(g_EmuCurrentCpuVertexShader == metadata)
    {
        return;
    }
    EmuVshLogCpuBinding(g_EmuCurrentCpuVertexShader, api, false);
    g_EmuCurrentCpuVertexShader = metadata;
    EmuVshLogCpuBinding(g_EmuCurrentCpuVertexShader, api, true);
}

static bool EmuVshRegisterLive(XTL::X_D3DVertexShader* pShader, bool cpuFallback,
                               const DWORD* xboxFunction, const DWORD* xboxDeclaration,
                               std::size_t declarationTokenCount)
{
    for(std::size_t index = 0; index < EMU_VSH_LIVE_CAPACITY; ++index)
    {
        if(g_EmuLiveVertexShaders[index] != nullptr)
        {
            continue;
        }

        g_EmuLiveVertexShaders[index] = pShader;
        EmuVshCpuFallback& metadata = g_EmuCpuVertexShaders[index];
        metadata = {};
        if(!cpuFallback)
        {
            return true;
        }

        metadata.enabled = true;
        metadata.hash = XTL::VshDiagnostics::HashXboxFunction(xboxFunction);
        const DWORD encodedCount = (xboxFunction[0] >> 16) & 0xFFFFu;
        metadata.instructionCount = encodedCount == 0 || encodedCount > EMU_VSH_MAX_INSTRUCTIONS
                                        ? EMU_VSH_MAX_INSTRUCTIONS
                                        : static_cast<std::size_t>(encodedCount);
        std::memcpy(metadata.function.data(), xboxFunction,
                    (1 + metadata.instructionCount * 4) * sizeof(DWORD));
        if(xboxDeclaration == nullptr || declarationTokenCount == 0 ||
           declarationTokenCount > metadata.declaration.size())
        {
            metadata.enabled = false;
            g_EmuLiveVertexShaders[index] = nullptr;
            metadata = {};
            return false;
        }
        metadata.declarationTokenCount = declarationTokenCount;
        std::memcpy(metadata.declaration.data(), xboxDeclaration,
                    declarationTokenCount * sizeof(DWORD));
        return true;
    }
    return false;
}

static bool EmuVshUnregisterLive(XTL::X_D3DVertexShader* pShader)
{
    EmuVshCpuFallback* metadata = EmuVshFindLive(pShader);
    if(metadata == nullptr)
    {
        return false;
    }
    if(g_EmuCurrentCpuVertexShader == metadata)
    {
        EmuVshSetCurrentCpuShader(nullptr, "DeleteVertexShader");
    }
    const std::size_t index = static_cast<std::size_t>(metadata - g_EmuCpuVertexShaders);
    g_EmuLiveVertexShaders[index] = nullptr;
    *metadata = {};
    return true;
}

static HRESULT EmuVshCreateHostShader(const DWORD* declaration, const DWORD* function,
                                      DWORD* handle, DWORD usage)
{
    __try
    {
        return g_pD3DDevice8->CreateVertexShader(declaration, function, handle, usage);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return D3DERR_INVALIDCALL;
    }
}

static void EmuVshDumpRejectedShader(const DWORD* xboxFunction, const DWORD* d3dFunction,
                                     const DWORD* xboxDeclaration,
                                     const DWORD* d3dDeclaration, const char* reason)
{
    try
    {
        const XTL::VshDiagnostics::TranslationCapture capture = {
            xboxFunction,
            d3dFunction,
            xboxDeclaration,
            d3dDeclaration,
            reason,
            g_EmuVshCpuConstants,
            std::size(g_EmuVshCpuConstants),
            nullptr,
            0,
            "canonical",
        };
        XTL::VshDiagnostics::DumpRejectedTranslation(stdout, capture);
        if(xboxFunction != nullptr && (xboxFunction[0] & 0xFFFFu) == 0x2078u)
        {
            static std::array<std::uint32_t, 256> replayedHashes{};
            static std::size_t replayedHashCount = 0;
            const std::uint32_t hash = XTL::VshDiagnostics::HashXboxFunction(xboxFunction);
            bool alreadyCaptured = false;
            for(std::size_t index = 0; index < replayedHashCount; ++index)
            {
                alreadyCaptured = alreadyCaptured || replayedHashes[index] == hash;
            }
            if(!alreadyCaptured)
            {
                if(replayedHashCount < replayedHashes.size())
                {
                    replayedHashes[replayedHashCount++] = hash;
                }
                else
                {
                    replayedHashes[hash % replayedHashes.size()] = hash;
                }
                XTL::VshDiagnostics::DumpReplayCapture(stdout, capture);
            }
        }
    }
    catch(...)
    {
        EmuWarning("VshDecoder: rejected-shader diagnostic capture raised a host exception");
    }
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateVertexShader
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateVertexShader
(
    CONST DWORD    *pDeclaration,
    CONST DWORD    *pFunction,
    DWORD          *pHandle,
    DWORD           Usage
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("CreateVertexShader");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateVertexShader\n"
               "(\n"
               "   pDeclaration        : 0x%.08X\n"
               "   pFunction           : 0x%.08X\n"
               "   pHandle             : 0x%.08X\n"
               "   Usage               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pDeclaration, pFunction, pHandle, Usage);
    }
    #endif

    // ******************************************************************
    // * create emulated shader struct
    // ******************************************************************
    X_D3DVertexShader *pD3DVertexShader = new X_D3DVertexShader();

    // Todo: Intelligently fill out these fields as necessary
    ZeroMemory(pD3DVertexShader, sizeof(X_D3DVertexShader));

    // An Xbox function blob (version token low word 0x2078) is 128-bit NV2A
    // vertex-program microcode, and the Xbox declaration carries extended
    // data-type codes -- neither is host-consumable. Recompile the microcode
    // into vs.1.1 bytecode and rewrite the declaration types (EmuVshDecoder).
    extern DWORD* EmuVshRecompileXboxFunction(CONST DWORD * pXboxFunction);

    DWORD* pRecompiled = NULL;
    const DWORD* pHostFunction = pFunction;
    bool cpuFallback = false;
    std::string cpuFallbackReason;
    bool rejectShader = false;
    std::string rejectionReason;
    const bool isXboxFunction = pFunction != NULL && (pFunction[0] & 0xFFFF) == 0x2078;
    if(isXboxFunction)
    {
        const XTL::VshDiagnostics::XboxFunctionDisposition disposition =
            XTL::VshDiagnostics::ClassifyXboxFunction(pFunction, rejectionReason);
        rejectShader = disposition == XTL::VshDiagnostics::XboxFunctionDisposition::Reject;
        cpuFallback =
            disposition == XTL::VshDiagnostics::XboxFunctionDisposition::ExecuteOnCpu;
        if(cpuFallback && pDeclaration == nullptr)
        {
            rejectShader = true;
            cpuFallback = false;
            rejectionReason = "cpu_fallback_requires_declaration";
        }
        else if(cpuFallback)
        {
            cpuFallbackReason = rejectionReason;
            pHostFunction = nullptr;
        }
        else if(!rejectShader)
        {
            pRecompiled = EmuVshRecompileXboxFunction(pFunction);
            pHostFunction = pRecompiled;
            if(pRecompiled == NULL)
            {
                rejectShader = true;
                rejectionReason = "recompilation_failed";
            }
        }
    }

    if(rejectShader)
    {
        EmuVshDumpRejectedShader(pFunction, pRecompiled, pDeclaration, nullptr,
                                 rejectionReason.c_str());
        if(pHandle != nullptr)
        {
            *pHandle = 0;
        }
        delete pD3DVertexShader;
        EmuSwapFS(); // XBox FS
        return D3DERR_INVALIDCALL;
    }

    DWORD TranslatedDecl[128];
    const DWORD* pHostDeclaration = pDeclaration;
    std::size_t declarationTokenCount = 0;
    bool declarationCpuCompatible = true;
    bool translatedDeclarationAvailable = false;
    std::string declarationCpuIncompatibilityReason;
    if(pDeclaration != NULL)
    {
        const XTL::VshDiagnostics::DeclarationTranslationResult declarationResult =
            XTL::VshDiagnostics::TranslateXboxDeclaration(pDeclaration, TranslatedDecl,
                                                          std::size(TranslatedDecl));
        declarationTokenCount = declarationResult.tokenCount;
        declarationCpuCompatible = declarationResult.cpuCompatible;
        translatedDeclarationAvailable =
            declarationResult.disposition !=
                XTL::VshDiagnostics::XboxFunctionDisposition::Reject &&
            declarationResult.tokenCount != 0;
        declarationCpuIncompatibilityReason = declarationResult.cpuIncompatibilityReason;
        if(cpuFallback && !declarationResult.cpuCompatible)
        {
            rejectShader = true;
            rejectionReason = declarationResult.cpuIncompatibilityReason;
        }
        else if(declarationResult.disposition ==
                XTL::VshDiagnostics::XboxFunctionDisposition::Reject)
        {
            rejectShader = true;
            rejectionReason = declarationResult.reason;
        }
        else if(declarationResult.disposition ==
                XTL::VshDiagnostics::XboxFunctionDisposition::ExecuteOnCpu)
        {
            if(!isXboxFunction)
            {
                rejectShader = true;
                rejectionReason = "cpu_declaration_requires_xbox_function";
            }
            else
            {
                cpuFallback = true;
                cpuFallbackReason = declarationResult.reason;
                delete[] pRecompiled;
                pRecompiled = NULL;
                pHostFunction = nullptr;
            }
        }
        else
        {
            pHostDeclaration = TranslatedDecl;
        }
    }

    if(rejectShader)
    {
        EmuVshDumpRejectedShader(
            pFunction, pRecompiled, pDeclaration,
            translatedDeclarationAvailable ? TranslatedDecl : nullptr, rejectionReason.c_str());
        if(pHandle != nullptr)
        {
            *pHandle = 0;
        }
        delete[] pRecompiled;
        delete pD3DVertexShader;
        EmuSwapFS(); // XBox FS
        return D3DERR_INVALIDCALL;
    }
    const bool wasRecompiled = pRecompiled != NULL;

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    HRESULT hRet = D3D_OK;
    bool hostCallAttempted = false;
    bool translationRejected = false;
    if(pRecompiled != NULL)
    {
        try
        {
            const XTL::VshDiagnostics::ValidationResult validation =
                XTL::VshDiagnostics::ValidateD3D8Translation(pFunction, pRecompiled);
            if(!validation.valid)
            {
                const bool exceedsHostLimit =
                    validation.message == "instruction count exceeds the vs.1.1 limit of 128";
                if(exceedsHostLimit && pDeclaration != nullptr)
                {
                    if(declarationCpuCompatible)
                    {
                        cpuFallback = true;
                        cpuFallbackReason = "host_instruction_limit";
                    }
                    else
                    {
                        hRet = D3DERR_INVALIDCALL;
                        translationRejected = true;
                        rejectionReason = declarationCpuIncompatibilityReason;
                    }
                }
                else
                {
                    hRet = D3DERR_INVALIDCALL;
                    translationRejected = true;
                    rejectionReason = validation.message;
                }
            }
        }
        catch(...)
        {
            EmuWarning("VshDecoder: validation raised a host exception");
            hRet = D3DERR_INVALIDCALL;
            translationRejected = true;
            rejectionReason = "translation_validation_exception";
        }
    }

    if(SUCCEEDED(hRet) && !cpuFallback)
    {
        hostCallAttempted = true;
        hRet = EmuVshCreateHostShader(pHostDeclaration, pHostFunction,
                                      &pD3DVertexShader->Handle, g_dwVertexShaderUsage);
    }

    if(FAILED(hRet) && pRecompiled != NULL)
    {
        const char* diagnosticReason = rejectionReason.empty()
                                           ? (hostCallAttempted ? "host_create_failed"
                                                                : "translation_validation_failed")
                                           : rejectionReason.c_str();
        EmuVshDumpRejectedShader(pFunction, pRecompiled, pDeclaration, pHostDeclaration,
                                 diagnosticReason);
    }

    delete[] pRecompiled;

    if(translationRejected)
    {
        if(pHandle != nullptr)
        {
            *pHandle = 0;
        }
        delete pD3DVertexShader;
        EmuSwapFS(); // XBox FS
        return D3DERR_INVALIDCALL;
    }

    if(!EmuVshRegisterLive(pD3DVertexShader, cpuFallback, pFunction, pDeclaration,
                           declarationTokenCount))
    {
        if(pD3DVertexShader->Handle != 0)
        {
            g_pD3DDevice8->DeleteVertexShader(pD3DVertexShader->Handle);
        }
        delete pD3DVertexShader;
        EmuSwapFS(); // XBox FS
        return E_OUTOFMEMORY;
    }

    *pHandle = (DWORD)pD3DVertexShader;

    if(FAILED(hRet))
    {
        printf("EmuD3D8 (0x%X): CreateVertexShader FAILED %s (hr=0x%.08X)%s.\n",
               GetCurrentThreadId(), hostCallAttempted ? "on the host" : "during translation validation", hRet,
               wasRecompiled ? " for a recompiled Xbox shader" : "");
        fflush(stdout);

        hRet = D3D_OK;
    }
    else if(cpuFallback)
    {
        printf("VSH| fallback hash=%08X mode=cpu reason=%s\n",
               static_cast<unsigned int>(XTL::VshDiagnostics::HashXboxFunction(pFunction)),
               cpuFallbackReason.c_str());
        fflush(stdout);
    }
    else if(wasRecompiled)
    {
        printf("EmuD3D8 (0x%X): CreateVertexShader OK, host handle 0x%.08X (recompiled Xbox shader).\n",
               GetCurrentThreadId(), pD3DVertexShader->Handle);
        fflush(stdout);
    }

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShaderConstant
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetVertexShaderConstant
(
    INT         Register,
    CONST PVOID pConstantData,
    DWORD       ConstantCount
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetVertexShaderConstant");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexShaderConstant\n"
               "(\n"
               "   Register            : 0x%.08X\n"
               "   pConstantData       : 0x%.08X\n"
               "   ConstantCount       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Register, pConstantData, ConstantCount);
    }
#endif

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    // On Xbox the vertex-program constant file is larger than the host
    // vs.1.1 constant limit (96), and titles set constants at indices the
    // host rejects by throwing the HRESULT internally (a bare C++ `throw`),
    // which does not unwind across our FS content-swap -- the process dies
    // 0xE06D7363/E_FAIL (Turok Evolution's render path, immediately before
    // its 512x512 render-to-texture viewport). Clamp the register range into
    // the host-valid [0, 95] window and forward only what fits; report
    // success, as real hardware would have accepted the call. (Same guard
    // discipline as SetRenderTarget / Clear / SetViewport.)
    if(pConstantData != nullptr && ConstantCount != 0)
    {
        __try
        {
            const float* source = static_cast<const float*>(pConstantData);
            for(DWORD constant = 0; constant < ConstantCount; ++constant)
            {
                const std::int64_t hardwareIndex = static_cast<std::int64_t>(Register) + 96 + constant;
                if(hardwareIndex >= 0 && hardwareIndex < 192)
                {
                    std::memcpy(&g_EmuVshCpuConstants[hardwareIndex * 4], &source[constant * 4], 4 * sizeof(float));
                }
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            EmuWarning("SetVertexShaderConstant could not mirror guest constant data");
        }
    }

    HRESULT hRet = D3D_OK;
    if(g_pD3DDevice8 != 0)
    {
        const INT HostMaxConstants = 96; // D3DVS_CONSTREG_MAX (vs.1.1)
        INT Reg = Register;
        DWORD Count = ConstantCount;
        CONST BYTE* pData = (CONST BYTE*)pConstantData;

        if(Reg < 0)
        {
            DWORD Skip = (DWORD)(-Reg);
            if(Count <= Skip)
                Count = 0;
            else
            {
                Count -= Skip;
                Reg = 0;
                pData += Skip * 16;
            }
        }
        if(Reg >= HostMaxConstants)
            Count = 0;
        else if(Reg + (INT)Count > HostMaxConstants)
            Count = (DWORD)(HostMaxConstants - Reg);

        if(Count > 0)
            hRet = g_pD3DDevice8->SetVertexShaderConstant(Reg, pData, Count);
    }

    if(FAILED(hRet))
    {
        static LONG WarnCount = 0;
        if(InterlockedIncrement(&WarnCount) <= 5)
            EmuWarning("SetVertexShaderConstant failed (Register = %d, Count = %d) hr=0x%.08X", Register, ConstantCount, (DWORD)hRet);

        hRet = D3D_OK;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShaderConstantNotInline
// ******************************************************************
VOID __fastcall XTL::EmuIDirect3DDevice8_SetVertexShaderConstantNotInline
(
    INT         Register,
    CONST PVOID pConstantData,
    DWORD       ConstantCount
)
{
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexShaderConstantNotInline\n"
               "(\n"
               "   Register            : 0x%.08X\n"
               "   pConstantData       : 0x%.08X\n"
               "   ConstantCount       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Register, pConstantData, ConstantCount);
        EmuSwapFS();   // XBox FS
    }
    #endif

    XTL::EmuIDirect3DDevice8_SetVertexShaderConstant(Register - 96, pConstantData, ConstantCount / 4);
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShaderConstant1
// ******************************************************************
VOID __fastcall XTL::EmuIDirect3DDevice8_SetVertexShaderConstant1
(
    INT         Register,
    CONST PVOID pConstantData
)
{
    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexShaderConstant1\n"
               "(\n"
               "   Register            : 0x%.08X\n"
               "   pConstantData       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Register, pConstantData);
        EmuSwapFS();   // XBox FS
    }
    #endif

    XTL::EmuIDirect3DDevice8_SetVertexShaderConstant(Register - 96, pConstantData, 1);

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShaderConstant4
// ******************************************************************
VOID __fastcall XTL::EmuIDirect3DDevice8_SetVertexShaderConstant4
(
    INT         Register,
    CONST PVOID pConstantData
)
{
    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexShaderConstant4\n"
               "(\n"
               "   Register            : 0x%.08X\n"
               "   pConstantData       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Register, pConstantData);
        EmuSwapFS();   // XBox FS
    }
    #endif

    XTL::EmuIDirect3DDevice8_SetVertexShaderConstant(Register - 96, pConstantData, 4);

    return;
}

// ******************************************************************
// * Pixel-shader fixed-function fallback
// ******************************************************************
// Untranslated Xbox register-combiner pixel shaders are emulated by driving the
// PC fixed-function texture stages: modulate the stage-0 texture by the vertex-
// lit diffuse colour (and modulate-in a second bound texture, e.g. a projected
// caustic map), which approximates the common "texture * lighting [* detail]"
// combiners well enough to show a lit, textured surface instead of black.
#define X_PIXELSHADER_FALLBACK_MARKER   0xF5000000u
#define X_PIXELSHADER_FALLBACK_MASK     0xFF000000u

static bool g_bUsePixelShaderFallback = false;
static DWORD g_EmuCurrentPixelShaderHandle = 0;

struct EmuPixelShaderFallbackDefinition
{
    DWORD handle = 0;
    cxbx::d3d::XboxPixelShaderDefinition definition{};
};

static std::vector<EmuPixelShaderFallbackDefinition> g_EmuPixelShaderFallbacks;

static const cxbx::d3d::XboxPixelShaderDefinition* EmuFindPixelShaderFallback(
    DWORD handle)
{
    for(const auto& fallback : g_EmuPixelShaderFallbacks)
    {
        if(fallback.handle == handle)
        {
            return &fallback.definition;
        }
    }
    return nullptr;
}

static cxbx::d3d::PixelShaderFallback EmuCurrentPixelShaderFallback()
{
    const auto* definition = EmuFindPixelShaderFallback(g_EmuCurrentPixelShaderHandle);
    return definition != nullptr
               ? cxbx::d3d::ClassifyPixelShaderFallback(*definition)
               : cxbx::d3d::PixelShaderFallback::TextureModulate;
}

static void EmuApplyPixelShaderFallback(cxbx::d3d::PixelShaderFallback fallback,
                                        bool allowStage3Remap)
{
    using namespace XTL;

    if(g_pD3DDevice8 == 0)
    {
        return;
    }

    g_pD3DDevice8->SetPixelShader(0);   // fixed-function

    DWORD stage0ColorOperation = D3DTOP_MODULATE;
    if(fallback == cxbx::d3d::PixelShaderFallback::Texture)
    {
        stage0ColorOperation = D3DTOP_SELECTARG1;
    }
    else if(fallback == cxbx::d3d::PixelShaderFallback::TextureModulate2X ||
            fallback == cxbx::d3d::PixelShaderFallback::TextureModulate2XStage3)
    {
        stage0ColorOperation = D3DTOP_MODULATE2X;
    }

    g_pD3DDevice8->SetTextureStageState(0, D3DTSS_COLOROP, stage0ColorOperation);
    g_pD3DDevice8->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_pD3DDevice8->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    g_pD3DDevice8->SetTextureStageState(
        0, D3DTSS_ALPHAOP,
        fallback == cxbx::d3d::PixelShaderFallback::Texture ? D3DTOP_SELECTARG1
                                                             : D3DTOP_MODULATE);
    g_pD3DDevice8->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    g_pD3DDevice8->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

    if(fallback == cxbx::d3d::PixelShaderFallback::DualTextureModulate2X)
    {
        g_pD3DDevice8->SetTextureStageState(1, D3DTSS_COLOROP,   D3DTOP_MODULATE2X);
        g_pD3DDevice8->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        g_pD3DDevice8->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
        g_pD3DDevice8->SetTextureStageState(1, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG2);
        g_pD3DDevice8->SetTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
        g_pD3DDevice8->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);
    }
    else if(fallback == cxbx::d3d::PixelShaderFallback::TextureModulate2XStage3 &&
            allowStage3Remap)
    {
        IDirect3DBaseTexture8* stage3Texture = nullptr;
        if(SUCCEEDED(g_pD3DDevice8->GetTexture(3, &stage3Texture)) &&
           stage3Texture != nullptr)
        {
            g_pD3DDevice8->SetTexture(1, stage3Texture);
            stage3Texture->Release();
            g_pD3DDevice8->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
            g_pD3DDevice8->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            g_pD3DDevice8->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
            g_pD3DDevice8->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
            g_pD3DDevice8->SetTextureStageState(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
            g_pD3DDevice8->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 3);
        }
        else
        {
            g_pD3DDevice8->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            g_pD3DDevice8->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        }
    }
    else
    {
        g_pD3DDevice8->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        g_pD3DDevice8->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    }
    g_pD3DDevice8->SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
    g_pD3DDevice8->SetTextureStageState(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

// ******************************************************************
// * Graphics debugging aids (all opt-in via environment variables)
// ******************************************************************
// CXBX_D3D_DRAW_TRACE=1      one DRAW| line per guest draw call
// CXBX_D3D_SKIP_DRAWS=i[:j]  skip guest draws with per-frame index in [i,j)
// CXBX_D3D_DUMP_DRAWS=i[:j]  after each guest draw with per-frame index in
//                            [i,j), dump the backbuffer to %TEMP%\cxbx_draw\
// CXBX_D3D_DUMP_FRAMES=i[:j] limit draw dumps to frames in [i,j)
// CXBX_D3D_TEXTURE_DUMP=1    dump the converted level 0 of every registered
//                            texture/surface to %TEMP%\cxbx_tex\
// CXBX_TEX_TRACE=1           one TEX| line per registered texture/surface
// CXBX_PSH_DUMP=1            dump the 60-dword combiner definition of every
//                            pixel shader that lands on the fixed-function
//                            fallback
// CXBX_D3D_PERF_MARKERS=1    emit D3DPERF (PIX) frame/draw events so external
//                            captures (apitrace, DXVK d3d8 + RenderDoc) are
//                            self-describing

static DWORD g_D3DDebugFrame = 0;     // increments at Swap
static DWORD g_D3DDebugDrawIndex = 0; // resets at Swap

static DWORD EmuD3DHashBytes(const void* data, std::size_t size)
{
    const BYTE* bytes = static_cast<const BYTE*>(data);
    DWORD hash = 2166136261u;
    for(std::size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static bool EmuD3DParseDrawRange(const char* value, DWORD& begin, DWORD& end)
{
    if(value == NULL || *value == '\0')
        return false;

    char* cursor = NULL;
    begin = strtoul(value, &cursor, 0);
    if(cursor != NULL && (*cursor == ':' || *cursor == '-'))
        end = strtoul(cursor + 1, NULL, 0);
    else
        end = begin + 1;
    return end > begin;
}

// Build "%TEMP%\<subDirectory>\<fileName>" and make sure the directory exists.
static void EmuD3DDebugDumpPath(char* buffer, std::size_t size,
                                const char* subDirectory, const char* fileName)
{
    char tempPath[MAX_PATH] = { 0 };
    const DWORD length = GetTempPathA(MAX_PATH, tempPath);
    if(length == 0 || length >= MAX_PATH)
        tempPath[0] = '\0';

    char directory[MAX_PATH];
    snprintf(directory, sizeof(directory), "%s%s", tempPath, subDirectory);
    CreateDirectoryA(directory, NULL);
    snprintf(buffer, size, "%s\\%s", directory, fileName);
}

// DXT (BC1/BC2/BC3) decompression for the texture dumper -- most of what
// real titles register is compressed, so the dumper decodes it rather than
// skipping it.
#define EMU_FOURCC_DXT1 0x31545844u
#define EMU_FOURCC_DXT2 0x32545844u
#define EMU_FOURCC_DXT3 0x33545844u
#define EMU_FOURCC_DXT4 0x34545844u
#define EMU_FOURCC_DXT5 0x35545844u

static DWORD EmuD3DExpand565(WORD color)
{
    const DWORD r = ((color >> 11) & 0x1F) * 255 / 31;
    const DWORD g = ((color >> 5) & 0x3F) * 255 / 63;
    const DWORD b = (color & 0x1F) * 255 / 31;
    return (r << 16) | (g << 8) | b;
}

// Decode one 4x4 DXT block into blockPixels[16] (ARGB). The block is 8
// bytes for DXT1, 16 bytes (alpha half + color half) for DXT2-5.
static void EmuD3DDecodeDxtBlock(const BYTE* block, DWORD fourCC,
                                 DWORD* blockPixels)
{
    const BYTE* colorBlock = fourCC == EMU_FOURCC_DXT1 ? block : block + 8;

    const WORD color0 = static_cast<WORD>(colorBlock[0] | (colorBlock[1] << 8));
    const WORD color1 = static_cast<WORD>(colorBlock[2] | (colorBlock[3] << 8));
    const DWORD rgb0 = EmuD3DExpand565(color0);
    const DWORD rgb1 = EmuD3DExpand565(color1);
    const DWORD r0 = rgb0 >> 16 & 0xFF, g0 = rgb0 >> 8 & 0xFF, b0 = rgb0 & 0xFF;
    const DWORD r1 = rgb1 >> 16 & 0xFF, g1 = rgb1 >> 8 & 0xFF, b1 = rgb1 & 0xFF;

    DWORD palette[4];
    palette[0] = 0xFF000000u | rgb0;
    palette[1] = 0xFF000000u | rgb1;
    // DXT2-5 color blocks always use 4-color mode; DXT1 selects punch-through
    // alpha when color0 <= color1.
    if(fourCC != EMU_FOURCC_DXT1 || color0 > color1)
    {
        palette[2] = 0xFF000000u | (((r0 * 2 + r1) / 3) << 16) |
                     (((g0 * 2 + g1) / 3) << 8) | ((b0 * 2 + b1) / 3);
        palette[3] = 0xFF000000u | (((r0 + r1 * 2) / 3) << 16) |
                     (((g0 + g1 * 2) / 3) << 8) | ((b0 + b1 * 2) / 3);
    }
    else
    {
        palette[2] = 0xFF000000u | (((r0 + r1) / 2) << 16) |
                     (((g0 + g1) / 2) << 8) | ((b0 + b1) / 2);
        palette[3] = 0x00000000u; // punch-through
    }

    for(unsigned int texel = 0; texel < 16; ++texel)
    {
        const DWORD indexBits = colorBlock[4 + texel / 4];
        blockPixels[texel] = palette[(indexBits >> ((texel % 4) * 2)) & 0x3];
    }

    if(fourCC == EMU_FOURCC_DXT2 || fourCC == EMU_FOURCC_DXT3)
    {
        // Explicit 4-bit alpha, low nibble first.
        for(unsigned int texel = 0; texel < 16; ++texel)
        {
            const BYTE pair = block[texel / 2];
            const DWORD alpha = ((texel & 1) ? (pair >> 4) : (pair & 0xF)) * 17u;
            blockPixels[texel] = (blockPixels[texel] & 0x00FFFFFFu) | (alpha << 24);
        }
    }
    else if(fourCC == EMU_FOURCC_DXT4 || fourCC == EMU_FOURCC_DXT5)
    {
        // Interpolated alpha: two endpoints + 16 3-bit codes.
        const DWORD alpha0 = block[0];
        const DWORD alpha1 = block[1];
        DWORD alphaPalette[8];
        alphaPalette[0] = alpha0;
        alphaPalette[1] = alpha1;
        if(alpha0 > alpha1)
        {
            for(DWORD i = 0; i < 6; ++i)
                alphaPalette[2 + i] = ((6 - i) * alpha0 + (i + 1) * alpha1) / 7;
        }
        else
        {
            for(DWORD i = 0; i < 4; ++i)
                alphaPalette[2 + i] = ((4 - i) * alpha0 + (i + 1) * alpha1) / 5;
            alphaPalette[6] = 0;
            alphaPalette[7] = 255;
        }
        unsigned long long codes = 0;
        for(int byteIndex = 5; byteIndex >= 0; --byteIndex)
            codes = (codes << 8) | block[2 + byteIndex];
        for(unsigned int texel = 0; texel < 16; ++texel)
        {
            const DWORD alpha = alphaPalette[(codes >> (texel * 3)) & 0x7];
            blockPixels[texel] = (blockPixels[texel] & 0x00FFFFFFu) | (alpha << 24);
        }
    }
}

// Decompress a whole DXT level (pitch = bytes per block row) to ARGB.
static void EmuD3DDecodeDxt(const BYTE* bits, INT pitch, DWORD width,
                            DWORD height, DWORD fourCC, std::vector<DWORD>& out)
{
    const DWORD blockBytes = fourCC == EMU_FOURCC_DXT1 ? 8 : 16;
    const DWORD blocksAcross = (width + 3) / 4;
    const DWORD blocksDown = (height + 3) / 4;
    out.assign(static_cast<std::size_t>(width) * height, 0);

    for(DWORD blockY = 0; blockY < blocksDown; ++blockY)
    {
        const BYTE* blockRow = bits + static_cast<std::size_t>(blockY) * pitch;
        for(DWORD blockX = 0; blockX < blocksAcross; ++blockX)
        {
            DWORD blockPixels[16];
            EmuD3DDecodeDxtBlock(
                blockRow + static_cast<std::size_t>(blockX) * blockBytes, fourCC,
                blockPixels);
            for(DWORD y = 0; y < 4 && blockY * 4 + y < height; ++y)
            {
                for(DWORD x = 0; x < 4 && blockX * 4 + x < width; ++x)
                {
                    out[static_cast<std::size_t>(blockY * 4 + y) * width +
                        blockX * 4 + x] = blockPixels[y * 4 + x];
                }
            }
        }
    }
}

// Write one locked surface level as a 32-bit top-down BMP. Uncompressed
// 32/16-bit host formats are expanded directly and DXT1-5 levels are
// decompressed first; anything else is skipped by returning false.
static bool EmuD3DWriteBmp(const char* path, DWORD width, DWORD height,
                           INT pitch, const void* bits, XTL::D3DFORMAT format)
{
    if(bits == NULL || width == 0 || height == 0)
        return false;

    std::vector<DWORD> decoded;
    const DWORD formatValue = static_cast<DWORD>(format);
    if(formatValue == EMU_FOURCC_DXT1 || formatValue == EMU_FOURCC_DXT2 ||
       formatValue == EMU_FOURCC_DXT3 || formatValue == EMU_FOURCC_DXT4 ||
       formatValue == EMU_FOURCC_DXT5)
    {
        EmuD3DDecodeDxt(static_cast<const BYTE*>(bits), pitch, width, height,
                        formatValue, decoded);
        bits = decoded.data();
        pitch = static_cast<INT>(width * 4);
        format = XTL::D3DFMT_A8R8G8B8;
    }
    else if(format != XTL::D3DFMT_A8R8G8B8 && format != XTL::D3DFMT_X8R8G8B8 &&
            format != XTL::D3DFMT_R5G6B5 && format != XTL::D3DFMT_A4R4G4B4)
        return false;

    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if(file == INVALID_HANDLE_VALUE)
        return false;

    const DWORD imageBytes = width * height * 4;
    BITMAPFILEHEADER fileHeader = { 0 };
    BITMAPINFOHEADER infoHeader = { 0 };
    fileHeader.bfType = 0x4D42; // 'BM'
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(infoHeader);
    fileHeader.bfSize = fileHeader.bfOffBits + imageBytes;
    infoHeader.biSize = sizeof(infoHeader);
    infoHeader.biWidth = static_cast<LONG>(width);
    infoHeader.biHeight = -static_cast<LONG>(height); // top-down
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = imageBytes;

    std::vector<DWORD> row(width);
    DWORD written = 0;
    bool ok = WriteFile(file, &fileHeader, sizeof(fileHeader), &written, NULL) != 0 &&
              WriteFile(file, &infoHeader, sizeof(infoHeader), &written, NULL) != 0;
    for(DWORD y = 0; ok && y < height; ++y)
    {
        const BYTE* source = static_cast<const BYTE*>(bits) +
                             static_cast<std::size_t>(y) * pitch;
        if(format == XTL::D3DFMT_A8R8G8B8 || format == XTL::D3DFMT_X8R8G8B8)
        {
            std::memcpy(row.data(), source, width * 4);
        }
        else if(format == XTL::D3DFMT_R5G6B5)
        {
            const WORD* pixels = reinterpret_cast<const WORD*>(source);
            for(DWORD x = 0; x < width; ++x)
            {
                const WORD p = pixels[x];
                const DWORD r = ((p >> 11) & 0x1F) * 255 / 31;
                const DWORD g = ((p >> 5) & 0x3F) * 255 / 63;
                const DWORD b = (p & 0x1F) * 255 / 31;
                row[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }
        else // A4R4G4B4
        {
            const WORD* pixels = reinterpret_cast<const WORD*>(source);
            for(DWORD x = 0; x < width; ++x)
            {
                const WORD p = pixels[x];
                const DWORD a = ((p >> 12) & 0xF) * 17;
                const DWORD r = ((p >> 8) & 0xF) * 17;
                const DWORD g = ((p >> 4) & 0xF) * 17;
                const DWORD b = (p & 0xF) * 17;
                row[x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
        ok = WriteFile(file, row.data(), width * 4, &written, NULL) != 0;
    }

    CloseHandle(file);
    return ok;
}

static bool EmuD3DDumpBackbuffer(const char* path)
{
    XTL::IDirect3DSurface8* backBuffer = nullptr;
    bool dumped = false;
    __try
    {
        if(g_pD3DDevice8 != 0 &&
           SUCCEEDED(g_pD3DDevice8->GetBackBuffer(
               0, XTL::D3DBACKBUFFER_TYPE_MONO, &backBuffer)) &&
           backBuffer != nullptr)
        {
            XTL::D3DSURFACE_DESC description = {
                XTL::D3DFMT_UNKNOWN, XTL::D3DRTYPE_SURFACE, 0, XTL::D3DPOOL_DEFAULT,
                0, XTL::D3DMULTISAMPLE_NONE, 0, 0
            };
            XTL::D3DLOCKED_RECT locked{};
            if(SUCCEEDED(backBuffer->GetDesc(&description)) &&
               SUCCEEDED(backBuffer->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
            {
                dumped = EmuD3DWriteBmp(path, description.Width, description.Height,
                                        locked.Pitch, locked.pBits, description.Format);
                backBuffer->UnlockRect();
            }
            backBuffer->Release();
            backBuffer = nullptr;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        if(backBuffer != nullptr)
        {
            backBuffer->Release();
        }
        dumped = false;
    }
    return dumped;
}

// D3DPERF (PIX-style) event bridge. The host device is d3d8, which has no
// event API of its own; the D3DPERF_* exports live in d3d9.dll and are
// process-wide, so capture tools that honor them (apitrace, DXVK's d3d9
// under a DXVK d3d8 drop-in, PIX-era tools) pick the labels up regardless.
// Without such a tool they are cheap no-ops.
typedef int(WINAPI* EmuD3DPerfBeginEventProc)(DWORD color, LPCWSTR name);
typedef int(WINAPI* EmuD3DPerfEndEventProc)(void);
typedef void(WINAPI* EmuD3DPerfSetMarkerProc)(DWORD color, LPCWSTR name);

static EmuD3DPerfBeginEventProc g_pfnD3DPerfBeginEvent = NULL;
static EmuD3DPerfEndEventProc g_pfnD3DPerfEndEvent = NULL;
static EmuD3DPerfSetMarkerProc g_pfnD3DPerfSetMarker = NULL;
static int g_D3DPerfMarkersEnabled = -1;

static bool EmuD3DPerfMarkersActive(void)
{
    if(g_D3DPerfMarkersEnabled < 0)
    {
        g_D3DPerfMarkersEnabled = 0;
        if(getenv("CXBX_D3D_PERF_MARKERS") != NULL)
        {
            const HMODULE d3d9 = LoadLibraryA("d3d9.dll");
            if(d3d9 != NULL)
            {
                g_pfnD3DPerfBeginEvent = reinterpret_cast<EmuD3DPerfBeginEventProc>(
                    GetProcAddress(d3d9, "D3DPERF_BeginEvent"));
                g_pfnD3DPerfEndEvent = reinterpret_cast<EmuD3DPerfEndEventProc>(
                    GetProcAddress(d3d9, "D3DPERF_EndEvent"));
                g_pfnD3DPerfSetMarker = reinterpret_cast<EmuD3DPerfSetMarkerProc>(
                    GetProcAddress(d3d9, "D3DPERF_SetMarker"));
                if(g_pfnD3DPerfBeginEvent != NULL && g_pfnD3DPerfEndEvent != NULL &&
                   g_pfnD3DPerfSetMarker != NULL)
                {
                    g_D3DPerfMarkersEnabled = 1;
                }
            }
        }
    }
    return g_D3DPerfMarkersEnabled == 1;
}

static void EmuD3DPerfFrameBoundary(void)
{
    if(!EmuD3DPerfMarkersActive())
        return;

    static bool s_FrameOpen = false;
    if(s_FrameOpen)
        g_pfnD3DPerfEndEvent();

    WCHAR name[64];
    swprintf(name, 64, L"Frame %lu", static_cast<unsigned long>(g_D3DDebugFrame));
    g_pfnD3DPerfBeginEvent(0xFF4080FFu, name);
    s_FrameOpen = true;
}

// Called at the top of every guest draw wrapper. Assigns the draw its
// per-frame index, traces/marks it, and returns false when the draw falls
// inside the CXBX_D3D_SKIP_DRAWS bisection range (the caller must skip it).
static bool EmuD3DDrawGate(const char* what, DWORD pcPrimitiveType,
                           DWORD primitiveCount)
{
    static int s_SkipEnabled = -1;
    static DWORD s_SkipBegin = 0, s_SkipEnd = 0;
    static int s_TraceEnabled = -1;

    const DWORD index = g_D3DDebugDrawIndex++;

    if(s_SkipEnabled < 0)
        s_SkipEnabled = EmuD3DParseDrawRange(getenv("CXBX_D3D_SKIP_DRAWS"),
                                             s_SkipBegin, s_SkipEnd)
                            ? 1
                            : 0;
    if(s_TraceEnabled < 0)
        s_TraceEnabled = (getenv("CXBX_D3D_DRAW_TRACE") != NULL) ? 1 : 0;

    const bool skipped =
        s_SkipEnabled == 1 && index >= s_SkipBegin && index < s_SkipEnd;

    if(s_TraceEnabled == 1)
    {
        DWORD vertexShader = 0;
        if(g_pD3DDevice8 != 0)
            g_pD3DDevice8->GetVertexShader(&vertexShader);
        printf("DRAW| frame=%lu draw=%lu %s prim=%lu count=%lu vsh=0x%08lX "
               "psh=0x%08lX%s\n",
               static_cast<unsigned long>(g_D3DDebugFrame),
               static_cast<unsigned long>(index), what,
               static_cast<unsigned long>(pcPrimitiveType),
               static_cast<unsigned long>(primitiveCount),
               static_cast<unsigned long>(vertexShader),
               static_cast<unsigned long>(g_EmuCurrentPixelShaderHandle),
               skipped ? " SKIPPED" : "");
    }

    if(!skipped && EmuD3DPerfMarkersActive())
    {
        WCHAR marker[128];
        swprintf(marker, 128, L"%S draw=%lu prim=%lu count=%lu", what,
                 static_cast<unsigned long>(index),
                 static_cast<unsigned long>(pcPrimitiveType),
                 static_cast<unsigned long>(primitiveCount));
        g_pfnD3DPerfSetMarker(0xFF00FF00u, marker);
    }

    return !skipped;
}

// Called after a guest draw was submitted to the host device; dumps the
// backbuffer when the draw falls inside the CXBX_D3D_DUMP_DRAWS range.
static void EmuD3DDrawPost(void)
{
    static int s_DumpEnabled = -1;
    static DWORD s_DumpBegin = 0, s_DumpEnd = 0;
    static bool s_FrameRangeParsed = false;
    static bool s_FrameRangeEnabled = false;
    static DWORD s_FrameBegin = 0, s_FrameEnd = 0;
    static LONG s_DumpsRemaining = 64;

    if(s_DumpEnabled < 0)
        s_DumpEnabled = EmuD3DParseDrawRange(getenv("CXBX_D3D_DUMP_DRAWS"),
                                             s_DumpBegin, s_DumpEnd)
                            ? 1
                            : 0;
    if(s_DumpEnabled != 1)
        return;

    if(!s_FrameRangeParsed)
    {
        s_FrameRangeEnabled = EmuD3DParseDrawRange(
            getenv("CXBX_D3D_DUMP_FRAMES"), s_FrameBegin, s_FrameEnd);
        s_FrameRangeParsed = true;
    }
    if(s_FrameRangeEnabled &&
       (g_D3DDebugFrame < s_FrameBegin || g_D3DDebugFrame >= s_FrameEnd))
        return;

    const DWORD index = g_D3DDebugDrawIndex != 0 ? g_D3DDebugDrawIndex - 1 : 0;
    if(index < s_DumpBegin || index >= s_DumpEnd || s_DumpsRemaining <= 0)
        return;

    s_DumpsRemaining--;

    char fileName[64];
    snprintf(fileName, sizeof(fileName), "f%05lu_d%04lu.bmp",
             static_cast<unsigned long>(g_D3DDebugFrame),
             static_cast<unsigned long>(index));
    char path[MAX_PATH * 2];
    EmuD3DDebugDumpPath(path, sizeof(path), "cxbx_draw", fileName);
    if(EmuD3DDumpBackbuffer(path))
        printf("DRAW| dumped %s\n", path);
    else
        printf("DRAW| backbuffer dump failed (%s)\n", path);
}

// Combiner programs the ps.1.1 translator compiled to real host shaders,
// keyed by host handle; their constants are applied at SetPixelShader.
struct EmuTranslatedPixelShader
{
    DWORD handle;
    std::vector<cxbx::d3d::PixelShaderConstant> constants;
};

static std::vector<EmuTranslatedPixelShader> g_EmuTranslatedPixelShaders;

static HRESULT EmuHostCreatePixelShader(const DWORD *pTokens, DWORD *pHandle)
{
    __try
    {
        return g_pD3DDevice8->CreatePixelShader(pTokens, pHandle);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return D3DERR_INVALIDCALL;
    }
}

static void EmuApplyTranslatedPixelShaderConstants(DWORD Handle)
{
    for(const auto& shader : g_EmuTranslatedPixelShaders)
    {
        if(shader.handle != Handle)
            continue;
        for(const auto& constant : shader.constants)
            g_pD3DDevice8->SetPixelShaderConstant(constant.index, constant.value, 1);
        return;
    }
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreatePixelShader
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreatePixelShader
(
    CONST DWORD    *pFunction,
    DWORD          *pHandle
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("CreatePixelShader");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreatePixelShader\n"
               "(\n"
               "   pFunction           : 0x%.08X\n"
               "   pHandle             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pFunction, pHandle);
    }
    #endif

    // ******************************************************************
    // * translate, else approximate
    // ******************************************************************
    // pFunction is an Xbox X_D3DPIXELSHADERDEF (NV2A register-combiner state),
    // not PC ps.1.x bytecode. Combiner programs the conservative translator
    // can express exactly become real host ps.1.1 shaders; everything else
    // gets a fallback marker handle, which SetPixelShader routes to the
    // fixed-function modulate approximation of the bound texture(s) with the
    // vertex-lit diffuse.
    static DWORD s_PixelShaderFallbackCounter = 0;

    cxbx::d3d::XboxPixelShaderDefinition definition{};
    SIZE_T bytesRead = 0;
    const bool definitionReadable =
        ReadProcessMemory(GetCurrentProcess(), pFunction, definition.data(),
                          sizeof(definition), &bytesRead) &&
        bytesRead == sizeof(definition);

    static int s_PshTranslateEnabled = -1;
    if(s_PshTranslateEnabled < 0)
    {
        const char *value = getenv("CXBX_PSH_TRANSLATE");
        s_PshTranslateEnabled = (value == NULL || *value != '0') ? 1 : 0;
    }

    const char *failureReason = "translate_disabled";
    if(definitionReadable && s_PshTranslateEnabled == 1)
    {
        cxbx::d3d::PixelShaderTranslation translation =
            cxbx::d3d::TranslatePixelShader(definition);
        if(translation.ok())
        {
            if(SUCCEEDED(EmuHostCreatePixelShader(
                   reinterpret_cast<const DWORD *>(translation.bytecode.data()),
                   pHandle)))
            {
                const DWORD hash =
                    EmuD3DHashBytes(definition.data(), sizeof(definition));
                printf("PSH| translated handle=0x%08lX hash=0x%08lX combiners=%lu "
                       "arith=%u tex=%u const=%u\n",
                       static_cast<unsigned long>(*pHandle),
                       static_cast<unsigned long>(hash),
                       static_cast<unsigned long>(definition[53] & 0x0Fu),
                       translation.arithmetic, translation.textures,
                       static_cast<unsigned>(translation.constants.size()));

                g_EmuTranslatedPixelShaders.push_back(
                    {*pHandle, std::move(translation.constants)});

                EmuSwapFS();   // XBox FS
                return D3D_OK;
            }
            failureReason = "host_rejected_bytecode";
        }
        else
        {
            failureReason = translation.failure;
        }
    }

    HRESULT hRet = D3D_OK;
    {
        *pHandle = X_PIXELSHADER_FALLBACK_MARKER | (++s_PixelShaderFallbackCounter & 0x00FFFFFF);
        if(definitionReadable)
        {
            g_EmuPixelShaderFallbacks.push_back({*pHandle, definition});

            // The combiner was approximated, not translated -- always say
            // which approximation each shader got and why translation bailed,
            // so a wrong material is attributable without a rebuild.
            const DWORD hash = EmuD3DHashBytes(definition.data(), sizeof(definition));
            const cxbx::d3d::PixelShaderFallback fallback =
                cxbx::d3d::ClassifyPixelShaderFallback(definition);
            printf("PSH| fallback_create handle=0x%08lX hash=0x%08lX combiners=%lu class=%s reason=%s\n",
                   static_cast<unsigned long>(*pHandle),
                   static_cast<unsigned long>(hash),
                   static_cast<unsigned long>(definition[53] & 0x0Fu),
                   cxbx::d3d::PixelShaderFallbackName(fallback),
                   failureReason);

            static int s_PshDumpEnabled = -1;
            if(s_PshDumpEnabled < 0)
                s_PshDumpEnabled = (getenv("CXBX_PSH_DUMP") != NULL) ? 1 : 0;
            if(s_PshDumpEnabled == 1)
            {
                for(std::size_t word = 0;
                    word < cxbx::d3d::XboxPixelShaderDefinitionWords; word += 6)
                {
                    printf("PSH|   def[%02u]", static_cast<unsigned int>(word));
                    for(std::size_t column = 0;
                        column < 6 &&
                        word + column < cxbx::d3d::XboxPixelShaderDefinitionWords;
                        ++column)
                    {
                        printf(" %08lX",
                               static_cast<unsigned long>(definition[word + column]));
                    }
                    printf("\n");
                }
            }
        }
        else
        {
            printf("PSH| fallback_create handle=0x%08lX definition unreadable (0x%08lX)\n",
                   static_cast<unsigned long>(*pHandle),
                   reinterpret_cast<unsigned long>(pFunction));
        }
        hRet = D3D_OK;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetPixelShader
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetPixelShader
(
    DWORD           Handle
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetPixelShader");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetPixelShader\n"
               "(\n"
               "   Handle             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Handle);
    }
    #endif

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    // A fallback-marker handle (or any handle the host rejects) is an untranslated
    // Xbox register-combiner shader: drive the fixed-function pipeline instead so
    // the bound textures are sampled and lit. Record it so the draw path keeps the
    // modulate stages asserted even if the title later touches texture state.
    g_EmuCurrentPixelShaderHandle = Handle;
    if(Handle == 0)
    {
        g_bUsePixelShaderFallback = false;
        g_pD3DDevice8->SetPixelShader(0);
    }
    else if((Handle & X_PIXELSHADER_FALLBACK_MASK) == X_PIXELSHADER_FALLBACK_MARKER ||
            FAILED(g_pD3DDevice8->SetPixelShader(Handle)))
    {
        g_bUsePixelShaderFallback = true;

        // Once per handle: this shader is now driving the fixed-function
        // approximation (either a fallback marker, or a host-created shader
        // the host device refused to bind).
        static std::vector<DWORD> s_LoggedFallbackHandles;
        if(std::find(s_LoggedFallbackHandles.begin(), s_LoggedFallbackHandles.end(),
                     Handle) == s_LoggedFallbackHandles.end())
        {
            s_LoggedFallbackHandles.push_back(Handle);
            printf("PSH| fallback_active handle=0x%08lX class=%s\n",
                   static_cast<unsigned long>(Handle),
                   cxbx::d3d::PixelShaderFallbackName(EmuCurrentPixelShaderFallback()));
        }

        EmuApplyPixelShaderFallback(EmuCurrentPixelShaderFallback(), false);
    }
    else
    {
        g_bUsePixelShaderFallback = false;
        // Translated combiner programs carry their baked stage constants.
        EmuApplyTranslatedPixelShaderConstants(Handle);
    }

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetPixelShaderConstant
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetPixelShaderConstant
(
    DWORD       Register,
    CONST PVOID pConstantData,
    DWORD       ConstantCount
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetPixelShaderConstant");

    #ifdef _DEBUG_TRACE
    printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetPixelShaderConstant\n"
           "(\n"
           "   Register            : 0x%.08X\n"
           "   pConstantData       : 0x%.08X\n"
           "   ConstantCount       : 0x%.08X\n"
           ");\n",
           GetCurrentThreadId(), Register, pConstantData, ConstantCount);
    #endif

    HRESULT Result = g_pD3DDevice8->SetPixelShaderConstant(Register, pConstantData, ConstantCount);

    EmuSwapFS();   // Xbox FS

    return Result;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTextureState_BorderColor
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetTextureState_BorderColor
(
    DWORD Stage,
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetTextureState_BorderColor");

    HRESULT Result = g_pD3DDevice8->SetTextureStageState(Stage, D3DTSS_BORDERCOLOR, Value);

    EmuSwapFS();   // Xbox FS

    return Result;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTextureState_BumpEnv
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetTextureState_BumpEnv
(
    DWORD Stage,
    D3DTEXTURESTAGESTATETYPE Type,
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetTextureState_BumpEnv");

    if(Stage < 8 &&
       ((Type >= D3DTSS_BUMPENVMAT00 && Type <= D3DTSS_BUMPENVMAT11) ||
        Type == D3DTSS_BUMPENVLSCALE || Type == D3DTSS_BUMPENVLOFFSET))
    {
        g_pD3DDevice8->SetTextureStageState(Stage, Type, Value);
    }
    else
    {
        EmuWarning("SetTextureState_BumpEnv ignored invalid stage/type (%lu, 0x%.08lX)",
                   Stage,
                   Type);
    }

    EmuSwapFS();   // Xbox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BeginVisibilityTest
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_BeginVisibilityTest()
{
    D3D_TRACE("BeginVisibilityTest");
    // Direct3D 8 on Windows has no occlusion-query equivalent. The matching
    // result wrapper reports a conservative visible result when implemented.
}

HRESULT WINAPI XTL::EmuIDirect3DDevice8_EndVisibilityTest(DWORD Index)
{
    D3D_TRACE("EndVisibilityTest");
    (void)Index;
    return D3D_OK;
}

HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetVisibilityTestResult
(
    DWORD      Index,
    UINT      *pResult,
    ULONGLONG *pTimeStamp
)
{
    D3D_TRACE("GetVisibilityTestResult");
    (void)Index;

    if(pResult != NULL)
        *pResult = 1;
    if(pTimeStamp != NULL)
        *pTimeStamp = 0;

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_DeletePixelShader
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_DeletePixelShader(DWORD Handle)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("DeletePixelShader");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_DeletePixelShader"
               "(Handle = 0x%.08X);\n", GetCurrentThreadId(), Handle);
    }
    #endif

    // Xbox register-combiner definitions that the host cannot compile are
    // represented by marker handles. They own no host shader. Zero and repeat
    // deletion are likewise harmless at the Xbox API boundary.
    if(Handle != 0 &&
       (Handle & X_PIXELSHADER_FALLBACK_MASK) != X_PIXELSHADER_FALLBACK_MARKER)
    {
        HRESULT hRet = g_pD3DDevice8->DeletePixelShader(Handle);
        if(FAILED(hRet))
        {
            EmuWarning("DeletePixelShader failed for handle 0x%.08X", Handle);
        }

        g_EmuTranslatedPixelShaders.erase(
            std::remove_if(g_EmuTranslatedPixelShaders.begin(),
                           g_EmuTranslatedPixelShaders.end(),
                           [Handle](const EmuTranslatedPixelShader& shader) {
                               return shader.handle == Handle;
                           }),
            g_EmuTranslatedPixelShaders.end());
    }
    else if((Handle & X_PIXELSHADER_FALLBACK_MASK) == X_PIXELSHADER_FALLBACK_MARKER)
    {
        g_EmuPixelShaderFallbacks.erase(
            std::remove_if(g_EmuPixelShaderFallbacks.begin(),
                           g_EmuPixelShaderFallbacks.end(),
                           [Handle](const EmuPixelShaderFallbackDefinition& fallback) {
                               return fallback.handle == Handle;
                           }),
            g_EmuPixelShaderFallbacks.end());
    }

    EmuSwapFS();   // XBox FS
}

// ******************************************************************
// * Swizzled-texture write-through
// ******************************************************************
// Xbox SWIZZLED texture formats store texels in Morton order; titles upload
// pre-swizzled assets through LockRect and the hardware samples them
// swizzled. The host texture is linear, so the Morton-order bytes must be
// unswizzled before the host samples them. Track every swizzled-format
// texture created through the D3D wrappers; a writable LockRect marks the
// level dirty, and the pending block is unswizzled in place right before the
// host lock is released (SetTexture's flush, or a re-lock). The Register
// path has its own unswizzle and does not use this.
struct EmuSwizzledTextureLevelLock
{
    void *pBits;
    INT Pitch;
};

#define EMU_SWIZZLED_TEXTURE_MAX_LEVELS 16

struct EmuSwizzledTextureInfo
{
    XTL::IDirect3DBaseTexture8 *pHostTexture;
    DWORD dwBPP;
    DWORD dwDirtyLevels; // bitmask of levels with pending Morton-order writes
    EmuSwizzledTextureLevelLock Levels[EMU_SWIZZLED_TEXTURE_MAX_LEVELS];
};

static std::vector<EmuSwizzledTextureInfo> g_EmuSwizzledTextures;

static bool EmuXboxFormatIsSwizzled(DWORD XboxFormat, DWORD *pdwBPP)
{
    switch(XboxFormat)
    {
        case 0x06: /* A8R8G8B8 */
        case 0x07: /* X8R8G8B8 */
            *pdwBPP = 4;
            return true;
        case 0x04: /* A4R4G4B4 */
        case 0x05: /* R5G6B5 */
        case 0x28: /* G8B8 */
        case 0x1A: /* A8L8 */
            *pdwBPP = 2;
            return true;
        case 0x19: /* A8 */
            *pdwBPP = 1;
            return true;
    }
    return false;
}

static EmuSwizzledTextureInfo *EmuFindSwizzledTexture(
    XTL::IDirect3DBaseTexture8 *pHostTexture)
{
    if(pHostTexture == nullptr)
        return nullptr;
    for(auto &info : g_EmuSwizzledTextures)
    {
        if(info.pHostTexture == pHostTexture)
            return &info;
    }
    return nullptr;
}

static void EmuDiscardSwizzledTexture(XTL::IDirect3DBaseTexture8 *pHostTexture)
{
    for(auto it = g_EmuSwizzledTextures.begin(); it != g_EmuSwizzledTextures.end(); ++it)
    {
        if(it->pHostTexture == pHostTexture)
        {
            g_EmuSwizzledTextures.erase(it);
            return;
        }
    }
}

// Unswizzle every dirty level in place, while the host lock that received the
// title's Morton-order write is still held.
static void EmuCommitSwizzledTexture(XTL::IDirect3DBaseTexture8 *pBaseTexture)
{
    EmuSwizzledTextureInfo *info = EmuFindSwizzledTexture(pBaseTexture);
    if(info == nullptr || info->dwDirtyLevels == 0)
        return;

    auto *texture = static_cast<XTL::IDirect3DTexture8 *>(info->pHostTexture);
    for(DWORD level = 0; level < EMU_SWIZZLED_TEXTURE_MAX_LEVELS; ++level)
    {
        if((info->dwDirtyLevels & (1u << level)) == 0)
            continue;

        const EmuSwizzledTextureLevelLock &lock = info->Levels[level];
        XTL::D3DSURFACE_DESC description;
        if(lock.pBits == nullptr || FAILED(texture->GetLevelDesc(level, &description)))
            continue;

        const DWORD width = description.Width;
        const DWORD height = description.Height;
        const DWORD rowBytes = width * info->dwBPP;
        if(rowBytes == 0 || height == 0)
            continue;

        // The title wrote the contiguous Morton block through the row
        // pointers the host lock reports; gather it back before unswizzling
        // over the same memory.
        std::vector<BYTE> morton(static_cast<std::size_t>(rowBytes) * height);
        for(DWORD y = 0; y < height; ++y)
        {
            std::memcpy(&morton[static_cast<std::size_t>(y) * rowBytes],
                        static_cast<const BYTE *>(lock.pBits) +
                            static_cast<std::size_t>(y) * lock.Pitch,
                        rowBytes);
        }

        RECT sourceRect = {0, 0, 0, 0};
        POINT destinationPoint = {0, 0};
        XTL::EmuXGUnswizzleRect(morton.data(), width, height, 1, lock.pBits,
                                lock.Pitch, sourceRect, destinationPoint,
                                info->dwBPP);
    }

    info->dwDirtyLevels = 0;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateTexture2
// ******************************************************************
XTL::X_D3DResource * WINAPI XTL::EmuIDirect3DDevice8_CreateTexture2
(
    UINT                Width,
    UINT                Height,
    UINT                Depth,
    UINT                Levels,
    DWORD               Usage,
    D3DFORMAT           Format,
    D3DRESOURCETYPE     D3DResource
)
{
    D3D_TRACE("CreateTexture2");
    X_D3DTexture *pTexture;

    EmuIDirect3DDevice8_CreateTexture(Width, Height, Levels, Usage, Format, D3DPOOL_MANAGED, &pTexture);

    return pTexture;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateTexture
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateTexture
(
    UINT            Width,
    UINT            Height,
    UINT            Levels,
    DWORD           Usage,
    D3DFORMAT       Format,
    D3DPOOL         Pool,
    X_D3DTexture  **ppTexture
)
{
    D3D_TRACE("CreateTexture");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateTexture\n"
               "(\n"
               "   Width               : 0x%.08X\n"
               "   Height              : 0x%.08X\n"
               "   Levels              : 0x%.08X\n"
               "   Usage               : 0x%.08X\n"
               "   Format              : 0x%.08X\n"
               "   Pool                : 0x%.08X\n"
               "   ppTexture           : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Width, Height, Levels, Usage, Format, Pool, ppTexture);
    }
    #endif

    // Convert Format (Xbox->PC)
    D3DFORMAT PCFormat = EmuXB2PC_D3DFormat(Format);

    // TODO: HACK: Devices that don't support this should somehow emulate it!
    if(PCFormat == D3DFMT_D16)
    {
        printf("*Warning* D3DFMT_16 is an unsupported texture format!\n");
        PCFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCFormat == D3DFMT_P8)
    {
        printf("*Warning* D3DFMT_P8 is an unsupported texture format!\n");
        PCFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCFormat == D3DFMT_D24S8)
    {
        printf("*Warning* D3DFMT_D24S8 is an unsupported texture format!\n");
        PCFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCFormat == D3DFMT_YUY2)
    {
        // cache the overlay size
        g_dwOverlayW = Width;
        g_dwOverlayH = Height;
    }

    HRESULT hRet;

    if(PCFormat != D3DFMT_YUY2 || g_bSupportsYUY2)
    {
        EmuAdjustPower2(&Width, &Height);

        *ppTexture = new X_D3DTexture();

        // ******************************************************************
        // * redirect to windows d3d
        // ******************************************************************
        __try
        {
            hRet = g_pD3DDevice8->CreateTexture
            (
                Width, Height, Levels,
                0,
                PCFormat, D3DPOOL_MANAGED, &((*ppTexture)->EmuTexture8)
            );
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            hRet = D3DERR_INVALIDCALL;
        }

        if(FAILED(hRet))
            printf("*Warning* CreateTexture FAILED\n");

        DWORD dwSwizzledBPP = 0;
        if(SUCCEEDED(hRet) && (*ppTexture)->EmuTexture8 != NULL &&
           EmuXboxFormatIsSwizzled(static_cast<DWORD>(Format), &dwSwizzledBPP))
        {
            g_EmuSwizzledTextures.push_back(
                {(*ppTexture)->EmuTexture8, dwSwizzledBPP, 0, {}});
        }
    }
    else
    {
        // If YUY2 is not supported in hardware, we'll actually mark this as a special fake texture (set highest bit)
        *ppTexture = EmuCreateYuy2Texture(Width, Height);
        hRet = (*ppTexture != NULL) ? D3D_OK : E_OUTOFMEMORY;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateVolumeTexture
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateVolumeTexture
(
    UINT                 Width,
    UINT                 Height,
    UINT                 Depth,
    UINT                 Levels,
    DWORD                Usage,
    D3DFORMAT            Format,
    D3DPOOL              Pool,
    X_D3DVolumeTexture **ppVolumeTexture
)
{
    D3D_TRACE("CreateVolumeTexture");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateVolumeTexture\n"
               "(\n"
               "   Width               : 0x%.08X\n"
               "   Height              : 0x%.08X\n"
               "   Depth               : 0x%.08X\n"
               "   Levels              : 0x%.08X\n"
               "   Usage               : 0x%.08X\n"
               "   Format              : 0x%.08X\n"
               "   Pool                : 0x%.08X\n"
               "   ppVolumeTexture     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture);
    }
    #endif

    // Convert Format (Xbox->PC)
    D3DFORMAT PCFormat = EmuXB2PC_D3DFormat(Format);

    // TODO: HACK: Devices that don't support this should somehow emulate it!
    if(PCFormat == D3DFMT_D16)
    {
        printf("*Warning* D3DFMT_16 is an unsupported texture format!\n");
        PCFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCFormat == D3DFMT_P8)
    {
        printf("*Warning* D3DFMT_P8 is an unsupported texture format!\n");
        PCFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCFormat == D3DFMT_D24S8)
    {
        printf("*Warning* D3DFMT_D24S8 is an unsupported texture format!\n");
        PCFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCFormat == D3DFMT_YUY2)
    {
        // cache the overlay size
        g_dwOverlayW = Width;
        g_dwOverlayH = Height;
    }

    HRESULT hRet;

    if(PCFormat != D3DFMT_YUY2 || g_bSupportsYUY2)
    {
        EmuAdjustPower2(&Width, &Height);

        *ppVolumeTexture = new X_D3DVolumeTexture();

        // ******************************************************************
        // * redirect to windows d3d
        // ******************************************************************
        hRet = g_pD3DDevice8->CreateVolumeTexture
        (
            Width, Height, Depth, Levels, 
            0,  // TODO: Xbox Allows a border to be drawn (maybe hack this in software ;[)
            PCFormat, D3DPOOL_MANAGED, &((*ppVolumeTexture)->EmuVolumeTexture8)
        );

        if(FAILED(hRet))
            printf("*Warning* CreateVolumeTexture FAILED\n");
    }
    else
    {
        // If YUY2 is not supported in hardware, we'll actually mark this as a special fake texture (set highest bit)
        *ppVolumeTexture = (X_D3DVolumeTexture*)((uint32)(new uint08[g_dwOverlayW*g_dwOverlayH*2]) | 0x80000000);

        hRet = D3D_OK;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateCubeTexture
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateCubeTexture
(
    UINT                 EdgeLength,
    UINT                 Levels,
    DWORD                Usage,
    D3DFORMAT            Format,
    D3DPOOL              Pool,
    X_D3DCubeTexture  **ppCubeTexture
)
{
    D3D_TRACE("CreateCubeTexture");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateCubeTexture\n"
               "(\n"
               "   EdgeLength          : 0x%.08X\n"
               "   Levels              : 0x%.08X\n"
               "   Usage               : 0x%.08X\n"
               "   Format              : 0x%.08X\n"
               "   Pool                : 0x%.08X\n"
               "   ppCubeTexture       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture);
    }
    #endif

    // Convert Format (Xbox->PC)
    D3DFORMAT PCFormat = EmuXB2PC_D3DFormat(Format);

    // TODO: HACK: Devices that don't support this should somehow emulate it!
    if(PCFormat == D3DFMT_D16)
    {
        printf("*Warning* D3DFMT_16 is an unsupported texture format!\n");
        PCFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCFormat == D3DFMT_P8)
    {
        printf("*Warning* D3DFMT_P8 is an unsupported texture format!\n");
        PCFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCFormat == D3DFMT_D24S8)
    {
        printf("*Warning* D3DFMT_D24S8 is an unsupported texture format!\n");
        PCFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCFormat == D3DFMT_YUY2)
    {
        EmuCleanup("YUV not supported for cube textures");
    }

    *ppCubeTexture = new X_D3DCubeTexture();

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    HRESULT hRet = g_pD3DDevice8->CreateCubeTexture
    (
        EdgeLength, Levels, 
        0,  // TODO: Xbox Allows a border to be drawn (maybe hack this in software ;[)
        PCFormat, D3DPOOL_MANAGED, &((*ppCubeTexture)->EmuCubeTexture8)
    );

    if(FAILED(hRet))
        printf("*Warning* CreateCubeTexture FAILED\n");

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateIndexBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateIndexBuffer
(
    UINT                 Length,
    DWORD                Usage,
    D3DFORMAT            Format,
    D3DPOOL              Pool,
    X_D3DIndexBuffer   **ppIndexBuffer
)
{
    D3D_TRACE("CreateIndexBuffer");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateIndexBuffer\n"
               "(\n"
               "   Length              : 0x%.08X\n"
               "   Usage               : 0x%.08X\n"
               "   Format              : 0x%.08X\n"
               "   Pool                : 0x%.08X\n"
               "   ppIndexBuffer       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Length, Usage, Format, Pool, ppIndexBuffer);
    }
    #endif

    *ppIndexBuffer = new X_D3DIndexBuffer();

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    HRESULT hRet = g_pD3DDevice8->CreateIndexBuffer
    (
        Length, D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_MANAGED, &((*ppIndexBuffer)->EmuIndexBuffer8)
    );

    if(FAILED(hRet))
        printf("*Warning* CreateIndexBuffer FAILED\n");

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetIndices
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetIndices
(
    X_D3DIndexBuffer   *pIndexData,
    UINT                BaseVertexIndex
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetIndices");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetIndices\n"
               "(\n"
               "   pIndexData          : 0x%.08X\n"
               "   BaseVertexIndex     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pIndexData, BaseVertexIndex);
    }
#endif

    IDirect3DIndexBuffer8* pIndexBuffer = 0;

    if(pIndexData != 0)
    {
        EmuVerifyResourceIsRegistered(pIndexData);

        pIndexBuffer = pIndexData->EmuIndexBuffer8;
    }
    g_EmuVshCpuIndexBuffer = pIndexData;
    g_EmuVshCpuBaseVertexIndex = BaseVertexIndex;

    HRESULT hRet = D3D_OK;
    __try
    {
        hRet = g_pD3DDevice8->SetIndices(pIndexBuffer, BaseVertexIndex);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        hRet = D3D_OK;
    }

    EmuSwapFS(); // XBox FS

    return hRet;
}

// A title's YUV video (NestopiaX's skin/NES output) is created as a D3DFMT_YUY2
// texture. With no hardware YUY2 overlay on a modern host, CreateTexture hands
// back a "fake" texture -- a raw YUY2 memory block with the high pointer bit set
// -- which SetTexture cannot bind, so the video showed as garbage. Convert that
// YUY2 block to a real RGB texture on the host and bind that instead, doing the
// YUV->RGB the Xbox YuvEnable render state would have done in hardware.
static XTL::IDirect3DTexture8 *g_pYuvConvertTexture = NULL;
static DWORD g_dwYuvConvertW = 0, g_dwYuvConvertH = 0;
static const EmuYuy2TextureInfo *g_pYuvConvertedSource = NULL;

static XTL::IDirect3DTexture8 *EmuConvertYuy2Texture(EmuYuy2TextureInfo *pInfo)
{
    if(pInfo == NULL)
        return NULL;

    DWORD w = pInfo->Width & ~1u, h = pInfo->Height; // YUY2 packs two pixels per unit
    if(w == 0 || h == 0)
        return NULL;

    if(g_pYuvConvertTexture == NULL || g_dwYuvConvertW != w || g_dwYuvConvertH != h)
    {
        if(g_pYuvConvertTexture != NULL)
            g_pYuvConvertTexture->Release();
        g_pYuvConvertTexture = NULL;
        if(FAILED(g_pD3DDevice8->CreateTexture(w, h, 1, 0, XTL::D3DFMT_A8R8G8B8,
                                               XTL::D3DPOOL_MANAGED, &g_pYuvConvertTexture)))
            return NULL;
        g_dwYuvConvertW = w; g_dwYuvConvertH = h;
        g_pYuvConvertedSource = NULL;
    }

    DWORD hash = 2166136261u;
    for(DWORD offset = 0; offset < pInfo->DataSize; ++offset)
    {
        const BYTE value = pInfo->pPixels[offset];
        hash = (hash ^ value) * 16777619u;
    }

    if(pInfo->HashValid && pInfo->ContentHash == hash && g_pYuvConvertedSource == pInfo)
        return g_pYuvConvertTexture;

    XTL::D3DLOCKED_RECT lr;
    if(FAILED(g_pYuvConvertTexture->LockRect(0, &lr, NULL, 0)))
        return NULL;

    const bool converted = CxbxVideo::ConvertYuy2ToBgra(
        pInfo->pPixels, pInfo->Pitch, static_cast<uint08*>(lr.pBits),
        static_cast<size_t>(lr.Pitch), w, h);

    static int s_YuvTraceEnabled = -1;
    static DWORD s_YuvTraceSequence = 0;
    if(s_YuvTraceEnabled < 0)
    {
        s_YuvTraceEnabled = getenv("CXBX_YUV_TRACE") != NULL ? 1 : 0;
    }
    if(s_YuvTraceEnabled == 1)
    {
        BYTE minimum = 0xFF;
        BYTE maximum = 0;
        for(DWORD offset = 0; offset < pInfo->DataSize; ++offset)
        {
            minimum = (std::min)(minimum, pInfo->pPixels[offset]);
            maximum = (std::max)(maximum, pInfo->pPixels[offset]);
        }

        const DWORD sequence = ++s_YuvTraceSequence;
        if(sequence <= 8 || sequence % 60 == 0)
        {
            printf("YUV| convert seq=%lu handle=0x%.08lX size=%lux%lu pitch=%lu "
                   "range=%u-%u hash=0x%.08lX converted=%u\n",
                   static_cast<unsigned long>(sequence),
                   reinterpret_cast<unsigned long>(pInfo->pHandle),
                   static_cast<unsigned long>(pInfo->Width),
                   static_cast<unsigned long>(pInfo->Height),
                   static_cast<unsigned long>(pInfo->Pitch),
                   static_cast<unsigned>(minimum),
                   static_cast<unsigned>(maximum),
                   static_cast<unsigned long>(hash),
                   converted ? 1u : 0u);
        }
    }

    g_pYuvConvertTexture->UnlockRect(0);
    if(converted)
    {
        pInfo->ContentHash = hash;
        pInfo->HashValid = true;
        g_pYuvConvertedSource = pInfo;
    }
    return converted ? g_pYuvConvertTexture : NULL;
}

static void EmuFlushHostTextureLocks(XTL::IDirect3DBaseTexture8* baseTexture)
{
    if(baseTexture == nullptr)
    {
        return;
    }

    // Unswizzle pending Morton-order writes while their lock still holds.
    EmuCommitSwizzledTexture(baseTexture);

    constexpr DWORD maximumMipLevels = 32;
    __try
    {
        switch(baseTexture->GetType())
        {
            case XTL::D3DRTYPE_TEXTURE:
            {
                auto* texture = static_cast<XTL::IDirect3DTexture8*>(baseTexture);
                const DWORD levelCount = (std::min)(texture->GetLevelCount(), maximumMipLevels);
                for(DWORD level = 0; level < levelCount; ++level)
                {
                    texture->UnlockRect(level);
                }
                break;
            }
            case XTL::D3DRTYPE_CUBETEXTURE:
            {
                auto* texture = static_cast<XTL::IDirect3DCubeTexture8*>(baseTexture);
                const DWORD levelCount = (std::min)(texture->GetLevelCount(), maximumMipLevels);
                for(DWORD face = 0; face < 6; ++face)
                {
                    for(DWORD level = 0; level < levelCount; ++level)
                    {
                        texture->UnlockRect(static_cast<XTL::D3DCUBEMAP_FACES>(face), level);
                    }
                }
                break;
            }
            case XTL::D3DRTYPE_VOLUMETEXTURE:
            {
                auto* texture = static_cast<XTL::IDirect3DVolumeTexture8*>(baseTexture);
                const DWORD levelCount = (std::min)(texture->GetLevelCount(), maximumMipLevels);
                for(DWORD level = 0; level < levelCount; ++level)
                {
                    texture->UnlockBox(level);
                }
                break;
            }
            default:
                break;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static bool EmuHostTextureHasData(XTL::IDirect3DBaseTexture8* baseTexture)
{
    if(baseTexture == nullptr || baseTexture->GetType() != XTL::D3DRTYPE_TEXTURE)
    {
        return false;
    }

    auto* texture = static_cast<XTL::IDirect3DTexture8*>(baseTexture);
    XTL::D3DSURFACE_DESC description = {
        XTL::D3DFMT_UNKNOWN, XTL::D3DRTYPE_TEXTURE, 0,
        XTL::D3DPOOL_DEFAULT, 0, XTL::D3DMULTISAMPLE_NONE, 0, 0
    };
    XTL::D3DLOCKED_RECT locked = {};
    bool lockedTexture = false;
    bool hasData = false;
    __try
    {
        if(SUCCEEDED(texture->GetLevelDesc(0, &description)) && description.Height != 0 &&
           SUCCEEDED(texture->LockRect(0, &locked, nullptr, D3DLOCK_READONLY)) &&
           locked.pBits != nullptr && locked.Pitch != 0)
        {
            lockedTexture = true;
            const std::size_t pitch = static_cast<std::size_t>(
                locked.Pitch < 0 ? -static_cast<std::int64_t>(locked.Pitch) : locked.Pitch);
            const bool compressed = description.Format == XTL::D3DFMT_DXT1 ||
                                    description.Format == XTL::D3DFMT_DXT2 ||
                                    description.Format == XTL::D3DFMT_DXT3 ||
                                    description.Format == XTL::D3DFMT_DXT4 ||
                                    description.Format == XTL::D3DFMT_DXT5;
            const std::size_t rowCount = compressed ? (description.Height + 3u) / 4u
                                                    : description.Height;
            const std::size_t sampleCount = (std::min<std::size_t>)(rowCount, 32);
            const std::size_t bytesPerSample = (std::min<std::size_t>)(pitch, 256);
            const auto* bytes = static_cast<const BYTE*>(locked.pBits);
            for(std::size_t sample = 0; sample < sampleCount && !hasData; ++sample)
            {
                const std::size_t row = sampleCount <= 1 ? 0 : sample * (rowCount - 1) / (sampleCount - 1);
                const BYTE* rowBytes = bytes + row * pitch;
                for(std::size_t byte = 0; byte < bytesPerSample; ++byte)
                {
                    if(rowBytes[byte] != 0)
                    {
                        hasData = true;
                        break;
                    }
                }
            }
        }
        if(lockedTexture)
        {
            texture->UnlockRect(0);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        if(lockedTexture)
        {
            texture->UnlockRect(0);
        }
        hasData = false;
    }
    return hasData;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTexture
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetTexture
(
    DWORD           Stage,
    X_D3DResource  *pTexture
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetTexture");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetTexture\n"
               "(\n"
               "   Stage               : 0x%.08X\n"
               "   pTexture            : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Stage, pTexture);
    }
    #endif

    IDirect3DBaseTexture8 *pBaseTexture8 = NULL;
    BOOL bConvertedYuv = FALSE;

    if(pTexture != NULL)
    {
        if((uint32)pTexture & 0x80000000)
        {
            // Fake YUY2 overlay texture: convert its YUV block to a real RGB
            // texture on the host and bind that (the YuvEnable path).
            EmuYuy2TextureInfo *pInfo = EmuFindYuy2Texture(pTexture);
            pBaseTexture8 = EmuConvertYuy2Texture(pInfo);
            if(pInfo != NULL)
            {
                g_dwOverlayW = pInfo->Width;
                g_dwOverlayH = pInfo->Height;
                bConvertedYuv = TRUE;
            }
        }
        else
        {
            EmuVerifyResourceIsRegistered(pTexture);
            pBaseTexture8 = pTexture->EmuBaseTexture8;
        }
    }

    if(Stage == 0)
        g_bStage0ConvertedYuv = bConvertedYuv;

    // Xbox texture locks expose unified-memory mappings and have no matching
    // guest Unlock call. End any host lock before sampling the resource.
    EmuFlushHostTextureLocks(pBaseTexture8);

    HRESULT hRet = D3D_OK;
    __try
    {
        hRet = g_pD3DDevice8->SetTexture(Stage, pBaseTexture8);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        hRet = D3D_OK;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SwitchTexture
// ******************************************************************
VOID __fastcall XTL::EmuIDirect3DDevice8_SwitchTexture
(
    DWORD           Method,
    DWORD           Data,
    DWORD           Format
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SwitchTexture\n"
               "(\n"
               "   Method              : 0x%.08X\n"
               "   Data                : 0x%.08X\n"
               "   Format              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Method, Data, Format);
    }
    #endif

    EmuCleanup("EmuIDirect3DDevice8_SwitchTexture is not implemented!");
/*** 
    IDirect3DBaseTexture8 *pBaseTexture8 = pTexture->EmuBaseTexture8;
    IDirect3DBaseTexture8 *pPrevTexture8 = NULL;
    
    // Xbox SwitchTexture does not decrement the reference count on the
    // old texture, but SetTexture does, so we need to pre-increment
    g_pD3DDevice8->GetTexture(Stage, &pPrevTexture8);

    HRESULT hRet = g_pD3DDevice8->SetTexture(Stage, pBaseTexture8);

    // Xbox SwitchTexture does not increment reference count, but the
    // above SetTexture does, so we need to remove it.
    pBaseTexture8->Release();
***/
    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetDisplayMode
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetDisplayMode
(
    X_D3DDISPLAYMODE         *pMode
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("GetDisplayMode");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetDisplayMode\n"
               "(\n"
               "   pMode               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pMode);
    }
    #endif

    HRESULT hRet;

    // ******************************************************************
    // * make adjustments to parameters to make sense with windows d3d
    // ******************************************************************
    {
        D3DDISPLAYMODE *pPCMode = (D3DDISPLAYMODE*)pMode;

        hRet = g_pD3DDevice8->GetDisplayMode(pPCMode);

        // Convert Format (PC->Xbox)
        pMode->Format = EmuPC2XB_D3DFormat(pPCMode->Format);

        // TODO: Make this configurable in the future?
        pMode->Flags  = 0x000000A1; // D3DPRESENTFLAG_FIELD | D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_LOCKABLE_BACKBUFFER
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetDisplayFieldStatus
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetDisplayFieldStatus
(
    X_D3DFIELD_STATUS       *pFieldStatus
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("GetDisplayFieldStatus");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetDisplayFieldStatus\n"
               "(\n"
               "   pFieldStatus        : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pFieldStatus);
    }
    #endif

    // On real hardware this reads the current display field parity and the
    // vblank tally straight from the NV2A CRTC.  Cxbx presents whole progressive
    // frames through the host GPU, so report D3DFIELD_PROGRESSIVE and synthesise
    // a 60 Hz vblank counter from the wall clock.  That is enough for callers
    // that pace themselves against the vblank count -- e.g. XMV video decode,
    // whose XMVDecoder_GetNextFrame samples this every frame and advances when
    // (VBlankCount - baseline) crosses the next frame's presentation time.
    if(pFieldStatus != NULL)
    {
        pFieldStatus->Field       = 3; // D3DFIELD_PROGRESSIVE
        pFieldStatus->VBlankCount  = (DWORD)(((unsigned __int64)GetTickCount() * 60) / 1000);

        static int s_FieldTraceEnabled = -1;
        static DWORD s_FieldTraceSequence = 0;
        if(s_FieldTraceEnabled < 0)
            s_FieldTraceEnabled = getenv("CXBX_FIELD_TRACE") != NULL ? 1 : 0;
        if(s_FieldTraceEnabled == 1)
        {
            const DWORD sequence = ++s_FieldTraceSequence;
            if(sequence <= 8 || sequence % 120 == 0)
            {
                printf("FIELD| seq=%lu type=%lu vblank=%lu tick=%lu\n",
                       static_cast<unsigned long>(sequence),
                       static_cast<unsigned long>(pFieldStatus->Field),
                       static_cast<unsigned long>(pFieldStatus->VBlankCount),
                       static_cast<unsigned long>(GetTickCount()));
            }
        }
    }

    EmuSwapFS();   // XBox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_Clear
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_Clear
(
    DWORD           Count,
    CONST D3DRECT  *pRects,
    DWORD           Flags,
    D3DCOLOR        Color,
    float           Z,
    DWORD           Stencil
)
{
    D3D_TRACE("Clear");
    EmuSwapFS();   // Win2k/XP FS
    const std::uint32_t traceSequence = cxbx::trace::RecordD3dCall(
        cxbx::trace::D3dApi::Clear, static_cast<std::uint32_t>(Flags),
        static_cast<std::uint32_t>(g_D3DDebugMarker));

    EmuLocateD3DDebugGlobals();
    DWORD *pApiCounters = EmuD3DPerfApiCounters();
    if(pApiCounters != NULL)
        pApiCounters[EMU_API_D3DDEVICE_CLEAR]++;

    EmuFlushTiledSurfaceLocks();

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_Clear\n"
               "(\n"
               "   Count               : 0x%.08X\n"
               "   pRects              : 0x%.08X\n"
               "   Flags               : 0x%.08X\n"
               "   Color               : 0x%.08X\n"
               "   Z                   : %f\n"
               "   Stencil             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Count, pRects, Flags,
               Color, Z, Stencil);
    }
    #endif
    
    // ******************************************************************
    // * make adjustments to parameters to make sense with windows d3d
    // ******************************************************************
    {
        // TODO: D3DCLEAR_TARGET_A, *R, *G, *B don't exist on windows
        DWORD newFlags = 0;

        if(Flags & 0x000000f0)
            newFlags |= D3DCLEAR_TARGET;

        if(Flags & 0x00000001)
            newFlags |= D3DCLEAR_ZBUFFER;

        if(Flags & 0x00000002)
            newFlags |= D3DCLEAR_STENCIL;

        // On Xbox a Clear never fails; host d3d8 rejects depth/stencil flags
        // that do not match the bound depth surface (it throws
        // D3DERR_INVALIDCALL internally -- Turok Evolution's frame loop).
        // Drop the flags the host cannot honor.
        if(newFlags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL))
        {
            IDirect3DSurface8 *pDepth = NULL;
            if(FAILED(g_pD3DDevice8->GetDepthStencilSurface(&pDepth)) || pDepth == NULL)
            {
                newFlags &= ~(D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL);
            }
            else
            {
                D3DSURFACE_DESC Desc;
                if(SUCCEEDED(pDepth->GetDesc(&Desc)) &&
                   Desc.Format != D3DFMT_D24S8 && Desc.Format != D3DFMT_D24X4S4 &&
                   Desc.Format != D3DFMT_D15S1)
                    newFlags &= ~D3DCLEAR_STENCIL;
                pDepth->Release();
            }
        }

        Flags = newFlags;
    }

    HRESULT ret = D3D_OK;
    if(Flags != 0)
        ret = g_pD3DDevice8->Clear(Count, pRects, Flags, Color, Z, Stencil);

    // Advance the recording pushbuffer offset to reflect the bytes this call
    // would have consumed in a real NV2A command stream.
    if(g_pRecordingPushBuffer != NULL)
        g_pRecordingPushBuffer->Size += 4;

    if(FAILED(ret))
    {
        static LONG WarnCount = 0;
        if(InterlockedIncrement(&WarnCount) <= 5)
            EmuWarning("Clear failed (0x%.08X) host flags=0x%.08X", ret, Flags);
    }

    if(g_pD3DSingleStepPusher != NULL && (*g_pD3DSingleStepPusher & 1) != 0)
    {
        // A read lock is the D3D8 host-side synchronization primitive available
        // here: it cannot complete until rendering into the target has drained.
        XTL::IDirect3DSurface8 *pRenderTarget = NULL;
        if(SUCCEEDED(g_pD3DDevice8->GetRenderTarget(&pRenderTarget)) &&
           pRenderTarget != NULL)
        {
            XTL::D3DLOCKED_RECT LockedRect;
            RECT Pixel = { 0, 0, 1, 1 };
            if(SUCCEEDED(pRenderTarget->LockRect(&LockedRect, &Pixel, D3DLOCK_READONLY)))
                pRenderTarget->UnlockRect();
            pRenderTarget->Release();
        }

        if(pApiCounters != NULL)
            pApiCounters[EMU_API_D3DDEVICE_BLOCKUNTILIDLE]++;

        cxbx::trace::RecordD3dWait(cxbx::trace::D3dWaitReason::SingleStep,
                                   traceSequence, 0, false);
    }

    cxbx::trace::RecordD3dReturn(cxbx::trace::D3dApi::Clear, traceSequence,
                                 static_cast<std::uint32_t>(ret));
    EmuSwapFS();   // XBox FS

    return ret;
}

DWORD WINAPI XTL::EmuIDirect3DDevice8_SetDebugMarker(DWORD Marker)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetDebugMarker");
    DWORD PreviousMarker = g_D3DDebugMarker;
    g_D3DDebugMarker = Marker;
    EmuSwapFS();   // XBox FS
    return PreviousMarker;
}

DWORD WINAPI XTL::EmuIDirect3DDevice8_GetDebugMarker()
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("GetDebugMarker");
    DWORD Marker = g_D3DDebugMarker;
    EmuSwapFS();   // XBox FS
    return Marker;
}

VOID WINAPI XTL::EmuD3DPERF_Reset()
{
    D3D_TRACE("D3DPERF_Reset");
    EmuSwapFS();   // Win2k/XP FS
    EmuLocateD3DDebugGlobals();
    if(g_pD3DPerfStatistics != NULL)
    {
        memset(g_pD3DPerfStatistics + EMU_D3DPERF_WAIT_COUNTER_OFFSET, 0,
               EMU_D3DPERF_WAIT_COUNTER_DWORDS * sizeof(DWORD));
        memset(g_pD3DPerfStatistics + EMU_D3DPERF_API_COUNTER_OFFSET, 0,
               EMU_D3DAPI_MAX * sizeof(DWORD));
        memset(g_pD3DPerfStatistics + EMU_D3DPERF_RENDER_COUNTER_OFFSET, 0,
               EMU_D3DPERF_RENDER_COUNTER_DWORDS * sizeof(DWORD));
        memset(g_pD3DPerfStatistics + EMU_D3DPERF_TEXTURE_COUNTER_OFFSET, 0,
               EMU_D3DPERF_TEXTURE_COUNTER_DWORDS * sizeof(DWORD));
    }
    EmuSwapFS(); // XBox FS
}

// Mirror the presented frame to the emulator's GDI window (Emu.cpp).
extern "C" void EmuHostBlitToWindow(const void* Pixels, unsigned Width, unsigned Height);

struct EmuPresentMirrorFrame
{
    DWORD* pixels = nullptr;
    std::size_t capacity = 0;
    unsigned width = 0;
    unsigned height = 0;
    bool valid = false;
};

static EmuPresentMirrorFrame g_EmuPresentMirrorFrame;

static bool EmuPresentMirrorEnabled()
{
    static int mirrorWindow = -1;
    if(mirrorWindow < 0)
    {
        char mirrorEnvironment[8] = {};
        const DWORD mirrorEnvironmentLength = GetEnvironmentVariableA(
            "CXBX_D3D_WINDOW", mirrorEnvironment, sizeof(mirrorEnvironment));
        const char* mirrorEnvironmentValue =
            mirrorEnvironmentLength == 0 ? nullptr : mirrorEnvironment;
        mirrorWindow = cxbx::d3d::MirrorWindowEnabled(mirrorEnvironmentValue) ? 1 : 0;
    }
    return mirrorWindow != 0;
}

// Capture before Present while the completed backbuffer is still lockable.
// The pixels are blitted afterward so an unreliable windowed D3D8 Present
// cannot overwrite the GDI fallback with a stale or partially composed frame.
// Must run with the host (Win2k/XP) FS active.
static void EmuCaptureHostBackbuffer()
{
    g_EmuPresentMirrorFrame.valid = false;

    XTL::IDirect3DSurface8* pHostBack = NULL;
    const HRESULT getBackBufferResult =
        g_pD3DDevice8->GetBackBuffer(0, XTL::D3DBACKBUFFER_TYPE_MONO, &pHostBack);
    if(FAILED(getBackBufferResult) || pHostBack == NULL)
    {
        static bool loggedGetBackBufferFailure = false;
        if(!loggedGetBackBufferFailure)
        {
            printf("EmuD3D8: visible-frame mirror could not get the host back buffer (0x%.08lX).\n",
                   getBackBufferResult);
            loggedGetBackBufferFailure = true;
        }
        return;
    }

    XTL::D3DSURFACE_DESC sd;
    XTL::D3DLOCKED_RECT lr;
    // The guest's UnlockRect is not HLE-patched, so the host back buffer is left
    // locked by the emulated LockRect the guest used to write the frame. Clear
    // that stale lock before reading it, or our LockRect returns INVALIDCALL.
    pHostBack->UnlockRect();
    const HRESULT getDescResult = pHostBack->GetDesc(&sd);
    const HRESULT lockResult = SUCCEEDED(getDescResult)
                                   ? pHostBack->LockRect(&lr, NULL, D3DLOCK_READONLY)
                                   : getDescResult;
    if(SUCCEEDED(getDescResult) && SUCCEEDED(lockResult))
    {
        const std::size_t width = sd.Width;
        const std::size_t height = sd.Height;
        const bool validSize = width != 0 && height != 0 &&
                               width <= (std::numeric_limits<std::size_t>::max)() / height;
        const std::size_t requiredPixels = validSize ? width * height : 0;
        if(requiredPixels > g_EmuPresentMirrorFrame.capacity &&
           requiredPixels <= (std::numeric_limits<std::size_t>::max)() / sizeof(DWORD))
        {
            DWORD* replacement = static_cast<DWORD*>(
                malloc(requiredPixels * sizeof(DWORD)));
            if(replacement != nullptr)
            {
                free(g_EmuPresentMirrorFrame.pixels);
                g_EmuPresentMirrorFrame.pixels = replacement;
                g_EmuPresentMirrorFrame.capacity = requiredPixels;
            }
        }

        if(requiredPixels != 0 &&
           requiredPixels <= g_EmuPresentMirrorFrame.capacity)
        {
            for(DWORD y = 0; y < sd.Height; y++)
            {
                memcpy(g_EmuPresentMirrorFrame.pixels + y * sd.Width,
                       (BYTE*)lr.pBits + y * lr.Pitch, sd.Width * sizeof(DWORD));
            }
            g_EmuPresentMirrorFrame.width = sd.Width;
            g_EmuPresentMirrorFrame.height = sd.Height;
            g_EmuPresentMirrorFrame.valid = true;
        }

        pHostBack->UnlockRect();
    }
    else
    {
        static bool loggedLockFailure = false;
        if(!loggedLockFailure)
        {
            printf("EmuD3D8: visible-frame mirror could not read the host back buffer "
                   "(desc=0x%.08lX lock=0x%.08lX).\n",
                   getDescResult, lockResult);
            loggedLockFailure = true;
        }
    }
    pHostBack->Release();
}

static void EmuCapturePresentMirror()
{
    if(!EmuPresentMirrorEnabled() || g_pD3DDevice8 == NULL)
    {
        return;
    }

    // Titles that hand the emulator an incomplete/scratch device (e.g. the CDX
    // D3D__pDevice hack) can fault inside GetBackBuffer/LockRect; the mirror is a
    // display convenience and must never take the title down, so swallow it.
    __try
    {
        EmuCaptureHostBackbuffer();
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        g_EmuPresentMirrorFrame.valid = false;
    }
}

static void EmuBlitPresentMirror()
{
    if(!g_EmuPresentMirrorFrame.valid)
    {
        return;
    }

    __try
    {
        EmuHostBlitToWindow(g_EmuPresentMirrorFrame.pixels,
                            g_EmuPresentMirrorFrame.width,
                            g_EmuPresentMirrorFrame.height);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    g_EmuPresentMirrorFrame.valid = false;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_Present
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_Present
(
    CONST RECT* pSourceRect,
    CONST RECT* pDestRect,
    PVOID       pDummy1,
    PVOID       pDummy2
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("Present");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_Present\n"
               "(\n"
               "   pSourceRect         : 0x%.08X\n"
               "   pDestRect           : 0x%.08X\n"
               "   pDummy1             : 0x%.08X\n"
               "   pDummy2             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pSourceRect, pDestRect, pDummy1, pDummy2);
    }
    #endif

    EmuFlushTiledSurfaceLocks();

    const HRESULT hRet = EmuPresentHostDevice(
        pSourceRect, pDestRect, static_cast<HWND>(pDummy1),
        static_cast<const RGNDATA*>(pDummy2), true);

    g_D3DDebugMarker = 0;

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * EmuComposeOverlay
// ******************************************************************
// Draw the latest converted overlay frame as a fullscreen quad right before
// Present. The touched device state is saved and restored so the title's
// HLE-managed state is unaffected.
static void EmuComposeOverlay()
{
    using namespace XTL;

    if(g_pOverlayFrameTexture == NULL)
        return;

    IDirect3DSurface8 *pBackBuffer = NULL;
    if(FAILED(g_pD3DDevice8->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer)) || pBackBuffer == NULL)
        return;

    D3DSURFACE_DESC Desc;
    pBackBuffer->GetDesc(&Desc);
    pBackBuffer->Release();

    struct { FLOAT x, y, z, rhw, u, v; } Quad[4] =
    {
        {           -0.5f,            -0.5f, 0.0f, 1.0f, 0.0f, 0.0f },
        { Desc.Width-0.5f,            -0.5f, 0.0f, 1.0f, 1.0f, 0.0f },
        { Desc.Width-0.5f, Desc.Height-0.5f, 0.0f, 1.0f, 1.0f, 1.0f },
        {           -0.5f, Desc.Height-0.5f, 0.0f, 1.0f, 0.0f, 1.0f },
    };

    // Save the state the quad needs.
    DWORD OldVS = 0, OldPS = 0, OldZ = 0, OldBlend = 0, OldCull = 0;
    DWORD OldColorOp = 0, OldColorArg1 = 0, OldMag = 0, OldMin = 0;
    IDirect3DBaseTexture8 *pOldTexture = NULL;

    g_pD3DDevice8->GetVertexShader(&OldVS);
    g_pD3DDevice8->GetPixelShader(&OldPS);
    g_pD3DDevice8->GetRenderState(D3DRS_ZENABLE, &OldZ);
    g_pD3DDevice8->GetRenderState(D3DRS_ALPHABLENDENABLE, &OldBlend);
    g_pD3DDevice8->GetRenderState(D3DRS_CULLMODE, &OldCull);
    g_pD3DDevice8->GetTextureStageState(0, D3DTSS_COLOROP, &OldColorOp);
    g_pD3DDevice8->GetTextureStageState(0, D3DTSS_COLORARG1, &OldColorArg1);
    g_pD3DDevice8->GetTextureStageState(0, D3DTSS_MAGFILTER, &OldMag);
    g_pD3DDevice8->GetTextureStageState(0, D3DTSS_MINFILTER, &OldMin);
    g_pD3DDevice8->GetTexture(0, &pOldTexture);

    g_pD3DDevice8->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_TEX1);
    g_pD3DDevice8->SetPixelShader(0);
    g_pD3DDevice8->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pD3DDevice8->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pD3DDevice8->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pD3DDevice8->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pD3DDevice8->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_pD3DDevice8->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    g_pD3DDevice8->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    g_pD3DDevice8->SetTexture(0, g_pOverlayFrameTexture);

    g_pD3DDevice8->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, Quad, sizeof(Quad[0]));

    // Restore.
    g_pD3DDevice8->SetVertexShader(OldVS);
    g_pD3DDevice8->SetPixelShader(OldPS);
    g_pD3DDevice8->SetRenderState(D3DRS_ZENABLE, OldZ);
    g_pD3DDevice8->SetRenderState(D3DRS_ALPHABLENDENABLE, OldBlend);
    g_pD3DDevice8->SetRenderState(D3DRS_CULLMODE, OldCull);
    g_pD3DDevice8->SetTextureStageState(0, D3DTSS_COLOROP, OldColorOp);
    g_pD3DDevice8->SetTextureStageState(0, D3DTSS_COLORARG1, OldColorArg1);
    g_pD3DDevice8->SetTextureStageState(0, D3DTSS_MAGFILTER, OldMag);
    g_pD3DDevice8->SetTextureStageState(0, D3DTSS_MINFILTER, OldMin);
    g_pD3DDevice8->SetTexture(0, pOldTexture);

    if(pOldTexture != NULL)
        pOldTexture->Release();
}

static void EmuPacePresent(
    LARGE_INTEGER& NextDeadline,
    LONGLONG PeriodNumerator,
    LONGLONG PeriodDenominator)
{
    static LARGE_INTEGER Frequency = {};

    if(Frequency.QuadPart == 0 && !QueryPerformanceFrequency(&Frequency))
    {
        return;
    }

    LARGE_INTEGER Now = {};
    QueryPerformanceCounter(&Now);
    const LONGLONG Interval =
        Frequency.QuadPart * PeriodNumerator / PeriodDenominator;
    if(Interval <= 0)
    {
        return;
    }

    if(NextDeadline.QuadPart == 0 || Now.QuadPart >= NextDeadline.QuadPart)
    {
        // Skip periods that elapsed while the guest was decoding or blocked.
        // Never issue a burst of catch-up updates or presents.
        NextDeadline.QuadPart = cxbx::d3d::NextPresentDeadline(
            Now.QuadPart, NextDeadline.QuadPart, Interval);
        return;
    }

    const LONGLONG Target = NextDeadline.QuadPart;
    do
    {
        const LONGLONG RemainingMilliseconds =
            (Target - Now.QuadPart) * 1000 / Frequency.QuadPart;
        if(RemainingMilliseconds > 1)
        {
            Sleep(static_cast<DWORD>(RemainingMilliseconds - 1));
        }
        else
        {
            SwitchToThread();
        }
        QueryPerformanceCounter(&Now);
    } while(Now.QuadPart < Target);

    NextDeadline.QuadPart = Target + Interval;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_Swap
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_Swap
(
    DWORD Flags
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("Swap");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_Swap\n"
               "(\n"
               "   Flags               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Flags);
    }
    #endif

    // 4627-era titles always pass 0; the 5849 library swaps with real flag
    // bits (0 means D3DSWAP default 5 inside the library). All of them still
    // reduce to presenting the frame on the host, so warn instead of killing
    // the emulation.
    if(Flags != 0)
        EmuWarning("EmuIDirect3DDevice8_Swap: Flags = 0x%.08X (ignored)", Flags);

    EmuFlushTiledSurfaceLocks();
    EmuComposeOverlay();

    // Windowed host Present does not reliably block for the Xbox's 60 Hz
    // display cadence. Pace every Swap so frame-counted title animations and
    // attract-mode timeouts do not run at unrestricted host speed. If Present
    // already blocked, EmuPacePresent advances over that elapsed period without
    // adding another wait.
    EmuPacePresent(g_NextSwapPresent, 1, 60);

    const HRESULT hRet = EmuPresentHostDevice(nullptr, nullptr, nullptr, nullptr, true);

    if(g_pOverlayFrameTexture != NULL &&
       cxbx::trace::IsEnabled(cxbx::trace::Channel::Media))
    {
        cxbx::trace::RecordBinary(
            cxbx::trace::Event::MediaPresent,
            g_OverlayFrameGeneration.load(std::memory_order_relaxed));
    }

    // The debug runtime scopes markers to one presented frame.
    g_D3DDebugMarker = 0;

    // Draw bisection/tracing counts draws per presented frame.
    g_D3DDebugFrame++;
    g_D3DDebugDrawIndex = 0;
    EmuD3DPerfFrameBoundary();

    if(g_D3DCallStatsEnabled == 1)
    {
        static DWORD s_dwSwapCount = 0;

        if((++s_dwSwapCount & 127) == 0)
            EmuD3DDumpCallStats();
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * Immediate-mode drawing (D3DDevice_Begin / SetVertexData* / End)
// ******************************************************************
// * The Xbox immediate mode streams per-vertex attributes into the push
// * buffer; a vertex is emitted whenever the position register (0, or the
// * -1 "vertex" pseudo-register) is written. Collect the vertices host-side
// * and draw them with DrawPrimitiveUP on End -- pretransformed UI quads
// * (menus, text) are the main users of this path.
// ******************************************************************
struct EmuImVertex
{
    FLOAT x, y, z, rhw;
    DWORD Diffuse;
    FLOAT u, v;
};

#define EMU_IM_FVF       (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)
#define EMU_IM_MAXVERTS  8192

static EmuImVertex g_EmuImVerts[EMU_IM_MAXVERTS];
static EmuImVertex g_EmuImCur = { 0, 0, 0, 1.0f, 0xFFFFFFFF, 0, 0 };
static DWORD g_EmuImPrim = 0;
static int   g_EmuImCount = 0;
static BOOL  g_EmuImActive = FALSE;
static BOOL  g_EmuImCustomShader = FALSE;
static BOOL  g_EmuImConvertedYuv = FALSE;
static BOOL  g_EmuCurrentVertexShaderIsCustom = FALSE;

VOID WINAPI XTL::EmuIDirect3DDevice8_Begin
(
    X_D3DPRIMITIVETYPE PrimitiveType
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("Begin");

    #ifdef _DEBUG_TRACE
    printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_Begin(%d)\n", GetCurrentThreadId(), PrimitiveType);
    #endif

    g_EmuImPrim = (DWORD)PrimitiveType;
    g_EmuImCount = 0;
    g_EmuImActive = TRUE;
    g_EmuImCustomShader = g_EmuCurrentVertexShaderIsCustom;
    g_EmuImConvertedYuv = g_bStage0ConvertedYuv;
    g_EmuImCur.rhw = 1.0f;
    g_EmuImCur.Diffuse = 0xFFFFFFFF;

    EmuSwapFS();   // XBox FS
}

VOID WINAPI XTL::EmuIDirect3DDevice8_SetVertexData4f
(
    INT     Register,
    FLOAT   a,
    FLOAT   b,
    FLOAT   c,
    FLOAT   d
)
{
    D3D_TRACE("SetVertexData4f");
    EmuSwapFS();   // Win2k/XP FS

    switch(Register)
    {
        case -1:   // D3DVSDE_VERTEX: final position write, emits the vertex
        case 0:    // D3DVSDE_POSITION
            g_EmuImCur.x = a;
            g_EmuImCur.y = b;
            if(g_EmuImCustomShader)
            {
                // Xbox UI shaders commonly pack screen-space position in v0.xy
                // and texture coordinates in v0.zw (for example CXBFont).
                g_EmuImCur.z = 0.0f;
                g_EmuImCur.rhw = 1.0f;
                g_EmuImCur.u = c;
                g_EmuImCur.v = d;
            }
            else
            {
                g_EmuImCur.z = c;
                g_EmuImCur.rhw = (d == 0.0f) ? 1.0f : d;
            }
            if(g_EmuImActive && g_EmuImCount < EMU_IM_MAXVERTS)
                g_EmuImVerts[g_EmuImCount++] = g_EmuImCur;
            break;

        case 3:    // D3DVSDE_DIFFUSE as floats
        {
            DWORD r = (DWORD)(a * 255.0f) & 0xFF;
            DWORD g = (DWORD)(b * 255.0f) & 0xFF;
            DWORD bl = (DWORD)(c * 255.0f) & 0xFF;
            DWORD al = (DWORD)(d * 255.0f) & 0xFF;
            g_EmuImCur.Diffuse = (al << 24) | (r << 16) | (g << 8) | bl;
            break;
        }

        case 9:    // D3DVSDE_TEXCOORD0
            if(g_EmuImConvertedYuv && g_dwOverlayW != 0 && g_dwOverlayH != 0)
            {
                // Xbox linear textures use texel-space coordinates. The YUY2
                // fallback is a normal host RGB texture, so normalize its UVs.
                g_EmuImCur.u = a / (FLOAT)g_dwOverlayW;
                g_EmuImCur.v = b / (FLOAT)g_dwOverlayH;
            }
            else
            {
                g_EmuImCur.u = a;
                g_EmuImCur.v = b;
            }
            break;

        default:
            // other attribute registers (normal, specular, tex1..3) are not
            // part of the pretransformed-UI vertex; ignore
            break;
    }

    EmuSwapFS();   // XBox FS
}

VOID WINAPI XTL::EmuIDirect3DDevice8_SetVertexData2f
(
    INT     Register,
    FLOAT   a,
    FLOAT   b
)
{
    D3D_TRACE("SetVertexData2f");
    // 2f writes to the position register leave z=0, w=1
    XTL::EmuIDirect3DDevice8_SetVertexData4f(Register, a, b, 0.0f, 1.0f);
}

VOID WINAPI XTL::EmuIDirect3DDevice8_SetVertexDataColor
(
    INT     Register,
    DWORD   Color
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetVertexDataColor");

    if(Register == 3)
        g_EmuImCur.Diffuse = Color;

    EmuSwapFS();   // XBox FS
}

VOID WINAPI XTL::EmuIDirect3DDevice8_End()
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("End");

    #ifdef _DEBUG_TRACE
    printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_End() verts=%d\n", GetCurrentThreadId(), g_EmuImCount);
    #endif

    if(g_EmuImActive && g_EmuImCount >= 3 && g_pD3DDevice8 != 0)
    {
        g_pD3DDevice8->SetVertexShader(EMU_IM_FVF);

        if(g_EmuImPrim == 8)   // X_D3DPT_QUADLIST: expand each quad to two triangles
        {
            static EmuImVertex Tris[EMU_IM_MAXVERTS + (EMU_IM_MAXVERTS / 2)];
            int Quads = g_EmuImCount / 4;
            int n = 0;
            for(int q = 0; q < Quads; q++)
            {
                EmuImVertex *v = &g_EmuImVerts[q * 4];
                Tris[n++] = v[0]; Tris[n++] = v[1]; Tris[n++] = v[2];
                Tris[n++] = v[0]; Tris[n++] = v[2]; Tris[n++] = v[3];
            }
            if(EmuD3DDrawGate("End", D3DPT_TRIANGLELIST, n / 3))
            {
                __try
                {
                    g_pD3DDevice8->DrawPrimitiveUP(D3DPT_TRIANGLELIST, n / 3, Tris, sizeof(EmuImVertex));
                }
                __except(EXCEPTION_EXECUTE_HANDLER)
                {
                }
                EmuD3DDrawPost();
            }
        }
        else
        {
            D3DPRIMITIVETYPE PCPrim = EmuPrimitiveType((X_D3DPRIMITIVETYPE)g_EmuImPrim);
            UINT PrimCount = EmuD3DVertex2PrimitiveCount((X_D3DPRIMITIVETYPE)g_EmuImPrim, g_EmuImCount);
            if(EmuD3DDrawGate("End", PCPrim, PrimCount))
            {
                __try
                {
                    g_pD3DDevice8->DrawPrimitiveUP(PCPrim, PrimCount, g_EmuImVerts, sizeof(EmuImVertex));
                }
                __except(EXCEPTION_EXECUTE_HANDLER)
                {
                }
                EmuD3DDrawPost();
            }
        }
    }

    g_EmuImActive = FALSE;
    g_EmuImCount = 0;

    EmuSwapFS();   // XBox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_MakeSpace
// ******************************************************************
DWORD* WINAPI XTL::EmuIDirect3DDevice8_MakeSpace()
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("MakeSpace");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_MakeSpace();\n", GetCurrentThreadId());
    }
    #endif

    // Xbox extension: reserve room in the NV2A push buffer and return its
    // writable cursor. HLE has no guest-visible host push buffer, but callers
    // still emit packets through the returned pointer before advancing their
    // device cursor. Give each guest thread discardable storage for that ABI.
    alignas(64) static thread_local std::array<DWORD, 16 * 1024> pushBufferScratch{};
    DWORD* const pushBuffer = pushBufferScratch.data();

    EmuSwapFS();   // XBox FS

    return pushBuffer;
}

// ******************************************************************
// * func: EmuIDirect3DResource8_Register
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DResource8_Register
(
    X_D3DResource      *pThis,
    PVOID               pBase
)
{
    D3D_TRACE("Resource_Register");
    EmuSwapFS();   // Win2k/XP FS

    // A partially-HLE title may pass a resource whose fields (Common, Data,
    // Lock) contain garbage from uninitialized guest memory, causing an
    // access violation somewhere in the host resource creation/copy below.
    // Guard the entire body so a bad resource is left unbacked instead of
    // crashing the process.
    HRESULT hRet = D3D_OK;
    __try
    {

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DResource8_Register\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               "   pBase               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pBase);
    }
    #endif

    HRESULT hRet;

    X_D3DResource *pResource = (X_D3DResource*)pThis;

    DWORD dwCommonType = pResource->Common & X_D3DCOMMON_TYPE_MASK;

    const DWORD dwRegisterBase = reinterpret_cast<DWORD>(pBase);
    const DWORD dwDataOffset = pThis->Data;
    const DWORD dwUnmaskedDataAddress = dwRegisterBase + dwDataOffset;
    const bool isPushBuffer = dwCommonType == X_D3DCOMMON_TYPE_PUSHBUFFER;

    ULONG contiguousBlockSize = 0;
    const ULONG contiguousBlockBase =
        EmuContiguousBlockBase(dwUnmaskedDataAddress, &contiguousBlockSize);
    const bool hasTrackedBacking = contiguousBlockBase != 0;

    // Xbox D3DResource::Register adds the resource's Data offset to pBase and
    // masks non-push-buffer resources into the NV2A's 28-bit UMA address
    // window. Guest code can therefore pass a pointer-shaped alias which is
    // unreadable on the host even though its masked address names live title
    // memory. Match the XDK routine before inspecting or copying the resource.
    const DWORD dwResolvedDataAddress = cxbx::d3d::XboxResourceDataAddress(
        reinterpret_cast<std::uintptr_t>(pBase), pThis->Data, isPushBuffer);
    const DWORD dwHostDataAddress = isPushBuffer
                                        ? dwResolvedDataAddress
                                        : cxbx::d3d::XboxResourceHostDataAddress(
                                              dwRegisterBase, dwDataOffset,
                                              hasTrackedBacking);

    // Preserve the XDK's masked guest/NV2A address semantics, but upload from
    // the live host alias when the contiguous-allocation tracker proves that
    // the unmasked pointer belongs to title-owned memory.  Heap-backed Xbox
    // allocations commonly live above the 256 MiB D3D resource window (for
    // example Turok's level textures at 0x11xxxxxx); masking those host
    // pointers before the copy turns valid pixels into an unreadable address.
    pBase = reinterpret_cast<PVOID>(dwHostDataAddress);

    // ******************************************************************
    // * Determine the resource type, and initialize
    // ******************************************************************
    switch(dwCommonType)
    {
        case X_D3DCOMMON_TYPE_VERTEXBUFFER:
        {
            #ifdef _DEBUG_TRACE
            printf("EmuIDirect3DResource8_Register (0x%X) : Creating VertexBuffer...\n", GetCurrentThreadId());
            #endif

            X_D3DVertexBuffer *pVertexBuffer = (X_D3DVertexBuffer*)pResource;

            // ******************************************************************
            // * Create the vertex buffer
            // ******************************************************************
            {
                PVOID vertexData = reinterpret_cast<PVOID>(dwHostDataAddress);
                const DWORD dwSize = hasTrackedBacking
                                         ? contiguousBlockSize -
                                               (dwUnmaskedDataAddress - contiguousBlockBase)
                                         : EmuCheckAllocationSize(vertexData);

                // Registered Xbox vertex buffers retain their title-owned UMA
                // backing store. Preserve that live source separately from the
                // host D3D copy so CPU vertex-shader draws observe later guest
                // writes instead of the registration-time snapshot.
                EmuResetRegisteredVertexBuffer(pVertexBuffer, vertexData, dwSize);

                hRet = g_pD3DDevice8->CreateVertexBuffer
                (
                    dwSize, 0, 0, D3DPOOL_MANAGED,
                    &pResource->EmuVertexBuffer8
                );

                // A zero / unknown allocation size (common when a partial-HLE
                // title registers a resource whose backing size can't be
                // determined) makes CreateVertexBuffer fail and leaves
                // EmuVertexBuffer8 NULL; locking it then crashes the render
                // thread. Warn and leave the resource unbacked instead.
                if(FAILED(hRet) || pResource->EmuVertexBuffer8 == 0)
                {
                    EmuWarning("EmuIDirect3DResource8_Register: CreateVertexBuffer(%u) failed (0x%.08X) -- resource left unbacked",
                               dwSize, hRet);
                    pResource->EmuVertexBuffer8 = 0;
                    pResource->Data = 0;
                    EmuDiscardRegisteredVertexBuffer(pVertexBuffer);
                    break;
                }

                BYTE *pData = 0;

                hRet = pResource->EmuVertexBuffer8->Lock(0, 0, &pData, 0);

                if(FAILED(hRet) || pData == 0)
                {
                    EmuWarning("EmuIDirect3DResource8_Register: VertexBuffer Lock failed -- resource left unbacked");
                    EmuDiscardRegisteredVertexBuffer(pVertexBuffer);
                    break;
                }

                // Guard the memcpy: a partially-HLE title may register a
                // resource whose Data field points to invalid memory, causing
                // an access violation in the copy. Catch it locally and leave
                // the resource unbacked instead of crashing the process.
                __try
                {
                    memcpy(pData, vertexData, dwSize);
                }
                __except(EXCEPTION_EXECUTE_HANDLER)
                {
                    EmuWarning("EmuIDirect3DResource8_Register: VertexBuffer data copy fault at 0x%.08X -- resource left unbacked", (DWORD)vertexData);
                    pResource->EmuVertexBuffer8->Unlock();
                    pResource->EmuVertexBuffer8 = 0;
                    pResource->Data = 0;
                    EmuDiscardRegisteredVertexBuffer(pVertexBuffer);
                    break;
                }

                pResource->EmuVertexBuffer8->Unlock();

                pResource->Data = (ULONG)pData;
            }

            #ifdef _DEBUG_TRACE
            printf("EmuIDirect3DResource8_Register (0x%X) : Successfully Created VertexBuffer\n", GetCurrentThreadId());
            #endif
        }
        break;

        case X_D3DCOMMON_TYPE_INDEXBUFFER:
        {
            #ifdef _DEBUG_TRACE
            printf("EmuIDirect3DResource8_Register :-> IndexBuffer...\n");
            #endif

            X_D3DIndexBuffer *pIndexBuffer = (X_D3DIndexBuffer*)pResource;

            // ******************************************************************
            // * Create the index buffer
            // ******************************************************************
            {
                DWORD dwSize = EmuCheckAllocationSize(pBase);

                HRESULT hRet = g_pD3DDevice8->CreateIndexBuffer
                (
                    dwSize, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED,
                    &pIndexBuffer->EmuIndexBuffer8
                );

                if(FAILED(hRet))
                    EmuCleanup("CreateIndexBuffer failed");

                BYTE *pData = 0;

                hRet = pResource->EmuIndexBuffer8->Lock(0, dwSize, &pData, 0);

                if(FAILED(hRet))
                    EmuCleanup("IndexBuffer Lock failed");

                __try
                {
                    memcpy(pData, (void*)pBase, dwSize);
                }
                __except(EXCEPTION_EXECUTE_HANDLER)
                {
                    EmuWarning("EmuIDirect3DResource8_Register: IndexBuffer data copy fault at 0x%.08X -- resource left unbacked", (DWORD)pBase);
                    pResource->EmuIndexBuffer8->Unlock();
                    pResource->EmuIndexBuffer8 = 0;
                    pResource->Data = 0;
                    break;
                }

                pResource->EmuIndexBuffer8->Unlock();

                pResource->Data = (ULONG)pData;
            }
        }
        break;

        case X_D3DCOMMON_TYPE_PUSHBUFFER:
        {
            // Static XDK pushbuffers store Data as an offset from the base passed
            // to Register. Preserve the resolved host span out-of-line: mutating
            // Data would change the guest resource ABI and make repeat Register
            // calls add the base twice.
            X_D3DPushBuffer *pPushBuffer = (X_D3DPushBuffer*)pResource;
            EmuRecordedPushBuffer* recording = EmuResetRecordedPushBuffer(pPushBuffer);
            if(recording != nullptr && pPushBuffer->AllocationSize != 0 &&
               EmuD3DIsReadableRange(pBase, pPushBuffer->AllocationSize))
            {
                recording->registeredData = static_cast<const DWORD*>(pBase);
                recording->registeredBytes = pPushBuffer->AllocationSize;
            }
        }
        break;

        case X_D3DCOMMON_TYPE_SURFACE:
        case X_D3DCOMMON_TYPE_TEXTURE:
        {
            #ifdef _DEBUG_TRACE
            if(dwCommonType == X_D3DCOMMON_TYPE_SURFACE)
                printf("EmuIDirect3DResource8_Register :-> Surface...\n");
            else
                printf("EmuIDirect3DResource8_Register :-> Texture...\n");
            #endif

            X_D3DPixelContainer *pPixelContainer = (X_D3DPixelContainer*)pResource;

            X_D3DFORMAT X_Format = (X_D3DFORMAT)((pPixelContainer->Format & X_D3DFORMAT_FORMAT_MASK) >> X_D3DFORMAT_FORMAT_SHIFT);
            D3DFORMAT   Format   = EmuXB2PC_D3DFormat(X_Format);

            // TODO: HACK: Temporary?
            if(X_Format == 0x2E)
            {
                X_Format = 0x12;
                Format   = D3DFMT_A8R8G8B8;
            }

            DWORD dwWidth, dwHeight, dwBPP, dwDepth = 1, dwPitch = 0, dwMipMapLevels = 1;
            DWORD dwCompressedSize = 0;
            DWORD dwCompressedBytesPerBlock = 0;
            BOOL  bSwizzled = FALSE, bCompressed = FALSE;
            BOOL  bCubemap = pPixelContainer->Format & X_D3DFORMAT_CUBEMAP;

            if(bCubemap)
                EmuCleanup("Cubemaps are temporarily unsupported");

            // ******************************************************************
            // * Interpret Width/Height/BPP
            // ******************************************************************
            if(X_Format == 0x07 /* X_D3DFMT_X8R8G8B8 */ || X_Format == 0x06 /* X_D3DFMT_A8R8G8B8 */)
            {
                bSwizzled = TRUE;

                // Swizzled 32 Bit
                dwWidth  = 1 << ((pPixelContainer->Format & X_D3DFORMAT_USIZE_MASK) >> X_D3DFORMAT_USIZE_SHIFT);
                dwHeight = 1 << ((pPixelContainer->Format & X_D3DFORMAT_VSIZE_MASK) >> X_D3DFORMAT_VSIZE_SHIFT);
                dwDepth  = 1 << ((pPixelContainer->Format & X_D3DFORMAT_PSIZE_MASK) >> X_D3DFORMAT_PSIZE_SHIFT);
                dwPitch  = dwWidth*4;
                dwBPP = 4;
            }
            else if(X_Format == 0x05 /* X_D3DFMT_R5G6B5 */ || X_Format == 0x04 /* X_D3DFMT_A4R4G4B4 */ ||
                    X_Format == 0x28 /* X_D3DFMT_G8B8 */   || X_Format == 0x1A /* X_D3DFMT_A8L8 */)
            {
                bSwizzled = TRUE;

                // Swizzled 16 Bit
                dwWidth  = 1 << ((pPixelContainer->Format & X_D3DFORMAT_USIZE_MASK) >> X_D3DFORMAT_USIZE_SHIFT);
                dwHeight = 1 << ((pPixelContainer->Format & X_D3DFORMAT_VSIZE_MASK) >> X_D3DFORMAT_VSIZE_SHIFT);
                dwDepth  = 1 << ((pPixelContainer->Format & X_D3DFORMAT_PSIZE_MASK) >> X_D3DFORMAT_PSIZE_SHIFT);
                dwPitch  = dwWidth*2;
                dwBPP = 2;
            }
            else if(X_Format == 0x12 /* X_D3DFORMAT_A8R8G8B8 */ || X_Format == 0x2E /* D3DFMT_LIN_D24S8 */)
            {
                // Linear 32 Bit
                dwWidth  = (pPixelContainer->Size & X_D3DSIZE_WIDTH_MASK) + 1;
                dwHeight = ((pPixelContainer->Size & X_D3DSIZE_HEIGHT_MASK) >> X_D3DSIZE_HEIGHT_SHIFT) + 1;
                dwPitch  = (((pPixelContainer->Size & X_D3DSIZE_PITCH_MASK) >> X_D3DSIZE_PITCH_SHIFT)+1)*64;
                dwBPP = 4;
            }
            else if(X_Format == 0x11 /* D3DFMT_LIN_R5G6B5 */)
            {
                // Linear 16 Bit
                dwWidth  = (pPixelContainer->Size & X_D3DSIZE_WIDTH_MASK) + 1;
                dwHeight = ((pPixelContainer->Size & X_D3DSIZE_HEIGHT_MASK) >> X_D3DSIZE_HEIGHT_SHIFT) + 1;
                dwPitch  = (((pPixelContainer->Size & X_D3DSIZE_PITCH_MASK) >> X_D3DSIZE_PITCH_SHIFT)+1)*64;
                dwBPP = 2;
            }
            else if(X_Format == 0x0C /* D3DFMT_DXT1 */ || X_Format == 0x0E /* D3DFMT_DXT2 */ || X_Format == 0x0F /* D3DFMT_DXT3 */)
            {
                bCompressed = TRUE;

                // Compressed
                dwWidth  = 1 << ((pPixelContainer->Format & X_D3DFORMAT_USIZE_MASK) >> X_D3DFORMAT_USIZE_SHIFT);
                dwHeight = 1 << ((pPixelContainer->Format & X_D3DFORMAT_VSIZE_MASK) >> X_D3DFORMAT_VSIZE_SHIFT);
                dwDepth  = 1 << ((pPixelContainer->Format & X_D3DFORMAT_PSIZE_MASK) >> X_D3DFORMAT_PSIZE_SHIFT);

                // DXT textures always occupy complete 4x4 blocks. This matters
                // for Xbox UI textures smaller than one block: a 1x2 or 2x2
                // DXT1 level still contains one full eight-byte block.
                const std::size_t bytesPerBlock = X_Format == 0x0C ? 8u : 16u;
                dwCompressedBytesPerBlock = static_cast<DWORD>(bytesPerBlock);
                dwCompressedSize = static_cast<DWORD>(
                    cxbx::d3d::CompressedTextureLevelSize(
                        dwWidth, dwHeight, bytesPerBlock));

                dwMipMapLevels = (pPixelContainer->Format & X_D3DFORMAT_MIPMAP_MASK) >> X_D3DFORMAT_MIPMAP_SHIFT;
            }
            else
            {
                // Degrade instead of dying: create a swizzled-32-bit-sized
                // texture and skip the pixel copy (the source interpretation
                // is unknown). One wrong texture beats a dead process.
                printf("*Warning* Register: unhandled format 0x%.08X, texture left blank\n", X_Format);

                Format = D3DFMT_A8R8G8B8;
                bSwizzled = TRUE;
                dwWidth  = 1 << ((pPixelContainer->Format & X_D3DFORMAT_USIZE_MASK) >> X_D3DFORMAT_USIZE_SHIFT);
                dwHeight = 1 << ((pPixelContainer->Format & X_D3DFORMAT_VSIZE_MASK) >> X_D3DFORMAT_VSIZE_SHIFT);
                dwDepth  = 1 << ((pPixelContainer->Format & X_D3DFORMAT_PSIZE_MASK) >> X_D3DFORMAT_PSIZE_SHIFT);
                dwPitch  = dwWidth*4;
                dwBPP = 4;
                pBase = NULL;   // the source-validation guard below skips the copy
            }

            const DWORD dwSourceWidth = dwWidth;
            const DWORD dwSourceHeight = dwHeight;
            const DWORD dwSourceMipMapLevels =
                bCompressed && dwMipMapLevels != 0 ? dwMipMapLevels : 1;
            const DWORD dwCompressedMipChainSize = bCompressed
                ? static_cast<DWORD>(cxbx::d3d::CompressedTextureMipChainSize(
                      dwSourceWidth, dwSourceHeight, dwCompressedBytesPerBlock,
                      dwSourceMipMapLevels))
                : 0;

            static int s_TexTraceEnabled = -1;
            if(s_TexTraceEnabled < 0)
                s_TexTraceEnabled = (getenv("CXBX_TEX_TRACE") != NULL) ? 1 : 0;
            if(s_TexTraceEnabled == 1)
            {
                printf("TEX| register %s=0x%08lX xfmt=0x%02lX host=0x%08lX %lux%lux%lu "
                       "pitch=%lu bpp=%lu layout=%s mips=%lu data=0x%08lX\n",
                       dwCommonType == X_D3DCOMMON_TYPE_SURFACE ? "surface" : "texture",
                       reinterpret_cast<unsigned long>(pResource),
                       static_cast<unsigned long>(X_Format),
                       static_cast<unsigned long>(Format),
                       static_cast<unsigned long>(dwWidth),
                       static_cast<unsigned long>(dwHeight),
                       static_cast<unsigned long>(dwDepth),
                       static_cast<unsigned long>(dwPitch),
                       // dwBPP stays uninitialized on the compressed path
                       static_cast<unsigned long>(bCompressed ? 0 : dwBPP),
                       bSwizzled ? "swizzled" : (bCompressed ? "compressed" : "linear"),
                       static_cast<unsigned long>(dwMipMapLevels),
                       reinterpret_cast<unsigned long>(pBase));
            }

            // ******************************************************************
            // * Create the happy little texture
            // ******************************************************************
            if(dwCommonType == X_D3DCOMMON_TYPE_SURFACE)
            {
                hRet = g_pD3DDevice8->CreateImageSurface(dwWidth, dwHeight, Format, &pResource->EmuSurface8);
            }
            else
            {
                // TODO: HACK: Figure out why this is necessary!
                // TODO: This is necessary for DXT1 textures at least (4x4 blocks minimum)
                if(dwWidth < 4)
                {
                    printf("*Warning* expanding texture width (%d->4)\n", dwWidth);
                    dwWidth = 4;
                    
                    dwMipMapLevels = 3;
                }

                if(dwHeight < 4)
                {
                    printf("*Warning* expanding texture height (%d->4)\n", dwHeight);
                    dwHeight = 4;

                    dwMipMapLevels = 3;
                }

                #ifdef _DEBUG_TRACE
                printf("CreateTexture(%d, %d, %d, 0, %d, D3DPOOL_MANAGED, 0x%.08X)\n", dwWidth, dwHeight,
                    dwMipMapLevels, Format, &pResource->EmuTexture8);
                #endif

                hRet = g_pD3DDevice8->CreateTexture
                (
                    dwWidth, dwHeight, dwMipMapLevels, 0, Format,
                    D3DPOOL_MANAGED, &pResource->EmuTexture8
                );

                if(FAILED(hRet))
                {
                    EmuWarning("Resource_Register: CreateTexture(%lu, %lu, %lu, format=0x%.08lX) failed (0x%.08lX); using a blank placeholder",
                               dwWidth,
                               dwHeight,
                               dwMipMapLevels,
                               Format,
                               hRet);

                    pResource->EmuTexture8 = NULL;
                    hRet = g_pD3DDevice8->CreateTexture(
                        4,
                        4,
                        1,
                        0,
                        D3DFMT_A8R8G8B8,
                        D3DPOOL_MANAGED,
                        &pResource->EmuTexture8);
                    if(FAILED(hRet) || pResource->EmuTexture8 == NULL)
                    {
                        EmuWarning("Resource_Register: placeholder texture creation failed (0x%.08lX); resource left unbacked",
                                   hRet);
                        break;
                    }

                    dwWidth = 4;
                    dwHeight = 4;
                    dwMipMapLevels = 1;
                    dwPitch = 4 * sizeof(DWORD);
                    dwBPP = sizeof(DWORD);
                    bSwizzled = FALSE;
                    bCompressed = FALSE;
                    pBase = NULL;
                }
            }

            D3DLOCKED_RECT LockedRect;

            // ******************************************************************
            // * Copy over data (deswizzle if necessary)
            // ******************************************************************
            if(dwCommonType == X_D3DCOMMON_TYPE_SURFACE)
                hRet = pResource->EmuSurface8->LockRect(&LockedRect, NULL, 0);
            else
                hRet = pResource->EmuTexture8->LockRect(0, &LockedRect, NULL, 0);

            if(FAILED(hRet))
            {
                EmuWarning("Resource_Register: texture level 0 lock failed (0x%.08lX)",
                           hRet);
                break;
            }

            RECT  iRect  = {0,0,0,0};
            POINT iPoint = {0,0};

            // Validate the source before copying: titles register the odd
            // degenerate resource whose data pointer/offset doesn't resolve
            // (Turok Evolution's frontend registers a 1x2 DXT1 whose source
            // lands in unmapped memory). A skipped copy leaves one texture
            // blank; an unguarded copy killed the process through the title's
            // last-resort exception handler.
            DWORD dwSourceSize = bCompressed ? dwCompressedMipChainSize
                                             : (dwPitch != 0 ? dwPitch : dwSourceWidth * dwBPP) *
                                                   dwSourceHeight;

            std::vector<BYTE> sourceCopy;
            bool copiedSource = EmuD3DCopyReadableRange(pBase, dwSourceSize, sourceCopy);
            if(!copiedSource && dwUnmaskedDataAddress != reinterpret_cast<DWORD>(pBase))
            {
                // Register may receive a directly readable host alias that does not
                // belong to a tracked Xbox allocation. Preserve that exact alias
                // before declaring its masked UMA address unreadable.
                copiedSource = EmuD3DCopyReadableRange(
                    reinterpret_cast<const void*>(dwUnmaskedDataAddress), dwSourceSize,
                    sourceCopy);
            }
            if(!copiedSource)
            {
                printf("*Warning* Register skipped copying a texture with an unreadable source "
                       "(resource=0x%.08lX common=0x%.08lX base_arg=0x%.08lX data=0x%.08lX "
                       "resolved=0x%.08lX format=0x%.08lX size=0x%.08lX copy=0x%lX)\n",
                       reinterpret_cast<DWORD>(pThis), pResource->Common,
                       dwRegisterBase, dwDataOffset, reinterpret_cast<DWORD>(pBase),
                       pPixelContainer->Format, pPixelContainer->Size, dwSourceSize);
            }
            else if(bSwizzled)
            {
                XTL::EmuXGUnswizzleRect(
                    sourceCopy.data(), dwSourceWidth, dwSourceHeight, dwDepth, LockedRect.pBits,
                    LockedRect.Pitch, iRect, iPoint, dwBPP);
            }
            else if(bCompressed)
            {
                const DWORD rowBytes =
                    ((dwSourceWidth + 3) / 4) * dwCompressedBytesPerBlock;
                const DWORD blockRows = (dwSourceHeight + 3) / 4;
                BYTE* destination = static_cast<BYTE*>(LockedRect.pBits);
                const BYTE* source = sourceCopy.data();
                for(DWORD row = 0; row < blockRows; ++row)
                {
                    memcpy(destination, source, rowBytes);
                    destination += LockedRect.Pitch;
                    source += rowBytes;
                }
            }
            else
            {
                BYTE *pDest = (BYTE*)LockedRect.pBits;
                const BYTE *pSrc = sourceCopy.data();

                if((DWORD)LockedRect.Pitch == dwPitch && dwPitch == dwSourceWidth * dwBPP)
                    memcpy(pDest, pSrc, dwSourceWidth * dwSourceHeight * dwBPP);
                else
                {
                    // TODO: Faster copy (maybe unnecessary)
                    for(DWORD v = 0; v < dwSourceHeight; v++)
                    {
                        memcpy(pDest, pSrc, dwSourceWidth * dwBPP);

                        pDest += LockedRect.Pitch;
                        pSrc  += dwPitch;
                    }
                }
            }

            // Hash-named dump of the CONVERTED level 0 -- exactly what the host
            // device will sample, after unswizzle/pitch adjustment.
            static int s_TextureDumpEnabled = -1;
            if(s_TextureDumpEnabled < 0)
                s_TextureDumpEnabled = (getenv("CXBX_D3D_TEXTURE_DUMP") != NULL) ? 1 : 0;
            if(s_TextureDumpEnabled == 1)
            {
                static LONG s_TextureDumpsRemaining = 256;
                if(s_TextureDumpsRemaining > 0)
                {
                    const DWORD sourceHash = sourceCopy.empty()
                                                 ? 0
                                                 : EmuD3DHashBytes(sourceCopy.data(), sourceCopy.size());
                    char fileName[96];
                    snprintf(fileName, sizeof(fileName), "%s_%08lX_x%02lX_%lux%lu.bmp",
                             dwCommonType == X_D3DCOMMON_TYPE_SURFACE ? "surf" : "tex",
                             static_cast<unsigned long>(sourceHash),
                             static_cast<unsigned long>(X_Format),
                             static_cast<unsigned long>(dwWidth),
                             static_cast<unsigned long>(dwHeight));
                    char path[MAX_PATH * 2];
                    EmuD3DDebugDumpPath(path, sizeof(path), "cxbx_tex", fileName);
                    if(EmuD3DWriteBmp(path, dwWidth, dwHeight, LockedRect.Pitch,
                                      LockedRect.pBits, Format))
                    {
                        // The cap only counts written files, so a DXT-heavy
                        // title can't exhaust it with skipped dumps.
                        s_TextureDumpsRemaining--;
                        printf("TEX| dumped %s\n", path);
                    }
                    else
                    {
                        printf("TEX| dump skipped (host format 0x%08lX not BMP-expandable) %s\n",
                               static_cast<unsigned long>(Format), fileName);
                    }
                }
            }

            if(dwCommonType == X_D3DCOMMON_TYPE_SURFACE)
                pResource->EmuSurface8->UnlockRect();
            else
                pResource->EmuTexture8->UnlockRect(0);

            if(bCompressed && dwCommonType == X_D3DCOMMON_TYPE_TEXTURE &&
               !sourceCopy.empty())
            {
                const DWORD hostLevelCount = pResource->EmuTexture8->GetLevelCount();
                const DWORD copyLevelCount =
                    (std::min)(dwSourceMipMapLevels, hostLevelCount);
                DWORD sourceOffset = dwCompressedSize;
                DWORD levelWidth = dwSourceWidth > 1 ? dwSourceWidth / 2 : 1;
                DWORD levelHeight = dwSourceHeight > 1 ? dwSourceHeight / 2 : 1;
                for(DWORD level = 1; level < copyLevelCount; ++level)
                {
                    D3DLOCKED_RECT levelLock{};
                    const HRESULT levelResult =
                        pResource->EmuTexture8->LockRect(level, &levelLock, NULL, 0);
                    if(FAILED(levelResult))
                    {
                        EmuWarning("Resource_Register: compressed texture level %lu lock "
                                   "failed (0x%.08lX)", level, levelResult);
                        break;
                    }

                    const DWORD rowBytes =
                        ((levelWidth + 3) / 4) * dwCompressedBytesPerBlock;
                    const DWORD blockRows = (levelHeight + 3) / 4;
                    BYTE* destination = static_cast<BYTE*>(levelLock.pBits);
                    const BYTE* source = sourceCopy.data() + sourceOffset;
                    for(DWORD row = 0; row < blockRows; ++row)
                    {
                        memcpy(destination, source, rowBytes);
                        destination += levelLock.Pitch;
                        source += rowBytes;
                    }
                    pResource->EmuTexture8->UnlockRect(level);

                    sourceOffset += static_cast<DWORD>(
                        cxbx::d3d::CompressedTextureLevelSize(
                            levelWidth, levelHeight, dwCompressedBytesPerBlock));
                    levelWidth = levelWidth > 1 ? levelWidth / 2 : 1;
                    levelHeight = levelHeight > 1 ? levelHeight / 2 : 1;
                }
            }
        }
        break;

        default:
            EmuCleanup("IDirect3DResource8::Register -> Common Type 0x%.08X not yet supported", dwCommonType);
    }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        EmuWarning("EmuIDirect3DResource8_Register: access violation (resource at 0x%.08X) -- left unbacked", (DWORD)pThis);
        hRet = D3D_OK;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DResource8_AddRef
// ******************************************************************
ULONG WINAPI XTL::EmuIDirect3DResource8_AddRef
(
    X_D3DResource      *pThis
)
{
    D3D_TRACE("Resource_AddRef");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DResource8_AddRef\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis);
    }
    #endif

    ULONG uRet = 0;

    EmuYuy2TextureInfo *pYuy2 = EmuFindYuy2Texture(pThis);
    if(pYuy2 != NULL)
    {
        uRet = ++pYuy2->RefCount;
        EmuSwapFS();   // XBox FS
        return uRet;
    }

    if((pThis->Common & X_D3DCOMMON_TYPE_MASK) == X_D3DCOMMON_TYPE_PUSHBUFFER &&
       (pThis->Common & X_D3DCOMMON_D3DCREATED) != 0)
    {
        const ULONG referenceCount = pThis->Common & X_D3DCOMMON_REFCOUNT_MASK;
        if(referenceCount < X_D3DCOMMON_REFCOUNT_MASK)
        {
            uRet = referenceCount + 1;
            pThis->Common = (pThis->Common & ~X_D3DCOMMON_REFCOUNT_MASK) | uRet;
        }

        EmuSwapFS(); // XBox FS
        return uRet;
    }

    IDirect3DResource8 *pResource8 = pThis->EmuResource8;

    if(pThis->Lock == 0x8000BEEF)
        uRet = ++pThis->Lock;
    else if(pResource8 != 0)
        uRet = pResource8->AddRef();

    EmuSwapFS();   // XBox FS

    return uRet;
}

// ******************************************************************
// * func: EmuIDirect3DResource8_Release
// ******************************************************************
ULONG WINAPI XTL::EmuIDirect3DResource8_Release
(
    X_D3DResource      *pThis
)
{
    D3D_TRACE("Resource_Release");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DResource8_Release\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis);
    }
    #endif

    ULONG uRet = 0;

    EmuYuy2TextureInfo *pYuy2 = EmuFindYuy2Texture(pThis);
    if(pYuy2 != NULL)
    {
        if(pYuy2->RefCount != 0)
            uRet = --pYuy2->RefCount;
        if(uRet == 0)
        {
            // Report how far the decoder wrote past the nominal Pitch*Height
            // (the slop tail absorbed it; see EMU_YUY2_TAIL_SLOP).
            DWORD dwOverrun = 0;
            for(DWORD i = EMU_YUY2_TAIL_SLOP; i > 0; i--)
            {
                if(pYuy2->pPixels[pYuy2->DataSize + i - 1] != EMU_YUY2_TAIL_FILL)
                {
                    dwOverrun = i;
                    break;
                }
            }
            if(dwOverrun != 0)
                printf("EmuD3D8 (0x%X): YUY2 texture %ux%u was overwritten 0x%X bytes past its %u-byte buffer.\n",
                       GetCurrentThreadId(), pYuy2->Width, pYuy2->Height, dwOverrun, pYuy2->DataSize);

            if(g_pYuvConvertedSource == pYuy2)
                g_pYuvConvertedSource = NULL;
            delete[] pYuy2->pPixels;
            memset(pYuy2, 0, sizeof(*pYuy2));
        }

        EmuSwapFS();   // XBox FS
        return uRet;
    }

    if((pThis->Common & X_D3DCOMMON_TYPE_MASK) == X_D3DCOMMON_TYPE_PUSHBUFFER &&
       (pThis->Common & X_D3DCOMMON_D3DCREATED) != 0)
    {
        X_D3DPushBuffer* pushBuffer = static_cast<X_D3DPushBuffer*>(pThis);
        const ULONG referenceCount = pThis->Common & X_D3DCOMMON_REFCOUNT_MASK;
        if(referenceCount > 1)
        {
            uRet = referenceCount - 1;
            pThis->Common = (pThis->Common & ~X_D3DCOMMON_REFCOUNT_MASK) | uRet;
        }
        else
        {
            EmuDiscardRecordedPushBuffer(pushBuffer);
            delete[] reinterpret_cast<BYTE*>(pushBuffer->Data);
            delete pushBuffer;
        }

        EmuSwapFS(); // XBox FS
        return uRet;
    }

    IDirect3DResource8 *pResource8 = pThis->EmuResource8;

    if(pThis->Lock == 0x8000BEEF)
    {
        delete[] (PVOID)pThis->Data;
        uRet = --pThis->Lock;
    }
    else if(pResource8 != 0)
    {
        uRet = pResource8->Release();

        if(uRet == 0)
        {
            #ifdef _DEBUG_TRACE
            printf("EmuIDirect3DResource8_Release (0x%X): Cleaned up a Resource!\n", GetCurrentThreadId());
            #endif
            if((pThis->Common & X_D3DCOMMON_TYPE_MASK) == X_D3DCOMMON_TYPE_VERTEXBUFFER)
            {
                EmuDiscardRegisteredVertexBuffer(static_cast<X_D3DVertexBuffer*>(pThis));
            }
            EmuDiscardSwizzledTexture(
                reinterpret_cast<IDirect3DBaseTexture8*>(pResource8));
            delete pThis;
        }
    }

    EmuSwapFS();   // XBox FS

    return uRet;
}

// ******************************************************************
// * func: EmuIDirect3DResource8_IsBusy
// ******************************************************************
BOOL WINAPI XTL::EmuIDirect3DResource8_IsBusy
(
    X_D3DResource      *pThis
)
{
    D3D_TRACE("Resource_IsBusy");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DResource8_IsBusy\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis);
    }
    #endif

    IDirect3DResource8 *pResource8 = pThis->EmuResource8;

    // I guess we arent doing anything, just return false..

    EmuSwapFS();   // XBox FS

    return FALSE;
}

// ******************************************************************
// * func: EmuGet2DSurfaceDesc
// ******************************************************************
VOID WINAPI XTL::EmuGet2DSurfaceDesc
(
    X_D3DPixelContainer *pPixelContainer,
    DWORD                dwLevel,
    X_D3DSURFACE_DESC   *pDesc
)
{
    D3D_TRACE("Get2DSurfaceDesc");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuGet2DSurfaceDesc\n"
               "(\n"
               "   pPixelContainer     : 0x%.08X\n"
               "   dwLevel             : 0x%.08X\n"
               "   pDesc               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pPixelContainer, dwLevel, pDesc);
    }
    #endif

    EmuVerifyResourceIsRegistered(pPixelContainer);

    D3DSURFACE_DESC SurfaceDesc;

    ZeroMemory(&SurfaceDesc, sizeof(SurfaceDesc));

    HRESULT hRet;

    if(dwLevel == 0xFEFEFEFE)
        hRet = pPixelContainer->EmuSurface8->GetDesc(&SurfaceDesc);
    else
        hRet = pPixelContainer->EmuTexture8->GetLevelDesc(dwLevel, &SurfaceDesc);

    // ******************************************************************
    // * Rearrange into windows format (remove D3DPool)
    // ******************************************************************
    {
        // Convert Format (PC->Xbox)
        pDesc->Format = EmuPC2XB_D3DFormat(SurfaceDesc.Format);
        pDesc->Type   = SurfaceDesc.Type;

        if(pDesc->Type > 7)
            EmuCleanup("EmuGet2DSurfaceDesc: pDesc->Type > 7");

        pDesc->Usage  = SurfaceDesc.Usage;
        pDesc->Size   = SurfaceDesc.Size;

        // TODO: Convert from Xbox to PC!!
        if(SurfaceDesc.MultiSampleType == D3DMULTISAMPLE_NONE)
            pDesc->MultiSampleType = (XTL::D3DMULTISAMPLE_TYPE)0x0011;
        else
            EmuCleanup("EmuGet2DSurfaceDesc Unknown Multisample format! (%d)", SurfaceDesc.MultiSampleType);

        pDesc->Width  = SurfaceDesc.Width;
        pDesc->Height = SurfaceDesc.Height;
    }

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuGet2DSurfaceDescD
// ******************************************************************
VOID WINAPI XTL::EmuGet2DSurfaceDescD
(
    X_D3DPixelContainer *pPixelContainer,
    X_D3DSURFACE_DESC   *pDesc
)
{
    D3D_TRACE("Get2DSurfaceDescD");
    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        printf("EmuD3D8 (0x%X): EmuGet2DSurfaceDescD\n"
               "(\n"
               "   pPixelContainer     : 0x%.08X\n"
               "   pDesc               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pPixelContainer, pDesc);
        EmuSwapFS();   // Xbox FS
    }
    #endif

    EmuGet2DSurfaceDesc(pPixelContainer, 0xFEFEFEFE, pDesc);

    return;
}

// ******************************************************************
// * func: EmuIDirect3DSurface8_GetDesc
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DSurface8_GetDesc
(
    X_D3DResource      *pThis,
    X_D3DSURFACE_DESC  *pDesc
)
{
    D3D_TRACE("Surface_GetDesc");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DSurface8_GetDesc\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               "   pDesc               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pDesc);
    }
    #endif

    HRESULT hRet;

    EmuYuy2TextureInfo *pYuy2 = EmuFindYuy2Texture(pThis);
    if(pYuy2 != NULL)
    {
        pDesc->Format = EmuPC2XB_D3DFormat(D3DFMT_YUY2);
        pDesc->Height = pYuy2->Height;
        pDesc->Width  = pYuy2->Width;
        pDesc->MultiSampleType = (D3DMULTISAMPLE_TYPE)0;
        pDesc->Size   = pYuy2->Pitch * pYuy2->Height;
        pDesc->Type   = D3DRTYPE_SURFACE;
        pDesc->Usage  = 0;

        hRet = D3D_OK;
    }
    else
    {
        EmuVerifyResourceIsRegistered(pThis);

        IDirect3DSurface8 *pSurface8 = pThis->EmuSurface8;

        D3DSURFACE_DESC SurfaceDesc;

        hRet = pSurface8->GetDesc(&SurfaceDesc);

        // ******************************************************************
        // * Rearrange into windows format (remove D3DPool)
        // ******************************************************************
        {
            // Convert Format (PC->Xbox)
            pDesc->Format = EmuPC2XB_D3DFormat(SurfaceDesc.Format);
            pDesc->Type   = SurfaceDesc.Type;

            if(pDesc->Type > 7)
                EmuCleanup("EmuIDirect3DSurface8_GetDesc: pDesc->Type > 7");

            pDesc->Usage  = SurfaceDesc.Usage;
            pDesc->Size   = SurfaceDesc.Size;

            // TODO: Convert from Xbox to PC!!
            if(SurfaceDesc.MultiSampleType == D3DMULTISAMPLE_NONE)
                pDesc->MultiSampleType = (XTL::D3DMULTISAMPLE_TYPE)0x0011;
            else
                EmuCleanup("EmuIDirect3DSurface8_GetDesc Unknown Multisample format! (%d)", SurfaceDesc.MultiSampleType);

            pDesc->Width  = SurfaceDesc.Width;
            pDesc->Height = SurfaceDesc.Height;
        }
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DSurface8_LockRect
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DSurface8_LockRect
(
    X_D3DResource      *pThis,
    D3DLOCKED_RECT     *pLockedRect,
    CONST RECT         *pRect,
    DWORD               Flags
)
{
    D3D_TRACE("Surface_LockRect");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DSurface8_LockRect\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               "   pLockedRect         : 0x%.08X\n"
               "   pRect               : 0x%.08X\n"
               "   Flags               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pLockedRect, pRect, Flags);
            }
    #endif

    HRESULT hRet;

    EmuYuy2TextureInfo *pYuy2 = EmuFindYuy2Texture(pThis);
    if(pYuy2 != NULL)
    {
        hRet = EmuLockYuy2Texture(pYuy2, pLockedRect, pRect);
    }
    else
    {
        EmuVerifyResourceIsRegistered(pThis);

        IDirect3DSurface8 *pSurface8 = pThis->EmuSurface8;

        DWORD NewFlags = 0;

        if(Flags & EMU_D3DLOCK_READONLY)
            NewFlags |= D3DLOCK_READONLY;

        if(Flags & !(EMU_D3DLOCK_READONLY | EMU_D3DLOCK_TILED))
            EmuCleanup("EmuIDirect3DSurface8_LockRect: Unknown Flags! (0x%.08X)", Flags);

        if(Flags & EMU_D3DLOCK_TILED)
        {
            hRet = EmuLockTiledSurface(pThis, pLockedRect, pRect, Flags);
            if(FAILED(hRet))
                printf("*Warning* D3DLOCK_TILED failed, falling back to linear LockRect\n");
        }
        else
        {
            hRet = E_FAIL;
        }

        if(!(Flags & EMU_D3DLOCK_TILED) || FAILED(hRet))
        {
            EmuFlushTiledSurfaceLock(pThis);

            // Remove old lock(s)
            pSurface8->UnlockRect();

            hRet = pSurface8->LockRect(pLockedRect, pRect, NewFlags);

            if(FAILED(hRet))
                printf("*Warning* LockRect failed\n");
        }
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DBaseTexture8_GetLevelCount
// ******************************************************************
DWORD WINAPI XTL::EmuIDirect3DBaseTexture8_GetLevelCount
(
    X_D3DBaseTexture   *pThis
)
{
    D3D_TRACE("BaseTexture_GetLevelCount");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DBaseTexture8_GetLevelCount\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis);
    }
    #endif

    if(EmuFindYuy2Texture(pThis) != NULL)
    {
        EmuSwapFS();   // XBox FS
        return 1;
    }

    EmuVerifyResourceIsRegistered(pThis);

    IDirect3DBaseTexture8 *pBaseTexture8 = pThis->EmuBaseTexture8;

    DWORD dwRet = pBaseTexture8->GetLevelCount();

    EmuSwapFS();   // XBox FS

    return dwRet;
}

// ******************************************************************
// * func: EmuIDirect3DTexture8_GetSurfaceLevel2
// ******************************************************************
XTL::X_D3DResource * WINAPI XTL::EmuIDirect3DTexture8_GetSurfaceLevel2
(
    X_D3DTexture   *pThis,
    UINT            Level
)
{
    D3D_TRACE("Texture_GetSurfaceLevel2");
    X_D3DSurface *pSurfaceLevel;

    // In a special situation, we are actually returning a memory ptr with high bit set
    if(EmuFindYuy2Texture(pThis) != NULL)
    {
        EmuIDirect3DResource8_AddRef(pThis);
        return pThis;
    }

    EmuIDirect3DTexture8_GetSurfaceLevel(pThis, Level, &pSurfaceLevel);

    return pSurfaceLevel;
}

// ******************************************************************
// * func: EmuIDirect3DTexture8_LockRect
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DTexture8_LockRect
(
    X_D3DTexture   *pThis,
    UINT            Level,
    D3DLOCKED_RECT *pLockedRect,
    CONST RECT     *pRect,
    DWORD           Flags
)
{
    D3D_TRACE("Texture_LockRect");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DTexture8_LockRect\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               "   Level               : 0x%.08X\n"
               "   pLockedRect         : 0x%.08X\n"
               "   pRect               : 0x%.08X\n"
               "   Flags               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, Level, pLockedRect, pRect, Flags);
    }
    #endif

    EmuYuy2TextureInfo *pYuy2 = EmuFindYuy2Texture(pThis);
    if(pYuy2 != NULL)
    {
        HRESULT hRet = (Level == 0) ? EmuLockYuy2Texture(pYuy2, pLockedRect, pRect)
                                    : D3DERR_INVALIDCALL;
        EmuSwapFS();   // XBox FS
        return hRet;
    }

    EmuVerifyResourceIsRegistered(pThis);

    IDirect3DTexture8 *pTexture8 = pThis->EmuTexture8;

    DWORD NewFlags = 0;

    if(Flags & 0x80)
        NewFlags |= D3DLOCK_READONLY;

    if(Flags & !(0x80 | 0x40))
        EmuCleanup("EmuIDirect3DTexture8_LockRect: Unknown Flags! (0x%.08X)", Flags);

    // Commit any pending Morton-order write before releasing its lock.
    EmuCommitSwizzledTexture(pTexture8);

    // Remove old lock(s)
    pTexture8->UnlockRect(Level);

    HRESULT hRet = pTexture8->LockRect(Level, pLockedRect, pRect, NewFlags);

    // A writable full-level lock of a swizzled-format texture receives the
    // title's pre-swizzled (Morton-order) texels; remember the lock so the
    // block can be unswizzled in place when the lock is flushed.
    if(SUCCEEDED(hRet) && pRect == NULL && (NewFlags & D3DLOCK_READONLY) == 0)
    {
        EmuSwizzledTextureInfo *pSwizzled = EmuFindSwizzledTexture(pTexture8);
        if(pSwizzled != NULL && Level < EMU_SWIZZLED_TEXTURE_MAX_LEVELS)
        {
            pSwizzled->dwDirtyLevels |= 1u << Level;
            pSwizzled->Levels[Level].pBits = pLockedRect->pBits;
            pSwizzled->Levels[Level].Pitch = pLockedRect->Pitch;
        }
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DTexture8_GetSurfaceLevel
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DTexture8_GetSurfaceLevel
(
    X_D3DTexture       *pThis,
    UINT                Level,
    X_D3DSurface      **ppSurfaceLevel
)
{
    D3D_TRACE("Texture_GetSurfaceLevel");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DTexture8_GetSurfaceLevel\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               "   Level               : 0x%.08X\n"
               "   ppSurfaceLevel      : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, Level, ppSurfaceLevel);
    }
    #endif

    HRESULT hRet;

    // if highest bit is set, this is actually a raw memory pointer (for YUY2 simulation)
    if(EmuFindYuy2Texture(pThis) != NULL)
    {
        *ppSurfaceLevel = (X_D3DSurface*)pThis;
        EmuIDirect3DResource8_AddRef(pThis);
        hRet = D3D_OK;
    }
    else
    {
        EmuVerifyResourceIsRegistered(pThis);

        IDirect3DTexture8 *pTexture8 = pThis->EmuTexture8;

        *ppSurfaceLevel = new X_D3DSurface();

        hRet = pTexture8->GetSurfaceLevel(Level, &((*ppSurfaceLevel)->EmuSurface8));
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DVolumeTexture8_LockBox
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DVolumeTexture8_LockBox
(
    X_D3DVolumeTexture *pThis,
    UINT                Level,
    D3DLOCKED_BOX      *pLockedVolume,
    CONST D3DBOX       *pBox,
    DWORD               Flags
)
{
    D3D_TRACE("VolumeTexture_LockBox");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DVolumeTexture8_LockBox\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               "   Level               : 0x%.08X\n"
               "   pLockedVolume       : 0x%.08X\n"
               "   pBox                : 0x%.08X\n"
               "   Flags               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, Level, pLockedVolume, pBox, Flags);
    }
    #endif

    EmuVerifyResourceIsRegistered(pThis);

    IDirect3DVolumeTexture8 *pVolumeTexture8 = pThis->EmuVolumeTexture8;

    HRESULT hRet = pVolumeTexture8->LockBox(Level, pLockedVolume, pBox, Flags);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DCubeTexture8_LockRect
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DCubeTexture8_LockRect
(
    X_D3DCubeTexture   *pThis,
    D3DCUBEMAP_FACES    FaceType,
    UINT                Level,
    D3DLOCKED_RECT     *pLockedBox,
    CONST RECT         *pRect,
    DWORD               Flags
)
{
    D3D_TRACE("CubeTexture_LockRect");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DCubeTexture8_LockRect\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               "   FaceType            : 0x%.08X\n"
               "   Level               : 0x%.08X\n"
               "   pLockedBox          : 0x%.08X\n"
               "   pRect               : 0x%.08X\n"
               "   Flags               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, FaceType, Level, pLockedBox, pRect, Flags);
    }
    #endif

    EmuVerifyResourceIsRegistered(pThis);

    IDirect3DCubeTexture8 *pCubeTexture8 = pThis->EmuCubeTexture8;

    HRESULT hRet = pCubeTexture8->LockRect(FaceType, Level, pLockedBox, pRect, Flags);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_Release
// ******************************************************************
ULONG WINAPI XTL::EmuIDirect3DDevice8_Release()
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("Release");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_Release();\n", GetCurrentThreadId());
    }
    #endif

    EmuCleanup("Release should use proxy...");
    ULONG uRet = g_pD3DDevice8->Release();

    EmuSwapFS();   // XBox FS

    return uRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateVertexBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateVertexBuffer
(
    UINT                Length,
    DWORD               Usage,
    DWORD               FVF,
    D3DPOOL             Pool,
    X_D3DVertexBuffer **ppVertexBuffer
)
{
    D3D_TRACE("CreateVertexBuffer");
    *ppVertexBuffer = EmuIDirect3DDevice8_CreateVertexBuffer2(Length);

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateVertexBuffer2
// ******************************************************************
XTL::X_D3DVertexBuffer* WINAPI XTL::EmuIDirect3DDevice8_CreateVertexBuffer2
(
    UINT Length
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("CreateVertexBuffer2");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateVertexBuffer2\n"
               "(\n"
               "   Length              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Length);
    }
    #endif

    X_D3DVertexBuffer *pD3DVertexBuffer = new X_D3DVertexBuffer();

    IDirect3DVertexBuffer8 *ppVertexBuffer=NULL;

    HRESULT hRet = g_pD3DDevice8->CreateVertexBuffer
    (
        Length, 
        0,
        0,
        D3DPOOL_MANAGED, 
        &pD3DVertexBuffer->EmuVertexBuffer8
    );

    EmuSwapFS();   // XBox FS

    return pD3DVertexBuffer;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_EnableOverlay
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_EnableOverlay
(
    BOOL Enable
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("EnableOverlay");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_EnableOverlay\n"
               "(\n"
               "   Enable              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Enable);
    }
    #endif

    // Stop compositing the software-overlay frame once the title turns the
    // overlay off (end of a video).
    if(!Enable)
    {
        g_pOverlayFrameTexture = NULL;
        g_OverlayFrameGeneration.store(0, std::memory_order_relaxed);
        g_NextOverlayUpdate.QuadPart = 0;
        g_NextSwapPresent.QuadPart = 0;
    }

    if(g_bSupportsYUY2)
    {
        if(Enable)
        {
            // ******************************************************************
            // * Initialize Primary Surface
            // ******************************************************************
            {
                DDSURFACEDESC2 ddsd2;

                ZeroMemory(&ddsd2, sizeof(ddsd2));

                ddsd2.dwSize = sizeof(ddsd2);
                ddsd2.dwFlags = DDSD_CAPS;
                ddsd2.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_VIDEOMEMORY;
		            
	            HRESULT hRet = g_pDD7->CreateSurface(&ddsd2, &g_pDDSPrimary, 0);

                if(FAILED(hRet))
                    EmuCleanup("Could not create primary surface");
            }

            // ******************************************************************
            // * Initialize Overlay Surface
            // ******************************************************************
            {
                DDSURFACEDESC2 ddsd2;

                ZeroMemory(&ddsd2, sizeof(ddsd2));

                ddsd2.dwSize = sizeof(ddsd2);
                ddsd2.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
                ddsd2.ddsCaps.dwCaps = DDSCAPS_OVERLAY;
                ddsd2.dwWidth = g_dwOverlayW;
                ddsd2.dwHeight = g_dwOverlayH; 
                ddsd2.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
                ddsd2.ddpfPixelFormat.dwFlags = DDPF_FOURCC;
                ddsd2.ddpfPixelFormat.dwFourCC = MAKEFOURCC('Y','U','Y','2');

                HRESULT hRet = g_pDD7->CreateSurface(&ddsd2, &g_pDDSOverlay7, NULL);

                if(FAILED(hRet))
                    EmuCleanup("Could not create overlay surface");
            }
        }
        else
        {
            // Cleanup Primary/Overlay Surfaces
            if(g_pDDSOverlay7 != 0)
            {
                g_pDDSOverlay7->Release();
                g_pDDSOverlay7 = 0;
            }
        
            if(g_pDDSPrimary != 0)
            {
                g_pDDSPrimary->Release();
                g_pDDSPrimary = 0;
            }
        }
    }

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_UpdateOverlay
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_UpdateOverlay
(
    X_D3DSurface *pSurface,
    CONST RECT   *SrcRect,
    CONST RECT   *DstRect,
    BOOL          EnableColorKey,
    D3DCOLOR      ColorKey
)
{
    D3D_TRACE("UpdateOverlay");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_UpdateOverlay\n"
               "(\n"
               "   pSurface            : 0x%.08X\n"
               "   SrcRect             : 0x%.08X\n"
               "   DstRect             : 0x%.08X\n"
               "   EnableColorKey      : 0x%.08X\n"
               "   ColorKey            : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pSurface, SrcRect, DstRect, EnableColorKey, ColorKey);
    }
    #endif

    // ******************************************************************
    // * manually copy data over to overlay
    // ******************************************************************
    if(g_bSupportsYUY2)
    {
        D3DSURFACE_DESC SurfaceDesc;
        DDSURFACEDESC2  ddsd2;
        D3DLOCKED_RECT  LockedRect;

        pSurface->EmuSurface8->GetDesc(&SurfaceDesc);

        ZeroMemory(&ddsd2, sizeof(ddsd2));

        ddsd2.dwSize = sizeof(ddsd2);

        g_pDDSOverlay7->Lock(NULL, &ddsd2, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
        pSurface->EmuSurface8->UnlockRect();

        pSurface->EmuSurface8->LockRect(&LockedRect, NULL, NULL);

        // Copy Data
        {
            char *pDest = (char*)ddsd2.lpSurface;
            char *pSour = (char*)LockedRect.pBits;

            int w = SurfaceDesc.Width;
            int h = SurfaceDesc.Height;

            // TODO: sucker the game into rendering directly to the overlay :]
            for(int y=0;y<h;y++)
            {
                memcpy(pDest, pSour, w*2);

                pDest += ddsd2.lPitch;
                pSour += LockedRect.Pitch;
            }
        }

        g_pDDSOverlay7->Unlock(NULL);
    }

    // ******************************************************************
    // * update overlay!
    // ******************************************************************
    if(g_bSupportsYUY2)
    {
        RECT SourRect = {0, 0, (LONG)g_dwOverlayW, (LONG)g_dwOverlayH}, DestRect;

        int nTitleHeight  = GetSystemMetrics(SM_CYCAPTION);
        int nBorderWidth  = GetSystemMetrics(SM_CXSIZEFRAME);
        int nBorderHeight = GetSystemMetrics(SM_CYSIZEFRAME);

        GetWindowRect(g_hEmuWindow, &DestRect);

        DestRect.left   += nBorderWidth;
        DestRect.right  -= nBorderWidth;
        DestRect.top    += nTitleHeight + nBorderHeight;
        DestRect.bottom -= nBorderHeight;

        HRESULT hRet = g_pDDSOverlay7->UpdateOverlay(&SourRect, g_pDDSPrimary, &DestRect, DDOVER_SHOW, 0);
    }
    else
    {
        // No DirectDraw hardware overlays on modern hosts. Convert the YUY2
        // frame into the shared RGB texture and let Swap composite it over
        // the backbuffer every frame until EnableOverlay(FALSE). The old
        // fallback wrote the pixels straight into the backbuffer HERE, which
        // stayed invisible: the title clears the backbuffer at the top of
        // every frame, wiping the overlay before its Swap presented it.
        EmuYuy2TextureInfo *pInfo = EmuFindYuy2Texture((X_D3DResource*)pSurface);

        // XMV decodes 29.97/30 fps video ahead of its DirectSound master clock.
        // The Xbox overlay consumes those frames at video cadence; accepting
        // them at the 60 Hz display cadence makes the decoder run ahead and
        // then pause at each audio packet boundary. Rate-match software-overlay
        // updates while the latest frame continues to scan out at 60 Hz.
        EmuPacePresent(g_NextOverlayUpdate, 1001, 30000);
        g_pOverlayFrameTexture = EmuConvertYuy2Texture(pInfo);
        if(g_pOverlayFrameTexture != NULL &&
           cxbx::trace::IsEnabled(cxbx::trace::Channel::Media))
        {
            const std::uint32_t frame =
                g_OverlayFrameGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
            cxbx::trace::RecordBinary(cxbx::trace::Event::MediaOverlayUpdate, frame);
        }
    }

    EmuSwapFS();   // XBox FS

    return;
}
// ******************************************************************
// * func: EmuIDirect3DDevice8_GetOverlayUpdateStatus
// ******************************************************************
BOOL WINAPI XTL::EmuIDirect3DDevice8_GetOverlayUpdateStatus()
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("GetOverlayUpdateStatus");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetOverlayUpdateStatus();\n",
               GetCurrentThreadId());
    }
    #endif

    EmuSwapFS();   // XBox FS

    // Overlay updates complete synchronously in the software path.
    return TRUE;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BlockUntilVerticalBlank
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_BlockUntilVerticalBlank()
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("BlockUntilVerticalBlank");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_BlockUntilVerticalBlank();\n",
               GetCurrentThreadId());
    }
    #endif

    if(g_XBVideo.GetVSync())
        g_pDD7->WaitForVerticalBlank(DDWAITVB_BLOCKBEGIN, 0);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVerticalBlankCallback
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetVerticalBlankCallback(PVOID pCallback)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetVerticalBlankCallback");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVerticalBlankCallback\n"
               "(\n"
               "   pCallback           : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pCallback);
    }
    #endif

    printf("*Warning* EmuIDirect3DDevice8_SetVerticalBlankCallback is not implemented\n");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTextureState_TexCoordIndex
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetTextureState_TexCoordIndex
(
    DWORD Stage,
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetTextureState_TexCoordIndex");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetTextureState_TexCoordIndex\n"
               "(\n"
               "   Stage               : 0x%.08X\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Stage, Value);
    }
    #endif

    if(Value > 0x00030000)
        EmuCleanup("EmuIDirect3DDevice8_SetTextureState_TexCoordIndex: Unknown TexCoordIndex Value (0x%.08X)", Value);

    g_pD3DDevice8->SetTextureStageState(Stage, D3DTSS_TEXCOORDINDEX, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_NormalizeNormals
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_NormalizeNormals
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_NormalizeNormals");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_NormalizeNormals\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    g_pD3DDevice8->SetRenderState(D3DRS_NORMALIZENORMALS, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_TextureFactor
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_TextureFactor
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_TextureFactor");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_TextureFactor\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    g_pD3DDevice8->SetRenderState(D3DRS_TEXTUREFACTOR, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_ZBias
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_ZBias
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_ZBias");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_ZBias\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    g_pD3DDevice8->SetRenderState(D3DRS_ZBIAS, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_EdgeAntiAlias
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_EdgeAntiAlias
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_EdgeAntiAlias");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_EdgeAntiAlias\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

//  TODO: Analyze performance and compatibility (undefined behavior on PC with triangles or points)
//  g_pD3DDevice8->SetRenderState(D3DRS_EDGEANTIALIAS, Value);

//    printf("*Warning* SetRenderState_EdgeAntiAlias not implemented!\n");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_FillMode
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_FillMode
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_FillMode");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_FillMode\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    g_pD3DDevice8->SetRenderState(D3DRS_FILLMODE, EmuXB2PC_D3DFILLMODE(Value));

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_FogColor
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_FogColor
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_FogColor");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_FogColor\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    g_pD3DDevice8->SetRenderState(D3DRS_FOGCOLOR, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_Dxt1NoiseEnable
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_Dxt1NoiseEnable
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_Dxt1NoiseEnable");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_Dxt1NoiseEnable\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    printf("*Warning* SetRenderState_Dxt1NoiseEnable not implemented!\n");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_Simple
// ******************************************************************
VOID __fastcall XTL::EmuIDirect3DDevice8_SetRenderState_Simple
(
    DWORD Method,
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_Simple\n"
               "(\n"
               "   Method              : 0x%.08X\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Method, Value);
    }
    #endif

    int State = -1;

    // Todo: make this faster and more elegant
    for(int v=0;v<174;v++)
    {
        if(EmuD3DRenderStateSimpleEncoded[v] == Method)
        {
            State = v;
            break;
        }
    }

    if(State == -1)
    {
        static volatile LONG WarningCount = 0;
        const LONG Count = InterlockedIncrement(&WarningCount);
        if(Count <= 16)
        {
            printf("*Warning* RenderState_Simple(0x%.08X, 0x%.08X) is unsupported\n", Method, Value);
        }
        else if(Count == 17)
        {
            printf("*Warning* further unsupported RenderState_Simple calls are suppressed\n");
        }
    }
    else
    {
        switch(State)
        {
            case D3DRS_COLORWRITEENABLE:
            {
                DWORD OrigValue = Value;

                Value = 0;

                if(OrigValue & (1L<<16))
                    Value |= D3DCOLORWRITEENABLE_RED;
                if(OrigValue & (1L<<8))
                    Value |= D3DCOLORWRITEENABLE_GREEN;
                if(OrigValue & (1L<<0))
                    Value |= D3DCOLORWRITEENABLE_BLUE;
                if(OrigValue & (1L<<24))
                    Value |= D3DCOLORWRITEENABLE_ALPHA;
            }
            break;

            case D3DRS_SHADEMODE:
                Value = Value & 0x03;
                break;

            case D3DRS_BLENDOP:
                Value = EmuXB2PC_D3DBLENDOP(Value);
                break;
            
            case D3DRS_SRCBLEND:
            case D3DRS_DESTBLEND:
                Value = EmuXB2PC_D3DBLEND(Value);
                break;
            
            case D3DRS_ZFUNC:
            case D3DRS_ALPHAFUNC:
                Value = EmuXB2PC_D3DCMPFUNC(Value);
                break;
        };

        // Todo: Verify these params as you add support for them!
        g_pD3DDevice8->SetRenderState((D3DRENDERSTATETYPE)State, Value);
    }

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_VertexBlend
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_VertexBlend
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_VertexBlend");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_VertexBlend\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    // ******************************************************************
    // * Convert from Xbox D3D to PC D3D enumeration
    // ******************************************************************
    if(Value <= 1)
        Value = Value;
    else if(Value == 3)
        Value = 2;
    else if(Value == 5)
        Value = 3;
    else
        EmuCleanup("Unsupported D3DVERTEXBLENDFLAGS (%d)", Value);

    g_pD3DDevice8->SetRenderState(D3DRS_VERTEXBLEND, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_CullMode
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_CullMode
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_CullMode");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_CullMode\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    // ******************************************************************
    // * Convert from Xbox D3D to PC D3D enumeration
    // ******************************************************************
    // TODO: XDK-Specific Tables? So far they are the same
    switch(Value)
    {
        case 0:
            Value = D3DCULL_NONE;
            break;
        case 0x900:
            Value = D3DCULL_CW;
            break;
        case 0x901:
            Value = D3DCULL_CCW;
            break;
        default:
            EmuCleanup("EmuIDirect3DDevice8_SetRenderState_CullMode: Unknown Cullmode (%d)", Value);
    }

    g_pD3DDevice8->SetRenderState(D3DRS_CULLMODE, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_ZEnable
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_ZEnable
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_ZEnable");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_ZEnable\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    g_pD3DDevice8->SetRenderState(D3DRS_ZENABLE, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_StencilEnable
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_StencilEnable
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_StencilEnable");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_StencilEnable\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    g_pD3DDevice8->SetRenderState(D3DRS_STENCILENABLE, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_StencilFail
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_StencilFail
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_StencilFail");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_StencilFail\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    g_pD3DDevice8->SetRenderState(D3DRS_STENCILFAIL, Value);

    EmuSwapFS();   // XBox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_MultiSampleAntiAlias
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_MultiSampleAntiAlias
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_MultiSampleAntiAlias");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_MultiSampleAntiAlias\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    g_pD3DDevice8->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_ShadowFunc
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_ShadowFunc
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_ShadowFunc");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_ShadowFunc\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    // Xbox shadow-buffer compare function (NV2A hardware shadow mapping);
    // no host D3D8 equivalent, so accept and ignore. Warn once, not per call
    // -- titles set this every frame (Turok Evolution's render loop).
    static bool WarnedOnce = false;
    if(!WarnedOnce)
    {
        WarnedOnce = true;
        EmuWarning("ShadowFunc not implemented (ignored; warning shown once)");
    }

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_YuvEnable
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_YuvEnable
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_YuvEnable");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_YuvEnable\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    // D3DRS_YUVENABLE tells the NV2A to sample bound textures as YUY2 and
    // convert YUV->RGB in the texture unit (titles composite XMV video this
    // way). The host has no such texture-unit conversion, so we do the YUV->RGB
    // in software when the surface is bound: a YUY2 texture reaches SetTexture
    // either as a fake high-bit handle (converted via EmuConvertYuy2Texture) or,
    // on hardware that lacks YUY2 sampling, is handled there too. Here we just
    // latch the state (and stop spamming a per-frame warning).
    BOOL bEnable = Value != 0;
    static BOOL bLoggedYuvEnable = FALSE;
    if(bEnable && !bLoggedYuvEnable)
    {
        printf("EmuD3D8: YuvEnable ON (YUY2 textures convert to RGB on bind)\n");
        bLoggedYuvEnable = TRUE;
    }
    g_bYuvEnable = bEnable;

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_FrontFace
// ******************************************************************
// Xbox extension render states have no PC D3DRS equivalent. We accept and
// discard the value (the NV2A-specific front-face winding is handled via
// CullMode on the host); patching the call prevents the guest from touching
// raw device fields.
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_FrontFace
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_FrontFace");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_FrontFace\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_LineWidth
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_LineWidth
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_LineWidth");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_LineWidth\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_LogicOp
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_LogicOp
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_LogicOp");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_LogicOp\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_BackFillMode
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_BackFillMode
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_BackFillMode");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_BackFillMode\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_TwoSidedLighting
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_TwoSidedLighting
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_TwoSidedLighting");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_TwoSidedLighting\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_MultiSampleMode
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_MultiSampleMode
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_MultiSampleMode");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_MultiSampleMode\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_MultiSampleRenderTargetMode
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_MultiSampleRenderTargetMode
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_MultiSampleRenderTargetMode");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_MultiSampleRenderTargetMode\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_MultiSampleMask
// ******************************************************************
// Unlike the other Xbox multisample states, MultiSampleMask has a direct PC
// D3DRS equivalent, so forward the value to the host device.
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_MultiSampleMask
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_MultiSampleMask");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_MultiSampleMask\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    g_pD3DDevice8->SetRenderState(D3DRS_MULTISAMPLEMASK, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_SampleAlpha
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_SampleAlpha
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_SampleAlpha");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_SampleAlpha\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_PSTextureModes
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_PSTextureModes
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderState_PSTextureModes");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_PSTextureModes\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderStateNotInline
// ******************************************************************
// The generic D3DDevice::SetRenderState(State, Value) dispatcher: the Xbox
// implementation is a switch that calls the dedicated SetRenderState_<Name>
// helper for each state ordinal. In the HLE model every dedicated helper is
// patched individually, so patching only the dispatcher would leave those
// direct-call sites unpatched. We patch the dispatcher to a no-op so that if
// a title calls it directly (rather than the dedicated helper) it does not
// touch raw NV2A deferred-state globals; the per-state helpers carry the
// real (host-forwarding or no-op) behaviour.
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderStateNotInline
(
    DWORD State,
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderStateNotInline");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderStateNotInline\n"
               "(\n"
               "   State               : 0x%.08X\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), State, Value);
    }
    #endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTransform
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetTransform
(
    D3DTRANSFORMSTATETYPE State,
    CONST D3DMATRIX      *pMatrix
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetTransform");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetTransform\n"
               "(\n"
               "   State               : 0x%.08X\n"
               "   pMatrix             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), State, pMatrix);
    }
    #endif

    State = EmuXB2PC_D3DTS(State);

    __try
    {
        g_pD3DDevice8->SetTransform(State, pMatrix);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetGammaRamp
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetGammaRamp
(
    DWORD                  Flags,
    CONST D3DGAMMARAMP    *pRamp
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetGammaRamp");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetGammaRamp\n"
               "(\n"
               "   Flags               : 0x%.08X\n"
               "   pRamp               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Flags, pRamp);
    }
    #endif

    // The Xbox implementation writes the console video DAC. Applying the
    // ramp through host D3D would alter the entire desktop, so retain it as
    // accepted emulated display state until per-window gamma is available.
    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetTransform
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetTransform
(
    D3DTRANSFORMSTATETYPE State,
    D3DMATRIX            *pMatrix
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("GetTransform");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetTransform\n"
               "(\n"
               "   State               : 0x%.08X\n"
               "   pMatrix             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), State, pMatrix);
    }
    #endif

    State = EmuXB2PC_D3DTS(State);

    g_pD3DDevice8->GetTransform(State, pMatrix);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DVertexBuffer8_Lock
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DVertexBuffer8_Lock
(
    X_D3DVertexBuffer  *ppVertexBuffer,
    UINT                OffsetToLock,
    UINT                SizeToLock,
    BYTE              **ppbData,
    DWORD               Flags
)
{
    D3D_TRACE("VertexBuffer_Lock");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DVertexBuffer8_Lock\n"
               "(\n"
               "   ppVertexBuffer      : 0x%.08X\n"
               "   OffsetToLock        : 0x%.08X\n"
               "   SizeToLock          : 0x%.08X\n"
               "   ppbData             : 0x%.08X\n"
               "   Flags               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), ppVertexBuffer, OffsetToLock, SizeToLock, ppbData, Flags);
    }
    #endif

    IDirect3DVertexBuffer8 *pVertexBuffer8 = ppVertexBuffer->EmuVertexBuffer8;

    HRESULT hRet = pVertexBuffer8->Lock(OffsetToLock, SizeToLock, ppbData, Flags);
    if(SUCCEEDED(hRet))
    {
        // A title that locks a registered buffer is now updating the host D3D
        // copy. Stop preferring its original UMA registration span for CPU
        // vertex-shader draws so those writes remain visible.
        EmuDiscardRegisteredVertexBuffer(ppVertexBuffer);
    }

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DVertexBuffer8_Lock2
// ******************************************************************
BYTE* WINAPI XTL::EmuIDirect3DVertexBuffer8_Lock2
(
    X_D3DVertexBuffer  *ppVertexBuffer,
    DWORD               Flags
)
{
    D3D_TRACE("VertexBuffer_Lock2");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DVertexBuffer8_Lock2\n"
               "(\n"
               "   ppVertexBuffer      : 0x%.08X\n"
               "   Flags               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), ppVertexBuffer, Flags);
    }
    #endif

    IDirect3DVertexBuffer8 *pVertexBuffer8 = ppVertexBuffer->EmuVertexBuffer8;

    BYTE *pbData = NULL;

    HRESULT hRet = pVertexBuffer8->Lock(0, 0, &pbData, Flags);
    if(SUCCEEDED(hRet))
    {
        EmuDiscardRegisteredVertexBuffer(ppVertexBuffer);
    }

    EmuSwapFS();   // XBox FS

    return pbData;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetStreamSource
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetStreamSource
(
    UINT                StreamNumber,
    X_D3DVertexBuffer  *pStreamData,
    UINT                Stride
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetStreamSource");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetStreamSource\n"
               "(\n"
               "   StreamNumber        : 0x%.08X\n"
               "   pStreamData         : 0x%.08X\n"
               "   Stride              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), StreamNumber, pStreamData, Stride);
    }
#endif

    IDirect3DVertexBuffer8* pVertexBuffer8 = NULL;

    if(pStreamData != NULL)
    {
        EmuVerifyResourceIsRegistered(pStreamData);

        pVertexBuffer8 = pStreamData->EmuVertexBuffer8;
        pVertexBuffer8->Unlock();
    }
    if(StreamNumber < 16)
    {
        g_EmuVshCpuStreams[StreamNumber] = pStreamData;
        g_EmuVshCpuStreamStrides[StreamNumber] = Stride;
    }

    HRESULT hRet = D3D_OK;
    __try
    {
        hRet = g_pD3DDevice8->SetStreamSource(StreamNumber, pVertexBuffer8, Stride);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        hRet = D3D_OK;
    }

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShader
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetVertexShader(
    DWORD Handle)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("SetVertexShader");

// ******************************************************************
// * debug trace
// ******************************************************************
#ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexShader\n"
               "(\n"
               "   Handle              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Handle);
    }
#endif

    // A handle from EmuIDirect3DDevice8_CreateVertexShader is a pointer to our
    // X_D3DVertexShader wrapper (heap, >= 0x10000); resolve it to the host
    // shader handle it wraps. Raw FVF handles (small bit patterns) pass through.
    DWORD HostHandle = Handle;
    EmuVshCpuFallback* metadata = Handle >= 0x00010000
                                      ? EmuVshFindLive(reinterpret_cast<X_D3DVertexShader*>(Handle))
                                      : nullptr;
    g_EmuCurrentVertexShaderIsCustom = metadata != nullptr;
    EmuVshSetCurrentCpuShader(metadata != nullptr && metadata->enabled ? metadata : nullptr,
                              "SetVertexShader");
    if(metadata != nullptr)
    {
        HostHandle = reinterpret_cast<X_D3DVertexShader*>(Handle)->Handle;
    }

    HRESULT hRet = D3D_OK;
    if(g_EmuCurrentCpuVertexShader == nullptr)
    {
        __try
        {
            hRet = g_pD3DDevice8->SetVertexShader(HostHandle);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            hRet = D3D_OK;
        }
    }

    if(FAILED(hRet))
    {
        EmuWarning("SetVertexShader failed (Handle = 0x%.08X, Host = 0x%.08X)", Handle, HostHandle);
    }

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_LoadVertexShader
// ******************************************************************
// Xbox extension: loads an already-created vertex shader into NV2A vertex
// instruction memory at the given slot Address. The host D3D device manages
// shader storage itself (CreateVertexShader uploaded it), so this is a no-op;
// patching it prevents the guest from programming NV2A method registers.
VOID WINAPI XTL::EmuIDirect3DDevice8_LoadVertexShader
(
    DWORD Handle,
    DWORD Address
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("LoadVertexShader");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_LoadVertexShader\n"
               "(\n"
               "   Handle              : 0x%.08X\n"
               "   Address             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Handle, Address);
    }
    #endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SelectVertexShader
// ******************************************************************
// Xbox extension: selects the vertex shader resident at slot Address and binds
// Handle as the active declaration. On the host this is equivalent to
// SetVertexShader (the slot model does not exist); resolve the wrapper handle
// the same way SetVertexShader does and forward it.
VOID WINAPI XTL::EmuIDirect3DDevice8_SelectVertexShader
(
    DWORD Handle,
    DWORD Address
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SelectVertexShader");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SelectVertexShader\n"
               "(\n"
               "   Handle              : 0x%.08X\n"
               "   Address             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Handle, Address);
    }
#endif

    // A handle from EmuIDirect3DDevice8_CreateVertexShader is a pointer to our
    // X_D3DVertexShader wrapper (heap, >= 0x10000); resolve it to the host
    // shader handle it wraps. Raw FVF handles (small bit patterns) pass through.
    DWORD HostHandle = Handle;
    EmuVshCpuFallback* metadata = Handle >= 0x00010000
                                      ? EmuVshFindLive(reinterpret_cast<X_D3DVertexShader*>(Handle))
                                      : nullptr;
    g_EmuCurrentVertexShaderIsCustom = metadata != nullptr;
    EmuVshSetCurrentCpuShader(metadata != nullptr && metadata->enabled ? metadata : nullptr,
                              "SelectVertexShader");
    if(metadata != nullptr)
    {
        HostHandle = reinterpret_cast<X_D3DVertexShader*>(Handle)->Handle;
    }

    HRESULT hRet = D3D_OK;
    if(g_EmuCurrentCpuVertexShader == nullptr)
    {
        __try
        {
            hRet = g_pD3DDevice8->SetVertexShader(HostHandle);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            hRet = D3D_OK;
        }
    }

    if(FAILED(hRet))
    {
        EmuWarning("SelectVertexShader failed (Handle = 0x%.08X, Host = 0x%.08X)", Handle, HostHandle);
    }

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_DeleteVertexShader
// ******************************************************************
// Frees a vertex shader created by CreateVertexShader. The Handle is our
// X_D3DVertexShader wrapper; release the host shader it wraps, then free the
// wrapper. Raw FVF handles have nothing to free.
VOID WINAPI XTL::EmuIDirect3DDevice8_DeleteVertexShader(
    DWORD Handle)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("DeleteVertexShader");

#ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_DeleteVertexShader\n"
               "(\n"
               "   Handle              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Handle);
    }
#endif

    if(Handle >= 0x00010000 && EmuVshUnregisterLive((X_D3DVertexShader*)Handle))
    {
        X_D3DVertexShader* pShader = (X_D3DVertexShader*)Handle;
        if(pShader->Handle != 0)
        {
            g_pD3DDevice8->DeleteVertexShader(pShader->Handle);
        }
        delete pShader;
    }
    else if(Handle >= 0x00010000)
    {
        // Not (or no longer) a live wrapper: stale/double delete from the
        // title, or a handle we never created. Deleting it would corrupt the
        // host heap.
        printf("EmuD3D8 (0x%X): DeleteVertexShader skipped a non-live handle 0x%.08X.\n",
               GetCurrentThreadId(), Handle);
    }

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetVertexShaderSize
// ******************************************************************
// Xbox extension: reports the size (in bytes) of a shader's NV2A microcode.
// The host D3D device does not expose shader microcode size. CPU-fallback
// shaders retain the original program, so report its exact instruction bytes;
// host-only shaders retain the legacy zero result.
VOID WINAPI XTL::EmuIDirect3DDevice8_GetVertexShaderSize(
    DWORD Handle,
    UINT* pSize)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("GetVertexShaderSize");

#ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetVertexShaderSize\n"
               "(\n"
               "   Handle              : 0x%.08X\n"
               "   pSize               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Handle, pSize);
    }
#endif

    if(pSize != NULL)
    {
        EmuVshCpuFallback* metadata = Handle >= 0x00010000
                                          ? EmuVshFindLive(reinterpret_cast<X_D3DVertexShader*>(Handle))
                                          : nullptr;
        *pSize = metadata != nullptr && metadata->enabled
                     ? static_cast<UINT>(metadata->instructionCount * 4 * sizeof(DWORD))
                     : 0;
    }

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuUpdateDeferredStates
// ******************************************************************
static void EmuUpdateDeferredStates()
{
    using namespace XTL;

    // Re-assert the fixed-function fallback for an untranslated pixel shader on
    // every draw: the bound textures (which it modulates) change per draw, and
    // the title may touch texture-stage state between draws.
    if(g_bUsePixelShaderFallback)
    {
        EmuApplyPixelShaderFallback(EmuCurrentPixelShaderFallback(), false);
    }

    // Certain D3DRS values need to be checked on each Draw[Indexed]Vertices
    if(EmuD3DDeferredRenderState != 0)
    {
        if(XTL::EmuD3DDeferredRenderState[0] != X_D3DRS_UNK)
            g_pD3DDevice8->SetRenderState(D3DRS_FOGENABLE,             XTL::EmuD3DDeferredRenderState[0]);

        if(XTL::EmuD3DDeferredRenderState[1] != X_D3DRS_UNK)
            g_pD3DDevice8->SetRenderState(D3DRS_FOGTABLEMODE,          XTL::EmuD3DDeferredRenderState[1]);

        if(XTL::EmuD3DDeferredRenderState[6] != X_D3DRS_UNK)
        {
            ::DWORD dwConv = 0;
            
            dwConv |= (XTL::EmuD3DDeferredRenderState[6] & 0x00000010) ? D3DWRAP_U : 0;
            dwConv |= (XTL::EmuD3DDeferredRenderState[6] & 0x00001000) ? D3DWRAP_V : 0;
            dwConv |= (XTL::EmuD3DDeferredRenderState[6] & 0x00100000) ? D3DWRAP_W : 0;

            g_pD3DDevice8->SetRenderState(D3DRS_WRAP0, dwConv);
        }

        if(XTL::EmuD3DDeferredRenderState[10] != X_D3DRS_UNK)
            g_pD3DDevice8->SetRenderState(D3DRS_LIGHTING,              XTL::EmuD3DDeferredRenderState[10]);

        if(XTL::EmuD3DDeferredRenderState[11] != X_D3DRS_UNK)
            g_pD3DDevice8->SetRenderState(D3DRS_SPECULARENABLE,        XTL::EmuD3DDeferredRenderState[11]);

        if(XTL::EmuD3DDeferredRenderState[20] != X_D3DRS_UNK)
            g_pD3DDevice8->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, XTL::EmuD3DDeferredRenderState[20]);

        if(XTL::EmuD3DDeferredRenderState[23] != X_D3DRS_UNK)
            g_pD3DDevice8->SetRenderState(D3DRS_AMBIENT,               XTL::EmuD3DDeferredRenderState[23]);

        if(XTL::EmuD3DDeferredRenderState[24] != X_D3DRS_UNK)
            g_pD3DDevice8->SetRenderState(D3DRS_POINTSIZE,             XTL::EmuD3DDeferredRenderState[24]);
                                                                       
        if(XTL::EmuD3DDeferredRenderState[25] != X_D3DRS_UNK)        
            g_pD3DDevice8->SetRenderState(D3DRS_POINTSIZE_MIN,         XTL::EmuD3DDeferredRenderState[25]);
                                                                       
        if(XTL::EmuD3DDeferredRenderState[26] != X_D3DRS_UNK)        
            g_pD3DDevice8->SetRenderState(D3DRS_POINTSPRITEENABLE,     XTL::EmuD3DDeferredRenderState[26]);

        if(XTL::EmuD3DDeferredRenderState[27] != X_D3DRS_UNK)        
            g_pD3DDevice8->SetRenderState(D3DRS_POINTSCALEENABLE,      XTL::EmuD3DDeferredRenderState[27]);

        if(XTL::EmuD3DDeferredRenderState[28] != X_D3DRS_UNK)
            g_pD3DDevice8->SetRenderState(D3DRS_POINTSCALE_A,          XTL::EmuD3DDeferredRenderState[28]);

        if(XTL::EmuD3DDeferredRenderState[29] != X_D3DRS_UNK)
            g_pD3DDevice8->SetRenderState(D3DRS_POINTSCALE_B,          XTL::EmuD3DDeferredRenderState[29]);

        if(XTL::EmuD3DDeferredRenderState[30] != X_D3DRS_UNK)
            g_pD3DDevice8->SetRenderState(D3DRS_POINTSCALE_C,          XTL::EmuD3DDeferredRenderState[30]);

        if(XTL::EmuD3DDeferredRenderState[33] != X_D3DRS_UNK)
            g_pD3DDevice8->SetRenderState(D3DRS_PATCHSEGMENTS,         XTL::EmuD3DDeferredRenderState[33]);

        /** To check for unhandled RenderStates
        for(int v=0;v<117-82;v++)
        {
            if(XTL::EmuD3DDeferredRenderState[v] != X_D3DRS_UNK)
            {
                if(v != 0  && v != 1  && v != 6  && v != 10 && v != 11 && v != 20 && v != 23
                && v != 24 && v != 25 && v != 26 && v != 27 && v != 28 && v != 29 && v != 30
                && v != 33)
                    printf("*Warning* Unhandled RenderState Change @ %d (%d)\n", v, v + 82);
            }
        }
        //**/
    }

    // Certain D3DTS values need to be checked on each Draw[Indexed]Vertices
    if(EmuD3DDeferredTextureState != 0)
    {
        for(int v=0;v<4;v++)
        {
            ::DWORD *pCur = &EmuD3DDeferredTextureState[v*32];

            if(pCur[0] != X_D3DTSS_UNK)
            {
                if(pCur[0] == 5)
                    EmuCleanup("ClampToEdge is unsupported (temporarily)");

                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_ADDRESSU, pCur[0]);
            }

            if(pCur[1] != X_D3DTSS_UNK)
            {
                if(pCur[1] == 5)
                    EmuCleanup("ClampToEdge is unsupported (temporarily)");

                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_ADDRESSV, pCur[1]);
            }

            if(pCur[2] != X_D3DTSS_UNK)
            {
                if(pCur[2] == 5)
                    EmuCleanup("ClampToEdge is unsupported (temporarily)");

                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_ADDRESSW, pCur[2]);
            }

            if(pCur[3] != X_D3DTSS_UNK)
            {
                if(pCur[3] == 4)
                    EmuCleanup("QuinCunx is unsupported (temporarily)");

                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_MAGFILTER, pCur[3]);
            }

            if(pCur[4] != X_D3DTSS_UNK)
            {
                if(pCur[4] == 4)
                    EmuCleanup("QuinCunx is unsupported (temporarily)");

                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_MINFILTER, pCur[4]);
            }

            if(pCur[5] != X_D3DTSS_UNK)
            {
                if(pCur[5] == 4)
                    EmuCleanup("QuinCunx is unsupported (temporarily)");

                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_MIPFILTER, pCur[5]);
            }

            if(pCur[6] != X_D3DTSS_UNK)
                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_MIPMAPLODBIAS, pCur[6]);

            if(pCur[7] != X_D3DTSS_UNK)
                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_MAXMIPLEVEL, pCur[7]);

            if(pCur[8] != X_D3DTSS_UNK)
                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_MAXANISOTROPY, pCur[8]);

            // TODO: Use a lookup table, this is not always a 1:1 map
            if(pCur[12] != X_D3DTSS_UNK)
            {
                if(pCur[12] > 12)
                    EmuCleanup("(Temporarily) Unsupported D3DTSS_ALPHAOP Value (%d)", pCur[12]);

                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_COLOROP, pCur[12]);
            }

            if(pCur[13] != X_D3DTSS_UNK)
                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_COLORARG0, pCur[13]);

            if(pCur[14] != X_D3DTSS_UNK)
                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_COLORARG1, pCur[14]);

            if(pCur[15] != X_D3DTSS_UNK)
                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_COLORARG2, pCur[15]);

            // TODO: Use a lookup table, this is not always a 1:1 map (same as D3DTSS_COLOROP)
            if(pCur[16] != X_D3DTSS_UNK)
            {
                if(pCur[16] > 12)
                    EmuCleanup("(Temporarily) Unsupported D3DTSS_ALPHAOP Value (%d)", pCur[16]);

                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_ALPHAOP, pCur[16]);
            }

            if(pCur[17] != X_D3DTSS_UNK)
                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_ALPHAARG0, pCur[17]);

            if(pCur[18] != X_D3DTSS_UNK)
                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_ALPHAARG1, pCur[18]);
            
            if(pCur[19] != X_D3DTSS_UNK)
                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_ALPHAARG2, pCur[19]);

            if(pCur[20] != X_D3DTSS_UNK)
                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_RESULTARG, pCur[20]);

            if(pCur[21] != X_D3DTSS_UNK)
                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_TEXTURETRANSFORMFLAGS, pCur[21]);

            if(pCur[29] != X_D3DTSS_UNK)
                g_pD3DDevice8->SetTextureStageState(v, D3DTSS_BORDERCOLOR, pCur[29]);

            /** To check for unhandled texture stage state changes
            for(int r=0;r<32;r++)
            {
                static const int unchecked[] = 
                {
                    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 29, 30, 31
                };

                if(pCur[r] != X_D3DTSS_UNK)
                {
                    bool pass = true;

                    for(int q=0;q<sizeof(unchecked)/sizeof(int);q++)
                    {
                        if(r == unchecked[q])
                        {
                            pass = false;
                            break;
                        }
                    }

                    if(pass)
                        printf("*Warning* Unhandled TextureState Change @ %d->%d\n", v, r);
                }
            }
            //**/
        }

        // if point sprites are enabled, copy stage 3 over to 0
        if(EmuD3DDeferredRenderState[26] == TRUE)
        {
            // pCur = Texture Stage 3 States
            ::DWORD *pCur = &EmuD3DDeferredTextureState[2*32];

            IDirect3DBaseTexture8 *pTexture;

            // set the point sprites texture 
            g_pD3DDevice8->GetTexture(3, &pTexture); 
            g_pD3DDevice8->SetTexture(0, pTexture); 

            // disable all other stages 
            g_pD3DDevice8->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE); 
            g_pD3DDevice8->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE); 

            // in that case we have to copy over the stage by hand
            for(int v=0;v<30;v++)
            { 
                if(pCur[v] != X_D3DTSS_UNK)
                {
                    ::DWORD dwValue;

                    g_pD3DDevice8->GetTextureStageState(3, (D3DTEXTURESTAGESTATETYPE)v, &dwValue);
                    g_pD3DDevice8->SetTextureStageState(0, (D3DTEXTURESTAGESTATETYPE)v, dwValue);
                }
            } 
        }
    }
}

// ******************************************************************
// * func: EmuQuadHackA
// ******************************************************************
uint32 EmuQuadHackA(uint32 PrimitiveCount, XTL::IDirect3DVertexBuffer8 *&pOrigVertexBuffer8, XTL::IDirect3DVertexBuffer8 *&pHackVertexBuffer8, UINT dwOffset, PVOID pVertexStreamZeroData, UINT VertexStreamZeroStride, PVOID *ppNewVertexStreamZeroData)
{
    UINT uiStride = 0;

    // These are the sizes of our part in the vertex buffer
    DWORD dwOriginalSize    = 0;
    DWORD dwNewSize         = 0;

    // These are the sizes with the rest of the buffer
    DWORD dwOriginalSizeWR  = 0; // the size of the original vertex buffer
    DWORD dwNewSizeWR       = 0; // the size of the new buffer!!!

    BYTE *pOrigVertexData = 0;
    BYTE *pHackVertexData = 0;

    if(ppNewVertexStreamZeroData != 0)
        *ppNewVertexStreamZeroData = pVertexStreamZeroData;

    if(pVertexStreamZeroData == 0)
    {
        g_pD3DDevice8->GetStreamSource(0, &pOrigVertexBuffer8, &uiStride);

        // No usable bound stream source: a title can feed quads through the
        // immediate-mode path (no vertex buffer), or a partial-HLE title can
        // leave GetStreamSource returning an inconsistently-wrapped buffer.
        // Reject a NULL / unreadable stream source and skip the expansion
        // instead of dereferencing it and tearing down the render thread.
        if(pOrigVertexBuffer8 == 0 || IsBadReadPtr(pOrigVertexBuffer8, 4) ||
           IsBadCodePtr((FARPROC)*(void**)pOrigVertexBuffer8))
        {
            EmuWarning("EmuQuadHackA: no usable stream source (0x%.08X) -- skipping quad expansion",
                       pOrigVertexBuffer8);
            if(pOrigVertexBuffer8 != 0)
                pOrigVertexBuffer8->Release();
            pOrigVertexBuffer8 = 0;
            return 0;
        }

        // This is a list of sqares/rectangles, so we convert it to a list of triangles
        dwOriginalSize  = PrimitiveCount*uiStride*2;
        dwNewSize       = PrimitiveCount*uiStride*3;

        // Retrieve the original buffer size
        {
            XTL::D3DVERTEXBUFFER_DESC Desc;

            if(FAILED(pOrigVertexBuffer8->GetDesc(&Desc)))
                EmuCleanup("Could not retrieve buffer size");

            // Here we save the full buffer size
            dwOriginalSizeWR = Desc.Size;

            // So we can now calculate the size of the rest (dwOriginalSizeWR - dwOriginalSize) and
            // add it to our new calculated size of the patched buffer
            dwNewSizeWR = dwNewSize + dwOriginalSizeWR - dwOriginalSize;
        }

        if(FAILED(g_pD3DDevice8->CreateVertexBuffer(dwNewSizeWR, 0, 0, XTL::D3DPOOL_MANAGED, &pHackVertexBuffer8)) ||
           pHackVertexBuffer8 == 0)
        {
            EmuWarning("EmuQuadHackA: CreateVertexBuffer(%u) failed -- skipping quad expansion", dwNewSizeWR);
            pOrigVertexBuffer8->Release();
            pOrigVertexBuffer8 = 0;
            pHackVertexBuffer8 = 0;
            return 0;
        }

        pOrigVertexBuffer8->Lock(0, 0, &pOrigVertexData, 0);
        pHackVertexBuffer8->Lock(0, 0, &pHackVertexData, 0);

        if(pOrigVertexData == 0 || pHackVertexData == 0)
        {
            EmuWarning("EmuQuadHackA: buffer Lock returned NULL -- skipping quad expansion");
            if(pOrigVertexData != 0) pOrigVertexBuffer8->Unlock();
            if(pHackVertexData != 0) pHackVertexBuffer8->Unlock();
            pOrigVertexBuffer8->Release();
            pHackVertexBuffer8->Release();
            pOrigVertexBuffer8 = 0;
            pHackVertexBuffer8 = 0;
            return 0;
        }
    }
    else
    {
        uiStride = VertexStreamZeroStride;

        // This is a list of sqares/rectangles, so we convert it to a list of triangles
        dwOriginalSize  = PrimitiveCount*uiStride*2;
        dwNewSize       = PrimitiveCount*uiStride*3;

        dwOriginalSizeWR = dwOriginalSize;
        dwNewSizeWR = dwNewSize;

        pHackVertexData = (uint08*)malloc(dwNewSizeWR);
        pOrigVertexData = (uint08*)pVertexStreamZeroData;

        *ppNewVertexStreamZeroData = pHackVertexData;
    }

    DWORD dwVertexShader = NULL;

    g_pD3DDevice8->GetVertexShader(&dwVertexShader);

    // Copy the nonmodified data
    memcpy(pHackVertexData, pOrigVertexData, dwOffset);
    memcpy(&pHackVertexData[dwOffset+dwNewSize], &pOrigVertexData[dwOffset+dwOriginalSize], dwOriginalSizeWR-dwOffset-dwOriginalSize);

    for(DWORD i=0;i<(PrimitiveCount/2);i++)
    {
        memcpy(&pHackVertexData[dwOffset+i*uiStride*6+0*uiStride], &pOrigVertexData[dwOffset+i*uiStride*4+0*uiStride], uiStride);
        memcpy(&pHackVertexData[dwOffset+i*uiStride*6+1*uiStride], &pOrigVertexData[dwOffset+i*uiStride*4+1*uiStride], uiStride);
        memcpy(&pHackVertexData[dwOffset+i*uiStride*6+2*uiStride], &pOrigVertexData[dwOffset+i*uiStride*4+2*uiStride], uiStride);
        memcpy(&pHackVertexData[dwOffset+i*uiStride*6+3*uiStride], &pOrigVertexData[dwOffset+i*uiStride*4+2*uiStride], uiStride);
        memcpy(&pHackVertexData[dwOffset+i*uiStride*6+4*uiStride], &pOrigVertexData[dwOffset+i*uiStride*4+3*uiStride], uiStride);
        memcpy(&pHackVertexData[dwOffset+i*uiStride*6+5*uiStride], &pOrigVertexData[dwOffset+i*uiStride*4+0*uiStride], uiStride);

        if(dwVertexShader & D3DFVF_XYZRHW)
        {
            for(int z=0;z<6;z++)
            {
                if(((FLOAT*)&pHackVertexData[dwOffset+i*uiStride*6+z*uiStride])[2] == 0.0f)
                    ((FLOAT*)&pHackVertexData[dwOffset+i*uiStride*6+z*uiStride])[2] = 1.0f;
                if(((FLOAT*)&pHackVertexData[dwOffset+i*uiStride*6+z*uiStride])[3] == 0.0f)
                    ((FLOAT*)&pHackVertexData[dwOffset+i*uiStride*6+z*uiStride])[3] = 1.0f;
            }
        }
    }

    if(pVertexStreamZeroData == 0)
    {
        pOrigVertexBuffer8->Unlock();
        pHackVertexBuffer8->Unlock();

        g_pD3DDevice8->SetStreamSource(0, pHackVertexBuffer8, uiStride);
    }

    return uiStride;
}

// ******************************************************************
// * func: EmuQuadHackB
// ******************************************************************
VOID EmuQuadHackB(uint32 nStride, XTL::IDirect3DVertexBuffer8 *&pOrigVertexBuffer8, XTL::IDirect3DVertexBuffer8 *&pHackVertexBuffer8)
{
    if(pOrigVertexBuffer8 != 0 && pHackVertexBuffer8 != 0)
        g_pD3DDevice8->SetStreamSource(0, pOrigVertexBuffer8, nStride);

    if(pOrigVertexBuffer8 != 0)
        pOrigVertexBuffer8->Release();

    if(pHackVertexBuffer8 != 0)
        pHackVertexBuffer8->Release();
}

// ******************************************************************
// * Pushbuffer recording (CreatePushBuffer2 / BeginPushBuffer / EndPushBuffer)
// ******************************************************************
// Turok Evolution's frontend records every menu widget's draws into D3D
// pushbuffers and replays them with RunPushBuffer. The guest library's
// recording redirects the guest device's internal NV2A pusher, which does
// not exist under HLE, so un-hooked recording silently produced nothing and
// the frontend stayed black. The v1 semantics here are EXECUTE-AT-RECORD:
// every D3D wrapper called between Begin and End simply runs against the
// host device immediately, and the created pushbuffer stays permanently
// empty (Size == 0) -- RunPushBuffer treats that as a successful no-op, and
// a title that checks the recorded size re-records each frame instead of
// replaying stale content, which is exactly what immediate execution needs.
// (g_pRecordingPushBuffer is declared with the cached D3D state above.)

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreatePushBuffer2
// ******************************************************************
XTL::X_D3DPushBuffer* WINAPI XTL::EmuIDirect3DDevice8_CreatePushBuffer2
(
    DWORD Size,
    BOOL  RunUsingCpuCopy
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("CreatePushBuffer2");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreatePushBuffer2\n"
               "(\n"
               "   Size                : 0x%.08X\n"
               "   RunUsingCpuCopy     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Size, RunUsingCpuCopy);
    }
    #endif

    X_D3DPushBuffer* pPushBuffer = new(std::nothrow) X_D3DPushBuffer();
    if(pPushBuffer == NULL)
    {
        EmuSwapFS(); // XBox FS
        return NULL;
    }

    BYTE* data = Size == 0 ? NULL : new(std::nothrow) BYTE[Size]();
    if(Size != 0 && data == NULL)
    {
        delete pPushBuffer;
        EmuSwapFS(); // XBox FS
        return NULL;
    }

    pPushBuffer->Common = X_D3DCOMMON_TYPE_PUSHBUFFER | X_D3DCOMMON_D3DCREATED | 1;
    if(RunUsingCpuCopy != FALSE)
    {
        pPushBuffer->Common |= cxbx::d3d::PushBufferCpuCopy;
    }
    pPushBuffer->Data = reinterpret_cast<ULONG>(data);
    pPushBuffer->Lock = 0;
    pPushBuffer->Size = 0;
    pPushBuffer->AllocationSize = Size;

    EmuSwapFS();   // XBox FS

    return pPushBuffer;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BeginPushBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_BeginPushBuffer
(
    X_D3DPushBuffer *pPushBuffer
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("BeginPushBuffer");

    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_BeginPushBuffer\n"
               "(\n"
               "   pPushBuffer         : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pPushBuffer);
    }
    #endif

    static bool bLogged = false;
    if(!bLogged)
    {
        printf("EmuD3D8 (0x%X): BeginPushBuffer: recording runs against the host device immediately.\n",
               GetCurrentThreadId());
        bLogged = true;
    }

    g_pRecordingPushBuffer = pPushBuffer;

    if(pPushBuffer != NULL)
    {
        pPushBuffer->Size = 0;
        EmuResetRecordedPushBuffer(pPushBuffer);
    }

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_EndPushBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_EndPushBuffer(VOID)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("EndPushBuffer");

    #ifdef _DEBUG_TRACE
    printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_EndPushBuffer();\n", GetCurrentThreadId());
    #endif

    if(g_pRecordingPushBuffer != NULL)
    {
        g_pRecordingPushBuffer->Size += 4;
    }

    g_pRecordingPushBuffer = NULL;

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetPushBufferOffset
// ******************************************************************
DWORD WINAPI XTL::EmuIDirect3DDevice8_GetPushBufferOffset
(
    DWORD *pOffset
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("GetPushBufferOffset");

    DWORD dwOffset = 0;

    if(g_pRecordingPushBuffer != NULL)
        dwOffset = g_pRecordingPushBuffer->Size;

    if(pOffset != NULL)
        *pOffset = dwOffset;

    EmuSwapFS();   // XBox FS;

    return dwOffset;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_InsertFence
// ******************************************************************
DWORD WINAPI XTL::EmuIDirect3DDevice8_InsertFence(VOID)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("InsertFence");

    // On real hardware, InsertFence inserts a fence token into the GPU command
    // stream and returns a handle. The host runtime has no equivalent fence
    // mechanism accessible through IDirect3DDevice8, so we return a monotonically
    // increasing handle that IsFencePending always reports as completed (not
    // pending). This is correct for titles that use fences only to wait for GPU
    // completion -- the host GPU processes commands synchronously within each
    // Present/Clear, so by the time InsertFence returns the prior work is done.
    static DWORD s_NextFence = 1;
    DWORD fence = s_NextFence++;

    EmuSwapFS();   // XBox FS

    return fence;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_IsFencePending
// ******************************************************************
BOOL WINAPI XTL::EmuIDirect3DDevice8_IsFencePending(DWORD Fence)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("IsFencePending");

    // The host GPU processes commands synchronously within each Present/Clear,
    // so by the time a title checks IsFencePending the prior work is already
    // complete. Report FALSE (not pending) for all fences.
    BOOL pending = FALSE;
    cxbx::trace::RecordD3dWait(cxbx::trace::D3dWaitReason::Fence, 0,
                               static_cast<std::uint32_t>(Fence), pending != FALSE);

    EmuSwapFS();   // XBox FS

    return pending;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BlockOnFence
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_BlockOnFence(DWORD Fence)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("BlockOnFence");

    // The host GPU processes synchronously, so there is nothing to block on.
    cxbx::trace::RecordD3dWait(cxbx::trace::D3dWaitReason::Fence, 0,
                               static_cast<std::uint32_t>(Fence), false);

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_KickPushBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_KickPushBuffer(VOID)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("KickPushBuffer");

    // KickPushBuffer tells the GPU to start processing the current push buffer.
    // The host runtime submits commands immediately, so this is a no-op.

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BlockUntilIdle
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_BlockUntilIdle(VOID)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("BlockUntilIdle");

    // BlockUntilIdle waits for the GPU to finish all pending work. The host
    // runtime processes synchronously, so use the D3D single-step pusher
    // idle barrier if available, otherwise this is a no-op.
    if(g_pD3DSingleStepPusher != NULL && (*g_pD3DSingleStepPusher & 1) != 0)
    {
        IDirect3DSurface8 *pRT = NULL;
        if(SUCCEEDED(g_pD3DDevice8->GetRenderTarget(&pRT)) && pRT != NULL)
        {
            D3DLOCKED_RECT lr;
            RECT px = {0, 0, 1, 1};
            if(SUCCEEDED(pRT->LockRect(&lr, &px, D3DLOCK_READONLY)))
                pRT->UnlockRect();
            pRT->Release();
        }
    }

    cxbx::trace::RecordD3dWait(cxbx::trace::D3dWaitReason::Idle, 0, 0, false);

    EmuSwapFS();   // XBox FS

    return D3D_OK;
}

static bool EmuVshTryDrawCpuBound(bool quadList, XTL::D3DPRIMITIVETYPE primitiveType,
                                  UINT primitiveCount, UINT firstVertex, UINT vertexCount,
                                  const std::uint32_t* indices);
static void EmuVshLogCpuDraw(const char* api, XTL::X_D3DPRIMITIVETYPE primitiveType,
                             UINT vertexCount, bool rendered, const char* reason);

static bool EmuReplayHlePushBuffer(const DWORD* commandData, DWORD size,
                                   const EmuRecordedPushBuffer* recording)
{
    if(commandData == nullptr || size == 0)
    {
        return size == 0;
    }

    std::vector<std::uint32_t> indices;
    try
    {
        indices.reserve(4096);
    }
    catch(...)
    {
        return false;
    }

    std::uint32_t beginEndOperation = 0;
    std::size_t drawIndex = 0;
    try
    {
        const bool walked = cxbx::d3d::WalkPushBuffer(
            size,
            [commandData](std::uint32_t offset, std::uint32_t& word)
            {
                std::memcpy(&word, reinterpret_cast<const BYTE*>(commandData) + offset,
                            sizeof(word));
                return true;
            },
            [&indices, &beginEndOperation, &drawIndex, recording](
                std::uint32_t, std::uint32_t method, std::uint32_t data)
            {
                constexpr std::uint32_t setBeginEnd = 0x17FCu;
                constexpr std::uint32_t arrayElement16 = 0x1800u;
                constexpr std::uint32_t arrayElement32 = 0x1808u;
                constexpr std::size_t maximumIndices = 65536;

                if(method == setBeginEnd)
                {
                    if(data != 0)
                    {
                        beginEndOperation = data;
                        indices.clear();
                        return true;
                    }

                    const cxbx::d3d::IndexedBatch batch = cxbx::d3d::ClassifyIndexedBatch(
                        beginEndOperation, static_cast<std::uint32_t>(indices.size()));
                    XTL::D3DPRIMITIVETYPE primitiveType = XTL::D3DPT_FORCE_DWORD;
                    bool quadList = false;
                    switch(batch.topology)
                    {
                        case cxbx::d3d::IndexedBatchTopology::PointList:
                            primitiveType = XTL::D3DPT_POINTLIST;
                            break;
                        case cxbx::d3d::IndexedBatchTopology::LineList:
                            primitiveType = XTL::D3DPT_LINELIST;
                            break;
                        case cxbx::d3d::IndexedBatchTopology::LineStrip:
                            primitiveType = XTL::D3DPT_LINESTRIP;
                            break;
                        case cxbx::d3d::IndexedBatchTopology::TriangleList:
                            primitiveType = XTL::D3DPT_TRIANGLELIST;
                            break;
                        case cxbx::d3d::IndexedBatchTopology::TriangleStrip:
                            primitiveType = XTL::D3DPT_TRIANGLESTRIP;
                            break;
                        case cxbx::d3d::IndexedBatchTopology::TriangleFan:
                            primitiveType = XTL::D3DPT_TRIANGLEFAN;
                            break;
                        case cxbx::d3d::IndexedBatchTopology::QuadList:
                            primitiveType = XTL::D3DPT_TRIANGLELIST;
                            quadList = true;
                            break;
                        case cxbx::d3d::IndexedBatchTopology::Invalid:
                            break;
                    }

                    bool rendered = false;
                    const bool supported = primitiveType != XTL::D3DPT_FORCE_DWORD;
                    if(recording != nullptr && drawIndex < recording->draws.size())
                    {
                        const DWORD stateBlock = recording->draws[drawIndex].stateBlock;
                        if(stateBlock != 0 && FAILED(g_pD3DDevice8->ApplyStateBlock(stateBlock)))
                        {
                            return false;
                        }
                    }
                    ++drawIndex;
                    if(supported && g_EmuCurrentCpuVertexShader != nullptr)
                    {
                        rendered = EmuVshTryDrawCpuBound(
                            quadList, primitiveType, batch.primitiveCount,
                            g_EmuVshCpuBaseVertexIndex,
                            static_cast<UINT>(indices.size()), indices.data());
                        EmuVshLogCpuDraw("RunPushBuffer", beginEndOperation,
                                         static_cast<UINT>(indices.size()), rendered,
                                         rendered ? "none" : "cpu_push_draw_failed");
                    }

                    beginEndOperation = 0;
                    indices.clear();
                    return true;
                }

                if(beginEndOperation != 0 && method == arrayElement16)
                {
                    const auto packedIndices = cxbx::d3d::DecodeArrayElement16(data);
                    if(indices.size() > maximumIndices - packedIndices.size())
                    {
                        return false;
                    }
                    indices.insert(indices.end(), packedIndices.begin(), packedIndices.end());
                }
                else if(beginEndOperation != 0 && method == arrayElement32)
                {
                    if(indices.size() >= maximumIndices)
                    {
                        return false;
                    }
                    indices.push_back(data);
                }
                return true;
            });
        return walked && beginEndOperation == 0;
    }
    catch(...)
    {
        return false;
    }
}

// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_RunPushBuffer(
    X_D3DPushBuffer* pPushBuffer,
    PVOID pFixup)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("RunPushBuffer");

    HRESULT Result = D3D_OK;

    __try
    {
        if(pPushBuffer == NULL)
        {
            Result = D3DERR_INVALIDCALL;
        }
        else if(pFixup != NULL)
        {
            printf("*Warning* RunPushBuffer fixups are not yet implemented\n");
        }
        else
        {
            EmuRecordedPushBuffer* recording = EmuFindRecordedPushBuffer(pPushBuffer);
            const bool hasRegisteredData =
                recording != nullptr && recording->registeredData != nullptr &&
                pPushBuffer->Size <= recording->registeredBytes;
            const bool hasRecordedDraws = recording != nullptr && !recording->draws.empty();
            const cxbx::d3d::PushBufferReplay replay = cxbx::d3d::SelectPushBufferReplay(
                hasRegisteredData, hasRecordedDraws, pPushBuffer->Common,
                pPushBuffer->Data, pPushBuffer->Size, pPushBuffer->AllocationSize);
            if(replay == cxbx::d3d::PushBufferReplay::Invalid)
            {
                Result = D3DERR_INVALIDCALL;
            }
            else if(replay == cxbx::d3d::PushBufferReplay::Decode)
            {
                const DWORD* commandData = hasRegisteredData
                                               ? recording->registeredData
                                               : reinterpret_cast<const DWORD*>(
                                                     pPushBuffer->Data);
                if(hasRegisteredData && g_EmuCurrentCpuVertexShader != nullptr)
                {
                    if(!EmuReplayHlePushBuffer(commandData, pPushBuffer->Size, recording))
                    {
                        printf("*Warning* RunPushBuffer rejected an invalid HLE command stream\n");
                        Result = D3DERR_INVALIDCALL;
                    }
                }
                else
                {
                    if(hasRegisteredData)
                    {
                        EmuNv2aEnableHleRaster();
                    }
                    if(!EmuNv2aExecutePushBuffer(commandData, pPushBuffer->Size))
                    {
                        printf("*Warning* RunPushBuffer rejected an invalid command stream\n");
                        Result = D3DERR_INVALIDCALL;
                    }
                }
            }
            else if(replay == cxbx::d3d::PushBufferReplay::GuestDecode &&
                    !EmuNv2aExecuteGuestPushBuffer(pPushBuffer->Data, pPushBuffer->Size))
            {
                static LONG warningCount = 0;
                if(InterlockedIncrement(&warningCount) <= 4)
                {
                    printf("*Warning* RunPushBuffer could not map an in-place GPU command stream\n");
                }
            }

            if(SUCCEEDED(Result) && replay != cxbx::d3d::PushBufferReplay::Decode &&
               !EmuReplayRecordedPushBuffer(pPushBuffer))
            {
                printf("*Warning* RunPushBuffer failed to replay recorded host draws\n");
                Result = D3DERR_INVALIDCALL;
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        Result = D3DERR_INVALIDCALL;
    }

    EmuSwapFS(); // Xbox FS

    return Result;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_DrawVertices
// ******************************************************************
struct EmuVshCpuVertex
{
    float x;
    float y;
    float z;
    float rhw;
    float pointSize;
    DWORD diffuse;
    DWORD specular;
    float texCoords[4][2];
    float clipPosition[4];
    float diffuseComponents[4];
    float specularComponents[4];
};

static HRESULT EmuVshGetViewport(XTL::D3DVIEWPORT8* viewport);

static void EmuVshProjectCpuVertex(EmuVshCpuVertex& vertex,
                                   const XTL::D3DVIEWPORT8& viewport)
{
    const float w = vertex.clipPosition[3];
    const float inverseW = w > 1.0e-8f || w < -1.0e-8f ? 1.0f / w : 1.0f;
    vertex.x = static_cast<float>(viewport.X) +
               (vertex.clipPosition[0] * inverseW + 1.0f) *
                   static_cast<float>(viewport.Width) * 0.5f;
    vertex.y = static_cast<float>(viewport.Y) +
               (1.0f - vertex.clipPosition[1] * inverseW) *
                   static_cast<float>(viewport.Height) * 0.5f;
    vertex.z = viewport.MinZ + vertex.clipPosition[2] * inverseW *
                                  (viewport.MaxZ - viewport.MinZ);
    vertex.rhw = inverseW;
    vertex.diffuse = XTL::VshDiagnostics::PackD3DColor(vertex.diffuseComponents);
    vertex.specular = XTL::VshDiagnostics::PackD3DColor(vertex.specularComponents);
}

static float EmuVshClipDistance(const EmuVshCpuVertex& vertex, std::size_t plane)
{
    const float x = vertex.clipPosition[0];
    const float y = vertex.clipPosition[1];
    const float z = vertex.clipPosition[2];
    const float w = vertex.clipPosition[3];
    switch(plane)
    {
        case 0: return x + w;
        case 1: return w - x;
        case 2: return y + w;
        case 3: return w - y;
        case 4: return z;
        case 5: return w - z;
        default: return -1.0f;
    }
}

static EmuVshCpuVertex EmuVshInterpolateCpuVertex(const EmuVshCpuVertex& begin,
                                                   const EmuVshCpuVertex& end,
                                                   float amount)
{
    EmuVshCpuVertex result{};
    result.pointSize = begin.pointSize + (end.pointSize - begin.pointSize) * amount;
    for(std::size_t component = 0; component < 4; ++component)
    {
        result.clipPosition[component] = begin.clipPosition[component] +
                                         (end.clipPosition[component] -
                                          begin.clipPosition[component]) * amount;
        result.diffuseComponents[component] = begin.diffuseComponents[component] +
                                              (end.diffuseComponents[component] -
                                               begin.diffuseComponents[component]) * amount;
        result.specularComponents[component] = begin.specularComponents[component] +
                                               (end.specularComponents[component] -
                                                begin.specularComponents[component]) * amount;
        result.texCoords[component][0] = begin.texCoords[component][0] +
                                         (end.texCoords[component][0] -
                                          begin.texCoords[component][0]) * amount;
        result.texCoords[component][1] = begin.texCoords[component][1] +
                                         (end.texCoords[component][1] -
                                          begin.texCoords[component][1]) * amount;
    }
    return result;
}

static bool EmuVshClipCpuTriangles(XTL::D3DPRIMITIVETYPE& primitiveType,
                                   UINT& primitiveCount,
                                   std::vector<EmuVshCpuVertex>& vertices)
{
    if(primitiveType != XTL::D3DPT_TRIANGLELIST &&
       primitiveType != XTL::D3DPT_TRIANGLESTRIP &&
       primitiveType != XTL::D3DPT_TRIANGLEFAN)
    {
        return true;
    }

    XTL::D3DVIEWPORT8 viewport{};
    if(FAILED(EmuVshGetViewport(&viewport)))
    {
        return false;
    }

    const UINT requiredVertices = EmuRecordedDrawVertexCount(primitiveType, primitiveCount);
    if(requiredVertices > vertices.size())
    {
        return false;
    }

    std::vector<EmuVshCpuVertex> clipped;
    try
    {
        clipped.reserve(static_cast<std::size_t>(primitiveCount) * 6);
    }
    catch(...)
    {
        return false;
    }

    for(UINT primitive = 0; primitive < primitiveCount; ++primitive)
    {
        std::array<EmuVshCpuVertex, 12> polygon{};
        std::array<EmuVshCpuVertex, 12> scratch{};
        if(primitiveType == XTL::D3DPT_TRIANGLELIST)
        {
            polygon[0] = vertices[primitive * 3];
            polygon[1] = vertices[primitive * 3 + 1];
            polygon[2] = vertices[primitive * 3 + 2];
        }
        else if(primitiveType == XTL::D3DPT_TRIANGLESTRIP)
        {
            const UINT first = primitive;
            polygon[0] = vertices[first + (primitive & 1u ? 1u : 0u)];
            polygon[1] = vertices[first + (primitive & 1u ? 0u : 1u)];
            polygon[2] = vertices[first + 2];
        }
        else
        {
            polygon[0] = vertices[0];
            polygon[1] = vertices[primitive + 1];
            polygon[2] = vertices[primitive + 2];
        }

        std::size_t polygonSize = 3;
        for(std::size_t plane = 0; plane < 6 && polygonSize != 0; ++plane)
        {
            std::size_t scratchSize = 0;
            EmuVshCpuVertex previous = polygon[polygonSize - 1];
            float previousDistance = EmuVshClipDistance(previous, plane);
            bool previousInside = std::isfinite(previousDistance) && previousDistance >= 0.0f;
            for(std::size_t vertexIndex = 0; vertexIndex < polygonSize; ++vertexIndex)
            {
                const EmuVshCpuVertex& current = polygon[vertexIndex];
                const float currentDistance = EmuVshClipDistance(current, plane);
                const bool currentInside =
                    std::isfinite(currentDistance) && currentDistance >= 0.0f;
                if(previousInside != currentInside)
                {
                    const float denominator = previousDistance - currentDistance;
                    const float amount = denominator == 0.0f
                                             ? 0.0f
                                             : previousDistance / denominator;
                    if(scratchSize >= scratch.size())
                    {
                        return false;
                    }
                    scratch[scratchSize++] =
                        EmuVshInterpolateCpuVertex(previous, current, amount);
                }
                if(currentInside)
                {
                    if(scratchSize >= scratch.size())
                    {
                        return false;
                    }
                    scratch[scratchSize++] = current;
                }
                previous = current;
                previousDistance = currentDistance;
                previousInside = currentInside;
            }
            polygon = scratch;
            polygonSize = scratchSize;
        }

        for(std::size_t triangle = 1; triangle + 1 < polygonSize; ++triangle)
        {
            if(clipped.size() > 262144 - 3)
            {
                return false;
            }
            EmuVshCpuVertex triangleVertices[3] = {
                polygon[0], polygon[triangle], polygon[triangle + 1]
            };
            for(EmuVshCpuVertex& vertex : triangleVertices)
            {
                EmuVshProjectCpuVertex(vertex, viewport);
                clipped.push_back(vertex);
            }
        }
    }

    vertices.swap(clipped);
    primitiveType = XTL::D3DPT_TRIANGLELIST;
    primitiveCount = static_cast<UINT>(vertices.size() / 3);
    return true;
}

struct EmuVshLockedStream
{
    XTL::IDirect3DVertexBuffer8* buffer = nullptr;
    BYTE* data = nullptr;
    UINT byteSize = 0;
};

static HRESULT EmuVshLockVertexBuffer(XTL::X_D3DVertexBuffer* resource, BYTE** data, UINT* byteSize)
{
    __try
    {
        XTL::D3DVERTEXBUFFER_DESC description = {
            XTL::D3DFMT_UNKNOWN, XTL::D3DRTYPE_VERTEXBUFFER, 0, XTL::D3DPOOL_DEFAULT, 0, 0
        };
        HRESULT result = resource->EmuVertexBuffer8->GetDesc(&description);
        if(FAILED(result))
        {
            return result;
        }
        result = resource->EmuVertexBuffer8->Lock(0, 0, data, D3DLOCK_READONLY);
        if(SUCCEEDED(result))
        {
            *byteSize = description.Size;
        }
        return result;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return D3DERR_INVALIDCALL;
    }
}

static void EmuVshUnlockVertexBuffer(XTL::IDirect3DVertexBuffer8* buffer)
{
    if(buffer == nullptr)
    {
        return;
    }
    __try
    {
        buffer->Unlock();
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

struct EmuVshLockedStreamsGuard
{
    explicit EmuVshLockedStreamsGuard(std::array<EmuVshLockedStream, 16>& streams)
        : streams(streams)
    {
    }

    ~EmuVshLockedStreamsGuard()
    {
        Unlock();
    }

    void Unlock()
    {
        for(EmuVshLockedStream& stream : streams)
        {
            if(stream.data != nullptr)
            {
                EmuVshUnlockVertexBuffer(stream.buffer);
                stream.data = nullptr;
            }
        }
    }

    std::array<EmuVshLockedStream, 16>& streams;
};

static HRESULT EmuVshLockIndexBuffer(XTL::X_D3DIndexBuffer* resource, BYTE** data, UINT* byteSize)
{
    __try
    {
        XTL::D3DINDEXBUFFER_DESC description = {
            XTL::D3DFMT_UNKNOWN, XTL::D3DRTYPE_INDEXBUFFER, 0, XTL::D3DPOOL_DEFAULT, 0
        };
        HRESULT result = resource->EmuIndexBuffer8->GetDesc(&description);
        if(FAILED(result))
        {
            return result;
        }
        result = resource->EmuIndexBuffer8->Lock(0, 0, data, D3DLOCK_READONLY);
        if(SUCCEEDED(result))
        {
            *byteSize = description.Size;
        }
        return result;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return D3DERR_INVALIDCALL;
    }
}

static void EmuVshUnlockIndexBuffer(XTL::IDirect3DIndexBuffer8* buffer)
{
    if(buffer == nullptr)
    {
        return;
    }
    __try
    {
        buffer->Unlock();
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static HRESULT EmuVshGetViewport(XTL::D3DVIEWPORT8* viewport)
{
    __try
    {
        return g_pD3DDevice8->GetViewport(viewport);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return D3DERR_INVALIDCALL;
    }
}

static HRESULT EmuVshDrawPrimitiveUp(XTL::D3DPRIMITIVETYPE primitiveType, UINT primitiveCount,
                                     const EmuVshCpuVertex* vertices)
{
    if(g_EmuCurrentCpuVertexShader != nullptr &&
       !g_EmuCurrentCpuVertexShader->geometryLogged && vertices != nullptr)
    {
        const UINT vertexCount = EmuRecordedDrawVertexCount(primitiveType, primitiveCount);
        if(vertexCount != 0)
        {
            float minX = vertices[0].x;
            float maxX = vertices[0].x;
            float minY = vertices[0].y;
            float maxY = vertices[0].y;
            UINT visibleColors = 0;
            UINT visibleAlpha = 0;
            float minZ = vertices[0].z;
            float maxZ = vertices[0].z;
            float minU = vertices[0].texCoords[0][0];
            float maxU = vertices[0].texCoords[0][0];
            float minV = vertices[0].texCoords[0][1];
            float maxV = vertices[0].texCoords[0][1];
            for(UINT index = 0; index < vertexCount; ++index)
            {
                minX = (std::min)(minX, vertices[index].x);
                maxX = (std::max)(maxX, vertices[index].x);
                minY = (std::min)(minY, vertices[index].y);
                maxY = (std::max)(maxY, vertices[index].y);
                minZ = (std::min)(minZ, vertices[index].z);
                maxZ = (std::max)(maxZ, vertices[index].z);
                minU = (std::min)(minU, vertices[index].texCoords[0][0]);
                maxU = (std::max)(maxU, vertices[index].texCoords[0][0]);
                minV = (std::min)(minV, vertices[index].texCoords[0][1]);
                maxV = (std::max)(maxV, vertices[index].texCoords[0][1]);
                if((vertices[index].diffuse & 0x00FFFFFF) != 0)
                {
                    ++visibleColors;
                }
                if((vertices[index].diffuse & 0xFF000000) != 0)
                {
                    ++visibleAlpha;
                }
            }
            printf("VSH| cpu_geometry hash=%08X vertices=%u bounds=%.3f,%.3f,%.3f,%.3f "
                   "depth=%.6f,%.6f tex0=%.6f,%.6f,%.6f,%.6f "
                   "visible_colors=%u visible_alpha=%u diffuse=%08X\n",
                   static_cast<unsigned int>(g_EmuCurrentCpuVertexShader->hash), vertexCount,
                   minX, minY, maxX, maxY, minZ, maxZ, minU, minV, maxU, maxV,
                   visibleColors, visibleAlpha,
                   static_cast<unsigned int>(vertices[0].diffuse));
            DWORD zEnable = 0;
            DWORD zFunction = 0;
            DWORD cullMode = 0;
            DWORD alphaBlend = 0;
            DWORD alphaTest = 0;
            DWORD colorWrite = 0;
            UINT renderTargetWidth = 0;
            UINT renderTargetHeight = 0;
            bool drawsToBackBuffer = false;
            __try
            {
                g_pD3DDevice8->GetRenderState(XTL::D3DRS_ZENABLE, &zEnable);
                g_pD3DDevice8->GetRenderState(XTL::D3DRS_ZFUNC, &zFunction);
                g_pD3DDevice8->GetRenderState(XTL::D3DRS_CULLMODE, &cullMode);
                g_pD3DDevice8->GetRenderState(XTL::D3DRS_ALPHABLENDENABLE, &alphaBlend);
                g_pD3DDevice8->GetRenderState(XTL::D3DRS_ALPHATESTENABLE, &alphaTest);
                g_pD3DDevice8->GetRenderState(XTL::D3DRS_COLORWRITEENABLE, &colorWrite);
                XTL::IDirect3DSurface8* renderTarget = nullptr;
                XTL::IDirect3DSurface8* backBuffer = nullptr;
                if(SUCCEEDED(g_pD3DDevice8->GetRenderTarget(&renderTarget)) &&
                   renderTarget != nullptr)
                {
                    XTL::D3DSURFACE_DESC description = {
                        XTL::D3DFMT_UNKNOWN, XTL::D3DRTYPE_SURFACE, 0,
                        XTL::D3DPOOL_DEFAULT, 0, XTL::D3DMULTISAMPLE_NONE, 0, 0
                    };
                    if(SUCCEEDED(renderTarget->GetDesc(&description)))
                    {
                        renderTargetWidth = description.Width;
                        renderTargetHeight = description.Height;
                    }
                    if(SUCCEEDED(g_pD3DDevice8->GetBackBuffer(
                           0, XTL::D3DBACKBUFFER_TYPE_MONO, &backBuffer)))
                    {
                        drawsToBackBuffer = renderTarget == backBuffer;
                    }
                }
                if(backBuffer != nullptr)
                {
                    backBuffer->Release();
                }
                if(renderTarget != nullptr)
                {
                    renderTarget->Release();
                }
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
            }
            printf("VSH| cpu_raster_state hash=%08X z=%lu zfunc=%lu cull=%lu blend=%lu "
                   "alpha_test=%lu color_write=%08lX pixel_fallback=%u rt=%ux%u backbuffer=%u\n",
                   static_cast<unsigned int>(g_EmuCurrentCpuVertexShader->hash), zEnable,
                   zFunction, cullMode, alphaBlend, alphaTest, colorWrite,
                   g_bUsePixelShaderFallback ? 1u : 0u, renderTargetWidth,
                   renderTargetHeight, drawsToBackBuffer ? 1u : 0u);
            fflush(stdout);
            g_EmuCurrentCpuVertexShader->geometryLogged = true;
        }
    }

    const DWORD savedState = EmuCaptureD3DStateBlock();
    DWORD previousShader = 0;
    HRESULT result = D3DERR_INVALIDCALL;
    __try
    {
        XTL::IDirect3DBaseTexture8* stage0Texture = nullptr;
        const bool stage0TextureBound =
            SUCCEEDED(g_pD3DDevice8->GetTexture(0, &stage0Texture)) &&
            stage0Texture != nullptr;
        const bool stage0TextureHasData = EmuHostTextureHasData(stage0Texture);
        DWORD stage0TextureType = 0;
        DWORD stage0TextureFormat = 0;
        UINT stage0TextureWidth = 0;
        UINT stage0TextureHeight = 0;
        if(stage0Texture != nullptr)
        {
            stage0TextureType = static_cast<DWORD>(stage0Texture->GetType());
            if(stage0TextureType == XTL::D3DRTYPE_TEXTURE)
            {
                XTL::D3DSURFACE_DESC description{};
                if(SUCCEEDED(static_cast<XTL::IDirect3DTexture8*>(stage0Texture)
                                 ->GetLevelDesc(0, &description)))
                {
                    stage0TextureFormat = static_cast<DWORD>(description.Format);
                    stage0TextureWidth = description.Width;
                    stage0TextureHeight = description.Height;
                }
            }
        }
        XTL::IDirect3DBaseTexture8* stage3Texture = nullptr;
        DWORD stage3TextureType = 0;
        DWORD stage3TextureFormat = 0;
        UINT stage3TextureWidth = 0;
        UINT stage3TextureHeight = 0;
        const bool stage3TextureBound =
            SUCCEEDED(g_pD3DDevice8->GetTexture(3, &stage3Texture)) &&
            stage3Texture != nullptr;
        const bool stage3TextureHasData = EmuHostTextureHasData(stage3Texture);
        if(stage3Texture != nullptr)
        {
            stage3TextureType = static_cast<DWORD>(stage3Texture->GetType());
            if(stage3TextureType == XTL::D3DRTYPE_TEXTURE)
            {
                XTL::D3DSURFACE_DESC description{};
                if(SUCCEEDED(static_cast<XTL::IDirect3DTexture8*>(stage3Texture)
                                 ->GetLevelDesc(0, &description)))
                {
                    stage3TextureFormat = static_cast<DWORD>(description.Format);
                    stage3TextureWidth = description.Width;
                    stage3TextureHeight = description.Height;
                }
            }
            stage3Texture->Release();
        }
        if(stage0Texture != nullptr)
        {
            stage0Texture->Release();
        }
        DWORD texCoordIndexState = 0;
        const HRESULT texCoordIndexResult = g_pD3DDevice8->GetTextureStageState(
            0, XTL::D3DTSS_TEXCOORDINDEX, &texCoordIndexState);
        const DWORD texCoordIndex = texCoordIndexState & 0xFFFFu;
        bool texCoordsFinite = SUCCEEDED(texCoordIndexResult) && texCoordIndex < 4;
        bool texCoordsVary = false;
        const UINT vertexCount = EmuRecordedDrawVertexCount(primitiveType, primitiveCount);
        if(texCoordsFinite && vertices != nullptr && vertexCount != 0)
        {
            const float firstU = vertices[0].texCoords[texCoordIndex][0];
            const float firstV = vertices[0].texCoords[texCoordIndex][1];
            texCoordsFinite = std::isfinite(firstU) && std::isfinite(firstV);
            for(UINT index = 1; index < vertexCount && texCoordsFinite; ++index)
            {
                const float u = vertices[index].texCoords[texCoordIndex][0];
                const float v = vertices[index].texCoords[texCoordIndex][1];
                texCoordsFinite = std::isfinite(u) && std::isfinite(v);
                texCoordsVary = texCoordsVary || u != firstU || v != firstV;
            }
        }
        const bool stage0TextureUsable = cxbx::d3d::CpuFallbackTextureUsable(
            stage0TextureBound, static_cast<unsigned int>(texCoordIndex),
            texCoordsFinite, texCoordsVary, stage0TextureHasData);
        const cxbx::d3d::CpuFallbackMaterial material =
            cxbx::d3d::SelectCpuFallbackMaterial(stage0TextureUsable);
        if(g_EmuCurrentCpuVertexShader != nullptr &&
           !g_EmuCurrentCpuVertexShader->materialLogged)
        {
            printf("VSH| cpu_material hash=%08X pixel=%08lX stage0_texture=%u texcoord=%lu "
                   "texture_type=%lu texture_format=%lu texture_size=%ux%u "
                   "stage3_texture=%u stage3_type=%lu stage3_format=%lu "
                   "stage3_size=%ux%u stage3_data=%u "
                   "finite=%u varies=%u texture_data=%u mode=%s "
                   "combiner=%s\n",
                   static_cast<unsigned int>(g_EmuCurrentCpuVertexShader->hash),
                   g_EmuCurrentPixelShaderHandle, stage0TextureBound ? 1u : 0u, texCoordIndex,
                   stage0TextureType, stage0TextureFormat, stage0TextureWidth,
                   stage0TextureHeight,
                   stage3TextureBound ? 1u : 0u, stage3TextureType,
                   stage3TextureFormat, stage3TextureWidth, stage3TextureHeight,
                   stage3TextureHasData ? 1u : 0u,
                   texCoordsFinite ? 1u : 0u, texCoordsVary ? 1u : 0u,
                   stage0TextureHasData ? 1u : 0u,
                   material == cxbx::d3d::CpuFallbackMaterial::TextureModulate
                       ? "texture_modulate"
                       : "diffuse",
                   cxbx::d3d::PixelShaderFallbackName(EmuCurrentPixelShaderFallback()));
            fflush(stdout);
            g_EmuCurrentCpuVertexShader->materialLogged = true;
        }

        if(material == cxbx::d3d::CpuFallbackMaterial::TextureModulate)
        {
            // Preserve the common NV2A combiner shapes which the D3D8 fixed-function
            // stages can express: texture-only, doubled vertex lighting, a second
            // detail texture, and the stage-3 lookup used by several lit materials.
            EmuApplyPixelShaderFallback(EmuCurrentPixelShaderFallback(), true);
        }
        else
        {
            g_pD3DDevice8->SetPixelShader(0);
            g_pD3DDevice8->SetTextureStageState(0, XTL::D3DTSS_COLOROP,
                                                XTL::D3DTOP_SELECTARG1);
            g_pD3DDevice8->SetTextureStageState(0, XTL::D3DTSS_COLORARG1,
                                                D3DTA_DIFFUSE);
            g_pD3DDevice8->SetTextureStageState(0, XTL::D3DTSS_ALPHAOP,
                                                XTL::D3DTOP_SELECTARG1);
            g_pD3DDevice8->SetTextureStageState(0, XTL::D3DTSS_ALPHAARG1,
                                                D3DTA_DIFFUSE);
            g_pD3DDevice8->SetTextureStageState(1, XTL::D3DTSS_COLOROP,
                                                XTL::D3DTOP_DISABLE);
            g_pD3DDevice8->SetTextureStageState(1, XTL::D3DTSS_ALPHAOP,
                                                XTL::D3DTOP_DISABLE);
        }
        result = g_pD3DDevice8->GetVertexShader(&previousShader);
        if(SUCCEEDED(result))
        {
            result = g_pD3DDevice8->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE |
                                                    D3DFVF_SPECULAR | D3DFVF_PSIZE |
                                                    D3DFVF_TEX4);
        }
        if(SUCCEEDED(result))
        {
            result = g_pD3DDevice8->DrawPrimitiveUP(primitiveType, primitiveCount, vertices,
                                                    sizeof(EmuVshCpuVertex));
            if(SUCCEEDED(result))
            {
                // This draw is submitted to the current backbuffer immediately. Static
                // push-buffer recording keeps its own copy for future RunPushBuffer calls;
                // replaying every CPU fallback draw again at Present would move it after
                // later UI draws and cover title logos or text.
                EmuRecordPushBufferDraw(primitiveType, primitiveCount, vertices,
                                        sizeof(EmuVshCpuVertex));
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        result = D3DERR_INVALIDCALL;
    }

    __try
    {
        if(savedState != 0)
        {
            g_pD3DDevice8->ApplyStateBlock(savedState);
            EmuDeleteD3DStateBlock(savedState);
        }
        else
        {
            g_pD3DDevice8->SetVertexShader(previousShader);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return result;
}

static float EmuVshGetDefaultPointSize()
{
    DWORD encodedPointSize = 0;
    __try
    {
        g_pD3DDevice8->GetRenderState(XTL::D3DRS_POINTSIZE, &encodedPointSize);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        encodedPointSize = 0;
    }
    float pointSize = 1.0f;
    std::memcpy(&pointSize, &encodedPointSize, sizeof(pointSize));
    return XTL::VshDiagnostics::ClampPointSize(pointSize, 1.0f, g_D3DCaps.MaxPointSize);
}

static void EmuVshLogCpuDraw(const char* api, XTL::X_D3DPRIMITIVETYPE primitiveType,
                             UINT vertexCount, bool rendered, const char* reason)
{
    if(g_EmuCurrentCpuVertexShader == nullptr)
    {
        return;
    }
    bool& logged = rendered ? g_EmuCurrentCpuVertexShader->drawLogged
                            : g_EmuCurrentCpuVertexShader->rejectionLogged;
    if(logged)
    {
        return;
    }
    printf("VSH| cpu_draw hash=%08X api=%s vertices=%u primitive=%lu result=%s reason=%s\n",
           static_cast<unsigned int>(g_EmuCurrentCpuVertexShader->hash), api, vertexCount,
           static_cast<unsigned long>(primitiveType), rendered ? "success" : "rejected", reason);
    fflush(stdout);
    logged = true;
}

static bool EmuVshTransformCpuVertices(const XTL::VshDiagnostics::VertexStreamView* streams,
                                       std::size_t streamCount, const std::uint32_t* indices,
                                       UINT firstVertex, UINT vertexCount,
                                       std::vector<EmuVshCpuVertex>& output)
{
    if(g_EmuCurrentCpuVertexShader == nullptr || vertexCount == 0 || vertexCount > 65536)
    {
        return false;
    }

    XTL::D3DVIEWPORT8 viewport{};
    if(FAILED(EmuVshGetViewport(&viewport)))
    {
        return false;
    }

    const float defaultPointSize = EmuVshGetDefaultPointSize();
    std::array<float, 192 * 4> shaderConstants{};
    if(!XTL::VshDiagnostics::ApplyXboxDeclarationConstants(
           g_EmuCurrentCpuVertexShader->declaration.data(), g_EmuVshCpuConstants,
           shaderConstants.data(), shaderConstants.size()))
    {
        return false;
    }
    output.resize(vertexCount);
    std::array<float, 16 * 4> firstInputs{};
    bool capturedFirstInputs = false;
    for(UINT outputIndex = 0; outputIndex < vertexCount; ++outputIndex)
    {
        const UINT relativeIndex = indices == nullptr ? outputIndex : indices[outputIndex];
        if(relativeIndex > (std::numeric_limits<UINT>::max)() - firstVertex)
        {
            return false;
        }
        const UINT vertexIndex = firstVertex + relativeIndex;
        float input[16 * 4] = {};
        if(!XTL::VshDiagnostics::DecodeXboxVertex(g_EmuCurrentCpuVertexShader->declaration.data(), streams,
                                                  streamCount, vertexIndex, input, 16 * 4))
        {
            return false;
        }
        if(!capturedFirstInputs)
        {
            std::copy(std::begin(input), std::end(input), firstInputs.begin());
            capturedFirstInputs = true;
        }

        float position[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        float colors[2 * 4] = { 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        float texCoords[4 * 4] = {};
        XTL::VshDiagnostics::RasterOutputs rasterOutputs{};
        if(!XTL::VshDiagnostics::ExecuteXboxVertexShader(g_EmuCurrentCpuVertexShader->function.data(),
                                                         shaderConstants.data(), input, position, colors,
                                                         2 * 4, texCoords, 4 * 4, &rasterOutputs))
        {
            return false;
        }
        EmuVshCpuVertex& vertex = output[outputIndex];
        std::copy(std::begin(position), std::end(position), vertex.clipPosition);
        std::copy(&colors[0], &colors[4], vertex.diffuseComponents);
        std::copy(&colors[4], &colors[8], vertex.specularComponents);
        vertex.specularComponents[3] = XTL::VshDiagnostics::SelectRasterOutput(
            rasterOutputs.fog, rasterOutputs.fogWriteMask, 1.0f);
        const float pointSize = XTL::VshDiagnostics::SelectRasterOutput(
            rasterOutputs.pointSize, rasterOutputs.pointSizeWriteMask, defaultPointSize);
        vertex.pointSize = XTL::VshDiagnostics::ClampPointSize(
            pointSize, defaultPointSize, g_D3DCaps.MaxPointSize);
        for(std::size_t texCoord = 0; texCoord < 4; ++texCoord)
        {
            vertex.texCoords[texCoord][0] = texCoords[texCoord * 4];
            vertex.texCoords[texCoord][1] = texCoords[texCoord * 4 + 1];
        }
        EmuVshProjectCpuVertex(vertex, viewport);
    }

    if(vertexCount >= 3 && capturedFirstInputs &&
       !g_EmuCurrentCpuVertexShader->collapseCaptureLogged)
    {
        float minX = output[0].x;
        float maxX = output[0].x;
        float minY = output[0].y;
        float maxY = output[0].y;
        for(UINT index = 1; index < vertexCount; ++index)
        {
            minX = (std::min)(minX, output[index].x);
            maxX = (std::max)(maxX, output[index].x);
            minY = (std::min)(minY, output[index].y);
            maxY = (std::max)(maxY, output[index].y);
        }
        if(maxX - minX <= 1.0e-4f && maxY - minY <= 1.0e-4f)
        {
            try
            {
                const XTL::VshDiagnostics::TranslationCapture capture = {
                    g_EmuCurrentCpuVertexShader->function.data(),
                    nullptr,
                    g_EmuCurrentCpuVertexShader->declaration.data(),
                    nullptr,
                    "collapsed_geometry",
                    shaderConstants.data(),
                    shaderConstants.size(),
                    firstInputs.data(),
                    firstInputs.size(),
                    "decoded_vertex_0",
                };
                XTL::VshDiagnostics::DumpReplayCapture(stdout, capture);
            }
            catch(...)
            {
                EmuWarning("VshDecoder: collapsed-geometry capture raised a host exception");
            }
            g_EmuCurrentCpuVertexShader->collapseCaptureLogged = true;
        }
    }
    return true;
}

static bool EmuVshExpandCpuQuadTopology(bool quadList, XTL::D3DPRIMITIVETYPE& primitiveType,
                                        UINT& primitiveCount, UINT& vertexCount,
                                        const std::uint32_t*& indices,
                                        std::vector<std::uint32_t>& expandedIndices)
{
    if(!quadList)
    {
        return true;
    }
    if(!XTL::VshDiagnostics::ExpandQuadListIndices(indices, vertexCount, expandedIndices) ||
       expandedIndices.size() > 65536)
    {
        return false;
    }
    indices = expandedIndices.data();
    vertexCount = static_cast<UINT>(expandedIndices.size());
    primitiveType = XTL::D3DPT_TRIANGLELIST;
    primitiveCount = vertexCount / 3;
    return true;
}

static bool EmuVshDrawCpuBound(bool quadList, XTL::D3DPRIMITIVETYPE primitiveType,
                               UINT primitiveCount, UINT firstVertex, UINT vertexCount,
                               const std::uint32_t* indices)
{
    std::vector<std::uint32_t> expandedIndices;
    if(!EmuVshExpandCpuQuadTopology(quadList, primitiveType, primitiveCount, vertexCount,
                                    indices, expandedIndices))
    {
        return false;
    }
    std::array<EmuVshLockedStream, 16> locked{};
    EmuVshLockedStreamsGuard lockedStreamsGuard(locked);
    std::array<XTL::VshDiagnostics::VertexStreamView, 16> streams{};
    bool lockedAll = true;
    for(std::size_t stream = 0; stream < streams.size(); ++stream)
    {
        XTL::X_D3DVertexBuffer* resource = g_EmuVshCpuStreams[stream];
        if(resource == nullptr || g_EmuVshCpuStreamStrides[stream] == 0)
        {
            continue;
        }
        const EmuRegisteredVertexBuffer* registration =
            EmuFindRegisteredVertexBuffer(resource);
        if(registration != nullptr && registration->guestData != nullptr &&
           EmuD3DIsReadableRange(registration->guestData, registration->byteSize))
        {
            streams[stream] = {
                registration->guestData,
                registration->byteSize,
                g_EmuVshCpuStreamStrides[stream],
            };
            continue;
        }
        locked[stream].buffer = resource->EmuVertexBuffer8;
        if(locked[stream].buffer == nullptr ||
           FAILED(EmuVshLockVertexBuffer(resource, &locked[stream].data, &locked[stream].byteSize)))
        {
            lockedAll = false;
            break;
        }
        streams[stream] = {
            locked[stream].data,
            locked[stream].byteSize,
            g_EmuVshCpuStreamStrides[stream],
        };
    }

    std::vector<EmuVshCpuVertex> vertices;
    bool transformed = false;
    if(lockedAll)
    {
        transformed = EmuVshTransformCpuVertices(streams.data(), streams.size(), indices,
                                                 firstVertex, vertexCount, vertices);
    }
    lockedStreamsGuard.Unlock();
    if(!transformed || !EmuVshClipCpuTriangles(primitiveType, primitiveCount, vertices))
    {
        return false;
    }
    return vertices.empty() ||
           SUCCEEDED(EmuVshDrawPrimitiveUp(primitiveType, primitiveCount, vertices.data()));
}

static bool EmuVshDrawCpuUp(bool quadList, XTL::D3DPRIMITIVETYPE primitiveType,
                            UINT primitiveCount, UINT vertexCount, const void* data, UINT stride)
{
    const UINT sourceVertexCount = vertexCount;
    if(data == nullptr || stride == 0 ||
       sourceVertexCount > (std::numeric_limits<std::size_t>::max)() / stride)
    {
        return false;
    }
    const std::uint32_t* indices = nullptr;
    std::vector<std::uint32_t> expandedIndices;
    if(!EmuVshExpandCpuQuadTopology(quadList, primitiveType, primitiveCount, vertexCount,
                                    indices, expandedIndices))
    {
        return false;
    }
    std::array<XTL::VshDiagnostics::VertexStreamView, 16> streams{};
    streams[0] = { data, static_cast<std::size_t>(sourceVertexCount) * stride, stride };
    std::vector<EmuVshCpuVertex> vertices;
    if(!EmuVshTransformCpuVertices(streams.data(), streams.size(), indices, 0, vertexCount,
                                   vertices) ||
       !EmuVshClipCpuTriangles(primitiveType, primitiveCount, vertices))
    {
        return false;
    }
    return vertices.empty() ||
           SUCCEEDED(EmuVshDrawPrimitiveUp(primitiveType, primitiveCount, vertices.data()));
}

static bool EmuVshTryDrawCpuBound(bool quadList, XTL::D3DPRIMITIVETYPE primitiveType,
                                  UINT primitiveCount, UINT firstVertex, UINT vertexCount,
                                  const std::uint32_t* indices)
{
    try
    {
        return EmuVshDrawCpuBound(quadList, primitiveType, primitiveCount,
                                  firstVertex, vertexCount, indices);
    }
    catch(...)
    {
        return false;
    }
}

static bool EmuVshTryDrawCpuUp(bool quadList, XTL::D3DPRIMITIVETYPE primitiveType,
                               UINT primitiveCount, UINT vertexCount, const void* data, UINT stride)
{
    try
    {
        return EmuVshDrawCpuUp(quadList, primitiveType, primitiveCount, vertexCount, data, stride);
    }
    catch(...)
    {
        return false;
    }
}

static bool EmuVshTryDrawCpuIndexed(bool quadList, XTL::D3DPRIMITIVETYPE primitiveType,
                                    UINT primitiveCount, UINT vertexCount, const WORD* indexData)
{
    XTL::IDirect3DIndexBuffer8* hostIndexBuffer = nullptr;
    BYTE* indexBytes = nullptr;
    try
    {
        hostIndexBuffer = g_EmuVshCpuIndexBuffer == nullptr ? nullptr : g_EmuVshCpuIndexBuffer->EmuIndexBuffer8;
        UINT indexByteSize = 0;
        const std::size_t byteOffset = reinterpret_cast<std::size_t>(indexData);
        bool rendered = false;
        if(hostIndexBuffer != nullptr &&
           SUCCEEDED(EmuVshLockIndexBuffer(g_EmuVshCpuIndexBuffer, &indexBytes, &indexByteSize)) &&
           byteOffset <= indexByteSize && vertexCount <= (indexByteSize - byteOffset) / sizeof(WORD))
        {
            std::vector<std::uint32_t> indices(vertexCount);
            for(UINT index = 0; index < vertexCount; ++index)
            {
                WORD value = 0;
                std::memcpy(&value, indexBytes + byteOffset + index * sizeof(WORD), sizeof(value));
                indices[index] = value;
            }
            EmuVshUnlockIndexBuffer(hostIndexBuffer);
            indexBytes = nullptr;
            rendered = EmuVshDrawCpuBound(quadList, primitiveType, primitiveCount,
                                          g_EmuVshCpuBaseVertexIndex, vertexCount, indices.data());
        }
        if(indexBytes != nullptr)
        {
            EmuVshUnlockIndexBuffer(hostIndexBuffer);
        }
        return rendered;
    }
    catch(...)
    {
        if(indexBytes != nullptr)
        {
            EmuVshUnlockIndexBuffer(hostIndexBuffer);
        }
        return false;
    }
}

static bool EmuVshIsValidPrimitiveType(XTL::X_D3DPRIMITIVETYPE primitiveType)
{
    return static_cast<DWORD>(primitiveType) < 11;
}

VOID WINAPI XTL::EmuIDirect3DDevice8_DrawVertices(
    X_D3DPRIMITIVETYPE PrimitiveType,
    UINT StartVertex,
    UINT VertexCount)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("DrawVertices");

// ******************************************************************
// * debug trace
// ******************************************************************
#ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_DrawVertices\n"
               "(\n"
               "   PrimitiveType       : 0x%.08X\n"
               "   StartVertex         : 0x%.08X\n"
               "   VertexCount         : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), PrimitiveType, StartVertex, VertexCount);
    }
#endif

    EmuUpdateDeferredStates();

    if(!EmuVshIsValidPrimitiveType(PrimitiveType))
    {
        EmuVshLogCpuDraw("DrawVertices", PrimitiveType, VertexCount, false, "invalid_primitive");
        EmuWarning("DrawVertices rejected invalid primitive type %lu", static_cast<unsigned long>(PrimitiveType));
        EmuSwapFS(); // XBox FS
        return;
    }

    if((DWORD)PrimitiveType == 0x03 || (DWORD)PrimitiveType == 0x09 || (DWORD)PrimitiveType == 0x10)
        printf("*Warning* unsupported PrimitiveType! (%d)\n", (DWORD)PrimitiveType);

    UINT PrimitiveCount = EmuD3DVertex2PrimitiveCount(PrimitiveType, VertexCount);

    // Convert from Xbox to PC enumeration
    D3DPRIMITIVETYPE PCPrimitiveType = EmuPrimitiveType(PrimitiveType);

    if(!EmuD3DDrawGate("DrawVertices", PCPrimitiveType, PrimitiveCount))
    {
        EmuSwapFS(); // XBox FS
        return;
    }

    if(g_EmuCurrentCpuVertexShader != nullptr)
    {
        const bool quadList = PrimitiveType == 8;
        const bool rendered = EmuVshTryDrawCpuBound(quadList, PCPrimitiveType, PrimitiveCount,
                                                    StartVertex, VertexCount, nullptr);
        if(!rendered)
        {
            EmuVshLogCpuDraw("DrawVertices", PrimitiveType, VertexCount, false,
                             quadList && VertexCount % 4 != 0
                                 ? "incomplete_quad_list"
                                 : "execution_failed");
            static LONG warningCount = 0;
            if(InterlockedIncrement(&warningCount) <= 5)
            {
                EmuWarning("VSH CPU fallback could not render DrawVertices");
            }
        }
        else
        {
            EmuVshLogCpuDraw("DrawVertices", PrimitiveType, VertexCount, true, "none");
            EmuD3DDrawPost();
        }
        EmuSwapFS(); // XBox FS
        return;
    }

    IDirect3DVertexBuffer8* pOrigVertexBuffer8 = 0;
    IDirect3DVertexBuffer8* pHackVertexBuffer8 = 0;

    uint32 nStride = 0;

    if(PrimitiveType == 8) // Quad List
    {
        PrimitiveCount *= 2;
        nStride = EmuQuadHackA(PrimitiveCount, pOrigVertexBuffer8, pHackVertexBuffer8, StartVertex, 0, 0, 0);
    }

    // Host d3d8 throws when bound state is invalid (e.g. null texture from an
    // unresolved archive path, or an incompatible vertex format). Guard so the
    // throw is caught locally rather than escaping the FS content-swap.
    __try
    {
        g_pD3DDevice8->DrawPrimitive(
            PCPrimitiveType,
            StartVertex,
            PrimitiveCount);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        static LONG s_DrawGuardCount = 0;
        if(InterlockedIncrement(&s_DrawGuardCount) <= 3)
            EmuWarning("DrawVertices: host d3d8 threw during DrawPrimitive (caught)");
    }

    EmuD3DDrawPost();

    // TODO: use original stride here (duh!)
    if(PrimitiveType == 8) // Quad List
        EmuQuadHackB(nStride, pOrigVertexBuffer8, pHackVertexBuffer8);

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_DrawVerticesUP
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_DrawVerticesUP(
    X_D3DPRIMITIVETYPE PrimitiveType,
    UINT VertexCount,
    CONST PVOID pVertexStreamZeroData,
    UINT VertexStreamZeroStride)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("DrawVerticesUP");

// ******************************************************************
// * debug trace
// ******************************************************************
#ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_DrawVerticesUP\n"
               "(\n"
               "   PrimitiveType            : 0x%.08X\n"
               "   VertexCount              : 0x%.08X\n"
               "   pVertexStreamZeroData    : 0x%.08X\n"
               "   VertexStreamZeroStride   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), PrimitiveType, VertexCount, pVertexStreamZeroData,
               VertexStreamZeroStride);
    }
#endif

    EmuUpdateDeferredStates();

    if(!EmuVshIsValidPrimitiveType(PrimitiveType))
    {
        EmuVshLogCpuDraw("DrawVerticesUP", PrimitiveType, VertexCount, false, "invalid_primitive");
        EmuWarning("DrawVerticesUP rejected invalid primitive type %lu",
                   static_cast<unsigned long>(PrimitiveType));
        EmuSwapFS(); // XBox FS
        return;
    }

    if((DWORD)PrimitiveType == 0x03 || (DWORD)PrimitiveType == 0x09 || (DWORD)PrimitiveType == 0x10)
        printf("Unsupported PrimitiveType! (%d)\n", (DWORD)PrimitiveType);

    UINT PrimitiveCount = EmuD3DVertex2PrimitiveCount(PrimitiveType, VertexCount);

    // Convert from Xbox to PC enumeration
    D3DPRIMITIVETYPE PCPrimitiveType = EmuPrimitiveType(PrimitiveType);

    if(!EmuD3DDrawGate("DrawVerticesUP", PCPrimitiveType, PrimitiveCount))
    {
        EmuSwapFS(); // XBox FS
        return;
    }

    if(g_EmuCurrentCpuVertexShader != nullptr)
    {
        const bool quadList = PrimitiveType == 8;
        const bool rendered = EmuVshTryDrawCpuUp(quadList, PCPrimitiveType, PrimitiveCount,
                                                 VertexCount, pVertexStreamZeroData,
                                                 VertexStreamZeroStride);
        if(!rendered)
        {
            EmuVshLogCpuDraw("DrawVerticesUP", PrimitiveType, VertexCount, false,
                             quadList && VertexCount % 4 != 0
                                 ? "incomplete_quad_list"
                                 : "execution_failed");
            static LONG warningCount = 0;
            if(InterlockedIncrement(&warningCount) <= 5)
            {
                EmuWarning("VSH CPU fallback could not render DrawVerticesUP");
            }
        }
        else
        {
            EmuVshLogCpuDraw("DrawVerticesUP", PrimitiveType, VertexCount, true, "none");
            EmuD3DDrawPost();
        }
        EmuSwapFS(); // XBox FS
        return;
    }

    IDirect3DVertexBuffer8* pOrigVertexBuffer8 = 0;
    IDirect3DVertexBuffer8* pHackVertexBuffer8 = 0;

    uint32 nStride = 0;

    PVOID pNewVertexStreamZeroData = pVertexStreamZeroData;

    if(PrimitiveType == 8) // Quad List
    {
        PrimitiveCount *= 2;
        nStride = EmuQuadHackA(PrimitiveCount, pOrigVertexBuffer8, pHackVertexBuffer8, 0, pVertexStreamZeroData, VertexStreamZeroStride, &pNewVertexStreamZeroData);
    }

    HRESULT hRet = D3D_OK;
    __try
    {
        hRet = g_pD3DDevice8->DrawPrimitiveUP(
            PCPrimitiveType,
            PrimitiveCount,
            pNewVertexStreamZeroData,
            VertexStreamZeroStride);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        hRet = D3DERR_INVALIDCALL;
    }

    if(FAILED(hRet))
    {
        static LONG WarnCount = 0;
        if(InterlockedIncrement(&WarnCount) <= 5)
        {
            printf("EmuD3D8: DrawVerticesUP failed (0x%.08lX) prim=%lu primCount=%u stride=%u.\n",
                   hRet, static_cast<unsigned long>(PCPrimitiveType), PrimitiveCount,
                   VertexStreamZeroStride);
            EmuWarning("DrawVerticesUP failed (0x%.08X) prim=%d primCount=%d stride=%d",
                       hRet, PCPrimitiveType, PrimitiveCount, VertexStreamZeroStride);
        }
    }
    else
    {
        EmuRecordPushBufferDraw(PCPrimitiveType, PrimitiveCount, pNewVertexStreamZeroData,
                                VertexStreamZeroStride);
        EmuD3DDrawPost();
    }

    if(PrimitiveType == 8) // Quad List
    {
        EmuQuadHackB(nStride, pOrigVertexBuffer8, pHackVertexBuffer8);

        if(pNewVertexStreamZeroData != 0)
            free(pNewVertexStreamZeroData);
    }

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_DrawIndexedVertices
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_DrawIndexedVertices(
    X_D3DPRIMITIVETYPE PrimitiveType,
    UINT VertexCount,
    CONST PWORD pIndexData)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("DrawIndexedVertices");

// ******************************************************************
// * debug trace
// ******************************************************************
#ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_DrawIndexedVertices\n"
               "(\n"
               "   PrimitiveType       : 0x%.08X\n"
               "   VertexCount         : 0x%.08X\n"
               "   pIndexData          : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), PrimitiveType, VertexCount, pIndexData);
    }
#endif

    EmuUpdateDeferredStates();

    if(!EmuVshIsValidPrimitiveType(PrimitiveType))
    {
        EmuVshLogCpuDraw("DrawIndexedVertices", PrimitiveType, VertexCount, false, "invalid_primitive");
        EmuWarning("DrawIndexedVertices rejected invalid primitive type %lu",
                   static_cast<unsigned long>(PrimitiveType));
        EmuSwapFS(); // XBox FS
        return;
    }

    if((DWORD)PrimitiveType == 0x03 || (DWORD)PrimitiveType == 0x08 || (DWORD)PrimitiveType == 0x09 || (DWORD)PrimitiveType == 0x10)
        printf("*Warning* unsupported PrimitiveType! (%d)\n", (DWORD)PrimitiveType);

    UINT PrimitiveCount = EmuD3DVertex2PrimitiveCount(PrimitiveType, VertexCount);

    // Convert from Xbox to PC enumeration
    D3DPRIMITIVETYPE PCPrimitiveType = EmuPrimitiveType(PrimitiveType);

    if(!EmuD3DDrawGate("DrawIndexedVertices", PCPrimitiveType, PrimitiveCount))
    {
        EmuSwapFS(); // XBox FS
        return;
    }

    if(g_EmuCurrentCpuVertexShader != nullptr)
    {
        const bool quadList = PrimitiveType == 8;
        const bool rendered = EmuVshTryDrawCpuIndexed(quadList, PCPrimitiveType, PrimitiveCount,
                                                      VertexCount, pIndexData);
        if(!rendered)
        {
            EmuVshLogCpuDraw("DrawIndexedVertices", PrimitiveType, VertexCount, false,
                             quadList && VertexCount % 4 != 0
                                 ? "incomplete_quad_list"
                                 : "execution_failed");
            static LONG warningCount = 0;
            if(InterlockedIncrement(&warningCount) <= 5)
            {
                EmuWarning("VSH CPU fallback could not render DrawIndexedVertices");
            }
        }
        else
        {
            EmuVshLogCpuDraw("DrawIndexedVertices", PrimitiveType, VertexCount, true, "none");
            EmuD3DDrawPost();
        }
        EmuSwapFS(); // XBox FS
        return;
    }

    IDirect3DVertexBuffer8* pOrigVertexBuffer8 = 0;
    IDirect3DVertexBuffer8* pHackVertexBuffer8 = 0;

    uint32 nStride = 0;

    if(PrimitiveType == 8) // Quad List
    {
        PrimitiveCount *= 2;
        nStride = EmuQuadHackA(PrimitiveCount, pOrigVertexBuffer8, pHackVertexBuffer8, 0, 0, 0, 0);
    }

    __try
    {
        g_pD3DDevice8->DrawIndexedPrimitive
        (
            PCPrimitiveType, 0, VertexCount, ((DWORD)pIndexData)/2, PrimitiveCount
        );
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        static LONG s_DrawGuardCount = 0;
        if(InterlockedIncrement(&s_DrawGuardCount) <= 3)
            EmuWarning("DrawIndexedVertices: host d3d8 threw during DrawIndexedPrimitive (caught)");
    }

    EmuD3DDrawPost();

    if(PrimitiveType == 8)  // Quad List
        EmuQuadHackB(nStride, pOrigVertexBuffer8, pHackVertexBuffer8);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetLight
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetLight
(
    DWORD            Index,
    CONST D3DLIGHT8 *pLight
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetLight");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetLight\n"
               "(\n"
               "   Index               : 0x%.08X\n"
               "   pLight              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Index, pLight);
    }
    #endif

    HRESULT hRet = D3D_OK;
    __try
    {
        hRet = g_pD3DDevice8->SetLight(Index, pLight);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        hRet = D3D_OK;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetMaterial
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetMaterial
(
    CONST D3DMATERIAL8 *pMaterial
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetMaterial");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetMaterial\n"
               "(\n"
               "   pMaterial           : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pMaterial);
    }
    #endif

    HRESULT hRet = D3D_OK;
    __try
    {
        hRet = g_pD3DDevice8->SetMaterial(pMaterial);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        hRet = D3D_OK;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_LightEnable
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_LightEnable
(
    DWORD            Index,
    BOOL             bEnable
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("LightEnable");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_LightEnable\n"
               "(\n"
               "   Index               : 0x%.08X\n"
               "   bEnable             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Index, bEnable);
    }
    #endif

    HRESULT hRet = D3D_OK;
    __try
    {
        hRet = g_pD3DDevice8->LightEnable(Index, bEnable);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        hRet = D3D_OK;
    }

    EmuSwapFS();   // XBox FS
    
    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderTarget
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetRenderTarget
(
    X_D3DSurface    *pRenderTarget,
    X_D3DSurface    *pNewZStencil
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetRenderTarget");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderTarget\n"
               "(\n"
               "   pRenderTarget       : 0x%.08X\n"
               "   pNewZStencil        : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pRenderTarget, pNewZStencil);
    }
    #endif

    IDirect3DSurface8 *pPCRenderTarget = 0;
    IDirect3DSurface8 *pPCNewZStencil  = 0;

    if(pRenderTarget != 0)
    {
        EmuVerifyResourceIsRegistered(pRenderTarget);
        pPCRenderTarget = pRenderTarget->EmuSurface8;
    }

    if(pNewZStencil != 0)
    {
        EmuVerifyResourceIsRegistered(pNewZStencil);
        pPCNewZStencil  = pNewZStencil->EmuSurface8;
    }

    // On Xbox any surface can become the render target or depth buffer --
    // there is no D3DUSAGE_RENDERTARGET/DEPTHSTENCIL distinction -- so titles
    // hand over surfaces the host runtime rejects. Host d3d8 rejects them by
    // throwing the HRESULT internally (a bare C++ `throw <long>`), and that
    // throw does not unwind across our FS content-swap: the process dies
    // 0xE06D7363/D3DERR_INVALIDCALL (Turok Evolution's render-to-texture
    // path). Pre-validate host-side and only forward pairs the host can
    // accept; otherwise warn and report success, as real hardware would have
    // accepted the call.
    D3DSURFACE_DESC RTDesc, ZSDesc;
    bool GotRTDesc = (pPCRenderTarget != 0 && SUCCEEDED(pPCRenderTarget->GetDesc(&RTDesc)));
    bool GotZSDesc = (pPCNewZStencil != 0 && SUCCEEDED(pPCNewZStencil->GetDesc(&ZSDesc)));

    bool HostCompatible = true;
    if(GotRTDesc && (RTDesc.Usage & D3DUSAGE_RENDERTARGET) == 0)
        HostCompatible = false;
    if(GotZSDesc && (ZSDesc.Usage & D3DUSAGE_DEPTHSTENCIL) == 0)
        HostCompatible = false;
    if(GotRTDesc && GotZSDesc &&
       (ZSDesc.Width < RTDesc.Width || ZSDesc.Height < RTDesc.Height ||
        ZSDesc.MultiSampleType != RTDesc.MultiSampleType))
        HostCompatible = false;

    HRESULT hRet = D3D_OK;
    if(HostCompatible)
        hRet = g_pD3DDevice8->SetRenderTarget(pPCRenderTarget, pPCNewZStencil);

    // Update the cached guest-side pointers so GetRenderTarget2 /
    // GetDepthStencilSurface2 return what the title just set, even when the
    // host call was skipped (host-incompatible surfaces). On real hardware
    // these always succeed.
    if(pRenderTarget != 0)
        g_pCachedRenderTarget = pRenderTarget;
    if(pNewZStencil != 0)
        g_pCachedZStencilSurface = pNewZStencil;
    else
        g_pCachedZStencilSurface = NULL;

    EmuSwapFS();   // XBox FS

    if(!HostCompatible || FAILED(hRet))
    {
        EmuWarning(HostCompatible ? "SetRenderTarget Failed! (0x%.08X)"
                                  : "SetRenderTarget skipped (host-incompatible surfaces) (0x%.08X)", hRet);
        if(GotRTDesc)
            EmuWarning("SetRenderTarget RT   : fmt=%d %dx%d usage=0x%X pool=%d ms=%d",
                       RTDesc.Format, RTDesc.Width, RTDesc.Height, RTDesc.Usage, RTDesc.Pool, RTDesc.MultiSampleType);
        if(GotZSDesc)
            EmuWarning("SetRenderTarget ZS   : fmt=%d %dx%d usage=0x%X pool=%d ms=%d",
                       ZSDesc.Format, ZSDesc.Width, ZSDesc.Height, ZSDesc.Usage, ZSDesc.Pool, ZSDesc.MultiSampleType);
    }

    // The title's call would have succeeded on hardware; do not feed it an
    // error path it never takes there.
    if(!HostCompatible)
        hRet = D3D_OK;

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreatePalette
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreatePalette
(
    X_D3DPALETTESIZE    Size,
    X_D3DPalette      **ppPalette
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("CreatePalette");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreatePalette\n"
               "(\n"
               "   Size                : 0x%.08X\n"
               "   ppPalette           : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Size, ppPalette);
    }
    #endif

    *ppPalette = new X_D3DPalette();

    static int lk[4] =
    {
        256*sizeof(D3DCOLOR),    // D3DPALETTE_256
        128*sizeof(D3DCOLOR),    // D3DPALETTE_128
        64*sizeof(D3DCOLOR),     // D3DPALETTE_64
        32*sizeof(D3DCOLOR)      // D3DPALETTE_32
    };

    (*ppPalette)->Common = 0;
    (*ppPalette)->Lock = 0x8000BEEF; // emulated reference count for palettes
    (*ppPalette)->Data = (DWORD)new uint08[lk[Size]];

    EmuSwapFS();   // XBox FS
    
    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetPalette
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetPalette
(
    DWORD         Stage,
    X_D3DPalette *pPalette
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetPalette");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetPalette\n"
               "(\n"
               "   Stage               : 0x%.08X\n"
               "   pPalette            : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Stage, pPalette);
    }
    #endif

    EmuWarning("Not setting palette");

    EmuSwapFS();   // XBox FS
    
    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetFlickerFilter
// ******************************************************************
void WINAPI XTL::EmuIDirect3DDevice8_SetFlickerFilter
(
    DWORD         Filter
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetFlickerFilter");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetFlickerFilter\n"
               "(\n"
               "   Filter              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Filter);
    }
    #endif

    EmuWarning("Not setting flicker filter");

    EmuSwapFS();   // XBox FS
    
    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetSoftDisplayFilter
// ******************************************************************
void WINAPI XTL::EmuIDirect3DDevice8_SetSoftDisplayFilter
(
    BOOL Enable
)
{
    EmuSwapFS();   // Win2k/XP FS
    D3D_TRACE("SetSoftDisplayFilter");

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetSoftDisplayFilter\n"
               "(\n"
               "   Enable              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Enable);
    }
    #endif

    EmuWarning("Not setting soft display filter");

    EmuSwapFS();   // XBox FS
    
    return;
}

// ******************************************************************
// * func: EmuIDirect3DPalette8_Lock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DPalette8_Lock
(
    X_D3DPalette   *pThis,
    D3DCOLOR      **ppColors,
    DWORD           Flags
)
{
    D3D_TRACE("Palette_Lock");
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DPalette8_Lock\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               "   ppColors            : 0x%.08X\n"
               "   Flags               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), ppColors, Flags);
    }
    #endif

    *ppColors = (D3DCOLOR*)pThis->Data;

    EmuSwapFS();   // XBox FS
    
    return D3D_OK;
}
