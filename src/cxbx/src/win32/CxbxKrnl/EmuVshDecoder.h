// Portable diagnostics for the NV2A-to-D3D8 vertex-shader translator.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if defined(CXBX_VSH_HOST_TEST)
using DWORD = std::uint32_t;
#endif

namespace XTL
{
#if defined(CXBX_VSH_HOST_TEST)
DWORD* EmuVshRecompileXboxFunction(const DWORD* xboxFunction);
int EmuVshTranslateXboxDeclaration(const DWORD* xboxDeclaration, DWORD* pcDeclaration, int maxTokens);
#endif

namespace VshDiagnostics
{
struct ValidationResult
{
    bool valid = false;
    std::size_t instructionIndex = 0;
    std::string message;
};

struct OptimizationResult
{
    bool valid = false;
    std::size_t beforeInstructionCount = 0;
    std::size_t afterInstructionCount = 0;
    std::size_t tokenCount = 0;
};

struct TranslationCapture
{
    const void* xboxFunction = nullptr;
    const void* d3dFunction = nullptr;
    const void* xboxDeclaration = nullptr;
    const void* d3dDeclaration = nullptr;
};

std::uint32_t HashXboxFunction(const void* xboxFunction);
OptimizationResult OptimizeD3D8Function(void* d3dFunction, std::size_t maxTokens);
ValidationResult ValidateD3D8Function(const void* d3dFunction, std::size_t maxTokens);
ValidationResult ValidateD3D8Translation(const void* xboxFunction, const void* d3dFunction);
std::vector<std::string> DecodeXboxFunction(const void* xboxFunction);
std::vector<std::string> DecodeD3D8Function(const void* d3dFunction, std::size_t maxTokens);
void DumpRejectedTranslation(FILE* stream, const TranslationCapture& capture);
} // namespace VshDiagnostics
} // namespace XTL
