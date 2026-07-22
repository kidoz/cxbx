#pragma once

#include "core/vertex_shader_translator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace XTL::VshShaderCreation
{
enum class Disposition
{
    CreateOnHost,
    ExecuteOnCpu,
    Reject,
};

struct Request
{
    VshDiagnostics::ShaderWordView xboxFunction;
    VshDiagnostics::ShaderWordView xboxDeclaration;
    bool isXboxFunction = false;
    bool hasDeclaration = false;
    VshDiagnostics::DiagnosticSink diagnosticSink = nullptr;
};

struct Plan
{
    Disposition disposition = Disposition::CreateOnHost;
    std::vector<std::uint32_t> translatedFunction;
    std::array<std::uint32_t, 128> translatedDeclaration{};
    std::size_t declarationTokenCount = 0;
    bool translatedDeclarationAvailable = false;
    std::string reason;
};

[[nodiscard]] Plan BuildPlan(const Request& request);
} // namespace XTL::VshShaderCreation
