// hle_resolve -- asserts the emulator's OOVPA/HLE pass actually resolved and
// patched the XDK library functions this image links.
//
// Built with the real XDK 5849 toolchain and linking the real d3d8.lib /
// dsound.lib, this image contains byte-identical library code to what real
// 5849 titles ship. At boot the emulator's HLE pass scans the image against
// its OOVPA signature database (D3D8.1.0.5849.inl et al.) and overwrites each
// located function's prologue with `jmp <host wrapper>` (0xE9 rel32 into the
// emulator's own module, far above guest address space -- see
// EmuInstallWrapper in Emu.cpp). The probe reads its own function prologues
// and checks, per function, that the patch landed.
//
// Expectations encode current reality: `1` = signature must resolve (a FAIL
// is a signature/HLE regression), `0` = documented gap (a FAIL here means the
// coverage IMPROVED -- update the expectation and the golden). DSOUND has no
// 5849 OOVPA table at all, so every DSOUND entry currently expects 0.
#include <xtl.h>
#include <dsound.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

extern "C" ULONG __cdecl DbgPrint(const char *Format, ...);
extern "C" VOID __stdcall HalReturnToFirmware(ULONG Routine);

// --------------------------------------------------------------------------
// trace harness (same protocol as every probe; see tests/suite/README.md)
// --------------------------------------------------------------------------
static HANDLE g_trace = INVALID_HANDLE_VALUE;
static int g_checks = 0;
static int g_fails = 0;

static void emit(const char *line)
{
    if (g_trace != INVALID_HANDLE_VALUE) {
        DWORD cb;
        WriteFile(g_trace, line, (DWORD)strlen(line), &cb, NULL);
        WriteFile(g_trace, "\n", 1, &cb, NULL);
    }
    DbgPrint("XT| %s\n", line);
}

static void emitf(const char *fmt, ...)
{
    char line[480];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(line, sizeof(line) - 1, fmt, ap);
    line[sizeof(line) - 1] = 0;
    va_end(ap);
    emit(line);
}

static void chk(const char *name, int expect, int got)
{
    g_checks++;
    if (expect != got)
        g_fails++;
    emitf("CHK  %s expect=%d got=%d %s", name, expect, got,
          (expect == got) ? "PASS" : "FAIL");
}

// --------------------------------------------------------------------------
// HLE-patch detection
// --------------------------------------------------------------------------
// Anything at/above this is host emulator space; the guest image + RAM window
// live far below it.
#define GUEST_TOP 0x04000000UL

static DWORD jump_target(const unsigned char *p)
{
    return (DWORD)(p + 5) + *(const DWORD *)(p + 1);
}

static int is_hle_patched(const void *fn)
{
    const unsigned char *p = (const unsigned char *)fn;
    if (p[0] != 0xE9)
        return 0;
    DWORD tgt = jump_target(p);
    if (tgt >= GUEST_TOP)
        return 1;
    // Follow one guest-local jump (linker thunk) and re-test.
    p = (const unsigned char *)tgt;
    return p[0] == 0xE9 && jump_target(p) >= GUEST_TOP;
}

// --------------------------------------------------------------------------
// function tables
// --------------------------------------------------------------------------
typedef struct {
    const char *name;
    const void *fn;
    int expect; // 1 = must be HLE-patched, 0 = documented gap
} XFUNC;

#define XF(f, e) { #f, (const void *)(f), (e) }

