#include "vsh_shader_registry.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace XTL::VshShaderRegistry
{
namespace
{
constexpr std::size_t liveCapacity = 256;

std::array<X_D3DVertexShader*, liveCapacity> g_liveShaders{};
std::array<CpuFallbackMetadata, liveCapacity> g_cpuFallbacks{};
CpuFallbackMetadata* g_currentCpuFallback = nullptr;

void LogCpuBinding(CpuFallbackMetadata* metadata, const char* api, bool bound)
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
    std::printf("VSH| cpu_bind hash=%08X api=%s state=%s\n",
                static_cast<unsigned int>(metadata->hash), api,
                bound ? "bound" : "unbound");
    std::fflush(stdout);
    logged = true;
}
} // namespace

CpuFallbackMetadata* Find(X_D3DVertexShader* shader)
{
    for(std::size_t index = 0; index < liveCapacity; ++index)
    {
        if(g_liveShaders[index] == shader)
        {
            return &g_cpuFallbacks[index];
        }
    }
    return nullptr;
}

CpuFallbackMetadata* Current()
{
    return g_currentCpuFallback;
}

void SetCurrent(CpuFallbackMetadata* metadata, const char* api)
{
    if(g_currentCpuFallback == metadata)
    {
        return;
    }
    LogCpuBinding(g_currentCpuFallback, api, false);
    g_currentCpuFallback = metadata;
    LogCpuBinding(g_currentCpuFallback, api, true);
}

bool Register(X_D3DVertexShader* shader, bool cpuFallback,
              VshDiagnostics::ShaderWordView xboxFunction,
              VshDiagnostics::ShaderWordView xboxDeclaration)
{
    for(std::size_t index = 0; index < liveCapacity; ++index)
    {
        if(g_liveShaders[index] != nullptr)
        {
            continue;
        }

        g_liveShaders[index] = shader;
        CpuFallbackMetadata& metadata = g_cpuFallbacks[index];
        metadata = {};
        if(!cpuFallback)
        {
            return true;
        }
        if(xboxFunction.empty() || xboxDeclaration.empty() ||
           xboxDeclaration.size() > metadata.declaration.size())
        {
            g_liveShaders[index] = nullptr;
            metadata = {};
            return false;
        }

        metadata.enabled = true;
        metadata.hash = VshDiagnostics::HashXboxFunction(xboxFunction);
        const std::uint32_t encodedCount = (xboxFunction[0] >> 16) & 0xFFFFu;
        metadata.instructionCount =
            encodedCount == 0 || encodedCount > CpuFallbackMetadata::maxInstructions
                ? (xboxFunction.size() - 1) / 4
                : static_cast<std::size_t>(encodedCount);
        const std::size_t functionWordCount = 1 + metadata.instructionCount * 4;
        if(xboxFunction.size() < functionWordCount)
        {
            metadata.enabled = false;
            g_liveShaders[index] = nullptr;
            metadata = {};
            return false;
        }
        std::copy_n(xboxFunction.begin(), functionWordCount, metadata.function.begin());
        metadata.declarationTokenCount = xboxDeclaration.size();
        std::copy(xboxDeclaration.begin(), xboxDeclaration.end(), metadata.declaration.begin());
        return true;
    }
    return false;
}

bool Unregister(X_D3DVertexShader* shader)
{
    CpuFallbackMetadata* metadata = Find(shader);
    if(metadata == nullptr)
    {
        return false;
    }
    if(g_currentCpuFallback == metadata)
    {
        SetCurrent(nullptr, "DeleteVertexShader");
    }
    const std::size_t index = static_cast<std::size_t>(metadata - g_cpuFallbacks.data());
    g_liveShaders[index] = nullptr;
    *metadata = {};
    return true;
}
} // namespace XTL::VshShaderRegistry
