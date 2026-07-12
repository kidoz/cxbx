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
enum class XboxFunctionDisposition
{
    TranslateToHost,
    ExecuteOnCpu,
    Reject,
};

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

struct DeclarationTranslationResult
{
    XboxFunctionDisposition disposition = XboxFunctionDisposition::Reject;
    bool cpuCompatible = true;
    std::size_t tokenCount = 0;
    std::string reason;
    std::string cpuIncompatibilityReason;
};

struct VertexStreamView
{
    const void* data = nullptr;
    std::size_t byteSize = 0;
    std::size_t stride = 0;
};

struct RasterOutputs
{
    float fog[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float pointSize[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    std::uint8_t fogWriteMask = 0;
    std::uint8_t pointSizeWriteMask = 0;
};

struct TranslationCapture
{
    const void* xboxFunction = nullptr;
    const void* d3dFunction = nullptr;
    const void* xboxDeclaration = nullptr;
    const void* d3dDeclaration = nullptr;
    const char* rejectionReason = nullptr;
};

std::uint32_t HashXboxFunction(const void* xboxFunction);
OptimizationResult OptimizeD3D8Function(void* d3dFunction, std::size_t maxTokens);
ValidationResult ValidateD3D8Function(const void* d3dFunction, std::size_t maxTokens);
ValidationResult ValidateD3D8Translation(const void* xboxFunction, const void* d3dFunction);
XboxFunctionDisposition ClassifyXboxFunction(const void* xboxFunction, std::string& reason);
bool RequiresCpuFallback(const void* xboxFunction, std::string& reason);
DeclarationTranslationResult TranslateXboxDeclaration(const void* xboxDeclaration,
                                                      void* d3dDeclaration,
                                                      std::size_t maxTokens);
bool ApplyXboxDeclarationConstants(const void* xboxDeclaration, const float* baseConstants,
                                   float* outputConstants, std::size_t constantFloatCount);
bool ExpandQuadListIndices(const std::uint32_t* sourceIndices, std::size_t sourceIndexCount,
                           std::vector<std::uint32_t>& expandedIndices);
float SelectRasterOutput(const float values[4], std::uint8_t writeMask, float fallback);
float ClampPointSize(float pointSize, float fallback, float maximum);
std::uint32_t PackD3DColor(const float color[4]);
std::uint32_t PackD3DSpecularFog(const float specular[4], const RasterOutputs& rasterOutputs);
bool DecodeXboxVertex(const void* xboxDeclaration, const VertexStreamView* streams,
                      std::size_t streamCount, std::size_t vertexIndex,
                      float* inputRegisters, std::size_t inputFloatCount);
bool ExecuteXboxVertexShader(const void* xboxFunction, const float* constants,
                             const float* inputRegisters, float* outputPosition,
                             float* outputColors, std::size_t outputColorFloatCount,
                             float* outputTexCoords,
                             std::size_t outputTexCoordFloatCount,
                             RasterOutputs* outputRaster = nullptr);
std::vector<std::string> DecodeXboxFunction(const void* xboxFunction);
std::vector<std::string> DecodeD3D8Function(const void* d3dFunction, std::size_t maxTokens);
std::vector<std::string> DecodeVertexDeclaration(const void* declaration,
                                                 std::size_t maxTokens);
void DumpRejectedTranslation(FILE* stream, const TranslationCapture& capture);
} // namespace VshDiagnostics
} // namespace XTL