// Every entry below has an OOVPA registered in D3D8.1.0.5849.inl. An
// expect=0 entry is SIGNATURE DEBT: its table entry's signature does not
// byte-match the 5849 library code, so it never resolves on real 5849 titles
// either. Fixing one (tools/oovpa/gen_oovpa.py) flips its check to FAIL --
// update the expectation to 1 and refresh the golden. 2026-07-08: the original
// nine-entry debt list was regenerated down to one; SetRenderStateNotInline
// stays 0 deliberately (the "SetRenderState_Simple" OOVPA matches an internal
// __fastcall(Method, Value) helper, not the stdcall D3DRS-enum entry point,
// so a fresh signature for it must wait for a matching Emu impl).
static const XFUNC k_d3d[] = {
    // core render path + the hand-authored 5849/5933 signatures
    XF(Direct3D_CreateDevice, 1),
    XF(D3DDevice_Clear, 1),
    XF(D3DDevice_Swap, 1),
    XF(D3DDevice_SetRenderTarget, 1),
    XF(D3DDevice_MakeSpace, 1),
    XF(D3DDevice_Begin, 1),
    XF(D3DDevice_SetVertexData2f, 1),
    XF(D3DDevice_SetVertexData4f, 1),
    XF(D3DDevice_SetVertexDataColor, 1),
    XF(D3DDevice_End, 1),
    // generated 5849 signature set
    XF(Direct3D_GetAdapterDisplayMode, 1),
    XF(D3DDevice_GetBackBuffer2, 1),
    XF(D3DDevice_SetViewport, 1),
    XF(D3DDevice_SetShaderConstantMode, 1),
    XF(D3DDevice_GetRenderTarget2, 1),
    XF(D3DDevice_GetDepthStencilSurface2, 1),
    XF(D3DDevice_GetTile, 1),
    XF(D3DDevice_CreateVertexShader, 1),
    XF(D3DDevice_CreatePixelShader, 1),
    XF(D3DDevice_SetPixelShader, 1),
    XF(D3DDevice_CreateTexture2, 1),
    XF(D3DDevice_SetIndices, 1),
    XF(D3DDevice_SetTexture, 1),
    XF(D3DDevice_GetDisplayMode, 1),
    XF(D3DDevice_EnableOverlay, 1),
    XF(D3DDevice_CreateVertexBuffer2, 1),
    XF(D3DDevice_UpdateOverlay, 1),
    XF(D3DDevice_GetOverlayUpdateStatus, 1),
    XF(D3DDevice_BlockUntilVerticalBlank, 1),
    XF(D3DDevice_SetVerticalBlankCallback, 1),
    XF(D3DDevice_SetTextureState_TexCoordIndex, 1),
    XF(D3DDevice_SetRenderState_CullMode, 1),
    XF(D3DDevice_SetRenderState_NormalizeNormals, 1),
    XF(D3DDevice_SetRenderState_TextureFactor, 1),
    XF(D3DDevice_SetRenderState_ZBias, 1),
    XF(D3DDevice_SetRenderState_EdgeAntiAlias, 1),
    XF(D3DDevice_SetRenderState_FillMode, 1),
    XF(D3DDevice_SetRenderState_FogColor, 1),
    XF(D3DDevice_SetRenderState_Dxt1NoiseEnable, 1),
    XF(D3DDevice_SetRenderStateNotInline, 0),
    XF(D3DDevice_SetRenderState_ZEnable, 1),
    XF(D3DDevice_SetRenderState_StencilEnable, 1),
    XF(D3DDevice_SetRenderState_MultiSampleAntiAlias, 1),
    XF(D3DDevice_SetRenderState_ShadowFunc, 1),
    XF(D3DDevice_SetRenderState_YuvEnable, 1),
    XF(D3DDevice_SetTransform, 1),
    XF(D3DDevice_GetTransform, 1),
    XF(D3DDevice_SetStreamSource, 1),
    XF(D3DDevice_SetVertexShader, 1),
    XF(D3DDevice_DrawVertices, 1),
    XF(D3DDevice_DrawVerticesUP, 1),
    XF(D3DDevice_SetLight, 1),
    XF(D3DDevice_DrawIndexedVertices, 1),
    XF(D3DDevice_SetMaterial, 1),
    XF(D3DDevice_LightEnable, 1),
    XF(D3DVertexBuffer_Lock2, 1),
    XF(D3DResource_Register, 1),
    XF(D3DResource_Release, 1),
    XF(D3DResource_IsBusy, 1),
    XF(D3DSurface_GetDesc, 1),
    XF(D3DSurface_LockRect, 1),
    XF(D3DBaseTexture_GetLevelCount, 1),
    XF(D3DTexture_GetSurfaceLevel2, 1),
    XF(D3DTexture_LockRect, 1),
};

// DSOUND: no 5849 OOVPA table exists in the HLE database, so nothing may
// resolve. When a DSOUND 5849 table lands, flip these to 1.
static const XFUNC k_dsound[] = {
    XF(DirectSoundCreate, 0),
    XF(DirectSoundCreateBuffer, 0),
    XF(DirectSoundCreateStream, 0),
    XF(DirectSoundDoWork, 0),
    XF(IDirectSound_CreateSoundBuffer, 0),
};

static int run_table(const char *prefix, const XFUNC *t, int n)
{
    int resolved = 0;
    char name[128];
    for (int i = 0; i < n; i++) {
        int got = is_hle_patched(t[i].fn);
        resolved += got;
        _snprintf(name, sizeof(name) - 1, "%s.%s.hle", prefix, t[i].name);
        name[sizeof(name) - 1] = 0;
        chk(name, t[i].expect, got);
    }
    emitf("EV   %s.resolved n=%d/%d", prefix, resolved, n);
    return resolved;
}

void __cdecl main()
{
    g_trace = CreateFile("D:\\hle_resolve.trace", GENERIC_WRITE, 0, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    emit("#suite xbox-conformance v1");
    emit("#probe hle_resolve");
    emit("EV   boot toolchain=xdk5849 libs=d3d8+dsound");

    run_table("d3d", k_d3d, sizeof(k_d3d) / sizeof(k_d3d[0]));
    run_table("dsound", k_dsound, sizeof(k_dsound) / sizeof(k_dsound[0]));

    emitf("#result hle_resolve verdict=%s checks=%d fail=%d",
          g_fails ? "FAIL" : "PASS", g_checks, g_fails);
    emit("#end");

    if (g_trace != INVALID_HANDLE_VALUE)
        CloseHandle(g_trace);

    HalReturnToFirmware(2);
}
