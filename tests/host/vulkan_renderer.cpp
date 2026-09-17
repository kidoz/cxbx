// GPU integration coverage linked to the production Vulkan renderer. This
// deliberately bypasses guest boot and D3D8. Opt in with CXBX_TEST_VULKAN=1;
// an enabled run requires a working Windows Vulkan device (no silent fallback).
#include "vulkan/vulkan_backend.h"
#include "vulkan/vulkan_renderer.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace vk = cxbx::d3d8::vulkan;

namespace
{
constexpr unsigned int Width = 640;
constexpr unsigned int Height = 480;
constexpr unsigned int NoAttribute = 0xFFFFFFFFu;
constexpr std::uint32_t Red = 0xFFFF0000u;
constexpr std::uint32_t Green = 0xFF00FF00u;
constexpr std::uint32_t Blue = 0xFF0000FFu;

struct Vertex
{
    float x, y, z, rhw;
    std::uint32_t color;
    float u, v;
};

std::array<Vertex, 6> Quad(float x, float y, std::uint32_t color, float z = 0.5f)
{
    return { { { x, y, z, 1, color, 0, 0 },
               { x + 16, y, z, 1, color, 1, 0 },
               { x, y + 16, z, 1, color, 0, 1 },
               { x + 16, y, z, 1, color, 1, 0 },
               { x + 16, y + 16, z, 1, color, 1, 1 },
               { x, y + 16, z, 1, color, 0, 1 } } };
}

bool Draw(float x, float y, std::uint32_t color, float z = 0.5f)
{
    const auto vertices = Quad(x, y, color, z);
    return vk::RendererDrawUP(4, 2, vertices.data(), sizeof(Vertex),
                              offsetof(Vertex, color), offsetof(Vertex, u));
}

bool Pixel(unsigned int x, unsigned int y, std::uint32_t expected, bool checkAlpha = false,
           unsigned int tolerance = 0)
{
    std::vector<std::uint32_t> pixels(Width * Height);
    if(!vk::RendererReadMainTarget(pixels.data(), Width * 4))
    {
        std::fprintf(stderr, "readback failed\n");
        return false;
    }
    const auto mask = checkAlpha ? 0xFFFFFFFFu : 0xFFFFFFu;
    const auto actual = pixels[y * Width + x] & mask;
    bool matches = true;
    for(unsigned int shift = 0; shift < (checkAlpha ? 32u : 24u); shift += 8)
    {
        const auto a = (actual >> shift) & 255u;
        const auto e = (expected >> shift) & 255u;
        matches = matches && (a > e ? a - e : e - a) <= tolerance;
    }
    if(!matches)
    {
        std::fprintf(stderr, "pixel (%u,%u): expected %06X, got %06X\n",
                     x, y, expected & mask, actual);
        return false;
    }
    return true;
}

bool Run(const char* scenario)
{
    vk::RendererClear(1, Blue, 1, 0);
    if(std::strcmp(scenario, "vertex") == 0)
    {
        if(!Draw(8, 8, Red))
        {
            return false;
        }
        const auto quad = Quad(40, 8, Green);
        std::vector<Vertex> vertices;
        for(unsigned int i = 0; i < 1600; ++i)
        {
            vertices.insert(vertices.end(), quad.begin(), quad.end());
        }
        return vk::RendererDrawUP(4, static_cast<unsigned int>(vertices.size() / 3),
                                  vertices.data(), sizeof(Vertex), offsetof(Vertex, color),
                                  NoAttribute) &&
               Pixel(16, 16, Red) && Pixel(48, 16, Green);
    }
    if(std::strcmp(scenario, "index") == 0)
    {
        const auto quad = Quad(40, 8, Green);
        std::vector<std::uint16_t> indices;
        for(unsigned int i = 0; i < 22000; ++i)
        {
            for(std::uint16_t index = 0; index < 6; ++index)
            {
                indices.push_back(index);
            }
        }
        return Draw(8, 8, Red) &&
               vk::RendererDrawIndexed(4, static_cast<unsigned int>(indices.size() / 3),
                                       quad.data(), 6, sizeof(Vertex), offsetof(Vertex, color),
                                       NoAttribute, indices.data(), static_cast<unsigned int>(indices.size()), 0) &&
               Pixel(16, 16, Red) && Pixel(48, 16, Green);
    }
    if(std::strcmp(scenario, "target") == 0)
    {
        if(!Draw(8, 8, Red))
        {
            return false;
        }
        int key = 0;
        vk::RendererSetRenderTarget(&key, 64, 64);
        vk::RendererClear(1, Green, 1, 0);
        vk::RendererSetRenderTarget(nullptr, 0, 0);
        return Pixel(16, 16, Red) && Pixel(100, 100, Blue);
    }
    if(std::strcmp(scenario, "combiner") == 0)
    {
        // One combiner: r0 = C0 * diffuse; C0 maps to runtime constant 7.
        std::array<std::uint32_t, 60> shader = {};
        shader[53] = 1;
        shader[34] = 0x01040000;
        shader[0] = 0x11140000;
        shader[45] = shader[26] = 0x00000C00;
        shader[57] = 7;
        vk::RendererSetPixelShader(shader.data());
        for(unsigned int i = 0; i < 40; ++i)
        {
            const float color[4] = { static_cast<float>(i * 6) / 255.0f,
                                     static_cast<float>(255 - i * 6) / 255.0f, 0, 1 };
            vk::RendererSetPixelShaderConstant(7, color);
            if(!Draw(static_cast<float>((i % 20) * 24), static_cast<float>((i / 20) * 24), 0xFFFFFFFFu))
            {
                return false;
            }
        }
        // No readback/submit until every differently colored draw is recorded.
        for(unsigned int i = 0; i < 40; ++i)
        {
            if(!Pixel((i % 20) * 24 + 8, (i / 20) * 24 + 8, (i * 6 << 16) | ((255 - i * 6) << 8)))
            {
                return false;
            }
        }
        // Submission and a disable/re-enable must preserve the current shader.
        vk::RendererSetPixelShader(nullptr);
        vk::RendererSetPixelShader(shader.data());
        return Draw(8, 80, 0xFFFFFFFFu) && Pixel(16, 88, 0xEA1500u);
    }
    if(std::strcmp(scenario, "depth") == 0)
    {
        vk::RendererSetDepthState(0, 1);
        vk::RendererClear(2, 0, 0.25f, 0);
        if(!vk::RendererEndFrameForPresent())
        {
            return false;
        }
        return Draw(8, 8, Red) && Pixel(16, 16, Blue) &&
               vk::RendererClear(2, 0, 1.0f, 0) && Draw(8, 8, Green) && Pixel(16, 16, Green);
    }
    if(std::strcmp(scenario, "combiner_logic") == 0)
    {
        std::array<std::uint32_t, 60> shader = {};
        // Final RGB takes D, final alpha takes G, independently of count flags.
        shader[53] = 1;
        shader[8] = 4;
        shader[9] = 0x00001400;
        vk::RendererSetPixelShader(shader.data());
        if(!Draw(8, 8, 0x80402010u) || !Pixel(16, 16, 0x80402010u, true))
        {
            return false;
        }
        // A half-biased input must subtract one half, not scale the input.
        shader = {};
        shader[53] = 1;
        shader[10] = 0xFFFFFFFFu;
        shader[34] = 0x81200000;
        shader[45] = 0x000000C0;
        vk::RendererSetPixelShader(shader.data());
        // Halfway UNORM rounding can differ by one across drivers.
        if(!Draw(40, 8, 0xFFFFFFFFu) || !Pixel(48, 16, 0x808080u, false, 1))
        {
            return false;
        }
        // Output scaling applies to the separate AB result too.
        shader[34] = 0x04200000;
        shader[45] = 0x000100C0;
        vk::RendererSetPixelShader(shader.data());
        if(!Draw(72, 8, 0xFF204060u) || !Pixel(80, 16, 0x4080C0u))
        {
            return false;
        }
        // Without UNIQUE_C0, the second stage still reads constant zero.
        shader = {};
        shader[53] = 2;
        shader[10] = Red;
        shader[11] = Green;
        shader[35] = 0x01200000;
        shader[46] = 0x000000C0;
        vk::RendererSetPixelShader(shader.data());
        if(!Draw(104, 8, 0xFFFFFFFFu) || !Pixel(112, 16, Red))
        {
            return false;
        }
        shader[53] |= 0x1000;
        vk::RendererSetPixelShader(shader.data());
        if(!Draw(136, 8, 0xFFFFFFFFu) || !Pixel(144, 16, Green))
        {
            return false;
        }
        // RGB writes r0 while alpha reads its OLD blue component.
        shader = {};
        shader[53] = 2;
        shader[10] = Red;
        shader[34] = 0x04200000;
        shader[45] = 0xC0;
        shader[35] = 0x01200000;
        shader[46] = 0xC0;
        shader[1] = 0x0C200000;
        shader[27] = 0xC0;
        vk::RendererSetPixelShader(shader.data());
        if(!Draw(168, 8, 0xFF000040u) || !Pixel(176, 16, 0x40FF0000u, true))
        {
            return false;
        }
        // Runtime c0 is a valid mapping, including an explicit zero update.
        shader = {};
        shader[53] = 1;
        shader[8] = 1;
        shader[43] = Red;
        const float zero[4] = {};
        vk::RendererSetPixelShader(shader.data());
        vk::RendererSetPixelShaderConstant(0, zero);
        return Draw(200, 8, 0xFFFFFFFFu) && Pixel(208, 16, 0);
    }
    if(std::strcmp(scenario, "sampling") == 0)
    {
        // Reupload and resize a texture while earlier draws still reference
        // its previous contents. Each draw must retain its own sampled color.
        int textureKey = 0;
        const std::array<std::uint32_t, 4> greenPixels = { Green, Green, Green, Green };
        if(!vk::RendererSetTexture(0, &textureKey, &Red, 4, 1, 1, 21) ||
           !Draw(8, 40, 0xFFFFFFFFu) ||
           !vk::RendererSetTexture(0, &textureKey, &Blue, 4, 1, 1, 21) ||
           !Draw(40, 40, 0xFFFFFFFFu) ||
           !vk::RendererSetTexture(0, &textureKey, greenPixels.data(), 8, 2, 2, 21) ||
           !Draw(72, 40, 0xFFFFFFFFu) ||
           !Pixel(16, 48, Red) || !Pixel(48, 48, Blue) || !Pixel(80, 48, Green))
        {
            return false;
        }
        // Keep a texture bound on stage 1 while cycling beyond the cache's
        // capacity on stage 0. Unbound stages must retain the white dummy.
        int pinnedKey = 0;
        std::array<int, 60> keys = {};
        if(!vk::RendererSetTexture(1, &pinnedKey, &Green, 4, 1, 1, 21))
        {
            return false;
        }
        for(auto& cacheKey : keys)
        {
            if(!vk::RendererSetTexture(0, &cacheKey, &Red, 4, 1, 1, 21) ||
               !Draw(104, 40, 0xFFFFFFFFu))
            {
                return false;
            }
        }
        vk::RendererSetTextureOp(1, 0, 2); // SELECTARG1 = texture
        if(!Draw(136, 40, 0xFFFFFFFFu) || !Pixel(144, 48, Green))
        {
            return false;
        }
        vk::RendererSetTexture(1, nullptr, nullptr, 0, 0, 0, 0);
        if(!Draw(168, 40, 0xFFFFFFFFu) || !Pixel(176, 48, 0xFFFFFFu))
        {
            return false;
        }
        vk::RendererSetTextureOp(1, 0, 1);
        int key = 0;
        vk::RendererSetRenderTarget(&key, 64, 64);
        vk::RendererClear(1, Green, 1, 0);
        vk::RendererSetRenderTarget(nullptr, 0, 0);
        if(!vk::RendererSetStageRenderTargetTexture(0, &key) || !Draw(8, 8, 0xFFFFFFFFu))
        {
            return false;
        }
        vk::RendererSetTexture(0, nullptr, nullptr, 0, 0, 0, 0);
        return Draw(40, 8, Red) && Pixel(16, 16, Green) && Pixel(48, 16, Red);
    }
    if(std::strcmp(scenario, "descriptors") == 0)
    {
        // More draws than the pool can hold, with no constant changes or
        // readbacks to hide exhaustion by submitting early.
        for(unsigned int i = 0; i < 1100; ++i)
        {
            if(!Draw(8, 8, Red))
            {
                return false;
            }
        }
        return Draw(40, 8, Green) && Pixel(16, 16, Red) && Pixel(48, 16, Green);
    }
    if(std::strcmp(scenario, "raster") == 0)
    {
        vk::RendererSetRasterState(19, 5); // SRCALPHA
        vk::RendererSetRasterState(20, 6); // INVSRCALPHA
        vk::RendererSetRasterState(27, 1);
        if(!Draw(8, 8, 0x80FF0000u) || !Pixel(16, 16, 0x80007Fu, false, 1))
        {
            return false;
        }
        vk::RendererSetRasterState(27, 0);
        if(!Draw(40, 8, Red))
        {
            return false;
        }
        vk::RendererSetRasterState(168, 2); // green-only write mask
        if(!Draw(40, 8, Green) || !Pixel(48, 16, 0xFFFF00u))
        {
            return false;
        }
        vk::RendererSetRasterState(168, 15);
        vk::RendererSetRasterState(15, 1);
        vk::RendererSetRasterState(24, 128);
        const bool passes[8] = { false, false, true, true, false, false, true, true };
        for(unsigned int function = 1; function <= 8; ++function)
        {
            vk::RendererSetRasterState(25, function);
            const auto x = 8 + (function - 1) * 24;
            if(!Draw(static_cast<float>(x), 40, 0x80FF0000u) ||
               !Pixel(x + 8, 48, passes[function - 1] ? Red : Blue))
            {
                return false;
            }
        }
        vk::RendererSetRasterState(15, 0);
        vk::RendererSetRasterState(22, 2); // cull clockwise
        if(!Draw(8, 72, Red) || !Pixel(16, 80, Blue))
        {
            return false;
        }
        vk::RendererSetRasterState(22, 3); // cull counterclockwise
        return Draw(40, 72, Green) && Pixel(48, 80, Green);
    }
    if(std::strcmp(scenario, "present") == 0)
    {
        for(unsigned int i = 0; i < 8; ++i)
        {
            if(!vk::RendererClear(1, (i & 1u) ? Green : Red, 1, 0) ||
               !vk::PresentFrame(nullptr, 0, 0, 0))
            {
                return false;
            }
        }
        return Pixel(100, 100, Green);
    }
    return false;
}
} // namespace

int main(int argc, char** argv)
{
    char enabled[8] = {};
    if(GetEnvironmentVariableA("CXBX_TEST_VULKAN", enabled, sizeof(enabled)) != 1 || enabled[0] != '1')
    {
        std::puts("SKIP: set CXBX_TEST_VULKAN=1 to run on a Windows Vulkan GPU");
        return 77;
    }
    if(argc != 2)
    {
        return 1;
    }
    HWND window = CreateWindowExW(0, L"STATIC", L"Vulkan renderer regression", WS_POPUP,
                                  0, 0, Width, Height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if(window == nullptr)
    {
        return 1;
    }
    const bool ready = vk::Initialize(window, true) && vk::RendererValid();
    const bool passed = ready && Run(argv[1]);
    vk::Shutdown();
    DestroyWindow(window);
    const unsigned int errors = vk::ValidationErrorCount();
    if(errors != 0)
    {
        std::fprintf(stderr, "Vulkan validation reported %u error(s)\n", errors);
    }
    return passed && errors == 0 ? 0 : 1;
}
