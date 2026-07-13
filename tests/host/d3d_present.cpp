#include "core/d3d_present.h"
#include "core/d3d_pixel_shader.h"
#include "core/d3d_texture.h"

#include <cstdio>

int main()
{
    cxbx::d3d::XboxPixelShaderDefinition pixelShader{};
    pixelShader[53] = 1;
    pixelShader[54] = 1;
    pixelShader[34] = 0xC8200000u;
    pixelShader[45] = 0x000000C0u;
    if(cxbx::d3d::ClassifyPixelShaderFallback(pixelShader) !=
       cxbx::d3d::PixelShaderFallback::Texture)
    {
        std::fputs("a direct T0 combiner must select the texture without diffuse modulation\n",
                   stderr);
        return 1;
    }

    pixelShader = {};
    pixelShader[53] = 2;
    pixelShader[54] = 1;
    pixelShader[34] = 0xC4200000u;
    pixelShader[45] = 0x000000D0u;
    pixelShader[35] = 0xC8CD0000u;
    pixelShader[46] = 0x000100C0u;
    if(cxbx::d3d::ClassifyPixelShaderFallback(pixelShader) !=
       cxbx::d3d::PixelShaderFallback::TextureModulate2X)
    {
        std::fputs("a shifted T0 times lighting combiner must use MODULATE2X\n", stderr);
        return 1;
    }

    pixelShader[54] = 0x21u;
    pixelShader[34] = 0x49450000u;
    if(cxbx::d3d::ClassifyPixelShaderFallback(pixelShader) !=
       cxbx::d3d::PixelShaderFallback::DualTextureModulate2X)
    {
        std::fputs("an active T1 detail combiner must preserve the second texture\n", stderr);
        return 1;
    }

    pixelShader = {};
    pixelShader[53] = 4;
    pixelShader[54] = 0x8001u;
    pixelShader[35] = 0xCBD52035u;
    if(cxbx::d3d::ClassifyPixelShaderFallback(pixelShader) !=
       cxbx::d3d::PixelShaderFallback::TextureModulate2XStage3)
    {
        std::fputs("an active T3 lookup combiner must preserve the stage-3 texture\n", stderr);
        return 1;
    }

    if(cxbx::d3d::CompressedTextureLevelSize(0, 4, 8) != 0 ||
       cxbx::d3d::CompressedTextureLevelSize(4, 0, 8) != 0 ||
       cxbx::d3d::CompressedTextureLevelSize(1, 2, 8) != 8 ||
       cxbx::d3d::CompressedTextureLevelSize(2, 2, 8) != 8 ||
       cxbx::d3d::CompressedTextureLevelSize(4, 4, 8) != 8 ||
       cxbx::d3d::CompressedTextureLevelSize(5, 4, 8) != 16 ||
       cxbx::d3d::CompressedTextureLevelSize(4, 5, 16) != 32 ||
       cxbx::d3d::CompressedTextureMipChainSize(8, 4, 8, 4) != 40 ||
       cxbx::d3d::CompressedTextureMipChainSize(1, 2, 8, 3) != 24)
    {
        std::fputs("compressed texture sizes must round up to complete 4x4 blocks\n", stderr);
        return 1;
    }

    if(cxbx::d3d::XboxPhysicalAddress(0x80012345u) != 0x00012345u ||
       cxbx::d3d::XboxPhysicalAddress(0xF4012345u) != 0x00012345u ||
       cxbx::d3d::XboxPhysicalAddress(0x03FFFFFFu) != 0x03FFFFFFu)
    {
        std::fputs("Xbox texture aliases must normalize to the 64 MiB physical aperture\n",
                   stderr);
        return 1;
    }

    if(cxbx::d3d::CpuFallbackTextureUsable(false, 0, true, true, true) ||
       cxbx::d3d::CpuFallbackTextureUsable(true, 4, true, true, true) ||
       cxbx::d3d::CpuFallbackTextureUsable(true, 0, false, true, true) ||
       cxbx::d3d::CpuFallbackTextureUsable(true, 0, true, false, true) ||
       cxbx::d3d::CpuFallbackTextureUsable(true, 0, true, true, false) ||
       !cxbx::d3d::CpuFallbackTextureUsable(true, 3, true, true, true) ||
       cxbx::d3d::SelectCpuFallbackMaterial(false) !=
           cxbx::d3d::CpuFallbackMaterial::Diffuse ||
       cxbx::d3d::SelectCpuFallbackMaterial(true) !=
           cxbx::d3d::CpuFallbackMaterial::TextureModulate)
    {
        std::fputs("CPU fallback must reject unusable stage-0 texture coordinates\n", stderr);
        return 1;
    }

    constexpr auto steps = cxbx::d3d::PresentSceneSteps();
    if(steps[0] != cxbx::d3d::PresentSceneStep::EndScene ||
       steps[1] != cxbx::d3d::PresentSceneStep::CaptureMirror ||
       steps[2] != cxbx::d3d::PresentSceneStep::Present ||
       steps[3] != cxbx::d3d::PresentSceneStep::BlitMirror ||
       steps[4] != cxbx::d3d::PresentSceneStep::BeginScene)
    {
        std::fputs("D3D8 presentation must capture before Present and blit afterward\n", stderr);
        return 1;
    }
    if(cxbx::d3d::NextPresentDeadline(100, 0, 16) != 116 ||
       cxbx::d3d::NextPresentDeadline(110, 116, 16) != 116 ||
       cxbx::d3d::NextPresentDeadline(116, 116, 16) != 132 ||
       cxbx::d3d::NextPresentDeadline(149, 116, 16) != 164 ||
       cxbx::d3d::NextPresentDeadline(149, 116, 0) != 116)
    {
        std::fputs("present pacing must advance past elapsed display periods\n", stderr);
        return 1;
    }
    if(!cxbx::d3d::MirrorWindowEnabled(nullptr))
    {
        std::fputs("unset mirror setting must enable visible HLE frames\n", stderr);
        return 1;
    }
    if(cxbx::d3d::MirrorWindowEnabled("0"))
    {
        std::fputs("CXBX_D3D_WINDOW=0 must disable the mirror\n", stderr);
        return 1;
    }
    if(!cxbx::d3d::MirrorWindowEnabled("1"))
    {
        std::fputs("CXBX_D3D_WINDOW=1 must enable the mirror\n", stderr);
        return 1;
    }
    if(cxbx::d3d::ControlProbeEnabled(nullptr) ||
       cxbx::d3d::ControlProbeEnabled("0") ||
       !cxbx::d3d::ControlProbeEnabled("1") ||
       cxbx::d3d::ControlProbeEnabled("10"))
    {
        std::fputs("CXBX_D3D_CONTROL_PROBE must accept only the exact value 1\n", stderr);
        return 1;
    }

    return 0;
}
