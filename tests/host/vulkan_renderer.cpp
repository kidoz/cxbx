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
                              offsetof(Vertex, color), offsetof(Vertex, u),
                              0, NoAttribute);
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
                                  NoAttribute, 0, NoAttribute) &&
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
                                       NoAttribute, indices.data(), static_cast<unsigned int>(indices.size()), 0, 0, NoAttribute) &&
               Pixel(16, 16, Red) && Pixel(48, 16, Green);
    }
    if(std::strcmp(scenario, "target") == 0)
    {
        if(!Draw(8, 8, Red))
        {
            return false;
        }
        int key = 0;
        vk::RendererSetRenderTarget(&key, 64, 64, nullptr, &key);
        vk::RendererClear(1, Green, 1, 0);
        vk::RendererSetRenderTarget(nullptr, 0, 0, nullptr, nullptr);
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
    if(std::strcmp(scenario, "bump") == 0)
    {
        // BUMPENVMAP displacement through the PS texture modes: a 1x1 bump
        // source (du = dv = 1, B = 0) shifts a 2x1 stage sample by the
        // stage's matrix; BUMPENVMAP_LUM then scales the sample by
        // B*lScale + lOffset (texbem/texbeml). The check pixel sits at
        // u = 0.28125, so M00 = 0.5 moves it from the red to the green
        // texel under any address or filter mode.
        int bumpKey = 0;
        int sampleKey = 1;
        const std::uint32_t bumpTexel = 0xFFFFFF00u; // R=255 G=255 B=0
        const std::uint32_t sampleTexels[2] = { Red, Green };
        if(!vk::RendererSetTexture(0, &bumpKey, &bumpTexel, 4, 1, 1, 21) ||
           !vk::RendererSetTexture(1, &sampleKey, sampleTexels, 8, 2, 1, 21))
        {
            return false;
        }
        // r0.rgb = t1 (A = t1, B = ONE); both stages sample plain first.
        std::array<std::uint32_t, 60> shader = {};
        shader[53] = 1;
        shader[34] = 0x09200000u;
        shader[45] = 0x00000C00u;
        shader[54] = 0x21u; // stage 0/1 = PROJECT2D
        vk::RendererSetPixelShader(shader.data());
        if(!Draw(8, 8, 0xFFFFFFFFu) || !Pixel(12, 16, Red))
        {
            return false;
        }
        // Stage 1 in BUMPENVMAP mode with MAT00 = 0.5: the matrices are
        // device state and survive the new shader revision.
        shader[54] = 0xC1u; // stage 1 = BUMPENVMAP
        vk::RendererSetPixelShader(shader.data());
        vk::RendererSetBumpEnvState(1, 0, 0x3F000000u); // MAT00 = 0.5f
        if(!Draw(40, 8, 0xFFFFFFFFu) || !Pixel(44, 16, Green))
        {
            return false;
        }
        // BUMPENVMAP_LUM scales the sampled texel; the bump texel's B is
        // zero, so the 0.5 offset halves the green channel.
        shader[54] = 0xE1u; // stage 1 = BUMPENVMAP_LUM
        vk::RendererSetPixelShader(shader.data());
        vk::RendererSetBumpEnvState(1, 4, 0x3F800000u); // LSCALE = 1.0f
        vk::RendererSetBumpEnvState(1, 5, 0x3F000000u); // LOFFSET = 0.5f
        if(!Draw(72, 8, 0xFFFFFFFFu) || !Pixel(76, 16, 0x008000u, false, 1))
        {
            return false;
        }
        // Stage 2 bump with the PSInputTexture-selected source 0: the same
        // displacement through stage 2's own matrix and texture.
        if(!vk::RendererSetTexture(2, &sampleKey, sampleTexels, 8, 2, 1, 21))
        {
            return false;
        }
        shader[34] = 0x0A200000u; // r0.rgb = t2
        shader[54] = 0x1821u;     // stage 2 = BUMPENVMAP
        shader[56] = 0u;          // stage 2 source = t0
        vk::RendererSetPixelShader(shader.data());
        vk::RendererSetBumpEnvState(2, 0, 0x3F000000u);
        if(!Draw(104, 8, 0xFFFFFFFFu) || !Pixel(108, 16, Green))
        {
            return false;
        }
        return true;
    }
    if(std::strcmp(scenario, "fog") == 0)
    {
        // Post-combiner fog blend: the per-vertex o[FOG] value rides a
        // trailing float. Mode NONE passes the factor through clamped
        // (1 = shade, 0 = fog color); LINEAR runs the table lookup over the
        // view-space coordinate with the FOGSTART/FOGEND parameters.
        struct FogVertex
        {
            float x, y, z, rhw;
            std::uint32_t color;
            float u, v;
            float fog;
        };
        const auto fogQuad = [&](float ox, float oy, float fogValue) {
            const FogVertex verts[6] = {
                { ox, oy, 0.5f, 1, Red, 0, 0, fogValue },
                { ox + 16, oy, 0.5f, 1, Red, 1, 0, fogValue },
                { ox, oy + 16, 0.5f, 1, Red, 0, 1, fogValue },
                { ox + 16, oy, 0.5f, 1, Red, 1, 0, fogValue },
                { ox + 16, oy + 16, 0.5f, 1, Red, 1, 1, fogValue },
                { ox, oy + 16, 0.5f, 1, Red, 0, 1, fogValue },
            };
            return vk::RendererDrawUP(4, 2, verts, sizeof(FogVertex),
                                      offsetof(FogVertex, color),
                                      offsetof(FogVertex, u), 0,
                                      offsetof(FogVertex, fog));
        };
        const std::uint32_t green = 0xFF00FF00u; // fog color (0,255,0)
        vk::RendererSetFogColor(green);
        // Mode NONE: the shader-written factor blends directly.
        vk::RendererSetFogState(1, 0, 0x3F800000u, 0x3F800000u, 0x3F800000u);
        if(!fogQuad(8, 8, 1.0f) || !Pixel(16, 16, Red) ||
           !fogQuad(40, 8, 0.0f) || !Pixel(48, 16, Green & 0xFFFFFFu) ||
           !fogQuad(72, 8, 0.5f) || !Pixel(76, 16, 0x808000u, false, 1))
        {
            return false;
        }
        // Mode LINEAR: factor = (end - v) / (end - start); v = 0.25 over
        // [0,1] gives 0.75 shade + 0.25 fog.
        vk::RendererSetFogState(1, 3, 0u, 0x3F800000u, 0x3F800000u);
        if(!fogQuad(104, 8, 0.25f) || !Pixel(108, 16, 0xBF4000u, false, 1))
        {
            return false;
        }
        // Disabling fog leaves the raw shade even with a fogged vertex.
        vk::RendererSetFogState(0, 0, 0u, 0x3F800000u, 0x3F800000u);
        return fogQuad(136, 8, 0.0f) && Pixel(140, 16, Red);
    }
    if(std::strcmp(scenario, "cube") == 0)
    {
        // Cube-map mirroring: six solid faces uploaded face-major, sampled
        // by direction through the vec3 texcoord layout. D3D8 face order:
        // +X, -X, +Y, -Y, +Z, -Z.
        struct CubeVertex
        {
            float x, y, z, rhw;
            std::uint32_t color;
            float dirX, dirY, dirZ;
        };
        constexpr unsigned int kCubeSize = 64;
        std::vector<std::uint32_t> faces(
            static_cast<std::size_t>(kCubeSize) * kCubeSize * 6);
        for(std::size_t i = 0; i < faces.size(); ++i)
        {
            faces[i] = Blue;
        }
        const auto fillFace = [&](unsigned int face, std::uint32_t color) {
            const std::size_t begin =
                static_cast<std::size_t>(face) * kCubeSize * kCubeSize;
            std::fill(faces.begin() + static_cast<std::ptrdiff_t>(begin),
                      faces.begin() + static_cast<std::ptrdiff_t>(
                                          begin + kCubeSize * kCubeSize),
                      color);
        };
        fillFace(0, Red);    // +X
        fillFace(4, Green);  // +Z
        fillFace(5, 0xFFFFFF00u); // -Z (yellow)

        int cubeKey = 0;
        const auto cubeQuad = [&](float x, float y, float dx, float dy,
                                  float dz) {
            const CubeVertex verts[6] = {
                { x, y, 0.5f, 1, 0xFFFFFFFFu, dx, dy, dz },
                { x + 16, y, 0.5f, 1, 0xFFFFFFFFu, dx, dy, dz },
                { x, y + 16, 0.5f, 1, 0xFFFFFFFFu, dx, dy, dz },
                { x + 16, y, 0.5f, 1, 0xFFFFFFFFu, dx, dy, dz },
                { x + 16, y + 16, 0.5f, 1, 0xFFFFFFFFu, dx, dy, dz },
                { x, y + 16, 0.5f, 1, 0xFFFFFFFFu, dx, dy, dz },
            };
            return vk::RendererDrawUP(4, 2, verts, sizeof(CubeVertex),
                                      offsetof(CubeVertex, color),
                                      offsetof(CubeVertex, dirX), 3,
                                      NoAttribute);
        };
        if(!vk::RendererSetCubeTexture(0, &cubeKey, faces.data(),
                                       kCubeSize * 4, kCubeSize, 21) ||
           !cubeQuad(8, 8, 0.0f, 0.0f, 1.0f) ||   // +Z -> green
           !cubeQuad(40, 8, 1.0f, 0.0f, 0.0f) ||  // +X -> red
           !cubeQuad(72, 8, 0.0f, 0.0f, -1.0f) || // -Z -> yellow
           !Pixel(16, 16, Green) || !Pixel(48, 16, Red) ||
           !Pixel(80, 16, 0xFFFF00u))
        {
            return false;
        }
        // Rebinding a 2D texture on the stage must restore 2D sampling.
        if(!vk::RendererSetTexture(0, &cubeKey, &Red, 4, 1, 1, 21) ||
           !cubeQuad(8, 40, 0.0f, 0.0f, 1.0f) || !Pixel(16, 48, Red))
        {
            return false;
        }
        // And back to the cube: the cached entry must survive the 2D bind.
        if(!(vk::RendererSetCubeTexture(0, &cubeKey, faces.data(),
                                        kCubeSize * 4, kCubeSize, 21) &&
             cubeQuad(40, 40, 0.0f, 0.0f, 1.0f) && Pixel(48, 48, Green)))
        {
            return false;
        }
        // DXT1 cube: 4x4 faces = one solid block per face (color0 expands
        // everywhere). +Z block decodes green, -Z yellow.
        constexpr unsigned int kDxtSize = 4;
        std::vector<std::uint8_t> dxtFaces(8 * 6, 0);
        const auto dxtBlock = [](std::uint16_t rgb565,
                                 std::uint8_t out[8]) {
            // One 8-byte DXT1 block: color0, color1, four all-zero selector
            // bytes -- every texel takes color0.
            out[0] = static_cast<std::uint8_t>(rgb565 & 0xFF);
            out[1] = static_cast<std::uint8_t>(rgb565 >> 8);
            out[2] = out[3] = out[4] = out[5] = out[6] = out[7] = 0;
        };
        const auto green565 = 0x07E0; // 0 111111 00000 -> (0,255,0)
        const auto yellow565 = 0xFFE0; // 11111 111111 00000 -> (255,255,0)
        dxtBlock(green565, dxtFaces.data() + 4 * 8);
        dxtBlock(yellow565, dxtFaces.data() + 5 * 8);
        return vk::RendererSetCubeTexture(0, &cubeKey, dxtFaces.data(), 8,
                                          kDxtSize, 0x31545844u /*'DXT1'*/) &&
               cubeQuad(8, 72, 0.0f, 0.0f, 1.0f) &&
               cubeQuad(40, 72, 0.0f, 0.0f, -1.0f) &&
               Pixel(16, 80, Green) && Pixel(48, 80, 0xFFFF00u);
    }
    if(std::strcmp(scenario, "shadow") == 0)
    {
        // Shadow-map sampling: the RT's depth image (cleared to 0.5) is
        // bound by its depth key; LESSEQUAL compares depth <= r, so
        // r=0.7 lights and r=0.3 stays dark. Single-vec4 texcoord mode
        // (u, v, r, q).
        int rtKey = 0;
        int depthKey = 0;
        vk::RendererSetRenderTarget(&rtKey, 64, 64, &depthKey, nullptr);
        vk::RendererClear(2, 0, 0.5f, 0); // D3DCLEAR_ZBUFFER
        vk::RendererSetRenderTarget(nullptr, 0, 0, nullptr, nullptr);
        vk::RendererSetShadowFunc(4); // D3DCMP_LESSEQUAL
        struct ShadowVertex
        {
            float x, y, z, rhw;
            std::uint32_t color;
            float u, v, r, q;
        };
        const auto shadowQuad = [&](float ox, float oy, float ref) {
            const ShadowVertex verts[6] = {
                { ox, oy, 0.5f, 1, 0xFFFFFFFFu, 0, 0, ref, 1 },
                { ox + 16, oy, 0.5f, 1, 0xFFFFFFFFu, 1, 0, ref, 1 },
                { ox, oy + 16, 0.5f, 1, 0xFFFFFFFFu, 0, 1, ref, 1 },
                { ox + 16, oy, 0.5f, 1, 0xFFFFFFFFu, 1, 0, ref, 1 },
                { ox + 16, oy + 16, 0.5f, 1, 0xFFFFFFFFu, 1, 1, ref, 1 },
                { ox, oy + 16, 0.5f, 1, 0xFFFFFFFFu, 0, 1, ref, 1 },
            };
            return vk::RendererDrawUP(4, 2, verts, sizeof(ShadowVertex),
                                      offsetof(ShadowVertex, color),
                                      offsetof(ShadowVertex, u), 5,
                                      NoAttribute);
        };
        if(!vk::RendererSetDepthStageTexture(0, &depthKey) ||
           !shadowQuad(8, 8, 0.7f) ||   // 0.5 <= 0.7 -> lit
           !shadowQuad(40, 8, 0.3f) ||  // 0.5 <= 0.3 -> dark
           !Pixel(16, 16, 0xFFFFFFu) || !Pixel(48, 16, 0x000000u))
        {
            return false;
        }
        // An ordinary 2D bind must clear the shadow mode.
        int texKey = 0;
        return vk::RendererSetTexture(0, &texKey, &Green, 4, 1, 1, 21) &&
               shadowQuad(8, 40, 0.7f) && Pixel(16, 48, Green);
    }
    if(std::strcmp(scenario, "volume") == 0)
    {
        // Volume-texture sampling: 4x4x4 slices of distinct colors,
        // sampled by texcoord xyz (slice 0 red, slice 3 blue).
        struct VolumeVertex
        {
            float x, y, z, rhw;
            std::uint32_t color;
            float u, v, w;
        };
        constexpr unsigned int kW = 4, kH = 4, kD = 4;
        std::vector<std::uint32_t> slices(
            static_cast<std::size_t>(kW) * kH * kD, Red);
        for(auto& texel : slices)
        {
            texel = Red;
        }
        const auto fillSlice = [&](unsigned int slice, std::uint32_t color) {
            const std::size_t begin =
                static_cast<std::size_t>(slice) * kW * kH;
            std::fill(slices.begin() + static_cast<std::ptrdiff_t>(begin),
                      slices.begin() + static_cast<std::ptrdiff_t>(
                                          begin + kW * kH),
                      color);
        };
        fillSlice(3, Blue);
        int volumeKey = 0;
        const auto volumeQuad = [&](float x, float y, float u, float v,
                                    float w) {
            const VolumeVertex verts[6] = {
                { x, y, 0.5f, 1, 0xFFFFFFFFu, u, v, w },
                { x + 16, y, 0.5f, 1, 0xFFFFFFFFu, u, v, w },
                { x, y + 16, 0.5f, 1, 0xFFFFFFFFu, u, v, w },
                { x + 16, y, 0.5f, 1, 0xFFFFFFFFu, u, v, w },
                { x + 16, y + 16, 0.5f, 1, 0xFFFFFFFFu, u, v, w },
                { x, y + 16, 0.5f, 1, 0xFFFFFFFFu, u, v, w },
            };
            return vk::RendererDrawUP(4, 2, verts, sizeof(VolumeVertex),
                                      offsetof(VolumeVertex, color),
                                      offsetof(VolumeVertex, u), 3,
                                      NoAttribute);
        };
        if(!vk::RendererSetVolumeTexture(0, &volumeKey, slices.data(),
                                         kW * 4, kW, kH, kD, 21) ||
           !volumeQuad(8, 8, 0.5f, 0.5f, 0.125f) ||  // slice 0 -> red
           !volumeQuad(40, 8, 0.5f, 0.5f, 0.875f) || // slice 3 -> blue
           !Pixel(16, 16, Red) || !Pixel(48, 16, Blue))
        {
            return false;
        }
        // A 2D rebind must clear the volume mode.
        int texKey = 0;
        return vk::RendererSetTexture(0, &texKey, &Green, 4, 1, 1, 21) &&
               volumeQuad(8, 40, 0.5f, 0.5f, 0.125f) && Pixel(16, 48, Green);
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
        vk::RendererSetRenderTarget(&key, 64, 64, nullptr, &key);
        vk::RendererClear(1, Green, 1, 0);
        vk::RendererSetRenderTarget(nullptr, 0, 0, nullptr, nullptr);
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
    if(std::strcmp(scenario, "state_block") == 0)
    {
        // P6 state blocks: capture the live state, mutate it, and verify the
        // apply restores the write mask and rebinds the stage texture from
        // the upload cache.
        int textureKey = 0;
        if(!vk::RendererSetTexture(0, &textureKey, &Red, 4, 1, 1, 21) ||
           !Draw(8, 8, 0xFFFFFFFFu) || !Pixel(16, 16, Red))
        {
            return false;
        }
        if(!vk::RendererStateBlockCapture(1))
        {
            return false;
        }
        vk::RendererSetTexture(0, nullptr, nullptr, 0, 0, 0, 0);
        vk::RendererSetRasterState(168, 2); // green-only write mask
        if(!Draw(40, 8, Green) || !Pixel(48, 16, 0x00FFFFu))
        {
            return false;
        }
        if(!vk::RendererStateBlockApply(1))
        {
            return false;
        }
        // The restored mask must write all channels again and the restored
        // stage-0 binding must sample the red texture (not the white dummy).
        return Draw(40, 8, 0xFFFFFFFFu) && Pixel(48, 16, Red);
    }
    if(std::strcmp(scenario, "lifetime") == 0)
    {
        // P6 resource lifetime: a released host key must not serve stale
        // cached content, and its registry entry must be gone.
        int keyA = 0;
        if(!vk::RendererSetTexture(0, &keyA, &Red, 4, 1, 1, 21) ||
           !Draw(8, 8, 0xFFFFFFFFu) || !Pixel(16, 16, Red))
        {
            return false;
        }
        vk::RendererReleaseTexture(&keyA);
        // The released key is no longer cached: a rebind binds white, not
        // the red upload that existed before the release.
        if(vk::RendererRebindStageTexture(0, &keyA))
        {
            return false;
        }
        if(!Draw(40, 8, 0xFFFFFFFFu) || !Pixel(48, 16, 0xFFFFFFu))
        {
            return false;
        }
        // A recycled pointer re-uploads fresh content normally.
        if(!vk::RendererSetTexture(0, &keyA, &Blue, 4, 1, 1, 21) ||
           !Draw(72, 8, 0xFFFFFFFFu) || !Pixel(80, 16, Blue))
        {
            return false;
        }
        // Render-target release: the registry entry and any stage override
        // view referencing it are gone; sampling falls back to the
        // ordinary stage texture (the white dummy here). The RT is keyed by
        // its owning texture (slice C), so the override resolves by that.
        int rtKey = 0;
        vk::RendererSetRenderTarget(&rtKey, 64, 64, nullptr, &rtKey);
        vk::RendererClear(1, Green, 1, 0);
        vk::RendererSetRenderTarget(nullptr, 0, 0, nullptr, nullptr);
        if(!vk::RendererSetStageRenderTargetTexture(0, &rtKey))
        {
            return false;
        }
        vk::RendererReleaseRenderTarget(&rtKey);
        if(vk::RendererSetStageRenderTargetTexture(0, &rtKey))
        {
            return false;
        }
        return Draw(104, 8, 0xFFFFFFFFu) && Pixel(112, 16, 0xFFFFFFu);
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
    if(std::strcmp(scenario, "overlay") == 0)
    {
        // Padded rows, BGRA channel order, scaling and source ownership. An
        // overlay must bypass guest viewport, target and color/alpha state.
        std::array<std::uint32_t, 6> movie = { Red, Green, 0, Blue, 0xFFFFFFFFu, 0 };
        if(vk::RendererComposeOverlay(movie.data(), 2, 2, 4) || !Pixel(100, 100, Blue))
        {
            return false;
        }
        int textureKey = 0;
        const std::uint32_t texel = Green;
        if(!vk::RendererSetTexture(0, &textureKey, &texel, 4, 1, 1, 21))
        {
            return false;
        }
        vk::RendererSetTextureOp(0, 0, 2); // SELECTARG1 (texture)
        int targetKey = 0;
        vk::RendererSetRenderTarget(&targetKey, 64, 64, nullptr, nullptr);
        vk::RendererSetViewport(0, 0, 64, 64);
        vk::RendererClear(1, Red, 1, 0);
        vk::RendererSetRasterState(168, 0); // no guest color writes
        vk::RendererSetRasterState(15, 1);  // alpha test
        vk::RendererSetRasterState(25, 1);  // NEVER
        if(!vk::RendererComposeOverlay(movie.data(), 2, 2, 12))
        {
            return false;
        }
        movie.fill(0); // the queued upload must already own its bytes
        if(!Pixel(0, 0, Red) || !Pixel(Width - 1, 0, Green) ||
           !Pixel(0, Height - 1, Blue) || !Pixel(Width - 1, Height - 1, 0xFFFFFFFFu))
        {
            return false;
        }
        std::array<std::uint32_t, 64 * 64> target = {};
        if(!Draw(8, 8, Blue) || !vk::RendererReadTarget(target.data(), 64 * 4) ||
           target[16 * 64 + 16] != Red)
        {
            return false;
        }
        vk::RendererSetRasterState(168, 15);
        if(!Draw(8, 8, Blue) || !vk::RendererReadTarget(target.data(), 64 * 4) ||
           target[16 * 64 + 16] != Red)
        {
            return false;
        }
        vk::RendererSetRasterState(15, 0);
        if(!Draw(8, 8, Blue) || !vk::RendererReadTarget(target.data(), 64 * 4) ||
           target[16 * 64 + 16] != Green || target[40 * 64 + 40] != Red)
        {
            return false;
        }
        // Repeated uploads and a resolution change must replace the movie,
        // without retaining the prior staging bytes or destroying in-flight work.
        movie.fill(Blue);
        if(!vk::RendererComposeOverlay(movie.data(), 2, 2, 12))
        {
            return false;
        }
        movie.fill(Green);
        if(!vk::RendererComposeOverlay(movie.data(), 2, 2, 12) || !Pixel(100, 100, Green) ||
           !vk::RendererComposeOverlay(&texel, 1, 1, 4) ||
           !vk::PresentFrame(nullptr, 0, 0, 0) || !Pixel(100, 100, Green))
        {
            return false;
        }
        // Ending the overlay does not leave a persistent compositor enabled.
        vk::RendererSetRenderTarget(nullptr, 0, 0, nullptr, nullptr);
        return vk::RendererClear(1, Blue, 1, 0) && Pixel(100, 100, Blue);
    }
    if(std::strcmp(scenario, "turok144") == 0)
    {
        // Temporary bring-up probe: the Turok Evolution frontend draw shape.
        // EmuVshCpuVertex layout (144 bytes) with the draw's diffuse @20,
        // texCoords[0] @28 (vec4), fogValue @140; translated one-combiner
        // shader ("output = t0"); depth on (LEQUAL, write), texture unbound
        // (samples the white dummy).
        struct Vertex144
        {
            float x, y, z, rhw;
            float pointSize;
            std::uint32_t diffuse;
            std::uint32_t specular;
            float texCoords[4][4];
            float clipPosition[4];
            float diffuseComponents[4];
            float specularComponents[4];
            float fogValue;
        };
        std::uint32_t def60[60] = {};
        def60[0] = 0x00000000;
        def60[8] = 0x200C0300;
        def60[9] = 0x00001180;
        def60[34] = 0xC8200000;
        def60[45] = 0x000000C0;
        def60[53] = 0x00011101;
        def60[54] = 0x00000001;
        def60[57] = 0xFFFFFFFF;
        def60[58] = 0xFFFFFFFF;
        vk::RendererSetPixelShader(def60);
        vk::RendererSetDepthState(0, 1);
        vk::RendererSetDepthState(1, 1);
        vk::RendererSetDepthState(2, 4);
        vk::RendererSetRasterState(22, 2);
        vk::RendererClear(7, 0xFF000000, 1.0f, 0);

        auto make = [](float x, float y, float z, std::uint32_t diffuse,
                       float rhw, bool flip)
        {
            Vertex144 vertex{};
            vertex.x = flip ? (x < 40.0f ? x + 64.0f : x - 64.0f) : x;
            vertex.y = y;
            vertex.z = z;
            vertex.rhw = rhw;
            vertex.pointSize = 1.0f;
            vertex.diffuse = diffuse;
            vertex.specular = 0xFF000000u;
            vertex.texCoords[0][0] = 0.25f;
            vertex.texCoords[0][1] = 0.5f;
            vertex.texCoords[0][2] = 0.0f;
            vertex.texCoords[0][3] = 1.0f;
            vertex.texCoords[1][3] = 1.0f;
            vertex.texCoords[2][3] = 1.0f;
            vertex.texCoords[3][3] = 1.0f;
            vertex.clipPosition[3] = rhw != 0.0f ? 1.0f / rhw : 1.0f;
            vertex.fogValue = 1.0f;
            return vertex;
        };
        const std::array<Vertex144, 6> quad = {
            make(8.0f, 8.0f, 0.9998686f, 0xFF795B3Fu, 0.0016238f, false),
            make(72.0f, 8.0f, 0.9998686f, 0xFF795B3Fu, 0.0016238f, false),
            make(8.0f, 72.0f, 0.9998686f, 0xFF795B3Fu, 0.0016238f, false),
            make(72.0f, 8.0f, 0.9998686f, 0xFF795B3Fu, 0.0016238f, false),
            make(72.0f, 72.0f, 0.9998686f, 0xFF795B3Fu, 0.0016238f, false),
            make(8.0f, 72.0f, 0.9998686f, 0xFF795B3Fu, 0.0016238f, false)
        };
        const std::array<Vertex144, 6> quadFlipped = {
            make(8.0f, 8.0f, 0.9998686f, 0xFF00FF00u, 0.0016238f, true),
            make(72.0f, 8.0f, 0.9998686f, 0xFF00FF00u, 0.0016238f, true),
            make(8.0f, 72.0f, 0.9998686f, 0xFF00FF00u, 0.0016238f, true),
            make(72.0f, 8.0f, 0.9998686f, 0xFF00FF00u, 0.0016238f, true),
            make(72.0f, 72.0f, 0.9998686f, 0xFF00FF00u, 0.0016238f, true),
            make(8.0f, 72.0f, 0.9998686f, 0xFF00FF00u, 0.0016238f, true)
        };

        // Run every sub-probe regardless; report each.
        bool allPassed = true;
        auto check = [&](const char* label, bool drawn, unsigned x, unsigned y,
                         std::uint32_t expected)
        {
            std::vector<std::uint32_t> pixels(Width * Height);
            const bool read =
                vk::RendererReadMainTarget(pixels.data(), Width * 4);
            const std::uint32_t actual =
                read ? pixels[y * Width + x] & 0xFFFFFFu : 0u;
            const bool ok = drawn && read && actual == expected;
            std::printf("PROBE %s: %s (got %06X want %06X)\n", label,
                        ok ? "PASS" : "FAIL", actual, expected);
            return ok;
        };

        std::printf("PROBE A1: 144/sets4/fog140, z=0.99987 rhw=0.0016\n");
        vk::RendererClear(7, 0xFF000000, 1.0f, 0);
        allPassed &= check("A1", vk::RendererDrawUP(4, 2, quad.data(), 144,
                                                    20, 28, 4, 140),
                           40, 40, 0x795B3Fu);
        std::printf("PROBE A1F: flipped winding\n");
        vk::RendererClear(7, 0xFF000000, 1.0f, 0);
        allPassed &= check("A1F", vk::RendererDrawUP(4, 2, quadFlipped.data(),
                                                     144, 20, 28, 4, 140),
                           40, 40, 0x00FF00u);
        std::printf("PROBE A2: same, z=0.5 rhw=1\n");
        {
            auto flat = quad;
            for(auto& vertex : flat)
            {
                vertex.z = 0.5f;
                vertex.rhw = 1.0f;
                vertex.clipPosition[3] = 1.0f;
            }
            auto flatFlipped = quadFlipped;
            for(auto& vertex : flatFlipped)
            {
                vertex.z = 0.5f;
                vertex.rhw = 1.0f;
                vertex.clipPosition[3] = 1.0f;
            }
            vk::RendererClear(7, 0xFF000000, 1.0f, 0);
            allPassed &= check("A2", vk::RendererDrawUP(4, 2, flat.data(), 144,
                                                        20, 28, 4, 140),
                               40, 40, 0x795B3Fu);
            vk::RendererClear(7, 0xFF000000, 1.0f, 0);
            allPassed &= check("A2F", vk::RendererDrawUP(
                                          4, 2, flatFlipped.data(), 144, 20,
                                          28, 4, 140),
                               40, 40, 0x00FF00u);
        }
        std::printf("PROBE A3: 144/sets0/fog140\n");
        vk::RendererClear(7, 0xFF000000, 1.0f, 0);
        allPassed &= check("A3", vk::RendererDrawUP(4, 2, quad.data(), 144,
                                                    20, 28, 0, 140),
                           40, 40, 0x795B3Fu);
        vk::RendererClear(7, 0xFF000000, 1.0f, 0);
        allPassed &= check("A3F", vk::RendererDrawUP(4, 2, quadFlipped.data(),
                                                     144, 20, 28, 0, 140),
                           40, 40, 0x00FF00u);
        std::printf("PROBE A4: 144/sets4/nofog\n");
        vk::RendererClear(7, 0xFF000000, 1.0f, 0);
        allPassed &= check("A4", vk::RendererDrawUP(4, 2, quad.data(), 144,
                                                    20, 28, 4,
                                                    0xFFFFFFFFu),
                           40, 40, 0x795B3Fu);
        vk::RendererClear(7, 0xFF000000, 1.0f, 0);
        allPassed &= check("A4F", vk::RendererDrawUP(4, 2, quadFlipped.data(),
                                                     144, 20, 28, 4,
                                                     0xFFFFFFFFu),
                           40, 40, 0x00FF00u);
        std::printf("PROBE A5: depth disabled\n");
        vk::RendererSetDepthState(0, 0);
        vk::RendererClear(7, 0xFF000000, 1.0f, 0);
        const bool drawnA5 =
            vk::RendererDrawUP(4, 2, quad.data(), 144, 20, 28, 4, 140);
        vk::RendererClear(7, 0xFF000000, 1.0f, 0);
        const bool drawnA5F =
            vk::RendererDrawUP(4, 2, quadFlipped.data(), 144, 20, 28, 4, 140);
        vk::RendererSetDepthState(0, 1);
        allPassed &= check("A5", drawnA5, 40, 40, 0x795B3Fu);
        allPassed &= check("A5F", drawnA5F, 40, 40, 0x00FF00u);
        std::printf("allPassed=%d\n", allPassed ? 1 : 0);
        return allPassed;
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
    const bool ready =
        vk::Initialize(window, true, true) && vk::RendererValid();
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
