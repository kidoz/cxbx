#pragma once

#include "core/vertex_shader_translator.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace XTL
{
struct X_D3DVertexShader;

namespace VshShaderRegistry
{
struct CpuFallbackMetadata
{
    static constexpr std::size_t maxInstructions = 136;
    static constexpr std::size_t maxDeclarationTokens = 128;

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
    std::array<std::uint32_t, 1 + maxInstructions * 4> function{};
    std::array<std::uint32_t, maxDeclarationTokens> declaration{};

    // Output texcoord remap: the host vs.1.1 validator requires texture
    // coordinate outputs to start at oT0 and stay contiguous, while an Xbox
    // shader may write, say, only oT2 and oT3. Non-contiguous writes are
    // compacted at creation and this game-stage -> host-stage mapping lets
    // texture binds follow the compaction.
    std::uint32_t texcoordRemapCount = 0;
    std::array<std::uint8_t, 4> texcoordRemapFrom{};
    std::array<std::uint8_t, 4> texcoordRemapTo{};
};

[[nodiscard]] CpuFallbackMetadata* Find(X_D3DVertexShader* shader);
[[nodiscard]] CpuFallbackMetadata* Current();
void SetCurrent(CpuFallbackMetadata* metadata, const char* api);

[[nodiscard]] bool Register(X_D3DVertexShader* shader, bool cpuFallback,
                            VshDiagnostics::ShaderWordView xboxFunction,
                            VshDiagnostics::ShaderWordView xboxDeclaration);
[[nodiscard]] bool Unregister(X_D3DVertexShader* shader);
} // namespace VshShaderRegistry
} // namespace XTL
