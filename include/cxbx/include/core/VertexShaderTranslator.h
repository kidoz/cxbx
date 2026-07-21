// Portable API for NV2A vertex-shader translation, validation, and replay.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace XTL::VshDiagnostics
{
// Receives printf-style translator warnings. Recompilation adapters provide a
// non-null sink for their runtime environment.
using DiagnosticSink = void (*)(const char* format, ...);
using ShaderWordView = std::span<const std::uint32_t>;
using MutableShaderWordView = std::span<std::uint32_t>;

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

struct FunctionTranslationResult
{
    // Empty when the function cannot be translated to host bytecode.
    std::vector<std::uint32_t> tokens;
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
    ShaderWordView xboxFunction;
    ShaderWordView d3dFunction;
    ShaderWordView xboxDeclaration;
    ShaderWordView d3dDeclaration;
    const char* rejectionReason = nullptr;
    const float* constants = nullptr;
    std::size_t constantFloatCount = 0;
    const float* inputs = nullptr;
    std::size_t inputFloatCount = 0;
    const char* inputSource = nullptr;
};

std::uint32_t HashXboxFunction(ShaderWordView xboxFunction);
FunctionTranslationResult TranslateXboxFunction(ShaderWordView xboxFunction,
                                                DiagnosticSink diagnosticSink);
OptimizationResult OptimizeD3D8Function(MutableShaderWordView d3dFunction);
ValidationResult ValidateD3D8Function(ShaderWordView d3dFunction);
ValidationResult ValidateD3D8Translation(ShaderWordView xboxFunction,
                                         ShaderWordView d3dFunction);
XboxFunctionDisposition ClassifyXboxFunction(ShaderWordView xboxFunction,
                                             std::string& reason);
bool RequiresCpuFallback(ShaderWordView xboxFunction, std::string& reason);
DeclarationTranslationResult TranslateXboxDeclaration(ShaderWordView xboxDeclaration,
                                                      MutableShaderWordView d3dDeclaration);
bool ApplyXboxDeclarationConstants(ShaderWordView xboxDeclaration, const float* baseConstants,
                                   float* outputConstants, std::size_t constantFloatCount);
bool ExpandQuadListIndices(const std::uint32_t* sourceIndices, std::size_t sourceIndexCount,
                           std::vector<std::uint32_t>& expandedIndices);
float SelectRasterOutput(const float values[4], std::uint8_t writeMask, float fallback);
float ClampPointSize(float pointSize, float fallback, float maximum);
std::uint32_t PackD3DColor(const float color[4]);
std::uint32_t PackD3DSpecularFog(const float specular[4], const RasterOutputs& rasterOutputs);
bool DecodeXboxVertex(ShaderWordView xboxDeclaration, const VertexStreamView* streams,
                      std::size_t streamCount, std::size_t vertexIndex,
                      float* inputRegisters, std::size_t inputFloatCount);
bool ExecuteXboxVertexShader(ShaderWordView xboxFunction, const float* constants,
                             const float* inputRegisters, float* outputPosition,
                             float* outputColors, std::size_t outputColorFloatCount,
                             float* outputTexCoords, std::size_t outputTexCoordFloatCount,
                             RasterOutputs* outputRaster = nullptr);
std::vector<std::string> DecodeXboxFunction(ShaderWordView xboxFunction);
std::vector<std::string> DecodeD3D8Function(ShaderWordView d3dFunction);
std::vector<std::string> DecodeVertexDeclaration(ShaderWordView declaration);
void DumpRejectedTranslation(FILE* stream, const TranslationCapture& capture);
void DumpReplayCapture(FILE* stream, const TranslationCapture& capture);
} // namespace XTL::VshDiagnostics
