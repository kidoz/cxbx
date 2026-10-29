// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;;
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbxkrnl->d3d8_emulation.cpp
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

#include "emulation_runtime.h"
#include "core/vertex_shader_translator.h"
#include "vsh_decoder.h"
#include "vsh_decoder_internal.h"
#include "vsh_cpu_device_state.h"
#include "vsh_shader_creation.h"
#include "vsh_shader_registry.h"
#include "fs_emulation.h"
#include "xbox_video.h"
#include "host_input_lifecycle.h"
#include "shared_video_config.h"
#include "host_backend.h"
#include "core/d3d_push_buffer.h"
#include "core/d3d_pixel_shader.h"
#include "core/d3d_pixel_shader_translate.h"
#include "core/d3d_present.h"
#include "core/d3d_texture.h"
#include "core/trace.h"
#include "core/yuy2_converter.h"

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace XTL
{
#include "d3d8_emulation.h"
#include "xg_emulation.h"
}; // namespace XTL

#include "runtime_resource_ids.gen.h"

#include <process.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <clocale>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

extern "C" bool EmuNv2aExecutePushBuffer(const DWORD* Buffer, DWORD Size);
extern "C" bool EmuNv2aExecuteGuestPushBuffer(DWORD GuestAddress, DWORD Size);
extern "C" void EmuNv2aEnableHleRaster();
extern "C" void EmuNv2aSetTransformConstant(ULONG HardwareIndex, const float* Value);
extern "C" bool EmuNv2aSetRenderState(ULONG Method, ULONG Value);

// ******************************************************************
// * Global(s)
// ******************************************************************
HWND XTL::g_hEmuWindow = NULL; // Rendering Window

// ******************************************************************
// * Static Function(s)
// ******************************************************************
static DWORD WINAPI EmuRenderWindow(LPVOID);
static DWORD WINAPI EmuCreateDeviceProxy(LPVOID);
static LRESULT WINAPI EmuMsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static DWORD WINAPI EmuUpdateTickCount(LPVOID);
static DWORD EmuCheckAllocationSize(LPVOID);
static inline void EmuVerifyResourceIsRegistered(XTL::X_D3DResource* pResource);
static void EmuRememberOwnedGuestResource(XTL::X_D3DResource* pResource);
static void EmuAdjustPower2(UINT* dwWidth, UINT* dwHeight);
static void EmuFlushTiledSurfaceLock(XTL::X_D3DResource* pResource);
static void EmuFlushTiledSurfaceLocks();
static HRESULT EmuLockTiledSurface(XTL::X_D3DResource* pResource, XTL::D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags);
static bool EmuD3DIsReadableRange(const void* base, DWORD bytes);
static void EmuCommitTextureContainerLocks();
static void EmuForgetTextureContainerState(XTL::IDirect3DBaseTexture8* pHostTexture);
static void EmuInvalidateTextureContainerForSurface(XTL::IDirect3DSurface8* pLevelHostSurface);
static bool EmuCopyRectsFromTargetNative(XTL::X_D3DSurface* pSourceSurface,
                                         XTL::X_D3DSurface* pDestinationSurface,
                                         CONST RECT* pSourceRectsArray,
                                         UINT cRects,
                                         CONST POINT* pDestPointsArray);

// ******************************************************************
// * Static Variable(s)
// ******************************************************************
static XTL::LPDIRECT3D8 g_pD3D8 = NULL;             // Direct3D8
static XTL::LPDIRECT3DDEVICE8 g_pD3DDevice8 = NULL; // Direct3D8 Device
static BOOL g_bSupportsYUY2 = FALSE;                // Does device support YUY2 overlays?
static BOOL g_bYuvEnable = FALSE;                   // D3DRS_YUVENABLE: sample bound textures as YUY2 and convert to RGB
static BOOL g_bStage0ConvertedYuv = FALSE;
static XTL::LPDIRECTDRAW7 g_pDD7 = NULL;                // DirectDraw7
static XTL::LPDIRECTDRAWSURFACE7 g_pDDSPrimary = NULL;  // DirectDraw7 Primary Surface
static XTL::LPDIRECTDRAWSURFACE7 g_pDDSOverlay7 = NULL; // DirectDraw7 Overlay Surface
static DWORD g_dwOverlayW = 640;                        // Cached Overlay Width
static DWORD g_dwOverlayH = 480;                        // Cached Overlay Height

// Converted RGB copy of the latest UpdateOverlay frame (owned by the YUY2
// conversion cache, do not Release); composited over the backbuffer at Swap
// while the title keeps the overlay enabled. Under the native flavor the
// frame stays a CPU BGRA block (the shared YUY2 conversion cache) and the
// backend composites it directly.
static XTL::IDirect3DTexture8* g_pOverlayFrameTexture = NULL;
static const BYTE* g_pOverlayFrameBgra = nullptr;
static DWORD g_dwOverlayFrameW = 0, g_dwOverlayFrameH = 0;
static std::atomic<std::uint32_t> g_OverlayFrameGeneration{ 0 };
static LARGE_INTEGER g_NextOverlayUpdate = {};
static LARGE_INTEGER g_NextSwapPresent = {};
static Xbe::Header* g_XbeHeader = NULL; // XbeHeader
static uint32 g_XbeHeaderSize = 0;      // XbeHeaderSize
static XTL::D3DCAPS8 g_D3DCaps;         // Direct3D8 Caps
static HBRUSH g_hBgBrush = NULL;        // Background Brush
static std::atomic_bool g_bRenderWindowActive{ false };
static XBVideo g_XBVideo;
static DWORD g_D3DDebugMarker = 0;
static uint08* g_pD3DPerfStatistics = NULL;
static volatile DWORD* g_pD3DSingleStepPusher = NULL;
static bool g_bD3DDebugGlobalsScanned = false;

struct EmuD3DPushBufferInfo
{
    DWORD PushBufferSize;
    DWORD PushSegmentSize;
    DWORD PushSegmentCount;
    DWORD* pPushBase;
    DWORD* pPushLimit;
    ULONGLONG PushBufferBytesWritten;
};

static_assert(sizeof(EmuD3DPushBufferInfo) == 32,
              "D3DPUSHBUFFERINFO must retain the Xbox ABI layout");

static std::array<DWORD, 16> g_D3DPerfPushBuffer = {};
static ULONGLONG g_D3DPerfPushBufferBytesWritten = 0;
static INT g_D3DPerfEventDepth = 0;

// Last D3D wrapper entered -- a lightweight (single store) entry trace so the
// vectored-exception dump can name the HLE wrapper that was on the call path
// when host d3d8 throws (e.g. the 0xE06D7363/D3DERR_NOTAVAILABLE storm on the
// Turok Evolution render path). Printed by the vectored handler in
// emulation_runtime.cpp via
// EmuGetLastD3DCall(). The store is now unconditional (it earned its keep on
// every Turok crash triage); CXBX_D3D_CALL_STATS additionally counts the
// calls per wrapper and dumps the table every 128 Swaps, which shows what a
// title's frame is actually made of (e.g. a black screen: are draws even
// running?).
static volatile const char* g_LastD3DCall = NULL;

#define EMU_D3D_CALL_STATS_SLOTS 96
struct EmuD3DCallStat
{
    const char* Name;
    DWORD Count;
};
static EmuD3DCallStat g_D3DCallStats[EMU_D3D_CALL_STATS_SLOTS] = { 0 };
static int g_D3DCallStatsEnabled = -1;

static bool EmuD3DReadEnvironment(const char* Name, char* Value,
                                  DWORD ValueCapacity)
{
    if(Value == NULL || ValueCapacity == 0)
    {
        return false;
    }

    Value[0] = '\0';
    const DWORD Length = GetEnvironmentVariableA(Name, Value, ValueCapacity);
    return Length != 0 && Length < ValueCapacity;
}

static bool EmuD3DEnvironmentEnabled(const char* Name)
{
    char Value[2] = {};
    return EmuD3DReadEnvironment(Name, Value, sizeof(Value));
}

static void EmuD3DTraceEntry(const char* Name)
{
    g_LastD3DCall = Name;

    if(g_D3DCallStatsEnabled < 0)
        g_D3DCallStatsEnabled = EmuD3DEnvironmentEnabled("CXBX_D3D_CALL_STATS") ? 1 : 0;

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

// Frame profile (CXBX_FRAME_PROFILE=1): coarse per-frame host-cost split for
// the push-buffer path, printed every 128 Swaps. POD-only timing helpers so
// the call sites stay legal inside __try bodies.
struct EmuFrameProfile
{
    bool enabled;
    bool checked;
    LARGE_INTEGER freq;
    double frameNs;
    double replayNs;
    double cpuDrawNs;
    double applyNs;
    DWORD frames;
    LARGE_INTEGER lastSwap;
};
static EmuFrameProfile g_FrameProfile = {};

static void EmuFrameProfileStart(LARGE_INTEGER& Start)
{
    if(!g_FrameProfile.checked)
    {
        g_FrameProfile.checked = true;
        g_FrameProfile.enabled = EmuD3DEnvironmentEnabled("CXBX_FRAME_PROFILE");
        QueryPerformanceFrequency(&g_FrameProfile.freq);
        QueryPerformanceCounter(&g_FrameProfile.lastSwap);
    }
    if(g_FrameProfile.enabled)
        QueryPerformanceCounter(&Start);
    else
        Start = g_FrameProfile.lastSwap;
}

static void EmuFrameProfileAccumulate(double& Total, const LARGE_INTEGER& Start)
{
    if(!g_FrameProfile.enabled)
        return;
    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);
    Total += static_cast<double>(end.QuadPart - Start.QuadPart) * 1000000000.0 /
             static_cast<double>(g_FrameProfile.freq.QuadPart);
}

static void EmuFrameProfileFrame(void)
{
    if(!g_FrameProfile.checked)
    {
        g_FrameProfile.checked = true;
        g_FrameProfile.enabled = EmuD3DEnvironmentEnabled("CXBX_FRAME_PROFILE");
        QueryPerformanceFrequency(&g_FrameProfile.freq);
        QueryPerformanceCounter(&g_FrameProfile.lastSwap);
    }
    if(!g_FrameProfile.enabled)
        return;

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    g_FrameProfile.frameNs += static_cast<double>(now.QuadPart - g_FrameProfile.lastSwap.QuadPart) *
                              1000000000.0 / static_cast<double>(g_FrameProfile.freq.QuadPart);
    g_FrameProfile.lastSwap = now;

    if(++g_FrameProfile.frames < 128)
        return;

    const double frameMs = g_FrameProfile.frameNs / 128.0 / 1000000.0;
    const double replayMs = g_FrameProfile.replayNs / 128.0 / 1000000.0;
    const double cpuDrawMs = g_FrameProfile.cpuDrawNs / 128.0 / 1000000.0;
    const double applyMs = g_FrameProfile.applyNs / 128.0 / 1000000.0;
    printf("FRMPROF| frame=%.2fms replay=%.2fms (cpuDraw=%.2fms apply=%.2fms) other=%.2fms\n",
           frameMs, replayMs, cpuDrawMs, applyMs, frameMs - replayMs);
    fflush(stdout);
    g_FrameProfile.frameNs = 0;
    g_FrameProfile.replayNs = 0;
    g_FrameProfile.cpuDrawNs = 0;
    g_FrameProfile.applyNs = 0;
    g_FrameProfile.frames = 0;
}

// Host-forward cache: the HLE re-forwards large volumes of unchanged state on
// every push-buffer decode (Turok Evolution: ~4,000 Set* calls per frame, most
// byte-identical). Each mirrored entry holds the last value actually sent to
// the host device; a wrapper skips its host call when the new value matches.
// The cache expires wholesale wherever host state changes outside the wrappers
// (state-block applies, direct helper calls), so a stale skip is impossible.
static DWORD g_EmuForwardedConstants[96 * 4];
static bool g_EmuForwardedConstantValid[96];
static DWORD g_EmuForwardedTransforms[256][16];
static bool g_EmuForwardedTransformValid[256];
static const void* g_EmuForwardedStreamResource[16];
static DWORD g_EmuForwardedStreamStride[16];
static bool g_EmuForwardedStreamValid[16];
static bool g_EmuForwardCacheExpired = true;

static void EmuForwardCacheFlush(void)
{
    g_EmuForwardCacheExpired = true;
}

static void EmuForwardCacheExpire(void)
{
    if(g_EmuForwardCacheExpired)
    {
        ZeroMemory(g_EmuForwardedConstantValid, sizeof(g_EmuForwardedConstantValid));
        ZeroMemory(g_EmuForwardedTransformValid, sizeof(g_EmuForwardedTransformValid));
        ZeroMemory(g_EmuForwardedStreamValid, sizeof(g_EmuForwardedStreamValid));
        g_EmuForwardCacheExpired = false;
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
extern "C" const char* EmuGetLastD3DCall(void)
{
    return (const char*)g_LastD3DCall;
}

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

// Native-Vulkan migration P1: with the backend presenting, the composed d3d8
// backbuffer is uploaded to the swapchain and the host device must not flip
// its own swapchain on the same window. The frame is copied 1:1 (the Xbox
// Present rect parameters stay d3d8-side until the render path migrates).
static bool EmuPresentViaVulkan()
{
    // P2 render path: the frame content lives in the backend render target;
    // present it directly instead of reading the d3d8 backbuffer.
    if(cxbx::d3d8::HostBackendRenders())
    {
        return cxbx::d3d8::HostBackendPresentFrame(nullptr, 0, 0, 0);
    }
    XTL::IDirect3DSurface8* backBuffer = nullptr;
    bool presented = false;
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
            if(SUCCEEDED(backBuffer->GetDesc(&description)) &&
               ((int)description.Format == 21 /*D3DFMT_A8R8G8B8*/ ||
                (int)description.Format == 22 /*D3DFMT_X8R8G8B8*/))
            {
                XTL::D3DLOCKED_RECT locked{};
                if(SUCCEEDED(backBuffer->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
                {
                    presented = cxbx::d3d8::HostBackendPresentFrame(
                        locked.pBits, description.Width, description.Height,
                        (unsigned)locked.Pitch);
                    backBuffer->UnlockRect();
                }
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        presented = false;
    }
    if(backBuffer != nullptr)
    {
        backBuffer->Release();
    }
    return presented;
}

// Whether the host presents in exclusive fullscreen. The Vulkan backend
// presents windowed only: its swapchain owns the emulator window and the
// host d3d8 device is parked on a hidden dummy window that must never own
// an exclusive mode (a fullscreen host Present would flip the dummy window
// and show nothing). Decided from the CXBX_HOST_BACKEND selection rather
// than HostBackendPresents(): the render window is created before the
// presenter exists, and window style, device mode, and Present routing
// must agree.
static bool EmuHostFullscreen()
{
    if(!g_XBVideo.GetFullscreen())
    {
        return false;
    }
    if(cxbx::d3d8::HostBackendFlavorFromEnvironment() !=
       cxbx::d3d8::HostBackendFlavor::Vulkan)
    {
        return true;
    }
    static bool s_Reported = false;
    if(!s_Reported)
    {
        s_Reported = true;
        printf("VULKAN| fullscreen video setting ignored: CXBX_HOST_BACKEND="
               "vulkan presents windowed\n");
    }
    return false;
}

static HRESULT EmuHostPresent(const RECT* sourceRect, const RECT* destinationRect,
                              HWND destinationWindow, const RGNDATA* dirtyRegion)
{
    if(cxbx::d3d8::HostBackendPresents())
    {
        if(EmuPresentViaVulkan())
        {
            return D3D_OK;
        }
        // The presenter cannot take this frame; d3d8 keeps the window alive.
    }
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

// Native-Vulkan migration P2: with the backend rendering, the guest-visible
// backbuffer is the Vulkan render target, read back into this shadow on
// demand (pixel probes and backbuffer LockRects).
static unsigned char* g_EmuVulkanShadow = nullptr;
static unsigned g_EmuVulkanShadowWidth = 0;
static unsigned g_EmuVulkanShadowHeight = 0;
static unsigned g_EmuVulkanShadowPitch = 0;

static bool EmuVulkanSyncBackbufferShadow()
{
    unsigned width = 0;
    unsigned height = 0;
    if(!cxbx::d3d8::HostBackendTargetSize(&width, &height) || width == 0 ||
       height == 0)
    {
        return false;
    }
    const unsigned pitch = width * 4;
    if(g_EmuVulkanShadowWidth != width || g_EmuVulkanShadowHeight != height)
    {
        delete[] g_EmuVulkanShadow;
        g_EmuVulkanShadow = new(std::nothrow) unsigned char[pitch * height];
        g_EmuVulkanShadowWidth = width;
        g_EmuVulkanShadowHeight = height;
        g_EmuVulkanShadowPitch = pitch;
    }
    if(g_EmuVulkanShadow == nullptr)
    {
        return false;
    }
    if(!cxbx::d3d8::HostBackendReadFrame(g_EmuVulkanShadow, pitch))
    {
        return false;
    }
    {
        static LONG dumpsWritten = 0;
        char enabled[8] = {};
        if(dumpsWritten < 4 &&
           GetEnvironmentVariableA("CXBX_VULKAN_DUMP_SHADOW", enabled,
                                   sizeof(enabled)) != 0 &&
           enabled[0] == '1')
        {
            ++dumpsWritten;
            char directory[MAX_PATH];
            directory[0] = '\0';
            GetTempPathA(MAX_PATH, directory);
            char path[MAX_PATH];
            wsprintfA(path, "%scxbx_vulkan_shadow_%ld.bmp", directory,
                      static_cast<long>(dumpsWritten));
            // Minimal 32-bit BMP: file header + info header + bottom-up rows.
            const unsigned size = pitch * height;
            const unsigned fileSize = 54 + size;
            unsigned char header[54] = {};
            header[0] = 'B';
            header[1] = 'M';
            memcpy(header + 2, &fileSize, 4);
            header[10] = 54;
            header[14] = 40;
            memcpy(header + 18, &width, 4);
            memcpy(header + 22, &height, 4);
            header[26] = 1;
            header[28] = 32;
            FILE* file = fopen(path, "wb");
            if(file != nullptr)
            {
                fwrite(header, 1, 54, file);
                for(int row = static_cast<int>(height) - 1; row >= 0; --row)
                {
                    fwrite(g_EmuVulkanShadow + static_cast<size_t>(row) * pitch,
                           1, pitch, file);
                }
                fclose(file);
                printf("VULKAN| shadow dump: %s\n", path);
            }
        }
    }
    return true;
}

static bool EmuReadHostBackbufferPixel(UINT x, UINT y, DWORD& pixel)
{
    if(cxbx::d3d8::HostBackendRenders())
    {
        if(!EmuVulkanSyncBackbufferShadow() || x >= g_EmuVulkanShadowWidth ||
           y >= g_EmuVulkanShadowHeight)
        {
            return false;
        }
        const BYTE* row = g_EmuVulkanShadow +
                          static_cast<std::size_t>(y) * g_EmuVulkanShadowPitch;
        std::memcpy(&pixel, row + static_cast<std::size_t>(x) * sizeof(pixel),
                    sizeof(pixel));
        return true;
    }

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
            cxbx::d3d8::HostBackendStateBlockApply(savedState);
            EmuForwardCacheFlush();
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
    // The probe draws through the HOST device by design (a host-d3d8
    // liveness diagnostic); under the native backend its host draws never
    // reach the present source, so it only measures the parked shadow.
    if(cxbx::d3d8::HostBackendRenders())
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
extern "C" void EmuNv2aBlitResolvedFrame();

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
                EmuNv2aBlitResolvedFrame();
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

    // A windowed host Present that silently starts failing (or recovering) is
    // exactly how a title "fades to black" while its backbuffer stays correct;
    // log every transition so display-path failures are visible in run logs.
    static HRESULT s_LastPresentResult = D3D_OK;
    if(presentResult != s_LastPresentResult)
    {
        printf("EmuD3D8 (0x%X): host Present result changed 0x%.08lX -> 0x%.08lX\n",
               GetCurrentThreadId(), static_cast<unsigned long>(s_LastPresentResult),
               static_cast<unsigned long>(presentResult));
        fflush(stdout);
        s_LastPresentResult = presentResult;
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

static DWORD* EmuD3DPerfApiCounters()
{
    return g_pD3DPerfStatistics == NULL ? NULL : (DWORD*)(g_pD3DPerfStatistics + EMU_D3DPERF_API_COUNTER_OFFSET);
}

// Locate the two debug-runtime globals without depending on title link
// addresses. D3DPERF_GetStatistics and D3DPERF_GetNames are consecutive,
// 16-byte-aligned accessors (mov eax, <static>; ret). Requiring the pair
// prevents unrelated accessors from being mistaken for the statistics block.
// The single-step reference is identified by its surrounding pusher validation
// sequence. Relocated operands are deliberately decoded rather than embedded
// in OOVPAs.
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

    Xbe::SectionHeader* pSections =
        (Xbe::SectionHeader*)((uint08*)g_XbeHeader + SectionOffset);
    uint08* pDebugStatisticsCandidate = NULL;

    for(uint32 SectionIndex = 0; SectionIndex < g_XbeHeader->dwSections; SectionIndex++)
    {
        Xbe::SectionHeader* pSection = &pSections[SectionIndex];
        if(!pSection->dwFlags.bExecutable || pSection->dwVirtualSize < 22 ||
           pSection->dwVirtualAddr < ImageBase ||
           pSection->dwVirtualAddr > ImageEnd ||
           pSection->dwVirtualSize > ImageEnd - pSection->dwVirtualAddr)
            continue;

        uint08* pCode = (uint08*)pSection->dwVirtualAddr;
        const uint32 ScanSize = pSection->dwVirtualSize - 21;
        for(uint32 Offset = 0; Offset < ScanSize; Offset++)
        {
            uint08* p = pCode + Offset;

            if(g_pD3DPerfStatistics == NULL &&
               p[0] == 0xB8 && p[5] == 0xC3 &&
               p[6] == 0xCC && p[7] == 0xCC && p[8] == 0xCC &&
               p[9] == 0xCC && p[10] == 0xCC && p[11] == 0xCC &&
               p[12] == 0xCC && p[13] == 0xCC && p[14] == 0xCC &&
               p[15] == 0xCC && p[16] == 0xB8 && p[21] == 0xC3)
            {
                const uint32 Statistics = *(uint32*)(p + 1);
                if(Statistics >= ImageBase &&
                   Statistics <= ImageEnd &&
                   EMU_D3DPERF_REQUIRED_SIZE <= ImageEnd - Statistics)
                {
                    g_pD3DPerfStatistics = (uint08*)Statistics;
                }
            }

            // The debug library places a regular function after the accessor
            // rather than GetNames. Retain that candidate, but prefer the
            // profile library's accessor pair if one appears later.
            if(g_pD3DPerfStatistics == NULL &&
               pDebugStatisticsCandidate == NULL &&
               p[0] == 0xB8 && p[5] == 0xC3 &&
               p[6] == 0xCC && p[7] == 0xCC && p[8] == 0xCC &&
               p[9] == 0xCC && p[10] == 0xCC && p[11] == 0xCC &&
               p[12] == 0xCC && p[13] == 0xCC && p[14] == 0xCC &&
               p[15] == 0xCC && p[16] == 0x55 && p[17] == 0x8B &&
               p[18] == 0xEC)
            {
                const uint32 Statistics = *(uint32*)(p + 1);
                if(Statistics >= ImageBase &&
                   Statistics <= ImageEnd &&
                   EMU_D3DPERF_REQUIRED_SIZE <= ImageEnd - Statistics)
                {
                    pDebugStatisticsCandidate = (uint08*)Statistics;
                }
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

    if(g_pD3DPerfStatistics == NULL)
    {
        g_pD3DPerfStatistics = pDebugStatisticsCandidate;
    }
}

#define EMU_D3DLOCK_TILED    0x40
#define EMU_D3DLOCK_READONLY 0x80

struct EmuTiledSurfaceLock
{
    XTL::X_D3DResource* pResource;
    XTL::IDirect3DSurface8* pSurface8;
    // Set when the host surface refused a direct lock (render targets are
    // not lockable in host d3d8, while every Xbox surface is): the guest
    // edits a system-memory image instead, and the commit copies it back
    // with CopyRects.
    XTL::IDirect3DSurface8* pStagingSurface8;
    bool StagingWritable;
    // Depth-stencil host surfaces can be neither locked nor copied: hand the
    // guest a zero-filled raw buffer and drop the contents at commit.
    uint08* pRawStaging;
    DWORD RawStagingSize;
};

static EmuTiledSurfaceLock g_TiledSurfaceLocks[8] = {};

// P8 C3-surface: the native byte container for surfaces, the exact analogue
// of the texture container. Under the Vulkan flavor, writable full-surface
// guest locks are served from container memory (the guest's edits land in
// native bytes immediately) and commits push the rows back into the host
// surface at the same flush points the tiled-lock machinery uses (Clear,
// Present, Swap, CopyRects, re-locks). UMA-aliased lock bits (surfaces whose
// granted pBits is the host mapping of emulated RAM) stay on the host path.
// Keyed by the host surface pointer; the later slices re-key on the guest
// resource when host objects go away.
//
// The registry is a fixed slot array under one mutex, NOT a vector: Turok
// decodes movies on a guest thread while the render thread presents, so a
// reallocating registry (or a forget that frees a served buffer) corrupted
// the heap deterministically at world load. Slots keep buffer pointers
// stable, and a slot referenced by an outstanding served lock is never
// reclaimed or resized.
#define EMU_SURFACE_CONTAINER_SLOTS 16

struct EmuNativeSurface
{
    XTL::IDirect3DSurface8* pHostSurface = nullptr;
    // Slice C: the GUEST surface pointer when known -- releases resolve by
    // it, so a recycled host pointer can never alias a live container.
    XTL::X_D3DResource* pGuestResource = nullptr;
    std::vector<BYTE> Pixels; // linear rows, host-format byte layout
    UINT Pitch = 0;
    UINT Width = 0;
    UINT Height = 0;
    UINT HostFormat = 0;
    bool Used = false;
};
static EmuNativeSurface g_EmuNativeSurfaces[EMU_SURFACE_CONTAINER_SLOTS] = {};

static std::mutex& EmuSurfaceContainerMutex()
{
    static std::mutex mutex;
    return mutex;
}

extern "C" ULONG EmuContiguousBlockBase(ULONG HostAddress, ULONG* BlockSize);

// Uncapped serve/commit tracing (CXBX_SURFACE_CONTAINER_TRACE=1): correlates
// the last container operations before a title fault.
static bool EmuSurfaceContainerTraceEnabled()
{
    static int enabled = -1;
    if(enabled < 0)
    {
        enabled = EmuD3DEnvironmentEnabled("CXBX_SURFACE_CONTAINER_TRACE")
                      ? 1
                      : 0;
    }
    return enabled != 0;
}

// Registry structure helpers; callers hold EmuSurfaceContainerMutex().
static EmuNativeSurface* EmuNativeSurfaceFind(
    XTL::IDirect3DSurface8* pHostSurface)
{
    for(auto& slot : g_EmuNativeSurfaces)
    {
        if(slot.Used && slot.pHostSurface == pHostSurface)
        {
            return &slot;
        }
    }
    return nullptr;
}

// POD-only __try helpers: functions that construct unwinding objects
// (std::vector containers) cannot carry their own __try bodies.
static bool EmuTryHostSurfaceDesc(XTL::IDirect3DSurface8* pHostSurface,
                                  XTL::D3DSURFACE_DESC* pDescription)
{
    __try
    {
        return SUCCEEDED(pHostSurface->GetDesc(pDescription));
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static void EmuSafeHostSurfaceUnlock(XTL::IDirect3DSurface8* pHostSurface)
{
    __try
    {
        pHostSurface->UnlockRect();
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static void EmuSafeHostTextureLevelUnlock(XTL::IDirect3DTexture8* pTexture8)
{
    __try
    {
        pTexture8->UnlockRect(0);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

// Outstanding served locks: the guest holds a pointer into container memory
// with no UnlockRect wrapper to observe, so commits run at the shared flush
// points instead.
struct EmuSurfaceContainerLock
{
    XTL::X_D3DResource* pResource = nullptr;
    XTL::IDirect3DSurface8* pSurface8 = nullptr;
    bool Writable = false;
    // Set while a commit is copying this slot's rows: a registry-full steal
    // must not retire (and thereby unprotect) a claim whose buffer the copy
    // is still reading.
    bool Committing = false;
    // Monotonic claim order: the registry-full steal retires the oldest.
    unsigned long ClaimSeq = 0;
};
static EmuSurfaceContainerLock g_EmuSurfaceContainerLocks[8] = {};
static unsigned long g_EmuSurfaceContainerLockSeq = 0;

// Lock-slot array helpers. The _Locked forms require the caller to hold
// EmuSurfaceContainerMutex(); the plain forms take it themselves. Slot
// addresses are stable (fixed array), so a pointer handed out alongside the
// mutex stays valid, but its claim must only be dropped under the mutex.
static EmuSurfaceContainerLock* EmuFindSurfaceContainerLockLocked(
    XTL::X_D3DResource* pResource)
{
    for(unsigned v = 0;
        v < sizeof(g_EmuSurfaceContainerLocks) / sizeof(g_EmuSurfaceContainerLocks[0]);
        v++)
    {
        if(g_EmuSurfaceContainerLocks[v].pResource == pResource)
        {
            return &g_EmuSurfaceContainerLocks[v];
        }
    }
    return nullptr;
}

static EmuSurfaceContainerLock* EmuFindSurfaceContainerLock(
    XTL::X_D3DResource* pResource)
{
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    return EmuFindSurfaceContainerLockLocked(pResource);
}

static EmuSurfaceContainerLock* EmuFindFreeSurfaceContainerLockLocked()
{
    EmuSurfaceContainerLock* pRetired = nullptr;
    for(unsigned v = 0;
        v < sizeof(g_EmuSurfaceContainerLocks) / sizeof(g_EmuSurfaceContainerLocks[0]);
        v++)
    {
        EmuSurfaceContainerLock& slot = g_EmuSurfaceContainerLocks[v];
        if(slot.pResource == NULL)
        {
            slot.ClaimSeq = ++g_EmuSurfaceContainerLockSeq;
            return &slot;
        }
        // Steal candidate: the oldest claim no commit is copying. A slot
        // mid-commit keeps its claim; the retired content was already
        // best-effort (the same retirement policy as the tiled-lock
        // registry).
        if(!slot.Committing &&
           (pRetired == nullptr || slot.ClaimSeq < pRetired->ClaimSeq))
        {
            pRetired = &slot;
        }
    }
    if(pRetired == nullptr)
    {
        // Every slot is mid-commit: hand out slot 0 to preserve liveness.
        pRetired = &g_EmuSurfaceContainerLocks[0];
    }
    pRetired->ClaimSeq = ++g_EmuSurfaceContainerLockSeq;
    return pRetired;
}

static void EmuReleaseSurfaceContainerLockSlotLocked(
    EmuSurfaceContainerLock* pSlot)
{
    pSlot->pResource = NULL;
    pSlot->pSurface8 = nullptr;
    pSlot->Writable = false;
    pSlot->Committing = false;
}

static void EmuReleaseSurfaceContainerLockSlot(
    EmuSurfaceContainerLock* pSlot)
{
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    EmuReleaseSurfaceContainerLockSlotLocked(pSlot);
}

// True when any outstanding served lock still hands out this surface's
// container buffer; the caller holds EmuSurfaceContainerMutex().
static bool EmuSurfaceSlotServedLocked(
    XTL::IDirect3DSurface8* pHostSurface)
{
    for(unsigned v = 0;
        v < sizeof(g_EmuSurfaceContainerLocks) / sizeof(g_EmuSurfaceContainerLocks[0]);
        v++)
    {
        if(g_EmuSurfaceContainerLocks[v].pSurface8 == pHostSurface)
        {
            return true;
        }
    }
    return false;
}

// Drops a container entry by GUEST surface pointer (slice C: releases
// resolve by guest identity, so a recycled host pointer can never alias a
// live container). A surface with an outstanding served lock keeps its
// slot (the buffer is live guest-visible memory); the caller's divergence
// concern is handled by re-seeding at the next full lock.
static void EmuNativeSurfaceForget(XTL::X_D3DResource* pGuestSurface)
{
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    for(auto& slot : g_EmuNativeSurfaces)
    {
        if(slot.Used && slot.pGuestResource == pGuestSurface &&
           !EmuSurfaceSlotServedLocked(slot.pHostSurface))
        {
            slot.Used = false;
            slot.pHostSurface = nullptr;
            slot.pGuestResource = nullptr;
            slot.Pixels.clear();
            slot.Pixels.shrink_to_fit();
            return;
        }
    }
}

// Pushes one outstanding served lock's container rows back into the host
// surface. A refusal (render targets and depth surfaces do not take host
// locks) keeps the container: the guest's bytes stay readable through later
// container-served locks. The open-lock entry is released only after the
// copy so the slot stays reserved (no eviction, no resize) throughout.
//
// Containers carry the same pattern-filled slop tail as YUY2 decode buffers:
// title decoders that assume Xbox pitch/size padding write PAST Pitch*Height,
// and an exactly-sized container turned that into host heap corruption.
#define EMU_SURFACE_CONTAINER_SLOP 0x4000
#define EMU_SURFACE_CONTAINER_SLOP_FILL 0xC5

static int s_SurfaceCommitRefusalWarnings = 0;

// Mutex-guarded container lookup for commit paths (separate helper: the
// commit body itself carries __try and must stay POD-only).
static EmuNativeSurface* EmuCommitAcquireContainerSurface(
    XTL::IDirect3DSurface8* pSurface8)
{
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    return pSurface8 != nullptr ? EmuNativeSurfaceFind(pSurface8) : nullptr;
}

// Mutex-carried slot claim for the commit body (separate helper: the commit
// body itself carries __try and must stay free of unwinding objects).
// Returns false when the slot is empty or another commit already owns it.
// The Committing flag keeps a registry-full steal from retiring this claim
// (and dropping the eviction protection its buffer relies on) until the
// copy ends.
static bool EmuClaimSurfaceContainerLockForCommit(
    EmuSurfaceContainerLock* pLock, XTL::IDirect3DSurface8** ppSurface8,
    bool* pWritable)
{
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    if(pLock->pResource == NULL || pLock->Committing)
    {
        return false;
    }
    *ppSurface8 = pLock->pSurface8;
    *pWritable = pLock->Writable;
    pLock->Committing = true;
    return true;
}

static void EmuCommitSurfaceContainerLock(EmuSurfaceContainerLock* pLock)
{
    XTL::IDirect3DSurface8* pSurface8 = nullptr;
    bool writable = false;
    if(!EmuClaimSurfaceContainerLockForCommit(pLock, &pSurface8, &writable))
    {
        return;
    }
    EmuNativeSurface* pNative = EmuCommitAcquireContainerSurface(pSurface8);

    if(pNative == nullptr || !writable)
    {
        EmuReleaseSurfaceContainerLockSlot(pLock);
        return;
    }

    XTL::D3DLOCKED_RECT locked = {};
    HRESULT hRet = D3DERR_INVALIDCALL;
    __try
    {
        hRet = pSurface8->LockRect(&locked, NULL, 0);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        hRet = D3DERR_INVALIDCALL;
    }
    if(FAILED(hRet))
    {
        if(EmuSurfaceContainerTraceEnabled())
            printf("SURF| commit tid=%X host=0x%.08lX %ux%u REFUSED\n",
                   GetCurrentThreadId(),
                   reinterpret_cast<unsigned long>(pSurface8),
                   static_cast<unsigned>(pNative->Width),
                   static_cast<unsigned>(pNative->Height));
        if(s_SurfaceCommitRefusalWarnings++ < 3)
        {
            printf("*Warning* surface container commit refused by the host "
                   "surface (content kept in the native container)\n");
        }
        EmuReleaseSurfaceContainerLockSlot(pLock);
        return;
    }

    const UINT rowBytes = min(pNative->Pitch, static_cast<UINT>(locked.Pitch));
    for(UINT y = 0; y < pNative->Height; ++y)
    {
        memcpy(static_cast<BYTE*>(locked.pBits) +
                   static_cast<std::size_t>(y) * locked.Pitch,
               pNative->Pixels.data() +
                   static_cast<std::size_t>(y) * pNative->Pitch,
               rowBytes);
    }

    // Report how far the title wrote past the nominal Pitch*Height block
    // (same diagnostic as the YUY2 tail: the overwrite is harmless here, but
    // an exactly-sized buffer would have corrupted the host heap).
    {
        BYTE* pSlop =
            pNative->Pixels.data() +
            static_cast<std::size_t>(pNative->Pitch) * pNative->Height;
        DWORD overrun = 0;
        for(DWORD i = EMU_SURFACE_CONTAINER_SLOP; i > 0; --i)
        {
            if(pSlop[i - 1] != EMU_SURFACE_CONTAINER_SLOP_FILL)
            {
                overrun = i;
                break;
            }
        }
        if(overrun != 0 && s_SurfaceCommitRefusalWarnings++ < 6)
        {
            printf("*Warning* surface container %ux%u was overwritten 0x%X "
                   "bytes past its %u-byte buffer\n",
                   static_cast<unsigned>(pNative->Width),
                   static_cast<unsigned>(pNative->Height),
                   static_cast<unsigned>(overrun),
                   static_cast<unsigned>(pNative->Pitch * pNative->Height));
        }
        if(overrun != 0)
        {
            memset(pSlop, EMU_SURFACE_CONTAINER_SLOP_FILL,
                   EMU_SURFACE_CONTAINER_SLOP);
        }
    }
    __try
    {
        pSurface8->UnlockRect();
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }

    if(EmuSurfaceContainerTraceEnabled())
        printf("SURF| commit tid=%X host=0x%.08lX %ux%u pitch=%lu ok\n",
               GetCurrentThreadId(),
               reinterpret_cast<unsigned long>(pSurface8),
               static_cast<unsigned>(pNative->Width),
               static_cast<unsigned>(pNative->Height),
               static_cast<unsigned long>(pNative->Pitch));

    EmuReleaseSurfaceContainerLockSlot(pLock);
}

static void EmuCommitSurfaceContainerLock(XTL::X_D3DResource* pResource)
{
    if(pResource == NULL)
    {
        return;
    }
    if(EmuSurfaceContainerLock* pLock = EmuFindSurfaceContainerLock(pResource))
    {
        EmuCommitSurfaceContainerLock(pLock);
    }
}

static void EmuCommitSurfaceContainerLocks()
{
    if(!cxbx::d3d8::HostBackendRenders())
    {
        return;
    }
    EmuSurfaceContainerLock* pending[sizeof(g_EmuSurfaceContainerLocks) / sizeof(g_EmuSurfaceContainerLocks[0])];
    unsigned count = 0;
    {
        // Snapshot the occupied slots under the mutex (slot addresses are
        // stable); the commits themselves copy rows and call the host, so
        // they run with the registry mutex released.
        std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
        for(unsigned v = 0;
            v < sizeof(g_EmuSurfaceContainerLocks) / sizeof(g_EmuSurfaceContainerLocks[0]);
            v++)
        {
            if(g_EmuSurfaceContainerLocks[v].pResource != NULL &&
               !g_EmuSurfaceContainerLocks[v].Committing)
            {
                pending[count++] = &g_EmuSurfaceContainerLocks[v];
            }
        }
    }
    for(unsigned v = 0; v < count; v++)
    {
        EmuCommitSurfaceContainerLock(pending[v]);
    }
}

// True when [pBase, pBase+size) is committed and readable. VirtualQuery
// walks without faulting: a faulting probe is NOT viable here because the
// vectored guest-trap machinery repairs the SEH chain on the first pass and
// a real fault then bypasses inner __except handlers entirely.
static bool EmuSpanReadable(const void* pBase, std::size_t Size)
{
    if(pBase == nullptr || Size == 0)
    {
        return false;
    }
    const BYTE* pCursor = static_cast<const BYTE*>(pBase);
    const BYTE* pEnd = pCursor + Size;
    while(pCursor < pEnd)
    {
        MEMORY_BASIC_INFORMATION info = {};
        if(VirtualQuery(pCursor, &info, sizeof(info)) != sizeof(info))
        {
            return false;
        }
        if(info.State != MEM_COMMIT)
        {
            return false;
        }
        if((info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0 ||
           info.Protect == 0)
        {
            return false;
        }
        const BYTE* pRegionEnd =
            static_cast<const BYTE*>(info.BaseAddress) + info.RegionSize;
        if(pRegionEnd <= pCursor)
        {
            return false;
        }
        pCursor = pRegionEnd;
    }
    return true;
}

// Seed-copy helper: some granted locks hand out pBits that is not safely
// readable for Pitch*Height (Turok's movie surfaces hit an unmapped page),
// and a faulting seed turns a granted lock into a failed one -- which the
// title's decoder then mishandles (it memcpy's into the zeroed pBits). The
// __try keeps the fault inside the boundary; the caller falls back to the
// host lock on failure. POD-only: FromLock carries the registry mutex.
static bool EmuSeedSurfaceContainerSafe(BYTE* pDst, UINT dstPitch,
                                        const void* pSrc, UINT srcPitch,
                                        UINT rows, UINT rowBytes)
{
    __try
    {
        for(UINT y = 0; y < rows; ++y)
        {
            memcpy(pDst + static_cast<std::size_t>(y) * dstPitch,
                   static_cast<const BYTE*>(pSrc) +
                       static_cast<std::size_t>(y) * srcPitch,
                   rowBytes);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return true;
}

// Re-serves a host lock the surface just granted from the container: the
// held host bits seed it, the host lock is released, and the guest edits
// native bytes that commits push back at the shared flush points. Called
// only after a successful host LockRect, so every host object here is alive
// -- the container never calls through a surface the guest may have freed.
// Registry structure mutations hold EmuSurfaceContainerMutex: guest movie
// decoders lock surfaces on their own thread while the render thread
// presents, so unsynchronized registry edits corrupted the heap.
static HRESULT EmuServeSurfaceContainerFromLock(
    XTL::X_D3DResource* pResource, XTL::IDirect3DSurface8* pSurface8,
    XTL::D3DLOCKED_RECT* pLockedRect, bool writable)
{
    // A serve is already outstanding for this resource: the guest keeps
    // editing the same container view. Re-seeding (or resizing) here would
    // free or stale-fill memory an earlier served pointer still references,
    // so discard the fresh host bits and hand back the live container.
    // A unique_lock (not a lock_guard): the claim below releases it manually
    // so EmuSafeHostSurfaceUnlock can run its SEH outside the registry
    // mutex -- the guard destructor must then be a no-op on return.
    std::unique_lock<std::mutex> lock(EmuSurfaceContainerMutex());
    EmuSurfaceContainerLock* pOpen = EmuFindSurfaceContainerLockLocked(pResource);
    if(pOpen != nullptr)
    {
        EmuNativeSurface* pServed = EmuNativeSurfaceFind(pSurface8);
        if(pServed == nullptr)
        {
            if(EmuSurfaceContainerTraceEnabled())
                printf("SURF| serve tid=%X res=0x%.08lX host=0x%.08lX FALLBACK "
                       "open-lock without container\n",
                       GetCurrentThreadId(),
                       reinterpret_cast<unsigned long>(pResource),
                       reinterpret_cast<unsigned long>(pSurface8));
            return D3DERR_INVALIDCALL;
        }
        EmuSafeHostSurfaceUnlock(pSurface8);
        pLockedRect->pBits = pServed->Pixels.data();
        pLockedRect->Pitch = static_cast<LONG>(pServed->Pitch);
        pOpen->Writable = pOpen->Writable || writable;
        if(EmuSurfaceContainerTraceEnabled())
            printf("SURF| serve tid=%X res=0x%.08lX host=0x%.08lX RELOCK %ux%u "
                   "pitch=%lu bits=0x%.08lX\n",
                   GetCurrentThreadId(),
                   reinterpret_cast<unsigned long>(pResource),
                   reinterpret_cast<unsigned long>(pSurface8),
                   static_cast<unsigned>(pServed->Width),
                   static_cast<unsigned>(pServed->Height),
                   static_cast<unsigned long>(pServed->Pitch),
                   reinterpret_cast<unsigned long>(pServed->Pixels.data()));
        return D3D_OK;
    }

    XTL::D3DSURFACE_DESC description = {};
    if(!EmuTryHostSurfaceDesc(pSurface8, &description) ||
       description.Width == 0 || description.Height == 0)
    {
        if(EmuSurfaceContainerTraceEnabled())
            printf("SURF| serve tid=%X res=0x%.08lX host=0x%.08lX FALLBACK "
                   "desc\n",
                   GetCurrentThreadId(),
                   reinterpret_cast<unsigned long>(pResource),
                   reinterpret_cast<unsigned long>(pSurface8));
        return D3DERR_INVALIDCALL;
    }

    EmuNativeSurface* pNative = EmuNativeSurfaceFind(pSurface8);
    if(pNative == nullptr)
    {
        // First free slot, then any slot no served lock references.
        for(auto& slot : g_EmuNativeSurfaces)
        {
            if(!slot.Used)
            {
                pNative = &slot;
                break;
            }
        }
        if(pNative == nullptr)
        {
            for(auto& slot : g_EmuNativeSurfaces)
            {
                if(!EmuSurfaceSlotServedLocked(slot.pHostSurface))
                {
                    pNative = &slot;
                    break;
                }
            }
        }
        if(pNative == nullptr)
        {
            return D3DERR_INVALIDCALL;
        }
        pNative->Used = true;
        pNative->pHostSurface = pSurface8;
        pNative->pGuestResource = pResource;
        pNative->HostFormat = static_cast<UINT>(description.Format);
    }
    if(pNative->Width != description.Width ||
       pNative->Height != description.Height ||
       pNative->Pitch != static_cast<UINT>(pLockedRect->Pitch))
    {
        pNative->Width = description.Width;
        pNative->Height = description.Height;
        pNative->Pitch = static_cast<UINT>(pLockedRect->Pitch);
        pNative->Pixels.resize(static_cast<std::size_t>(pNative->Pitch) *
                                   pNative->Height +
                               EMU_SURFACE_CONTAINER_SLOP);
    }
    else if(pNative->Pixels.size() ==
            static_cast<std::size_t>(pNative->Pitch) * pNative->Height)
    {
        // A container allocated before the slop tail existed.
        pNative->Pixels.resize(static_cast<std::size_t>(pNative->Pitch) *
                                   pNative->Height +
                               EMU_SURFACE_CONTAINER_SLOP);
    }
    memset(pNative->Pixels.data() +
               static_cast<std::size_t>(pNative->Pitch) * pNative->Height,
           EMU_SURFACE_CONTAINER_SLOP_FILL, EMU_SURFACE_CONTAINER_SLOP);

    // Seed the container with the content the host lock granted. If the
    // granted bits are not readable at this geometry, abandon the serve: the
    // guest keeps the held host lock and its host bits -- byte-for-byte the
    // pre-interception outcome (a served fallback here would turn a granted
    // lock into a failed one, which the title mishandles).
    const UINT rowBytes = min(pNative->Pitch, static_cast<UINT>(pLockedRect->Pitch));
    const LONG grantedPitch = pLockedRect->Pitch;
    const std::size_t grantedBytes =
        static_cast<std::size_t>(pNative->Pitch) * pNative->Height;

    // UMA alias detection: Turok's XMV player locks surfaces whose granted
    // pBits IS the host mapping of emulated RAM (physically-aliased
    // resources). The guest reads and writes that memory directly through
    // the resource's own pointers, so the interception must not stand
    // between them -- the guest keeps the host lock and its native bits.
    {
        ULONG contiguousBlockSize = 0;
        if(EmuContiguousBlockBase(
               static_cast<ULONG>(reinterpret_cast<uintptr_t>(
                   pLockedRect->pBits)),
               &contiguousBlockSize) != 0)
        {
            if(EmuSurfaceContainerTraceEnabled())
            {
                printf("SURF| serve tid=%X res=0x%.08lX host=0x%.08lX FALLBACK "
                       "uma bits=0x%.08lX\n",
                       GetCurrentThreadId(),
                       reinterpret_cast<unsigned long>(pResource),
                       reinterpret_cast<unsigned long>(pSurface8),
                       reinterpret_cast<unsigned long>(pLockedRect->pBits));
                fflush(stdout);
            }
            pNative->Used = false;
            pNative->pHostSurface = nullptr;
            pNative->Pixels.clear();
            pNative->Pixels.shrink_to_fit();
            return D3DERR_INVALIDCALL;
        }
    }

    const bool bitsReadable = EmuSpanReadable(pLockedRect->pBits, grantedBytes);
    if(EmuSurfaceContainerTraceEnabled())
    {
        printf("SURF| probe tid=%X res=0x%.08lX host=0x%.08lX %ux%u "
               "bits=0x%.08lX readable=%d\n",
               GetCurrentThreadId(),
               reinterpret_cast<unsigned long>(pResource),
               reinterpret_cast<unsigned long>(pSurface8),
               static_cast<unsigned>(pNative->Width),
               static_cast<unsigned>(pNative->Height),
               reinterpret_cast<unsigned long>(pLockedRect->pBits),
               bitsReadable ? 1 : 0);
        fflush(stdout);
    }
    if(!bitsReadable)
    {
        if(EmuSurfaceContainerTraceEnabled())
        {
            printf("SURF| serve tid=%X res=0x%.08lX host=0x%.08lX FALLBACK "
                   "bits unreadable\n",
                   GetCurrentThreadId(),
                   reinterpret_cast<unsigned long>(pResource),
                   reinterpret_cast<unsigned long>(pSurface8));
            fflush(stdout);
        }
        pNative->Used = false;
        pNative->pHostSurface = nullptr;
        pNative->Pixels.clear();
        pNative->Pixels.shrink_to_fit();
        return D3DERR_INVALIDCALL;
    }
    if(!EmuSeedSurfaceContainerSafe(pNative->Pixels.data(), pNative->Pitch,
                                    pLockedRect->pBits, pLockedRect->Pitch,
                                    pNative->Height, rowBytes))
    {
        if(EmuSurfaceContainerTraceEnabled())
        {
            printf("SURF| serve tid=%X res=0x%.08lX host=0x%.08lX FALLBACK "
                   "seed fault\n",
                   GetCurrentThreadId(),
                   reinterpret_cast<unsigned long>(pResource),
                   reinterpret_cast<unsigned long>(pSurface8));
            fflush(stdout);
        }
        pNative->Used = false;
        pNative->pHostSurface = nullptr;
        pNative->Pixels.clear();
        pNative->Pixels.shrink_to_fit();
        return D3DERR_INVALIDCALL;
    }

    EmuSurfaceContainerLock* pSlot = EmuFindFreeSurfaceContainerLockLocked();
    // Claim the open-lock entry before releasing the mutex so the container
    // slot this serve hands out is referenced (evict/forget-proof) from now
    // on. An occupied slot is evicted by dropping its entry (a 16-slots-deep
    // eviction means heavy churn; the stale content was already best-effort)
    // -- committing it here would deadlock on the registry mutex and would
    // release the freshly claimed entry.
    pSlot->pResource = pResource;
    pSlot->pSurface8 = pSurface8;
    pSlot->Writable = writable;
    pSlot->Committing = false;
    lock.unlock();

    // Release the host lock and hand the guest the container memory.
    EmuSafeHostSurfaceUnlock(pSurface8);
    pLockedRect->pBits = pNative->Pixels.data();
    pLockedRect->Pitch = static_cast<LONG>(pNative->Pitch);
    if(EmuSurfaceContainerTraceEnabled())
        printf("SURF| serve tid=%X res=0x%.08lX host=0x%.08lX %ux%u pitch=%lu "
               "fmt=%u bits=0x%.08lX (granted %lux%lu lockpitch=%ld)\n",
               GetCurrentThreadId(),
               reinterpret_cast<unsigned long>(pResource),
               reinterpret_cast<unsigned long>(pSurface8),
               static_cast<unsigned>(pNative->Width),
               static_cast<unsigned>(pNative->Height),
               static_cast<unsigned long>(pNative->Pitch),
               static_cast<unsigned>(pNative->HostFormat),
               reinterpret_cast<unsigned long>(pNative->Pixels.data()),
               static_cast<unsigned long>(description.Width),
               static_cast<unsigned long>(description.Height),
               grantedPitch);
    return D3D_OK;
}

#define EMU_YUY2_TEXTURE_SLOTS 16

struct EmuYuy2TextureInfo
{
    XTL::X_D3DResource* pHandle;
    uint08* pPixels;
    DWORD Width;
    DWORD Height;
    DWORD Pitch;
    ULONG RefCount;
    DWORD DataSize; // Pitch * Height (start of the canary tail)
    DWORD ContentHash;
    bool HashValid;
    bool OwnsPixels;
};

// The video decoder writes frames into pPixels through LockRect; a decoder
// that assumes Xbox pitch/size padding writes PAST Pitch*Height and, on a
// plain new[] block, corrupts the host heap (detected only much later by an
// unrelated free). Give every buffer a pattern-filled slop tail: moderate
// overwrites land harmlessly, and the Release path measures and reports how
// far the title actually wrote.
#define EMU_YUY2_TAIL_SLOP 0x4000
#define EMU_YUY2_TAIL_FILL 0xC5

extern "C" BYTE* EmuResolvePhysicalHostSpan(ULONG Address, ULONG Size); // emulation_runtime.cpp
static bool EmuD3DTexTraceEnabled();

static EmuYuy2TextureInfo g_Yuy2Textures[EMU_YUY2_TEXTURE_SLOTS] = {};

static EmuYuy2TextureInfo* EmuFindYuy2Texture(const XTL::X_D3DResource* pHandle)
{
    for(int i = 0; i < EMU_YUY2_TEXTURE_SLOTS; i++)
    {
        if(g_Yuy2Textures[i].pHandle == pHandle)
            return &g_Yuy2Textures[i];
    }

    return NULL;
}

static HRESULT EmuRegisterYuy2Texture(XTL::X_D3DResource* pHandle,
                                      uint08* pPixels,
                                      DWORD Width,
                                      DWORD Height,
                                      DWORD Pitch)
{
    if(pHandle == NULL || pPixels == NULL || Width == 0 || Height == 0 ||
       Width > MAXDWORD / 2)
    {
        return D3DERR_INVALIDCALL;
    }

    const DWORD MinimumPitch = Width * 2;
    if(Pitch < MinimumPitch || Height > MAXDWORD / Pitch)
    {
        return D3DERR_INVALIDCALL;
    }

    const DWORD DataSize = Pitch * Height;

    if(EmuD3DTexTraceEnabled())
    {
        printf("TEX| yuy2-register handle=0x%.08lX pixels=0x%.08lX %lux%lu pitch=%lu\n",
               (unsigned long)(uintptr_t)pHandle, (unsigned long)(uintptr_t)pPixels,
               (unsigned long)Width, (unsigned long)Height, (unsigned long)Pitch);
        fflush(stdout);
    }

    // The Xbox header's Data field carries a PHYSICAL address, and the movie
    // decoder writes its frames through the 0x8xxxxxxx physical alias -- so
    // trap-emulated stores land wherever the physical-span resolver sends
    // them. Registration must resolve the buffer the same way, with the same
    // helper, or the display converts from a different backing than the
    // decoder writes (observed as Turok's solid-green intro: the display read
    // untouched memory while frames landed in the tracked contiguous block).
    // Identity-mapped blocks resolve to the same pointer, so the historical
    // working cases are unchanged.
    {
        const ULONG RawAddress = (ULONG)(uintptr_t)pPixels;
        BYTE* pResolved = NULL;

        if(RawAddress < 0x10000000)
        {
            pResolved = EmuResolvePhysicalHostSpan(0x80000000u | RawAddress, DataSize);
        }
        if(pResolved == NULL)
        {
            pResolved = EmuResolvePhysicalHostSpan(RawAddress, DataSize);
        }

        if(pResolved != NULL && (BYTE*)(uintptr_t)RawAddress != pResolved &&
           EmuD3DIsReadableRange(pResolved, DataSize))
        {
            printf("EmuD3D8 (0x%X): YUY2 register resolved frame buffer 0x%.08X -> 0x%.08X (+0x%X).\n",
                   GetCurrentThreadId(), RawAddress,
                   (DWORD)(uintptr_t)pResolved, DataSize);
            fflush(stdout);
            pPixels = pResolved;
        }
    }

    if(!EmuD3DIsReadableRange(pPixels, DataSize))
    {
        // Nothing resolved and the raw range is not host-committed either:
        // back it on demand as a last resort so registration cannot fail
        // silently (an unregistered movie surface dies later on the unfilled
        // host-surface field).
        void* pCommitted = VirtualAlloc(pPixels, DataSize, MEM_COMMIT, PAGE_READWRITE);
        if(pCommitted == NULL)
        {
            pCommitted = VirtualAlloc(pPixels, DataSize, MEM_RESERVE | MEM_COMMIT,
                                      PAGE_READWRITE);
        }

        if(pCommitted == NULL || !EmuD3DIsReadableRange(pPixels, DataSize))
        {
            EmuWarning("YUY2 register: could not back frame buffer 0x%.08X (+0x%X)",
                       (DWORD)(uintptr_t)pPixels, DataSize);
            return D3DERR_INVALIDCALL;
        }

        printf("EmuD3D8 (0x%X): YUY2 register backed frame buffer 0x%.08X (+0x%X) on demand.\n",
               GetCurrentThreadId(), (DWORD)(uintptr_t)pPixels, DataSize);
        fflush(stdout);
    }

    EmuYuy2TextureInfo* pInfo = EmuFindYuy2Texture(pHandle);
    if(pInfo == NULL)
    {
        for(int i = 0; i < EMU_YUY2_TEXTURE_SLOTS; ++i)
        {
            if(g_Yuy2Textures[i].pHandle == NULL)
            {
                pInfo = &g_Yuy2Textures[i];
                break;
            }
        }
    }

    if(pInfo == NULL)
    {
        return E_OUTOFMEMORY;
    }
    if(pInfo->OwnsPixels)
    {
        return D3DERR_INVALIDCALL;
    }

    pInfo->pHandle = pHandle;
    pInfo->pPixels = pPixels;
    pInfo->Width = Width;
    pInfo->Height = Height;
    pInfo->Pitch = Pitch;
    pInfo->RefCount =
        (std::max)(1ul, pHandle->Common & X_D3DCOMMON_REFCOUNT_MASK);
    pInfo->DataSize = DataSize;
    pInfo->ContentHash = 0;
    pInfo->HashValid = false;
    pInfo->OwnsPixels = false;

    g_dwOverlayW = Width;
    g_dwOverlayH = Height;
    return D3D_OK;
}

static XTL::X_D3DTexture* EmuCreateYuy2Texture(DWORD Width, DWORD Height)
{
    for(int i = 0; i < EMU_YUY2_TEXTURE_SLOTS; i++)
    {
        EmuYuy2TextureInfo* pInfo = &g_Yuy2Textures[i];
        if(pInfo->pHandle != NULL)
            continue;

        DWORD Pitch = Width * 2;
        DWORD DataSize = (DWORD)((size_t)Pitch * Height);
        uint08* pPixels = new uint08[(size_t)DataSize + EMU_YUY2_TAIL_SLOP];
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
        pInfo->OwnsPixels = true;

        return (XTL::X_D3DTexture*)pInfo->pHandle;
    }

    return NULL;
}

static HRESULT EmuLockYuy2Texture(EmuYuy2TextureInfo* pInfo,
                                  XTL::D3DLOCKED_RECT* pLockedRect,
                                  CONST RECT* pRect)
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
static XTL::X_D3DSurface* g_pCachedRenderTarget = NULL;
static XTL::X_D3DSurface* g_pCachedZStencilSurface = NULL;
static XTL::X_D3DPushBuffer* g_pRecordingPushBuffer = NULL;
static DWORD g_dwVertexShaderUsage = 0;
static constexpr DWORD EmuXboxTextureStageCount = 4;
// Host state blocks retain COM objects, but Xbox push buffers retain guest
// resource identities whose host backing can change before replay.
static std::array<XTL::X_D3DResource*, EmuXboxTextureStageCount>
    g_EmuBoundGuestTextures = {};
static std::array<cxbx::d3d::RecordedTextureDescriptor, EmuXboxTextureStageCount>
    g_EmuBoundGuestTextureDescriptors = {};

struct EmuRecordedPushBufferDraw
{
    XTL::D3DPRIMITIVETYPE primitiveType = XTL::D3DPT_POINTLIST;
    UINT primitiveCount = 0;
    UINT stride = 0;
    DWORD stateBlock = 0;
    std::array<XTL::X_D3DResource*, EmuXboxTextureStageCount> guestTextures = {};
    std::array<cxbx::d3d::RecordedTextureDescriptor, EmuXboxTextureStageCount>
        guestTextureDescriptors = {};
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

struct EmuCreatedIndexBuffer
{
    XTL::X_D3DIndexBuffer* resource = nullptr;
    BYTE* guestData = nullptr;
    DWORD byteSize = 0;
};

static std::vector<EmuRecordedPushBuffer> g_EmuRecordedPushBuffers;
static std::vector<EmuRegisteredVertexBuffer> g_EmuRegisteredVertexBuffers;
static std::vector<EmuCreatedIndexBuffer> g_EmuCreatedIndexBuffers;
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
    if(stateBlock != 0)
    {
        cxbx::d3d8::HostBackendStateBlockCapture(stateBlock);
    }
    return stateBlock;
}

static void EmuDeleteD3DStateBlock(DWORD stateBlock)
{
    if(stateBlock == 0 || g_pD3DDevice8 == NULL)
    {
        return;
    }
    cxbx::d3d8::HostBackendStateBlockDelete(stateBlock);
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

static EmuCreatedIndexBuffer* EmuFindCreatedIndexBuffer(
    XTL::X_D3DIndexBuffer* indexBuffer)
{
    for(EmuCreatedIndexBuffer& created : g_EmuCreatedIndexBuffers)
    {
        if(created.resource == indexBuffer)
        {
            return &created;
        }
    }
    return nullptr;
}

static bool EmuTrackCreatedIndexBuffer(XTL::X_D3DIndexBuffer* indexBuffer,
                                       BYTE* guestData, DWORD byteSize)
{
    try
    {
        g_EmuCreatedIndexBuffers.push_back({ indexBuffer, guestData, byteSize });
        return true;
    }
    catch(...)
    {
        return false;
    }
}

static void EmuDiscardCreatedIndexBuffer(XTL::X_D3DIndexBuffer* indexBuffer)
{
    for(std::vector<EmuCreatedIndexBuffer>::iterator created =
            g_EmuCreatedIndexBuffers.begin();
        created != g_EmuCreatedIndexBuffers.end(); ++created)
    {
        if(created->resource == indexBuffer)
        {
            delete[] created->guestData;
            g_EmuCreatedIndexBuffers.erase(created);
            return;
        }
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

static cxbx::d3d::RecordedTextureDescriptor EmuCaptureRecordedTextureDescriptor(
    XTL::X_D3DResource* resource)
{
    if(resource == nullptr ||
       (reinterpret_cast<std::uintptr_t>(resource) & 0x80000000u) != 0 ||
       !EmuD3DIsReadableRange(resource, sizeof(XTL::X_D3DPixelContainer)))
    {
        return {};
    }

    const auto* texture =
        reinterpret_cast<const XTL::X_D3DPixelContainer*>(resource);
    if((texture->Common & X_D3DCOMMON_TYPE_MASK) != X_D3DCOMMON_TYPE_TEXTURE)
    {
        return {};
    }

    return cxbx::d3d::CaptureRecordedTextureDescriptor(
        texture->Common, texture->Data, texture->Format, texture->Size);
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
    draw.guestTextures = g_EmuBoundGuestTextures;
    draw.guestTextureDescriptors = g_EmuBoundGuestTextureDescriptors;

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

// Byte offset of EmuVshCpuVertex::fogValue (declared below the recorded-draw
// replay that consumes it); pinned to the layout by the static_assert at the
// definition.
constexpr unsigned int kEmuVshCpuVertexFogOffset = 140;

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

    // Under the native backend the replay draws into the Vulkan target
    // (the host shadow never reaches the present source); state blocks
    // resolve through the backend mirror. The recorded vertices carry the
    // CPU-fallback layout (EmuVshCpuVertex: diffuse at 20, texcoords at
    // 28, four sets).
    if(cxbx::d3d8::HostBackendRenders())
    {
        for(const EmuRecordedPushBufferDraw& draw : recording.draws)
        {
            if(draw.stateBlock != 0)
            {
                cxbx::d3d8::HostBackendStateBlockApply(draw.stateBlock);
            }
            cxbx::d3d8::HostBackendDrawUP(draw.primitiveType,
                                          draw.primitiveCount,
                                          draw.vertices.data(), draw.stride,
                                          20, 28, 4,
                                          kEmuVshCpuVertexFogOffset);
        }
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
XTL::X_D3DTILE XTL::EmuD3DTileCache[0x08] = { 0 };

// ******************************************************************
// * EmuD3DDeferredRenderState
// ******************************************************************
DWORD* XTL::EmuD3DDeferredRenderState;

// ******************************************************************
// * EmuD3DDeferredTextureState
// ******************************************************************
DWORD* XTL::EmuD3DDeferredTextureState;

// ******************************************************************
// * EmuD3D8CreateDeviceProxyData
// ******************************************************************
struct EmuD3D8CreateDeviceProxyData
{
    UINT Adapter;
    XTL::D3DDEVTYPE DeviceType;
    HWND hFocusWindow;
    DWORD BehaviorFlags;
    XTL::X_D3DPRESENT_PARAMETERS* pPresentationParameters;
    XTL::IDirect3DDevice8** ppReturnedDeviceInterface;
    volatile bool bReady;
    volatile HRESULT hRet;
} g_EmuD3D8CreateDeviceProxyData = { 0 };

// ******************************************************************
// * func: XTL::EmuD3DInit
// ******************************************************************
VOID XTL::EmuD3DInit(Xbe::Header* XbeHeader, uint32 XbeHeaderSize)
{
    cxbx::platform::GetSharedVideoConfig(g_XBVideo);

    // ******************************************************************
    // * store XbeHeader and XbeHeaderSize for further use
    // ******************************************************************
    g_XbeHeader = XbeHeader;
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

        PresParam.BackBufferWidth = 640;
        PresParam.BackBufferHeight = 480;
        PresParam.BackBufferFormat = 6; /* X_D3DFMT_A8R8G8B8 */
        PresParam.BackBufferCount = 1;
        PresParam.EnableAutoDepthStencil = TRUE;
        PresParam.AutoDepthStencilFormat = 0x2A; /* X_D3DFMT_D24S8 */
        PresParam.SwapEffect = XTL::D3DSWAPEFFECT_DISCARD;

        EmuSwapFS(); // XBox FS
        XTL::EmuIDirect3D8_CreateDevice(0, XTL::D3DDEVTYPE_HAL, 0, 0x00000040, &PresParam, &g_pD3DDevice8);
        EmuSwapFS(); // Win2k/XP FS
    }
}

// ******************************************************************
// * func: XTL::EmuD3DCleanup
// ******************************************************************
VOID XTL::EmuD3DCleanup()
{
    // Release the Vulkan presenter (swapchain, device, instance) before the
    // render window and host input wind down.
    cxbx::d3d8::HostBackendShutdown();

    cxbx::platform::ShutdownHostInput();

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

        LOGBRUSH logBrush = { BS_SOLID, RGB(0, 0, 0) };

        g_hBgBrush = CreateBrushIndirect(&logBrush);

        WNDCLASSEX wc = {
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
                Xbe::Certificate* XbeCert = (Xbe::Certificate*)((uint32)g_XbeHeader + CertAddr);

                setlocale(LC_ALL, "English");

                wcstombs(tAsciiTitle, XbeCert->wszTitleName, 40);
            }

            sprintf(AsciiTitle, "cxbx : Emulating %s", tAsciiTitle);
        }

        // ******************************************************************
        // * Create Window
        // ******************************************************************
        {
            DWORD dwStyle = WS_OVERLAPPEDWINDOW;

            int nTitleHeight = GetSystemMetrics(SM_CYCAPTION);
            int nBorderWidth = GetSystemMetrics(SM_CXSIZEFRAME);
            int nBorderHeight = GetSystemMetrics(SM_CYSIZEFRAME);

            int x = 100, y = 100, nWidth = 640, nHeight = 480;

            nWidth += nBorderWidth * 2;
            nHeight += nBorderHeight * 2 + nTitleHeight;

            sscanf(g_XBVideo.GetVideoResolution(), "%d x %d", &nWidth, &nHeight);

            if(EmuHostFullscreen())
            {
                x = y = nWidth = nHeight = 0;
                dwStyle = WS_POPUP;
            }

            XTL::g_hEmuWindow = CreateWindow(
                "CxbxRender", AsciiTitle,
                dwStyle, x, y, nWidth, nHeight,
                GetDesktopWindow(), NULL, GetModuleHandle(NULL), NULL);
        }
    }

    ShowWindow(XTL::g_hEmuWindow, SW_SHOWDEFAULT);
    UpdateWindow(XTL::g_hEmuWindow);

    // Native-Vulkan migration P0: honor CXBX_HOST_BACKEND and, when vulkan,
    // smoke-bootstrap a Vulkan device/swapchain on the render window.
    // Rendering still goes through the d3d8 backend in this phase.
    cxbx::d3d8::HostBackendInitialize(XTL::g_hEmuWindow);

    // ******************************************************************
    // * initialize direct input
    // ******************************************************************
    if(!cxbx::platform::InitializeHostInput())
        EmuCleanup("Could not initialize DirectInput!");

    printf("EmuD3D8 (0x%X): Message-Pump thread is running.\n", GetCurrentThreadId());

    // ******************************************************************
    // * message processing loop
    // ******************************************************************
    {
        MSG msg;

        ZeroMemory(&msg, sizeof(msg));
        g_bRenderWindowActive.store(true, std::memory_order_release);

        DWORD NextResolvedBlit = 0;
        while(msg.message != WM_QUIT)
        {
            const DWORD Now = GetTickCount();
            if(static_cast<LONG>(Now - NextResolvedBlit) >= 0)
            {
                EmuNv2aBlitResolvedFrame();
                NextResolvedBlit = Now + 16;
            }
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
            cxbx::platform::NotifyHostInputDeviceChange();
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
            if(EmuHostFullscreen())
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

                while(g_pD3DDevice8->Release() != 0)
                    ;

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
                g_EmuD3D8CreateDeviceProxyData.DeviceType = (g_XBVideo.GetDirect3DDevice() == 0) ? XTL::D3DDEVTYPE_HAL : XTL::D3DDEVTYPE_REF;
                g_EmuD3D8CreateDeviceProxyData.Adapter = g_XBVideo.GetDisplayAdapter();

                g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->Windowed = !EmuHostFullscreen();

                if(g_XBVideo.GetVSync())
                    g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->SwapEffect = XTL::D3DSWAPEFFECT_COPY_VSYNC;

                // Under the Vulkan render path the backend swapchain owns
                // the emulator window's flip (P1); the host d3d8 device must
                // not bind its own swapchain to the same window — two
                // presenters on one HWND crash the NVIDIA user-mode driver
                // (Turok Evolution's attract transition). Park the host
                // device on a hidden dummy window instead; its Present is
                // never reached (EmuHostPresent routes to the backend).
                HWND hostDeviceWindow = XTL::g_hEmuWindow;
                if(cxbx::d3d8::HostBackendPresents())
                {
                    static HWND s_HostDummyWindow = NULL;
                    if(s_HostDummyWindow == NULL)
                    {
                        s_HostDummyWindow = CreateWindowExA(
                            WS_EX_TOOLWINDOW, "STATIC", "CxbxHostDevice",
                            WS_POPUP, 0, 0, 8, 8, NULL, NULL,
                            GetModuleHandleA(NULL), NULL);
                        if(s_HostDummyWindow == NULL)
                        {
                            EmuWarning("Host dummy device window creation "
                                       "failed (0x%.08X)",
                                       static_cast<unsigned>(GetLastError()));
                        }
                    }
                    if(s_HostDummyWindow != NULL)
                    {
                        hostDeviceWindow = s_HostDummyWindow;
                    }
                }
                g_EmuD3D8CreateDeviceProxyData.hFocusWindow = hostDeviceWindow;
                g_EmuD3D8CreateDeviceProxyData.pPresentationParameters
                    ->hDeviceWindow = hostDeviceWindow;

                g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->BackBufferFormat = XTL::EmuXB2PC_D3DFormat(g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->BackBufferFormat);
                g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->AutoDepthStencilFormat = XTL::EmuXB2PC_D3DFormat(g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->AutoDepthStencilFormat);

                if(!g_XBVideo.GetVSync() && (g_D3DCaps.PresentationIntervals & D3DPRESENT_INTERVAL_IMMEDIATE) && EmuHostFullscreen())
                    g_EmuD3D8CreateDeviceProxyData.pPresentationParameters->FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
                else
                {
                    if(g_D3DCaps.PresentationIntervals & D3DPRESENT_INTERVAL_ONE && EmuHostFullscreen())
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
            g_EmuD3D8CreateDeviceProxyData.hRet = g_pD3D8->CreateDevice(
                g_EmuD3D8CreateDeviceProxyData.Adapter,
                g_EmuD3D8CreateDeviceProxyData.DeviceType,
                g_EmuD3D8CreateDeviceProxyData.hFocusWindow,
                g_EmuD3D8CreateDeviceProxyData.BehaviorFlags,
                (XTL::D3DPRESENT_PARAMETERS*)g_EmuD3D8CreateDeviceProxyData.pPresentationParameters,
                g_EmuD3D8CreateDeviceProxyData.ppReturnedDeviceInterface);

            // ******************************************************************
            // * report error
            // ******************************************************************
            if(FAILED(g_EmuD3D8CreateDeviceProxyData.hRet))
            {
                XTL::X_D3DPRESENT_PARAMETERS* pp = g_EmuD3D8CreateDeviceProxyData.pPresentationParameters;
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

                EmuCleanup("IDirect3D8::CreateDevice failed (Unknown 0x%.08X)",
                           g_EmuD3D8CreateDeviceProxyData.hRet);
            }

            // ******************************************************************
            // * it is necessary to store this pointer globally for emulation
            // ******************************************************************
            g_pD3DDevice8 = *g_EmuD3D8CreateDeviceProxyData.ppReturnedDeviceInterface;

            // Native-Vulkan migration P2: size the backend render target to
            // the emulated device's backbuffer.
            cxbx::d3d8::HostBackendSetTargetSize(
                g_EmuD3D8CreateDeviceProxyData.pPresentationParameters
                    ->BackBufferWidth,
                g_EmuD3D8CreateDeviceProxyData.pPresentationParameters
                    ->BackBufferHeight);

            // Xbox D3D starts fixed-function lighting disabled, unlike host
            // Direct3D 8. Titles may rely on this default before their first
            // render-state write.
            g_pD3DDevice8->SetRenderState(XTL::D3DRS_LIGHTING, FALSE);

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
                EmuRememberOwnedGuestResource(g_pCachedRenderTarget);
                g_pD3DDevice8->GetRenderTarget(&g_pCachedRenderTarget->EmuSurface8);

                g_pCachedZStencilSurface = new XTL::X_D3DSurface();
                EmuRememberOwnedGuestResource(g_pCachedZStencilSurface);
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
// Host-side D3D objects we created (device creates and Register). A non-zero
// Lock that is NOT one of these belongs to a NATIVE-created Xbox resource
// whose Lock field holds a guest lock cookie -- treating it as a host
// interface hands garbage to the host device (silently no-ops under the SEH
// guards) and the resource renders black.
static std::vector<void*> g_EmuKnownHostResources;
static std::vector<XTL::X_D3DResource*> g_EmuOwnedGuestResources;

static void EmuRememberHostResource(void* pHostResource)
{
    if(pHostResource == nullptr)
        return;
    for(void* known : g_EmuKnownHostResources)
        if(known == pHostResource)
            return;
    g_EmuKnownHostResources.push_back(pHostResource);
}

static bool EmuIsKnownHostResource(void* pHostResource)
{
    for(void* known : g_EmuKnownHostResources)
        if(known == pHostResource)
            return true;
    return false;
}

static void EmuForgetHostResource(void* pHostResource)
{
    for(auto resource = g_EmuKnownHostResources.begin();
        resource != g_EmuKnownHostResources.end(); ++resource)
    {
        if(*resource == pHostResource)
        {
            g_EmuKnownHostResources.erase(resource);
            return;
        }
    }
}

static void EmuRememberOwnedGuestResource(XTL::X_D3DResource* pResource)
{
    if(pResource != NULL)
    {
        g_EmuOwnedGuestResources.push_back(pResource);
    }
}

static bool EmuForgetOwnedGuestResource(XTL::X_D3DResource* pResource)
{
    for(auto resource = g_EmuOwnedGuestResources.begin();
        resource != g_EmuOwnedGuestResources.end(); ++resource)
    {
        if(*resource == pResource)
        {
            g_EmuOwnedGuestResources.erase(resource);
            return true;
        }
    }

    return false;
}

static inline void EmuVerifyResourceIsRegistered(XTL::X_D3DResource* pResource)
{
    bool bNeedsRegister = pResource->Lock == 0;

    // A texture-typed resource created by the NATIVE runtime (unmatched
    // CreateTexture2) carries a full Xbox header and a guest Lock value;
    // register it so a real host texture backs it.
    if(!bNeedsRegister &&
       (pResource->Common & X_D3DCOMMON_TYPE_MASK) == X_D3DCOMMON_TYPE_TEXTURE &&
       !EmuIsKnownHostResource((void*)pResource->Lock))
    {
        bNeedsRegister = true;
    }

    if(bNeedsRegister)
    {
        EmuSwapFS(); // XBox FS;
        XTL::EmuIDirect3DResource8_Register(pResource, 0 /*(PVOID)pResource->Data*/);
        EmuSwapFS(); // Win2k/XP FS
        EmuRememberHostResource((void*)pResource->Lock);
    }
}

static EmuYuy2TextureInfo* EmuResolveYuy2Texture(XTL::X_D3DResource* pResource)
{
    EmuYuy2TextureInfo* pInfo = EmuFindYuy2Texture(pResource);
    if(pInfo == NULL && pResource != NULL &&
       (reinterpret_cast<uintptr_t>(pResource) & 0x80000000u) == 0)
    {
        EmuVerifyResourceIsRegistered(pResource);
        pInfo = EmuFindYuy2Texture(pResource);
    }

    return pInfo;
}

// ******************************************************************
// * func: EmuAdjustPower2
// ******************************************************************
static void EmuAdjustPower2(UINT* dwWidth, UINT* dwHeight)
{
    UINT NewWidth = 0, NewHeight = 0;

    int v;

    for(v = 0; v < 32; v++)
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
static EmuTiledSurfaceLock* EmuFindTiledSurfaceLock(XTL::X_D3DResource* pResource)
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
static EmuTiledSurfaceLock* EmuFindFreeTiledSurfaceLock()
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
static void EmuCommitTiledSurfaceLock(EmuTiledSurfaceLock* pLock)
{
    if(pLock->pRawStaging != NULL)
    {
        // Depth-stencil host surfaces can be neither locked nor copied back.
        // A title that fills its depth surface from the CPU (an Xbox idiom:
        // clear the depth buffer through a tiled lock) has its uniform fill
        // mirrored into the host depth buffer with Clear; non-uniform edits
        // are dropped.
        DWORD dwValue = 0;
        bool uniform = pLock->RawStagingSize > 0;
        for(DWORD offset = 0; offset + 4 <= pLock->RawStagingSize; offset += 4)
        {
            DWORD word;
            memcpy(&word, pLock->pRawStaging + offset, 4);
            if(offset == 0)
                dwValue = word;
            else if(word != dwValue)
            {
                uniform = false;
                break;
            }
        }
        if(uniform)
        {
            float depth;
            memcpy(&depth, &dwValue, 4);
            __try
            {
                g_pD3DDevice8->Clear(0, NULL, D3DCLEAR_ZBUFFER, 0, depth, 0);
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        static int s_RawDepthCommits = 0;
        if(s_RawDepthCommits++ < 3)
            printf("*Warning* staged depth commit %ux uniform=0x%.08X (%s)\n",
                   pLock->RawStagingSize, dwValue, uniform ? "mirrored" : "dropped");
        free(pLock->pRawStaging);
        pLock->pRawStaging = NULL;
        pLock->RawStagingSize = 0;
        return;
    }

    if(pLock->pStagingSurface8 != NULL)
    {
        // Staged lock: the host surface was never directly locked. Hand the
        // edited image back with CopyRects (read-only locks skip the copy).
        pLock->pStagingSurface8->UnlockRect();
        if(pLock->StagingWritable)
        {
            __try
            {
                const HRESULT CopyResult = g_pD3DDevice8->CopyRects(
                    pLock->pStagingSurface8, NULL, 0, pLock->pSurface8, NULL);
                if(FAILED(CopyResult))
                {
                    printf("*Warning* staged surface commit failed (0x%.08X)\n",
                           static_cast<uint32>(CopyResult));
                }
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                EmuWarning("staged surface commit caught a host fault");
            }
        }
        pLock->pStagingSurface8->Release();
        pLock->pStagingSurface8 = NULL;
        pLock->StagingWritable = false;
        return;
    }

    // The guest UnlockRect is not HLE-patched in this tree. Commit the host
    // surface before presenting; otherwise D3D8 can display stale/partial data.
    pLock->pSurface8->UnlockRect();
}

// ******************************************************************
// * func: EmuFlushTiledSurfaceLock
// ******************************************************************
static void EmuFlushTiledSurfaceLock(XTL::X_D3DResource* pResource)
{
    EmuTiledSurfaceLock* pLock = EmuFindTiledSurfaceLock(pResource);
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
static HRESULT EmuLockTiledSurface(XTL::X_D3DResource* pResource, XTL::D3DLOCKED_RECT* pLockedRect, CONST RECT* pRect, DWORD Flags)
{
    if(pLockedRect == NULL)
        return D3DERR_INVALIDCALL;

    XTL::IDirect3DSurface8* pSurface8 = pResource->EmuSurface8;
    if(pSurface8 == NULL)
        return D3DERR_INVALIDCALL;

    EmuFlushTiledSurfaceLock(pResource);
    EmuCommitSurfaceContainerLock(pResource);

    EmuTiledSurfaceLock* pLock = EmuFindFreeTiledSurfaceLock();
    if(pLock->pResource != NULL)
        EmuFlushTiledSurfaceLock(pLock->pResource);

    pSurface8->UnlockRect();

    DWORD NewFlags = 0;
    if(Flags & EMU_D3DLOCK_READONLY)
        NewFlags |= D3DLOCK_READONLY;

    HRESULT hRet = pSurface8->LockRect(pLockedRect, pRect, NewFlags);
    if(FAILED(hRet))
    {
        // Host render targets (and other non-lockable host surfaces) refuse
        // LockRect, while every Xbox surface is lockable. Stage the lock
        // through a system-memory image: the guest edits the image, and the
        // commit copies it back with CopyRects.
        XTL::D3DSURFACE_DESC Desc = {};
        __try
        {
            hRet = pSurface8->GetDesc(&Desc);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            hRet = D3DERR_INVALIDCALL;
        }
        if(FAILED(hRet) || Desc.Width == 0 || Desc.Height == 0)
        {
            printf("*Warning* staged lock: GetDesc failed (hr=0x%.08X, %lux%lu)\n",
                   static_cast<uint32>(hRet),
                   static_cast<unsigned long>(Desc.Width),
                   static_cast<unsigned long>(Desc.Height));
            return D3DERR_INVALIDCALL;
        }

        XTL::IDirect3DSurface8* pStaging = NULL;
        HRESULT StagingResult = D3DERR_INVALIDCALL;
        __try
        {
            StagingResult = g_pD3DDevice8->CreateImageSurface(
                Desc.Width, Desc.Height, Desc.Format, &pStaging);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            StagingResult = D3DERR_INVALIDCALL;
        }
        if(FAILED(StagingResult) || pStaging == NULL)
        {
            // Image surfaces refuse depth-stencil formats, and host d3d8
            // offers no depth readback at all. Hand the guest a zero-filled
            // raw buffer instead of failing the lock (a failed Xbox lock is
            // followed by a write through the returned pointer).
            const int HostFormat = (int)Desc.Format;
            const bool DepthFormat =
                HostFormat == 75 /*D3DFMT_D24S8*/ || HostFormat == 80 /*D3DFMT_D16*/ ||
                HostFormat == 71 /*D3DFMT_D32*/ || HostFormat == 73 /*D3DFMT_D15S1*/ ||
                HostFormat == 74 /*D3DFMT_D24X8*/ || HostFormat == 79 /*D3DFMT_D24X4S4*/;
            if(!DepthFormat)
            {
                printf("*Warning* staged lock: CreateImageSurface %ux%u fmt=%d failed "
                       "(hr=0x%.08X)\n",
                       (unsigned)Desc.Width, (unsigned)Desc.Height,
                       HostFormat, static_cast<uint32>(StagingResult));
                return D3DERR_INVALIDCALL;
            }

            const DWORD BytesPerPixel = 4;
            const DWORD RawPitch = Desc.Width * BytesPerPixel;
            const DWORD RawSize = RawPitch * Desc.Height;
            uint08* pRaw = (uint08*)malloc(RawSize);
            if(pRaw == NULL)
                return E_OUTOFMEMORY;
            memset(pRaw, 0, RawSize);

            pLockedRect->pBits = pRaw;
            pLockedRect->Pitch = (LONG)RawPitch;

            pLock->pResource = pResource;
            pLock->pSurface8 = pSurface8;
            pLock->pSurface8->AddRef();
            pLock->pStagingSurface8 = NULL;
            pLock->StagingWritable = false;
            pLock->pRawStaging = pRaw;
            pLock->RawStagingSize = RawSize;

            printf("*Warning* staged depth lock %ux%u as raw buffer (contents "
                   "not preserved)\n",
                   (unsigned)Desc.Width, (unsigned)Desc.Height);
            return D3D_OK;
        }

        __try
        {
            // Seed the staging copy so read-modify-write locks see the
            // current content; a refusal here only means stale image data.
            g_pD3DDevice8->CopyRects(pSurface8, NULL, 0, pStaging, NULL);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
        }

        StagingResult = pStaging->LockRect(pLockedRect, pRect, NewFlags);
        if(FAILED(StagingResult))
        {
            pStaging->Release();
            return StagingResult;
        }

        pLock->pResource = pResource;
        pLock->pSurface8 = pSurface8;
        pLock->pSurface8->AddRef();
        pLock->pStagingSurface8 = pStaging;
        pLock->StagingWritable = (Flags & EMU_D3DLOCK_READONLY) == 0;

        return D3D_OK;
    }

    pLock->pResource = pResource;
    pLock->pSurface8 = pSurface8;
    pLock->pSurface8->AddRef();

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3D8_CreateDevice
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3D8_CreateDevice(
    UINT Adapter,
    D3DDEVTYPE DeviceType,
    HWND hFocusWindow,
    DWORD BehaviorFlags,
    X_D3DPRESENT_PARAMETERS* pPresentationParameters,
    IDirect3DDevice8** ppReturnedDeviceInterface)
{
    D3D_TRACE("CreateDevice");
    EmuSwapFS(); // Win2k/XP FS

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

    EmuSwapFS(); // XBox FS

    return g_EmuD3D8CreateDeviceProxyData.hRet;
}

// ******************************************************************
// * func: EmuIDirect3D8_GetAdapterModeCount
// ******************************************************************
UINT WINAPI XTL::EmuIDirect3D8_GetAdapterModeCount(
    UINT Adapter)
{
    D3D_TRACE("GetAdapterModeCount");
    EmuSwapFS(); // Win2k/XP FS

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

    EmuSwapFS(); // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3D8_GetAdapterDisplayMode
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3D8_GetAdapterDisplayMode(
    UINT Adapter,
    X_D3DDISPLAYMODE* pMode)
{
    D3D_TRACE("GetAdapterDisplayMode");
    EmuSwapFS(); // Win2k/XP FS

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
    HRESULT hRet = g_pD3D8->GetAdapterDisplayMode(
        g_XBVideo.GetDisplayAdapter(),
        (D3DDISPLAYMODE*)pMode);

    // ******************************************************************
    // * make adjustments to parameters to make sense with windows d3d
    // ******************************************************************
    {
        D3DDISPLAYMODE* pPCMode = (D3DDISPLAYMODE*)pMode;

        // Convert Format (PC->Xbox)
        pMode->Format = EmuPC2XB_D3DFormat(pPCMode->Format);

        // TODO: Make this configurable in the future?
        // D3DPRESENTFLAG_FIELD | D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_LOCKABLE_BACKBUFFER
        pMode->Flags = 0x000000A1;
    }

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3D8_EnumAdapterModes
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3D8_EnumAdapterModes(
    UINT Adapter,
    UINT Mode,
    X_D3DDISPLAYMODE* pMode)
{
    D3D_TRACE("EnumAdapterModes");
    EmuSwapFS(); // Win2k/XP FS

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
        D3DDISPLAYMODE* pPCMode = (D3DDISPLAYMODE*)pMode;

        // Convert Format (PC->Xbox)
        pMode->Format = EmuPC2XB_D3DFormat(pPCMode->Format);

        // TODO: Make this configurable in the future?
        // D3DPRESENTFLAG_FIELD | D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_LOCKABLE_BACKBUFFER
        pMode->Flags = 0x000000A1;
    }

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3D8_KickOffAndWaitForIdle
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3D8_KickOffAndWaitForIdle()
{
    D3D_TRACE("KickOffAndWaitForIdle");
    EmuSwapFS(); // Win2k/XP FS

// ******************************************************************
// * debug trace
// ******************************************************************
#ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3D8_KickOffAndWaitForIdle()\n", GetCurrentThreadId());
    }
#endif

    // TODO: Actually do something here?

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_AddRef
// ******************************************************************
ULONG WINAPI XTL::EmuIDirect3DDevice8_AddRef()
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BeginStateBlock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_BeginStateBlock()
{
    EmuSwapFS(); // Win2k/XP FS
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

    if(SUCCEEDED(static_cast<HRESULT>(ret)))
    {
        cxbx::d3d8::HostBackendStateBlockBegin();
    }

    EmuSwapFS(); // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CaptureStateBlock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CaptureStateBlock(DWORD Token)
{
    EmuSwapFS(); // Win2k/XP FS
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

    if(SUCCEEDED(static_cast<HRESULT>(ret)))
    {
        cxbx::d3d8::HostBackendStateBlockCapture(Token);
    }

    EmuSwapFS(); // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_ApplyStateBlock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_ApplyStateBlock(DWORD Token)
{
    EmuSwapFS(); // Win2k/XP FS
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

    if(SUCCEEDED(static_cast<HRESULT>(ret)))
    {
        cxbx::d3d8::HostBackendStateBlockApply(Token);
    }
    EmuForwardCacheFlush();

    EmuSwapFS(); // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_EndStateBlock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_EndStateBlock(DWORD* pToken)
{
    EmuSwapFS(); // Win2k/XP FS
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

    if(SUCCEEDED(static_cast<HRESULT>(ret)) && pToken != NULL)
    {
        cxbx::d3d8::HostBackendStateBlockEnd(*pToken);
    }

    EmuSwapFS(); // XBox FS

    return ret;
}

// POD-only __try helper: locks the destination surface for a native copy.
static bool EmuLockSurfaceForWrite(XTL::IDirect3DSurface8* pHostSurface,
                                   XTL::D3DLOCKED_RECT* pLocked)
{
    __try
    {
        return SUCCEEDED(pHostSurface->LockRect(pLocked, NULL, 0));
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool EmuLockSurfaceForRead(XTL::IDirect3DSurface8* pHostSurface,
                                  XTL::D3DLOCKED_RECT* pLocked)
{
    __try
    {
        return SUCCEEDED(pHostSurface->LockRect(pLocked, NULL,
                                                D3DLOCK_READONLY));
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// P8 C3: serves whole-surface and rect-list like-format CopyRects whose
// source is a backend registry target. Reads the target's live pixels (the
// host surface holds stale bytes under the render path) and writes the
// destination -- host surface, plus the served container view when one is
// outstanding. Guest-only destinations (created by the unpatched guest
// library, no host surface) receive the rows in their guest-RAM Data
// allocation, which is the memory the title's own locks read. Returns false
// when the copy does not apply and the host forward should run instead.
static bool EmuCopyRectsFromTargetNative(
    XTL::X_D3DSurface* pSourceSurface, XTL::X_D3DSurface* pDestinationSurface,
    CONST RECT* pSourceRectsArray, UINT cRects, CONST POINT* pDestPointsArray)
{
    if(EmuSurfaceContainerTraceEnabled())
    {
        printf("SURF| rt-copy enter src_host=0x%.08lX dst_host=0x%.08lX\n",
               reinterpret_cast<unsigned long>(pSourceSurface->EmuSurface8),
               reinterpret_cast<unsigned long>(pDestinationSurface->EmuSurface8));
        fflush(stdout);
    }
    XTL::D3DSURFACE_DESC SrcDesc = {};
    if(pSourceSurface->EmuSurface8 == NULL ||
       FAILED(pSourceSurface->EmuSurface8->GetDesc(&SrcDesc)) ||
       SrcDesc.Width == 0 || SrcDesc.Height == 0)
    {
        return false;
    }
    // 4-byte texel formats only (A8R8G8B8 / X8R8G8B8 -- the Xbox RT set).
    if(SrcDesc.Format != 21 && SrcDesc.Format != 22)
    {
        return false;
    }
    const UINT rowBytes = SrcDesc.Width * 4;

    std::vector<BYTE> pixels(static_cast<std::size_t>(SrcDesc.Width) *
                             SrcDesc.Height * 4);
    // Slice C: the target registry is keyed by the guest surface pointer.
    bool readOk = cxbx::d3d8::HostBackendReadTargetPixels(
        pSourceSurface, pixels.data(), rowBytes, SrcDesc.Width,
        SrcDesc.Height);
    if(!readOk && !cxbx::d3d8::HostBackendRenders())
    {
        // d3d8 flavor: the host surface is authoritative (the host binding
        // exists and rendering landed there) but refuses LockRect as a
        // render target -- stage through a system-memory image, exactly like
        // the render-target dump path.
        XTL::IDirect3DSurface8* pScratch = NULL;
        if(SUCCEEDED(g_pD3DDevice8->CreateImageSurface(
               SrcDesc.Width, SrcDesc.Height, SrcDesc.Format, &pScratch)) &&
           pScratch != NULL &&
           SUCCEEDED(g_pD3DDevice8->CopyRects(pSourceSurface->EmuSurface8,
                                              NULL, 0, pScratch, NULL)))
        {
            XTL::D3DLOCKED_RECT srcLocked = {};
            if(EmuLockSurfaceForRead(pScratch, &srcLocked))
            {
                const UINT srcRowBytes =
                    min(rowBytes, static_cast<UINT>(srcLocked.Pitch));
                for(UINT y = 0; y < SrcDesc.Height; ++y)
                {
                    memcpy(pixels.data() +
                               static_cast<std::size_t>(y) * rowBytes,
                           static_cast<const BYTE*>(srcLocked.pBits) +
                               static_cast<std::size_t>(y) * srcLocked.Pitch,
                           srcRowBytes);
                }
                EmuSafeHostSurfaceUnlock(pScratch);
                readOk = true;
            }
        }
        if(pScratch != NULL)
        {
            pScratch->Release();
        }
    }
    if(EmuSurfaceContainerTraceEnabled())
    {
        printf("SURF| rt-copy readback ok=%d\n", readOk ? 1 : 0);
        fflush(stdout);
    }
    if(!readOk)
    {
        return false;
    }

    // Resolve the copy list: NULL rects = one whole-surface copy. Every
    // rect must sit inside the source and its destination offset must keep
    // the copy inside the destination.
    struct CopyRectEntry
    {
        RECT src;
        POINT dst;
    };
    std::vector<CopyRectEntry> copies;
    if(cRects == 0)
    {
        copies.push_back({{0, 0, (LONG)SrcDesc.Width, (LONG)SrcDesc.Height},
                          {0, 0}});
    }
    else
    {
        for(UINT i = 0; i < cRects; ++i)
        {
            const RECT& r = pSourceRectsArray[i];
            const POINT& p = pDestPointsArray[i];
            if(r.left < 0 || r.top < 0 || r.right > (LONG)SrcDesc.Width ||
               r.bottom > (LONG)SrcDesc.Height || r.left >= r.right ||
               r.top >= r.bottom || p.x < 0 || p.y < 0 ||
               p.x + (r.right - r.left) > (LONG)SrcDesc.Width ||
               p.y + (r.bottom - r.top) > (LONG)SrcDesc.Height)
            {
                return false;
            }
            copies.push_back({r, p});
        }
    }

    // Guest-only destination: write the rows straight into the surface's
    // guest-RAM allocation. The pixel-container header carries the geometry
    // the guest's own locks will use.
    if(pDestinationSurface->EmuSurface8 == NULL)
    {
        const XTL::X_D3DPixelContainer* pContainer =
            reinterpret_cast<const XTL::X_D3DPixelContainer*>(pDestinationSurface);
        const DWORD guestWidth =
            (pContainer->Size & X_D3DSIZE_WIDTH_MASK) + 1;
        const DWORD guestHeight =
            ((pContainer->Size & X_D3DSIZE_HEIGHT_MASK) >>
             X_D3DSIZE_HEIGHT_SHIFT) + 1;
        const DWORD guestPitch =
            (((pContainer->Size & X_D3DSIZE_PITCH_MASK) >>
              X_D3DSIZE_PITCH_SHIFT) + 1) * 64;
        if(EmuSurfaceContainerTraceEnabled())
        {
            printf("SURF| rt-copy dst guest-only data=0x%.08lX common=0x%.08lX "
                   "size=0x%.08lX fmt=0x%.08lX guest=%lux%lu pitch=%lu\n",
                   reinterpret_cast<unsigned long>(pDestinationSurface->Data),
                   static_cast<unsigned long>(pDestinationSurface->Common),
                   static_cast<unsigned long>(pContainer->Size),
                   static_cast<unsigned long>(pContainer->Format),
                   guestWidth, guestHeight, guestPitch);
            fflush(stdout);
        }
        if(pDestinationSurface->Data == 0 ||
           guestWidth < SrcDesc.Width || guestHeight < SrcDesc.Height ||
           guestPitch < rowBytes)
        {
            return false;
        }
        BYTE* pGuest = nullptr;
        ULONG contiguousSize = 0;
        const ULONG dstData = pDestinationSurface->Data;
        if(EmuContiguousBlockBase(dstData, &contiguousSize) != 0)
        {
            // Kernel-contiguous allocations are host memory handed to the
            // guest raw: the pointer is directly writable here.
            pGuest = reinterpret_cast<BYTE*>(static_cast<uintptr_t>(dstData));
            const ULONG blockBase = EmuContiguousBlockBase(dstData, nullptr);
            if(blockBase != 0 &&
               dstData - blockBase + guestPitch * guestHeight > contiguousSize)
            {
                return false;
            }
        }
        else
        {
            pGuest = EmuResolvePhysicalHostSpan(
                pDestinationSurface->Data,
                static_cast<ULONG>(guestPitch) * guestHeight);
        }
        if(pGuest == nullptr)
        {
            return false;
        }
        for(const CopyRectEntry& copy : copies)
        {
            for(LONG y = copy.src.top; y < copy.src.bottom; ++y)
            {
                memcpy(pGuest +
                           static_cast<std::size_t>(copy.dst.y + y - copy.src.top) *
                               guestPitch +
                           static_cast<std::size_t>(copy.dst.x) * 4,
                       pixels.data() +
                           static_cast<std::size_t>(y) * rowBytes +
                           static_cast<std::size_t>(copy.src.left) * 4,
                       static_cast<std::size_t>(copy.src.right - copy.src.left) * 4);
            }
        }
        return true;
    }

    XTL::D3DSURFACE_DESC DstDesc = {};
    if(FAILED(pDestinationSurface->EmuSurface8->GetDesc(&DstDesc)) ||
       DstDesc.Format != SrcDesc.Format || DstDesc.Width < SrcDesc.Width ||
       DstDesc.Height < SrcDesc.Height)
    {
        return false;
    }

    XTL::D3DLOCKED_RECT locked = {};
    if(!EmuLockSurfaceForWrite(pDestinationSurface->EmuSurface8, &locked))
    {
        return false;
    }

    for(const CopyRectEntry& copy : copies)
    {
        for(LONG y = copy.src.top; y < copy.src.bottom; ++y)
        {
            memcpy(static_cast<BYTE*>(locked.pBits) +
                       static_cast<std::size_t>(copy.dst.y + y - copy.src.top) *
                           locked.Pitch +
                       static_cast<std::size_t>(copy.dst.x) * 4,
                   pixels.data() +
                       static_cast<std::size_t>(y) * rowBytes +
                       static_cast<std::size_t>(copy.src.left) * 4,
                   static_cast<std::size_t>(copy.src.right - copy.src.left) * 4);
        }
    }
    EmuSafeHostSurfaceUnlock(pDestinationSurface->EmuSurface8);

    // A served destination lock keeps the container bytes authoritative for
    // the guest: update them too.
    if(EmuFindSurfaceContainerLock(pDestinationSurface) != nullptr)
    {
        if(EmuNativeSurface* pNative =
               EmuNativeSurfaceFind(pDestinationSurface->EmuSurface8))
        {
            const UINT containerRows = pNative->Height;
            const UINT containerRowBytes =
                min(pNative->Pitch, (UINT)(SrcDesc.Width * 4));
            for(const CopyRectEntry& copy : copies)
            {
                for(LONG y = copy.src.top; y < copy.src.bottom && y < (LONG)containerRows; ++y)
                {
                    memcpy(pNative->Pixels.data() +
                               static_cast<std::size_t>(copy.dst.y + y - copy.src.top) *
                                   pNative->Pitch +
                               static_cast<std::size_t>(copy.dst.x) * 4,
                           pixels.data() +
                               static_cast<std::size_t>(y) * rowBytes +
                               static_cast<std::size_t>(copy.src.left) * 4,
                           containerRowBytes < static_cast<std::size_t>(copy.src.right - copy.src.left) * 4
                               ? containerRowBytes
                               : static_cast<std::size_t>(copy.src.right - copy.src.left) * 4);
                }
            }
        }
    }

    // The destination host surface gained content behind a texture
    // container's back: same rule as the host copy.
    EmuInvalidateTextureContainerForSurface(pDestinationSurface->EmuSurface8);
    return true;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CopyRects
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CopyRects(
    X_D3DSurface* pSourceSurface,
    CONST RECT* pSourceRectsArray,
    UINT cRects,
    X_D3DSurface* pDestinationSurface,
    CONST POINT* pDestPointsArray)
{
    D3D_TRACE("CopyRects");
    EmuSwapFS(); // Win2k/XP FS

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
    if(EmuSurfaceContainerTraceEnabled())
    {
        printf("SURF| copyrects wrapper src=0x%.08lX dst=0x%.08lX cRects=%lu\n",
               reinterpret_cast<unsigned long>(
                   pSourceSurface ? pSourceSurface->EmuSurface8 : 0),
               reinterpret_cast<unsigned long>(
                   pDestinationSurface ? pDestinationSurface->EmuSurface8 : 0),
               static_cast<unsigned long>(cRects));
        fflush(stdout);
    }
    // Container-served surface locks commit at the shared flush points; a
    // copy between a guest lock and the next flush must see the guest's
    // bytes, so retire outstanding locks first (no-op on the d3d8 flavor).
    EmuCommitSurfaceContainerLocks();
    EmuCommitTextureContainerLocks();

    // P8 C3: a render-target source's current pixels live in the backend's
    // target registry -- the host surface holds stale bytes. For like-format
    // copies (whole surface or validated rect lists), read the registry
    // target back and write the destination directly; unlike formats keep
    // the host forward (stale for RT sources -- a documented gap). The
    // details live in a helper: this wrapper carries __try, which forbids
    // unwinding locals in its frame.
    bool NativeCopied = false;
    if(pSourceSurface != NULL && pDestinationSurface != NULL &&
       pSourceSurface->EmuSurface8 != NULL)
    {
        __try
        {
            NativeCopied = EmuCopyRectsFromTargetNative(
                pSourceSurface, pDestinationSurface, pSourceRectsArray,
                cRects, pDestPointsArray);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            NativeCopied = false;
        }
    }

    HRESULT hRet = D3D_OK;
    if(!NativeCopied)
    {
        __try
        {
            hRet = g_pD3DDevice8->CopyRects(
                pSourceSurface->EmuSurface8,
                pSourceRectsArray,
                cRects,
                pDestinationSurface->EmuSurface8,
                pDestPointsArray);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            hRet = D3DERR_INVALIDCALL;
        }

        // A copy into a texture's level surface updates the host texture behind
        // the container's back: invalidate the capture so the bind pull falls
        // back to the authoritative host texture (unless a served lock is
        // outstanding, in which case the container stays authoritative).
        if(SUCCEEDED(hRet) && cxbx::d3d8::HostBackendRenders() &&
           pDestinationSurface != NULL && pDestinationSurface->EmuSurface8 != NULL)
        {
            EmuInvalidateTextureContainerForSurface(pDestinationSurface->EmuSurface8);
        }
    }

    EmuSwapFS(); // Xbox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateImageSurface
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateImageSurface(
    UINT Width,
    UINT Height,
    X_D3DFORMAT Format,
    X_D3DSurface** ppBackBuffer)
{
    EmuSwapFS(); // Win2k/XP FS
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
    EmuRememberOwnedGuestResource(*ppBackBuffer);

    D3DFORMAT PCFormat = EmuXB2PC_D3DFormat(Format);

    HRESULT hRet = g_pD3DDevice8->CreateImageSurface(Width, Height, PCFormat, &((*ppBackBuffer)->EmuSurface8));

    EmuSwapFS(); // Xbox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetBackBuffer2
// ******************************************************************
XTL::X_D3DSurface* WINAPI XTL::EmuIDirect3DDevice8_GetBackBuffer2(
    INT BackBuffer)
{
    EmuSwapFS(); // Win2k/XP FS
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

    X_D3DSurface* pBackBuffer = new X_D3DSurface();
    EmuRememberOwnedGuestResource(pBackBuffer);

    if(BackBuffer == -1)
        BackBuffer = 0;

    HRESULT hRet = g_pD3DDevice8->GetBackBuffer(BackBuffer, D3DBACKBUFFER_TYPE_MONO, &(pBackBuffer->EmuSurface8));

    if(FAILED(hRet))
        EmuCleanup("Unable to retrieve back buffer");

    EmuSwapFS(); // Xbox FS

    return pBackBuffer;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetBackBuffer
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetBackBuffer(
    INT BackBuffer,
    D3DBACKBUFFER_TYPE Type,
    X_D3DSurface** ppBackBuffer)
{
    D3D_TRACE("GetBackBuffer");
// ******************************************************************
// * debug trace
// ******************************************************************
#ifdef _DEBUG_TRACE
    {
        EmuSwapFS(); // Win2k/XP FS
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetBackBuffer\n"
               "(\n"
               "   BackBuffer          : 0x%.08X\n"
               "   Type                : 0x%.08X\n"
               "   ppBackBuffer        : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), BackBuffer, Type, ppBackBuffer);
        EmuSwapFS(); // Xbox FS
    }
#endif

    *ppBackBuffer = EmuIDirect3DDevice8_GetBackBuffer2(BackBuffer);

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetViewport
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetViewport(
    CONST D3DVIEWPORT8* pViewport)
{
    EmuSwapFS(); // Win2k/XP FS
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

    // Record the full viewport into the backend mirror (the CPU-fallback
    // projection and the fixed-function draw path read it back).
    {
        const float recorded[6] = {
            static_cast<float>(vp.X), static_cast<float>(vp.Y),
            static_cast<float>(vp.Width), static_cast<float>(vp.Height),
            vp.MinZ, vp.MaxZ
        };
        cxbx::d3d8::HostBackendRecordViewport(recorded);
    }

    // P5: pretransformed draws map through the viewport inside the backend
    // shader, so the render path needs the live viewport.
    if(cxbx::d3d8::HostBackendRenders())
    {
        cxbx::d3d8::HostBackendSetViewport(pViewport->X, pViewport->Y,
                                           pViewport->Width,
                                           pViewport->Height);
    }

    const FLOAT XboxHalfWidth = static_cast<FLOAT>(pViewport->Width) * 0.5f;
    const FLOAT XboxHalfHeight = static_cast<FLOAT>(pViewport->Height) * 0.5f;
    const FLOAT XboxViewportScale[4] = {
        XboxHalfWidth,
        -XboxHalfHeight,
        pViewport->MaxZ - pViewport->MinZ,
        0.0f,
    };
    const FLOAT XboxViewportOffset[4] = {
        static_cast<FLOAT>(pViewport->X) + XboxHalfWidth,
        static_cast<FLOAT>(pViewport->Y) + XboxHalfHeight,
        pViewport->MinZ,
        0.0f,
    };
    EmuNv2aSetTransformConstant(58, XboxViewportScale);
    EmuNv2aSetTransformConstant(59, XboxViewportOffset);

    bool Clamped = false;

    // Depth range: host requires MinZ/MaxZ within [0,1].
    if(vp.MinZ < 0.0f)
    {
        vp.MinZ = 0.0f;
        Clamped = true;
    }
    if(vp.MinZ > 1.0f)
    {
        vp.MinZ = 1.0f;
        Clamped = true;
    }
    if(vp.MaxZ < 0.0f)
    {
        vp.MaxZ = 0.0f;
        Clamped = true;
    }
    if(vp.MaxZ > 1.0f)
    {
        vp.MaxZ = 1.0f;
        Clamped = true;
    }

    // Extents: host requires X+Width <= RT width and Y+Height <= RT height.
    IDirect3DSurface8* pRT = NULL;
    if(SUCCEEDED(g_pD3DDevice8->GetRenderTarget(&pRT)) && pRT != NULL)
    {
        D3DSURFACE_DESC RTDesc;
        if(SUCCEEDED(pRT->GetDesc(&RTDesc)))
        {
            if((DWORD)vp.X > RTDesc.Width)
            {
                vp.X = RTDesc.Width;
                Clamped = true;
            }
            if((DWORD)vp.Y > RTDesc.Height)
            {
                vp.Y = RTDesc.Height;
                Clamped = true;
            }
            DWORD MaxW = RTDesc.Width - vp.X;
            DWORD MaxH = RTDesc.Height - vp.Y;
            if(vp.Width > MaxW)
            {
                vp.Width = MaxW;
                Clamped = true;
            }
            if(vp.Height > MaxH)
            {
                vp.Height = MaxH;
                Clamped = true;
            }
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

    HRESULT hRet = D3D_OK;
    if(!cxbx::d3d8::HostBackendRenders())
        hRet = g_pD3DDevice8->SetViewport(&vp);

    EmuSwapFS(); // Xbox FS

    // The title's call would have succeeded on hardware; do not feed it an
    // error path it never takes there.
    if(FAILED(hRet))
        hRet = D3D_OK;

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetViewportOffsetAndScale
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetViewportOffsetAndScale(
    FLOAT* pOffset,
    FLOAT* pScale)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("GetViewportOffsetAndScale");

    D3DVIEWPORT8 Viewport = {};
    const HRESULT Result = g_pD3DDevice8->GetViewport(&Viewport);
    if(SUCCEEDED(Result))
    {
        const FLOAT HalfWidth = static_cast<FLOAT>(Viewport.Width) * 0.5f;
        const FLOAT HalfHeight = static_cast<FLOAT>(Viewport.Height) * 0.5f;

        pOffset[0] = static_cast<FLOAT>(Viewport.X) + HalfWidth;
        pOffset[1] = static_cast<FLOAT>(Viewport.Y) + HalfHeight;
        pOffset[2] = Viewport.MinZ;
        pOffset[3] = 0.0f;

        pScale[0] = HalfWidth;
        pScale[1] = -HalfHeight;
        pScale[2] = Viewport.MaxZ - Viewport.MinZ;
        pScale[3] = 0.0f;
    }
    else
    {
        memset(pOffset, 0, sizeof(FLOAT) * 4);
        memset(pScale, 0, sizeof(FLOAT) * 4);
    }

    EmuSwapFS(); // Xbox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetShaderConstantMode
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetShaderConstantMode(
    DWORD dwMode // TODO: Fill out enumeration
)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // Xbox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetRenderTarget
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetRenderTarget(
    X_D3DSurface** ppRenderTarget)
{
    EmuSwapFS(); // Win2k/XP FS
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

    IDirect3DSurface8* pSurface8 = g_pCachedRenderTarget->EmuSurface8;

    pSurface8->AddRef();

    *ppRenderTarget = g_pCachedRenderTarget;

    EmuSwapFS(); // Xbox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetRenderTarget2
// ******************************************************************
XTL::X_D3DSurface* WINAPI XTL::EmuIDirect3DDevice8_GetRenderTarget2()
{
    EmuSwapFS(); // Win2k/XP FS
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

    IDirect3DSurface8* pSurface8 = g_pCachedRenderTarget->EmuSurface8;

    pSurface8->AddRef();

    EmuSwapFS(); // Xbox FS

    return g_pCachedRenderTarget;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetDepthStencilSurface
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetDepthStencilSurface(
    X_D3DSurface** ppZStencilSurface)
{
    EmuSwapFS(); // Win2k/XP FS
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

    IDirect3DSurface8* pSurface8 = g_pCachedZStencilSurface->EmuSurface8;

    if(pSurface8 != 0)
        pSurface8->AddRef();

    *ppZStencilSurface = g_pCachedZStencilSurface;

    EmuSwapFS(); // Xbox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetDepthStencilSurface2
// ******************************************************************
XTL::X_D3DSurface* WINAPI XTL::EmuIDirect3DDevice8_GetDepthStencilSurface2()
{
    EmuSwapFS(); // Win2k/XP FS
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
        EmuSwapFS(); // Xbox FS
        return NULL;
    }

    IDirect3DSurface8* pSurface8 = g_pCachedZStencilSurface->EmuSurface8;

    if(pSurface8 != 0)
        pSurface8->AddRef();

    EmuSwapFS(); // Xbox FS

    return g_pCachedZStencilSurface;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetTile
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetTile(
    DWORD Index,
    X_D3DTILE* pTile)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTileNoWait
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetTileNoWait(
    DWORD Index,
    CONST X_D3DTILE* pTile)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return D3D_OK;
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

static void EmuVshDumpRejectedShader(
    XTL::VshDiagnostics::ShaderWordView xboxFunction,
    XTL::VshDiagnostics::ShaderWordView d3dFunction,
    XTL::VshDiagnostics::ShaderWordView xboxDeclaration,
    XTL::VshDiagnostics::ShaderWordView d3dDeclaration, const char* reason)
{
    try
    {
        const std::span<const float> constants = XTL::VshCpuDeviceState::Constants();
        const XTL::VshDiagnostics::TranslationCapture capture = {
            xboxFunction,
            d3dFunction,
            xboxDeclaration,
            d3dDeclaration,
            reason,
            constants.data(),
            constants.size(),
            nullptr,
            0,
            "canonical",
        };
        XTL::VshDiagnostics::DumpRejectedTranslation(stdout, capture);
        if(!xboxFunction.empty() && (xboxFunction[0] & 0xFFFFu) == 0x2078u)
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
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateVertexShader(
    CONST DWORD* pDeclaration,
    CONST DWORD* pFunction,
    DWORD* pHandle,
    DWORD Usage)
{
    EmuSwapFS(); // Win2k/XP FS
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
    X_D3DVertexShader* pD3DVertexShader = new X_D3DVertexShader();

    // Todo: Intelligently fill out these fields as necessary
    ZeroMemory(pD3DVertexShader, sizeof(X_D3DVertexShader));

    // An Xbox function blob (version token low word 0x2078) is 128-bit NV2A
    // vertex-program microcode, and the Xbox declaration carries extended
    // data-type codes -- neither is host-consumable. Recompile the microcode
    // into vs.1.1 bytecode and rewrite the declaration types (EmuVshDecoder).
    const bool isXboxFunction = pFunction != NULL && (pFunction[0] & 0xFFFF) == 0x2078;
    const std::vector<std::uint32_t> fixedFunction =
        isXboxFunction ? VshInternal::CopyXboxFunction(pFunction)
                       : std::vector<std::uint32_t>{};
    const std::vector<std::uint32_t> fixedDeclaration =
        pDeclaration != nullptr ? VshInternal::CopyXboxDeclaration(pDeclaration)
                                : std::vector<std::uint32_t>{};
    const XTL::VshShaderCreation::Plan creationPlan =
        XTL::VshShaderCreation::BuildPlan({
            fixedFunction,
            fixedDeclaration,
            isXboxFunction,
            pDeclaration != nullptr,
            EmuWarning,
        });
    std::vector<DWORD> hostTranslatedFunction(creationPlan.translatedFunction.size());
    std::transform(creationPlan.translatedFunction.begin(),
                   creationPlan.translatedFunction.end(), hostTranslatedFunction.begin(),
                   [](std::uint32_t word)
                   { return static_cast<DWORD>(word); });
    std::array<DWORD, 128> hostTranslatedDeclaration{};
    for(std::size_t tokenIndex = 0;
        tokenIndex < creationPlan.declarationTokenCount; ++tokenIndex)
    {
        hostTranslatedDeclaration[tokenIndex] =
            static_cast<DWORD>(creationPlan.translatedDeclaration[tokenIndex]);
    }
    const XTL::VshDiagnostics::ShaderWordView translatedDeclaration =
        creationPlan.translatedDeclarationAvailable
            ? XTL::VshDiagnostics::ShaderWordView{
                  creationPlan.translatedDeclaration.data(),
                  creationPlan.declarationTokenCount,
              }
            : XTL::VshDiagnostics::ShaderWordView{};

    if(creationPlan.disposition == XTL::VshShaderCreation::Disposition::Reject)
    {
        EmuVshDumpRejectedShader(fixedFunction, creationPlan.translatedFunction,
                                 fixedDeclaration, translatedDeclaration,
                                 creationPlan.reason.c_str());
        if(pHandle != nullptr)
        {
            *pHandle = 0;
        }
        delete pD3DVertexShader;
        EmuSwapFS(); // XBox FS
        return D3DERR_INVALIDCALL;
    }
    const bool cpuFallback =
        creationPlan.disposition == XTL::VshShaderCreation::Disposition::ExecuteOnCpu;
    const bool wasRecompiled = !creationPlan.translatedFunction.empty();
    const DWORD* pHostFunction =
        cpuFallback
            ? nullptr
            : (hostTranslatedFunction.empty() ? pFunction
                                              : hostTranslatedFunction.data());
    const DWORD* pHostDeclaration =
        pDeclaration != nullptr && !cpuFallback ? hostTranslatedDeclaration.data()
                                                : pDeclaration;

    // The host vs.1.1 validator requires texture coordinate outputs to start
    // at oT0 and stay contiguous, while an Xbox shader may write only oT2 and
    // oT3 (Arx Fatalis' intro-video shader). Compact the written oT set and
    // keep the game-stage -> host-stage mapping so texture binds follow.
    std::uint8_t texcoordRemapFrom[4] = {};
    std::uint8_t texcoordRemapTo[4] = {};
    std::uint32_t texcoordRemapCount = 0;
    if(!cpuFallback && !hostTranslatedFunction.empty() &&
       pDeclaration != nullptr && hostTranslatedFunction.back() == 0x0000FFFFu)
    {
        auto D3dTokensFor = [](DWORD opcode) -> int
        {
            switch(opcode & 0xFFFF)
            {
                case 0: return 1; // nop
                case 1:
                case 6:
                case 7:
                case 14:
                case 15:
                case 16:
                    return 3; // 1 source
                case 2:
                case 5:
                case 8:
                case 9:
                case 10:
                case 11:
                case 12:
                case 13:
                case 17:
                    return 4;     // 2 sources
                case 4: return 5; // mad
                default: return -1;
            }
        };
        bool written[4] = {};
        std::size_t index = 1;
        bool walkedFully = true;
        while(index + 1 < hostTranslatedFunction.size())
        {
            const int tokens = D3dTokensFor(hostTranslatedFunction[index]);
            if(tokens < 0)
            {
                walkedFully = false;
                break;
            }
            const DWORD dest = hostTranslatedFunction[index + 1];
            if(((dest >> 28) & 7u) == 6u)
            {
                const DWORD reg = dest & 0xFu;
                if(reg < 4) written[reg] = true;
            }
            index += static_cast<std::size_t>(tokens);
        }
        if(walkedFully && index + 1 == hostTranslatedFunction.size())
        {
            std::uint32_t newReg = 0;
            for(DWORD reg = 0; reg < 4; ++reg)
            {
                if(!written[reg]) continue;
                if(reg != newReg)
                {
                    texcoordRemapFrom[texcoordRemapCount] = (std::uint8_t)reg;
                    texcoordRemapTo[texcoordRemapCount] = (std::uint8_t)newReg;
                    ++texcoordRemapCount;
                }
                ++newReg;
            }
            if(texcoordRemapCount > 0)
            {
                DWORD remapped[4] = { 0, 1, 2, 3 };
                for(std::uint32_t entry = 0; entry < texcoordRemapCount; ++entry)
                    remapped[texcoordRemapFrom[entry]] = texcoordRemapTo[entry];
                for(std::size_t tokenIndex = 1;
                    tokenIndex + 1 < hostTranslatedFunction.size();)
                {
                    const int tokens = D3dTokensFor(hostTranslatedFunction[tokenIndex]);
                    DWORD& dest = hostTranslatedFunction[tokenIndex + 1];
                    if(((dest >> 28) & 7u) == 6u && (dest & 0xFu) < 4)
                        dest = (dest & 0xFFFFF800u) | remapped[dest & 0xFu];
                    tokenIndex += static_cast<std::size_t>(tokens);
                }
                printf("EmuD3D8 (0x%X): compacted %u texture-coordinate output(s) to "
                       "oT0..oT%u.\n",
                       GetCurrentThreadId(), texcoordRemapCount,
                       newReg - 1);
            }
        }
    }

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    HRESULT hRet = D3D_OK;
    bool hostCallAttempted = false;
    if(!cpuFallback)
    {
        hostCallAttempted = true;
        hRet = EmuVshCreateHostShader(pHostDeclaration, pHostFunction,
                                      &pD3DVertexShader->Handle, g_dwVertexShaderUsage);
    }

    if(FAILED(hRet) && !creationPlan.translatedFunction.empty())
    {
        const char* diagnosticReason = creationPlan.reason.empty()
                                           ? "host_create_failed"
                                           : creationPlan.reason.c_str();
        EmuVshDumpRejectedShader(fixedFunction, creationPlan.translatedFunction,
                                 fixedDeclaration, translatedDeclaration,
                                 diagnosticReason);
    }

    const XTL::VshDiagnostics::ShaderWordView ownedDeclaration =
        fixedDeclaration.empty()
            ? XTL::VshDiagnostics::ShaderWordView{}
            : XTL::VshDiagnostics::ShaderWordView{ fixedDeclaration.data(),
                                                   creationPlan.declarationTokenCount };
    if(!XTL::VshShaderRegistry::Register(pD3DVertexShader, cpuFallback, fixedFunction,
                                         ownedDeclaration))
    {
        if(pD3DVertexShader->Handle != 0)
        {
            g_pD3DDevice8->DeleteVertexShader(pD3DVertexShader->Handle);
        }
        delete pD3DVertexShader;
        EmuSwapFS(); // XBox FS
        return E_OUTOFMEMORY;
    }
    if(texcoordRemapCount > 0)
    {
        if(XTL::VshShaderRegistry::CpuFallbackMetadata* registeredMetadata =
               XTL::VshShaderRegistry::Find(pD3DVertexShader))
        {
            registeredMetadata->texcoordRemapCount = texcoordRemapCount;
            for(std::uint32_t entry = 0; entry < texcoordRemapCount; ++entry)
            {
                registeredMetadata->texcoordRemapFrom[entry] = texcoordRemapFrom[entry];
                registeredMetadata->texcoordRemapTo[entry] = texcoordRemapTo[entry];
            }
        }
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
               static_cast<unsigned int>(XTL::VshDiagnostics::HashXboxFunction(fixedFunction)),
               creationPlan.reason.c_str());
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
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetVertexShaderConstant(
    INT Register,
    CONST PVOID pConstantData,
    DWORD ConstantCount)
{
    EmuSwapFS(); // Win2k/XP FS
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
                    XTL::VshCpuDeviceState::SetConstant(
                        static_cast<std::size_t>(hardwareIndex),
                        std::span<const float, 4>{ &source[constant * 4], 4 });
                    EmuNv2aSetTransformConstant(static_cast<ULONG>(hardwareIndex),
                                                &source[constant * 4]);
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
        {
            EmuForwardCacheExpire();
            const DWORD* forwarded = (const DWORD*)pData;
            bool identical = true;
            for(DWORD constant = 0; constant < Count && identical; ++constant)
            {
                const DWORD hostRegister = (DWORD)Reg + constant;
                identical = g_EmuForwardedConstantValid[hostRegister] &&
                            memcmp(&g_EmuForwardedConstants[hostRegister * 4],
                                   forwarded + constant * 4, 16) == 0;
            }
            if(!identical)
            {
                // Slice C1: the Vulkan flavor's CPU fallback reads the
                // HLE-side constant records; the host device copy is not
                // consulted, so keep only the forward cache fresh.
                const bool forwardToHost = !cxbx::d3d8::HostBackendRenders();
                if(forwardToHost)
                    hRet = g_pD3DDevice8->SetVertexShaderConstant(Reg, pData, Count);
                else
                    hRet = D3D_OK;
                if(SUCCEEDED(hRet))
                {
                    for(DWORD constant = 0; constant < Count; ++constant)
                    {
                        const DWORD hostRegister = (DWORD)Reg + constant;
                        memcpy(&g_EmuForwardedConstants[hostRegister * 4],
                               forwarded + constant * 4, 16);
                        g_EmuForwardedConstantValid[hostRegister] = true;
                    }
                }
            }
        }
    }

    if(FAILED(hRet))
    {
        static LONG WarnCount = 0;
        if(InterlockedIncrement(&WarnCount) <= 5)
            EmuWarning("SetVertexShaderConstant failed (Register = %d, Count = %d) hr=0x%.08X", Register, ConstantCount, (DWORD)hRet);

        hRet = D3D_OK;
    }

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShaderConstantNotInline
// ******************************************************************
VOID __fastcall XTL::EmuIDirect3DDevice8_SetVertexShaderConstantNotInline(
    INT Register,
    CONST PVOID pConstantData,
    DWORD ConstantCount)
{
#ifdef _DEBUG_TRACE
    {
        EmuSwapFS(); // Win2k/XP FS
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexShaderConstantNotInline\n"
               "(\n"
               "   Register            : 0x%.08X\n"
               "   pConstantData       : 0x%.08X\n"
               "   ConstantCount       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Register, pConstantData, ConstantCount);
        EmuSwapFS(); // XBox FS
    }
#endif

    XTL::EmuIDirect3DDevice8_SetVertexShaderConstant(Register - 96, pConstantData, ConstantCount / 4);
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShaderConstant1
// ******************************************************************
VOID __fastcall XTL::EmuIDirect3DDevice8_SetVertexShaderConstant1(
    INT Register,
    CONST PVOID pConstantData)
{
// ******************************************************************
// * debug trace
// ******************************************************************
#ifdef _DEBUG_TRACE
    {
        EmuSwapFS(); // Win2k/XP FS
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexShaderConstant1\n"
               "(\n"
               "   Register            : 0x%.08X\n"
               "   pConstantData       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Register, pConstantData);
        EmuSwapFS(); // XBox FS
    }
#endif

    XTL::EmuIDirect3DDevice8_SetVertexShaderConstant(Register - 96, pConstantData, 1);

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShaderConstant4
// ******************************************************************
VOID __fastcall XTL::EmuIDirect3DDevice8_SetVertexShaderConstant4(
    INT Register,
    CONST PVOID pConstantData)
{
// ******************************************************************
// * debug trace
// ******************************************************************
#ifdef _DEBUG_TRACE
    {
        EmuSwapFS(); // Win2k/XP FS
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexShaderConstant4\n"
               "(\n"
               "   Register            : 0x%.08X\n"
               "   pConstantData       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Register, pConstantData);
        EmuSwapFS(); // XBox FS
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
#define X_PIXELSHADER_FALLBACK_MARKER 0xF5000000u
#define X_PIXELSHADER_FALLBACK_MASK   0xFF000000u

static bool g_bUsePixelShaderFallback = false;
static DWORD g_EmuCurrentPixelShaderHandle = 0;
// texbem consumes four BUMPENVMAT values. Do not bind a translated texbem
// program until the title has supplied all of them for the affected stage.
static std::array<std::uint8_t, 8> g_EmuBumpEnvMatrixWrites{};

struct EmuPixelShaderFallbackDefinition
{
    DWORD handle = 0;
    cxbx::d3d::XboxPixelShaderDefinition definition{};
};

static std::vector<EmuPixelShaderFallbackDefinition> g_EmuPixelShaderFallbacks;

static const cxbx::d3d::XboxPixelShaderDefinition* EmuFindTranslatedPixelShader(
    DWORD handle);

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
    if(definition == nullptr)
    {
        definition = EmuFindTranslatedPixelShader(g_EmuCurrentPixelShaderHandle);
    }
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

    g_pD3DDevice8->SetPixelShader(0); // fixed-function

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
        g_pD3DDevice8->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
        g_pD3DDevice8->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        g_pD3DDevice8->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
        g_pD3DDevice8->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
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
// CXBX_D3D_DUMP_BOUND_TEXTURE=frame:draw|frame:*
//                            dump the stage-0 host texture before one draw, or
//                            each unique stage-0 texture in a frame, to
//                            %TEMP%\cxbx_bound_tex\
// CXBX_TEX_TRACE=1           one TEX| line per registered texture/surface
// CXBX_PSH_DUMP=1            dump the 60-dword combiner definition of every
//                            pixel shader that lands on the fixed-function
//                            fallback
// CXBX_D3D_PERF_MARKERS=1    emit D3DPERF (PIX) frame/draw events so external
//                            captures (apitrace, DXVK d3d8 + RenderDoc) are
//                            self-describing

static DWORD g_D3DDebugFrame = 0;     // increments at Swap
static DWORD g_D3DDebugDrawIndex = 0; // resets at Swap

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
            format != XTL::D3DFMT_R5G6B5 && format != XTL::D3DFMT_A1R5G5B5 &&
            format != XTL::D3DFMT_A4R4G4B4)
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
        else if(format == XTL::D3DFMT_A1R5G5B5)
        {
            const WORD* pixels = reinterpret_cast<const WORD*>(source);
            for(DWORD x = 0; x < width; ++x)
            {
                const WORD p = pixels[x];
                const DWORD a = (p & 0x8000) != 0 ? 255 : 0;
                const DWORD r = ((p >> 10) & 0x1F) * 255 / 31;
                const DWORD g = ((p >> 5) & 0x1F) * 255 / 31;
                const DWORD b = (p & 0x1F) * 255 / 31;
                row[x] = (a << 24) | (r << 16) | (g << 8) | b;
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

// Under the Vulkan render path the host d3d8 backbuffer and render targets
// are never drawn into (the legacy dumps came out solid-color), so the
// ground-truth dumps read the backend's own targets: mainTarget selects the
// present-source target, otherwise the target draws currently land in.
static bool EmuVulkanDumpTarget(const char* path, bool mainTarget)
{
    unsigned width = 0;
    unsigned height = 0;
    const bool sized =
        mainTarget ? cxbx::d3d8::HostBackendTargetSize(&width, &height)
                   : cxbx::d3d8::HostBackendCurrentTargetSize(&width, &height);
    if(!sized || width == 0 || height == 0)
    {
        return false;
    }
    const unsigned pitch = width * 4;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(pitch) * height);
    const bool read =
        mainTarget ? cxbx::d3d8::HostBackendReadMainTarget(pixels.data(), pitch)
                   : cxbx::d3d8::HostBackendReadFrame(pixels.data(), pitch);
    if(!read)
    {
        return false;
    }
    return EmuD3DWriteBmp(path, width, height, static_cast<INT>(pitch),
                          pixels.data(), XTL::D3DFMT_A8R8G8B8);
}

static bool EmuD3DDumpBackbuffer(const char* path)
{
    if(cxbx::d3d8::HostBackendRenders())
    {
        return EmuVulkanDumpTarget(path, true);
    }
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

// Dump the CURRENTLY BOUND render target (not the swap-chain back buffer), so a
// render-to-texture pass can be inspected directly -- e.g. is Turok's world
// actually landing in its 512x512 RT, or is the RT black? A D3DPOOL_DEFAULT
// render target is not lockable, so copy it into a system-memory scratch
// surface (CopyRects) before locking.
static bool EmuD3DDumpCurrentRenderTarget(const char* path)
{
    if(cxbx::d3d8::HostBackendRenders())
    {
        return EmuVulkanDumpTarget(path, false);
    }
    XTL::IDirect3DSurface8* renderTarget = nullptr;
    XTL::IDirect3DSurface8* scratch = nullptr;
    bool dumped = false;
    __try
    {
        if(g_pD3DDevice8 != 0 &&
           SUCCEEDED(g_pD3DDevice8->GetRenderTarget(&renderTarget)) &&
           renderTarget != nullptr)
        {
            XTL::D3DSURFACE_DESC description{};
            XTL::D3DLOCKED_RECT locked{};
            if(SUCCEEDED(renderTarget->GetDesc(&description)))
            {
                if(SUCCEEDED(renderTarget->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
                {
                    dumped = EmuD3DWriteBmp(path, description.Width, description.Height,
                                            locked.Pitch, locked.pBits, description.Format);
                    renderTarget->UnlockRect();
                }
                else if(SUCCEEDED(g_pD3DDevice8->CreateImageSurface(
                            description.Width, description.Height, description.Format, &scratch)) &&
                        scratch != nullptr &&
                        SUCCEEDED(g_pD3DDevice8->CopyRects(renderTarget, nullptr, 0, scratch, nullptr)) &&
                        SUCCEEDED(scratch->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
                {
                    dumped = EmuD3DWriteBmp(path, description.Width, description.Height,
                                            locked.Pitch, locked.pBits, description.Format);
                    scratch->UnlockRect();
                }
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        dumped = false;
    }
    if(scratch != nullptr)
    {
        scratch->Release();
    }
    if(renderTarget != nullptr)
    {
        renderTarget->Release();
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
static void EmuFlushBoundHostTextureLocks();
static void EmuTraceBoundTextureAtDraw();
static void EmuDumpBoundTextureAtDraw();

static bool EmuD3DDrawGate(const char* what, DWORD pcPrimitiveType,
                           DWORD primitiveCount)
{
    static int s_SkipEnabled = -1;
    static DWORD s_SkipBegin = 0, s_SkipEnd = 0;
    static int s_TraceEnabled = -1;

    // Xbox texture locks expose unified memory and do not require a matching
    // UnlockRect call. Host D3D8 does, so commit every bound texture before
    // the guest submits a draw, even when it bound the texture only once.
    EmuFlushBoundHostTextureLocks();
    EmuTraceBoundTextureAtDraw();
    EmuDumpBoundTextureAtDraw();

    const DWORD index = g_D3DDebugDrawIndex++;

    if(s_SkipEnabled < 0)
    {
        char Value[64] = {};
        s_SkipEnabled =
            EmuD3DReadEnvironment("CXBX_D3D_SKIP_DRAWS", Value, sizeof(Value)) &&
                    EmuD3DParseDrawRange(Value, s_SkipBegin, s_SkipEnd)
                ? 1
                : 0;
    }
    if(s_TraceEnabled < 0)
    {
        s_TraceEnabled = EmuD3DEnvironmentEnabled("CXBX_D3D_DRAW_TRACE") ? 1 : 0;
    }

    const bool skipped =
        s_SkipEnabled == 1 && index >= s_SkipBegin && index < s_SkipEnd;

    if(s_TraceEnabled == 1)
    {
        DWORD vertexShader = 0;
        if(g_pD3DDevice8 != 0)
            g_pD3DDevice8->GetVertexShader(&vertexShader);
        // Which texture is actually bound when the draw issues, plus the
        // stage-0 colour op. A textured quad that renders flat white has
        // either lost its binding between SetTexture and the draw, or has a
        // stage op that ignores the texture -- the trace could not tell those
        // apart while it reported only the shader handles.
        XTL::IDirect3DBaseTexture8* boundTexture = NULL;
        DWORD stage0ColorOp = 0;
        DWORD stage0ColorArg1 = 0;
        if(g_pD3DDevice8 != 0)
        {
            g_pD3DDevice8->GetTexture(0, &boundTexture);
            if(boundTexture != NULL)
                boundTexture->Release(); // GetTexture adds a reference
            g_pD3DDevice8->GetTextureStageState(0, XTL::D3DTSS_COLOROP, &stage0ColorOp);
            g_pD3DDevice8->GetTextureStageState(0, XTL::D3DTSS_COLORARG1, &stage0ColorArg1);
        }
        printf("DRAW| frame=%lu draw=%lu %s prim=%lu count=%lu vsh=0x%08lX "
               "psh=0x%08lX tex0=0x%08lX yuv=%u op=%lu arg1=0x%lX%s\n",
               static_cast<unsigned long>(g_D3DDebugFrame),
               static_cast<unsigned long>(index), what,
               static_cast<unsigned long>(pcPrimitiveType),
               static_cast<unsigned long>(primitiveCount),
               static_cast<unsigned long>(vertexShader),
               static_cast<unsigned long>(g_EmuCurrentPixelShaderHandle),
               reinterpret_cast<unsigned long>(boundTexture),
               g_bStage0ConvertedYuv ? 1u : 0u,
               static_cast<unsigned long>(stage0ColorOp),
               static_cast<unsigned long>(stage0ColorArg1),
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
    {
        char Value[64] = {};
        s_DumpEnabled =
            EmuD3DReadEnvironment("CXBX_D3D_DUMP_DRAWS", Value, sizeof(Value)) &&
                    EmuD3DParseDrawRange(Value, s_DumpBegin, s_DumpEnd)
                ? 1
                : 0;
    }
    if(s_DumpEnabled != 1)
    {
        return;
    }

    if(!s_FrameRangeParsed)
    {
        char Value[64] = {};
        s_FrameRangeEnabled =
            EmuD3DReadEnvironment("CXBX_D3D_DUMP_FRAMES", Value, sizeof(Value)) &&
            EmuD3DParseDrawRange(Value, s_FrameBegin, s_FrameEnd);
        s_FrameRangeParsed = true;
    }
    if(s_FrameRangeEnabled &&
       (g_D3DDebugFrame < s_FrameBegin || g_D3DDebugFrame >= s_FrameEnd))
    {
        return;
    }

    const DWORD index = g_D3DDebugDrawIndex != 0 ? g_D3DDebugDrawIndex - 1 : 0;
    if(index < s_DumpBegin || index >= s_DumpEnd || s_DumpsRemaining <= 0)
    {
        return;
    }

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

    // Also dump the currently bound render target, so a render-to-texture pass
    // (Turok's 512x512 world RT) can be inspected directly instead of only the
    // composited back buffer.
    char rtFile[64];
    snprintf(rtFile, sizeof(rtFile), "f%05lu_d%04lu_rt.bmp",
             static_cast<unsigned long>(g_D3DDebugFrame),
             static_cast<unsigned long>(index));
    char rtPath[MAX_PATH * 2];
    EmuD3DDebugDumpPath(rtPath, sizeof(rtPath), "cxbx_rt", rtFile);
    if(EmuD3DDumpCurrentRenderTarget(rtPath))
        printf("DRAW| dumped RT %s\n", rtPath);
}

// Combiner programs the ps.1.1 translator compiled to real host shaders,
// keyed by host handle; their constants are applied at SetPixelShader.
struct EmuTranslatedPixelShader
{
    DWORD handle;
    std::vector<cxbx::d3d::PixelShaderConstant> constants;
    cxbx::d3d::XboxPixelShaderDefinition definition;
};

static std::vector<EmuTranslatedPixelShader> g_EmuTranslatedPixelShaders;

static HRESULT EmuHostCreatePixelShader(const DWORD* pTokens, DWORD* pHandle)
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

static HRESULT EmuSetTranslatedPixelShaderConstants(DWORD Handle, DWORD Register,
                                                    CONST PVOID pConstantData,
                                                    DWORD ConstantCount)
{
    if(ConstantCount != 0 && pConstantData == nullptr)
        return D3DERR_INVALIDCALL;

    for(const auto& shader : g_EmuTranslatedPixelShaders)
    {
        if(shader.handle != Handle)
            continue;

        HRESULT result = D3D_OK;
        const float* source = static_cast<const float*>(pConstantData);
        for(const auto& constant : shader.constants)
        {
            if(constant.xboxRegister < Register ||
               constant.xboxRegister - Register >= ConstantCount)
            {
                continue;
            }

            const DWORD sourceIndex = constant.xboxRegister - Register;
            const HRESULT setResult = g_pD3DDevice8->SetPixelShaderConstant(
                constant.index, source + sourceIndex * 4, 1);
            if(FAILED(setResult))
                result = setResult;
        }
        return result;
    }

    // Fallback shaders run through fixed function and have no host constants.
    return D3D_OK;
}

static const cxbx::d3d::XboxPixelShaderDefinition* EmuFindTranslatedPixelShader(
    DWORD handle)
{
    for(const auto& shader : g_EmuTranslatedPixelShaders)
    {
        if(shader.handle == handle)
        {
            return &shader.definition;
        }
    }
    return nullptr;
}

static bool EmuTranslatedPixelShaderHasUnconfiguredBump(
    const cxbx::d3d::XboxPixelShaderDefinition& definition)
{
    for(unsigned int stage = 1; stage < 4; ++stage)
    {
        if(cxbx::d3d::PixelShaderTextureMode(definition, stage) == 6 &&
           g_EmuBumpEnvMatrixWrites[stage] != 0x0Fu)
        {
            return true;
        }
    }
    return false;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreatePixelShader
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreatePixelShader(
    CONST DWORD* pFunction,
    DWORD* pHandle)
{
    EmuSwapFS(); // Win2k/XP FS
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
        char value[16] = {};
        const DWORD length = GetEnvironmentVariableA("CXBX_PSH_TRANSLATE", value,
                                                     sizeof(value));
        s_PshTranslateEnabled = (length == 0 || value[0] != '0') ? 1 : 0;
    }

    const char* failureReason = "translate_disabled";
    if(definitionReadable && s_PshTranslateEnabled == 1)
    {
        cxbx::d3d::PixelShaderTranslation translation =
            cxbx::d3d::TranslatePixelShader(definition);
        if(translation.ok())
        {
            if(SUCCEEDED(EmuHostCreatePixelShader(
                   reinterpret_cast<const DWORD*>(translation.bytecode.data()),
                   pHandle)))
            {
                const DWORD hash =
                    cxbx::d3d::ResourceContentHash(
                        definition.data(), sizeof(definition));
                printf("PSH| translated handle=0x%08lX hash=0x%08lX combiners=%lu "
                       "arith=%u tex=%u const=%u\n",
                       static_cast<unsigned long>(*pHandle),
                       static_cast<unsigned long>(hash),
                       static_cast<unsigned long>(definition[53] & 0x0Fu),
                       translation.arithmetic, translation.textures,
                       static_cast<unsigned>(translation.constants.size()));

                g_EmuTranslatedPixelShaders.push_back(
                    { *pHandle, std::move(translation.constants), definition });

                EmuSwapFS(); // XBox FS
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
            g_EmuPixelShaderFallbacks.push_back({ *pHandle, definition });

            // The combiner was approximated, not translated -- always say
            // which approximation each shader got and why translation bailed,
            // so a wrong material is attributable without a rebuild.
            const DWORD hash = cxbx::d3d::ResourceContentHash(
                definition.data(), sizeof(definition));
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
                s_PshDumpEnabled = EmuD3DEnvironmentEnabled("CXBX_PSH_DUMP") ? 1 : 0;
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

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetPixelShader
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetPixelShader(
    DWORD Handle)
{
    EmuSwapFS(); // Win2k/XP FS
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
    if(cxbx::d3d8::HostBackendRenders())
    {
        // P4: mirror the active combiner program into the backend (nullptr
        // for handle 0 and for handles with no stored definition, which
        // fall back to the fixed cascade).
        const auto* definition = EmuFindPixelShaderFallback(Handle);
        if(definition == nullptr)
        {
            definition = EmuFindTranslatedPixelShader(Handle);
        }
        cxbx::d3d8::HostBackendSetPixelShader(definition != nullptr
                                                  ? definition->data()
                                                  : nullptr);
    }
    const auto* translatedDefinition = EmuFindTranslatedPixelShader(Handle);
    const bool useFixedFunctionBump = translatedDefinition != nullptr &&
                                      EmuTranslatedPixelShaderHasUnconfiguredBump(*translatedDefinition);
    if(Handle == 0)
    {
        g_bUsePixelShaderFallback = false;
        if(!cxbx::d3d8::HostBackendRenders())
            g_pD3DDevice8->SetPixelShader(0);
    }
    else if(cxbx::d3d8::HostBackendRenders())
    {
        // Slice C1: the combiner interpreter handles every translated
        // definition; the host fixed-function fallback never runs.
        g_bUsePixelShaderFallback = false;
    }
    else if((Handle & X_PIXELSHADER_FALLBACK_MASK) == X_PIXELSHADER_FALLBACK_MARKER ||
            useFixedFunctionBump ||
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
            printf("PSH| fallback_active handle=0x%08lX class=%s%s\n",
                   static_cast<unsigned long>(Handle),
                   cxbx::d3d::PixelShaderFallbackName(EmuCurrentPixelShaderFallback()),
                   useFixedFunctionBump ? " reason=unconfigured_bump_matrix" : "");
        }

        EmuApplyPixelShaderFallback(EmuCurrentPixelShaderFallback(), false);
    }
    else
    {
        g_bUsePixelShaderFallback = false;
        // Translated combiner programs carry their baked stage constants.
        EmuApplyTranslatedPixelShaderConstants(Handle);
    }

    EmuSwapFS(); // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetPixelShaderProgram
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetPixelShaderProgram(
    CONST DWORD* pFunction)
{
    D3D_TRACE("SetPixelShaderProgram");

    DWORD Handle = 0;
    HRESULT Result = EmuIDirect3DDevice8_CreatePixelShader(pFunction, &Handle);
    if(SUCCEEDED(Result))
    {
        Result = EmuIDirect3DDevice8_SetPixelShader(Handle);
    }

    return Result;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetPixelShaderConstant
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetPixelShaderConstant(
    DWORD Register,
    CONST PVOID pConstantData,
    DWORD ConstantCount)
{
    EmuSwapFS(); // Win2k/XP FS
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

    static int s_PshTraceEnabled = -1;
    if(s_PshTraceEnabled < 0)
        s_PshTraceEnabled = EmuD3DEnvironmentEnabled("CXBX_PSH_TRACE") ? 1 : 0;
    if(s_PshTraceEnabled == 1)
    {
        float first[4] = {};
        SIZE_T bytesRead = 0;
        const bool readable = ConstantCount != 0 && pConstantData != nullptr &&
                              ReadProcessMemory(GetCurrentProcess(), pConstantData, first, sizeof(first),
                                                &bytesRead) &&
                              bytesRead == sizeof(first);
        printf("PSH| constant handle=0x%08lX xbox_c=%lu count=%lu",
               static_cast<unsigned long>(g_EmuCurrentPixelShaderHandle),
               static_cast<unsigned long>(Register),
               static_cast<unsigned long>(ConstantCount));
        if(readable)
            printf(" first={%.6g,%.6g,%.6g,%.6g}", first[0], first[1], first[2], first[3]);
        else
            printf(" first=unreadable");
        printf("\n");
    }

    // P4: combiner constants live in the backend UBO. The Xbox combiner
    // constant registers are 0..7 (higher registers only matter to the
    // vs.1.1 path, which the CPU fallback consumes).
    if(cxbx::d3d8::HostBackendRenders() && pConstantData != nullptr)
    {
        const float* constants = static_cast<const float*>(pConstantData);
        for(DWORD c = 0; c < ConstantCount && Register + c < 8; ++c)
        {
            cxbx::d3d8::HostBackendSetPixelShaderConstant(
                Register + c, constants + static_cast<size_t>(c) * 4);
        }
    }

    HRESULT Result = EmuSetTranslatedPixelShaderConstants(
        g_EmuCurrentPixelShaderHandle, Register, pConstantData, ConstantCount);

    EmuSwapFS(); // Xbox FS

    return Result;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTextureState_BorderColor
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetTextureState_BorderColor(
    DWORD Stage,
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("SetTextureState_BorderColor");

HRESULT Result = D3D_OK;
    if(!cxbx::d3d8::HostBackendRenders())
        Result = g_pD3DDevice8->SetTextureStageState(Stage, D3DTSS_BORDERCOLOR, Value);

    EmuSwapFS(); // Xbox FS

    return Result;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTextureState_ColorKeyColor
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetTextureState_ColorKeyColor(
    DWORD Stage,
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("SetTextureState_ColorKeyColor");

    // Host Direct3D 8 has no texture-stage color-key equivalent. Keep the
    // call side-effect free; texture conversion paths implement formats that
    // require explicit alpha rather than relying on this NV2A comparison.
    (void)Stage;
    (void)Value;

    EmuSwapFS(); // Xbox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTextureState_BumpEnv
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetTextureState_BumpEnv(
    DWORD Stage,
    D3DTEXTURESTAGESTATETYPE Type,
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("SetTextureState_BumpEnv");

    constexpr DWORD XboxBumpEnvMat00 = 22;
    constexpr DWORD XboxBumpEnvMat01 = 23;
    constexpr DWORD XboxBumpEnvMat11 = 24;
    constexpr DWORD XboxBumpEnvMat10 = 25;
    constexpr DWORD XboxBumpEnvLScale = 26;
    constexpr DWORD XboxBumpEnvLOffset = 27;
    const DWORD xboxType = static_cast<DWORD>(Type);

    if(Stage < 8 && xboxType >= XboxBumpEnvMat00 && xboxType <= XboxBumpEnvLOffset)
    {
        // Xbox and PC D3D8 assign different numeric values to the bump-env
        // states. Forwarding the guest enum directly silently updates an
        // unrelated host state, which makes texbem sample without displacement.
        D3DTEXTURESTAGESTATETYPE hostType = D3DTSS_BUMPENVMAT00;
        switch(xboxType)
        {
            case XboxBumpEnvMat00:
                hostType = D3DTSS_BUMPENVMAT00;
                break;
            case XboxBumpEnvMat01:
                hostType = D3DTSS_BUMPENVMAT01;
                break;
            case XboxBumpEnvMat10:
                hostType = D3DTSS_BUMPENVMAT10;
                break;
            case XboxBumpEnvMat11:
                hostType = D3DTSS_BUMPENVMAT11;
                break;
            case XboxBumpEnvLScale:
                hostType = D3DTSS_BUMPENVLSCALE;
                break;
            case XboxBumpEnvLOffset:
                hostType = D3DTSS_BUMPENVLOFFSET;
                break;
            default:
                EmuWarning("SetTextureState_BumpEnv ignored unsupported type 0x%.08lX",
                           Type);
                EmuSwapFS(); // Xbox FS
                return;
        }
        g_pD3DDevice8->SetTextureStageState(Stage, hostType, Value);

        std::uint8_t matrixBit = 0;
        unsigned int rendererType = 0;
        switch(xboxType)
        {
            case XboxBumpEnvMat00:
                matrixBit = 0x01u;
                rendererType = 0;
                break;
            case XboxBumpEnvMat01:
                matrixBit = 0x02u;
                rendererType = 1;
                break;
            case XboxBumpEnvMat10:
                matrixBit = 0x04u;
                rendererType = 2;
                break;
            case XboxBumpEnvMat11:
                matrixBit = 0x08u;
                rendererType = 3;
                break;
            case XboxBumpEnvLScale:
                rendererType = 4;
                break;
            case XboxBumpEnvLOffset:
                rendererType = 5;
                break;
        }
        g_EmuBumpEnvMatrixWrites[Stage] |= matrixBit;
        // The combiner interpreter consumes the same state under the Vulkan
        // flavor; the Xbox enum orders MAT11 before MAT10, hence the switch.
        cxbx::d3d8::HostBackendSetBumpEnvState(Stage, rendererType, Value);
    }
    else
    {
        EmuWarning("SetTextureState_BumpEnv ignored invalid stage/type (%lu, 0x%.08lX)",
                   Stage,
                   Type);
    }

    EmuSwapFS(); // Xbox FS
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

HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetVisibilityTestResult(
    DWORD Index,
    UINT* pResult,
    ULONGLONG* pTimeStamp)
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
    if(Handle != 0 && Handle == g_EmuCurrentPixelShaderHandle &&
       cxbx::d3d8::HostBackendRenders())
    {
        // P4: the active combiner program went away; fall back to the fixed
        // cascade for subsequent draws.
        cxbx::d3d8::HostBackendSetPixelShader(nullptr);
        g_EmuCurrentPixelShaderHandle = 0;
    }
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("DeletePixelShader");

#ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_DeletePixelShader"
               "(Handle = 0x%.08X);\n",
               GetCurrentThreadId(), Handle);
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
                           [Handle](const EmuTranslatedPixelShader& shader)
                           {
                               return shader.handle == Handle;
                           }),
            g_EmuTranslatedPixelShaders.end());
    }
    else if((Handle & X_PIXELSHADER_FALLBACK_MASK) == X_PIXELSHADER_FALLBACK_MARKER)
    {
        g_EmuPixelShaderFallbacks.erase(
            std::remove_if(g_EmuPixelShaderFallbacks.begin(),
                           g_EmuPixelShaderFallbacks.end(),
                           [Handle](const EmuPixelShaderFallbackDefinition& fallback)
                           {
                               return fallback.handle == Handle;
                           }),
            g_EmuPixelShaderFallbacks.end());
    }

    EmuSwapFS(); // XBox FS
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
    void* pBits;
    INT Pitch;
};

#define EMU_SWIZZLED_TEXTURE_MAX_LEVELS 16

struct EmuSwizzledTextureInfo
{
    XTL::IDirect3DBaseTexture8* pHostTexture;
    DWORD dwBPP;
    DWORD dwDirtyLevels; // bitmask of levels with pending Morton-order writes
    EmuSwizzledTextureLevelLock Levels[EMU_SWIZZLED_TEXTURE_MAX_LEVELS];

    // Guest-visible Morton-order backing (X_D3DResource::Data). Titles with a
    // native D3DX/XGraphics upload path write texels straight through the
    // resource's Data pointer, never calling a LockRect wrapper; the block is
    // unswizzled into the host texture at bind time.
    BYTE* pGuestData;
    DWORD dwGuestSize;
    DWORD dwWidth;
    DWORD dwHeight;
    bool bLockPathUsed;   // a LockRect wrapper wrote this texture: trust it
    bool bGuestCommitted; // the one-shot Data-pointer upload already ran
};

static std::vector<EmuSwizzledTextureInfo> g_EmuSwizzledTextures;

static bool EmuXboxFormatIsSwizzled(DWORD XboxFormat, DWORD* pdwBPP)
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

static bool EmuXboxFormatIsLinear(DWORD xboxFormat, DWORD* bytesPerPixel)
{
    switch(xboxFormat)
    {
        case 0x11: // LIN_R5G6B5
        case 0x17: // LIN_G8B8
        case 0x20: // LIN_A8L8
        {
            *bytesPerPixel = 2;
            return true;
        }
        case 0x12: // LIN_A8R8G8B8
        {
            *bytesPerPixel = 4;
            return true;
        }
        case 0x13: // LIN_L8
        {
            *bytesPerPixel = 1;
            return true;
        }
        default:
        {
            return false;
        }
    }
}

static EmuSwizzledTextureInfo* EmuFindSwizzledTexture(
    XTL::IDirect3DBaseTexture8* pHostTexture)
{
    if(pHostTexture == nullptr)
    {
        return nullptr;
    }
    for(auto& info : g_EmuSwizzledTextures)
    {
        if(info.pHostTexture == pHostTexture)
        {
            return &info;
        }
    }
    return nullptr;
}

static void EmuDiscardSwizzledTexture(XTL::IDirect3DBaseTexture8* pHostTexture)
{
    for(auto it = g_EmuSwizzledTextures.begin(); it != g_EmuSwizzledTextures.end(); ++it)
    {
        if(it->pHostTexture == pHostTexture)
        {
            if(it->pGuestData != nullptr)
            {
                _aligned_free(it->pGuestData);
            }
            g_EmuSwizzledTextures.erase(it);
            return;
        }
    }
}

static bool EmuD3DTexTraceEnabled()
{
    static int s_Enabled = -1;
    if(s_Enabled < 0)
        s_Enabled = EmuD3DEnvironmentEnabled("CXBX_TEX_TRACE") ? 1 : 0;
    return s_Enabled == 1;
}

// LINEAR texture metadata serves two purposes: registered resources with guest
// backing need a fresh upload, while host-created linear textures still need
// texel-space coordinate normalization when sampled.
struct EmuLinearTextureRefresh
{
    XTL::IDirect3DBaseTexture8* pHostTexture;
    const BYTE* pGuestData;
    DWORD dwPitch;
    DWORD dwWidth;
    DWORD dwHeight;
    DWORD dwBPP;
};

static std::vector<EmuLinearTextureRefresh> g_EmuLinearTextureRefreshes;

static void EmuTrackLinearTexture(XTL::IDirect3DBaseTexture8* pHostTexture,
                                  const BYTE* pGuestData, DWORD dwPitch,
                                  DWORD dwWidth, DWORD dwHeight, DWORD dwBPP)
{
    if(pHostTexture == nullptr)
    {
        return;
    }
    for(auto& entry : g_EmuLinearTextureRefreshes)
    {
        if(entry.pHostTexture == pHostTexture)
        {
            entry = { pHostTexture, pGuestData, dwPitch, dwWidth, dwHeight, dwBPP };
            return;
        }
    }
    g_EmuLinearTextureRefreshes.push_back(
        { pHostTexture, pGuestData, dwPitch, dwWidth, dwHeight, dwBPP });

    if(EmuD3DTexTraceEnabled())
    {
        printf("TEX| linear-track host=0x%p guest=0x%p pitch=%lu %lux%lu bpp=%lu\n",
               pHostTexture, pGuestData, dwPitch, dwWidth, dwHeight, dwBPP);
        fflush(stdout);
    }
}

static void EmuDiscardLinearTexture(XTL::IDirect3DBaseTexture8* pHostTexture)
{
    for(auto it = g_EmuLinearTextureRefreshes.begin();
        it != g_EmuLinearTextureRefreshes.end(); ++it)
    {
        if(it->pHostTexture == pHostTexture)
        {
            g_EmuLinearTextureRefreshes.erase(it);
            return;
        }
    }
}

// Guest-direct lock serving: returns the tracked live guest memory (level
// 0) of a registered linear texture, with its pitch. On real hardware a
// linear-texture LockRect returns this same UMA memory -- serving it keeps
// the title's writes and the container's live refresh consistent without
// touching the host texture.
static const BYTE* EmuLinearTrackedGuestData(
    XTL::IDirect3DBaseTexture8* pHostTexture, LONG* outPitch)
{
    for(const auto& entry : g_EmuLinearTextureRefreshes)
    {
        if(entry.pHostTexture == pHostTexture && entry.pGuestData != nullptr &&
           EmuD3DIsReadableRange(
               entry.pGuestData,
               static_cast<std::size_t>(entry.dwPitch) * entry.dwHeight))
        {
            if(outPitch != nullptr)
            {
                *outPitch = static_cast<LONG>(entry.dwPitch);
            }
            return entry.pGuestData;
        }
    }
    return nullptr;
}

static void EmuCopyLinearTexture(XTL::IDirect3DTexture8* pTexture,
                                 const BYTE* pGuestData, DWORD dwPitch,
                                 DWORD dwWidth, DWORD dwHeight, DWORD dwBPP)
{
    XTL::D3DLOCKED_RECT lock;
    __try
    {
        if(FAILED(pTexture->LockRect(0, &lock, NULL, 0)))
            return;

        const DWORD rowBytes = dwWidth * dwBPP;
        BYTE* destination = static_cast<BYTE*>(lock.pBits);
        const BYTE* source = pGuestData;
        for(DWORD y = 0; y < dwHeight; y++)
        {
            memcpy(destination, source, rowBytes);
            destination += lock.Pitch;
            source += dwPitch;
        }
        pTexture->UnlockRect(0);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

// P8 part 2 slice A: the native pixel container. Under the Vulkan flavor
// the container is the authoritative level-0 store: the mirror binds from
// it without a host LockRect, title locks write it in place, the linear
// refresh copies guest rows into it, and the swizzled commit unswizzles
// level 0 into it. The d3d8 fallback flavor never consults it and keeps
// today's host-texture paths. Keyed by the host texture pointer; slice C
// re-keys on the guest resource when the host objects go away.
struct EmuNativeTexture
{
    XTL::IDirect3DBaseTexture8* pHostTexture = nullptr;
    // Slice C: the GUEST resource pointer (stable for the resource's
    // lifetime) when known -- binds and releases resolve by it first, so a
    // recycled host pointer can never alias a live container.
    XTL::X_D3DResource* pGuestResource = nullptr;
    std::vector<BYTE> Pixels; // level-0 texels, host-format byte layout
    UINT Pitch = 0;
    UINT Width = 0;
    UINT Height = 0;
    UINT HostFormat = 0;
};
static std::vector<EmuNativeTexture> g_EmuNativeTextures;

static EmuNativeTexture* EmuNativeTextureFind(
    XTL::IDirect3DBaseTexture8* pHostTexture)
{
    for(auto& entry : g_EmuNativeTextures)
    {
        if(entry.pHostTexture == pHostTexture)
        {
            return &entry;
        }
    }
    return nullptr;
}

// Bind-time lookup: prefer the guest identity, fall back to the host
// pointer for entries that never learned it.
static EmuNativeTexture* EmuNativeTextureFindForBind(
    XTL::IDirect3DBaseTexture8* pHostTexture, XTL::X_D3DResource* pGuestResource)
{
    if(pGuestResource != nullptr)
    {
        for(auto& entry : g_EmuNativeTextures)
        {
            if(entry.pGuestResource == pGuestResource)
            {
                return &entry;
            }
        }
    }
    return pHostTexture != nullptr ? EmuNativeTextureFind(pHostTexture)
                                   : nullptr;
}

// Attaches the guest identity to an existing container (Register-time).
static void EmuNativeTextureAttachGuest(
    XTL::IDirect3DBaseTexture8* pHostTexture, XTL::X_D3DResource* pGuestResource)
{
    if(pHostTexture == nullptr || pGuestResource == nullptr)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    if(EmuNativeTexture* entry = EmuNativeTextureFind(pHostTexture))
    {
        entry->pGuestResource = pGuestResource;
    }
}

static void EmuNativeTextureForgetGuest(XTL::X_D3DResource* pGuestResource)
{
    if(pGuestResource == nullptr)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    for(std::size_t i = 0; i < g_EmuNativeTextures.size(); ++i)
    {
        if(g_EmuNativeTextures[i].pGuestResource == pGuestResource)
        {
            g_EmuNativeTextures.erase(g_EmuNativeTextures.begin() + i);
            return;
        }
    }
}

// Creates the container for pHostTexture when missing, pre-filling level 0
// from the host texture so the first lock sees Register-time content.
// Returns null when the host texture cannot be read.
static EmuNativeTexture* EmuNativeTextureEnsure(
    XTL::IDirect3DBaseTexture8* pHostTexture)
{
    if(EmuNativeTexture* existing = EmuNativeTextureFind(pHostTexture))
    {
        return existing;
    }
    XTL::IDirect3DTexture8* pTexture8 =
        static_cast<XTL::IDirect3DTexture8*>(pHostTexture);
    XTL::D3DSURFACE_DESC description = {};
    if(FAILED(pTexture8->GetLevelDesc(0, &description)) ||
       description.Width == 0 || description.Height == 0)
    {
        return nullptr;
    }
    XTL::D3DLOCKED_RECT locked = {};
    if(FAILED(pTexture8->LockRect(0, &locked, NULL, D3DLOCK_READONLY)))
    {
        return nullptr;
    }
    EmuNativeTexture entry;
    entry.pHostTexture = pHostTexture;
    entry.Pitch = static_cast<UINT>(locked.Pitch);
    entry.Width = description.Width;
    entry.Height = description.Height;
    entry.HostFormat = static_cast<UINT>(description.Format);
    entry.Pixels.resize(static_cast<std::size_t>(entry.Pitch) * entry.Height);
    memcpy(entry.Pixels.data(), locked.pBits, entry.Pixels.size());
    pTexture8->UnlockRect(0);
    g_EmuNativeTextures.push_back(std::move(entry));
    return &g_EmuNativeTextures.back();
}

// Adopts an already-pulled level-0 image as the container birth content
// (the mirror pull path; avoids a second host lock).
static EmuNativeTexture* EmuNativeTextureAdopt(
    XTL::IDirect3DBaseTexture8* pHostTexture, const void* pBits, UINT pitch,
    UINT width, UINT height, UINT hostFormat)
{
    if(pBits == nullptr || pitch == 0 || width == 0 || height == 0)
    {
        return nullptr;
    }
    EmuNativeTexture entry;
    entry.pHostTexture = pHostTexture;
    entry.Pitch = pitch;
    entry.Width = width;
    entry.Height = height;
    entry.HostFormat = hostFormat;
    entry.Pixels.resize(static_cast<std::size_t>(pitch) * height);
    memcpy(entry.Pixels.data(), pBits,
           static_cast<std::size_t>(pitch) * height);
    g_EmuNativeTextures.push_back(std::move(entry));
    return &g_EmuNativeTextures.back();
}

static void EmuNativeTextureForget(XTL::IDirect3DBaseTexture8* pHostTexture)
{
    // Self-locking: no caller may hold the registry mutex (std::mutex is not
    // recursive).
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    for(std::size_t i = 0; i < g_EmuNativeTextures.size(); ++i)
    {
        if(g_EmuNativeTextures[i].pHostTexture == pHostTexture)
        {
            g_EmuNativeTextures.erase(g_EmuNativeTextures.begin() + i);
            return;
        }
    }
}

// Mutex-carrying Ensure for call sites inside __try functions (the lock
// must not live in a frame that carries SEH).
static void EmuNativeTextureEnsureThreadsafe(
    XTL::IDirect3DBaseTexture8* pHostTexture)
{
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    EmuNativeTextureEnsure(pHostTexture);
}

// Stage 2: finds or creates the guest-keyed container (zero-filled plus
// slop) at the given tight pitch. Returns null when the host pointer maps
// to a container owned by a different guest resource. Callers hold the
// mutex while filling.
static EmuNativeTexture* EmuNativeTextureEnsureGuestContainer(
    XTL::IDirect3DBaseTexture8* pHostTexture, XTL::X_D3DResource* pGuestResource,
    UINT width, UINT height, UINT pitch, UINT hostFormat)
{
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    EmuNativeTexture* entry = nullptr;
    for(auto& candidate : g_EmuNativeTextures)
    {
        if(candidate.pGuestResource == pGuestResource)
        {
            entry = &candidate;
            break;
        }
    }
    if(entry == nullptr)
    {
        if(EmuNativeTexture* byHost = EmuNativeTextureFind(pHostTexture))
        {
            if(byHost->pGuestResource != nullptr)
            {
                // The host pointer maps to a container owned by a different
                // guest resource: never overwrite it.
                return nullptr;
            }
        }
        g_EmuNativeTextures.push_back({});
        entry = &g_EmuNativeTextures.back();
        entry->pHostTexture = pHostTexture;
        entry->pGuestResource = pGuestResource;
        entry->HostFormat = hostFormat;
        entry->Width = width;
        entry->Height = height;
        entry->Pitch = pitch;
        entry->Pixels.resize(static_cast<std::size_t>(pitch) * height);
    }
    return entry;
}

// Stage 2 pilot: fills the container GUEST-DIRECT from the Register
// conversion's validated linear rows -- the host texture is never locked
// or written. Keyed by the host pointer with the guest resource attached
// (binds and releases resolve by the guest identity).
static void EmuNativeTextureEnsureGuestFilled(
    XTL::IDirect3DBaseTexture8* pHostTexture, XTL::X_D3DResource* pGuestResource,
    const void* pRows, UINT srcPitch, UINT width, UINT height,
    UINT hostFormat, UINT bytesPerPixel)
{
    if(pHostTexture == nullptr || pGuestResource == nullptr || pRows == nullptr ||
       width == 0 || height == 0 || hostFormat == 0 || bytesPerPixel == 0)
    {
        return;
    }
    EmuNativeTexture* entry = EmuNativeTextureEnsureGuestContainer(
        pHostTexture, pGuestResource, width, height, width * bytesPerPixel,
        hostFormat);
    if(entry == nullptr)
    {
        return;
    }
    const UINT rowBytes = min(entry->Pitch, srcPitch);
    for(UINT y = 0; y < entry->Height; ++y)
    {
        memcpy(entry->Pixels.data() +
                   static_cast<std::size_t>(y) * entry->Pitch,
               static_cast<const BYTE*>(pRows) +
                   static_cast<std::size_t>(y) * srcPitch,
               rowBytes);
    }
}

// P8 C3-texture: served level-0 locks. The texture analogue of the surface
// container handoff: a granted writable full lock of level 0 is re-served
// from the container (the guest edits native bytes; commits push the rows
// back into the host texture at the shared flush points), and the bind pull
// serves the container instead of re-reading the host texture. Swizzled
// textures are excluded -- their Morton-order lock machinery owns the lock
// bytes and updates the container through its own commit path.
struct EmuTextureContainerLock
{
    XTL::X_D3DResource* pResource = nullptr;
    XTL::IDirect3DTexture8* pTexture8 = nullptr;
    XTL::IDirect3DBaseTexture8* pHostTexture = nullptr;
    bool Writable = false;
    // Set while a commit is copying this slot's rows: a registry-full steal
    // must not retire a claim whose buffer the copy is still reading.
    bool Committing = false;
    // Monotonic claim order: the registry-full steal retires the oldest.
    unsigned long ClaimSeq = 0;
};
static EmuTextureContainerLock g_EmuTextureContainerLocks[8] = {};
static unsigned long g_EmuTextureContainerLockSeq = 0;

// Lock-slot array helpers. The _Locked forms require the caller to hold
// EmuSurfaceContainerMutex(); the plain forms take it themselves.
static EmuTextureContainerLock* EmuFindTextureContainerLockLocked(
    XTL::X_D3DResource* pResource)
{
    for(auto& slot : g_EmuTextureContainerLocks)
    {
        if(slot.pResource == pResource)
        {
            return &slot;
        }
    }
    return nullptr;
}

static EmuTextureContainerLock* EmuFindTextureContainerLock(
    XTL::X_D3DResource* pResource)
{
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    return EmuFindTextureContainerLockLocked(pResource);
}

static EmuTextureContainerLock* EmuFindTextureContainerLockByHostLocked(
    XTL::IDirect3DBaseTexture8* pHostTexture)
{
    for(auto& slot : g_EmuTextureContainerLocks)
    {
        if(slot.pHostTexture == pHostTexture)
        {
            return &slot;
        }
    }
    return nullptr;
}

static EmuTextureContainerLock* EmuFindTextureContainerLockByHost(
    XTL::IDirect3DBaseTexture8* pHostTexture)
{
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    return EmuFindTextureContainerLockByHostLocked(pHostTexture);
}

static EmuTextureContainerLock* EmuFindFreeTextureContainerLockLocked()
{
    EmuTextureContainerLock* pRetired = nullptr;
    for(auto& slot : g_EmuTextureContainerLocks)
    {
        if(slot.pResource == nullptr)
        {
            slot.ClaimSeq = ++g_EmuTextureContainerLockSeq;
            return &slot;
        }
        if(!slot.Committing &&
           (pRetired == nullptr || slot.ClaimSeq < pRetired->ClaimSeq))
        {
            pRetired = &slot;
        }
    }
    if(pRetired == nullptr)
    {
        // Every slot is mid-commit: hand out slot 0 to preserve liveness.
        pRetired = &g_EmuTextureContainerLocks[0];
    }
    pRetired->ClaimSeq = ++g_EmuTextureContainerLockSeq;
    return pRetired;
}

static void EmuReleaseTextureContainerLockSlotLocked(
    EmuTextureContainerLock* pSlot)
{
    pSlot->pResource = nullptr;
    pSlot->pTexture8 = nullptr;
    pSlot->pHostTexture = nullptr;
    pSlot->Writable = false;
    pSlot->Committing = false;
}

static void EmuReleaseTextureContainerLockSlot(
    EmuTextureContainerLock* pSlot)
{
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    EmuReleaseTextureContainerLockSlotLocked(pSlot);
}

// Pushes one outstanding served texture lock's container rows back into the
// host texture's level 0. Host textures always take level locks, so there is
// no refusal path (unlike surfaces).
// Mutex-guarded container lookup; the returned pointer is only valid while
// the registry mutex is held (the registry is a vector, so a concurrent
// claim can reallocate it).
static EmuNativeTexture* EmuAcquireTextureContainer(
    XTL::IDirect3DBaseTexture8* pHostTexture)
{
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    return pHostTexture != nullptr ? EmuNativeTextureFind(pHostTexture)
                                   : nullptr;
}

// Mutex-held row copy; separate helper because the commit body carries __try.
static void EmuCommitCopyTextureRows(XTL::IDirect3DBaseTexture8* pHostTexture,
                                     void* pDstBits, LONG dstPitch)
{
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    if(EmuNativeTexture* pNative = EmuNativeTextureFind(pHostTexture))
    {
        const UINT rowBytes = min(pNative->Pitch, static_cast<UINT>(dstPitch));
        for(UINT y = 0; y < pNative->Height; ++y)
        {
            memcpy(static_cast<BYTE*>(pDstBits) +
                       static_cast<std::size_t>(y) * dstPitch,
                   pNative->Pixels.data() +
                       static_cast<std::size_t>(y) * pNative->Pitch,
                   rowBytes);
        }
    }
}

// Mutex-carried slot claim for the commit body (separate helper: the commit
// body itself carries __try and must stay free of unwinding objects).
// Returns false when the slot is empty or another commit already owns it;
// the claim is held (Committing) until the copy ends.
static bool EmuClaimTextureContainerLockForCommit(
    EmuTextureContainerLock* pLock, XTL::IDirect3DTexture8** ppTexture8,
    XTL::IDirect3DBaseTexture8** ppHostTexture, bool* pWritable)
{
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    if(pLock->pResource == nullptr || pLock->Committing)
    {
        return false;
    }
    *ppTexture8 = pLock->pTexture8;
    *ppHostTexture = pLock->pHostTexture;
    *pWritable = pLock->Writable;
    pLock->Committing = true;
    return true;
}

static void EmuCommitTextureContainerLock(EmuTextureContainerLock* pLock)
{
    XTL::IDirect3DTexture8* pTexture8 = nullptr;
    XTL::IDirect3DBaseTexture8* pHostTexture = nullptr;
    bool writable = false;
    if(!EmuClaimTextureContainerLockForCommit(pLock, &pTexture8,
                                              &pHostTexture, &writable))
    {
        return;
    }

    if(pTexture8 == nullptr || !writable)
    {
        EmuReleaseTextureContainerLockSlot(pLock);
        return;
    }

    XTL::D3DLOCKED_RECT locked = {};
    HRESULT hRet = D3DERR_INVALIDCALL;
    __try
    {
        hRet = pTexture8->LockRect(0, &locked, NULL, 0);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        hRet = D3DERR_INVALIDCALL;
    }
    if(FAILED(hRet))
    {
        EmuReleaseTextureContainerLockSlot(pLock);
        return;
    }

    EmuCommitCopyTextureRows(pHostTexture, locked.pBits, locked.Pitch);
    __try
    {
        pTexture8->UnlockRect(0);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    EmuReleaseTextureContainerLockSlot(pLock);
}

static void EmuCommitTextureContainerLock(XTL::X_D3DResource* pResource)
{
    if(pResource == NULL)
    {
        return;
    }
    if(EmuTextureContainerLock* pLock = EmuFindTextureContainerLock(pResource))
    {
        EmuCommitTextureContainerLock(pLock);
    }
}

// Mutex note: texture locks run on the guest's D3D threads like surface
// locks; the commits themselves run with the registry mutex released (the
// snapshot below hands out stable slot addresses).
static void EmuCommitTextureContainerLocks()
{
    if(!cxbx::d3d8::HostBackendRenders())
    {
        return;
    }
    EmuTextureContainerLock* pending[sizeof(g_EmuTextureContainerLocks) / sizeof(g_EmuTextureContainerLocks[0])];
    unsigned count = 0;
    {
        std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
        for(unsigned v = 0;
            v < sizeof(g_EmuTextureContainerLocks) / sizeof(g_EmuTextureContainerLocks[0]);
            v++)
        {
            if(g_EmuTextureContainerLocks[v].pResource != nullptr &&
               !g_EmuTextureContainerLocks[v].Committing)
            {
                pending[count++] = &g_EmuTextureContainerLocks[v];
            }
        }
    }
    for(unsigned v = 0; v < count; v++)
    {
        EmuCommitTextureContainerLock(pending[v]);
    }
}

// Drops the texture container and any outstanding served lock when the host
// texture dies or an un-modeled write path (level-surface locks, CopyRects
// into levels) invalidates the captured bytes: the bind pull falls back to
// the host texture, which is authoritative.
static void EmuForgetTextureContainerState(
    XTL::IDirect3DBaseTexture8* pHostTexture)
{
    if(pHostTexture == nullptr)
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
        if(EmuTextureContainerLock* pLock =
               EmuFindTextureContainerLockByHostLocked(pHostTexture))
        {
            // A slot mid-commit releases itself when its copy ends; clearing
            // it here could drop whichever claim took the slot over in the
            // meantime.
            if(!pLock->Committing)
            {
                EmuReleaseTextureContainerLockSlotLocked(pLock);
            }
        }
    }
    // Takes the registry mutex itself.
    EmuNativeTextureForget(pHostTexture);
}

// Re-serves a granted writable full level-0 lock from the container. Called
// only after a successful host LockRect on a non-swizzled texture, so every
// host object here is alive. Returns false when the serve was abandoned (the
// guest keeps the held host lock and its host bits).
static bool EmuServeTextureContainerFromLock(
    XTL::X_D3DResource* pResource, XTL::IDirect3DTexture8* pTexture8,
    XTL::D3DLOCKED_RECT* pLockedRect)
{
    // Registry mutations (vector claims) serialize against the bind pull,
    // commits, and the other capture paths through the shared registry
    // mutex; guest lock threads can race the render thread here.
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    // A serve is already outstanding: the guest keeps editing the same
    // container view; discard the fresh host bits.
    if(EmuTextureContainerLock* pOpen =
           EmuFindTextureContainerLockLocked(pResource))
    {
        EmuNativeTexture* pServed = EmuNativeTextureFind(pTexture8);
        if(pServed != nullptr)
        {
            EmuSafeHostTextureLevelUnlock(pTexture8);
            pLockedRect->pBits = pServed->Pixels.data();
            pLockedRect->Pitch = static_cast<LONG>(pServed->Pitch);
        }
        return pServed != nullptr;
    }

    XTL::D3DSURFACE_DESC description = {};
    if(FAILED(pTexture8->GetLevelDesc(0, &description)) ||
       description.Width == 0 || description.Height == 0)
    {
        return false;
    }

    // The host texture lock bits must be fully readable before seeding; see
    // the surface container for why faulting probes are not viable.
    if(!EmuSpanReadable(pLockedRect->pBits,
                        static_cast<std::size_t>(pLockedRect->Pitch) *
                            description.Height))
    {
        if(EmuSurfaceContainerTraceEnabled())
        {
            printf("SURF| tex-serve tid=%X tex=0x%.08lX FALLBACK bits "
                   "unreadable\n",
                   GetCurrentThreadId(),
                   reinterpret_cast<unsigned long>(pTexture8));
            fflush(stdout);
        }
        return false;
    }

    bool freshlyClaimed = false;
    EmuNativeTexture* pNative = EmuNativeTextureFind(pTexture8);
    if(pNative == nullptr)
    {
        EmuNativeTexture entry;
        entry.pHostTexture = pTexture8;
        entry.Pitch = static_cast<UINT>(pLockedRect->Pitch);
        entry.Width = description.Width;
        entry.Height = description.Height;
        entry.HostFormat = static_cast<UINT>(description.Format);
        entry.Pixels.resize(static_cast<std::size_t>(entry.Pitch) *
                                entry.Height +
                            EMU_SURFACE_CONTAINER_SLOP);
        g_EmuNativeTextures.push_back(std::move(entry));
        pNative = &g_EmuNativeTextures.back();
        freshlyClaimed = true;
    }
    else if(pNative->Width != description.Width ||
            pNative->Height != description.Height ||
            pNative->Pitch != static_cast<UINT>(pLockedRect->Pitch))
    {
        pNative->Width = description.Width;
        pNative->Height = description.Height;
        pNative->Pitch = static_cast<UINT>(pLockedRect->Pitch);
        pNative->Pixels.resize(static_cast<std::size_t>(pNative->Pitch) *
                                   pNative->Height +
                               EMU_SURFACE_CONTAINER_SLOP);
    }

    if(!EmuSeedSurfaceContainerSafe(
           pNative->Pixels.data(), pNative->Pitch, pLockedRect->pBits,
           pLockedRect->Pitch, pNative->Height,
           min(pNative->Pitch, static_cast<UINT>(pLockedRect->Pitch))))
    {
        if(EmuSurfaceContainerTraceEnabled())
        {
            printf("SURF| tex-serve tid=%X tex=0x%.08lX FALLBACK seed "
                   "fault\n",
                   GetCurrentThreadId(),
                   reinterpret_cast<unsigned long>(pTexture8));
            fflush(stdout);
        }
        if(freshlyClaimed)
        {
            g_EmuNativeTextures.pop_back();
        }
        return false;
    }

    EmuTextureContainerLock* pSlot = EmuFindFreeTextureContainerLockLocked();
    pSlot->pResource = pResource;
    pSlot->pTexture8 = pTexture8;
    pSlot->pHostTexture = pTexture8;
    pSlot->Writable = true;
    pSlot->Committing = false;

    // Release the host lock and hand the guest the container memory.
    EmuSafeHostTextureLevelUnlock(pTexture8);
    pLockedRect->pBits = pNative->Pixels.data();
    pLockedRect->Pitch = static_cast<LONG>(pNative->Pitch);
    if(EmuSurfaceContainerTraceEnabled())
    {
        printf("SURF| tex-serve tid=%X tex=0x%.08lX %ux%u pitch=%lu "
               "bits=0x%.08lX\n",
               GetCurrentThreadId(),
               reinterpret_cast<unsigned long>(pTexture8),
               static_cast<unsigned>(pNative->Width),
               static_cast<unsigned>(pNative->Height),
               static_cast<unsigned long>(pNative->Pitch),
               reinterpret_cast<unsigned long>(pNative->Pixels.data()));
        fflush(stdout);
    }
    return true;
}

// Re-captures level 0 into the container after an un-modeled write path
// (swizzled commit) produced fresh linear rows in the host texture.
static void EmuRefreshTextureContainerFromLock(
    XTL::IDirect3DBaseTexture8* pHostTexture, const void* pBits, LONG pitch,
    UINT width, UINT height)
{
    if(pHostTexture == nullptr || pBits == nullptr || pitch <= 0 ||
       width == 0 || height == 0)
    {
        return;
    }
    if(EmuTextureContainerLock* pOpen =
           EmuFindTextureContainerLockByHost(pHostTexture))
    {
        // A served lock is outstanding: the container holds the guest's
        // newest bytes; do not overwrite them with the host snapshot.
        (void)pOpen;
        return;
    }
    if(!EmuSpanReadable(pBits, static_cast<std::size_t>(pitch) * height))
    {
        EmuNativeTextureForget(pHostTexture);
        return;
    }
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    if(EmuFindTextureContainerLockByHostLocked(pHostTexture) != nullptr)
    {
        // Re-check under the mutex: a serve may have claimed meanwhile.
        return;
    }
    EmuNativeTexture* pNative = EmuNativeTextureEnsure(pHostTexture);
    if(pNative == nullptr)
    {
        return;
    }
    const UINT rowBytes = min(pNative->Pitch, static_cast<UINT>(pitch));
    for(UINT y = 0; y < pNative->Height && y < height; ++y)
    {
        memcpy(pNative->Pixels.data() +
                   static_cast<std::size_t>(y) * pNative->Pitch,
               static_cast<const BYTE*>(pBits) +
                   static_cast<std::size_t>(y) * pitch,
               rowBytes);
    }
}

// P8 part 2 slice B: the native byte container for vertex/index buffers,
// the exact analogue of the texture container. Under the Vulkan flavor the
// CPU-fallback draw paths read the container instead of host Lock(READONLY),
// and title vertex-buffer locks write it in place. Keyed by the host buffer
// pointer (vertex and index buffers share the registry); slice C re-keys on
// the guest resource.
//
// The registry is a fixed slot array under EmuSurfaceContainerMutex, NOT a
// vector: the surface registry converted away from a reallocating vector
// after multi-threaded churn corrupted the heap, and the buffer paths run on
// the same guest threads. Containers carry the surface container's slop
// tail: title writers overrun a locked buffer's nominal size the same way
// they overrun locked surfaces, and an exactly-sized container turned that
// into host heap corruption.
#define EMU_BUFFER_CONTAINER_SLOTS 256

struct EmuNativeBuffer
{
    void* pHostBuffer = nullptr;
    // Slice C: the GUEST wrapper pointer when known -- releases resolve by
    // it, so a recycled host buffer pointer can never alias a live
    // container.
    void* pGuestResource = nullptr;
    std::vector<BYTE> Bytes; // Size payload bytes plus the slop tail
    UINT Size = 0;
    bool Used = false;
    // Monotonic adoption order: the registry-full eviction retires the
    // oldest (eviction is harmless -- the next lock re-births the buffer).
    unsigned long ClaimSeq = 0;
};
static EmuNativeBuffer g_EmuNativeBuffers[EMU_BUFFER_CONTAINER_SLOTS] = {};
static unsigned long g_EmuNativeBufferSeq = 0;

// Caller holds EmuSurfaceContainerMutex(). The returned pointer (and its
// Bytes buffer) stays stable while the mutex is held; hand the guest memory
// only inside that window.
static EmuNativeBuffer* EmuNativeBufferFindLocked(void* pHostBuffer)
{
    for(auto& slot : g_EmuNativeBuffers)
    {
        if(slot.Used && slot.pHostBuffer == pHostBuffer)
        {
            return &slot;
        }
    }
    return nullptr;
}

// Adopts an already-locked host buffer image as the container birth content
// (the consumer lock paths and the Register fill sites). Self-locking: no
// caller may hold EmuSurfaceContainerMutex().
static void EmuNativeBufferAdopt(void* pHostBuffer, const void* pData,
                                 UINT size)
{
    if(pData == nullptr || size == 0)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    if(EmuNativeBuffer* existing = EmuNativeBufferFindLocked(pHostBuffer))
    {
        existing->Bytes.assign(static_cast<const BYTE*>(pData),
                               static_cast<const BYTE*>(pData) + size);
        existing->Size = size;
        existing->Bytes.resize(
            static_cast<std::size_t>(size) + EMU_SURFACE_CONTAINER_SLOP,
            EMU_SURFACE_CONTAINER_SLOP_FILL);
        return;
    }
    EmuNativeBuffer* pSlot = nullptr;
    for(auto& slot : g_EmuNativeBuffers)
    {
        if(!slot.Used)
        {
            pSlot = &slot;
            break;
        }
    }
    if(pSlot == nullptr)
    {
        // Registry full: retire the oldest container. The retired buffer's
        // next lock re-births it, so eviction only costs a host pull.
        pSlot = &g_EmuNativeBuffers[0];
        for(auto& slot : g_EmuNativeBuffers)
        {
            if(slot.ClaimSeq < pSlot->ClaimSeq)
            {
                pSlot = &slot;
            }
        }
    }
    pSlot->Used = true;
    pSlot->pHostBuffer = pHostBuffer;
    pSlot->pGuestResource = nullptr;
    pSlot->Size = size;
    pSlot->ClaimSeq = ++g_EmuNativeBufferSeq;
    pSlot->Bytes.assign(static_cast<const BYTE*>(pData),
                        static_cast<const BYTE*>(pData) + size);
    pSlot->Bytes.resize(
        static_cast<std::size_t>(size) + EMU_SURFACE_CONTAINER_SLOP,
        EMU_SURFACE_CONTAINER_SLOP_FILL);
}

// Drops a container when the host buffer dies, so a recycled pointer never
// resolves to stale bytes. Self-locking.
static void EmuNativeBufferForget(void* pHostBuffer)
{
    if(pHostBuffer == nullptr)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    if(EmuNativeBuffer* existing = EmuNativeBufferFindLocked(pHostBuffer))
    {
        existing->Used = false;
        existing->pHostBuffer = nullptr;
        existing->pGuestResource = nullptr;
        existing->Size = 0;
        existing->Bytes.clear();
        existing->Bytes.shrink_to_fit();
    }
}

// Slice C: attaches the GUEST wrapper identity to a buffer container so
// releases resolve by it (a recycled host pointer can never alias).
static void EmuNativeBufferAttachGuest(void* pHostBuffer,
                                       void* pGuestResource)
{
    if(pHostBuffer == nullptr || pGuestResource == nullptr)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    if(EmuNativeBuffer* existing = EmuNativeBufferFindLocked(pHostBuffer))
    {
        existing->pGuestResource = pGuestResource;
    }
}

static void EmuNativeBufferForgetGuest(void* pGuestResource)
{
    if(pGuestResource == nullptr)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    for(auto& slot : g_EmuNativeBuffers)
    {
        if(slot.Used && slot.pGuestResource == pGuestResource)
        {
            slot.Used = false;
            slot.pHostBuffer = nullptr;
            slot.pGuestResource = nullptr;
            slot.Size = 0;
            slot.Bytes.clear();
            slot.Bytes.shrink_to_fit();
            return;
        }
    }
}

// Serves a guest vertex-buffer lock from the container: base +
// OffsetToLock, bounded by the locked range (SizeToLock 0 locks to the end
// of the buffer). Returns false when no container backs the buffer or the
// range leaves it, so the caller falls back to the host lock. The mutex
// lives in this frame: the caller's body carries __try.
static bool EmuServeNativeBufferLock(void* pHostBuffer, UINT OffsetToLock,
                                     UINT SizeToLock, BYTE** ppbData)
{
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    EmuNativeBuffer* native = EmuNativeBufferFindLocked(pHostBuffer);
    if(native == nullptr || OffsetToLock > native->Size ||
       (SizeToLock != 0 && OffsetToLock + SizeToLock > native->Size))
    {
        return false;
    }
    *ppbData = native->Bytes.data() + OffsetToLock;
    return true;
}

// Serves a CPU-fallback read (VSH draw paths) with the whole container view.
// Returns 0 when no container backs the buffer.
static UINT EmuServeNativeBufferBytes(void* pHostBuffer, BYTE** ppData)
{
    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
    if(EmuNativeBuffer* native = EmuNativeBufferFindLocked(pHostBuffer))
    {
        *ppData = native->Bytes.data();
        return native->Size;
    }
    return 0;
}

static void EmuRefreshLinearTexture(XTL::IDirect3DBaseTexture8* pBaseTexture)
{
    if(pBaseTexture == nullptr)
    {
        return;
    }

    for(size_t i = 0; i < g_EmuLinearTextureRefreshes.size(); i++)
    {
        const EmuLinearTextureRefresh& entry = g_EmuLinearTextureRefreshes[i];
        if(entry.pHostTexture != pBaseTexture)
        {
            continue;
        }

        if(entry.pGuestData != nullptr &&
           !IsBadReadPtr(entry.pGuestData,
                         (size_t)entry.dwPitch * entry.dwHeight))
        {
// Slice A: under the Vulkan flavor the refreshed rows land in the
            // container (the mirror's source); the fallback copies into
            // the host texture as today.
            if(cxbx::d3d8::HostBackendRenders())
            {
                std::lock_guard<std::mutex> lock(
                    EmuSurfaceContainerMutex());
                if(EmuNativeTexture* native = EmuNativeTextureEnsure(pBaseTexture))
                {
                    const DWORD rowBytes = entry.dwWidth * entry.dwBPP;
                    for(DWORD y = 0; y < entry.dwHeight; ++y)
                    {
                        memcpy(native->Pixels.data() +
                                   static_cast<std::size_t>(y) * native->Pitch,
                               entry.pGuestData +
                                   static_cast<std::size_t>(y) * entry.dwPitch,
                               rowBytes);
                    }
                }
                return;
            }
            EmuCopyLinearTexture(static_cast<XTL::IDirect3DTexture8*>(pBaseTexture),
                                 entry.pGuestData, entry.dwPitch,
                                 entry.dwWidth, entry.dwHeight, entry.dwBPP);
        }
        return;
    }
}

static void EmuConfigureLinearTextureCoordinates(
    DWORD stage, XTL::IDirect3DBaseTexture8* baseTexture)
{
    static bool linearStages[4] = {};
    if(stage >= 4 || g_pD3DDevice8 == nullptr)
    {
        return;
    }

    const EmuLinearTextureRefresh* linearTexture = nullptr;
    for(const auto& entry : g_EmuLinearTextureRefreshes)
    {
        if(entry.pHostTexture == baseTexture)
        {
            linearTexture = &entry;
            break;
        }
    }

    if(linearTexture == nullptr && !linearStages[stage])
    {
        return;
    }

    XTL::D3DMATRIX transform = {};
    transform.m[0][0] = 1.0f;
    transform.m[1][1] = 1.0f;
    transform.m[2][2] = 1.0f;
    transform.m[3][3] = 1.0f;
    DWORD flags = XTL::D3DTTFF_DISABLE;
    if(linearTexture != nullptr && linearTexture->dwWidth != 0 &&
       linearTexture->dwHeight != 0)
    {
        transform.m[0][0] = 1.0f / static_cast<float>(linearTexture->dwWidth);
        transform.m[1][1] = 1.0f / static_cast<float>(linearTexture->dwHeight);
        flags = XTL::D3DTTFF_COUNT2;
        linearStages[stage] = true;
    }
    else
    {
        linearStages[stage] = false;
    }

    EmuForwardCacheFlush();
    g_pD3DDevice8->SetTransform(
        static_cast<XTL::D3DTRANSFORMSTATETYPE>(16 + stage), &transform);
    g_pD3DDevice8->SetTextureStageState(
        stage, XTL::D3DTSS_TEXTURETRANSFORMFLAGS, flags);
}

// Titles (guest D3DX in particular) also upload swizzled texels through
// GetSurfaceLevel + Surface::LockRect, which bypasses the texture-lock
// tracking above. Remember which host surfaces belong to a tracked swizzled
// texture so the surface-lock path can mark the level dirty too.
// Slice C: the link also records the GUEST texture pointer -- the stable
// identity SetRenderTarget resolves for the backend's RT/shadow keys.
struct EmuSwizzledSurfaceLink
{
    XTL::IDirect3DSurface8* pHostSurface;
    XTL::IDirect3DBaseTexture8* pHostTexture;
    DWORD dwLevel;
    void* pGuestTexture;
};

static std::vector<EmuSwizzledSurfaceLink> g_EmuSwizzledSurfaceLinks;

static void EmuLinkSwizzledSurface(XTL::IDirect3DSurface8* pHostSurface,
                                   XTL::IDirect3DBaseTexture8* pHostTexture,
                                   DWORD dwLevel,
                                   void* pGuestTexture = nullptr)
{
    if(pHostSurface == nullptr || pHostTexture == nullptr)
        return;
    for(auto& link : g_EmuSwizzledSurfaceLinks)
    {
        if(link.pHostSurface == pHostSurface)
        {
            link.pHostTexture = pHostTexture;
            link.dwLevel = dwLevel;
            link.pGuestTexture = pGuestTexture;
            return;
        }
    }
    g_EmuSwizzledSurfaceLinks.push_back(
        { pHostSurface, pHostTexture, dwLevel, pGuestTexture });
}

static const EmuSwizzledSurfaceLink* EmuFindSwizzledSurfaceLink(
    XTL::IDirect3DSurface8* pHostSurface)
{
    if(pHostSurface == nullptr)
        return nullptr;
    for(const auto& link : g_EmuSwizzledSurfaceLinks)
    {
        if(link.pHostSurface == pHostSurface)
            return &link;
    }
    return nullptr;
}

// CopyRects (or any host blit) into a texture's level surface updates the
// host texture behind the container's back. Invalidate the capture so the
// bind pull falls back to the authoritative host texture -- unless a served
// lock is outstanding, in which case the container stays authoritative.
static void EmuInvalidateTextureContainerForSurface(
    XTL::IDirect3DSurface8* pLevelHostSurface)
{
    const EmuSwizzledSurfaceLink* pLink =
        EmuFindSwizzledSurfaceLink(pLevelHostSurface);
    if(pLink == nullptr)
    {
        return;
    }
    if(EmuFindTextureContainerLockByHost(pLink->pHostTexture) != nullptr)
    {
        return;
    }
    EmuForgetTextureContainerState(pLink->pHostTexture);
}

// Unswizzle every dirty level in place, while the host lock that received the
// title's Morton-order write is still held.
// One-shot upload of the guest's Data-pointer Morton block into host level 0,
// for textures whose texels arrived through the resource Data pointer (native
// guest D3DX / XGraphics) rather than a LockRect wrapper.
static void EmuCommitGuestSwizzledData(EmuSwizzledTextureInfo* info)
{
    if(info->pGuestData == nullptr || info->bGuestCommitted || info->bLockPathUsed)
        return;

    auto* texture = static_cast<XTL::IDirect3DTexture8*>(info->pHostTexture);
    XTL::D3DLOCKED_RECT lock;
    __try
    {
        if(FAILED(texture->LockRect(0, &lock, NULL, 0)))
            return;

        RECT sourceRect = { 0, 0, 0, 0 };
        POINT destinationPoint = { 0, 0 };
        XTL::EmuXGUnswizzleRect(info->pGuestData, info->dwWidth, info->dwHeight, 1,
                                lock.pBits, lock.Pitch, sourceRect,
                                destinationPoint, info->dwBPP);
        texture->UnlockRect(0);
        info->bGuestCommitted = true;

        // Level 0's linear rows are the container's capture domain.
        EmuRefreshTextureContainerFromLock(info->pHostTexture, lock.pBits,
                                           lock.Pitch, info->dwWidth,
                                           info->dwHeight);

        if(EmuD3DTexTraceEnabled())
        {
            printf("TEX| commit guest-data host=0x%.08lX %lux%lu bpp=%lu data=0x%.08lX\n",
                   reinterpret_cast<unsigned long>(info->pHostTexture),
                   static_cast<unsigned long>(info->dwWidth),
                   static_cast<unsigned long>(info->dwHeight),
                   static_cast<unsigned long>(info->dwBPP),
                   reinterpret_cast<unsigned long>(info->pGuestData));
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static void EmuCommitSwizzledTexture(XTL::IDirect3DBaseTexture8* pBaseTexture)
{
    EmuSwizzledTextureInfo* info = EmuFindSwizzledTexture(pBaseTexture);
    if(info == nullptr)
        return;

    EmuCommitGuestSwizzledData(info);

    if(info->dwDirtyLevels == 0)
        return;

    auto* texture = static_cast<XTL::IDirect3DTexture8*>(info->pHostTexture);
    for(DWORD level = 0; level < EMU_SWIZZLED_TEXTURE_MAX_LEVELS; ++level)
    {
        if((info->dwDirtyLevels & (1u << level)) == 0)
            continue;

        const EmuSwizzledTextureLevelLock& lock = info->Levels[level];
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
                        static_cast<const BYTE*>(lock.pBits) +
                            static_cast<std::size_t>(y) * lock.Pitch,
                        rowBytes);
        }

        RECT sourceRect = { 0, 0, 0, 0 };
        POINT destinationPoint = { 0, 0 };
        XTL::EmuXGUnswizzleRect(morton.data(), width, height, 1, lock.pBits,
                                lock.Pitch, sourceRect, destinationPoint,
                                info->dwBPP);

        // Level 0's unswizzled rows are the container's capture domain.
        if(level == 0)
        {
            EmuRefreshTextureContainerFromLock(info->pHostTexture, lock.pBits,
                                               lock.Pitch, width, height);
        }

        if(EmuD3DTexTraceEnabled())
        {
            printf("TEX| commit swizzled host=0x%.08lX level=%lu %lux%lu pitch=%ld rowBytes=%lu bpp=%lu\n",
                   reinterpret_cast<unsigned long>(info->pHostTexture),
                   static_cast<unsigned long>(level),
                   static_cast<unsigned long>(width), static_cast<unsigned long>(height),
                   static_cast<long>(lock.Pitch), static_cast<unsigned long>(rowBytes),
                   static_cast<unsigned long>(info->dwBPP));

            char tempDir[MAX_PATH];
            char path[MAX_PATH];
            if(info->dwBPP == 4 && GetTempPathA(MAX_PATH, tempDir) != 0)
            {
                CreateDirectoryA((std::string(tempDir) + "cxbx_tex").c_str(), NULL);
                sprintf(path, "%scxbx_tex\\swz_%08lX_L%lu.bmp", tempDir,
                        reinterpret_cast<unsigned long>(info->pHostTexture),
                        static_cast<unsigned long>(level));
                EmuD3DWriteBmp(path, width, height, lock.Pitch, lock.pBits,
                               XTL::D3DFMT_A8R8G8B8);
            }
        }
    }

    info->dwDirtyLevels = 0;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateTexture2
// ******************************************************************
XTL::X_D3DResource* WINAPI XTL::EmuIDirect3DDevice8_CreateTexture2(
    UINT Width,
    UINT Height,
    UINT Depth,
    UINT Levels,
    DWORD Usage,
    D3DFORMAT Format,
    D3DRESOURCETYPE D3DResource)
{
    D3D_TRACE("CreateTexture2");

    // The 5849 header inlines CreateCubeTexture/CreateVolumeTexture down to
    // this entry, so the resource type must be honoured here: a cube
    // request creates the host cube (and its guest wrapper with
    // EmuCubeTexture8) rather than a plain 2D texture.
    if(D3DResource == XTL::D3DRTYPE_CUBETEXTURE)
    {
        X_D3DCubeTexture* pCube = NULL;
        if(SUCCEEDED(EmuIDirect3DDevice8_CreateCubeTexture(
               Width, Levels, Usage, Format, D3DPOOL_MANAGED, &pCube)) &&
           pCube != NULL)
        {
            return pCube;
        }
        return NULL;
    }
    if(D3DResource == XTL::D3DRTYPE_VOLUMETEXTURE)
    {
        // 5849 inlines CreateVolumeTexture here the same way.
        X_D3DVolumeTexture* pVolume = NULL;
        if(SUCCEEDED(EmuIDirect3DDevice8_CreateVolumeTexture(
               Width, Height, Depth, Levels, Usage, Format, D3DPOOL_MANAGED,
               &pVolume)) &&
           pVolume != NULL)
        {
            return pVolume;
        }
        return NULL;
    }

    X_D3DTexture* pTexture;

    EmuIDirect3DDevice8_CreateTexture(Width, Height, Levels, Usage, Format, D3DPOOL_MANAGED, &pTexture);

    return pTexture;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateTexture
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateTexture(
    UINT Width,
    UINT Height,
    UINT Levels,
    DWORD Usage,
    D3DFORMAT Format,
    D3DPOOL Pool,
    X_D3DTexture** ppTexture)
{
    D3D_TRACE("CreateTexture");
    EmuSwapFS(); // Win2k/XP FS

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

    // On Xbox any surface can back a render target or depth buffer -- there is
    // no separate render-target resource class. A title that renders to a
    // texture (Turok Evolution's world passes create 512x512 RENDERTARGET
    // colour and DEPTHSTENCIL depth textures) hands that intent in through
    // Usage. Historically this wrapper discarded it, forced D3DPOOL_MANAGED,
    // and rewrote depth formats to X8R8G8B8; the resulting surface then failed
    // SetRenderTarget's host usage check and the world pass silently painted
    // the back buffer instead. Honour the usage for these resources: create
    // them with the matching host usage in D3DPOOL_DEFAULT and keep the real
    // depth format, so GetSurfaceLevel hands SetRenderTarget a surface the host
    // runtime accepts. Ordinary textures are untouched.
    // Runtime kill-switch for A/B diagnosis: CXBX_NO_RT_TEXTURES falls back to
    // the historical managed/usage-0/depth->X8R8G8B8 behavior so a regression
    // can be bisected without a rebuild.
    static int s_RenderResourceGate = -1;
    if(s_RenderResourceGate < 0)
        s_RenderResourceGate = EmuD3DEnvironmentEnabled("CXBX_NO_RT_TEXTURES") ? 0 : 1;
    const bool bWantRenderTarget = s_RenderResourceGate && (Usage & D3DUSAGE_RENDERTARGET) != 0;
    const bool bWantDepthStencil = s_RenderResourceGate && (Usage & D3DUSAGE_DEPTHSTENCIL) != 0;
    const bool bRenderResource = bWantRenderTarget || bWantDepthStencil;

    // TODO: HACK: Devices that don't support this should somehow emulate it!
    if(PCFormat == D3DFMT_D16 && !bRenderResource)
    {
        printf("*Warning* D3DFMT_16 is an unsupported texture format!\n");
        PCFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCFormat == D3DFMT_P8)
    {
        printf("*Warning* D3DFMT_P8 is an unsupported texture format!\n");
        PCFormat = D3DFMT_X8R8G8B8;
    }
    else if(PCFormat == D3DFMT_D24S8 && !bRenderResource)
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
        EmuRememberOwnedGuestResource(*ppTexture);

        const DWORD HostUsage = bWantDepthStencil   ? D3DUSAGE_DEPTHSTENCIL
                                : bWantRenderTarget ? D3DUSAGE_RENDERTARGET
                                                    : 0;
        const D3DPOOL HostPool = bRenderResource ? D3DPOOL_DEFAULT
                                                 : D3DPOOL_MANAGED;

        // ******************************************************************
        // * redirect to windows d3d
        // ******************************************************************
        __try
        {
            hRet = g_pD3DDevice8->CreateTexture(
                Width, Height, Levels,
                HostUsage,
                PCFormat, HostPool, &((*ppTexture)->EmuTexture8));
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            hRet = D3DERR_INVALIDCALL;
        }

        // A host device that cannot supply this depth/render-target format
        // falls back to the historical managed texture (blank, but present)
        // rather than leaving the title with an unbacked resource. The depth
        // format degrades to X8R8G8B8 as before; a colour render target keeps
        // its format and only loses the DEFAULT-pool render-target usage.
        if(bRenderResource && (FAILED(hRet) || (*ppTexture)->EmuTexture8 == NULL))
        {
            EmuWarning("CreateTexture: host rejected %s texture %ux%u fmt=%d (0x%.08X); "
                       "falling back to managed",
                       bWantDepthStencil ? "depth-stencil" : "render-target",
                       Width, Height, PCFormat, hRet);

            const D3DFORMAT FallbackFormat = bWantDepthStencil ? D3DFMT_X8R8G8B8 : PCFormat;
            (*ppTexture)->EmuTexture8 = NULL;
            __try
            {
                hRet = g_pD3DDevice8->CreateTexture(
                    Width, Height, Levels,
                    0,
                    FallbackFormat, D3DPOOL_MANAGED, &((*ppTexture)->EmuTexture8));
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                hRet = D3DERR_INVALIDCALL;
            }
        }

        if(FAILED(hRet))
            printf("*Warning* CreateTexture FAILED\n");

        DWORD dwSwizzledBPP = 0;
        const bool bSwizzled =
            !bRenderResource &&
            SUCCEEDED(hRet) && (*ppTexture)->EmuTexture8 != NULL &&
            EmuXboxFormatIsSwizzled(static_cast<DWORD>(Format), &dwSwizzledBPP);
        if(bSwizzled)
        {
            // Build a guest-visible Xbox texture header. A title's native
            // D3DX/XGraphics upload reads the dimensions from Format and
            // writes its Morton-order texels through Data without any
            // LockRect; leaving these fields zero silently discards every
            // skin/menu image such a title loads (NestopiaX 1.3).
            DWORD dwLog2W = 0, dwLog2H = 0;
            while((1u << dwLog2W) < Width)
                dwLog2W++;
            while((1u << dwLog2H) < Height)
                dwLog2H++;

            DWORD dwLevelCount = Levels;
            if(dwLevelCount == 0)
            {
                dwLevelCount = 1;
                while((Width >> dwLevelCount) > 0 || (Height >> dwLevelCount) > 0)
                    dwLevelCount++;
            }

            DWORD dwGuestSize = 0;
            for(DWORD level = 0; level < dwLevelCount; ++level)
            {
                const DWORD w = max(1u, Width >> level);
                const DWORD h = max(1u, Height >> level);
                dwGuestSize += w * h * dwSwizzledBPP;
            }

            BYTE* pGuestData = static_cast<BYTE*>(_aligned_malloc(dwGuestSize, 128));
            if(pGuestData != NULL)
                ZeroMemory(pGuestData, dwGuestSize);

            (*ppTexture)->Common = X_D3DCOMMON_TYPE_TEXTURE | X_D3DCOMMON_D3DCREATED | 1;
            (*ppTexture)->Data = reinterpret_cast<DWORD>(pGuestData);
            (*ppTexture)->Format =
                X_D3DFORMAT_DMACHANNEL_A | (2u << X_D3DFORMAT_DIMENSION_SHIFT) |
                ((static_cast<DWORD>(Format) & 0xFF) << X_D3DFORMAT_FORMAT_SHIFT) |
                (dwLevelCount << X_D3DFORMAT_MIPMAP_SHIFT) |
                (dwLog2W << X_D3DFORMAT_USIZE_SHIFT) |
                (dwLog2H << X_D3DFORMAT_VSIZE_SHIFT);
            (*ppTexture)->Size = 0; // power-of-2 swizzled: dims live in Format

            g_EmuSwizzledTextures.push_back(
                { (*ppTexture)->EmuTexture8, dwSwizzledBPP, 0, {}, pGuestData, dwGuestSize, Width, Height, false, false });
        }

        DWORD linearBPP = 0;
        if(!bRenderResource && SUCCEEDED(hRet) && (*ppTexture)->EmuTexture8 != NULL &&
           EmuXboxFormatIsLinear(static_cast<DWORD>(Format), &linearBPP))
        {
            EmuTrackLinearTexture((*ppTexture)->EmuTexture8, nullptr,
                                  Width * linearBPP, Width, Height, linearBPP);
        }

        if(SUCCEEDED(hRet))
            EmuRememberHostResource((*ppTexture)->EmuTexture8);

        if(EmuD3DTexTraceEnabled())
        {
            printf("TEX| create host=0x%.08lX %ux%u levels=%u xfmt=0x%02lX swizzled=%d status=0x%.08lX\n",
                   reinterpret_cast<unsigned long>(
                       SUCCEEDED(hRet) ? (*ppTexture)->EmuTexture8 : NULL),
                   Width, Height, Levels, static_cast<unsigned long>(Format),
                   bSwizzled ? 1 : 0, static_cast<unsigned long>(hRet));
        }
    }
    else
    {
        // If YUY2 is not supported in hardware, we'll actually mark this as a special fake texture (set highest bit)
        *ppTexture = EmuCreateYuy2Texture(Width, Height);
        hRet = (*ppTexture != NULL) ? D3D_OK : E_OUTOFMEMORY;
    }

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateVolumeTexture
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateVolumeTexture(
    UINT Width,
    UINT Height,
    UINT Depth,
    UINT Levels,
    DWORD Usage,
    D3DFORMAT Format,
    D3DPOOL Pool,
    X_D3DVolumeTexture** ppVolumeTexture)
{
    D3D_TRACE("CreateVolumeTexture");
    EmuSwapFS(); // Win2k/XP FS

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
        EmuRememberOwnedGuestResource(*ppVolumeTexture);

        // ******************************************************************
        // * redirect to windows d3d
        // ******************************************************************
        hRet = g_pD3DDevice8->CreateVolumeTexture(
            Width, Height, Depth, Levels,
            0, // TODO: Xbox Allows a border to be drawn (maybe hack this in software ;[)
            PCFormat, D3DPOOL_MANAGED, &((*ppVolumeTexture)->EmuVolumeTexture8));

        if(SUCCEEDED(hRet) && (*ppVolumeTexture)->EmuVolumeTexture8 != NULL)
        {
            // SetTexture resolves the host object through the base-texture
            // member; without it a bound volume reads as an unbound stage.
            (*ppVolumeTexture)->EmuBaseTexture8 =
                (*ppVolumeTexture)->EmuVolumeTexture8;
        }

        if(FAILED(hRet))
            printf("*Warning* CreateVolumeTexture FAILED\n");
    }
    else
    {
        // If YUY2 is not supported in hardware, we'll actually mark this as a special fake texture (set highest bit)
        *ppVolumeTexture = (X_D3DVolumeTexture*)((uint32)(new uint08[g_dwOverlayW * g_dwOverlayH * 2]) | 0x80000000);

        hRet = D3D_OK;
    }

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateCubeTexture
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateCubeTexture(
    UINT EdgeLength,
    UINT Levels,
    DWORD Usage,
    D3DFORMAT Format,
    D3DPOOL Pool,
    X_D3DCubeTexture** ppCubeTexture)
{
    D3D_TRACE("CreateCubeTexture");
    EmuSwapFS(); // Win2k/XP FS

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
    EmuRememberOwnedGuestResource(*ppCubeTexture);

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    HRESULT hRet = g_pD3DDevice8->CreateCubeTexture(
        EdgeLength, Levels,
        0, // TODO: Xbox Allows a border to be drawn (maybe hack this in software ;[)
        PCFormat, D3DPOOL_MANAGED, &((*ppCubeTexture)->EmuCubeTexture8));

    if(SUCCEEDED(hRet) && (*ppCubeTexture)->EmuCubeTexture8 != NULL)
    {
        // SetTexture resolves the host object through the base-texture
        // member; without it a bound cube reads as an unbound stage.
        (*ppCubeTexture)->EmuBaseTexture8 =
            (*ppCubeTexture)->EmuCubeTexture8;
    }

    if(FAILED(hRet))
        printf("*Warning* CreateCubeTexture FAILED\n");

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateIndexBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateIndexBuffer(
    UINT Length,
    DWORD Usage,
    D3DFORMAT Format,
    D3DPOOL Pool,
    X_D3DIndexBuffer** ppIndexBuffer)
{
    D3D_TRACE("CreateIndexBuffer");
    EmuSwapFS(); // Win2k/XP FS

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

    *ppIndexBuffer = new(std::nothrow) X_D3DIndexBuffer();
    if(*ppIndexBuffer == NULL)
    {
        EmuSwapFS(); // Xbox FS
        return E_OUTOFMEMORY;
    }

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    HRESULT hRet = g_pD3DDevice8->CreateIndexBuffer(
        Length, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &((*ppIndexBuffer)->EmuIndexBuffer8));

    if(FAILED(hRet))
    {
        printf("*Warning* CreateIndexBuffer FAILED\n");
        delete *ppIndexBuffer;
        *ppIndexBuffer = NULL;
    }
    else
    {
        BYTE* guestData = new(std::nothrow) BYTE[Length];
        if(guestData == NULL ||
           !EmuTrackCreatedIndexBuffer(*ppIndexBuffer, guestData, Length))
        {
            delete[] guestData;
            (*ppIndexBuffer)->EmuIndexBuffer8->Release();
            delete *ppIndexBuffer;
            *ppIndexBuffer = NULL;
            hRet = E_OUTOFMEMORY;
        }
        else
        {
            memset(guestData, 0, Length);
            (*ppIndexBuffer)->Common = X_D3DCOMMON_TYPE_INDEXBUFFER |
                                       X_D3DCOMMON_D3DCREATED | 1;
            (*ppIndexBuffer)->Data = reinterpret_cast<DWORD>(guestData);
        }
    }

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateIndexBuffer2
// ******************************************************************
XTL::X_D3DIndexBuffer* WINAPI XTL::EmuIDirect3DDevice8_CreateIndexBuffer2(
    UINT Length)
{
    D3D_TRACE("CreateIndexBuffer2");

    X_D3DIndexBuffer* IndexBuffer = NULL;
    const HRESULT Result = EmuIDirect3DDevice8_CreateIndexBuffer(
        Length, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &IndexBuffer);
    if(FAILED(Result))
    {
        return NULL;
    }

    return IndexBuffer;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetIndices
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetIndices(
    X_D3DIndexBuffer* pIndexData,
    UINT BaseVertexIndex)
{
    EmuSwapFS(); // Win2k/XP FS
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

        EmuCreatedIndexBuffer* created = EmuFindCreatedIndexBuffer(pIndexData);
        if(created != nullptr)
        {
            BYTE* hostData = nullptr;
            HRESULT uploadResult = pIndexData->EmuIndexBuffer8->Lock(
                0, created->byteSize, &hostData, 0);
            if(SUCCEEDED(uploadResult))
            {
                if(hostData != nullptr)
                {
                    memcpy(hostData, created->guestData, created->byteSize);
                }
                pIndexData->EmuIndexBuffer8->Unlock();
            }
            if(FAILED(uploadResult) || hostData == nullptr)
            {
                EmuWarning("SetIndices could not upload a created index buffer (0x%.08X)",
                           uploadResult);
            }
        }
        pIndexBuffer = pIndexData->EmuIndexBuffer8;
    }
    XTL::VshCpuDeviceState::SetIndexBuffer({ pIndexData, BaseVertexIndex });

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
static XTL::IDirect3DTexture8* g_pYuvConvertTexture = NULL;
static DWORD g_dwYuvConvertW = 0, g_dwYuvConvertH = 0;
static const EmuYuy2TextureInfo* g_pYuvConvertedSource = NULL;

static XTL::IDirect3DTexture8* EmuConvertYuy2Texture(EmuYuy2TextureInfo* pInfo)
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
        g_dwYuvConvertW = w;
        g_dwYuvConvertH = h;
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

    // Opt-in (CXBX_YUV_DUMP=N): dump the first N converted frames (BMP) plus
    // their raw YUY2 source blocks to %TEMP% so video corruption can be
    // attributed to decode (bad source) vs conversion/draw (bad output).
    static int s_YuvDumpRemaining = -1;
    static DWORD s_YuvDumpIndex = 0;
    if(s_YuvDumpRemaining < 0)
    {
        const char* v = getenv("CXBX_YUV_DUMP");
        s_YuvDumpRemaining = (v == NULL) ? 0 : max(1, atoi(v));
    }
    if(converted && s_YuvDumpRemaining > 0)
    {
        s_YuvDumpRemaining--;
        const DWORD index = ++s_YuvDumpIndex;
        char tempDir[MAX_PATH];
        char path[MAX_PATH];
        if(GetTempPathA(MAX_PATH, tempDir) != 0)
        {
            sprintf(path, "%scxbx_yuv_%03lu.bmp", tempDir, (unsigned long)index);
            EmuD3DWriteBmp(path, w, h, lr.Pitch, lr.pBits, XTL::D3DFMT_A8R8G8B8);

            sprintf(path, "%scxbx_yuv_%03lu.yuy2", tempDir, (unsigned long)index);
            HANDLE rawFile = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                         FILE_ATTRIBUTE_NORMAL, NULL);
            if(rawFile != INVALID_HANDLE_VALUE)
            {
                DWORD rawWritten = 0;
                WriteFile(rawFile, pInfo->pPixels, pInfo->DataSize, &rawWritten, NULL);
                CloseHandle(rawFile);
            }
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

static XTL::IDirect3DBaseTexture8* g_EmuBoundHostTextures[EmuXboxTextureStageCount] = {};

static void EmuFlushBoundHostTextureLocks()
{
    for(DWORD stage = 0; stage < EmuXboxTextureStageCount; ++stage)
    {
        EmuFlushHostTextureLocks(g_EmuBoundHostTextures[stage]);
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

static void EmuDumpBoundTextureAtDraw()
{
    static int dumpEnabled = -1;
    static DWORD targetFrame = 0;
    static DWORD targetDraw = 0;
    static bool dumpWholeFrame = false;
    static bool attempted = false;
    static std::vector<XTL::IDirect3DBaseTexture8*> dumpedTextures;
    if(dumpEnabled < 0)
    {
        char value[64] = {};
        char* separator = nullptr;
        char* end = nullptr;
        if(EmuD3DReadEnvironment(
               "CXBX_D3D_DUMP_BOUND_TEXTURE", value, sizeof(value)))
        {
            targetFrame = strtoul(value, &separator, 0);
            if(separator != nullptr && *separator == ':')
            {
                if(separator[1] == '*' && separator[2] == '\0')
                {
                    dumpWholeFrame = true;
                    dumpEnabled = 1;
                }
                else
                {
                    targetDraw = strtoul(separator + 1, &end, 0);
                    dumpEnabled =
                        end != separator + 1 && *end == '\0' ? 1 : 0;
                }
            }
        }
        if(dumpEnabled < 0)
        {
            dumpEnabled = 0;
        }
    }

    if(dumpEnabled != 1 || g_D3DDebugFrame != targetFrame ||
       (!dumpWholeFrame &&
        (attempted || g_D3DDebugDrawIndex != targetDraw)))
    {
        return;
    }

    XTL::IDirect3DBaseTexture8* baseTexture = g_EmuBoundHostTextures[0];
    if(baseTexture == nullptr || baseTexture->GetType() != XTL::D3DRTYPE_TEXTURE)
    {
        attempted = !dumpWholeFrame;
        return;
    }
    if(dumpWholeFrame)
    {
        if(std::find(
               dumpedTextures.begin(), dumpedTextures.end(), baseTexture) !=
           dumpedTextures.end())
        {
            return;
        }
        dumpedTextures.push_back(baseTexture);
    }
    else
    {
        attempted = true;
    }

    auto* texture = static_cast<XTL::IDirect3DTexture8*>(baseTexture);
    XTL::D3DSURFACE_DESC description = {};
    XTL::D3DLOCKED_RECT locked = {};
    bool lockedTexture = false;
    bool dumped = false;
    __try
    {
        if(SUCCEEDED(texture->GetLevelDesc(0, &description)) &&
           SUCCEEDED(texture->LockRect(0, &locked, nullptr, D3DLOCK_READONLY)))
        {
            lockedTexture = true;
            char fileName[64];
            snprintf(fileName, sizeof(fileName), "f%05lu_d%04lu.bmp",
                     static_cast<unsigned long>(targetFrame),
                     static_cast<unsigned long>(g_D3DDebugDrawIndex));
            char path[MAX_PATH * 2];
            EmuD3DDebugDumpPath(
                path, sizeof(path), "cxbx_bound_tex", fileName);
            dumped = EmuD3DWriteBmp(
                path, description.Width, description.Height, locked.Pitch,
                locked.pBits, description.Format);
            printf("TEX| bound texture %s frame=%lu draw=%lu host=0x%.08lX "
                   "%lux%lu format=0x%08lX path=%s\n",
                   dumped ? "dumped" : "dump failed",
                   static_cast<unsigned long>(targetFrame),
                   static_cast<unsigned long>(g_D3DDebugDrawIndex),
                   reinterpret_cast<unsigned long>(baseTexture),
                   static_cast<unsigned long>(description.Width),
                   static_cast<unsigned long>(description.Height),
                   static_cast<unsigned long>(description.Format), path);
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
        printf("TEX| bound texture dump fault frame=%lu draw=%lu\n",
               static_cast<unsigned long>(targetFrame),
               static_cast<unsigned long>(g_D3DDebugDrawIndex));
    }
}

static void EmuTraceBoundTextureAtDraw()
{
    static int traceEnabled = -1;
    static DWORD traceCount = 0;
    static bool stateTraced = false;
    if(traceEnabled < 0)
    {
        traceEnabled = EmuD3DEnvironmentEnabled("CXBX_TEX_TRACE") ? 1 : 0;
    }
    if(traceEnabled != 1 || traceCount >= 16)
    {
        return;
    }

    XTL::IDirect3DBaseTexture8* texture = g_EmuBoundHostTextures[0];
    const bool hasData = EmuHostTextureHasData(texture);
    printf("TEX| draw-bind frame=%lu stage=0 host=0x%.08lX has_data=%d\n",
           static_cast<unsigned long>(g_D3DDebugFrame),
           reinterpret_cast<unsigned long>(texture),
           hasData ? 1 : 0);
    ++traceCount;

    if(!hasData || stateTraced || g_pD3DDevice8 == nullptr)
    {
        return;
    }
    stateTraced = true;

    DWORD lighting = 0;
    DWORD cullMode = 0;
    DWORD zEnable = 0;
    DWORD colorOp = 0;
    DWORD colorArg1 = 0;
    DWORD colorArg2 = 0;
    DWORD alphaOp = 0;
    DWORD texCoordIndex = 0;
    XTL::D3DVIEWPORT8 viewport = {};
    XTL::D3DMATRIX world = {};
    XTL::D3DMATRIX view = {};
    XTL::D3DMATRIX projection = {};
    g_pD3DDevice8->GetRenderState(XTL::D3DRS_LIGHTING, &lighting);
    g_pD3DDevice8->GetRenderState(XTL::D3DRS_CULLMODE, &cullMode);
    g_pD3DDevice8->GetRenderState(XTL::D3DRS_ZENABLE, &zEnable);
    g_pD3DDevice8->GetTextureStageState(0, XTL::D3DTSS_COLOROP, &colorOp);
    g_pD3DDevice8->GetTextureStageState(0, XTL::D3DTSS_COLORARG1, &colorArg1);
    g_pD3DDevice8->GetTextureStageState(0, XTL::D3DTSS_COLORARG2, &colorArg2);
    g_pD3DDevice8->GetTextureStageState(0, XTL::D3DTSS_ALPHAOP, &alphaOp);
    g_pD3DDevice8->GetTextureStageState(0, XTL::D3DTSS_TEXCOORDINDEX,
                                        &texCoordIndex);
    g_pD3DDevice8->GetViewport(&viewport);
    g_pD3DDevice8->GetTransform(
        static_cast<XTL::D3DTRANSFORMSTATETYPE>(256), &world);
    g_pD3DDevice8->GetTransform(XTL::D3DTS_VIEW, &view);
    g_pD3DDevice8->GetTransform(XTL::D3DTS_PROJECTION, &projection);
    printf("TEX| draw-state lighting=%lu cull=%lu z=%lu color=%lu/%lu/%lu "
           "alpha=%lu texcoord=%lu viewport=%lu,%lu %lux%lu depth=%g:%g\n",
           static_cast<unsigned long>(lighting),
           static_cast<unsigned long>(cullMode),
           static_cast<unsigned long>(zEnable),
           static_cast<unsigned long>(colorOp),
           static_cast<unsigned long>(colorArg1),
           static_cast<unsigned long>(colorArg2),
           static_cast<unsigned long>(alphaOp),
           static_cast<unsigned long>(texCoordIndex),
           static_cast<unsigned long>(viewport.X),
           static_cast<unsigned long>(viewport.Y),
           static_cast<unsigned long>(viewport.Width),
           static_cast<unsigned long>(viewport.Height), viewport.MinZ,
           viewport.MaxZ);
    printf("TEX| draw-transform world_diag=%g,%g,%g,%g view_t=%g,%g,%g "
           "projection_diag=%g,%g,%g,%g\n",
           world.m[0][0], world.m[1][1], world.m[2][2], world.m[3][3],
           view.m[3][0], view.m[3][1], view.m[3][2], projection.m[0][0],
           projection.m[1][1], projection.m[2][2], projection.m[3][3]);

    XTL::IDirect3DVertexBuffer8* vertexBuffer = nullptr;
    UINT stride = 0;
    if(SUCCEEDED(g_pD3DDevice8->GetStreamSource(0, &vertexBuffer, &stride)) &&
       vertexBuffer != nullptr)
    {
        BYTE* bytes = nullptr;
        if(stride >= 24 && SUCCEEDED(vertexBuffer->Lock(0, stride * 4, &bytes, D3DLOCK_READONLY)) &&
           bytes != nullptr)
        {
            for(UINT vertex = 0; vertex < 4; ++vertex)
            {
                const BYTE* source = bytes + vertex * stride;
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                float u = 0.0f;
                float v = 0.0f;
                DWORD color = 0;
                memcpy(&x, source, sizeof(x));
                memcpy(&y, source + 4, sizeof(y));
                memcpy(&z, source + 8, sizeof(z));
                memcpy(&color, source + 12, sizeof(color));
                memcpy(&u, source + 16, sizeof(u));
                memcpy(&v, source + 20, sizeof(v));
                printf("TEX| draw-vertex index=%u xyz=%g,%g,%g color=0x%.08lX "
                       "uv=%g,%g stride=%u\n",
                       vertex, x, y, z, static_cast<unsigned long>(color), u,
                       v, stride);
            }
            vertexBuffer->Unlock();
        }
        vertexBuffer->Release();
    }
}

// Native-Vulkan migration P3: mirror the bound texture into the backend.
// The HLE has already converted formats and expanded palettes into the host
// d3d8 texture, so the backend receives host bytes and host formats only.
// Data is re-pulled on every real bind; redundant binds are suppressed by
// the host-forward cache upstream.
// Slice C: the renderer cache is keyed by the GUEST resource pointer
// (pGuestKey), not the host texture pointer -- guest identity is stable for
// the resource's lifetime, so a host pointer recycled by a new texture can
// never alias a live cache entry, and state-block replays rebind the same
// key the bind recorded.
// P8 YUY2: converts a movie frame into a shared hash-cached BGRA buffer
// (no host texture, no host lock -- the d3d8 flavor keeps the host-texture
// conversion in EmuConvertYuy2Texture). The frame hash suppresses
// reconversion of unchanged frames. The buffer is shared by all consumers
// (stage binds, the overlay compositor): each consumer copies out
// synchronously, so no outstanding references survive the next conversion.
static const BYTE* EmuVulkanConvertYuy2Bgra(EmuYuy2TextureInfo* pInfo,
                                            DWORD* outW, DWORD* outH)
{
    DWORD w = pInfo->Width & ~1u, h = pInfo->Height;
    if(pInfo->pPixels == nullptr || w == 0 || h == 0)
    {
        return nullptr;
    }

    DWORD hash = 2166136261u;
    for(DWORD offset = 0; offset < pInfo->DataSize; ++offset)
    {
        hash = (hash ^ pInfo->pPixels[offset]) * 16777619u;
    }

    static std::vector<BYTE> s_Bgra;
    static EmuYuy2TextureInfo* s_CacheSource = nullptr;
    static DWORD s_CacheHash = 0;
    static DWORD s_CacheW = 0, s_CacheH = 0;
    if(!(s_CacheSource == pInfo && s_CacheHash == hash &&
         s_CacheW == w && s_CacheH == h))
    {
        s_Bgra.resize(static_cast<std::size_t>(w) * h * 4);
        if(!CxbxVideo::ConvertYuy2ToBgra(
               pInfo->pPixels, pInfo->Pitch, s_Bgra.data(), w * 4, w, h))
        {
            return nullptr;
        }
        s_CacheSource = pInfo;
        s_CacheHash = hash;
        s_CacheW = w;
        s_CacheH = h;
    }
    if(outW != nullptr)
    {
        *outW = w;
    }
    if(outH != nullptr)
    {
        *outH = h;
    }
    return s_Bgra.data();
}

static bool EmuVulkanStageYuy2Upload(DWORD Stage, EmuYuy2TextureInfo* pInfo,
                                     void* pGuestKey)
{
    if(pGuestKey == nullptr)
    {
        return false;
    }
    DWORD w = 0, h = 0;
    const BYTE* bgra = EmuVulkanConvertYuy2Bgra(pInfo, &w, &h);
    if(bgra == nullptr)
    {
        return false;
    }
    cxbx::d3d8::HostBackendSetTexture(Stage, pGuestKey, bgra, w * 4, w, h,
                                      21);
    return true;
}

static void EmuVulkanStageTextureUpload(DWORD Stage,
                                        XTL::IDirect3DBaseTexture8* pBaseTexture8,
                                        void* pGuestKey)
{
    if(!cxbx::d3d8::HostBackendRenders())
    {
        return;
    }
    // P8 YUY2: the guest key identifies the movie frame (fake high-bit
    // handle or registered guest resource); convert straight into the
    // backend upload without a host texture.
    if(pGuestKey != nullptr)
    {
        if(EmuYuy2TextureInfo* pYuy2 =
               EmuResolveYuy2Texture(reinterpret_cast<XTL::X_D3DResource*>(
                   pGuestKey)))
        {
            if(!EmuVulkanStageYuy2Upload(Stage, pYuy2, pGuestKey))
            {
                cxbx::d3d8::HostBackendSetTexture(Stage, nullptr, nullptr, 0,
                                                  0, 0, 0);
            }
            return;
        }
    }
    if(pBaseTexture8 != NULL &&
       pBaseTexture8->GetType() == XTL::D3DRTYPE_CUBETEXTURE)
    {
        // P8 cube mirroring: commit any pending Morton-order face writes
        // into the host cube, then pull all six level-0 faces face-major
        // into the backend's cube cache (4-byte and DXT host formats; DXT
        // faces stay block-compressed here and decode in the renderer).
        // Volumes mirror in a later phase.
        auto* pCube8 = static_cast<XTL::IDirect3DCubeTexture8*>(pBaseTexture8);
        EmuCommitSwizzledTexture(pBaseTexture8);
        XTL::D3DSURFACE_DESC faceDesc = {};
        if(FAILED(pCube8->GetLevelDesc(0, &faceDesc)) ||
           faceDesc.Width == 0 || faceDesc.Height == 0 ||
           faceDesc.Width != faceDesc.Height)
        {
            static bool s_CubeUnsupportedLogged = false;
            if(!s_CubeUnsupportedLogged)
            {
                s_CubeUnsupportedLogged = true;
                printf("VULKAN| cube texture (host format %u) not mirrored "
                       "yet; stage samples white\n",
                       static_cast<unsigned>(faceDesc.Format));
            }
            cxbx::d3d8::HostBackendSetTexture(Stage, nullptr, nullptr, 0, 0,
                                              0, 0);
            return;
        }
        const UINT faceSize = faceDesc.Width;
        const UINT hostFormat = static_cast<UINT>(faceDesc.Format);
        const bool dxtFace =
            hostFormat == 0x31545844 || hostFormat == 0x32545844 ||
            hostFormat == 0x33545844 || hostFormat == 0x34545844 ||
            hostFormat == 0x35545844;
        if(!dxtFace && hostFormat != 21 && hostFormat != 22)
        {
            static bool s_CubeFmtLogged = false;
            if(!s_CubeFmtLogged)
            {
                s_CubeFmtLogged = true;
                printf("VULKAN| cube texture (host format %u) not mirrored "
                       "yet; stage samples white\n",
                       static_cast<unsigned>(hostFormat));
            }
            cxbx::d3d8::HostBackendSetTexture(Stage, nullptr, nullptr, 0, 0,
                                              0, 0);
            return;
        }
        const UINT blockBytes =
            !dxtFace ? 0 : (hostFormat == 0x31545844 ? 8u : 16u);
        const UINT rowsPerFace = dxtFace ? faceSize / 4 : faceSize;
        const UINT tightRowBytes =
            dxtFace ? (faceSize / 4) * blockBytes : faceSize * 4;
        std::vector<BYTE> faces(
            static_cast<std::size_t>(tightRowBytes) * rowsPerFace * 6);
        bool facesOk = true;
        for(DWORD face = 0; face < 6 && facesOk; ++face)
        {
            XTL::D3DLOCKED_RECT locked = {};
            if(FAILED(pCube8->LockRect(
                   static_cast<XTL::D3DCUBEMAP_FACES>(face), 0, &locked,
                   NULL, D3DLOCK_READONLY)))
            {
                facesOk = false;
                break;
            }
            for(UINT y = 0; y < rowsPerFace; ++y)
            {
                memcpy(faces.data() +
                           (static_cast<std::size_t>(face) * rowsPerFace + y) *
                               tightRowBytes,
                       static_cast<const BYTE*>(locked.pBits) +
                           static_cast<std::size_t>(y) * locked.Pitch,
                       tightRowBytes);
            }
            pCube8->UnlockRect(static_cast<XTL::D3DCUBEMAP_FACES>(face), 0);
        }
        if(!facesOk ||
           !cxbx::d3d8::HostBackendSetCubeTexture(
               Stage, pGuestKey, faces.data(), tightRowBytes, faceSize,
               hostFormat))
        {
            cxbx::d3d8::HostBackendSetTexture(Stage, nullptr, nullptr, 0, 0,
                                              0, 0);
        }
        return;
    }
    if(pBaseTexture8 != NULL &&
       pBaseTexture8->GetType() == XTL::D3DRTYPE_VOLUMETEXTURE)
    {
        // P8 volume mirroring: pull level-0 slices slice-major into the
        // backend's 3D cache (4-byte host formats only).
        auto* pVolume8 =
            static_cast<XTL::IDirect3DVolumeTexture8*>(pBaseTexture8);
        XTL::D3DVOLUME_DESC volumeDesc = {};
        if(FAILED(pVolume8->GetLevelDesc(0, &volumeDesc)) ||
           volumeDesc.Width == 0 || volumeDesc.Height == 0 ||
           volumeDesc.Depth == 0 ||
           (volumeDesc.Format != 21 && volumeDesc.Format != 22))
        {
            static bool s_VolumeUnsupportedLogged = false;
            if(!s_VolumeUnsupportedLogged)
            {
                s_VolumeUnsupportedLogged = true;
                printf("VULKAN| volume texture (host format %u) not mirrored "
                       "yet; stage samples white\n",
                       static_cast<unsigned>(volumeDesc.Format));
            }
            cxbx::d3d8::HostBackendSetTexture(Stage, nullptr, nullptr, 0, 0,
                                              0, 0);
            return;
        }
        std::vector<BYTE> slices(
            static_cast<std::size_t>(volumeDesc.Width) * volumeDesc.Height *
            4 * volumeDesc.Depth);
        XTL::D3DLOCKED_BOX lockedBox = {};
        if(FAILED(pVolume8->LockBox(0, &lockedBox, NULL,
                                    D3DLOCK_READONLY)))
        {
            cxbx::d3d8::HostBackendSetTexture(Stage, nullptr, nullptr, 0, 0,
                                              0, 0);
            return;
        }
        const UINT rowBytes = volumeDesc.Width * 4;
        for(UINT slice = 0; slice < volumeDesc.Depth; ++slice)
        {
            for(UINT y = 0; y < volumeDesc.Height; ++y)
            {
                memcpy(slices.data() +
                           (static_cast<std::size_t>(slice) *
                                volumeDesc.Height +
                            y) *
                               rowBytes,
                       static_cast<const BYTE*>(lockedBox.pBits) +
                           static_cast<std::size_t>(slice) * lockedBox.SlicePitch +
                           static_cast<std::size_t>(y) * lockedBox.RowPitch,
                       rowBytes);
            }
        }
        pVolume8->UnlockBox(0);
        if(!cxbx::d3d8::HostBackendSetVolumeTexture(
               Stage, pGuestKey, slices.data(), rowBytes,
               volumeDesc.Width, volumeDesc.Height, volumeDesc.Depth,
               static_cast<unsigned>(volumeDesc.Format)))
        {
            cxbx::d3d8::HostBackendSetTexture(Stage, nullptr, nullptr, 0, 0,
                                              0, 0);
        }
        return;
    }
    if(pBaseTexture8 == NULL ||
       pBaseTexture8->GetType() != XTL::D3DRTYPE_TEXTURE)
    {
        // Volumes mirror in a later phase; unbound stages sample the
        // backend's white dummy.
        cxbx::d3d8::HostBackendSetTexture(Stage, nullptr, nullptr, 0, 0, 0, 0);
        return;
    }
    XTL::IDirect3DTexture8* pTexture8 =
        static_cast<XTL::IDirect3DTexture8*>(pBaseTexture8);
    XTL::D3DSURFACE_DESC description = {};
    if(FAILED(pTexture8->GetLevelDesc(0, &description)) ||
       description.Width == 0 || description.Height == 0)
    {
        cxbx::d3d8::HostBackendSetTexture(Stage, nullptr, nullptr, 0, 0, 0, 0);
        return;
    }

    static int mirrorTraceEnabled = -1;
    if(mirrorTraceEnabled < 0)
    {
        mirrorTraceEnabled = EmuD3DEnvironmentEnabled("CXBX_TEX_TRACE") ? 1 : 0;
    }
    {
        // Diagnostic bypass: only an explicit 1 disables texture mirroring.
        // The generic presence-only trace helper also considers "0" enabled,
        // which makes backend comparisons accidentally render white textures.
        static int skipMirror = -1;
        if(skipMirror < 0)
        {
            char value[8] = {};
            skipMirror = EmuD3DReadEnvironment("CXBX_VULKAN_SKIP_MIRROR", value,
                                               sizeof(value)) &&
                                 strcmp(value, "1") == 0
                             ? 1
                             : 0;
        }
        if(skipMirror == 1)
        {
            cxbx::d3d8::HostBackendSetTexture(Stage, nullptr, nullptr, 0, 0, 0,
                                              0);
            return;
        }
    }
    if(mirrorTraceEnabled == 1)
    {
        printf("TEX| mirror stage=%lu host=0x%.08lX format=%u usage=0x%.08lX "
               "%ux%u\n",
               static_cast<unsigned long>(Stage),
               reinterpret_cast<unsigned long>(pBaseTexture8),
               static_cast<unsigned>(description.Format),
               static_cast<unsigned long>(description.Usage),
               static_cast<unsigned>(description.Width),
               static_cast<unsigned>(description.Height));
    }

    // Depth surfaces bound as textures are NV2A shadow-buffer sampling
    // (Turok Evolution arms SetRenderState_ShadowFunc right before the
    // bind): the depth content lives in the backend target's depth image,
    // registered under this texture's pointer at SetRenderTarget time.
    // Locking one through the host d3d8 stack faults inside the Vulkan
    // ICD, so the depth never flows through a host texture.
    {
        const XTL::D3DFORMAT format = description.Format;
        const bool depthFormat =
            format == XTL::D3DFMT_D16_LOCKABLE || format == XTL::D3DFMT_D32 ||
            format == XTL::D3DFMT_D15S1 || format == XTL::D3DFMT_D24S8 ||
            format == XTL::D3DFMT_D24X8 || format == XTL::D3DFMT_D24X4S4 ||
            format == XTL::D3DFMT_D16;
        if(depthFormat || (description.Usage & D3DUSAGE_DEPTHSTENCIL) != 0)
        {
            // Slice C: the depth key is the GUEST depth texture pointer.
            if(!cxbx::d3d8::HostBackendSetDepthStageTexture(Stage,
                                                            pGuestKey))
            {
                static bool s_Reported = false;
                if(!s_Reported)
                {
                    s_Reported = true;
                    printf("VULKAN| depth texture 0x%.08lX bound on stage %lu "
                           "has no registered depth target; stage samples "
                           "white\n",
                           reinterpret_cast<unsigned long>(pBaseTexture8),
                           static_cast<unsigned long>(Stage));
                }
                cxbx::d3d8::HostBackendSetTexture(Stage, nullptr, nullptr, 0,
                                                  0, 0, 0);
            }
            return;
        }
    }

    // P5: a texture whose level-0 surface is a registered render target
    // samples that target directly (the host d3d8 copy never sees the
    // Vulkan-rendered content). Slice C: the override resolves by the
    // texture's own guest key -- no host GetSurfaceLevel query per bind.
    if(pGuestKey != nullptr &&
       cxbx::d3d8::HostBackendSetStageRenderTargetTexture(Stage, pGuestKey))
    {
        return;
    }
    XTL::D3DLOCKED_RECT locked = {};

    // P8 C3-texture: a captured container serves the bind without touching
    // the host texture (the last per-bind host-d3d8 read under the Vulkan
    // flavor). Freshness is guaranteed by the capture paths: Register-time
    // population, the linear refresh, the swizzled commits, and served
    // level-0 locks write the container; CopyRects into levels and level-
    // surface locks invalidate it and fall back to the host pull below.
    if(cxbx::d3d8::HostBackendRenders())
    {
        // The container pointer must stay valid through the synchronous
        // renderer upload, so the registry mutex is held across the call.
        bool staleGeometry = false;
        {
            std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
            EmuNativeTexture* native = EmuNativeTextureFindForBind(
                pBaseTexture8, reinterpret_cast<XTL::X_D3DResource*>(pGuestKey));
            if(native != nullptr)
            {
                if(native->Width == description.Width &&
                   native->Height == description.Height)
                {
                    cxbx::d3d8::HostBackendSetTexture(
                        Stage, pGuestKey, native->Pixels.data(),
                        native->Pitch, native->Width, native->Height,
                        native->HostFormat);
                    return;
                }
                // Geometry drift: the capture is stale beyond repair.
                staleGeometry = true;
            }
        }
        if(staleGeometry)
        {
            // Takes the registry mutex itself.
            EmuNativeTextureForget(pBaseTexture8);
        }
    }

    if(FAILED(pTexture8->LockRect(0, &locked, NULL, D3DLOCK_READONLY)))
    {
        cxbx::d3d8::HostBackendSetTexture(Stage, nullptr, nullptr, 0, 0, 0, 0);
        return;
    }
    cxbx::d3d8::HostBackendSetTexture(Stage, pGuestKey, locked.pBits,
                                      static_cast<unsigned>(locked.Pitch),
                                      description.Width, description.Height,
                                      static_cast<unsigned>(description.Format));
    pTexture8->UnlockRect(0);
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTexture
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetTexture(
    DWORD Stage,
    X_D3DResource* pTexture)
{
    EmuSwapFS(); // Win2k/XP FS
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

    IDirect3DBaseTexture8* pBaseTexture8 = NULL;
    BOOL bConvertedYuv = FALSE;

    if(pTexture != NULL)
    {
        EmuYuy2TextureInfo* pInfo = EmuResolveYuy2Texture(pTexture);
        if(pInfo != NULL)
        {
            // The d3d8 flavour converts to a real RGB host texture before
            // sampling; under the native flavour the bind pull converts
            // straight into the backend upload (no host texture involved).
            g_dwOverlayW = pInfo->Width;
            g_dwOverlayH = pInfo->Height;
            bConvertedYuv = TRUE;
            if(!cxbx::d3d8::HostBackendRenders())
            {
                pBaseTexture8 = EmuConvertYuy2Texture(pInfo);
            }
        }
        else if((uint32)pTexture & 0x80000000)
        {
            EmuWarning("SetTexture ignored an unknown high-bit resource 0x%.08lX",
                       reinterpret_cast<unsigned long>(pTexture));
        }
        else
        {
            pBaseTexture8 = pTexture->EmuBaseTexture8;
        }
    }

    if(Stage == 0)
    {
        g_bStage0ConvertedYuv = bConvertedYuv;
    }

    // Xbox texture locks expose unified-memory mappings and have no matching
    // guest Unlock call. End any host lock before sampling the resource.
    EmuFlushHostTextureLocks(pBaseTexture8);

    // Linear textures stream from live guest memory; re-upload current pixels.
    EmuRefreshLinearTexture(pBaseTexture8);
    EmuConfigureLinearTextureCoordinates(Stage, pBaseTexture8);

    HRESULT hRet = D3D_OK;
    __try
    {
        // Follow the active shader's output-texcoord compaction: the game
        // binds to the stage its (uncompacted) shader writes, which is now a
        // different host stage. Clear binds (NULL) pass through untouched:
        // forwarding them would wipe a compacted host stage that still holds
        // the texture the game bound on another stage.
        DWORD HostStage = Stage;
        if(pBaseTexture8 != NULL)
        {
            if(const XTL::VshShaderRegistry::CpuFallbackMetadata* currentMetadata =
                   XTL::VshShaderRegistry::Current())
            {
                for(std::uint32_t entry = 0;
                    entry < currentMetadata->texcoordRemapCount; ++entry)
                {
                    if(Stage == currentMetadata->texcoordRemapFrom[entry])
                    {
                        HostStage = currentMetadata->texcoordRemapTo[entry];
                        break;
                    }
                }
            }
        }
        // Slice C3-pre: the host shadow bind is d3d8-flavor-only; the
        // container upload below serves the Vulkan flavor.
        if(cxbx::d3d8::HostBackendRenders())
            hRet = D3D_OK;
        else
            hRet = g_pD3DDevice8->SetTexture(HostStage, pBaseTexture8);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        hRet = D3D_OK;
    }

    if(Stage < EmuXboxTextureStageCount)
    {
        g_EmuBoundHostTextures[Stage] = pBaseTexture8;
        g_EmuBoundGuestTextures[Stage] = pTexture;
        g_EmuBoundGuestTextureDescriptors[Stage] =
            EmuCaptureRecordedTextureDescriptor(pTexture);
        EmuVulkanStageTextureUpload(Stage, pBaseTexture8, pTexture);
    }

    static int traceEnabled = -1;
    if(traceEnabled < 0)
    {
        traceEnabled = EmuD3DEnvironmentEnabled("CXBX_TEX_TRACE") ? 1 : 0;
    }
    if(traceEnabled == 1)
    {
        printf("TEX| bind stage=%lu guest=0x%.08lX host=0x%.08lX result=0x%.08lX\n",
               static_cast<unsigned long>(Stage),
               reinterpret_cast<unsigned long>(pTexture),
               reinterpret_cast<unsigned long>(pBaseTexture8),
               static_cast<unsigned long>(hRet));
    }

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SwitchTexture
// ******************************************************************
VOID __fastcall XTL::EmuIDirect3DDevice8_SwitchTexture(
    DWORD Method,
    DWORD Data,
    DWORD Format)
{
    EmuSwapFS(); // Win2k/XP FS

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
    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetDisplayMode
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_GetDisplayMode(
    X_D3DDISPLAYMODE* pMode)
{
    EmuSwapFS(); // Win2k/XP FS
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
        D3DDISPLAYMODE* pPCMode = (D3DDISPLAYMODE*)pMode;

        hRet = g_pD3DDevice8->GetDisplayMode(pPCMode);

        // Convert Format (PC->Xbox)
        pMode->Format = EmuPC2XB_D3DFormat(pPCMode->Format);

        // TODO: Make this configurable in the future?
        pMode->Flags = 0x000000A1; // D3DPRESENTFLAG_FIELD | D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_LOCKABLE_BACKBUFFER
    }

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetDisplayFieldStatus
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetDisplayFieldStatus(
    X_D3DFIELD_STATUS* pFieldStatus)
{
    EmuSwapFS(); // Win2k/XP FS
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
        pFieldStatus->Field = 3; // D3DFIELD_PROGRESSIVE
        pFieldStatus->VBlankCount = (DWORD)(((unsigned __int64)GetTickCount() * 60) / 1000);

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

    EmuSwapFS(); // XBox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_Clear
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_Clear(
    DWORD Count,
    CONST D3DRECT* pRects,
    DWORD Flags,
    D3DCOLOR Color,
    float Z,
    DWORD Stencil)
{
    D3D_TRACE("Clear");
    EmuSwapFS(); // Win2k/XP FS
    const std::uint32_t traceSequence = cxbx::trace::RecordD3dCall(
        cxbx::trace::D3dApi::Clear, static_cast<std::uint32_t>(Flags),
        static_cast<std::uint32_t>(g_D3DDebugMarker));

    EmuLocateD3DDebugGlobals();
    DWORD* pApiCounters = EmuD3DPerfApiCounters();
    if(pApiCounters != NULL)
    {
        pApiCounters[EMU_API_D3DDEVICE_CLEAR]++;
    }
    g_D3DPerfPushBufferBytesWritten += sizeof(DWORD);

    EmuFlushTiledSurfaceLocks();
    EmuCommitSurfaceContainerLocks();
    EmuCommitTextureContainerLocks();

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
        // Drop the flags the host cannot honor. Under the render path the
        // host device is unbound (the backend owns targets), so the guest's
        // cached depth surface answers the same question.
        if(newFlags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL))
        {
            IDirect3DSurface8* pDepth = NULL;
            bool HasDepth = false;
            if(cxbx::d3d8::HostBackendRenders())
            {
                HasDepth = (g_pCachedZStencilSurface != NULL);
                pDepth = HasDepth ? g_pCachedZStencilSurface->EmuSurface8 : NULL;
            }
            else
            {
                HasDepth = SUCCEEDED(g_pD3DDevice8->GetDepthStencilSurface(&pDepth));
            }

            if(!HasDepth || pDepth == NULL)
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
                if(!cxbx::d3d8::HostBackendRenders() && pDepth != NULL)
                    pDepth->Release();
            }
        }

        Flags = newFlags;
    }

    HRESULT ret = D3D_OK;
    if(cxbx::d3d8::HostBackendRenders())
    {
        // The backend render target replaces the host backbuffer; the
        // PC-flag subset carries the target/z/stencil bits with the
        // call's z and stencil values.
        cxbx::d3d8::HostBackendClear(Flags, Color, Z, Stencil);
        ret = D3D_OK;
    }
    else if(Flags != 0)
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
        XTL::IDirect3DSurface8* pRenderTarget = NULL;
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
    EmuSwapFS(); // XBox FS

    return ret;
}

DWORD WINAPI XTL::EmuIDirect3DDevice8_SetDebugMarker(DWORD Marker)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("SetDebugMarker");
    DWORD PreviousMarker = g_D3DDebugMarker;
    g_D3DDebugMarker = Marker;
    EmuSwapFS(); // XBox FS
    return PreviousMarker;
}

DWORD WINAPI XTL::EmuIDirect3DDevice8_GetDebugMarker()
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("GetDebugMarker");
    DWORD Marker = g_D3DDebugMarker;
    EmuSwapFS(); // XBox FS
    return Marker;
}

VOID WINAPI XTL::EmuD3DPERF_Reset()
{
    D3D_TRACE("D3DPERF_Reset");
    EmuSwapFS(); // Win2k/XP FS
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
    g_D3DPerfPushBufferBytesWritten = 0;
    g_D3DPerfEventDepth = 0;
    EmuSwapFS(); // XBox FS
}

VOID WINAPI XTL::EmuD3DPERF_GetPushBufferInfo(PVOID pInfo)
{
    D3D_TRACE("D3DPERF_GetPushBufferInfo");
    EmuSwapFS(); // Win2k/XP FS

    EmuD3DPushBufferInfo* pPushBufferInfo =
        static_cast<EmuD3DPushBufferInfo*>(pInfo);
    if(pPushBufferInfo != NULL)
    {
        pPushBufferInfo->PushBufferSize = sizeof(g_D3DPerfPushBuffer);
        pPushBufferInfo->PushSegmentSize = sizeof(g_D3DPerfPushBuffer);
        pPushBufferInfo->PushSegmentCount = 1;
        pPushBufferInfo->pPushBase = g_D3DPerfPushBuffer.data();
        pPushBufferInfo->pPushLimit =
            g_D3DPerfPushBuffer.data() + g_D3DPerfPushBuffer.size();
        pPushBufferInfo->PushBufferBytesWritten =
            g_D3DPerfPushBufferBytesWritten;
    }

    EmuSwapFS(); // XBox FS
}

VOID __cdecl XTL::EmuD3DPERF_SetMarker(DWORD Color, const char* Name, ...)
{
    (void)Color;
    (void)Name;
    D3D_TRACE("D3DPERF_SetMarker");
    EmuSwapFS(); // Win2k/XP FS
    EmuSwapFS(); // XBox FS
}

INT __cdecl XTL::EmuD3DPERF_BeginEvent(DWORD Color, const char* Name, ...)
{
    (void)Color;
    (void)Name;
    D3D_TRACE("D3DPERF_BeginEvent");
    EmuSwapFS(); // Win2k/XP FS
    const INT EventLevel = g_D3DPerfEventDepth++;
    EmuSwapFS(); // XBox FS
    return EventLevel;
}

INT WINAPI XTL::EmuD3DPERF_EndEvent()
{
    D3D_TRACE("D3DPERF_EndEvent");
    EmuSwapFS(); // Win2k/XP FS
    if(g_D3DPerfEventDepth > 0)
    {
        --g_D3DPerfEventDepth;
    }
    const INT EventLevel = g_D3DPerfEventDepth;
    EmuSwapFS(); // XBox FS
    return EventLevel;
}

// Mirror the presented frame to the emulator's GDI window
// (emulation_runtime.cpp).
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
HRESULT WINAPI XTL::EmuIDirect3DDevice8_Present(
    CONST RECT* pSourceRect,
    CONST RECT* pDestRect,
    PVOID pDummy1,
    PVOID pDummy2)
{
    EmuSwapFS(); // Win2k/XP FS
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
    EmuCommitSurfaceContainerLocks();
    EmuCommitTextureContainerLocks();

    const HRESULT hRet = EmuPresentHostDevice(
        pSourceRect, pDestRect, static_cast<HWND>(pDummy1),
        static_cast<const RGNDATA*>(pDummy2), true);

    g_D3DDebugMarker = 0;

    EmuSwapFS(); // XBox FS

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

    // The native renderer owns a separate backbuffer. The converted frame
    // already lives in the shared CPU BGRA cache (no host texture).
    if(cxbx::d3d8::HostBackendRenders())
    {
        if(g_pOverlayFrameBgra == nullptr || g_dwOverlayFrameW == 0 ||
           g_dwOverlayFrameH == 0)
        {
            return;
        }
        static bool warned = false;
        if(!cxbx::d3d8::HostBackendComposeOverlay(
               g_pOverlayFrameBgra, g_dwOverlayFrameW, g_dwOverlayFrameH,
               g_dwOverlayFrameW * 4) &&
           !warned)
        {
            warned = true;
            EmuWarning("Vulkan movie overlay composition failed");
        }
        return;
    }

    if(g_pOverlayFrameTexture == NULL)
    {
        return;
    }

    IDirect3DSurface8* pBackBuffer = NULL;
    if(FAILED(g_pD3DDevice8->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer)) || pBackBuffer == NULL)
        return;

    D3DSURFACE_DESC Desc;
    pBackBuffer->GetDesc(&Desc);
    pBackBuffer->Release();

    struct
    {
        FLOAT x, y, z, rhw, u, v;
    } Quad[4] = {
        { -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f },
        { Desc.Width - 0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f },
        { Desc.Width - 0.5f, Desc.Height - 0.5f, 0.0f, 1.0f, 1.0f, 1.0f },
        { -0.5f, Desc.Height - 0.5f, 0.0f, 1.0f, 0.0f, 1.0f },
    };

    // Save the state the quad needs.
    DWORD OldVS = 0, OldPS = 0, OldZ = 0, OldBlend = 0, OldCull = 0;
    DWORD OldColorOp = 0, OldColorArg1 = 0, OldMag = 0, OldMin = 0;
    IDirect3DBaseTexture8* pOldTexture = NULL;

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
HRESULT WINAPI XTL::EmuIDirect3DDevice8_Swap(
    DWORD Flags)
{
    EmuSwapFS(); // XBox FS
    D3D_TRACE("Swap");

    EmuFrameProfileFrame();

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
    EmuCommitSurfaceContainerLocks();
    EmuCommitTextureContainerLocks();

    EmuComposeOverlay();

    // Windowed host Present does not reliably block for the Xbox's 60 Hz
    // display cadence. Pace every Swap so frame-counted title animations and
    // attract-mode timeouts do not run at unrestricted host speed. If Present
    // already blocked, EmuPacePresent advances over that elapsed period without
    // adding another wait.
    EmuPacePresent(g_NextSwapPresent, 1, 60);

    const HRESULT hRet = EmuPresentHostDevice(nullptr, nullptr, nullptr, nullptr, true);

    if((g_pOverlayFrameTexture != NULL || g_pOverlayFrameBgra != nullptr) &&
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

    EmuSwapFS(); // XBox FS

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

#define EMU_IM_FVF      (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)
#define EMU_IM_MAXVERTS 8192

static EmuImVertex g_EmuImVerts[EMU_IM_MAXVERTS];
static EmuImVertex g_EmuImCur = { 0, 0, 0, 1.0f, 0xFFFFFFFF, 0, 0 };
static DWORD g_EmuImPrim = 0;
static int g_EmuImCount = 0;
static BOOL g_EmuImActive = FALSE;
static BOOL g_EmuImCustomShader = FALSE;
static BOOL g_EmuImConvertedYuv = FALSE;
static BOOL g_EmuCurrentVertexShaderIsCustom = FALSE;
// Raw FVF (low-bit handles) of the active vertex shader; the P2 render path
// uses it to interpret DrawVerticesUP vertex streams.
static DWORD g_EmuCurrentFvf = 0;

VOID WINAPI XTL::EmuIDirect3DDevice8_Begin(
    X_D3DPRIMITIVETYPE PrimitiveType)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS
}

VOID WINAPI XTL::EmuIDirect3DDevice8_SetVertexData4f(
    INT Register,
    FLOAT a,
    FLOAT b,
    FLOAT c,
    FLOAT d)
{
    D3D_TRACE("SetVertexData4f");
    EmuSwapFS(); // Win2k/XP FS

    switch(Register)
    {
        case -1: // D3DVSDE_VERTEX: final position write, emits the vertex
        case 0:  // D3DVSDE_POSITION
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

        case 3: // D3DVSDE_DIFFUSE as floats
        {
            DWORD r = (DWORD)(a * 255.0f) & 0xFF;
            DWORD g = (DWORD)(b * 255.0f) & 0xFF;
            DWORD bl = (DWORD)(c * 255.0f) & 0xFF;
            DWORD al = (DWORD)(d * 255.0f) & 0xFF;
            g_EmuImCur.Diffuse = (al << 24) | (r << 16) | (g << 8) | bl;
            break;
        }

        case 9: // D3DVSDE_TEXCOORD0
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

    EmuSwapFS(); // XBox FS
}

VOID WINAPI XTL::EmuIDirect3DDevice8_SetVertexData2f(
    INT Register,
    FLOAT a,
    FLOAT b)
{
    D3D_TRACE("SetVertexData2f");
    // 2f writes to the position register leave z=0, w=1
    XTL::EmuIDirect3DDevice8_SetVertexData4f(Register, a, b, 0.0f, 1.0f);
}

VOID WINAPI XTL::EmuIDirect3DDevice8_SetVertexDataColor(
    INT Register,
    DWORD Color)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("SetVertexDataColor");

    if(Register == 3)
        g_EmuImCur.Diffuse = Color;

    EmuSwapFS(); // XBox FS
}

VOID WINAPI XTL::EmuIDirect3DDevice8_End()
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("End");

#ifdef _DEBUG_TRACE
    printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_End() verts=%d\n", GetCurrentThreadId(), g_EmuImCount);
#endif

    if(g_EmuImActive && g_EmuImCount >= 3 && g_pD3DDevice8 != 0)
    {
        g_pD3DDevice8->SetVertexShader(EMU_IM_FVF);

        if(g_EmuImPrim == 8) // X_D3DPT_QUADLIST: expand each quad to two triangles
        {
            static EmuImVertex Tris[EMU_IM_MAXVERTS + (EMU_IM_MAXVERTS / 2)];
            int Quads = g_EmuImCount / 4;
            int n = 0;
            for(int q = 0; q < Quads; q++)
            {
                EmuImVertex* v = &g_EmuImVerts[q * 4];
                Tris[n++] = v[0];
                Tris[n++] = v[1];
                Tris[n++] = v[2];
                Tris[n++] = v[0];
                Tris[n++] = v[2];
                Tris[n++] = v[3];
            }
            if(EmuD3DDrawGate("End", D3DPT_TRIANGLELIST, n / 3))
            {
                __try
                {
                    if(cxbx::d3d8::HostBackendRenders() &&
                       GetEnvironmentVariableA("CXBX_VULKAN_NO_QUAD",
                                               nullptr, 0) == 0)
                    {
                        cxbx::d3d8::HostBackendDrawUP(
                            D3DPT_TRIANGLELIST, n / 3, Tris,
                            sizeof(EmuImVertex), 16 /*D3DCOLOR after rhw*/,
                            20 /*u,v after diffuse*/, 0, 0xFFFFFFFFu);
                    }
                    else
                    {
                        g_pD3DDevice8->DrawPrimitiveUP(D3DPT_TRIANGLELIST, n / 3, Tris, sizeof(EmuImVertex));
                    }
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
                    if(cxbx::d3d8::HostBackendRenders())
                    {
                        cxbx::d3d8::HostBackendDrawUP(
                            PCPrim, PrimCount, g_EmuImVerts,
                            sizeof(EmuImVertex), 16 /*D3DCOLOR after rhw*/,
                            20 /*u,v after diffuse*/, 0, 0xFFFFFFFFu);
                    }
                    else
                    {
                        g_pD3DDevice8->DrawPrimitiveUP(PCPrim, PrimCount, g_EmuImVerts, sizeof(EmuImVertex));
                    }
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

    EmuSwapFS(); // XBox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_MakeSpace
// ******************************************************************
DWORD* WINAPI XTL::EmuIDirect3DDevice8_MakeSpace()
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return pushBuffer;
}

// ******************************************************************
// * func: EmuIDirect3DResource8_Register
// ******************************************************************
struct EmuRegisteredCubemapSurface
{
    XTL::X_D3DResource* Resource;
    XTL::IDirect3DCubeTexture8* HostTexture;
    DWORD Face;
};

struct EmuRegisteredCubemapGroup
{
    XTL::IDirect3DCubeTexture8* HostTexture;
    std::uintptr_t SourceBase;
    std::size_t FaceStride;
};

static std::vector<EmuRegisteredCubemapSurface>
    g_EmuRegisteredCubemapSurfaces;
static std::vector<EmuRegisteredCubemapGroup> g_EmuRegisteredCubemapGroups;

static void EmuRememberRegisteredCubemapSurface(
    XTL::X_D3DResource* resource,
    XTL::IDirect3DCubeTexture8* hostTexture,
    const void* source,
    std::size_t faceStride)
{
    if(resource == nullptr || hostTexture == nullptr ||
       source == nullptr || faceStride == 0)
    {
        return;
    }

    const std::uintptr_t sourceAddress =
        reinterpret_cast<std::uintptr_t>(source);
    DWORD face = 0;
    bool foundGroup = false;
    for(const EmuRegisteredCubemapGroup& group :
        g_EmuRegisteredCubemapGroups)
    {
        if(group.HostTexture != hostTexture ||
           group.FaceStride != faceStride ||
           sourceAddress < group.SourceBase)
        {
            continue;
        }

        const std::uintptr_t offset = sourceAddress - group.SourceBase;
        if(offset < faceStride * 6 && offset % faceStride == 0)
        {
            face = static_cast<DWORD>(offset / faceStride);
            foundGroup = true;
            break;
        }
    }

    if(!foundGroup)
    {
        g_EmuRegisteredCubemapGroups.push_back(
            { hostTexture, sourceAddress, faceStride });
    }

    for(EmuRegisteredCubemapSurface& surface :
        g_EmuRegisteredCubemapSurfaces)
    {
        if(surface.Resource == resource)
        {
            surface.HostTexture = hostTexture;
            surface.Face = face;
            return;
        }
    }

    g_EmuRegisteredCubemapSurfaces.push_back(
        { resource, hostTexture, face });
}

static DWORD EmuRegisteredCubemapSurfaceFace(
    const XTL::X_D3DResource* resource)
{
    for(const EmuRegisteredCubemapSurface& surface :
        g_EmuRegisteredCubemapSurfaces)
    {
        if(surface.Resource == resource)
        {
            return surface.Face;
        }
    }
    return 0;
}

// SEH body helper (C2712: EmuRegisterCubeTexture holds std::vector staging,
// and a function containing __try may declare no destructor-bearing object).
static HRESULT EmuCreateCubeTextureGuarded(
    XTL::X_D3DResource* pResource, DWORD EdgeLength, DWORD hostMipLevels,
    XTL::D3DFORMAT Format)
{
    HRESULT result = D3DERR_INVALIDCALL;
    __try
    {
        result = g_pD3DDevice8->CreateCubeTexture(
            EdgeLength, hostMipLevels, 0, Format, XTL::D3DPOOL_MANAGED,
            &pResource->EmuCubeTexture8);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        result = D3DERR_INVALIDCALL;
    }
    return result;
}

// SEH body helper (C2712: the registration body holds std::vector staging,
// and a function containing __try may declare no destructor-bearing object).
static bool EmuMemcpyGuarded(void* destination, const void* source, DWORD size)
{
    __try
    {
        memcpy(destination, source, size);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return true;
}

static HRESULT EmuRegisterCubeTexture(
    XTL::X_D3DResource* pResource,
    const void* pSource,
    const void* pAlternateSource,
    DWORD EdgeLength,
    DWORD MipLevels,
    XTL::D3DFORMAT Format,
    BOOL Swizzled,
    DWORD BytesPerPixel,
    BOOL Compressed,
    DWORD BytesPerBlock)
{
    constexpr std::uint64_t CubeFaceCount = 6;
    constexpr std::uint64_t CubeFaceAlignment = 128;

    if(pResource == NULL || EdgeLength == 0 || MipLevels == 0 ||
       (!Swizzled && !Compressed) ||
       (Swizzled && BytesPerPixel == 0) ||
       (Compressed && BytesPerBlock == 0))
    {
        return D3DERR_INVALIDCALL;
    }

    std::uint64_t faceBytes64 = 0;
    DWORD levelWidth = EdgeLength;
    DWORD levelHeight = EdgeLength;
    for(DWORD level = 0; level < MipLevels; ++level)
    {
        const std::uint64_t levelBytes =
            Compressed
                ? static_cast<std::uint64_t>((levelWidth + 3) / 4) *
                      ((levelHeight + 3) / 4) * BytesPerBlock
                : static_cast<std::uint64_t>(levelWidth) * levelHeight *
                      BytesPerPixel;
        faceBytes64 += levelBytes;
        levelWidth = levelWidth > 1 ? levelWidth / 2 : 1;
        levelHeight = levelHeight > 1 ? levelHeight / 2 : 1;
    }

    const std::uint64_t faceStride64 =
        ((faceBytes64 + CubeFaceAlignment - 1) / CubeFaceAlignment) *
        CubeFaceAlignment;
    const std::uint64_t sourceBytes64 = faceStride64 * CubeFaceCount;
    if(faceBytes64 == 0 ||
       faceBytes64 > (std::numeric_limits<std::size_t>::max)() ||
       sourceBytes64 > MAXDWORD)
    {
        return D3DERR_INVALIDCALL;
    }

    const std::size_t faceBytes =
        Compressed
            ? cxbx::d3d::CompressedTextureMipChainSize(
                  EdgeLength, EdgeLength, BytesPerBlock, MipLevels)
            : cxbx::d3d::TextureMipChainSize(
                  EdgeLength, EdgeLength, BytesPerPixel, MipLevels);
    const std::size_t faceStride =
        cxbx::d3d::AlignTextureSize(faceBytes, CubeFaceAlignment);
    if(faceBytes != faceBytes64 || faceStride != faceStride64)
    {
        return D3DERR_INVALIDCALL;
    }

    const DWORD hostMipLevels = static_cast<DWORD>(
        cxbx::d3d::HostTextureMipLevelCount(
            EdgeLength, EdgeLength, MipLevels));

    HRESULT result = EmuCreateCubeTextureGuarded(
        pResource, EdgeLength, hostMipLevels, Format);
    if(FAILED(result) || pResource->EmuCubeTexture8 == NULL)
    {
        EmuWarning("Resource_Register: CreateCubeTexture(%lu, %lu, format=0x%.08lX) "
                   "failed (0x%.08lX); using a blank placeholder",
                   static_cast<unsigned long>(EdgeLength),
                   static_cast<unsigned long>(hostMipLevels),
                   static_cast<unsigned long>(Format),
                   static_cast<unsigned long>(result));

        pResource->EmuCubeTexture8 = NULL;
        result = EmuCreateCubeTextureGuarded(
            pResource, 4, 1, XTL::D3DFMT_A8R8G8B8);

        if(FAILED(result) || pResource->EmuCubeTexture8 == NULL)
        {
            EmuWarning("Resource_Register: cubemap placeholder creation failed "
                       "(0x%.08lX); resource left unbacked",
                       static_cast<unsigned long>(result));
            return result;
        }

        EmuRememberHostResource(pResource->EmuCubeTexture8);
        return D3D_OK;
    }

    EmuRememberHostResource(pResource->EmuCubeTexture8);

    const DWORD sourceBytes = static_cast<DWORD>(sourceBytes64);
    std::vector<BYTE> sourceCopy;
    bool copiedSource = EmuD3DCopyReadableRange(
        pSource, sourceBytes, sourceCopy);
    if(!copiedSource && pAlternateSource != pSource)
    {
        copiedSource = EmuD3DCopyReadableRange(
            pAlternateSource, sourceBytes, sourceCopy);
    }
    if(!copiedSource)
    {
        EmuWarning("Resource_Register: cubemap source is unreadable "
                   "(edge=%lu levels=%lu bytes=0x%.08lX); texture left blank",
                   static_cast<unsigned long>(EdgeLength),
                   static_cast<unsigned long>(MipLevels),
                   static_cast<unsigned long>(sourceBytes));
        return D3D_OK;
    }

    const DWORD copyMipLevels = (std::min)(MipLevels, pResource->EmuCubeTexture8->GetLevelCount());
    for(DWORD face = 0; face < CubeFaceCount; ++face)
    {
        std::size_t sourceOffset = face * faceStride;
        levelWidth = EdgeLength;
        levelHeight = EdgeLength;

        for(DWORD level = 0; level < copyMipLevels; ++level)
        {
            const DWORD levelBytes =
                Compressed
                    ? static_cast<DWORD>(
                          cxbx::d3d::CompressedTextureLevelSize(
                              levelWidth, levelHeight, BytesPerBlock))
                    : levelWidth * levelHeight * BytesPerPixel;
            XTL::D3DLOCKED_RECT lockedRect = {};
            result = pResource->EmuCubeTexture8->LockRect(
                static_cast<XTL::D3DCUBEMAP_FACES>(face), level,
                &lockedRect, NULL, 0);
            if(FAILED(result))
            {
                EmuWarning("Resource_Register: cubemap face %lu level %lu lock "
                           "failed (0x%.08lX)",
                           static_cast<unsigned long>(face),
                           static_cast<unsigned long>(level),
                           static_cast<unsigned long>(result));
                return D3D_OK;
            }

            BYTE* levelSource = sourceCopy.data() + sourceOffset;
            if(Swizzled)
            {
                RECT sourceRect = { 0, 0, 0, 0 };
                POINT destinationPoint = { 0, 0 };
                XTL::EmuXGUnswizzleRect(
                    levelSource, levelWidth, levelHeight, 1,
                    lockedRect.pBits, lockedRect.Pitch,
                    sourceRect, destinationPoint, BytesPerPixel);
            }
            else
            {
                const DWORD rowBytes =
                    ((levelWidth + 3) / 4) * BytesPerBlock;
                const DWORD blockRows = (levelHeight + 3) / 4;
                BYTE* destination = static_cast<BYTE*>(lockedRect.pBits);
                const BYTE* source = levelSource;
                for(DWORD row = 0; row < blockRows; ++row)
                {
                    memcpy(destination, source, rowBytes);
                    destination += lockedRect.Pitch;
                    source += rowBytes;
                }
            }

            pResource->EmuCubeTexture8->UnlockRect(
                static_cast<XTL::D3DCUBEMAP_FACES>(face), level);
            sourceOffset += levelBytes;
            levelWidth = levelWidth > 1 ? levelWidth / 2 : 1;
            levelHeight = levelHeight > 1 ? levelHeight / 2 : 1;
        }
    }

    if(EmuD3DTexTraceEnabled())
    {
        printf("TEX| cubemap-registered resource=0x%.08lX host=0x%.08lX "
               "edge=%lu levels=%lu face_stride=0x%.08lX bytes=0x%.08lX\n",
               reinterpret_cast<unsigned long>(pResource),
               reinterpret_cast<unsigned long>(pResource->EmuCubeTexture8),
               static_cast<unsigned long>(EdgeLength),
               static_cast<unsigned long>(copyMipLevels),
               static_cast<unsigned long>(faceStride),
               static_cast<unsigned long>(sourceBytes));
    }

    return D3D_OK;
}

// The registration body. Guarded by the __try in
// EmuIDirect3DResource8_Register; kept in its own function because C2712
// bans destructor-bearing objects (the std::vector staging below) in any
// function containing __try.
namespace XTL
{

static HRESULT EmuResourceRegisterGuardedBody(
    X_D3DResource* pThis,
    PVOID pBase)
{
    HRESULT hRet = D3D_OK;
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

        X_D3DResource* pResource = (X_D3DResource*)pThis;

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

                X_D3DVertexBuffer* pVertexBuffer = (X_D3DVertexBuffer*)pResource;

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
                    // backing store. Preserve that source separately from the host
                    // D3D copy so CPU vertex-shader draws observe later guest writes.
                    EmuResetRegisteredVertexBuffer(pVertexBuffer, vertexData, dwSize);

                    hRet = g_pD3DDevice8->CreateVertexBuffer(
                        dwSize, 0, 0, D3DPOOL_MANAGED,
                        &pResource->EmuVertexBuffer8);

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

                    BYTE* pData = 0;

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
                    if(!EmuMemcpyGuarded(pData, vertexData, dwSize))
                    {
                        EmuWarning("EmuIDirect3DResource8_Register: VertexBuffer data copy fault at 0x%.08X -- resource left unbacked", (DWORD)vertexData);
                        pResource->EmuVertexBuffer8->Unlock();
                        pResource->EmuVertexBuffer8 = 0;
                        pResource->Data = 0;
                        EmuDiscardRegisteredVertexBuffer(pVertexBuffer);
                        break;
                    }

                    // Slice B: dual-write — the container mirrors the
                    // registered bytes for the Vulkan flavor's reads.
                    if(cxbx::d3d8::HostBackendRenders())
                    {
                        EmuNativeBufferAdopt(pResource->EmuVertexBuffer8,
                                             pData, dwSize);
                        // Slice C: attach the guest wrapper identity so the
                        // release drops the container by it.
                        EmuNativeBufferAttachGuest(pResource->EmuVertexBuffer8,
                                                   pResource);
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

                X_D3DIndexBuffer* pIndexBuffer = (X_D3DIndexBuffer*)pResource;

                // ******************************************************************
                // * Create the index buffer
                // ******************************************************************
                {
                    DWORD dwSize = EmuCheckAllocationSize(pBase);

                    // A zero / unknown allocation size (hand-built resource
                    // headers) makes host CreateIndexBuffer fail -- DXVK
                    // refuses zero-length buffers where the MS runtime
                    // tolerated them. The registered resource's Data ends up
                    // pointing at the host lock, so use a realistic buffer
                    // size: an undersized one is overrun by the title's own
                    // index reads and writes.
                    if(dwSize == 0)
                        dwSize = 0x10000;

                    HRESULT hRet = g_pD3DDevice8->CreateIndexBuffer(
                        dwSize, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED,
                        &pIndexBuffer->EmuIndexBuffer8);

                    // Same policy as the vertex-buffer branch: leave the
                    // resource unbacked instead of terminating the process.
                    if(FAILED(hRet) || pResource->EmuIndexBuffer8 == 0)
                    {
                        EmuWarning("EmuIDirect3DResource8_Register: CreateIndexBuffer(%u) failed (0x%.08X) -- resource left unbacked",
                                   dwSize, hRet);
                        pResource->EmuIndexBuffer8 = 0;
                        break;
                    }

                    BYTE* pData = 0;

                    hRet = pResource->EmuIndexBuffer8->Lock(0, dwSize, &pData, 0);

                    if(FAILED(hRet))
                    {
                        EmuWarning("EmuIDirect3DResource8_Register: IndexBuffer Lock failed (0x%.08X) -- resource left unbacked",
                                   hRet);
                        pResource->EmuIndexBuffer8->Release();
                        pResource->EmuIndexBuffer8 = 0;
                        break;
                    }

                    if(!EmuMemcpyGuarded(pData, (void*)pBase, dwSize))
                    {
                        EmuWarning("EmuIDirect3DResource8_Register: IndexBuffer data copy fault at 0x%.08X -- resource left unbacked", (DWORD)pBase);
                        pResource->EmuIndexBuffer8->Unlock();
                        pResource->EmuIndexBuffer8 = 0;
                        pResource->Data = 0;
                        break;
                    }

                    // Slice B: dual-write — see the vertex-buffer branch.
                    if(cxbx::d3d8::HostBackendRenders())
                    {
                        EmuNativeBufferAdopt(pResource->EmuIndexBuffer8,
                                             pData, dwSize);
                        EmuNativeBufferAttachGuest(pResource->EmuIndexBuffer8,
                                                   pResource);
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
                X_D3DPushBuffer* pPushBuffer = (X_D3DPushBuffer*)pResource;
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

                X_D3DPixelContainer* pPixelContainer = (X_D3DPixelContainer*)pResource;

                X_D3DFORMAT X_Format = (X_D3DFORMAT)((pPixelContainer->Format & X_D3DFORMAT_FORMAT_MASK) >> X_D3DFORMAT_FORMAT_SHIFT);
                D3DFORMAT Format = EmuXB2PC_D3DFormat(X_Format);

                // TODO: HACK: Temporary?
                if(X_Format == 0x2E)
                {
                    X_Format = 0x12;
                    Format = D3DFMT_A8R8G8B8;
                }

                DWORD dwWidth = 0, dwHeight = 0, dwBPP = 0;
                DWORD dwDepth = 1, dwPitch = 0, dwMipMapLevels = 1;
                DWORD dwCompressedSize = 0;
                DWORD dwCompressedBytesPerBlock = 0;
                BOOL bSwizzled = FALSE, bCompressed = FALSE;
                BOOL bCubemap = pPixelContainer->Format & X_D3DFORMAT_CUBEMAP;

                if(bCubemap && EmuD3DTexTraceEnabled())
                {
                    printf("TEX| cubemap-register resource=0x%.08lX common=0x%.08lX "
                           "format=0x%.08lX size=0x%.08lX data=0x%.08lX "
                           "base=0x%.08lX resolved=0x%.08lX\n",
                           reinterpret_cast<unsigned long>(pResource),
                           static_cast<unsigned long>(pResource->Common),
                           static_cast<unsigned long>(pPixelContainer->Format),
                           static_cast<unsigned long>(pPixelContainer->Size),
                           static_cast<unsigned long>(pResource->Data),
                           static_cast<unsigned long>(dwRegisterBase),
                           reinterpret_cast<unsigned long>(pBase));
                }

                // ******************************************************************
                // * Interpret Width/Height/BPP
                // ******************************************************************
                if(X_Format == 0x07 /* X_D3DFMT_X8R8G8B8 */ || X_Format == 0x06 /* X_D3DFMT_A8R8G8B8 */)
                {
                    bSwizzled = TRUE;

                    // Swizzled 32 Bit
                    dwWidth = 1 << ((pPixelContainer->Format & X_D3DFORMAT_USIZE_MASK) >> X_D3DFORMAT_USIZE_SHIFT);
                    dwHeight = 1 << ((pPixelContainer->Format & X_D3DFORMAT_VSIZE_MASK) >> X_D3DFORMAT_VSIZE_SHIFT);
                    dwDepth = 1 << ((pPixelContainer->Format & X_D3DFORMAT_PSIZE_MASK) >> X_D3DFORMAT_PSIZE_SHIFT);
                    dwPitch = dwWidth * 4;
                    dwBPP = 4;
                }
                else if(X_Format == 0x05 /* X_D3DFMT_R5G6B5 */ || X_Format == 0x04 /* X_D3DFMT_A4R4G4B4 */ ||
                        X_Format == 0x02 /* X_D3DFMT_A1R5G5B5 */ ||
                        X_Format == 0x28 /* X_D3DFMT_G8B8 */ || X_Format == 0x1A /* X_D3DFMT_A8L8 */)
                {
                    bSwizzled = TRUE;

                    // Swizzled 16 Bit
                    dwWidth = 1 << ((pPixelContainer->Format & X_D3DFORMAT_USIZE_MASK) >> X_D3DFORMAT_USIZE_SHIFT);
                    dwHeight = 1 << ((pPixelContainer->Format & X_D3DFORMAT_VSIZE_MASK) >> X_D3DFORMAT_VSIZE_SHIFT);
                    dwDepth = 1 << ((pPixelContainer->Format & X_D3DFORMAT_PSIZE_MASK) >> X_D3DFORMAT_PSIZE_SHIFT);
                    dwPitch = dwWidth * 2;
                    dwBPP = 2;
                }
                else if(X_Format == 0x12 /* X_D3DFORMAT_A8R8G8B8 */ ||
                        X_Format == 0x1E /* X_D3DFMT_LIN_X8R8G8B8 */ ||
                        X_Format == 0x2E /* D3DFMT_LIN_D24S8 */)
                {
                    // Linear 32 Bit
                    dwWidth = (pPixelContainer->Size & X_D3DSIZE_WIDTH_MASK) + 1;
                    dwHeight = ((pPixelContainer->Size & X_D3DSIZE_HEIGHT_MASK) >> X_D3DSIZE_HEIGHT_SHIFT) + 1;
                    dwPitch = (((pPixelContainer->Size & X_D3DSIZE_PITCH_MASK) >> X_D3DSIZE_PITCH_SHIFT) + 1) * 64;
                    dwBPP = 4;
                }
                else if(X_Format == 0x11 /* D3DFMT_LIN_R5G6B5 */ ||
                        X_Format == 0x10 /* D3DFMT_LIN_A1R5G5B5 */ ||
                        X_Format == 0x24 /* D3DFMT_YUY2 */)
                {
                    // Linear/packed 16 Bit
                    dwWidth = (pPixelContainer->Size & X_D3DSIZE_WIDTH_MASK) + 1;
                    dwHeight = ((pPixelContainer->Size & X_D3DSIZE_HEIGHT_MASK) >> X_D3DSIZE_HEIGHT_SHIFT) + 1;
                    dwPitch = (((pPixelContainer->Size & X_D3DSIZE_PITCH_MASK) >> X_D3DSIZE_PITCH_SHIFT) + 1) * 64;
                    dwBPP = 2;
                }
                else if(X_Format == 0x0C /* D3DFMT_DXT1 */ || X_Format == 0x0E /* D3DFMT_DXT2 */ || X_Format == 0x0F /* D3DFMT_DXT3 */)
                {
                    bCompressed = TRUE;

                    // Compressed
                    dwWidth = 1 << ((pPixelContainer->Format & X_D3DFORMAT_USIZE_MASK) >> X_D3DFORMAT_USIZE_SHIFT);
                    dwHeight = 1 << ((pPixelContainer->Format & X_D3DFORMAT_VSIZE_MASK) >> X_D3DFORMAT_VSIZE_SHIFT);
                    dwDepth = 1 << ((pPixelContainer->Format & X_D3DFORMAT_PSIZE_MASK) >> X_D3DFORMAT_PSIZE_SHIFT);

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
                    dwWidth = 1 << ((pPixelContainer->Format & X_D3DFORMAT_USIZE_MASK) >> X_D3DFORMAT_USIZE_SHIFT);
                    dwHeight = 1 << ((pPixelContainer->Format & X_D3DFORMAT_VSIZE_MASK) >> X_D3DFORMAT_VSIZE_SHIFT);
                    dwDepth = 1 << ((pPixelContainer->Format & X_D3DFORMAT_PSIZE_MASK) >> X_D3DFORMAT_PSIZE_SHIFT);
                    dwPitch = dwWidth * 4;
                    dwBPP = 4;
                    pBase = NULL; // the source-validation guard below skips the copy
                }

                if(bCubemap)
                {
                    const DWORD encodedMipLevels =
                        (pPixelContainer->Format & X_D3DFORMAT_MIPMAP_MASK) >>
                        X_D3DFORMAT_MIPMAP_SHIFT;
                    dwMipMapLevels = (std::max)(1ul, encodedMipLevels);
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
                    s_TexTraceEnabled = EmuD3DEnvironmentEnabled("CXBX_TEX_TRACE") ? 1 : 0;
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

                if(bCubemap)
                {
                    if(dwWidth != dwHeight)
                    {
                        EmuWarning("Resource_Register: cubemap faces are not square "
                                   "(%lux%lu); resource left unbacked",
                                   static_cast<unsigned long>(dwWidth),
                                   static_cast<unsigned long>(dwHeight));
                        hRet = D3DERR_INVALIDCALL;
                        break;
                    }

                    hRet = EmuRegisterCubeTexture(
                        pResource,
                        pBase,
                        reinterpret_cast<const void*>(dwUnmaskedDataAddress),
                        dwWidth,
                        dwMipMapLevels,
                        Format,
                        bSwizzled,
                        dwBPP,
                        bCompressed,
                        dwCompressedBytesPerBlock);
                    if(SUCCEEDED(hRet) &&
                       dwCommonType == X_D3DCOMMON_TYPE_SURFACE &&
                       pResource->EmuCubeTexture8 != NULL)
                    {
                        const std::size_t faceBytes =
                            bCompressed
                                ? cxbx::d3d::CompressedTextureMipChainSize(
                                      dwWidth, dwHeight,
                                      dwCompressedBytesPerBlock,
                                      dwMipMapLevels)
                                : cxbx::d3d::TextureMipChainSize(
                                      dwWidth, dwHeight, dwBPP,
                                      dwMipMapLevels);
                        const std::size_t faceStride =
                            cxbx::d3d::AlignTextureSize(faceBytes, 128);
                        EmuRememberRegisteredCubemapSurface(
                            pResource, pResource->EmuCubeTexture8,
                            pBase, faceStride);
                    }
                    break;
                }

                if(X_Format == 0x24)
                {
                    hRet = EmuRegisterYuy2Texture(
                        pResource, static_cast<uint08*>(pBase),
                        dwWidth, dwHeight, dwPitch);
                    if(SUCCEEDED(hRet))
                    {
                        break;
                    }

                    EmuWarning("Resource_Register: invalid YUY2 resource %lux%lu pitch=%lu data=0x%.08lX (0x%.08lX); using a blank placeholder",
                               static_cast<unsigned long>(dwWidth),
                               static_cast<unsigned long>(dwHeight),
                               static_cast<unsigned long>(dwPitch),
                               reinterpret_cast<unsigned long>(pBase),
                               static_cast<unsigned long>(hRet));
                    Format = D3DFMT_A8R8G8B8;
                    dwPitch = dwWidth * sizeof(DWORD);
                    dwBPP = sizeof(DWORD);
                    pBase = NULL;
                }

                // ******************************************************************
                // * Create the happy little texture
                // ******************************************************************
                if(dwCommonType == X_D3DCOMMON_TYPE_SURFACE)
                {
                    hRet = g_pD3DDevice8->CreateImageSurface(dwWidth, dwHeight, Format, &pResource->EmuSurface8);
                    if(SUCCEEDED(hRet))
                        EmuRememberHostResource(pResource->EmuSurface8);
                }
                else
                {
                    // TODO: HACK: Figure out why this is necessary!
                    // TODO: This is necessary for DXT1 textures at least (4x4 blocks minimum)
                    if(dwWidth < 4)
                    {
                        printf("*Warning* expanding texture width (%d->4)\n", dwWidth);
                        dwWidth = 4;
                    }

                    if(dwHeight < 4)
                    {
                        printf("*Warning* expanding texture height (%d->4)\n", dwHeight);
                        dwHeight = 4;
                    }

                    // Xbox DXT resources can encode redundant sub-4x4 mip levels.
                    // Keep their complete source chain for copying, but never ask
                    // the host D3D8 device for levels beyond its legal dimensions.
                    const DWORD dwHostMipMapLevels = static_cast<DWORD>(
                        cxbx::d3d::HostTextureMipLevelCount(dwWidth, dwHeight,
                                                            dwMipMapLevels));

#ifdef _DEBUG_TRACE
                    printf("CreateTexture(%d, %d, %d, 0, %d, D3DPOOL_MANAGED, 0x%.08X)\n", dwWidth, dwHeight,
                           dwHostMipMapLevels, Format, &pResource->EmuTexture8);
#endif

                    hRet = g_pD3DDevice8->CreateTexture(
                        dwWidth, dwHeight, dwHostMipMapLevels, 0, Format,
                        D3DPOOL_MANAGED, &pResource->EmuTexture8);

                    if(SUCCEEDED(hRet))
                        EmuRememberHostResource(pResource->EmuTexture8);

                    if(FAILED(hRet))
                    {
                        EmuWarning("Resource_Register: CreateTexture(%lu, %lu, %lu, format=0x%.08lX) failed (0x%.08lX); using a blank placeholder",
                                   dwWidth,
                                   dwHeight,
                                   dwHostMipMapLevels,
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
                        EmuRememberHostResource(pResource->EmuTexture8);

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

                D3DLOCKED_RECT LockedRect = {};

                // Stage 2: linear and swizzled textures fill their container
                // guest-direct (sourceCopy already holds the validated guest
                // bytes) and skip the host LockRect relay entirely -- the
                // host texture stays an empty metadata shell under the
                // native flavor, and level-0 locks serve the tracked guest
                // memory. CXBX_NO_GUEST_DIRECT_REG=1 restores the relay.
                const bool guestDirect =
                    dwCommonType == X_D3DCOMMON_TYPE_TEXTURE &&
                    X_Format != 0x24 &&
                    cxbx::d3d8::HostBackendRenders() &&
                    !EmuD3DEnvironmentEnabled("CXBX_NO_GUEST_DIRECT_REG");

                // ******************************************************************
                // * Copy over data (deswizzle if necessary)
                // ******************************************************************
                if(!guestDirect)
                {
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
                }

                RECT iRect = { 0, 0, 0, 0 };
                POINT iPoint = { 0, 0 };

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
                bool copiedSource =
                    EmuD3DCopyReadableRange(pBase, dwSourceSize, sourceCopy);
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
                else if(guestDirect)
                {
                    // Stage 2: the container is filled GUEST-DIRECT -- the
                    // host texture stays an empty metadata shell under the
                    // native flavor (the bind pull serves the container;
                    // level-0 locks serve the tracked guest memory).
                    XTL::IDirect3DBaseTexture8* pHostTex = pResource->EmuTexture8;
                    const UINT tightPitch = bCompressed
                        ? ((dwSourceWidth + 3) / 4) * dwCompressedBytesPerBlock
                        : dwSourceWidth * dwBPP;
                    const UINT blockRows = bCompressed
                        ? (dwSourceHeight + 3) / 4
                        : dwSourceHeight;
                    EmuNativeTexture* pNative = EmuNativeTextureEnsureGuestContainer(
                        pHostTex, pThis, dwSourceWidth, dwSourceHeight,
                        tightPitch, static_cast<UINT>(Format));
                    if(pNative != nullptr)
                    {
                        if(bSwizzled)
                        {
                            XTL::EmuXGUnswizzleRect(
                                sourceCopy.data(), dwSourceWidth, dwSourceHeight, dwDepth,
                                pNative->Pixels.data(), pNative->Pitch, iRect, iPoint, dwBPP);
                        }
                        else if(bCompressed)
                        {
                            const DWORD rowBytes =
                                ((dwSourceWidth + 3) / 4) * dwCompressedBytesPerBlock;
                            BYTE* dst = pNative->Pixels.data();
                            const BYTE* src = sourceCopy.data();
                            for(DWORD row = 0; row < blockRows; ++row)
                            {
                                memcpy(dst, src, rowBytes);
                                dst += pNative->Pitch;
                                src += rowBytes;
                            }
                        }
                        else
                        {
                            const UINT rowBytes = min(pNative->Pitch, dwPitch);
                            for(DWORD v = 0; v < dwSourceHeight; ++v)
                            {
                                memcpy(pNative->Pixels.data() +
                                           static_cast<std::size_t>(v) * pNative->Pitch,
                                       sourceCopy.data() +
                                           static_cast<std::size_t>(v) * dwPitch,
                                       rowBytes);
                            }
                            // Linear textures keep a live guest backing the
                            // title streams into per frame; track the source
                            // so SetTexture re-uploads current pixels on bind.
                            if(dwCommonType == X_D3DCOMMON_TYPE_TEXTURE &&
                               pResource->EmuTexture8 != NULL)
                            {
                                const BYTE* LiveSource = copiedSource
                                                             ? reinterpret_cast<const BYTE*>(pBase)
                                                             : reinterpret_cast<const BYTE*>(dwUnmaskedDataAddress);
                                EmuTrackLinearTexture(pResource->EmuTexture8,
                                                      LiveSource, dwPitch,
                                                      dwSourceWidth,
                                                      dwSourceHeight, dwBPP);
                            }
                        }
                    }
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
                    BYTE* pDest = (BYTE*)LockedRect.pBits;
                    const BYTE* pSrc = sourceCopy.data();

                    if((DWORD)LockedRect.Pitch == dwPitch && dwPitch == dwSourceWidth * dwBPP)
                        memcpy(pDest, pSrc, dwSourceWidth * dwSourceHeight * dwBPP);
                    else
                    {
                        // TODO: Faster copy (maybe unnecessary)
                        for(DWORD v = 0; v < dwSourceHeight; v++)
                        {
                            memcpy(pDest, pSrc, dwSourceWidth * dwBPP);

                            pDest += LockedRect.Pitch;
                            pSrc += dwPitch;
                        }
                    }

                    // Linear textures keep a live guest backing the title streams
                    // into per frame (e.g. an emulator core's framebuffer); track
                    // the source so SetTexture re-uploads current pixels on bind.
                    if(dwCommonType == X_D3DCOMMON_TYPE_TEXTURE &&
                       pResource->EmuTexture8 != NULL)
                    {
                        const BYTE* LiveSource = copiedSource
                                                     ? reinterpret_cast<const BYTE*>(pBase)
                                                     : reinterpret_cast<const BYTE*>(dwUnmaskedDataAddress);
                        EmuTrackLinearTexture(pResource->EmuTexture8, LiveSource,
                                              dwPitch, dwSourceWidth, dwSourceHeight,
                                              dwBPP);
                    }
                }

                // Hash-named dump of the CONVERTED level 0 -- exactly what the host
                // device will sample, after unswizzle/pitch adjustment.
                // (Guest-direct fills skip it: the host texture was never
                // locked, so there is nothing to dump from.)
                static int s_TextureDumpEnabled = -1;
                if(s_TextureDumpEnabled < 0)
                    s_TextureDumpEnabled =
                        EmuD3DEnvironmentEnabled("CXBX_D3D_TEXTURE_DUMP") ? 1 : 0;
                if(s_TextureDumpEnabled == 1 && !guestDirect)
                {
                    static LONG s_TextureDumpsRemaining = 256;
                    if(s_TextureDumpsRemaining > 0)
                    {
                        const DWORD sourceHash = sourceCopy.empty()
                                                     ? 0
                                                     : cxbx::d3d::ResourceContentHash(
                                                           sourceCopy.data(),
                                                           sourceCopy.size());
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

                if(!guestDirect)
                {
                    if(dwCommonType == X_D3DCOMMON_TYPE_SURFACE)
                        pResource->EmuSurface8->UnlockRect();
                    else
                        pResource->EmuTexture8->UnlockRect(0);
                }

                // Compressed mip relay: under the native flavor the host
                // texture is an empty shell and the renderer samples level
                // 0 only, so the mips are skipped.
                if(bCompressed && dwCommonType == X_D3DCOMMON_TYPE_TEXTURE &&
                   !sourceCopy.empty() && !guestDirect)
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
                                       "failed (0x%.08lX)",
                                       level, levelResult);
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
    return hRet;
}

} // namespace XTL

HRESULT WINAPI XTL::EmuIDirect3DResource8_Register(
    X_D3DResource* pThis,
    PVOID pBase)
{
    D3D_TRACE("Resource_Register");
    EmuSwapFS(); // Win2k/XP FS
    // A (re-)registration can re-back a guest resource the forward cache
    // keyed on; expire the cache rather than trusting old identities.
    EmuForwardCacheFlush();

    // A partially-HLE title may pass a resource whose fields (Common, Data,
    // Lock) contain garbage from uninitialized guest memory, causing an
    // access violation somewhere in the host resource creation/copy below.
    // Guard the entire body so a bad resource is left unbacked instead of
    // crashing the process. The body lives in its own function so its C++
    // objects (std::vector staging) stay legal alongside the SEH scope.
    HRESULT hRet = D3D_OK;
    __try
    {
        hRet = EmuResourceRegisterGuardedBody(pThis, pBase);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        EmuWarning("EmuIDirect3DResource8_Register: access violation (resource at 0x%.08X) -- left unbacked", (DWORD)pThis);
        hRet = D3D_OK;
    }

    // Slice C3: populate the native texture container at Register time so
    // the Vulkan flavor's binds serve a deterministically populated store
    // instead of adopting lazily at the first bind. RT/DS textures adopt
    // empty content (their samples come through stage-override views).
    if(SUCCEEDED(hRet) && cxbx::d3d8::HostBackendRenders() &&
       (pThis->Common & X_D3DCOMMON_TYPE_MASK) == X_D3DCOMMON_TYPE_TEXTURE &&
       pThis->EmuTexture8 != NULL)
    {
        EmuNativeTextureEnsureThreadsafe(pThis->EmuTexture8);
        // Slice C: attach the guest identity so binds/releases resolve the
        // container by the stable guest pointer.
        EmuNativeTextureAttachGuest(pThis->EmuTexture8, pThis);
    }

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DResource8_AddRef
// ******************************************************************
ULONG WINAPI XTL::EmuIDirect3DResource8_AddRef(
    X_D3DResource* pThis)
{
    D3D_TRACE("Resource_AddRef");
    EmuSwapFS(); // Win2k/XP FS

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

    EmuYuy2TextureInfo* pYuy2 = EmuFindYuy2Texture(pThis);
    if(pYuy2 != NULL)
    {
        uRet = ++pYuy2->RefCount;
        EmuSwapFS(); // XBox FS
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

    IDirect3DResource8* pResource8 = pThis->EmuResource8;

    if(pThis->Lock == 0x8000BEEF)
        uRet = ++pThis->Lock;
    else if(pResource8 != 0)
        uRet = pResource8->AddRef();

    EmuSwapFS(); // XBox FS

    return uRet;
}

// ******************************************************************
// * func: EmuIDirect3DResource8_Release
// ******************************************************************
ULONG WINAPI XTL::EmuIDirect3DResource8_Release(
    X_D3DResource* pThis)
{
    D3D_TRACE("Resource_Release");
    EmuSwapFS(); // Win2k/XP FS

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

    EmuYuy2TextureInfo* pYuy2 = EmuFindYuy2Texture(pThis);
    if(pYuy2 != NULL)
    {
        if(pYuy2->RefCount != 0)
        {
            uRet = --pYuy2->RefCount;
        }
        if(uRet == 0)
        {
            if(pYuy2->OwnsPixels)
            {
                // Report how far the decoder wrote past the nominal
                // Pitch*Height allocation owned by Cxbx.
                DWORD dwOverrun = 0;
                for(DWORD i = EMU_YUY2_TAIL_SLOP; i > 0; --i)
                {
                    if(pYuy2->pPixels[pYuy2->DataSize + i - 1] !=
                       EMU_YUY2_TAIL_FILL)
                    {
                        dwOverrun = i;
                        break;
                    }
                }

                if(dwOverrun != 0)
                {
                    printf("EmuD3D8 (0x%X): YUY2 texture %ux%u was overwritten 0x%X bytes past its %u-byte buffer.\n",
                           GetCurrentThreadId(), pYuy2->Width, pYuy2->Height,
                           dwOverrun, pYuy2->DataSize);
                }

                delete[] pYuy2->pPixels;
            }

            if(g_pYuvConvertedSource == pYuy2)
            {
                g_pYuvConvertedSource = NULL;
            }
            memset(pYuy2, 0, sizeof(*pYuy2));
        }

        EmuSwapFS(); // XBox FS
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

    IDirect3DResource8* pResource8 = pThis->EmuResource8;

    if(pThis->Lock == 0x8000BEEF)
    {
        delete[](PVOID)pThis->Data;
        uRet = --pThis->Lock;
    }
    else if(pResource8 != 0)
    {
        uRet = pResource8->Release();

        if(uRet == 0)
        {
            EmuForgetHostResource(pResource8);
#ifdef _DEBUG_TRACE
            printf("EmuIDirect3DResource8_Release (0x%X): Cleaned up a Resource!\n", GetCurrentThreadId());
#endif
            if((pThis->Common & X_D3DCOMMON_TYPE_MASK) == X_D3DCOMMON_TYPE_VERTEXBUFFER)
            {
                EmuDiscardRegisteredVertexBuffer(static_cast<X_D3DVertexBuffer*>(pThis));
                // Resource-lifetime parity for the buffer container: drop it
                // by the GUEST wrapper identity (slice C) and by the host
                // pointer, so neither can be recycled into an alias.
                EmuNativeBufferForgetGuest(pThis);
                EmuNativeBufferForget(pResource8);
            }
            else if((pThis->Common & X_D3DCOMMON_TYPE_MASK) == X_D3DCOMMON_TYPE_INDEXBUFFER)
            {
                EmuDiscardCreatedIndexBuffer(static_cast<X_D3DIndexBuffer*>(pThis));
                EmuNativeBufferForgetGuest(pThis);
                EmuNativeBufferForget(pResource8);
            }
            EmuDiscardSwizzledTexture(
                reinterpret_cast<IDirect3DBaseTexture8*>(pResource8));
            EmuDiscardLinearTexture(
                reinterpret_cast<IDirect3DBaseTexture8*>(pResource8));
            // Resource-lifetime parity: the guest resource is dying, and
            // the backend's texture cache is keyed by the guest resource
            // pointer (slice C), so drop the entries under that key. The
            // host pointer is NOT used -- it can already have been recycled.
            if((pThis->Common & X_D3DCOMMON_TYPE_MASK) == X_D3DCOMMON_TYPE_TEXTURE)
            {
                cxbx::d3d8::HostBackendReleaseTexture(pThis);
                // The host texture pointer dies here; drop its native
                // container and any outstanding served lock with it.
                EmuForgetTextureContainerState(
                    reinterpret_cast<XTL::IDirect3DBaseTexture8*>(pResource8));
                // Slice C: also drop the HLE byte container by the guest
                // identity (containers attached at Register carry it).
                EmuNativeTextureForgetGuest(pThis);
            }
            else if((pThis->Common & X_D3DCOMMON_TYPE_MASK) == X_D3DCOMMON_TYPE_SURFACE)
            {
                // Slice C: the target registry is keyed by the guest
                // surface pointer.
                cxbx::d3d8::HostBackendReleaseRenderTarget(pThis);
                // The host surface pointer dies here; drop its native
                // container and any outstanding served lock with it.
                {
                    std::lock_guard<std::mutex> lock(EmuSurfaceContainerMutex());
                    if(EmuSurfaceContainerLock* pOpen =
                           EmuFindSurfaceContainerLockLocked(pThis))
                    {
                        // A slot mid-commit releases itself when its copy
                        // ends; clearing it here could drop whichever claim
                        // took the slot over in the meantime.
                        if(!pOpen->Committing)
                        {
                            EmuReleaseSurfaceContainerLockSlotLocked(pOpen);
                        }
                    }
                }
                EmuNativeSurfaceForget(pThis);
            }
            if(EmuForgetOwnedGuestResource(pThis))
            {
                delete pThis;
            }
        }
    }

    EmuSwapFS(); // XBox FS

    return uRet;
}

// ******************************************************************
// * func: EmuIDirect3DResource8_IsBusy
// ******************************************************************
BOOL WINAPI XTL::EmuIDirect3DResource8_IsBusy(
    X_D3DResource* pThis)
{
    D3D_TRACE("Resource_IsBusy");
    EmuSwapFS(); // Win2k/XP FS

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

    IDirect3DResource8* pResource8 = pThis->EmuResource8;

    // I guess we arent doing anything, just return false..

    EmuSwapFS(); // XBox FS

    return FALSE;
}

// ******************************************************************
// * func: EmuGet2DSurfaceDesc
// ******************************************************************
VOID WINAPI XTL::EmuGet2DSurfaceDesc(
    X_D3DPixelContainer* pPixelContainer,
    DWORD dwLevel,
    X_D3DSURFACE_DESC* pDesc)
{
    D3D_TRACE("Get2DSurfaceDesc");
    EmuSwapFS(); // Win2k/XP FS

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

    if((pPixelContainer->Format & X_D3DFORMAT_CUBEMAP) != 0)
    {
        const DWORD cubeLevel = dwLevel == 0xFEFEFEFE ? 0 : dwLevel;
        hRet = pPixelContainer->EmuCubeTexture8->GetLevelDesc(
            cubeLevel, &SurfaceDesc);
    }
    else if(dwLevel == 0xFEFEFEFE)
    {
        hRet = pPixelContainer->EmuSurface8->GetDesc(&SurfaceDesc);
    }
    else
    {
        hRet = pPixelContainer->EmuTexture8->GetLevelDesc(dwLevel, &SurfaceDesc);
    }

    // ******************************************************************
    // * Rearrange into windows format (remove D3DPool)
    // ******************************************************************
    {
        // Convert Format (PC->Xbox)
        pDesc->Format = EmuPC2XB_D3DFormat(SurfaceDesc.Format);
        pDesc->Type = SurfaceDesc.Type;

        if(pDesc->Type > 7)
            EmuCleanup("EmuGet2DSurfaceDesc: pDesc->Type > 7");

        pDesc->Usage = SurfaceDesc.Usage;
        pDesc->Size = SurfaceDesc.Size;

        // TODO: Convert from Xbox to PC!!
        if(SurfaceDesc.MultiSampleType == D3DMULTISAMPLE_NONE)
            pDesc->MultiSampleType = (XTL::D3DMULTISAMPLE_TYPE)0x0011;
        else
            EmuCleanup("EmuGet2DSurfaceDesc Unknown Multisample format! (%d)", SurfaceDesc.MultiSampleType);

        pDesc->Width = SurfaceDesc.Width;
        pDesc->Height = SurfaceDesc.Height;
    }

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuGet2DSurfaceDescD
// ******************************************************************
VOID WINAPI XTL::EmuGet2DSurfaceDescD(
    X_D3DPixelContainer* pPixelContainer,
    X_D3DSURFACE_DESC* pDesc)
{
    D3D_TRACE("Get2DSurfaceDescD");
// ******************************************************************
// * debug trace
// ******************************************************************
#ifdef _DEBUG_TRACE
    {
        EmuSwapFS(); // Win2k/XP FS
        printf("EmuD3D8 (0x%X): EmuGet2DSurfaceDescD\n"
               "(\n"
               "   pPixelContainer     : 0x%.08X\n"
               "   pDesc               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pPixelContainer, pDesc);
        EmuSwapFS(); // Xbox FS
    }
#endif

    EmuGet2DSurfaceDesc(pPixelContainer, 0xFEFEFEFE, pDesc);

    return;
}

extern "C" BYTE* EmuResolvePhysicalHostSpan(ULONG Address, ULONG Size); // emulation_runtime.cpp

// A guest library can pass a resource pointer through the 0x8xxxxxxx physical
// alias (Xbox D3D's CPU-visible AGP convention -- Turok's XMV movie player
// hands its overlay surface to LockRect/GetDesc this way). Guest code gets
// such reads trap-emulated; host wrapper code does not, so translate to the
// host-visible span before the first field access. Returns NULL when the
// alias resolves to nothing readable.
static bool EmuIsReadableResourceHeader(const void* Pointer)
{
    MEMORY_BASIC_INFORMATION Memory = {};
    return VirtualQuery(Pointer, &Memory, sizeof(Memory)) == sizeof(Memory) &&
           Memory.State == MEM_COMMIT &&
           (Memory.Protect & (PAGE_READONLY | PAGE_READWRITE |
                              PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                              PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)) != 0;
}

static XTL::X_D3DResource* EmuResolveAliasedResource(XTL::X_D3DResource* pThis)
{
    // A registered YUY2 handle IS an aliased value by design (the fake movie
    // texture is the host pixel buffer with the high bit set, and the
    // registry keys on exactly that value). Translating it before the lookup
    // made every YUY2 LockRect/GetDesc miss the registry and fall into the
    // host-surface path -- the movie decoder then never received its real
    // write pointer and the intro stayed a solid green (zeroed YUY2). Honor
    // the registry identity first; translate only unknown aliases.
    if(EmuFindYuy2Texture(pThis) != NULL)
        return pThis;

    // Physically-aliased pointers (Xbox D3D's CPU-visible AGP convention)
    // translate to their host span first.
    if((DWORD)(uintptr_t)pThis >= 0x80000000)
    {
        pThis = (XTL::X_D3DResource*)EmuResolvePhysicalHostSpan(
            (ULONG)(uintptr_t)pThis, sizeof(XTL::X_D3DPixelContainer));
        if(pThis == NULL)
        {
            EmuWarning("Aliased resource did not resolve to host memory");
            return NULL;
        }
    }

    // Require committed, readable memory before the wrapper dereferences the
    // header. Turok's XMV player builds its video surface header inside a
    // buffer whose guest address is not always host-committed (the value
    // shifts with module layout, so some builds crash and others do not);
    // refusing the pointer drops one call instead of killing the title.
    if(!EmuIsReadableResourceHeader(pThis))
    {
        EmuWarning("Resource header 0x%.08X is not readable host memory",
                   (DWORD)(uintptr_t)pThis);
        return NULL;
    }

    return pThis;
}

// ******************************************************************
// * func: EmuIDirect3DSurface8_GetDesc
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DSurface8_GetDesc(
    X_D3DResource* pThis,
    X_D3DSURFACE_DESC* pDesc)
{
    D3D_TRACE("Surface_GetDesc");
    EmuSwapFS(); // Win2k/XP FS

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

    // YUY2 movie handles are raw pixel pointers with the high bit set; the
    // registry lookup must run before any translation or header dereference
    // (EmuResolveYuy2Texture is deref-safe for them).
    EmuYuy2TextureInfo* pYuy2 = EmuResolveYuy2Texture(pThis);
    if(pYuy2 == NULL)
    {
        pThis = EmuResolveAliasedResource(pThis);
        if(pThis == NULL)
        {
            EmuSwapFS(); // XBox FS
            return D3DERR_INVALIDCALL;
        }
    }

    if(pYuy2 != NULL)
    {
        pDesc->Format = EmuPC2XB_D3DFormat(D3DFMT_YUY2);
        pDesc->Height = pYuy2->Height;
        pDesc->Width = pYuy2->Width;
        pDesc->MultiSampleType = (D3DMULTISAMPLE_TYPE)0;
        pDesc->Size = pYuy2->Pitch * pYuy2->Height;
        pDesc->Type = D3DRTYPE_SURFACE;
        pDesc->Usage = 0;

        hRet = D3D_OK;
    }
    else
    {
        D3DSURFACE_DESC SurfaceDesc;

        // The host-interface field is a union with the Xbox header's Lock
        // slot, which the title owns. A resource built by guest code that was
        // never (successfully) registered still carries the TITLE's value
        // there -- Turok's XMV player hands such a surface straight to
        // GetDesc, and calling through it is a wild vtable jump. Only
        // dereference interfaces this emulator actually created.
        X_D3DPixelContainer* pPixelContainer =
            reinterpret_cast<X_D3DPixelContainer*>(pThis);
        const bool bCubemap = (pPixelContainer->Format & X_D3DFORMAT_CUBEMAP) != 0;
        void* pHostInterface = bCubemap
                                   ? (void*)pPixelContainer->EmuCubeTexture8
                                   : (void*)pThis->EmuSurface8;
        if(!EmuIsKnownHostResource(pHostInterface))
        {
            EmuWarning("Surface_GetDesc on unregistered resource 0x%.08X "
                       "(host field 0x%.08X); refusing",
                       (DWORD)(uintptr_t)pThis, (DWORD)(uintptr_t)pHostInterface);
            EmuSwapFS(); // XBox FS
            return D3DERR_INVALIDCALL;
        }

        if(bCubemap)
        {
            hRet = pPixelContainer->EmuCubeTexture8->GetLevelDesc(
                0, &SurfaceDesc);
        }
        else
        {
            hRet = pThis->EmuSurface8->GetDesc(&SurfaceDesc);
        }

        // ******************************************************************
        // * Rearrange into windows format (remove D3DPool)
        // ******************************************************************
        {
            // Convert Format (PC->Xbox)
            pDesc->Format = EmuPC2XB_D3DFormat(SurfaceDesc.Format);
            pDesc->Type = SurfaceDesc.Type;

            if(pDesc->Type > 7)
                EmuCleanup("EmuIDirect3DSurface8_GetDesc: pDesc->Type > 7");

            pDesc->Usage = SurfaceDesc.Usage;
            pDesc->Size = SurfaceDesc.Size;

            // TODO: Convert from Xbox to PC!!
            if(SurfaceDesc.MultiSampleType == D3DMULTISAMPLE_NONE)
                pDesc->MultiSampleType = (XTL::D3DMULTISAMPLE_TYPE)0x0011;
            else
                EmuCleanup("EmuIDirect3DSurface8_GetDesc Unknown Multisample format! (%d)", SurfaceDesc.MultiSampleType);

            pDesc->Width = SurfaceDesc.Width;
            pDesc->Height = SurfaceDesc.Height;
        }
    }

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DSurface8_LockRect
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DSurface8_LockRect(
    X_D3DResource* pThis,
    D3DLOCKED_RECT* pLockedRect,
    CONST RECT* pRect,
    DWORD Flags)
{
    D3D_TRACE("Surface_LockRect");
    EmuSwapFS(); // Win2k/XP FS

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

    HRESULT hRet = D3DERR_INVALIDCALL;

    if(pThis == NULL || pLockedRect == NULL)
    {
        EmuSwapFS(); // XBox FS
        return hRet;
    }

    // The YUY2 short-circuit MUST precede every header dereference: a movie
    // texture handle is a raw pixel pointer with the high bit set, not a
    // readable X_D3DResource (EmuResolveYuy2Texture itself is deref-safe).
    {
        EmuYuy2TextureInfo* pYuy2Early = EmuResolveYuy2Texture(pThis);
        if(pYuy2Early != NULL)
        {
            ZeroMemory(pLockedRect, sizeof(*pLockedRect));
            hRet = EmuLockYuy2Texture(pYuy2Early, pLockedRect, pRect);
            EmuSwapFS(); // XBox FS
            return hRet;
        }
    }

    pThis = EmuResolveAliasedResource(pThis);
    if(pThis == NULL)
    {
        EmuSwapFS(); // XBox FS
        return hRet;
    }

    const X_D3DPixelContainer* pPixelContainer =
        reinterpret_cast<const X_D3DPixelContainer*>(pThis);
    const bool isCubemapSurface =
        (pThis->Common & X_D3DCOMMON_TYPE_MASK) == X_D3DCOMMON_TYPE_SURFACE &&
        (pPixelContainer->Format & X_D3DFORMAT_CUBEMAP) != 0;

    if(isCubemapSurface && EmuD3DTexTraceEnabled())
    {
        printf("TEX| surface-lock resource=0x%.08lX common=0x%.08lX "
               "format=0x%.08lX data=0x%.08lX host=0x%.08lX "
               "face=%lu rect=0x%.08lX flags=0x%.08lX\n",
               reinterpret_cast<unsigned long>(pThis),
               static_cast<unsigned long>(pThis->Common),
               static_cast<unsigned long>(pPixelContainer->Format),
               static_cast<unsigned long>(pThis->Data),
               reinterpret_cast<unsigned long>(pThis->EmuSurface8),
               static_cast<unsigned long>(
                   EmuRegisteredCubemapSurfaceFace(pThis)),
               reinterpret_cast<unsigned long>(pRect),
               static_cast<unsigned long>(Flags));
    }

    ZeroMemory(pLockedRect, sizeof(*pLockedRect));

    // Native-Vulkan migration P2: the guest-visible backbuffer is the Vulkan
    // render target. Read-only locks are served from the readback shadow;
    // write locks have nowhere meaningful to land and fail explicitly.
    if(cxbx::d3d8::HostBackendRenders())
    {
        IDirect3DSurface8* pHostBackBuffer = NULL;
        bool isBackBuffer = false;
        __try
        {
            if(g_pD3DDevice8 != NULL &&
               SUCCEEDED(g_pD3DDevice8->GetBackBuffer(
                   0, XTL::D3DBACKBUFFER_TYPE_MONO, &pHostBackBuffer)) &&
               pHostBackBuffer != NULL)
            {
                isBackBuffer = (pHostBackBuffer == pThis->EmuSurface8);
                pHostBackBuffer->Release();
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            isBackBuffer = false;
        }
        if(isBackBuffer)
        {
            if((Flags & EMU_D3DLOCK_READONLY) == 0)
            {
                EmuWarning("Surface_LockRect rejected a write lock on the "
                           "Vulkan-rendered backbuffer");
                EmuSwapFS(); // XBox FS
                return D3DERR_INVALIDCALL;
            }
            if(EmuVulkanSyncBackbufferShadow())
            {
                const RECT fullRect = { 0, 0,
                                        (LONG)g_EmuVulkanShadowWidth,
                                        (LONG)g_EmuVulkanShadowHeight };
                const RECT& lockRect = pRect != NULL ? *pRect : fullRect;
                pLockedRect->pBits =
                    g_EmuVulkanShadow +
                    static_cast<std::size_t>(lockRect.top) *
                        g_EmuVulkanShadowPitch +
                    static_cast<std::size_t>(lockRect.left) * 4;
                pLockedRect->Pitch = (LONG)g_EmuVulkanShadowPitch;
                EmuSwapFS(); // XBox FS
                return D3D_OK;
            }
            EmuSwapFS(); // XBox FS
            return D3DERR_INVALIDCALL;
        }
    }

    EmuYuy2TextureInfo* pYuy2 = EmuResolveYuy2Texture(pThis);
    if(pYuy2 != NULL)
    {
        hRet = EmuLockYuy2Texture(pYuy2, pLockedRect, pRect);
    }
    else if(isCubemapSurface)
    {
        DWORD newFlags = 0;
        if((Flags & EMU_D3DLOCK_READONLY) != 0)
        {
            newFlags |= D3DLOCK_READONLY;
        }

        if((Flags & ~(EMU_D3DLOCK_READONLY | EMU_D3DLOCK_TILED)) != 0)
        {
            EmuCleanup(
                "EmuIDirect3DSurface8_LockRect: Unknown Flags! (0x%.08X)",
                Flags);
        }

        if((Flags & EMU_D3DLOCK_TILED) != 0)
        {
            EmuWarning(
                "Surface_LockRect rejected a tiled lock on registered cubemap "
                "0x%.08X",
                reinterpret_cast<DWORD>(pThis));
        }
        else
        {
            const XTL::D3DCUBEMAP_FACES face =
                static_cast<XTL::D3DCUBEMAP_FACES>(
                    EmuRegisteredCubemapSurfaceFace(pThis));
            __try
            {
                // A surface-tagged cubemap header exposes the first contiguous
                // face through the surface LockRect ABI. Release a previous
                // host lock just like the ordinary surface path, then map that
                // view to the host cube's positive-X face.
                pThis->EmuCubeTexture8->UnlockRect(face, 0);
                hRet = pThis->EmuCubeTexture8->LockRect(
                    face, 0, pLockedRect, pRect, newFlags);
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                ZeroMemory(pLockedRect, sizeof(*pLockedRect));
                hRet = D3DERR_INVALIDCALL;
                EmuWarning(
                    "Surface_LockRect caught a host fault for registered "
                    "cubemap 0x%.08X",
                    reinterpret_cast<DWORD>(pThis));
            }
        }
    }
    else
    {
        IDirect3DSurface8* pSurface8 = pThis->EmuSurface8;
        if(pSurface8 == NULL)
        {
            EmuWarning("Surface_LockRect rejected an unbacked surface at 0x%.08X",
                       reinterpret_cast<DWORD>(pThis));
            EmuSwapFS(); // XBox FS
            return hRet;
        }

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

        // P8 C3-surface: a partial writable lock bypasses the container (the
        // host path below writes the host surface directly); the container
        // would diverge, so drop it and let a later full lock re-seed.
        // Read-only and d3d8-flavor locks keep the host path throughout.
        if(cxbx::d3d8::HostBackendRenders() &&
           (Flags & EMU_D3DLOCK_TILED) == 0 &&
           (NewFlags & D3DLOCK_READONLY) == 0 && pRect != NULL)
        {
            EmuNativeSurfaceForget(pThis);
        }

        if(!(Flags & EMU_D3DLOCK_TILED) || FAILED(hRet))
        {
            // Host D3D can fault internally when a partially-HLE title hands
            // us a stale surface interface during streaming. Keep that host
            // failure inside the HLE boundary so the guest can observe an
            // ordinary failed lock instead of losing the entire process.
            __try
            {
                EmuFlushTiledSurfaceLock(pThis);
                EmuCommitSurfaceContainerLock(pThis);

                const EmuSwizzledSurfaceLink* pLink = EmuFindSwizzledSurfaceLink(pSurface8);

                // Commit any pending Morton-order write before re-locking.
                if(pLink != nullptr)
                    EmuCommitSwizzledTexture(pLink->pHostTexture);

                // Remove old lock(s)
                pSurface8->UnlockRect();

                hRet = pSurface8->LockRect(pLockedRect, pRect, NewFlags);

                // A title that decodes video straight into a locked surface
                // copies Pitch*Height bytes as the GUEST understands them. If
                // the host surface is narrower or shorter, that memcpy runs
                // off the end of the host allocation and faults -- the frame
                // never lands and the surface stays blank. Report both
                // geometries so the mismatch is visible instead of appearing
                // as an unexplained access violation inside VCRUNTIME memcpy.
                if(SUCCEEDED(hRet) && EmuD3DTexTraceEnabled())
                {
                    D3DSURFACE_DESC HostDesc = {};
                    const bool GotHost = SUCCEEDED(pSurface8->GetDesc(&HostDesc));
                    const DWORD GuestWidth =
                        (pPixelContainer->Size & X_D3DSIZE_WIDTH_MASK) + 1;
                    const DWORD GuestHeight =
                        ((pPixelContainer->Size & X_D3DSIZE_HEIGHT_MASK) >>
                         X_D3DSIZE_HEIGHT_SHIFT) +
                        1;
                    const DWORD GuestPitch =
                        (((pPixelContainer->Size & X_D3DSIZE_PITCH_MASK) >>
                          X_D3DSIZE_PITCH_SHIFT) +
                         1) *
                        64;
                    printf("TEX| surface-lock resource=0x%.08lX format=0x%.08lX "
                           "size=0x%.08lX guest=%lux%lu pitch=%lu host=%lux%lu "
                           "hostfmt=%d lockpitch=%ld bits=0x%.08lX\n",
                           reinterpret_cast<unsigned long>(pThis),
                           static_cast<unsigned long>(pPixelContainer->Format),
                           static_cast<unsigned long>(pPixelContainer->Size),
                           GuestWidth, GuestHeight, GuestPitch,
                           GotHost ? (unsigned long)HostDesc.Width : 0ul,
                           GotHost ? (unsigned long)HostDesc.Height : 0ul,
                           GotHost ? (int)HostDesc.Format : -1,
                           (long)pLockedRect->Pitch,
                           reinterpret_cast<unsigned long>(pLockedRect->pBits));
                    fflush(stdout);
                }

                // A writable full lock of a swizzled texture level receives the
                // title's Morton-order texels; mark it for unswizzling exactly
                // like the texture-lock path.
                if(SUCCEEDED(hRet) && pLink != nullptr && pRect == NULL &&
                   (NewFlags & D3DLOCK_READONLY) == 0)
                {
                    EmuSwizzledTextureInfo* pSwizzled =
                        EmuFindSwizzledTexture(pLink->pHostTexture);
                    if(pSwizzled != nullptr &&
                       pLink->dwLevel < EMU_SWIZZLED_TEXTURE_MAX_LEVELS)
                    {
                        pSwizzled->dwDirtyLevels |= 1u << pLink->dwLevel;
                        pSwizzled->bLockPathUsed = true;
                        pSwizzled->Levels[pLink->dwLevel].pBits = pLockedRect->pBits;
                        pSwizzled->Levels[pLink->dwLevel].Pitch = pLockedRect->Pitch;
                        if(EmuD3DTexTraceEnabled())
                            printf("TEX| surface-lock dirty host_tex=0x%.08lX level=%lu pitch=%ld\n",
                                   reinterpret_cast<unsigned long>(pLink->pHostTexture),
                                   static_cast<unsigned long>(pLink->dwLevel),
                                   static_cast<long>(pLockedRect->Pitch));
                    }
                }

                // A writable full lock through a level surface updates the
                // host texture behind the container's back: invalidate the
                // capture so the bind pull falls back to the host texture
                // (unless a served lock is outstanding).
                if(SUCCEEDED(hRet) && pLink != nullptr && pLink->dwLevel == 0 &&
                   pRect == NULL && (NewFlags & D3DLOCK_READONLY) == 0 &&
                   cxbx::d3d8::HostBackendRenders() &&
                   EmuFindTextureContainerLockByHost(pLink->pHostTexture) ==
                       nullptr)
                {
                    EmuForgetTextureContainerState(pLink->pHostTexture);
                }

                // P8 C3-surface: re-serve a granted writable full lock from
                // the container -- the guest edits native bytes and commits
                // push the rows back at the shared flush points. Only locks
                // the host granted reach this path, and UMA-aliased lock
                // bits (guest-RAM-backed surfaces) are excluded below the
                // seeding step. Swizzled levels keep their own commit path
                // above.
                // CXBX_NO_SURFACE_CONTAINER=1 restores the pure host path.
                if(SUCCEEDED(hRet) && pRect == NULL && pLink == nullptr &&
                   cxbx::d3d8::HostBackendRenders() &&
                   (NewFlags & D3DLOCK_READONLY) == 0 &&
                   !EmuD3DEnvironmentEnabled("CXBX_NO_SURFACE_CONTAINER"))
                {
                    if(EmuSurfaceContainerTraceEnabled())
                    {
                        printf("SURF| gate tid=%X res=0x%.08lX host=0x%.08lX "
                               "serving\n",
                               GetCurrentThreadId(),
                               reinterpret_cast<unsigned long>(pThis),
                               reinterpret_cast<unsigned long>(pThis->EmuSurface8));
                        fflush(stdout);
                    }
                    EmuServeSurfaceContainerFromLock(pThis, pSurface8,
                                                     pLockedRect, true);
                }
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                ZeroMemory(pLockedRect, sizeof(*pLockedRect));
                hRet = D3DERR_INVALIDCALL;
                EmuWarning("Surface_LockRect caught a host fault for surface 0x%.08X",
                           reinterpret_cast<DWORD>(pThis));
            }

            if(FAILED(hRet))
                printf("*Warning* LockRect failed\n");
        }
    }

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DBaseTexture8_GetLevelCount
// ******************************************************************
DWORD WINAPI XTL::EmuIDirect3DBaseTexture8_GetLevelCount(
    X_D3DBaseTexture* pThis)
{
    D3D_TRACE("BaseTexture_GetLevelCount");
    EmuSwapFS(); // Win2k/XP FS

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

    if(EmuResolveYuy2Texture(pThis) != NULL)
    {
        EmuSwapFS(); // XBox FS
        return 1;
    }

    IDirect3DBaseTexture8* pBaseTexture8 = pThis->EmuBaseTexture8;

    DWORD dwRet = pBaseTexture8->GetLevelCount();

    EmuSwapFS(); // XBox FS

    return dwRet;
}

// ******************************************************************
// * func: EmuIDirect3DTexture8_GetSurfaceLevel2
// ******************************************************************
XTL::X_D3DResource* WINAPI XTL::EmuIDirect3DTexture8_GetSurfaceLevel2(
    X_D3DTexture* pThis,
    UINT Level)
{
    D3D_TRACE("Texture_GetSurfaceLevel2");
    X_D3DSurface* pSurfaceLevel;

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
HRESULT WINAPI XTL::EmuIDirect3DTexture8_LockRect(
    X_D3DTexture* pThis,
    UINT Level,
    D3DLOCKED_RECT* pLockedRect,
    CONST RECT* pRect,
    DWORD Flags)
{
    D3D_TRACE("Texture_LockRect");
    EmuSwapFS(); // Win2k/XP FS

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

    EmuYuy2TextureInfo* pYuy2 = EmuResolveYuy2Texture(pThis);
    if(pYuy2 != NULL)
    {
        HRESULT hRet = (Level == 0) ? EmuLockYuy2Texture(pYuy2, pLockedRect, pRect)
                                    : D3DERR_INVALIDCALL;
        EmuSwapFS(); // XBox FS
        return hRet;
    }

    IDirect3DTexture8* pTexture8 = pThis->EmuTexture8;

    DWORD NewFlags = 0;

    if(Flags & 0x80)
        NewFlags |= D3DLOCK_READONLY;

    if(Flags & !(0x80 | 0x40))
        EmuCleanup("EmuIDirect3DTexture8_LockRect: Unknown Flags! (0x%.08X)", Flags);

    // Commit any pending Morton-order write before releasing its lock.
    EmuCommitSwizzledTexture(pTexture8);

    // Stage 2 pilot: registered linear textures serve their level-0 locks
    // from the tracked guest UMA memory (hardware-accurate: on Xbox the
    // lock returns the same memory the title registered, and the bind's
    // live refresh picks the writes up). Full locks only; sub-rects and
    // deeper levels keep the host path. The kill switch matches Register's.
    if(Level == 0 && pRect == NULL && pTexture8 != NULL &&
       cxbx::d3d8::HostBackendRenders() &&
       !EmuD3DEnvironmentEnabled("CXBX_NO_GUEST_DIRECT_REG"))
    {
        LONG guestPitch = 0;
        if(const BYTE* pGuestBits =
               EmuLinearTrackedGuestData(pTexture8, &guestPitch))
        {
            pLockedRect->pBits = const_cast<BYTE*>(pGuestBits);
            pLockedRect->Pitch = guestPitch;
            EmuSwapFS(); // XBox FS
            return D3D_OK;
        }
    }

    // Remove old lock(s)
    pTexture8->UnlockRect(Level);

    HRESULT hRet = pTexture8->LockRect(Level, pLockedRect, pRect, NewFlags);

    // A writable full-level lock of a swizzled-format texture receives the
    // title's pre-swizzled (Morton-order) texels; remember the lock so the
    // block can be unswizzled in place when the lock is flushed.
    if(SUCCEEDED(hRet) && pRect == NULL && (NewFlags & D3DLOCK_READONLY) == 0)
    {
        EmuSwizzledTextureInfo* pSwizzled = EmuFindSwizzledTexture(pTexture8);
        if(pSwizzled != NULL && Level < EMU_SWIZZLED_TEXTURE_MAX_LEVELS)
        {
            pSwizzled->dwDirtyLevels |= 1u << Level;
            pSwizzled->bLockPathUsed = true;
            pSwizzled->Levels[Level].pBits = pLockedRect->pBits;
            pSwizzled->Levels[Level].Pitch = pLockedRect->Pitch;
        }
    }

    // P8 C3-texture: re-serve a granted writable full level-0 lock from the
    // container for plain (non-swizzled) textures -- the guest edits native
    // bytes and commits push the rows back at the shared flush points; the
    // bind pull reads the container. Swizzled textures keep their Morton
    // machinery above; the d3d8 flavor and every other lock shape keep the
    // host path. CXBX_NO_TEXTURE_CONTAINER=1 restores the pure host path.
    if(SUCCEEDED(hRet) && Level == 0 && pRect == NULL &&
       (NewFlags & D3DLOCK_READONLY) == 0 && pTexture8 != NULL &&
       cxbx::d3d8::HostBackendRenders() &&
       !EmuD3DEnvironmentEnabled("CXBX_NO_TEXTURE_CONTAINER") &&
       EmuFindSwizzledTexture(pTexture8) == NULL)
    {
        EmuServeTextureContainerFromLock(pThis, pTexture8, pLockedRect);
    }

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DTexture8_GetSurfaceLevel
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DTexture8_GetSurfaceLevel(
    X_D3DTexture* pThis,
    UINT Level,
    X_D3DSurface** ppSurfaceLevel)
{
    D3D_TRACE("Texture_GetSurfaceLevel");
    EmuSwapFS(); // Win2k/XP FS

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
    if(EmuResolveYuy2Texture(pThis) != NULL)
    {
        *ppSurfaceLevel = (X_D3DSurface*)pThis;
        EmuIDirect3DResource8_AddRef(pThis);
        hRet = D3D_OK;
    }
    else
    {
        IDirect3DTexture8* pTexture8 = pThis->EmuTexture8;

        *ppSurfaceLevel = new X_D3DSurface();
        EmuRememberOwnedGuestResource(*ppSurfaceLevel);

        hRet = pTexture8->GetSurfaceLevel(Level, &((*ppSurfaceLevel)->EmuSurface8));
        if(SUCCEEDED(hRet))
            EmuRememberHostResource((*ppSurfaceLevel)->EmuSurface8);

        // Surface writes to a swizzled texture level must trigger the same
        // Morton-order commit as texture locks; remember the association.
        // Every level surface is linked (not only swizzled ones): the
        // container capture must know which surfaces belong to a texture so
        // level-surface writes route around (or invalidate) the capture.
        const EmuSwizzledTextureInfo* pSwizzled = EmuFindSwizzledTexture(pTexture8);
        if(SUCCEEDED(hRet) && pSwizzled != nullptr)
        {
            EmuLinkSwizzledSurface((*ppSurfaceLevel)->EmuSurface8, pTexture8,
                                   Level, pThis);

            // Give the surface the same guest-visible header a native upload
            // path (guest D3DX) expects: the parent's format and the level's
            // Morton block inside the parent's Data allocation.
            DWORD dwLevelOffset = 0;
            for(DWORD level = 0; level < Level; ++level)
            {
                const DWORD w = max(1u, pSwizzled->dwWidth >> level);
                const DWORD h = max(1u, pSwizzled->dwHeight >> level);
                dwLevelOffset += w * h * pSwizzled->dwBPP;
            }

            (*ppSurfaceLevel)->Common = X_D3DCOMMON_TYPE_SURFACE | 1;
            (*ppSurfaceLevel)->Data =
                (pSwizzled->pGuestData != nullptr && dwLevelOffset < pSwizzled->dwGuestSize)
                    ? reinterpret_cast<DWORD>(pSwizzled->pGuestData + dwLevelOffset)
                    : 0;
            (*ppSurfaceLevel)->Format = pThis->Format;
            (*ppSurfaceLevel)->Size = pThis->Size;

            if(EmuD3DTexTraceEnabled())
                printf("TEX| surface-link host_tex=0x%.08lX level=%u surface=0x%.08lX data=0x%.08lX\n",
                       reinterpret_cast<unsigned long>(pTexture8), Level,
                       reinterpret_cast<unsigned long>((*ppSurfaceLevel)->EmuSurface8),
                       static_cast<unsigned long>((*ppSurfaceLevel)->Data));
        }
        else if(SUCCEEDED(hRet) && cxbx::d3d8::HostBackendRenders())
        {
            // Plain texture: still linked, so level-surface locks invalidate
            // the texture capture instead of diverging from it.
            EmuLinkSwizzledSurface((*ppSurfaceLevel)->EmuSurface8, pTexture8,
                                   Level, pThis);
        }
    }

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DVolumeTexture8_LockBox
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DVolumeTexture8_LockBox(
    X_D3DVolumeTexture* pThis,
    UINT Level,
    D3DLOCKED_BOX* pLockedVolume,
    CONST D3DBOX* pBox,
    DWORD Flags)
{
    D3D_TRACE("VolumeTexture_LockBox");
    EmuSwapFS(); // Win2k/XP FS

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

    IDirect3DVolumeTexture8* pVolumeTexture8 = pThis->EmuVolumeTexture8;

    HRESULT hRet = pVolumeTexture8->LockBox(Level, pLockedVolume, pBox, Flags);

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DCubeTexture8_LockRect
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DCubeTexture8_LockRect(
    X_D3DCubeTexture* pThis,
    D3DCUBEMAP_FACES FaceType,
    UINT Level,
    D3DLOCKED_RECT* pLockedBox,
    CONST RECT* pRect,
    DWORD Flags)
{
    D3D_TRACE("CubeTexture_LockRect");
    EmuSwapFS(); // Win2k/XP FS

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

    IDirect3DCubeTexture8* pCubeTexture8 = pThis->EmuCubeTexture8;
    if(pCubeTexture8 == NULL)
    {
        // The 5849 LockRect redirect sits on PixelJar's shared Lock2DSurface
        // internal (the cube export is a bare jmp thunk); a non-cube caller
        // that reaches it must fail gracefully rather than dereference a
        // missing host cube.
        return D3DERR_INVALIDCALL;
    }

    HRESULT hRet = pCubeTexture8->LockRect(FaceType, Level, pLockedBox, pRect, Flags);

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_Release
// ******************************************************************
ULONG WINAPI XTL::EmuIDirect3DDevice8_Release()
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return uRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateVertexBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreateVertexBuffer(
    UINT Length,
    DWORD Usage,
    DWORD FVF,
    D3DPOOL Pool,
    X_D3DVertexBuffer** ppVertexBuffer)
{
    D3D_TRACE("CreateVertexBuffer");
    *ppVertexBuffer = EmuIDirect3DDevice8_CreateVertexBuffer2(Length);

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateVertexBuffer2
// ******************************************************************
XTL::X_D3DVertexBuffer* WINAPI XTL::EmuIDirect3DDevice8_CreateVertexBuffer2(
    UINT Length)
{
    EmuSwapFS(); // Win2k/XP FS
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

    X_D3DVertexBuffer* pD3DVertexBuffer = new X_D3DVertexBuffer();
    EmuRememberOwnedGuestResource(pD3DVertexBuffer);

    IDirect3DVertexBuffer8* ppVertexBuffer = NULL;

    HRESULT hRet = g_pD3DDevice8->CreateVertexBuffer(
        Length,
        0,
        0,
        D3DPOOL_MANAGED,
        &pD3DVertexBuffer->EmuVertexBuffer8);

    EmuSwapFS(); // XBox FS

    return pD3DVertexBuffer;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_EnableOverlay
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_EnableOverlay(
    BOOL Enable)
{
    EmuSwapFS(); // Win2k/XP FS
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
        g_pOverlayFrameBgra = nullptr;
        g_dwOverlayFrameW = 0;
        g_dwOverlayFrameH = 0;
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
                ddsd2.ddpfPixelFormat.dwFourCC = MAKEFOURCC('Y', 'U', 'Y', '2');

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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_UpdateOverlay
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_UpdateOverlay(
    X_D3DSurface* pSurface,
    CONST RECT* SrcRect,
    CONST RECT* DstRect,
    BOOL EnableColorKey,
    D3DCOLOR ColorKey)
{
    D3D_TRACE("UpdateOverlay");
    EmuSwapFS(); // Win2k/XP FS

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
    // UpdateOverlay reads the surface bytes directly, so it is a container
    // flush point: push any outstanding served lock's rows back into the
    // host surface first, or the overlay shows the seed frame forever.
    EmuCommitSurfaceContainerLocks();
    if(g_bSupportsYUY2)
    {
        D3DSURFACE_DESC SurfaceDesc;
        DDSURFACEDESC2 ddsd2;
        D3DLOCKED_RECT LockedRect;

        pSurface->EmuSurface8->GetDesc(&SurfaceDesc);

        ZeroMemory(&ddsd2, sizeof(ddsd2));

        ddsd2.dwSize = sizeof(ddsd2);

        g_pDDSOverlay7->Lock(NULL, &ddsd2, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL);
        pSurface->EmuSurface8->UnlockRect();

        pSurface->EmuSurface8->LockRect(&LockedRect, NULL, NULL);

        // Copy Data
        {
            char* pDest = (char*)ddsd2.lpSurface;
            char* pSour = (char*)LockedRect.pBits;

            int w = SurfaceDesc.Width;
            int h = SurfaceDesc.Height;

            // TODO: sucker the game into rendering directly to the overlay :]
            for(int y = 0; y < h; y++)
            {
                memcpy(pDest, pSour, w * 2);

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
        RECT SourRect = { 0, 0, (LONG)g_dwOverlayW, (LONG)g_dwOverlayH }, DestRect;

        int nTitleHeight = GetSystemMetrics(SM_CYCAPTION);
        int nBorderWidth = GetSystemMetrics(SM_CXSIZEFRAME);
        int nBorderHeight = GetSystemMetrics(SM_CYSIZEFRAME);

        GetWindowRect(g_hEmuWindow, &DestRect);

        DestRect.left += nBorderWidth;
        DestRect.right -= nBorderWidth;
        DestRect.top += nTitleHeight + nBorderHeight;
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
        EmuYuy2TextureInfo* pInfo = EmuFindYuy2Texture((X_D3DResource*)pSurface);

        // XMV decodes 29.97/30 fps video ahead of its DirectSound master clock.
        // The Xbox overlay consumes those frames at video cadence; accepting
        // them at the 60 Hz display cadence makes the decoder run ahead and
        // then pause at each audio packet boundary. Rate-match software-overlay
        // updates while the latest frame continues to scan out at 60 Hz.
        EmuPacePresent(g_NextOverlayUpdate, 1001, 30000);
        if(cxbx::d3d8::HostBackendRenders())
        {
            // P8: the frame converts into the shared CPU cache; the backend
            // composites it at Swap (no host texture involved).
            g_pOverlayFrameTexture = NULL;
            g_pOverlayFrameBgra = nullptr;
            g_dwOverlayFrameW = 0;
            g_dwOverlayFrameH = 0;
            if(pInfo != NULL)
            {
                g_pOverlayFrameBgra = EmuVulkanConvertYuy2Bgra(
                    pInfo, &g_dwOverlayFrameW, &g_dwOverlayFrameH);
            }
        }
        else
        {
            g_pOverlayFrameTexture = EmuConvertYuy2Texture(pInfo);
        }
        if((g_pOverlayFrameTexture != NULL || g_pOverlayFrameBgra != nullptr) &&
           cxbx::trace::IsEnabled(cxbx::trace::Channel::Media))
        {
            const std::uint32_t frame =
                g_OverlayFrameGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
            cxbx::trace::RecordBinary(cxbx::trace::Event::MediaOverlayUpdate, frame);
        }
    }

    EmuSwapFS(); // XBox FS

    return;
}
// ******************************************************************
// * func: EmuIDirect3DDevice8_GetOverlayUpdateStatus
// ******************************************************************
BOOL WINAPI XTL::EmuIDirect3DDevice8_GetOverlayUpdateStatus()
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    // Overlay updates complete synchronously in the software path.
    return TRUE;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BlockUntilVerticalBlank
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_BlockUntilVerticalBlank()
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVerticalBlankCallback
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetVerticalBlankCallback(PVOID pCallback)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTextureState_TexCoordIndex
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetTextureState_TexCoordIndex(
    DWORD Stage,
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

    cxbx::d3d8::HostBackendRecordTextureStageState(
        Stage, D3DTSS_TEXCOORDINDEX, Value);
if(!cxbx::d3d8::HostBackendRenders())
        g_pD3DDevice8->SetTextureStageState(Stage, D3DTSS_TEXCOORDINDEX, Value);

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_NormalizeNormals
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_NormalizeNormals(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

if(!cxbx::d3d8::HostBackendRenders())
        g_pD3DDevice8->SetRenderState(D3DRS_NORMALIZENORMALS, Value);

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_TextureFactor
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_TextureFactor(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

if(!cxbx::d3d8::HostBackendRenders())
        g_pD3DDevice8->SetRenderState(D3DRS_TEXTUREFACTOR, Value);

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_ZBias
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_ZBias(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

if(!cxbx::d3d8::HostBackendRenders())
        g_pD3DDevice8->SetRenderState(D3DRS_ZBIAS, Value);

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_EdgeAntiAlias
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_EdgeAntiAlias(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_FillMode
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_FillMode(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

if(!cxbx::d3d8::HostBackendRenders())
        g_pD3DDevice8->SetRenderState(D3DRS_FILLMODE, EmuXB2PC_D3DFILLMODE(Value));

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_FogColor
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_FogColor(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

if(!cxbx::d3d8::HostBackendRenders())
        g_pD3DDevice8->SetRenderState(D3DRS_FOGCOLOR, Value);
    else
        cxbx::d3d8::HostBackendSetFogColor(Value);

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_Dxt1NoiseEnable
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_Dxt1NoiseEnable(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_Simple
// ******************************************************************
VOID __fastcall XTL::EmuIDirect3DDevice8_SetRenderState_Simple(
    DWORD Method,
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
    const bool Nv2aTracked = EmuNv2aSetRenderState(Method, Value);

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
    for(int v = 0; v < 174; v++)
    {
        if(EmuD3DRenderStateSimpleEncoded[v] == Method)
        {
            State = v;
            break;
        }
    }

    if(State == -1)
    {
        // The encoded D3DRS lookup binds to the 4627-era Xbox render-state
        // enumeration; newer XDKs (and the 5849 LTCG deferred ring) stream
        // methods it never listed. Only warn when the NV2A state model also
        // does not recognise the method -- tracked-but-unmapped state is
        // correct emulation, just without a host-D3D translation.
        if(!Nv2aTracked)
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
    }
    else
    {
        switch(State)
        {
            case D3DRS_COLORWRITEENABLE:
            {
                DWORD OrigValue = Value;

                Value = 0;

                if(OrigValue & (1L << 16))
                    Value |= D3DCOLORWRITEENABLE_RED;
                if(OrigValue & (1L << 8))
                    Value |= D3DCOLORWRITEENABLE_GREEN;
                if(OrigValue & (1L << 0))
                    Value |= D3DCOLORWRITEENABLE_BLUE;
                if(OrigValue & (1L << 24))
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

        // Slice C1: the Vulkan flavor's state reads are mirror-served; the
        // host-device write is d3d8-flavor-only.
        if(!cxbx::d3d8::HostBackendRenders())
            g_pD3DDevice8->SetRenderState((D3DRENDERSTATETYPE)State, Value);

        cxbx::d3d8::HostBackendSetRasterState(State, Value);

        // Depth-test state rides the render-state stream into the render
        // path (Value here is already translated to the host enumeration).
        if(State == D3DRS_ZENABLE)
        {
            cxbx::d3d8::HostBackendSetDepthState(0, Value);
        }
        else if(State == D3DRS_ZWRITEENABLE)
        {
            cxbx::d3d8::HostBackendSetDepthState(1, Value);
        }
        else if(State == D3DRS_ZFUNC)
        {
            cxbx::d3d8::HostBackendSetDepthState(2, Value);
        }
    }

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_VertexBlend
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_VertexBlend(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

if(!cxbx::d3d8::HostBackendRenders())
        g_pD3DDevice8->SetRenderState(D3DRS_VERTEXBLEND, Value);

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_CullMode
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_CullMode(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

    // Deferred-state restore paths can pass the internal "not initialized"
    // marker before the title has selected a cull mode. Preserve the current
    // host state instead of interpreting the marker as an Xbox enumeration.
    if(Value == X_D3DRS_UNK)
    {
        EmuSwapFS(); // XBox FS
        return;
    }

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

if(!cxbx::d3d8::HostBackendRenders())
        g_pD3DDevice8->SetRenderState(D3DRS_CULLMODE, Value);
    cxbx::d3d8::HostBackendSetRasterState(D3DRS_CULLMODE, Value);

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_ZEnable
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_ZEnable(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

if(!cxbx::d3d8::HostBackendRenders())
        g_pD3DDevice8->SetRenderState(D3DRS_ZENABLE, Value);
    cxbx::d3d8::HostBackendSetDepthState(0, Value);

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_StencilEnable
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_StencilEnable(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

if(!cxbx::d3d8::HostBackendRenders())
        g_pD3DDevice8->SetRenderState(D3DRS_STENCILENABLE, Value);

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_StencilFail
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_StencilFail(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

if(!cxbx::d3d8::HostBackendRenders())
        g_pD3DDevice8->SetRenderState(D3DRS_STENCILFAIL, Value);

    EmuSwapFS(); // XBox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_MultiSampleAntiAlias
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_MultiSampleAntiAlias(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

if(!cxbx::d3d8::HostBackendRenders())
        g_pD3DDevice8->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, Value);

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_ShadowFunc
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_ShadowFunc(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

    // Xbox shadow-buffer compare function (NV2A hardware shadow mapping).
    // The native backend mirrors it: shadow-map stage binds compare the
    // depth texel with the texcoord r under this function. Xbox's D3DCMP_*
    // enumeration is 0x200..0x207; the backend takes the 1..8 form.
    cxbx::d3d8::HostBackendSetShadowFunc(
        Value >= 0x200 && Value <= 0x207 ? Value - 0x1FF : Value);

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_YuvEnable
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_YuvEnable(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_CullControl
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_CullControl(DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("SetRenderState_CullControl");

    // The five 4627 entry points routed here select NV2A occlusion/stencil
    // packet-culling and ROP-Z read optimizations. They do not change the
    // rasterized result, and host Direct3D 8 exposes no matching controls.
    (void)Value;

    EmuSwapFS(); // Xbox FS
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_FrontFace
// ******************************************************************
// Xbox extension render states have no PC D3DRS equivalent. We accept and
// discard the value (the NV2A-specific front-face winding is handled via
// CullMode on the host); patching the call prevents the guest from touching
// raw device fields.
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_FrontFace(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_LineWidth
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_LineWidth(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_LogicOp
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_LogicOp(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_BackFillMode
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_BackFillMode(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_TwoSidedLighting
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_TwoSidedLighting(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_MultiSampleMode
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_MultiSampleMode(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_MultiSampleRenderTargetMode
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_MultiSampleRenderTargetMode(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_MultiSampleMask
// ******************************************************************
// Unlike the other Xbox multisample states, MultiSampleMask has a direct PC
// D3DRS equivalent, so forward the value to the host device.
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_MultiSampleMask(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

if(!cxbx::d3d8::HostBackendRenders())
        g_pD3DDevice8->SetRenderState(D3DRS_MULTISAMPLEMASK, Value);

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_SampleAlpha
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_SampleAlpha(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_PSTextureModes
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderState_PSTextureModes(
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

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
VOID WINAPI XTL::EmuIDirect3DDevice8_SetRenderStateNotInline(
    DWORD State,
    DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTransform
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetTransform(
    D3DTRANSFORMSTATETYPE State,
    CONST D3DMATRIX* pMatrix)
{
    EmuSwapFS(); // Win2k/XP FS
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

    bool forwardTransform = true;
    if(pMatrix != NULL && State < 256)
    {
        EmuForwardCacheExpire();
        if(g_EmuForwardedTransformValid[State] &&
           memcmp(g_EmuForwardedTransforms[State], pMatrix,
                  sizeof(D3DMATRIX)) == 0)
        {
            forwardTransform = false;
        }
        else
        {
            memcpy(g_EmuForwardedTransforms[State], pMatrix, sizeof(D3DMATRIX));
            g_EmuForwardedTransformValid[State] = true;
        }
    }
    if(forwardTransform)
    {
        // Record the transformed state into the backend mirror (host
        // transform indices; the fixed-function draw path reads it back).
        if(pMatrix != NULL && State < 300)
        {
            cxbx::d3d8::HostBackendRecordTransform(
                State, reinterpret_cast<const float*>(pMatrix));
        }
        if(!cxbx::d3d8::HostBackendRenders())
            __try
            {
                g_pD3DDevice8->SetTransform(State, pMatrix);
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
            }
    }

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetGammaRamp
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_SetGammaRamp(
    DWORD Flags,
    CONST D3DGAMMARAMP* pRamp)
{
    EmuSwapFS(); // Win2k/XP FS
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
    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetTransform
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_GetTransform(
    D3DTRANSFORMSTATETYPE State,
    D3DMATRIX* pMatrix)
{
    EmuSwapFS(); // Win2k/XP FS
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

    if(pMatrix == NULL ||
       !cxbx::d3d8::HostBackendGetTransform(State,
                                            reinterpret_cast<float*>(pMatrix)))
    {
        g_pD3DDevice8->GetTransform(State, pMatrix);
    }

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
static HRESULT EmuVshLockVertexBuffer(XTL::X_D3DVertexBuffer* resource,
                                      BYTE** data, UINT* byteSize);

// * func: EmuIDirect3DVertexBuffer8_Lock
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DVertexBuffer8_Lock(
    X_D3DVertexBuffer* ppVertexBuffer,
    UINT OffsetToLock,
    UINT SizeToLock,
    BYTE** ppbData,
    DWORD Flags)
{
    D3D_TRACE("VertexBuffer_Lock");
    EmuSwapFS(); // Win2k/XP FS

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

    IDirect3DVertexBuffer8* pVertexBuffer8 = ppVertexBuffer->EmuVertexBuffer8;

    // Slice B: under the Vulkan flavor the title locks the container in
    // place; the CPU-fallback reads read the same memory. The offset
    // semantics are preserved by returning base + OffsetToLock, and the
    // locked range must stay inside the container (a missing or oversized
    // range falls back to the host lock).
    if(cxbx::d3d8::HostBackendRenders())
    {
        BYTE* containerBits = nullptr;
        if(EmuServeNativeBufferLock(pVertexBuffer8, OffsetToLock, SizeToLock,
                                    &containerBits))
        {
            *ppbData = containerBits;
            EmuDiscardRegisteredVertexBuffer(ppVertexBuffer);
            EmuSwapFS(); // XBox FS
            return;
        }
        if(pVertexBuffer8 != nullptr)
        {
            __try
            {
                XTL::D3DVERTEXBUFFER_DESC description = {
                    XTL::D3DFMT_UNKNOWN, XTL::D3DRTYPE_VERTEXBUFFER, 0,
                    XTL::D3DPOOL_DEFAULT, 0, 0
                };
                if(SUCCEEDED(pVertexBuffer8->GetDesc(&description)))
                {
                    BYTE* birth = nullptr;
                    if(SUCCEEDED(pVertexBuffer8->Lock(0, 0, &birth,
                                                      D3DLOCK_READONLY)))
                    {
                        EmuNativeBufferAdopt(pVertexBuffer8, birth,
                                             description.Size);
                        pVertexBuffer8->Unlock();
                    }
                }
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
            }
            if(EmuServeNativeBufferLock(pVertexBuffer8, OffsetToLock,
                                        SizeToLock, &containerBits))
            {
                *ppbData = containerBits;
                EmuDiscardRegisteredVertexBuffer(ppVertexBuffer);
                EmuSwapFS(); // XBox FS
                return;
            }
        }
    }

    HRESULT hRet = pVertexBuffer8->Lock(OffsetToLock, SizeToLock, ppbData, Flags);
    if(SUCCEEDED(hRet))
    {
        // A title that locks a registered buffer is now updating the host D3D
        // copy. Stop preferring its original UMA registration span for CPU
        // vertex-shader draws so those writes remain visible.
        EmuDiscardRegisteredVertexBuffer(ppVertexBuffer);
    }

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DVertexBuffer8_Lock2
// ******************************************************************
BYTE* WINAPI XTL::EmuIDirect3DVertexBuffer8_Lock2(
    X_D3DVertexBuffer* ppVertexBuffer,
    DWORD Flags)
{
    D3D_TRACE("VertexBuffer_Lock2");
    EmuSwapFS(); // Win2k/XP FS

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

    IDirect3DVertexBuffer8* pVertexBuffer8 = ppVertexBuffer->EmuVertexBuffer8;

    // Slice B: container write-in-place under the Vulkan flavor (the Lock
    // wrapper above carries the birth logic).
    if(cxbx::d3d8::HostBackendRenders())
    {
        BYTE* lockBits = nullptr;
        UINT lockSize = 0;
        if(EmuVshLockVertexBuffer(ppVertexBuffer, &lockBits, &lockSize) ==
               D3D_OK &&
           lockBits != nullptr)
        {
            EmuDiscardRegisteredVertexBuffer(ppVertexBuffer);
            EmuSwapFS(); // XBox FS
            return lockBits;
        }
    }

    BYTE* pbData = NULL;

    HRESULT hRet = pVertexBuffer8->Lock(0, 0, &pbData, Flags);
    if(SUCCEEDED(hRet))
    {
        EmuDiscardRegisteredVertexBuffer(ppVertexBuffer);
    }

    EmuSwapFS(); // XBox FS

    return pbData;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetStreamSource
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetStreamSource(
    UINT StreamNumber,
    X_D3DVertexBuffer* pStreamData,
    UINT Stride)
{
    EmuSwapFS(); // Win2k/XP FS
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
    XTL::VshCpuDeviceState::SetStream(StreamNumber, { pStreamData, Stride });

    HRESULT hRet = D3D_OK;
    __try
    {
        EmuForwardCacheExpire();
        bool forwardStream = true;
        if(StreamNumber < 16)
        {
            forwardStream = !(g_EmuForwardedStreamValid[StreamNumber] &&
                              g_EmuForwardedStreamResource[StreamNumber] == pStreamData &&
                              g_EmuForwardedStreamStride[StreamNumber] == Stride);
            if(forwardStream)
            {
                g_EmuForwardedStreamResource[StreamNumber] = pStreamData;
                g_EmuForwardedStreamStride[StreamNumber] = Stride;
                g_EmuForwardedStreamValid[StreamNumber] = true;
            }
        }
        // Slice C3-pre: the host stream bind is d3d8-flavor-only.
        if(forwardStream && !cxbx::d3d8::HostBackendRenders())
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
// Texture binds follow the active shader's output-texcoord compaction: the
// game binds to the stage its (uncompacted) shader writes. Whenever the
// active shader's compaction changes, re-issue the host binds from the
// guest shadow state.
namespace
{
struct
{
    std::uint32_t count = 0;
    std::uint8_t from[4] = {};
    std::uint8_t to[4] = {};
} g_EmuActiveTexcoordRemap;
} // namespace

static void EmuApplyActiveTexcoordRemap(
    const XTL::VshShaderRegistry::CpuFallbackMetadata* metadata)
{
    const bool remapped = metadata != nullptr && metadata->texcoordRemapCount > 0;
    if(remapped)
    {
        g_EmuActiveTexcoordRemap.count = metadata->texcoordRemapCount;
        for(std::uint32_t entry = 0; entry < metadata->texcoordRemapCount; ++entry)
        {
            g_EmuActiveTexcoordRemap.from[entry] = metadata->texcoordRemapFrom[entry];
            g_EmuActiveTexcoordRemap.to[entry] = metadata->texcoordRemapTo[entry];
        }
    }
    else
    {
        g_EmuActiveTexcoordRemap.count = 0;
    }

    static bool s_HasPrevious = false;
    static std::uint32_t s_LastCount = 0;
    static std::uint8_t s_LastFrom[4] = {};
    static std::uint8_t s_LastTo[4] = {};
    bool changed = !s_HasPrevious || s_LastCount != g_EmuActiveTexcoordRemap.count;
    if(!changed)
    {
        for(std::uint32_t entry = 0; entry < g_EmuActiveTexcoordRemap.count; ++entry)
        {
            changed = changed ||
                      s_LastFrom[entry] != g_EmuActiveTexcoordRemap.from[entry] ||
                      s_LastTo[entry] != g_EmuActiveTexcoordRemap.to[entry];
        }
    }
    if(!changed) return;
    s_HasPrevious = true;
    s_LastCount = g_EmuActiveTexcoordRemap.count;
    for(std::uint32_t entry = 0; entry < g_EmuActiveTexcoordRemap.count; ++entry)
    {
        s_LastFrom[entry] = g_EmuActiveTexcoordRemap.from[entry];
        s_LastTo[entry] = g_EmuActiveTexcoordRemap.to[entry];
    }

    for(DWORD stage = 0; stage < EmuXboxTextureStageCount; ++stage)
    {
        XTL::IDirect3DBaseTexture8* bound = g_EmuBoundHostTextures[stage];
        if(bound == NULL) continue;
        DWORD hostStage = stage;
        for(std::uint32_t entry = 0; entry < g_EmuActiveTexcoordRemap.count; ++entry)
        {
            if(stage == g_EmuActiveTexcoordRemap.from[entry])
            {
                hostStage = g_EmuActiveTexcoordRemap.to[entry];
                break;
            }
        }
        g_pD3DDevice8->SetTexture(hostStage, bound);
    }
}

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
    XTL::VshShaderRegistry::CpuFallbackMetadata* metadata =
        Handle >= 0x00010000
            ? XTL::VshShaderRegistry::Find(reinterpret_cast<X_D3DVertexShader*>(Handle))
            : nullptr;
    g_EmuCurrentVertexShaderIsCustom = metadata != nullptr;
    g_EmuCurrentFvf = metadata != nullptr ? 0 : Handle;
    XTL::VshShaderRegistry::SetCurrent(
        metadata != nullptr && metadata->enabled ? metadata : nullptr, "SetVertexShader");
    if(metadata != nullptr)
    {
        HostHandle = reinterpret_cast<X_D3DVertexShader*>(Handle)->Handle;
    }

    HRESULT hRet = D3D_OK;
    if(XTL::VshShaderRegistry::Current() == nullptr &&
       !cxbx::d3d8::HostBackendRenders())
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
    EmuApplyActiveTexcoordRemap(metadata);

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
VOID WINAPI XTL::EmuIDirect3DDevice8_LoadVertexShader(
    DWORD Handle,
    DWORD Address)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SelectVertexShader
// ******************************************************************
// Xbox extension: selects the vertex shader resident at slot Address and binds
// Handle as the active declaration. On the host this is equivalent to
// SetVertexShader (the slot model does not exist); resolve the wrapper handle
// the same way SetVertexShader does and forward it.
VOID WINAPI XTL::EmuIDirect3DDevice8_SelectVertexShader(
    DWORD Handle,
    DWORD Address)
{
    EmuSwapFS(); // Win2k/XP FS
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
    XTL::VshShaderRegistry::CpuFallbackMetadata* metadata =
        Handle >= 0x00010000
            ? XTL::VshShaderRegistry::Find(reinterpret_cast<X_D3DVertexShader*>(Handle))
            : nullptr;
    g_EmuCurrentVertexShaderIsCustom = metadata != nullptr;
    g_EmuCurrentFvf = metadata != nullptr ? 0 : Handle;
    XTL::VshShaderRegistry::SetCurrent(
        metadata != nullptr && metadata->enabled ? metadata : nullptr, "SelectVertexShader");
    if(metadata != nullptr)
    {
        HostHandle = reinterpret_cast<X_D3DVertexShader*>(Handle)->Handle;
    }

    HRESULT hRet = D3D_OK;
    if(XTL::VshShaderRegistry::Current() == nullptr &&
       !cxbx::d3d8::HostBackendRenders())
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
    EmuApplyActiveTexcoordRemap(metadata);

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

    if(Handle >= 0x00010000 &&
       XTL::VshShaderRegistry::Unregister((X_D3DVertexShader*)Handle))
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
        XTL::VshShaderRegistry::CpuFallbackMetadata* metadata =
            Handle >= 0x00010000
                ? XTL::VshShaderRegistry::Find(reinterpret_cast<X_D3DVertexShader*>(Handle))
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
            if(!cxbx::d3d8::HostBackendRenders())
                g_pD3DDevice8->SetRenderState(D3DRS_FOGENABLE, XTL::EmuD3DDeferredRenderState[0]);

        if(XTL::EmuD3DDeferredRenderState[1] != X_D3DRS_UNK)
            if(!cxbx::d3d8::HostBackendRenders())
                g_pD3DDevice8->SetRenderState(D3DRS_FOGTABLEMODE, XTL::EmuD3DDeferredRenderState[1]);

        // The native backend consumes the deferred pixel-fog block (Xbox
        // render states 92..96: FOGENABLE, FOGTABLEMODE, FOGSTART, FOGEND,
        // FOGDENSITY) directly; words the title never wrote keep the
        // D3D defaults. The Xbox D3DFOGMODE enum orders NONE=0, EXP=1,
        // EXP2=2, LINEAR=3 and is forwarded raw.
        if(cxbx::d3d8::HostBackendRenders())
        {
            static const DWORD kFogDefaults[5] = {
                0,          // FOGENABLE = FALSE
                0,          // FOGTABLEMODE = D3DFOG_NONE
                0x00000000, // FOGSTART = 0.0f
                0x3F800000, // FOGEND = 1.0f
                0x3F800000, // FOGDENSITY = 1.0f
            };
            DWORD fogWords[5];
            for(unsigned int fogWord = 0; fogWord < 5; ++fogWord)
            {
                fogWords[fogWord] =
                    XTL::EmuD3DDeferredRenderState[fogWord] != X_D3DRS_UNK
                        ? XTL::EmuD3DDeferredRenderState[fogWord]
                        : kFogDefaults[fogWord];
            }
            cxbx::d3d8::HostBackendSetFogState(
                fogWords[0], fogWords[1], fogWords[2], fogWords[3],
                fogWords[4]);
        }

        if(XTL::EmuD3DDeferredRenderState[6] != X_D3DRS_UNK)
        {
            ::DWORD dwConv = 0;

            dwConv |= (XTL::EmuD3DDeferredRenderState[6] & 0x00000010) ? D3DWRAP_U : 0;
            dwConv |= (XTL::EmuD3DDeferredRenderState[6] & 0x00001000) ? D3DWRAP_V : 0;
            dwConv |= (XTL::EmuD3DDeferredRenderState[6] & 0x00100000) ? D3DWRAP_W : 0;

            if(!cxbx::d3d8::HostBackendRenders())
                g_pD3DDevice8->SetRenderState(D3DRS_WRAP0, dwConv);
        }

    if(XTL::EmuD3DDeferredRenderState[10] != X_D3DRS_UNK)
    {
        cxbx::d3d8::HostBackendRecordRenderState(D3DRS_LIGHTING,
                                                 XTL::EmuD3DDeferredRenderState[10]);
        if(!cxbx::d3d8::HostBackendRenders())
            g_pD3DDevice8->SetRenderState(D3DRS_LIGHTING, XTL::EmuD3DDeferredRenderState[10]);
    }

        if(XTL::EmuD3DDeferredRenderState[11] != X_D3DRS_UNK)
            if(!cxbx::d3d8::HostBackendRenders())
                g_pD3DDevice8->SetRenderState(D3DRS_SPECULARENABLE, XTL::EmuD3DDeferredRenderState[11]);

        if(XTL::EmuD3DDeferredRenderState[20] != X_D3DRS_UNK)
            if(!cxbx::d3d8::HostBackendRenders())
                g_pD3DDevice8->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, XTL::EmuD3DDeferredRenderState[20]);

        if(XTL::EmuD3DDeferredRenderState[23] != X_D3DRS_UNK)
            if(!cxbx::d3d8::HostBackendRenders())
                g_pD3DDevice8->SetRenderState(D3DRS_AMBIENT, XTL::EmuD3DDeferredRenderState[23]);

        if(XTL::EmuD3DDeferredRenderState[24] != X_D3DRS_UNK)
            if(!cxbx::d3d8::HostBackendRenders())
                g_pD3DDevice8->SetRenderState(D3DRS_POINTSIZE, XTL::EmuD3DDeferredRenderState[24]);

        if(XTL::EmuD3DDeferredRenderState[25] != X_D3DRS_UNK)
            if(!cxbx::d3d8::HostBackendRenders())
                g_pD3DDevice8->SetRenderState(D3DRS_POINTSIZE_MIN, XTL::EmuD3DDeferredRenderState[25]);

        if(XTL::EmuD3DDeferredRenderState[26] != X_D3DRS_UNK)
            if(!cxbx::d3d8::HostBackendRenders())
                g_pD3DDevice8->SetRenderState(D3DRS_POINTSPRITEENABLE, XTL::EmuD3DDeferredRenderState[26]);

        if(XTL::EmuD3DDeferredRenderState[27] != X_D3DRS_UNK)
            if(!cxbx::d3d8::HostBackendRenders())
                g_pD3DDevice8->SetRenderState(D3DRS_POINTSCALEENABLE, XTL::EmuD3DDeferredRenderState[27]);

        if(XTL::EmuD3DDeferredRenderState[28] != X_D3DRS_UNK)
            if(!cxbx::d3d8::HostBackendRenders())
                g_pD3DDevice8->SetRenderState(D3DRS_POINTSCALE_A, XTL::EmuD3DDeferredRenderState[28]);

        if(XTL::EmuD3DDeferredRenderState[29] != X_D3DRS_UNK)
            if(!cxbx::d3d8::HostBackendRenders())
                g_pD3DDevice8->SetRenderState(D3DRS_POINTSCALE_B, XTL::EmuD3DDeferredRenderState[29]);

        if(XTL::EmuD3DDeferredRenderState[30] != X_D3DRS_UNK)
            if(!cxbx::d3d8::HostBackendRenders())
                g_pD3DDevice8->SetRenderState(D3DRS_POINTSCALE_C, XTL::EmuD3DDeferredRenderState[30]);

        if(XTL::EmuD3DDeferredRenderState[33] != X_D3DRS_UNK)
            if(!cxbx::d3d8::HostBackendRenders())
                g_pD3DDevice8->SetRenderState(D3DRS_PATCHSEGMENTS, XTL::EmuD3DDeferredRenderState[33]);

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
        for(int v = 0; v < 4; v++)
        {
            ::DWORD* pCur = &EmuD3DDeferredTextureState[v * 32];

            if(pCur[0] != X_D3DTSS_UNK)
            {
                if(pCur[0] == 5)
                    EmuCleanup("ClampToEdge is unsupported (temporarily)");

                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_ADDRESSU, pCur[0]);
                if(!cxbx::d3d8::HostBackendRenders())
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_ADDRESSU, pCur[0]);
                cxbx::d3d8::HostBackendSetSamplerState(v, 0, pCur[0]);
            }

            if(pCur[1] != X_D3DTSS_UNK)
            {
                if(pCur[1] == 5)
                    EmuCleanup("ClampToEdge is unsupported (temporarily)");

                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_ADDRESSV, pCur[1]);
                if(!cxbx::d3d8::HostBackendRenders())
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_ADDRESSV, pCur[1]);
                cxbx::d3d8::HostBackendSetSamplerState(v, 1, pCur[1]);
            }

            if(pCur[2] != X_D3DTSS_UNK)
            {
                if(pCur[2] == 5)
                    EmuCleanup("ClampToEdge is unsupported (temporarily)");

                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_ADDRESSW, pCur[2]);
                if(!cxbx::d3d8::HostBackendRenders())
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_ADDRESSW, pCur[2]);
            }

            if(pCur[3] != X_D3DTSS_UNK)
            {
                if(pCur[3] == 4)
                    EmuCleanup("QuinCunx is unsupported (temporarily)");

                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_MAGFILTER, pCur[3]);
                if(!cxbx::d3d8::HostBackendRenders())
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_MAGFILTER, pCur[3]);
                cxbx::d3d8::HostBackendSetSamplerState(v, 2, pCur[3]);
            }

            if(pCur[4] != X_D3DTSS_UNK)
            {
                if(pCur[4] == 4)
                    EmuCleanup("QuinCunx is unsupported (temporarily)");

                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_MINFILTER, pCur[4]);
                if(!cxbx::d3d8::HostBackendRenders())
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_MINFILTER, pCur[4]);
                cxbx::d3d8::HostBackendSetSamplerState(v, 3, pCur[4]);
            }

            if(pCur[5] != X_D3DTSS_UNK)
            {
                if(pCur[5] == 4)
                    EmuCleanup("QuinCunx is unsupported (temporarily)");

                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_MIPFILTER, pCur[5]);
                if(!cxbx::d3d8::HostBackendRenders())
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_MIPFILTER, pCur[5]);
            }

            if(pCur[6] != X_D3DTSS_UNK)
            {
                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_MIPMAPLODBIAS, pCur[6]);
                if(!cxbx::d3d8::HostBackendRenders())
                {
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_MIPMAPLODBIAS, pCur[6]);
                }
            }

            if(pCur[7] != X_D3DTSS_UNK)
            {
                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_MAXMIPLEVEL, pCur[7]);
                if(!cxbx::d3d8::HostBackendRenders())
                {
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_MAXMIPLEVEL, pCur[7]);
                }
            }

            if(pCur[8] != X_D3DTSS_UNK)
            {
                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_MAXANISOTROPY, pCur[8]);
                if(!cxbx::d3d8::HostBackendRenders())
                {
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_MAXANISOTROPY, pCur[8]);
                }
            }

            // TODO: Use a lookup table, this is not always a 1:1 map
            if(pCur[12] != X_D3DTSS_UNK)
            {
                if(pCur[12] > 12)
                    EmuCleanup("(Temporarily) Unsupported D3DTSS_ALPHAOP Value (%d)", pCur[12]);

                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_COLOROP, pCur[12]);
                if(!cxbx::d3d8::HostBackendRenders())
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_COLOROP, pCur[12]);
                cxbx::d3d8::HostBackendSetTextureOp(v, 0, pCur[12]);
            }

            if(pCur[13] != X_D3DTSS_UNK)
            {
                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_COLORARG0, pCur[13]);
                if(!cxbx::d3d8::HostBackendRenders())
                {
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_COLORARG0, pCur[13]);
                }
            }

            if(pCur[14] != X_D3DTSS_UNK)
            {
                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_COLORARG1, pCur[14]);
                if(!cxbx::d3d8::HostBackendRenders())
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_COLORARG1, pCur[14]);
                cxbx::d3d8::HostBackendSetTextureOp(v, 1, pCur[14]);
            }

            if(pCur[15] != X_D3DTSS_UNK)
            {
                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_COLORARG2, pCur[15]);
                if(!cxbx::d3d8::HostBackendRenders())
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_COLORARG2, pCur[15]);
                cxbx::d3d8::HostBackendSetTextureOp(v, 2, pCur[15]);
            }

            // TODO: Use a lookup table, this is not always a 1:1 map (same as D3DTSS_COLOROP)
            if(pCur[16] != X_D3DTSS_UNK)
            {
                if(pCur[16] > 12)
                    EmuCleanup("(Temporarily) Unsupported D3DTSS_ALPHAOP Value (%d)", pCur[16]);

                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_ALPHAOP, pCur[16]);
                if(!cxbx::d3d8::HostBackendRenders())
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_ALPHAOP, pCur[16]);
            }

            if(pCur[17] != X_D3DTSS_UNK)
            {
                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_ALPHAARG0, pCur[17]);
                if(!cxbx::d3d8::HostBackendRenders())
                {
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_ALPHAARG0, pCur[17]);
                }
            }

            if(pCur[18] != X_D3DTSS_UNK)
            {
                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_ALPHAARG1, pCur[18]);
                if(!cxbx::d3d8::HostBackendRenders())
                {
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_ALPHAARG1, pCur[18]);
                }
            }

            if(pCur[19] != X_D3DTSS_UNK)
            {
                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_ALPHAARG2, pCur[19]);
                if(!cxbx::d3d8::HostBackendRenders())
                {
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_ALPHAARG2, pCur[19]);
                }
            }

            if(pCur[20] != X_D3DTSS_UNK)
            {
                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_RESULTARG, pCur[20]);
                if(!cxbx::d3d8::HostBackendRenders())
                {
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_RESULTARG, pCur[20]);
                }
            }

            if(pCur[21] != X_D3DTSS_UNK)
            {
                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_TEXTURETRANSFORMFLAGS, pCur[21]);
                if(!cxbx::d3d8::HostBackendRenders())
                {
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_TEXTURETRANSFORMFLAGS, pCur[21]);
                }
            }

            if(pCur[29] != X_D3DTSS_UNK)
            {
                cxbx::d3d8::HostBackendRecordTextureStageState(v, D3DTSS_BORDERCOLOR, pCur[29]);
                if(!cxbx::d3d8::HostBackendRenders())
                {
                    g_pD3DDevice8->SetTextureStageState(v, D3DTSS_BORDERCOLOR, pCur[29]);
                }
            }

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
            ::DWORD* pCur = &EmuD3DDeferredTextureState[2 * 32];

            IDirect3DBaseTexture8* pTexture;

            // set the point sprites texture
            g_pD3DDevice8->GetTexture(3, &pTexture);
            g_pD3DDevice8->SetTexture(0, pTexture);

            // disable all other stages
            g_pD3DDevice8->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            g_pD3DDevice8->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

            // in that case we have to copy over the stage by hand
            for(int v = 0; v < 30; v++)
            {
                if(pCur[v] != X_D3DTSS_UNK)
                {
                    ::DWORD dwValue;

                    g_pD3DDevice8->GetTextureStageState(3, (D3DTEXTURESTAGESTATETYPE)v, &dwValue);
                    if(!cxbx::d3d8::HostBackendRenders())
                        g_pD3DDevice8->SetTextureStageState(0, (D3DTEXTURESTAGESTATETYPE)v, dwValue);
                }
            }
        }
    }
}

// ******************************************************************
// * func: EmuQuadHackA
// ******************************************************************
uint32 EmuQuadHackA(uint32 PrimitiveCount, XTL::IDirect3DVertexBuffer8*& pOrigVertexBuffer8, XTL::IDirect3DVertexBuffer8*& pHackVertexBuffer8, UINT dwOffset, PVOID pVertexStreamZeroData, UINT VertexStreamZeroStride, PVOID* ppNewVertexStreamZeroData)
{
    UINT uiStride = 0;

    // These are the sizes of our part in the vertex buffer
    DWORD dwOriginalSize = 0;
    DWORD dwNewSize = 0;

    // These are the sizes with the rest of the buffer
    DWORD dwOriginalSizeWR = 0; // the size of the original vertex buffer
    DWORD dwNewSizeWR = 0;      // the size of the new buffer!!!

    BYTE* pOrigVertexData = 0;
    BYTE* pHackVertexData = 0;

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
           IsBadCodePtr((FARPROC) * (void**)pOrigVertexBuffer8))
        {
            EmuWarning("EmuQuadHackA: no usable stream source (0x%.08X) -- skipping quad expansion",
                       pOrigVertexBuffer8);
            if(pOrigVertexBuffer8 != 0)
                pOrigVertexBuffer8->Release();
            pOrigVertexBuffer8 = 0;
            return 0;
        }

        // This is a list of sqares/rectangles, so we convert it to a list of triangles
        dwOriginalSize = PrimitiveCount * uiStride * 2;
        dwNewSize = PrimitiveCount * uiStride * 3;

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
        dwOriginalSize = PrimitiveCount * uiStride * 2;
        dwNewSize = PrimitiveCount * uiStride * 3;

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
    memcpy(&pHackVertexData[dwOffset + dwNewSize], &pOrigVertexData[dwOffset + dwOriginalSize], dwOriginalSizeWR - dwOffset - dwOriginalSize);

    for(DWORD i = 0; i < (PrimitiveCount / 2); i++)
    {
        memcpy(&pHackVertexData[dwOffset + i * uiStride * 6 + 0 * uiStride], &pOrigVertexData[dwOffset + i * uiStride * 4 + 0 * uiStride], uiStride);
        memcpy(&pHackVertexData[dwOffset + i * uiStride * 6 + 1 * uiStride], &pOrigVertexData[dwOffset + i * uiStride * 4 + 1 * uiStride], uiStride);
        memcpy(&pHackVertexData[dwOffset + i * uiStride * 6 + 2 * uiStride], &pOrigVertexData[dwOffset + i * uiStride * 4 + 2 * uiStride], uiStride);
        memcpy(&pHackVertexData[dwOffset + i * uiStride * 6 + 3 * uiStride], &pOrigVertexData[dwOffset + i * uiStride * 4 + 2 * uiStride], uiStride);
        memcpy(&pHackVertexData[dwOffset + i * uiStride * 6 + 4 * uiStride], &pOrigVertexData[dwOffset + i * uiStride * 4 + 3 * uiStride], uiStride);
        memcpy(&pHackVertexData[dwOffset + i * uiStride * 6 + 5 * uiStride], &pOrigVertexData[dwOffset + i * uiStride * 4 + 0 * uiStride], uiStride);

        if(dwVertexShader & D3DFVF_XYZRHW)
        {
            for(int z = 0; z < 6; z++)
            {
                if(((FLOAT*)&pHackVertexData[dwOffset + i * uiStride * 6 + z * uiStride])[2] == 0.0f)
                    ((FLOAT*)&pHackVertexData[dwOffset + i * uiStride * 6 + z * uiStride])[2] = 1.0f;
                if(((FLOAT*)&pHackVertexData[dwOffset + i * uiStride * 6 + z * uiStride])[3] == 0.0f)
                    ((FLOAT*)&pHackVertexData[dwOffset + i * uiStride * 6 + z * uiStride])[3] = 1.0f;
            }
        }
    }

    if(pVertexStreamZeroData == 0)
    {
        pOrigVertexBuffer8->Unlock();
        pHackVertexBuffer8->Unlock();

        EmuForwardCacheFlush();
        g_pD3DDevice8->SetStreamSource(0, pHackVertexBuffer8, uiStride);
    }

    return uiStride;
}

// ******************************************************************
// * func: EmuQuadHackB
// ******************************************************************
VOID EmuQuadHackB(uint32 nStride, XTL::IDirect3DVertexBuffer8*& pOrigVertexBuffer8, XTL::IDirect3DVertexBuffer8*& pHackVertexBuffer8)
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
XTL::X_D3DPushBuffer* WINAPI XTL::EmuIDirect3DDevice8_CreatePushBuffer2(
    DWORD Size,
    BOOL RunUsingCpuCopy)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return pPushBuffer;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BeginPushBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_BeginPushBuffer(
    X_D3DPushBuffer* pPushBuffer)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_EndPushBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_EndPushBuffer(VOID)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("EndPushBuffer");

#ifdef _DEBUG_TRACE
    printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_EndPushBuffer();\n", GetCurrentThreadId());
#endif

    if(g_pRecordingPushBuffer != NULL)
    {
        g_pRecordingPushBuffer->Size += 4;
    }

    g_pRecordingPushBuffer = NULL;

    EmuSwapFS(); // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetPushBufferOffset
// ******************************************************************
DWORD WINAPI XTL::EmuIDirect3DDevice8_GetPushBufferOffset(
    DWORD* pOffset)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("GetPushBufferOffset");

    DWORD dwOffset = 0;

    if(g_pRecordingPushBuffer != NULL)
        dwOffset = g_pRecordingPushBuffer->Size;

    if(pOffset != NULL)
        *pOffset = dwOffset;

    EmuSwapFS(); // XBox FS;

    return dwOffset;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_InsertFence
// ******************************************************************
DWORD WINAPI XTL::EmuIDirect3DDevice8_InsertFence(VOID)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return fence;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_IsFencePending
// ******************************************************************
BOOL WINAPI XTL::EmuIDirect3DDevice8_IsFencePending(DWORD Fence)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("IsFencePending");

    // The host GPU processes commands synchronously within each Present/Clear,
    // so by the time a title checks IsFencePending the prior work is already
    // complete. Report FALSE (not pending) for all fences.
    BOOL pending = FALSE;
    cxbx::trace::RecordD3dWait(cxbx::trace::D3dWaitReason::Fence, 0,
                               static_cast<std::uint32_t>(Fence), pending != FALSE);

    EmuSwapFS(); // XBox FS

    return pending;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BlockOnFence
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_BlockOnFence(DWORD Fence)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("BlockOnFence");

    // The host GPU processes synchronously, so there is nothing to block on.
    cxbx::trace::RecordD3dWait(cxbx::trace::D3dWaitReason::Fence, 0,
                               static_cast<std::uint32_t>(Fence), false);

    EmuSwapFS(); // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_KickPushBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_KickPushBuffer(VOID)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("KickPushBuffer");

    // KickPushBuffer tells the GPU to start processing the current push buffer.
    // The host runtime submits commands immediately, so this is a no-op.

    EmuSwapFS(); // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_BlockUntilIdle
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_BlockUntilIdle(VOID)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("BlockUntilIdle");

    // BlockUntilIdle waits for the GPU to finish all pending work. The host
    // runtime processes synchronously, so use the D3D single-step pusher
    // idle barrier if available, otherwise this is a no-op.
    if(g_pD3DSingleStepPusher != NULL && (*g_pD3DSingleStepPusher & 1) != 0)
    {
        IDirect3DSurface8* pRT = NULL;
        if(SUCCEEDED(g_pD3DDevice8->GetRenderTarget(&pRT)) && pRT != NULL)
        {
            D3DLOCKED_RECT lr;
            RECT px = { 0, 0, 1, 1 };
            if(SUCCEEDED(pRT->LockRect(&lr, &px, D3DLOCK_READONLY)))
                pRT->UnlockRect();
            pRT->Release();
        }
    }

    cxbx::trace::RecordD3dWait(cxbx::trace::D3dWaitReason::Idle, 0, 0, false);

    EmuSwapFS(); // XBox FS

    return D3D_OK;
}

static bool EmuVshTryDrawCpuBound(bool quadList, XTL::D3DPRIMITIVETYPE primitiveType,
                                  UINT primitiveCount, UINT firstVertex, UINT vertexCount,
                                  const std::uint32_t* indices);
static void EmuVshLogCpuDraw(const char* api, XTL::X_D3DPRIMITIVETYPE primitiveType,
                             UINT vertexCount, bool rendered, const char* reason);

static void EmuRestoreRecordedGuestTextures(const EmuRecordedPushBufferDraw& draw)
{
    for(DWORD stage = 0; stage < EmuXboxTextureStageCount; ++stage)
    {
        XTL::X_D3DResource* guestTexture = draw.guestTextures[stage];
        XTL::IDirect3DBaseTexture8* hostTexture = nullptr;
        if(guestTexture != nullptr)
        {
            if((reinterpret_cast<std::uintptr_t>(guestTexture) & 0x80000000u) != 0)
            {
                // Under the native flavor the pull converts YUY2 straight
                // into the upload by the guest key; only the d3d8 flavor
                // needs the host texture here.
                if(!cxbx::d3d8::HostBackendRenders())
                {
                    hostTexture = EmuConvertYuy2Texture(EmuFindYuy2Texture(guestTexture));
                }
            }
            else if(EmuD3DIsReadableRange(guestTexture, sizeof(*guestTexture)))
            {
                const cxbx::d3d::RecordedTextureDescriptor currentDescriptor =
                    EmuCaptureRecordedTextureDescriptor(guestTexture);
                if(draw.stateBlock != 0 &&
                   draw.guestTextureDescriptors[stage].valid &&
                   !cxbx::d3d::CanRefreshRecordedTexture(
                       draw.guestTextureDescriptors[stage], currentDescriptor))
                {
                    if(SUCCEEDED(g_pD3DDevice8->GetTexture(stage, &hostTexture)))
                    {
                        EmuFlushHostTextureLocks(hostTexture);
                        EmuRefreshLinearTexture(hostTexture);
                        EmuConfigureLinearTextureCoordinates(stage, hostTexture);
                        g_EmuBoundHostTextures[stage] = hostTexture;
                        if(hostTexture != nullptr)
                        {
                            hostTexture->Release();
                        }
                    }
                    else
                    {
                        g_EmuBoundHostTextures[stage] = nullptr;
                    }
                    g_EmuBoundGuestTextures[stage] = guestTexture;
                    g_EmuBoundGuestTextureDescriptors[stage] =
                        draw.guestTextureDescriptors[stage];
                    // Slice C: the recorded draw replays through the backend
                    // under the native flavor; re-resolve its stage binding
                    // by the recorded GUEST key (YUY2 included).
                    if(cxbx::d3d8::HostBackendRenders())
                    {
                        EmuVulkanStageTextureUpload(stage, hostTexture,
                                                    guestTexture);
                    }
                    continue;
                }
                EmuVerifyResourceIsRegistered(guestTexture);
                hostTexture = guestTexture->EmuBaseTexture8;
            }
        }

        EmuFlushHostTextureLocks(hostTexture);
        EmuRefreshLinearTexture(hostTexture);
        EmuConfigureLinearTextureCoordinates(stage, hostTexture);
        if(cxbx::d3d8::HostBackendRenders())
        {
            // The host bind is dead state under the native flavor; the
            // backend bind resolves by the guest key (YUY2 frames convert
            // straight into the upload).
            g_EmuBoundHostTextures[stage] = hostTexture;
            g_EmuBoundGuestTextures[stage] = guestTexture;
            g_EmuBoundGuestTextureDescriptors[stage] =
                EmuCaptureRecordedTextureDescriptor(guestTexture);
            EmuVulkanStageTextureUpload(stage, hostTexture, guestTexture);
        }
        else
        {
            g_pD3DDevice8->SetTexture(stage, hostTexture);
            g_EmuBoundHostTextures[stage] = hostTexture;
            g_EmuBoundGuestTextures[stage] = guestTexture;
            g_EmuBoundGuestTextureDescriptors[stage] =
                EmuCaptureRecordedTextureDescriptor(guestTexture);
        }
    }
}

static bool EmuReplayHlePushBuffer(const DWORD* commandData, DWORD size,
                                   const EmuRecordedPushBuffer* recording)
{
    if(commandData == nullptr || size == 0)
    {
        return size == 0;
    }

    LARGE_INTEGER fpStart;
    EmuFrameProfileStart(fpStart);

    std::vector<std::uint32_t> indices;
    try
    {
        indices.reserve(4096);
    }
    catch(...)
    {
        EmuFrameProfileAccumulate(g_FrameProfile.replayNs, fpStart);
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
                        if(stateBlock != 0)
                        {
                            LARGE_INTEGER fpApplyStart;
                            EmuFrameProfileStart(fpApplyStart);
                            const HRESULT fpApplied =
                                g_pD3DDevice8->ApplyStateBlock(stateBlock);
                            EmuFrameProfileAccumulate(g_FrameProfile.applyNs, fpApplyStart);
                            if(FAILED(fpApplied))
                            {
                                return false;
                            }
                            cxbx::d3d8::HostBackendStateBlockApply(stateBlock);
                        }
                        EmuRestoreRecordedGuestTextures(recording->draws[drawIndex]);
                    }
                    ++drawIndex;
                    if(supported && XTL::VshShaderRegistry::Current() != nullptr)
                    {
                        const bool drawEnabled = EmuD3DDrawGate(
                            "RunPushBuffer", primitiveType,
                            batch.primitiveCount);
                        if(drawEnabled)
                        {
                            const XTL::VshCpuDeviceState::IndexBinding indexBinding =
                                XTL::VshCpuDeviceState::IndexBuffer();
                            rendered = EmuVshTryDrawCpuBound(
                                quadList, primitiveType, batch.primitiveCount,
                                indexBinding.baseVertex,
                                static_cast<UINT>(indices.size()), indices.data());
                            EmuVshLogCpuDraw(
                                "RunPushBuffer", beginEndOperation,
                                static_cast<UINT>(indices.size()), rendered,
                                rendered ? "none" : "cpu_push_draw_failed");
                            if(rendered)
                            {
                                EmuD3DDrawPost();
                            }
                        }
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
        EmuFrameProfileAccumulate(g_FrameProfile.replayNs, fpStart);
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
                if(hasRegisteredData && XTL::VshShaderRegistry::Current() != nullptr)
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
    float texCoords[4][4];
    float clipPosition[4];
    float diffuseComponents[4];
    float specularComponents[4];
    // The program's o[FOG] output (raw float; specular alpha keeps the
    // host-d3d8 packed copy). The Vulkan draw reads it at this offset.
    float fogValue;
};

static constexpr DWORD EmuVshCpuVertexFvf =
    D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_PSIZE | D3DFVF_TEX4 |
    D3DFVF_TEXCOORDSIZE4(0) | D3DFVF_TEXCOORDSIZE4(1) |
    D3DFVF_TEXCOORDSIZE4(2) | D3DFVF_TEXCOORDSIZE4(3);

static_assert(offsetof(EmuVshCpuVertex, fogValue) ==
                  kEmuVshCpuVertexFogOffset,
              "recorded draws replay with a baked-in fog offset");

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
                                          begin.clipPosition[component]) *
                                             amount;
        result.diffuseComponents[component] = begin.diffuseComponents[component] +
                                              (end.diffuseComponents[component] -
                                               begin.diffuseComponents[component]) *
                                                  amount;
        result.specularComponents[component] = begin.specularComponents[component] +
                                               (end.specularComponents[component] -
                                                begin.specularComponents[component]) *
                                                   amount;
        for(std::size_t coordinate = 0; coordinate < 4; ++coordinate)
        {
            result.texCoords[component][coordinate] =
                begin.texCoords[component][coordinate] +
                (end.texCoords[component][coordinate] -
                 begin.texCoords[component][coordinate]) *
                    amount;
        }
    }
    result.fogValue = begin.fogValue +
                      (end.fogValue - begin.fogValue) * amount;
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
    // Slice B: the container is the authoritative byte store under the
    // Vulkan flavor; the host lock below doubles as its birth pull.
    if(cxbx::d3d8::HostBackendRenders())
    {
        BYTE* containerBits = nullptr;
        const UINT containerSize =
            EmuServeNativeBufferBytes(resource->EmuVertexBuffer8,
                                      &containerBits);
        if(containerSize != 0)
        {
            *data = containerBits;
            *byteSize = containerSize;
            return D3D_OK;
        }
    }
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
    // Slice B: container-first under the Vulkan flavor (mirror of the
    // vertex-buffer branch above).
    if(cxbx::d3d8::HostBackendRenders())
    {
        BYTE* containerBits = nullptr;
        const UINT containerSize =
            EmuServeNativeBufferBytes(resource->EmuIndexBuffer8,
                                      &containerBits);
        if(containerSize != 0)
        {
            *data = containerBits;
            *byteSize = containerSize;
            return D3D_OK;
        }
    }
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
    // Backend mirror first (records every guest SetViewport); the host
    // d3d8 shadow remains the fallback for a never-programmed viewport.
    float recorded[6] = {};
    if(viewport != NULL &&
       cxbx::d3d8::HostBackendGetViewport(recorded))
    {
        viewport->X = static_cast<DWORD>(recorded[0]);
        viewport->Y = static_cast<DWORD>(recorded[1]);
        viewport->Width = static_cast<DWORD>(recorded[2]);
        viewport->Height = static_cast<DWORD>(recorded[3]);
        viewport->MinZ = recorded[4];
        viewport->MaxZ = recorded[5];
        return D3D_OK;
    }
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
    LARGE_INTEGER fpStart;
    EmuFrameProfileStart(fpStart);
    XTL::VshShaderRegistry::CpuFallbackMetadata* metadata =
        XTL::VshShaderRegistry::Current();
    if(metadata != nullptr && !metadata->geometryLogged && vertices != nullptr)
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
                   static_cast<unsigned int>(metadata->hash), vertexCount,
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
                // Backend mirror first; the host d3d8 shadow is the
                // fallback for a state nothing has recorded yet.
                if(!cxbx::d3d8::HostBackendGetRenderState(XTL::D3DRS_ZENABLE, &zEnable))
                    g_pD3DDevice8->GetRenderState(XTL::D3DRS_ZENABLE, &zEnable);
                if(!cxbx::d3d8::HostBackendGetRenderState(XTL::D3DRS_ZFUNC, &zFunction))
                    g_pD3DDevice8->GetRenderState(XTL::D3DRS_ZFUNC, &zFunction);
                if(!cxbx::d3d8::HostBackendGetRenderState(XTL::D3DRS_CULLMODE, &cullMode))
                    g_pD3DDevice8->GetRenderState(XTL::D3DRS_CULLMODE, &cullMode);
                if(!cxbx::d3d8::HostBackendGetRenderState(XTL::D3DRS_ALPHABLENDENABLE, &alphaBlend))
                    g_pD3DDevice8->GetRenderState(XTL::D3DRS_ALPHABLENDENABLE, &alphaBlend);
                if(!cxbx::d3d8::HostBackendGetRenderState(XTL::D3DRS_ALPHATESTENABLE, &alphaTest))
                    g_pD3DDevice8->GetRenderState(XTL::D3DRS_ALPHATESTENABLE, &alphaTest);
                if(!cxbx::d3d8::HostBackendGetRenderState(XTL::D3DRS_COLORWRITEENABLE, &colorWrite))
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
                   static_cast<unsigned int>(metadata->hash), zEnable,
                   zFunction, cullMode, alphaBlend, alphaTest, colorWrite,
                   g_bUsePixelShaderFallback ? 1u : 0u, renderTargetWidth,
                   renderTargetHeight, drawsToBackBuffer ? 1u : 0u);
            fflush(stdout);
            metadata->geometryLogged = true;
        }
    }

    // Targeted save of exactly the fixed-function state this draw mutates
    // (pixel shader, stage-0/1 blend selection, stage-0..3 projection flags).
    // A full D3DSBT_ALL capture/apply here cost ~3 full state-block passes
    // per CPU-fallback draw (Turok Evolution: ~600 draws per frame).
    DWORD previousPixelShader = 0;
    DWORD savedTss[16] = {};
    g_pD3DDevice8->GetPixelShader(&previousPixelShader);
    g_pD3DDevice8->GetTextureStageState(0, XTL::D3DTSS_COLOROP, &savedTss[0]);
    g_pD3DDevice8->GetTextureStageState(0, XTL::D3DTSS_COLORARG1, &savedTss[1]);
    g_pD3DDevice8->GetTextureStageState(0, XTL::D3DTSS_COLORARG2, &savedTss[2]);
    g_pD3DDevice8->GetTextureStageState(0, XTL::D3DTSS_ALPHAOP, &savedTss[3]);
    g_pD3DDevice8->GetTextureStageState(0, XTL::D3DTSS_ALPHAARG1, &savedTss[4]);
    g_pD3DDevice8->GetTextureStageState(0, XTL::D3DTSS_ALPHAARG2, &savedTss[5]);
    g_pD3DDevice8->GetTextureStageState(1, XTL::D3DTSS_COLOROP, &savedTss[6]);
    g_pD3DDevice8->GetTextureStageState(1, XTL::D3DTSS_ALPHAOP, &savedTss[7]);
    g_pD3DDevice8->GetTextureStageState(1, XTL::D3DTSS_COLORARG1, &savedTss[8]);
    g_pD3DDevice8->GetTextureStageState(1, XTL::D3DTSS_ALPHAARG1, &savedTss[9]);
    for(DWORD ttfStage = 0; ttfStage < 4; ++ttfStage)
        g_pD3DDevice8->GetTextureStageState(
            ttfStage, XTL::D3DTSS_TEXTURETRANSFORMFLAGS, &savedTss[10 + ttfStage]);
    DWORD previousShader = 0;
    HRESULT result = D3DERR_INVALIDCALL;
    // POD staging (no destructor): this function contains __try scopes, and
    // C2712 bans destructor-bearing objects in them.
    EmuVshCpuVertex* projectedVertices = NULL;
    UINT projectedVerticesCount = 0;
    const EmuVshCpuVertex* drawVertices = vertices;
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
        const HRESULT texCoordIndexResult =
            cxbx::d3d8::HostBackendGetTextureStageState(
                0, XTL::D3DTSS_TEXCOORDINDEX,
                reinterpret_cast<unsigned long*>(&texCoordIndexState))
                ? D3D_OK
                : g_pD3DDevice8->GetTextureStageState(
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
            cxbx::d3d::SelectCpuFallbackMaterial(
                g_bUsePixelShaderFallback, g_EmuCurrentPixelShaderHandle != 0,
                stage0TextureUsable);
        XTL::VshShaderRegistry::CpuFallbackMetadata* metadata =
            XTL::VshShaderRegistry::Current();
        if(metadata != nullptr && !metadata->materialLogged)
        {
            printf("VSH| cpu_material hash=%08X pixel=%08lX stage0_texture=%u texcoord=%lu "
                   "texture_type=%lu texture_format=%lu texture_size=%ux%u "
                   "stage3_texture=%u stage3_type=%lu stage3_format=%lu "
                   "stage3_size=%ux%u stage3_data=%u "
                   "finite=%u varies=%u texture_data=%u mode=%s "
                   "combiner=%s\n",
                   static_cast<unsigned int>(metadata->hash),
                   g_EmuCurrentPixelShaderHandle, stage0TextureBound ? 1u : 0u, texCoordIndex,
                   stage0TextureType, stage0TextureFormat, stage0TextureWidth,
                   stage0TextureHeight,
                   stage3TextureBound ? 1u : 0u, stage3TextureType,
                   stage3TextureFormat, stage3TextureWidth, stage3TextureHeight,
                   stage3TextureHasData ? 1u : 0u,
                   texCoordsFinite ? 1u : 0u, texCoordsVary ? 1u : 0u,
                   stage0TextureHasData ? 1u : 0u,
                   material == cxbx::d3d::CpuFallbackMaterial::PreservePixelShader
                       ? "preserve_pixel_shader"
                   : material == cxbx::d3d::CpuFallbackMaterial::TextureModulate
                       ? "texture_modulate"
                       : "diffuse",
                   cxbx::d3d::PixelShaderFallbackName(EmuCurrentPixelShaderFallback()));
            fflush(stdout);
            metadata->materialLogged = true;
        }

        if(material == cxbx::d3d::CpuFallbackMaterial::PreservePixelShader)
        {
            // A host ps.1.1 `tex` instruction does not implement the Xbox
            // PROJECT2D/PROJECT3D divide, and D3DTTFF_PROJECTED is ignored while
            // a programmable pixel shader is bound. Project the CPU output
            // explicitly before submitting its FVF vertices.
            const auto* definition =
                EmuFindTranslatedPixelShader(g_EmuCurrentPixelShaderHandle);
            const UINT vertexCount =
                EmuRecordedDrawVertexCount(primitiveType, primitiveCount);
            if(definition != nullptr && vertices != nullptr && vertexCount != 0)
            {
                projectedVertices = new EmuVshCpuVertex[vertexCount];
                memcpy(projectedVertices, vertices,
                       sizeof(EmuVshCpuVertex) * vertexCount);
                projectedVerticesCount = vertexCount;
                for(unsigned int stage = 0; stage < 4; ++stage)
                {
                    const unsigned int textureMode =
                        cxbx::d3d::PixelShaderTextureMode(*definition, stage);
                    if(cxbx::d3d::CpuFallbackTextureNeedsProjection(textureMode))
                    {
                        for(unsigned int vertex = 0; vertex < projectedVerticesCount; ++vertex)
                        {
                            cxbx::d3d::ProjectCpuFallbackTextureCoordinates(
                                projectedVertices[vertex].texCoords[stage], textureMode);
                        }
                    }
                    if(!cxbx::d3d8::HostBackendRenders())
                        g_pD3DDevice8->SetTextureStageState(
                            stage, XTL::D3DTSS_TEXTURETRANSFORMFLAGS,
                            XTL::D3DTTFF_DISABLE);
                }
                drawVertices = projectedVertices;
            }
        }
        else if(material == cxbx::d3d::CpuFallbackMaterial::TextureModulate)
        {
            // Preserve the common NV2A combiner shapes which the D3D8 fixed-function
            // stages can express: texture-only, doubled vertex lighting, a second
            // detail texture, and the stage-3 lookup used by several lit materials.
            // Slice C2: the host application is d3d8-flavor-only.
            if(!cxbx::d3d8::HostBackendRenders())
                EmuApplyPixelShaderFallback(EmuCurrentPixelShaderFallback(), true);
        }
        else if(!cxbx::d3d8::HostBackendRenders())
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
        if(cxbx::d3d8::HostBackendRenders())
        {
            // Slice C2: the host shader state is invisible; the seam draw
            // needs no FVF bind.
            result = D3D_OK;
        }
        else
        {
            result = g_pD3DDevice8->GetVertexShader(&previousShader);
            if(SUCCEEDED(result))
            {
                result = g_pD3DDevice8->SetVertexShader(EmuVshCpuVertexFvf);
            }
        }
        if(SUCCEEDED(result))
        {
            if(cxbx::d3d8::HostBackendRenders())
            {
                // P2: CPU-transformed fallback vertices draw into the
                // backend target (position at 0, diffuse at 20 in
                // EmuVshCpuVertex); texture stages land with P3, the
                // program's o[FOG] float with P5.
                cxbx::d3d8::HostBackendDrawUP(
                    primitiveType, primitiveCount, drawVertices,
                    sizeof(EmuVshCpuVertex), 20, 28 /*texCoords[0]*/, 4,
                    offsetof(EmuVshCpuVertex, fogValue));
                result = D3D_OK;
            }
            else
            {
                result = g_pD3DDevice8->DrawPrimitiveUP(primitiveType, primitiveCount, drawVertices,
                                                        sizeof(EmuVshCpuVertex));
            }
            if(SUCCEEDED(result))
            {
                // This draw is submitted to the current backbuffer immediately. Static
                // push-buffer recording keeps its own copy for future RunPushBuffer calls;
                // replaying every CPU fallback draw again at Present would move it after
                // later UI draws and cover title logos or text.
                EmuRecordPushBufferDraw(primitiveType, primitiveCount, drawVertices,
                                        sizeof(EmuVshCpuVertex));

                // Diagnostic (CXBX_D3D_DUMP_DRAWS): dump the render target this
                // CPU-fallback draw landed in. Render-to-texture world passes go
                // through this path, so the currently bound target here is the
                // title's 512x512 RT (not the swap back buffer the standard
                // EmuD3DDrawPost hook sees) -- this tells a black RT (world never
                // rendered) from a black sample (RT has the world but the later
                // sampling pass reads the wrong/stale host texture).
                static int s_CpuRtDumpEnabled = -1;
                if(s_CpuRtDumpEnabled < 0)
                {
                    char value[8] = {};
                    s_CpuRtDumpEnabled =
                        EmuD3DReadEnvironment("CXBX_D3D_DUMP_DRAWS", value, sizeof(value)) ? 1 : 0;
                }
                static LONG s_CpuRtDumpIndex = 0;
                if(s_CpuRtDumpEnabled == 1 && s_CpuRtDumpIndex < 128)
                {
                    char file[64];
                    snprintf(file, sizeof(file), "cpu_f%05lu_%04ld_rt.bmp",
                             static_cast<unsigned long>(g_D3DDebugFrame),
                             static_cast<long>(s_CpuRtDumpIndex));
                    char rtPath[MAX_PATH * 2];
                    EmuD3DDebugDumpPath(rtPath, sizeof(rtPath), "cxbx_rt", file);
                    if(EmuD3DDumpCurrentRenderTarget(rtPath))
                    {
                        printf("VSH| cpu_rt_dump %s\n", rtPath);
                        fflush(stdout);
                    }
                    s_CpuRtDumpIndex++;
                }
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        result = D3DERR_INVALIDCALL;
    }

    EmuFrameProfileAccumulate(g_FrameProfile.cpuDrawNs, fpStart);
    __try
    {
        if(!cxbx::d3d8::HostBackendRenders())
        {
            g_pD3DDevice8->SetPixelShader(previousPixelShader);
            g_pD3DDevice8->SetTextureStageState(0, XTL::D3DTSS_COLOROP, savedTss[0]);
            g_pD3DDevice8->SetTextureStageState(0, XTL::D3DTSS_COLORARG1, savedTss[1]);
            g_pD3DDevice8->SetTextureStageState(0, XTL::D3DTSS_COLORARG2, savedTss[2]);
            g_pD3DDevice8->SetTextureStageState(0, XTL::D3DTSS_ALPHAOP, savedTss[3]);
            g_pD3DDevice8->SetTextureStageState(0, XTL::D3DTSS_ALPHAARG1, savedTss[4]);
            g_pD3DDevice8->SetTextureStageState(0, XTL::D3DTSS_ALPHAARG2, savedTss[5]);
            g_pD3DDevice8->SetTextureStageState(1, XTL::D3DTSS_COLOROP, savedTss[6]);
            g_pD3DDevice8->SetTextureStageState(1, XTL::D3DTSS_ALPHAOP, savedTss[7]);
            g_pD3DDevice8->SetTextureStageState(1, XTL::D3DTSS_COLORARG1, savedTss[8]);
            g_pD3DDevice8->SetTextureStageState(1, XTL::D3DTSS_ALPHAARG1, savedTss[9]);
            for(DWORD ttfStage = 0; ttfStage < 4; ++ttfStage)
                g_pD3DDevice8->SetTextureStageState(
                    ttfStage, XTL::D3DTSS_TEXTURETRANSFORMFLAGS, savedTss[10 + ttfStage]);
            g_pD3DDevice8->SetVertexShader(previousShader);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    EmuFrameProfileAccumulate(g_FrameProfile.cpuDrawNs, fpStart);
    delete[] projectedVertices;
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
    XTL::VshShaderRegistry::CpuFallbackMetadata* metadata =
        XTL::VshShaderRegistry::Current();
    if(metadata == nullptr)
    {
        return;
    }
    bool& logged = rendered ? metadata->drawLogged : metadata->rejectionLogged;
    if(logged)
    {
        return;
    }
    printf("VSH| cpu_draw hash=%08X api=%s vertices=%u primitive=%lu result=%s reason=%s\n",
           static_cast<unsigned int>(metadata->hash), api, vertexCount,
           static_cast<unsigned long>(primitiveType), rendered ? "success" : "rejected", reason);
    fflush(stdout);
    logged = true;
}

static bool EmuVshTransformCpuVertices(const XTL::VshDiagnostics::VertexStreamView* streams,
                                       std::size_t streamCount, const std::uint32_t* indices,
                                       UINT firstVertex, UINT vertexCount,
                                       std::vector<EmuVshCpuVertex>& output)
{
    XTL::VshShaderRegistry::CpuFallbackMetadata* metadata =
        XTL::VshShaderRegistry::Current();
    if(metadata == nullptr || vertexCount == 0 || vertexCount > 65536)
    {
        return false;
    }

    XTL::D3DVIEWPORT8 viewport{};
    if(FAILED(EmuVshGetViewport(&viewport)))
    {
        return false;
    }

    const float defaultPointSize = EmuVshGetDefaultPointSize();
    const XTL::VshDiagnostics::ShaderWordView xboxDeclaration = {
        metadata->declaration.data(),
        metadata->declarationTokenCount,
    };
    const XTL::VshDiagnostics::ShaderWordView xboxFunction = {
        metadata->function.data(),
        1 + metadata->instructionCount * 4,
    };
    std::array<float, 192 * 4> shaderConstants{};
    const std::span<const float> constants = XTL::VshCpuDeviceState::Constants();
    if(!XTL::VshDiagnostics::ApplyXboxDeclarationConstants(
           xboxDeclaration, constants.data(),
           shaderConstants.data(), shaderConstants.size()))
    {
        return false;
    }
    output.resize(vertexCount);

    // Indexed draws repeat vertices across adjacent triangles. The CPU fallback
    // must still emit the original index order for clipping and rasterization,
    // but decoding and executing the vertex program once per source vertex
    // avoids multiplying its cost by the triangle count.
    constexpr UINT EmuVshCpuIndexCacheSize = 16384;
    static std::array<EmuVshCpuVertex, EmuVshCpuIndexCacheSize> transformedVertices{};
    static std::array<ULONG, EmuVshCpuIndexCacheSize> transformedVertexGenerations{};
    static ULONG transformedVertexGeneration = 0;
    if(indices != nullptr)
    {
        ++transformedVertexGeneration;
        if(transformedVertexGeneration == 0)
        {
            transformedVertexGenerations.fill(0);
            transformedVertexGeneration = 1;
        }
    }

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
        if(indices != nullptr && relativeIndex < EmuVshCpuIndexCacheSize &&
           transformedVertexGenerations[relativeIndex] == transformedVertexGeneration)
        {
            output[outputIndex] = transformedVertices[relativeIndex];
            continue;
        }

        float input[16 * 4] = {};
        if(!XTL::VshDiagnostics::DecodeXboxVertex(
               xboxDeclaration, streams, streamCount, vertexIndex, input, 16 * 4))
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
        if(!XTL::VshDiagnostics::ExecuteXboxVertexShader(
               xboxFunction, shaderConstants.data(), input, position, colors, 2 * 4,
               texCoords, 4 * 4, &rasterOutputs))
        {
            return false;
        }
        EmuVshCpuVertex& vertex = output[outputIndex];
        std::copy(std::begin(position), std::end(position), vertex.clipPosition);
        std::copy(&colors[0], &colors[4], vertex.diffuseComponents);
        std::copy(&colors[4], &colors[8], vertex.specularComponents);
        vertex.specularComponents[3] = XTL::VshDiagnostics::SelectRasterOutput(
            rasterOutputs.fog, rasterOutputs.fogWriteMask, 1.0f);
        vertex.fogValue = vertex.specularComponents[3];
        const float pointSize = XTL::VshDiagnostics::SelectRasterOutput(
            rasterOutputs.pointSize, rasterOutputs.pointSizeWriteMask, defaultPointSize);
        vertex.pointSize = XTL::VshDiagnostics::ClampPointSize(
            pointSize, defaultPointSize, g_D3DCaps.MaxPointSize);
        for(std::size_t texCoord = 0; texCoord < 4; ++texCoord)
        {
            std::copy(&texCoords[texCoord * 4], &texCoords[texCoord * 4 + 4],
                      vertex.texCoords[texCoord]);
        }
        EmuVshProjectCpuVertex(vertex, viewport);
        if(indices != nullptr && relativeIndex < EmuVshCpuIndexCacheSize)
        {
            transformedVertices[relativeIndex] = vertex;
            transformedVertexGenerations[relativeIndex] = transformedVertexGeneration;
        }
    }

    if(vertexCount >= 3 && capturedFirstInputs &&
       !metadata->collapseCaptureLogged)
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
                    xboxFunction,
                    {},
                    xboxDeclaration,
                    {},
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
            metadata->collapseCaptureLogged = true;
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
        const XTL::VshCpuDeviceState::StreamBinding binding =
            XTL::VshCpuDeviceState::Stream(stream);
        XTL::X_D3DVertexBuffer* resource = binding.resource;
        if(resource == nullptr || binding.stride == 0)
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
                binding.stride,
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
            binding.stride,
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
        const XTL::VshCpuDeviceState::IndexBinding binding =
            XTL::VshCpuDeviceState::IndexBuffer();
        hostIndexBuffer =
            binding.resource == nullptr ? nullptr : binding.resource->EmuIndexBuffer8;
        UINT indexByteSize = 0;
        const std::size_t byteOffset = reinterpret_cast<std::size_t>(indexData);
        bool rendered = false;
        if(hostIndexBuffer != nullptr &&
           SUCCEEDED(EmuVshLockIndexBuffer(binding.resource, &indexBytes, &indexByteSize)) &&
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
                                          binding.baseVertex, vertexCount, indices.data());
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

    if(XTL::VshShaderRegistry::Current() != nullptr)
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

    if(cxbx::d3d8::HostBackendRenders())
    {
        // Non-indexed VB draws under the Vulkan render path: pull the
        // stream-0 bytes (Register container first, host lock fallback) and
        // hand the staging block to the backend (fixed-function XYZRHW FVF,
        // mirroring DrawVerticesUP / DrawIndexedVertices). Without this
        // branch the draw fell into the host QuadHack path and was lost --
        // the host shadow's content never reaches the Vulkan present.
        if((g_EmuCurrentFvf & D3DFVF_XYZRHW) == 0)
        {
            static LONG fvfWarned = 0;
            if(InterlockedIncrement(&fvfWarned) <= 5)
            {
                EmuWarning("DrawVertices under the Vulkan render path "
                           "requires XYZRHW (fvf=0x%.08lX)",
                           g_EmuCurrentFvf);
            }
            EmuSwapFS(); // XBox FS
            return;
        }
        const XTL::VshCpuDeviceState::StreamBinding streamBinding =
            XTL::VshCpuDeviceState::Stream(0);
        if(streamBinding.resource == nullptr || streamBinding.stride == 0)
        {
            static LONG bindWarned = 0;
            if(InterlockedIncrement(&bindWarned) <= 5)
            {
                EmuWarning("DrawVertices under the Vulkan render path "
                           "requires a bound stream-0 vertex buffer");
            }
            EmuSwapFS(); // XBox FS
            return;
        }

        const EmuRegisteredVertexBuffer* registration =
            EmuFindRegisteredVertexBuffer(streamBinding.resource);
        const BYTE* vertexBytes = nullptr;
        UINT vertexByteSize = 0;
        XTL::IDirect3DVertexBuffer8* lockedVertexBuffer = nullptr;
        BYTE* lockedVertexData = nullptr;
        if(registration != nullptr && registration->guestData != nullptr &&
           EmuD3DIsReadableRange(registration->guestData,
                                 registration->byteSize))
        {
            vertexBytes = registration->guestData;
            vertexByteSize = registration->byteSize;
        }
        else
        {
            UINT lockSize = 0;
            if(FAILED(EmuVshLockVertexBuffer(streamBinding.resource,
                                             &lockedVertexData, &lockSize)))
            {
                EmuWarning("DrawVertices could not lock stream 0 under the "
                           "Vulkan render path");
                EmuSwapFS(); // XBox FS
                return;
            }
            lockedVertexBuffer = streamBinding.resource->EmuVertexBuffer8;
            vertexBytes = lockedVertexData;
            vertexByteSize = lockSize;
        }

        bool ready = StartVertex <= vertexByteSize / streamBinding.stride &&
                     static_cast<std::size_t>(StartVertex) +
                             static_cast<std::size_t>(VertexCount) <=
                         vertexByteSize / streamBinding.stride;
        const BYTE* drawData = nullptr;
        UINT drawVertexCount = VertexCount;
        D3DPRIMITIVETYPE drawPrimitiveType = PCPrimitiveType;
        UINT drawPrimitiveCount = PrimitiveCount;
        if(ready && (DWORD)PrimitiveType == 8) // Quad list: expand to tris
        {
            if(VertexCount % 4 != 0 || VertexCount > EMU_IM_MAXVERTS)
            {
                ready = false;
            }
            else
            {
                static BYTE s_VBQuadExpand[EMU_IM_MAXVERTS * 3 / 2 * 64];
                if(streamBinding.stride > 64)
                {
                    ready = false;
                }
                else
                {
                    const BYTE* source = vertexBytes +
                        static_cast<std::size_t>(StartVertex) *
                            streamBinding.stride;
                    BYTE* dst = s_VBQuadExpand;
                    static const UINT quadIndices[6] = { 0, 1, 2, 0, 2, 3 };
                    for(UINT quad = 0; quad < VertexCount / 4; ++quad)
                    {
                        for(UINT k = 0; k < 6; ++k)
                        {
                            memcpy(dst,
                                   source + static_cast<std::size_t>(
                                                 quad * 4 + quadIndices[k]) *
                                                 streamBinding.stride,
                                   streamBinding.stride);
                            dst += streamBinding.stride;
                        }
                    }
                    drawData = s_VBQuadExpand;
                    drawVertexCount = VertexCount / 4 * 6;
                    drawPrimitiveType = D3DPT_TRIANGLELIST;
                    drawPrimitiveCount = VertexCount / 2;
                }
            }
        }
        else if(ready)
        {
            drawData = vertexBytes +
                       static_cast<std::size_t>(StartVertex) *
                           streamBinding.stride;
        }

        if(ready)
        {
            const unsigned int DiffuseOffset =
                (g_EmuCurrentFvf & D3DFVF_DIFFUSE) != 0 ? 16u : 0xFFFFFFFFu;
            unsigned int TexCoordOffset = 0xFFFFFFFFu;
            unsigned int TexCoordSets = 0;
            if((g_EmuCurrentFvf & D3DFVF_TEX1) != 0)
            {
                TexCoordOffset = DiffuseOffset != 0xFFFFFFFFu ? 20u : 16u;
                // Xbox FVF texcoord-size encoding: 3-float sets (cube
                // directions) -> vec3 mode, 4-float sets (shadow u/v/r/q)
                // -> single-vec4 mode.
                const DWORD stage0Size = (g_EmuCurrentFvf >> 16) & 3;
                if(stage0Size == 1)
                {
                    TexCoordSets = 3;
                }
                else if(stage0Size == 2)
                {
                    TexCoordSets = 5;
                }
            }
            cxbx::d3d8::HostBackendDrawUP(
                drawPrimitiveType, drawPrimitiveCount, drawData,
                streamBinding.stride, DiffuseOffset, TexCoordOffset,
                TexCoordSets, 0xFFFFFFFFu);
            EmuD3DDrawPost();
        }
        else
        {
            static LONG dropWarned = 0;
            if(InterlockedIncrement(&dropWarned) <= 5)
            {
                EmuWarning("DrawVertices dropped under the Vulkan render "
                           "path (unreadable vertex range)");
            }
        }

        if(lockedVertexData != nullptr)
        {
            EmuVshUnlockVertexBuffer(lockedVertexBuffer);
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
    HRESULT hRet = D3D_OK;
    __try
    {
        hRet = g_pD3DDevice8->DrawPrimitive(
            PCPrimitiveType,
            StartVertex,
            PrimitiveCount);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        hRet = D3DERR_INVALIDCALL;
        static LONG s_DrawGuardCount = 0;
        if(InterlockedIncrement(&s_DrawGuardCount) <= 3)
        {
            EmuWarning("DrawVertices: host d3d8 threw during DrawPrimitive (caught)");
        }
    }

    if(FAILED(hRet))
    {
        static LONG warningCount = 0;
        if(InterlockedIncrement(&warningCount) <= 5)
        {
            printf("EmuD3D8: DrawVertices failed (0x%.08lX) prim=%lu "
                   "start=%u primCount=%u.\n",
                   static_cast<unsigned long>(hRet),
                   static_cast<unsigned long>(PCPrimitiveType), StartVertex,
                   PrimitiveCount);
            EmuWarning("DrawVertices failed (0x%.08X)", hRet);
        }
    }
    else
    {
        EmuD3DDrawPost();
    }

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

    if(XTL::VshShaderRegistry::Current() != nullptr)
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

    if(cxbx::d3d8::HostBackendRenders())
    {
        // P2 render path: pretransformed FVF draws go into the backend
        // target. XYZRHW streams (optional diffuse) feed the backend
        // directly. Fixed-function XYZ streams transform CPU-side through
        // the composite WORLD*VIEW*PROJECTION and the live viewport (Xbox
        // starts with fixed-function lighting disabled; lit FVF draws keep
        // the drop until lighting is modeled).
        unsigned int DiffuseOffset = 0xFFFFFFFFu;
        unsigned int DrawPrimitiveCount = PrimitiveCount;
        const void* DrawData = pVertexStreamZeroData;
        unsigned int DrawStride = VertexStreamZeroStride;
        static unsigned char s_TransformedVertices[EMU_IM_MAXVERTS * 20];
        bool transformed = false;

        if((g_EmuCurrentFvf & D3DFVF_XYZRHW) != 0)
        {
            DiffuseOffset =
                (g_EmuCurrentFvf & D3DFVF_DIFFUSE) != 0 ? 16u : 0xFFFFFFFFu;
        }
        else if((g_EmuCurrentFvf & D3DFVF_XYZ) != 0 &&
                g_pD3DDevice8 != NULL)
        {
            DWORD lighting = 0;
            if(!cxbx::d3d8::HostBackendGetRenderState(XTL::D3DRS_LIGHTING,
                                                      &lighting))
            {
                g_pD3DDevice8->GetRenderState(XTL::D3DRS_LIGHTING, &lighting);
            }
            if(lighting != 0)
            {
                static LONG litWarned = 0;
                if(InterlockedIncrement(&litWarned) <= 5)
                {
                    EmuWarning("DrawVerticesUP fixed-function lighting is "
                               "not modeled under the Vulkan render path "
                               "(fvf=0x%.08lX); draw dropped",
                               g_EmuCurrentFvf);
                }
                EmuSwapFS(); // XBox FS
                return;
            }
            if(VertexCount > EMU_IM_MAXVERTS)
            {
                static LONG countWarned = 0;
                if(InterlockedIncrement(&countWarned) <= 5)
                {
                    EmuWarning("DrawVerticesUP fixed-function transform "
                               "rejected (count=%lu > %d)",
                               static_cast<unsigned long>(VertexCount),
                               EMU_IM_MAXVERTS);
                }
                EmuSwapFS(); // XBox FS
                return;
            }

            // The host shadow carries the live guest transform and viewport
            // state (both forwarded on write).
            XTL::D3DMATRIX world = {};
            XTL::D3DMATRIX view = {};
            XTL::D3DMATRIX projection = {};
            XTL::D3DVIEWPORT8 viewport = {};
            // Backend mirror first (the SetTransform/SetViewport forwards
            // record); the host d3d8 shadow is the fallback.
            if(!cxbx::d3d8::HostBackendGetTransform(
                   256, reinterpret_cast<float*>(&world)))
            {
                g_pD3DDevice8->GetTransform(
                    static_cast<XTL::D3DTRANSFORMSTATETYPE>(256), &world);
            }
            if(!cxbx::d3d8::HostBackendGetTransform(
                   XTL::D3DTS_VIEW, reinterpret_cast<float*>(&view)))
            {
                g_pD3DDevice8->GetTransform(XTL::D3DTS_VIEW, &view);
            }
            if(!cxbx::d3d8::HostBackendGetTransform(
                   XTL::D3DTS_PROJECTION, reinterpret_cast<float*>(&projection)))
            {
                g_pD3DDevice8->GetTransform(XTL::D3DTS_PROJECTION, &projection);
            }
            {
                float recordedViewport[6] = {};
                if(cxbx::d3d8::HostBackendGetViewport(recordedViewport))
                {
                    viewport.X = static_cast<DWORD>(recordedViewport[0]);
                    viewport.Y = static_cast<DWORD>(recordedViewport[1]);
                    viewport.Width = static_cast<DWORD>(recordedViewport[2]);
                    viewport.Height = static_cast<DWORD>(recordedViewport[3]);
                    viewport.MinZ = recordedViewport[4];
                    viewport.MaxZ = recordedViewport[5];
                }
                else
                {
                    g_pD3DDevice8->GetViewport(&viewport);
                }
            }

            // Composite row-vector transform: clip = v * WORLD * VIEW * PROJ.
            float composite[4][4] = {};
            const XTL::D3DMATRIX* chain[2] = { &view, &projection };
            const XTL::D3DMATRIX* stage = &world;
            for(int stageIndex = 0; stageIndex < 2; ++stageIndex)
            {
                const XTL::D3DMATRIX* next = chain[stageIndex];
                float result[4][4] = {};
                for(int row = 0; row < 4; ++row)
                {
                    for(int col = 0; col < 4; ++col)
                    {
                        float sum = 0.0f;
                        for(int k = 0; k < 4; ++k)
                        {
                            sum += stage->m[row][k] * next->m[k][col];
                        }
                        result[row][col] = sum;
                    }
                }
                memcpy(composite, result, sizeof(composite));
                stage = reinterpret_cast<const XTL::D3DMATRIX*>(composite);
            }

            // Source diffuse position within the guest FVF: XYZ, then
            // NORMAL, then POINTSIZE, then DIFFUSE.
            unsigned int sourceDiffuse = 12;
            if((g_EmuCurrentFvf & D3DFVF_NORMAL) != 0)
            {
                sourceDiffuse += 12;
            }
            if((g_EmuCurrentFvf & D3DFVF_PSIZE) != 0)
            {
                sourceDiffuse += 4;
            }
            const bool hasDiffuse = (g_EmuCurrentFvf & D3DFVF_DIFFUSE) != 0;

            const unsigned char* source =
                static_cast<const unsigned char*>(pVertexStreamZeroData);
            for(UINT vertex = 0; vertex < VertexCount; ++vertex)
            {
                const BYTE* in = source + static_cast<size_t>(vertex) * VertexStreamZeroStride;
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                memcpy(&x, in, sizeof(x));
                memcpy(&y, in + 4, sizeof(y));
                memcpy(&z, in + 8, sizeof(z));

                const float clip[4] = {
                    x * composite[0][0] + y * composite[1][0] +
                        z * composite[2][0] + composite[3][0],
                    x * composite[0][1] + y * composite[1][1] +
                        z * composite[2][1] + composite[3][1],
                    x * composite[0][2] + y * composite[1][2] +
                        z * composite[2][2] + composite[3][2],
                    x * composite[0][3] + y * composite[1][3] +
                        z * composite[2][3] + composite[3][3],
                };
                float w = clip[3];
                if(w < 1e-8f && w > -1e-8f)
                {
                    w = w < 0.0f ? -1e-8f : 1e-8f;
                }
                const float ndcX = clip[0] / w;
                const float ndcY = clip[1] / w;
                const float ndcZ = clip[2] / w;
                // D3D viewport mapping (y flips from NDC's up-positive to
                // the framebuffer's down-positive pixels).
                const float screenX =
                    static_cast<float>(viewport.X) +
                    (ndcX + 1.0f) * static_cast<float>(viewport.Width) / 2.0f;
                const float screenY =
                    static_cast<float>(viewport.Y) +
                    (1.0f - ndcY) * static_cast<float>(viewport.Height) / 2.0f;
                const float screenZ =
                    viewport.MinZ +
                    ndcZ * (viewport.MaxZ - viewport.MinZ);

                unsigned char* out =
                    s_TransformedVertices + static_cast<size_t>(vertex) * 20;
                memcpy(out, &screenX, sizeof(screenX));
                memcpy(out + 4, &screenY, sizeof(screenY));
                memcpy(out + 8, &screenZ, sizeof(screenZ));
                const float rhw = 1.0f / w;
                memcpy(out + 12, &rhw, sizeof(rhw));
                DWORD diffuse = 0;
                if(hasDiffuse)
                {
                    memcpy(&diffuse, in + sourceDiffuse, sizeof(diffuse));
                }
                memcpy(out + 16, &diffuse, sizeof(diffuse));
            }

            DrawData = s_TransformedVertices;
            DrawStride = 20;
            DiffuseOffset = hasDiffuse ? 16u : 0xFFFFFFFFu;
            transformed = true;
        }
        else
        {
            static LONG fvfWarned = 0;
            if(InterlockedIncrement(&fvfWarned) <= 5)
            {
                EmuWarning("DrawVerticesUP under the Vulkan render path "
                           "requires XYZRHW (fvf=0x%.08lX)",
                           g_EmuCurrentFvf);
            }
            EmuSwapFS(); // XBox FS
            return;
        }
        if(PrimitiveType == 8) // quad list: expand each quad to two triangles
        {
            static unsigned char s_QuadExpand[EMU_IM_MAXVERTS * 3 / 2 * 64];
            if(DrawStride > 64 ||
               VertexCount > EMU_IM_MAXVERTS ||
               VertexCount % 4 != 0)
            {
                EmuWarning("DrawVerticesUP quad expansion rejected "
                           "(stride=%lu count=%lu)",
                           static_cast<unsigned long>(DrawStride),
                           static_cast<unsigned long>(VertexCount));
                EmuSwapFS(); // XBox FS
                return;
            }
            const unsigned char* src =
                static_cast<const unsigned char*>(DrawData);
            unsigned char* dst = s_QuadExpand;
            for(UINT quad = 0; quad < VertexCount / 4; ++quad)
            {
                static const UINT quadIndices[6] = { 0, 1, 2, 0, 2, 3 };
                for(UINT k = 0; k < 6; ++k)
                {
                    memcpy(dst, src + static_cast<size_t>(quad * 4 + quadIndices[k]) * DrawStride,
                           DrawStride);
                    dst += DrawStride;
                }
            }
            DrawPrimitiveCount = VertexCount / 2; // 6 verts per quad -> 2 tris
            DrawData = s_QuadExpand;
            DrawStride = DrawStride;
        }
        unsigned int TexCoordOffset = 0xFFFFFFFFu;
        unsigned int TexCoordSets = 0;
        if(!transformed && (g_EmuCurrentFvf & D3DFVF_TEX1) != 0)
        {
            TexCoordOffset = DiffuseOffset != 0xFFFFFFFFu ? 20u : 16u;
            // Stage-0 set size selects the renderer's texcoord layout:
            // Xbox's FVF size encoding is nonstandard (TEXTUREFORMAT2=0
            // default, 3=1, 4=2, 1=3 in the per-set bits). 3-float sets
            // (cube directions) take the vec3 mode; 4-float sets (shadow
            // u/v/r/q) take the single-vec4 mode.
            const DWORD stage0Size = (g_EmuCurrentFvf >> 16) & 3;
            if(stage0Size == 1)
            {
                TexCoordSets = 3;
            }
            else if(stage0Size == 2)
            {
                TexCoordSets = 5;
            }
        }
        cxbx::d3d8::HostBackendDrawUP(PCPrimitiveType, DrawPrimitiveCount,
                                      DrawData, DrawStride, DiffuseOffset,
                                      TexCoordOffset, TexCoordSets,
                                      0xFFFFFFFFu);
        EmuD3DDrawPost();
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
// * func: EmuIDirect3DDevice8_DrawIndexedVerticesUP
// ******************************************************************
VOID WINAPI XTL::EmuIDirect3DDevice8_DrawIndexedVerticesUP(
    X_D3DPRIMITIVETYPE PrimitiveType,
    UINT VertexCount,
    CONST PVOID pIndexData,
    CONST PVOID pVertexStreamZeroData,
    UINT VertexStreamZeroStride)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("DrawIndexedVerticesUP");

// ******************************************************************
// * debug trace
// ******************************************************************
#ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_DrawIndexedVerticesUP\n"
               "(\n"
               "   PrimitiveType            : 0x%.08X\n"
               "   VertexCount              : 0x%.08X\n"
               "   pIndexData               : 0x%.08X\n"
               "   pVertexStreamZeroData    : 0x%.08X\n"
               "   VertexStreamZeroStride   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), PrimitiveType, VertexCount, pIndexData,
               pVertexStreamZeroData, VertexStreamZeroStride);
    }
#endif

    EmuUpdateDeferredStates();

    if(!EmuVshIsValidPrimitiveType(PrimitiveType))
    {
        EmuWarning("DrawIndexedVerticesUP rejected invalid primitive type %lu",
                   static_cast<unsigned long>(PrimitiveType));
        EmuSwapFS(); // XBox FS
        return;
    }

    if(PrimitiveType == 8) // Quad List: indexed quads need index rewriting
    {
        static LONG QuadWarnCount = 0;
        if(InterlockedIncrement(&QuadWarnCount) <= 5)
        {
            EmuWarning("DrawIndexedVerticesUP: indexed QUADLIST not implemented, draw skipped");
        }
        EmuSwapFS(); // XBox FS
        return;
    }

    UINT PrimitiveCount = EmuD3DVertex2PrimitiveCount(PrimitiveType, VertexCount);
    D3DPRIMITIVETYPE PCPrimitiveType = EmuPrimitiveType(PrimitiveType);

    if(!EmuD3DDrawGate("DrawIndexedVerticesUP", PCPrimitiveType, PrimitiveCount))
    {
        EmuSwapFS(); // XBox FS
        return;
    }

    if(XTL::VshShaderRegistry::Current() != nullptr)
    {
        static LONG CpuWarnCount = 0;
        if(InterlockedIncrement(&CpuWarnCount) <= 5)
        {
            EmuWarning("DrawIndexedVerticesUP: CPU vertex-shader fallback not implemented, draw skipped");
        }
        EmuSwapFS(); // XBox FS
        return;
    }

    // Host DrawIndexedPrimitiveUP wants the number of vertices spanned by the
    // index set; the Xbox call does not carry it, so derive it from the
    // indices (VertexCount is the number of indices).
    UINT NumVertices = 0;
    {
        CONST WORD* pIndices = (CONST WORD*)pIndexData;
        for(UINT i = 0; i < VertexCount; i++)
        {
            if(pIndices[i] >= NumVertices)
            {
                NumVertices = pIndices[i] + 1;
            }
        }
    }

    HRESULT hRet = D3D_OK;
    __try
    {
        hRet = g_pD3DDevice8->DrawIndexedPrimitiveUP(
            PCPrimitiveType,
            0,
            NumVertices,
            PrimitiveCount,
            pIndexData,
            D3DFMT_INDEX16,
            pVertexStreamZeroData,
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
            EmuWarning("DrawIndexedVerticesUP failed (0x%.08X) prim=%d primCount=%d stride=%d",
                       hRet, PCPrimitiveType, PrimitiveCount, VertexStreamZeroStride);
        }
    }
    else
    {
        EmuD3DDrawPost();
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

    if(XTL::VshShaderRegistry::Current() != nullptr)
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

    if(cxbx::d3d8::HostBackendRenders())
    {
        // Indexed draws under the Vulkan render path: pull the current
        // stream-0 and index-buffer bytes and hand the staging blocks to
        // the backend (fixed-function XYZRHW FVF, mirroring DrawVerticesUP).
        if((g_EmuCurrentFvf & D3DFVF_XYZRHW) == 0)
        {
            static LONG fvfWarned = 0;
            if(InterlockedIncrement(&fvfWarned) <= 5)
            {
                EmuWarning("DrawIndexedVertices under the Vulkan render path "
                           "requires XYZRHW (fvf=0x%.08lX)",
                           g_EmuCurrentFvf);
            }
            EmuSwapFS(); // XBox FS
            return;
        }
        const XTL::VshCpuDeviceState::StreamBinding streamBinding =
            XTL::VshCpuDeviceState::Stream(0);
        const XTL::VshCpuDeviceState::IndexBinding indexBinding =
            XTL::VshCpuDeviceState::IndexBuffer();
        if(streamBinding.resource == nullptr || streamBinding.stride == 0 ||
           indexBinding.resource == nullptr)
        {
            static LONG bindWarned = 0;
            if(InterlockedIncrement(&bindWarned) <= 5)
            {
                EmuWarning("DrawIndexedVertices under the Vulkan render path "
                           "requires a bound stream-0 vertex buffer and "
                           "index buffer");
            }
            EmuSwapFS(); // XBox FS
            return;
        }

        const EmuRegisteredVertexBuffer* registration =
            EmuFindRegisteredVertexBuffer(streamBinding.resource);
        const BYTE* vertexBytes = nullptr;
        UINT vertexByteSize = 0;
        XTL::IDirect3DVertexBuffer8* lockedVertexBuffer = nullptr;
        BYTE* lockedVertexData = nullptr;
        if(registration != nullptr && registration->guestData != nullptr &&
           EmuD3DIsReadableRange(registration->guestData,
                                 registration->byteSize))
        {
            vertexBytes = registration->guestData;
            vertexByteSize = registration->byteSize;
        }
        else
        {
            UINT lockSize = 0;
            if(FAILED(EmuVshLockVertexBuffer(streamBinding.resource,
                                             &lockedVertexData, &lockSize)))
            {
                EmuWarning("DrawIndexedVertices could not lock stream 0 "
                           "under the Vulkan render path");
                EmuSwapFS(); // XBox FS
                return;
            }
            lockedVertexBuffer = streamBinding.resource->EmuVertexBuffer8;
            vertexBytes = lockedVertexData;
            vertexByteSize = lockSize;
        }

        BYTE* indexBytes = nullptr;
        UINT indexByteSize = 0;
        bool indexLocked =
            SUCCEEDED(EmuVshLockIndexBuffer(indexBinding.resource, &indexBytes,
                                            &indexByteSize)) &&
            indexBytes != nullptr;
        const std::size_t indexByteOffset =
            reinterpret_cast<std::size_t>(pIndexData);
        bool ready = indexLocked && indexByteOffset <= indexByteSize &&
                     static_cast<std::size_t>(VertexCount) * sizeof(WORD) <=
                         indexByteSize - indexByteOffset;

        // Quad list: expand each quad's four indices to two triangles (the
        // static buffer keeps the host-forward __try below unwind-free).
        static WORD s_IndexQuadExpand[EMU_IM_MAXVERTS * 3 / 2];
        const WORD* drawIndices = nullptr;
        UINT indexCount = VertexCount;
        UINT drawPrimitiveCount = PrimitiveCount;
        D3DPRIMITIVETYPE drawPrimitiveType = PCPrimitiveType;
        if(ready && (DWORD)PrimitiveType == 8)
        {
            if(VertexCount % 4 != 0 || VertexCount > EMU_IM_MAXVERTS)
            {
                ready = false;
            }
            else
            {
                for(UINT quad = 0; quad < VertexCount / 4; ++quad)
                {
                    static const WORD quadOrder[6] = { 0, 1, 2, 0, 2, 3 };
                    for(UINT k = 0; k < 6; ++k)
                    {
                        s_IndexQuadExpand[quad * 6 + k] = reinterpret_cast<const WORD*>(
                            indexBytes + indexByteOffset)[quad * 4 + quadOrder[k]];
                    }
                }
                drawIndices = s_IndexQuadExpand;
                indexCount = VertexCount / 4 * 6;
                drawPrimitiveCount = VertexCount / 2;
                drawPrimitiveType = D3DPT_TRIANGLELIST;
            }
        }
        else if(ready)
        {
            drawIndices =
                reinterpret_cast<const WORD*>(indexBytes + indexByteOffset);
        }

        unsigned int drawVertexCount = 0;
        if(ready)
        {
            WORD maxIndex = 0;
            for(UINT i = 0; i < indexCount; ++i)
            {
                if(drawIndices[i] > maxIndex)
                {
                    maxIndex = drawIndices[i];
                }
            }
            const unsigned int spanVertexCount =
                indexBinding.baseVertex + maxIndex + 1;
            if(spanVertexCount <= vertexByteSize / streamBinding.stride)
            {
                drawVertexCount = spanVertexCount;
            }
            else
            {
                ready = false;
            }
        }

        if(ready)
        {
            const unsigned int DiffuseOffset =
                (g_EmuCurrentFvf & D3DFVF_DIFFUSE) != 0 ? 16u : 0xFFFFFFFFu;
            const unsigned int TexCoordOffset =
                (g_EmuCurrentFvf & D3DFVF_TEX1) != 0
                    ? (DiffuseOffset != 0xFFFFFFFFu ? 20u : 16u)
                    : 0xFFFFFFFFu;
            cxbx::d3d8::HostBackendDrawIndexed(
                drawPrimitiveType, drawPrimitiveCount, vertexBytes,
                drawVertexCount, streamBinding.stride, DiffuseOffset,
                TexCoordOffset, drawIndices, indexCount,
                static_cast<int>(indexBinding.baseVertex), 0, 0xFFFFFFFFu);
            EmuD3DDrawPost();
        }
        else
        {
            static LONG dropWarned = 0;
            if(InterlockedIncrement(&dropWarned) <= 5)
            {
                EmuWarning("DrawIndexedVertices dropped under the Vulkan "
                           "render path (unreadable index or vertex range)");
            }
        }

        if(lockedVertexData != nullptr)
        {
            EmuVshUnlockVertexBuffer(lockedVertexBuffer);
        }
        if(indexLocked)
        {
            EmuVshUnlockIndexBuffer(indexBinding.resource->EmuIndexBuffer8);
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

    UINT NumVertices = VertexCount;
    if((DWORD)PrimitiveType != 8)
    {
        // Host DrawIndexedPrimitive validates referenced vertices against
        // [BaseVertexIndex, BaseVertexIndex + NumVertices); the Xbox call
        // carries no range, so derive it from the indices actually drawn
        // (the same derivation DrawIndexedVerticesUP performs). Passing the
        // raw index count rejected draws whose indices reach beyond it.
        const XTL::VshCpuDeviceState::IndexBinding indexBinding =
            XTL::VshCpuDeviceState::IndexBuffer();
        if(indexBinding.resource != nullptr)
        {
            BYTE* indexBytes = nullptr;
            UINT indexByteSize = 0;
            if(SUCCEEDED(EmuVshLockIndexBuffer(indexBinding.resource,
                                               &indexBytes, &indexByteSize)) &&
               indexBytes != nullptr)
            {
                const UINT startIndex = (UINT)(((DWORD)pIndexData) / 2);
                if(startIndex < indexByteSize / sizeof(WORD))
                {
                    const UINT available =
                        (UINT)(indexByteSize / sizeof(WORD) - startIndex);
                    const UINT scan =
                        VertexCount <= available ? VertexCount : available;
                    const WORD* words =
                        reinterpret_cast<const WORD*>(indexBytes) + startIndex;
                    WORD maxIndex = 0;
                    for(UINT i = 0; i < scan; ++i)
                    {
                        if(words[i] > maxIndex)
                        {
                            maxIndex = words[i];
                        }
                    }
                    if(maxIndex + 1 > NumVertices)
                    {
                        NumVertices = maxIndex + 1;
                    }
                }
                EmuVshUnlockIndexBuffer(indexBinding.resource->EmuIndexBuffer8);
            }
        }
    }

    __try
    {
        g_pD3DDevice8->DrawIndexedPrimitive(
            PCPrimitiveType, 0, NumVertices, ((DWORD)pIndexData) / 2, PrimitiveCount);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        static LONG s_DrawGuardCount = 0;
        if(InterlockedIncrement(&s_DrawGuardCount) <= 3)
            EmuWarning("DrawIndexedVertices: host d3d8 threw during DrawIndexedPrimitive (caught)");
    }

    EmuD3DDrawPost();

    if(PrimitiveType == 8) // Quad List
        EmuQuadHackB(nStride, pOrigVertexBuffer8, pHackVertexBuffer8);

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetLight
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetLight(
    DWORD Index,
    CONST D3DLIGHT8* pLight)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetMaterial
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetMaterial(
    CONST D3DMATERIAL8* pMaterial)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_LightEnable
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_LightEnable(
    DWORD Index,
    BOOL bEnable)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderTarget
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetRenderTarget(
    X_D3DSurface* pRenderTarget,
    X_D3DSurface* pNewZStencil)
{
    EmuSwapFS(); // Win2k/XP FS
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

    IDirect3DSurface8* pPCRenderTarget = 0;
    IDirect3DSurface8* pPCNewZStencil = 0;

    if(pRenderTarget != 0)
    {
        EmuVerifyResourceIsRegistered(pRenderTarget);
        pPCRenderTarget = pRenderTarget->EmuSurface8;
    }

    if(pNewZStencil != 0)
    {
        EmuVerifyResourceIsRegistered(pNewZStencil);
        pPCNewZStencil = pNewZStencil->EmuSurface8;
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
    // P8 C3: under the render path the host binding is dead state -- Clear
    // routes to the backend, Present presents the backend target, and the
    // RT registry is keyed by the host surface pointer, not by the host
    // binding. The guest-visible caches below carry everything titles read
    // back, so the host device is left unbound.
    if(HostCompatible && !cxbx::d3d8::HostBackendRenders())
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

    // P5 render targets: under the render path the Vulkan-side target keyed
    // by the host surface pointer is switched (created on demand from the
    // resolved dimensions). The backbuffer resolves to the main target; the
    // depth-stencil surface is accepted but not bound in this phase (ZEnable
    // FALSE covers the P5 gate; real depth testing lands with P6).
    if(cxbx::d3d8::HostBackendRenders())
    {
        IDirect3DSurface8* pHostBackBuffer = NULL;
        bool isBackBuffer = false;
        __try
        {
            if(g_pD3DDevice8 != NULL &&
               SUCCEEDED(g_pD3DDevice8->GetBackBuffer(
                   0, XTL::D3DBACKBUFFER_TYPE_MONO, &pHostBackBuffer)) &&
               pHostBackBuffer != NULL)
            {
                isBackBuffer =
                    (pHostBackBuffer == pRenderTarget->EmuSurface8);
                pHostBackBuffer->Release();
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            isBackBuffer = false;
        }

        // Slice C: the backend keys targets on GUEST identity. The colour
        // key is the guest surface pointer; the owning-texture key (for
        // stage-override binds) and the depth key (for shadow binds) come
        // from the level-surface link registry -- the link's GUEST texture
        // pointer, stable for the resource's lifetime.
        void* pDepthKey = nullptr;
        if(pPCNewZStencil != NULL)
        {
            if(const EmuSwizzledSurfaceLink* pDepthLink =
                   EmuFindSwizzledSurfaceLink(pPCNewZStencil))
            {
                pDepthKey = pDepthLink->pGuestTexture;
            }
        }
        void* pTextureKey = nullptr;
        if(pPCRenderTarget != NULL)
        {
            if(const EmuSwizzledSurfaceLink* pColourLink =
                   EmuFindSwizzledSurfaceLink(pPCRenderTarget))
            {
                pTextureKey = pColourLink->pGuestTexture;
            }
        }

        if(pRenderTarget == 0)
        {
            cxbx::d3d8::HostBackendSetRenderTarget(nullptr, 0, 0, pDepthKey,
                                                   nullptr);
        }
        else if(isBackBuffer)
        {
            cxbx::d3d8::HostBackendSetRenderTarget(nullptr, 0, 0, pDepthKey,
                                                   nullptr);
        }
        else if(GotRTDesc)
        {
            cxbx::d3d8::HostBackendSetRenderTarget(
                pRenderTarget, RTDesc.Width, RTDesc.Height, pDepthKey,
                pTextureKey);
        }
    }

    EmuSwapFS(); // XBox FS

    if(!HostCompatible || FAILED(hRet))
    {
        EmuWarning(HostCompatible ? "SetRenderTarget Failed! (0x%.08X)"
                                  : "SetRenderTarget skipped (host-incompatible surfaces) (0x%.08X)",
                   hRet);
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
HRESULT WINAPI XTL::EmuIDirect3DDevice8_CreatePalette(
    X_D3DPALETTESIZE Size,
    X_D3DPalette** ppPalette)
{
    EmuSwapFS(); // Win2k/XP FS
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
    EmuRememberOwnedGuestResource(*ppPalette);

    static int lk[4] = {
        256 * sizeof(D3DCOLOR), // D3DPALETTE_256
        128 * sizeof(D3DCOLOR), // D3DPALETTE_128
        64 * sizeof(D3DCOLOR),  // D3DPALETTE_64
        32 * sizeof(D3DCOLOR)   // D3DPALETTE_32
    };

    (*ppPalette)->Common = 0;
    (*ppPalette)->Lock = 0x8000BEEF; // emulated reference count for palettes
    (*ppPalette)->Data = (DWORD) new uint08[lk[Size]];

    EmuSwapFS(); // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetPalette
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DDevice8_SetPalette(
    DWORD Stage,
    X_D3DPalette* pPalette)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetFlickerFilter
// ******************************************************************
void WINAPI XTL::EmuIDirect3DDevice8_SetFlickerFilter(
    DWORD Filter)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetSoftDisplayFilter
// ******************************************************************
void WINAPI XTL::EmuIDirect3DDevice8_SetSoftDisplayFilter(
    BOOL Enable)
{
    EmuSwapFS(); // Win2k/XP FS
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

    EmuSwapFS(); // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DPalette8_Lock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirect3DPalette8_Lock(
    X_D3DPalette* pThis,
    D3DCOLOR** ppColors,
    DWORD Flags)
{
    D3D_TRACE("Palette_Lock");
    EmuSwapFS(); // Win2k/XP FS

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

    EmuSwapFS(); // XBox FS

    return D3D_OK;
}

// ******************************************************************
// * func: EmuD3D_MakeRequestedSpace
// ******************************************************************
// D3D::MakeRequestedSpace(RequestedSpace, RequestedSpaceEnd) is the pushbuffer
// reservation the inlined/un-hooked title paths call directly. Un-hooked it
// walks the guest CDevice fields that this HLE never initialises and faults
// (NFS Underground died in it right after SetTexture). Hand back discardable
// per-thread storage, exactly as EmuIDirect3DDevice8_MakeSpace does: callers
// still emit packets through the returned cursor, and the HLE renders through
// the host device instead of the emitted stream.
DWORD* WINAPI XTL::EmuD3D_MakeRequestedSpace(DWORD RequestedSpace, DWORD RequestedSpaceEnd)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("D3D_MakeRequestedSpace");

    alignas(64) static thread_local std::array<DWORD, 16 * 1024> requestedScratch{};
    DWORD* const pushBuffer = requestedScratch.data();

    EmuSwapFS(); // XBox FS

    return pushBuffer;
}

// ******************************************************************
// * func: EmuD3D_SetFence
// ******************************************************************
// Writes a fence token into the pushbuffer and hands it to the GPU. With no
// guest-visible pushbuffer there is nothing to write and nothing to kick.
VOID WINAPI XTL::EmuD3D_SetFence(DWORD Value)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("D3D_SetFence");
    EmuSwapFS(); // XBox FS
}

// ******************************************************************
// * func: EmuCDevice_KickOff
// ******************************************************************
// Submits the accumulated pushbuffer to the NV2A. The HLE has already issued
// the equivalent work through the host device, so completing immediately is
// correct; running the guest body instead dereferences the emulator-owned
// device through guest CDevice offsets and faults.
VOID WINAPI XTL::EmuCDevice_KickOff(VOID)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("CDevice_KickOff");
    EmuSwapFS(); // XBox FS
}

// ******************************************************************
// * func: EmuD3DResource8_BlockUntilNotBusy
// ******************************************************************
// Waits until the GPU has stopped reading a resource. Host D3D owns resource
// lifetime and has already serialised the access, so nothing is ever busy.
VOID WINAPI XTL::EmuD3DResource8_BlockUntilNotBusy(XTL::X_D3DResource* pThis)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("D3DResource_BlockUntilNotBusy");
    EmuSwapFS(); // XBox FS
}

// ******************************************************************
// * func: EmuCMiniport_IsFlipPending
// ******************************************************************
// Reports whether a queued flip is still outstanding. The host present path is
// synchronous by the time it returns, so a flip is never pending -- reporting
// otherwise is what leaves a title spinning on its own idle poll.
BOOL WINAPI XTL::EmuCMiniport_IsFlipPending(VOID)
{
    EmuSwapFS(); // Win2k/XP FS
    D3D_TRACE("CMiniport_IsFlipPending");
    EmuSwapFS(); // XBox FS

    return FALSE;
}
