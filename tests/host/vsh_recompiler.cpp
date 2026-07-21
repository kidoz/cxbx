#include "core/VertexShaderTranslator.h"
#include "../../src/cxbx/src/win32/CxbxKrnl/EmuVshDecoder.h"
#include "../../src/cxbx/src/win32/CxbxKrnl/EmuVshShaderRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

extern "C" bool EmuVshExecuteProgramRaster(const DWORD* Program, int InstrCount, int Start,
                                             const float* Const, const float* Input,
                                             float* OutPos, float* OutColors,
                                             float* OutTexCoords);

namespace
{
int g_failures = 0;

void Check(bool condition, const char* name)
{
    if(!condition)
    {
        std::fprintf(stderr, "FAIL %s\n", name);
        ++g_failures;
    }
}

void ReportTranslationWarning(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    std::fputc('\n', stderr);
    va_end(arguments);
}

struct ShaderOutputs
{
    std::array<float, 4> position{};
    std::array<float, 2 * 4> colors{};
    std::array<float, 4 * 4> texCoords{};
    std::array<float, 4> fog{};
    std::array<float, 4> pointSize{};
};

bool NearlyEqual(float left, float right)
{
    return std::fabs(left - right) <= 1.0e-5f;
}

bool OutputsEqual(const ShaderOutputs& left, const ShaderOutputs& right)
{
    for(std::size_t component = 0; component < left.position.size(); ++component)
    {
        if(!NearlyEqual(left.position[component], right.position[component]))
        {
            return false;
        }
    }
    for(std::size_t component = 0; component < left.colors.size(); ++component)
    {
        if(!NearlyEqual(left.colors[component], right.colors[component]))
        {
            return false;
        }
    }
    for(std::size_t component = 0; component < left.texCoords.size(); ++component)
    {
        if(!NearlyEqual(left.texCoords[component], right.texCoords[component]))
        {
            return false;
        }
    }
    return true;
}

bool PositionsEqual(const ShaderOutputs& left, const ShaderOutputs& right)
{
    for(std::size_t component = 0; component < left.position.size(); ++component)
    {
        if(!NearlyEqual(left.position[component], right.position[component]))
        {
            return false;
        }
    }
    return true;
}

const float* ResolveMatrixOutput(const ShaderOutputs& outputs, DWORD outputAddress)
{
    if(outputAddress == 3)
    {
        return outputs.colors.data();
    }
    if(outputAddress == 4)
    {
        return outputs.colors.data() + 4;
    }
    if(outputAddress == 5)
    {
        return outputs.fog.data();
    }
    if(outputAddress == 6)
    {
        return outputs.pointSize.data();
    }
    if(outputAddress >= 9 && outputAddress <= 12)
    {
        return outputs.texCoords.data() + (outputAddress - 9) * 4;
    }
    return nullptr;
}

bool MatrixOutputsEqual(const ShaderOutputs& left, const ShaderOutputs& right,
                        DWORD primaryOutputAddress, DWORD primaryOutputMask,
                        DWORD secondaryOutputAddress, DWORD secondaryOutputMask)
{
    if(!PositionsEqual(left, right))
    {
        return false;
    }
    const std::array<std::pair<DWORD, DWORD>, 2> outputs{ {
        { primaryOutputAddress, primaryOutputMask },
        { secondaryOutputAddress, secondaryOutputMask },
    } };
    for(const auto& output : outputs)
    {
        if(output.second == 0)
        {
            continue;
        }
        const float* leftValues = ResolveMatrixOutput(left, output.first);
        const float* rightValues = ResolveMatrixOutput(right, output.first);
        if(leftValues == nullptr || rightValues == nullptr)
        {
            return false;
        }
        for(std::size_t component = 0; component < 4; ++component)
        {
            const DWORD nv2aMaskBit = 1u << (3u - static_cast<DWORD>(component));
            if((output.second & nv2aMaskBit) != 0 &&
               !NearlyEqual(leftValues[component], rightValues[component]))
            {
                return false;
            }
        }
    }
    return true;
}

bool ReadD3D8Source(DWORD token, const std::array<std::array<float, 4>, 12>& temporaries,
                    const float* constants, const float* inputs, int addressRegister,
                    float output[4])
{
    static constexpr float zero[4] = {};
    const DWORD registerType = (token >> 28) & 0x7u;
    const DWORD registerIndex = token & 0x7FFu;
    const float* source = nullptr;
    if(registerType == 0 && registerIndex < temporaries.size())
    {
        source = temporaries[registerIndex].data();
    }
    else if(registerType == 1 && registerIndex < 16)
    {
        source = &inputs[registerIndex * 4];
    }
    else if(registerType == 2)
    {
        const int constantIndex = static_cast<int>(registerIndex) +
                                  (((token & (1u << 13)) != 0) ? addressRegister : 0);
        if(constantIndex >= 0 && constantIndex < 96)
        {
            source = &constants[static_cast<std::size_t>(constantIndex) * 4];
        }
        else
        {
            source = zero;
        }
    }
    if(source == nullptr)
    {
        return false;
    }

    for(std::size_t component = 0; component < 4; ++component)
    {
        const DWORD swizzle = (token >> (16 + component * 2)) & 0x3u;
        output[component] = source[swizzle];
        if((token & (1u << 24)) != 0)
        {
            output[component] = -output[component];
        }
    }
    return true;
}

float* ResolveD3D8Destination(DWORD token, std::array<std::array<float, 4>, 12>& temporaries,
                              ShaderOutputs& outputs)
{
    const DWORD registerType = (token >> 28) & 0x7u;
    const DWORD registerIndex = token & 0x7FFu;
    if(registerType == 0 && registerIndex < temporaries.size())
    {
        return temporaries[registerIndex].data();
    }
    if(registerType == 4 && registerIndex == 0)
    {
        return outputs.position.data();
    }
    if(registerType == 4 && registerIndex == 1)
    {
        return outputs.fog.data();
    }
    if(registerType == 4 && registerIndex == 2)
    {
        return outputs.pointSize.data();
    }
    if(registerType == 5 && registerIndex < 2)
    {
        return &outputs.colors[registerIndex * 4];
    }
    if(registerType == 6 && registerIndex < 4)
    {
        return &outputs.texCoords[registerIndex * 4];
    }
    return nullptr;
}

bool ExecuteD3D8Bytecode(const DWORD* function, std::size_t maxTokens, const float* constants,
                         const float* inputs, ShaderOutputs& outputs)
{
    if(function == nullptr || maxTokens < 2 || function[0] != 0xFFFE0101u)
    {
        return false;
    }

    std::array<std::array<float, 4>, 12> temporaries{};
    int addressRegister = 0;
    std::size_t tokenIndex = 1;
    while(tokenIndex < maxTokens && function[tokenIndex] != 0x0000FFFFu)
    {
        const DWORD opcode = function[tokenIndex++] & 0xFFFFu;
        if(opcode == 0)
        {
            continue;
        }
        std::size_t sourceCount = 0;
        switch(opcode)
        {
            case 1:
            case 6:
            case 7:
            case 14:
            case 15:
            case 16: sourceCount = 1; break;
            case 2:
            case 5:
            case 8:
            case 9:
            case 10:
            case 11:
            case 12:
            case 13:
            case 17: sourceCount = 2; break;
            case 4: sourceCount = 3; break;
            default: return false;
        }
        if(tokenIndex + 1 + sourceCount > maxTokens)
        {
            return false;
        }
        const DWORD destinationToken = function[tokenIndex++];
        float sources[3][4] = {};
        for(std::size_t sourceIndex = 0; sourceIndex < sourceCount; ++sourceIndex)
        {
            if(!ReadD3D8Source(function[tokenIndex++], temporaries, constants, inputs,
                               addressRegister,
                               sources[sourceIndex]))
            {
                return false;
            }
        }

        float result[4] = {};
        if(opcode == 1)
        {
            std::memcpy(result, sources[0], sizeof(result));
        }
        else if(opcode == 5)
        {
            for(std::size_t component = 0; component < 4; ++component)
            {
                result[component] = sources[0][component] * sources[1][component];
            }
        }
        else if(opcode == 2)
        {
            for(std::size_t component = 0; component < 4; ++component)
            {
                result[component] = sources[0][component] + sources[1][component];
            }
        }
        else if(opcode == 4)
        {
            for(std::size_t component = 0; component < 4; ++component)
            {
                result[component] = sources[0][component] * sources[1][component] +
                                    sources[2][component];
            }
        }
        else if(opcode == 6)
        {
            const float reciprocal = sources[0][0] != 0.0f ? 1.0f / sources[0][0] : 0.0f;
            result[0] = result[1] = result[2] = result[3] = reciprocal;
        }
        else if(opcode == 7)
        {
            const float magnitude = std::fabs(sources[0][0]);
            const float reciprocalRoot = magnitude > 0.0f ? 1.0f / std::sqrt(magnitude) : 0.0f;
            result[0] = result[1] = result[2] = result[3] = reciprocalRoot;
        }
        else if(opcode == 8)
        {
            const float dot = sources[0][0] * sources[1][0] + sources[0][1] * sources[1][1] +
                              sources[0][2] * sources[1][2];
            result[0] = result[1] = result[2] = result[3] = dot;
        }
        else if(opcode == 9)
        {
            const float dot = sources[0][0] * sources[1][0] + sources[0][1] * sources[1][1] +
                              sources[0][2] * sources[1][2] + sources[0][3] * sources[1][3];
            result[0] = result[1] = result[2] = result[3] = dot;
        }
        else if(opcode == 10 || opcode == 11 || opcode == 12 || opcode == 13)
        {
            for(std::size_t component = 0; component < 4; ++component)
            {
                if(opcode == 10)
                {
                    result[component] = sources[0][component] < sources[1][component]
                                            ? sources[0][component]
                                            : sources[1][component];
                }
                else if(opcode == 11)
                {
                    result[component] = sources[0][component] > sources[1][component]
                                            ? sources[0][component]
                                            : sources[1][component];
                }
                else if(opcode == 12)
                {
                    result[component] =
                        sources[0][component] < sources[1][component] ? 1.0f : 0.0f;
                }
                else
                {
                    result[component] =
                        sources[0][component] >= sources[1][component] ? 1.0f : 0.0f;
                }
            }
        }
        else if(opcode == 14 || opcode == 15)
        {
            const float scalar = opcode == 14
                                     ? std::exp2(sources[0][0])
                                     : (sources[0][0] > 0.0f ? std::log2(sources[0][0]) : 0.0f);
            result[0] = result[1] = result[2] = result[3] = scalar;
        }
        else if(opcode == 16)
        {
            const float diffuse = sources[0][0];
            const float specular = sources[0][1];
            const float power =
                std::max(-127.9961f, std::min(127.9961f, sources[0][3]));
            result[0] = 1.0f;
            result[1] = diffuse > 0.0f ? diffuse : 0.0f;
            result[2] = diffuse > 0.0f && specular > 0.0f
                            ? std::exp2(power * std::log2(specular))
                            : 0.0f;
            result[3] = 1.0f;
        }
        else if(opcode == 17)
        {
            result[0] = 1.0f;
            result[1] = sources[0][1] * sources[1][1];
            result[2] = sources[0][2];
            result[3] = sources[1][3];
        }
        else
        {
            return false;
        }

        const DWORD destinationType = (destinationToken >> 28) & 0x7u;
        if(destinationType == 3 && (destinationToken & 0x7FFu) == 0)
        {
            addressRegister = static_cast<int>(std::floor(result[0] + 0.5f));
            continue;
        }
        float* destination = ResolveD3D8Destination(destinationToken, temporaries, outputs);
        if(destination == nullptr)
        {
            return false;
        }
        const DWORD writeMask = (destinationToken >> 16) & 0xFu;
        for(std::size_t component = 0; component < 4; ++component)
        {
            if((writeMask & (1u << component)) != 0)
            {
                destination[component] = result[component];
            }
        }
    }
    return tokenIndex < maxTokens && function[tokenIndex] == 0x0000FFFFu;
}

struct NvTestSource
{
    DWORD mux = 0;
    DWORD reg = 0;
    bool negate = false;
    std::array<DWORD, 4> swizzle{ 0, 1, 2, 3 };
};

std::array<DWORD, 4> EncodeNvTestInstruction(DWORD mac, DWORD ilu, DWORD vertex,
                                             const NvTestSource& sourceA,
                                             const NvTestSource& sourceB,
                                             const NvTestSource& sourceC, DWORD macMask,
                                             DWORD outputR, DWORD iluMask, DWORD outputMask,
                                             DWORD outputAddress, DWORD outputMux, bool final,
                                             DWORD constant = 0)
{
    std::array<DWORD, 4> instruction{};
    instruction[1] = (ilu << 25) | (mac << 21) | (constant << 13) | (vertex << 9) |
                     (static_cast<DWORD>(sourceA.negate) << 8) |
                     (sourceA.swizzle[0] << 6) | (sourceA.swizzle[1] << 4) |
                     (sourceA.swizzle[2] << 2) | sourceA.swizzle[3];
    instruction[2] = (sourceA.reg << 28) | (sourceA.mux << 26) |
                     (static_cast<DWORD>(sourceB.negate) << 25) |
                     (sourceB.swizzle[0] << 23) | (sourceB.swizzle[1] << 21) |
                     (sourceB.swizzle[2] << 19) | (sourceB.swizzle[3] << 17) |
                     (sourceB.reg << 13) | (sourceB.mux << 11) |
                     (static_cast<DWORD>(sourceC.negate) << 10) |
                     (sourceC.swizzle[0] << 8) | (sourceC.swizzle[1] << 6) |
                     (sourceC.swizzle[2] << 4) | (sourceC.swizzle[3] << 2) |
                     ((sourceC.reg >> 2) & 0x3u);
    instruction[3] = ((sourceC.reg & 0x3u) << 30) | (sourceC.mux << 28) |
                     (macMask << 24) | (outputR << 20) | (iluMask << 16) |
                     (outputMask << 12) | (1u << 11) | (outputAddress << 3) |
                     (outputMux << 2) | static_cast<DWORD>(final);
    return instruction;
}

void AppendNvTestInstruction(std::vector<DWORD>& program,
                             const std::array<DWORD, 4>& instruction)
{
    program.insert(program.end(), instruction.begin(), instruction.end());
}

std::vector<DWORD> BuildMacDifferentialProgram(DWORD mac, const NvTestSource& sourceA,
                                               DWORD outputMask = 0xFu)
{
    const NvTestSource unused{};
    const NvTestSource vertexSource{ 2, 0, false, { 0, 1, 2, 3 } };
    const NvTestSource r1{ 1, 1, false, { 0, 1, 2, 3 } };
    const NvTestSource r2{ 1, 2, false, { 0, 1, 2, 3 } };
    std::vector<DWORD> program{ 0x00042078u };
    AppendNvTestInstruction(program, EncodeNvTestInstruction(
                                         1, 0, 1, vertexSource, unused, unused, 0xF, 1, 0, 0,
                                         9, 0, false));
    AppendNvTestInstruction(program, EncodeNvTestInstruction(
                                         1, 0, 2, vertexSource, unused, unused, 0xF, 2, 0, 0,
                                         9, 0, false));
    AppendNvTestInstruction(program, EncodeNvTestInstruction(
                                         mac, 0, 0, sourceA, r1, r2, 0, 0, 0, outputMask, 9, 0,
                                         false));
    AppendNvTestInstruction(program, EncodeNvTestInstruction(
                                         1, 0, 0, vertexSource, unused, unused, 0, 0, 0, 0xF,
                                         0, 0, true));
    return program;
}

std::vector<DWORD> BuildIluDifferentialProgram(DWORD ilu, const NvTestSource& sourceC,
                                               DWORD outputMask = 0xFu)
{
    const NvTestSource unused{};
    const NvTestSource vertexSource{ 2, 0, false, { 0, 1, 2, 3 } };
    std::vector<DWORD> program{ 0x00022078u };
    AppendNvTestInstruction(program, EncodeNvTestInstruction(
                                         0, ilu, 0, unused, unused, sourceC, 0, 0, 0,
                                         outputMask, 9, 1, false));
    AppendNvTestInstruction(program, EncodeNvTestInstruction(
                                         1, 0, 0, vertexSource, unused, unused, 0, 0, 0, 0xF,
                                         0, 0, true));
    return program;
}

bool DifferentialTexCoordMatches(const std::vector<DWORD>& program, const float* hardwareConstants,
                                 const float* hostConstants, const float* inputs)
{
    ShaderOutputs cpuOutputs{};
    XTL::VshDiagnostics::RasterOutputs rasterOutputs{};
    if(!XTL::VshDiagnostics::ExecuteXboxVertexShader(
           program, hardwareConstants, inputs, cpuOutputs.position.data(),
           cpuOutputs.colors.data(), cpuOutputs.colors.size(), cpuOutputs.texCoords.data(),
           cpuOutputs.texCoords.size(), &rasterOutputs))
    {
        return false;
    }

    DWORD* translated = XTL::EmuVshRecompileXboxFunction(program.data());
    if(translated == nullptr)
    {
        return false;
    }
    const std::size_t maxD3dTokens = 16 + program.size() * 24;
    const XTL::VshDiagnostics::ValidationResult validation =
        XTL::VshDiagnostics::ValidateD3D8Translation(
            program, { translated, maxD3dTokens });
    ShaderOutputs hostOutputs{};
    const bool executed = ExecuteD3D8Bytecode(translated, maxD3dTokens,
                                              hostConstants, inputs, hostOutputs);
    delete[] translated;
    if(!validation.valid || !executed)
    {
        return false;
    }
    for(std::size_t component = 0; component < 4; ++component)
    {
        if(!NearlyEqual(cpuOutputs.texCoords[component], hostOutputs.texCoords[component]))
        {
            return false;
        }
    }
    return true;
}

struct DifferentialSourcePattern
{
    NvTestSource sourceA;
    NvTestSource sourceB;
    NvTestSource sourceC;
};

std::vector<DWORD> BuildPipelineMatrixProgram(DWORD mac, DWORD ilu,
                                              const DifferentialSourcePattern& sources,
                                              DWORD outputMask, DWORD outputAddress,
                                              DWORD outputMux, bool targetIsFinal)
{
    const NvTestSource unused{};
    const NvTestSource vertex1{ 2, 0, false, { 0, 1, 2, 3 } };
    const NvTestSource vertex2{ 2, 0, false, { 0, 1, 2, 3 } };
    const NvTestSource vertex3{ 2, 0, false, { 0, 1, 2, 3 } };
    std::vector<DWORD> program{ 0x00042078u };
    AppendNvTestInstruction(program, EncodeNvTestInstruction(
                                         1, 0, 1, vertex1, unused, unused, 0xFu, 1, 0, 0,
                                         9, 0, false));
    AppendNvTestInstruction(program, EncodeNvTestInstruction(
                                         1, 0, 2, vertex2, unused, unused, 0xFu, 2, 0, 0,
                                         9, 0, false));
    const std::array<DWORD, 4> target = EncodeNvTestInstruction(
        mac, ilu, 0, sources.sourceA, sources.sourceB, sources.sourceC, 0, 0, 0,
        outputMask, outputAddress, outputMux, targetIsFinal, 96);
    const std::array<DWORD, 4> position = EncodeNvTestInstruction(
        1, 0, 3, vertex3, unused, unused, 0, 0, 0, 0xFu, 0, 0, !targetIsFinal);
    AppendNvTestInstruction(program, targetIsFinal ? position : target);
    AppendNvTestInstruction(program, targetIsFinal ? target : position);
    return program;
}

std::vector<DWORD> BuildPairedHazardMatrixProgram(bool iluReadsMacDestination,
                                                  DWORD destinationRegister,
                                                  const NvTestSource& reader,
                                                  DWORD temporaryMask, DWORD outputMask)
{
    const NvTestSource unused{};
    const NvTestSource vertex0{ 2, 0, false, { 0, 1, 2, 3 } };
    const NvTestSource vertex1{ 2, 0, false, { 0, 1, 2, 3 } };
    const NvTestSource vertex2{ 2, 0, false, { 0, 1, 2, 3 } };
    const NvTestSource destinationSource{ 1, destinationRegister, reader.negate,
                                          reader.swizzle };
    std::vector<DWORD> program{ 0x00042078u };
    AppendNvTestInstruction(program, EncodeNvTestInstruction(
                                         1, 0, 1, vertex1, unused, unused, 0xFu,
                                         destinationRegister, 0, 0, 9, 0, false));
    if(iluReadsMacDestination)
    {
        AppendNvTestInstruction(program, EncodeNvTestInstruction(
                                             1, 1, 0, vertex0, unused, destinationSource,
                                             temporaryMask, destinationRegister, 0, outputMask,
                                             9, 1, false));
    }
    else
    {
        AppendNvTestInstruction(program, EncodeNvTestInstruction(
                                             1, 1, 0, destinationSource, unused, vertex0, 0,
                                             destinationRegister, temporaryMask, outputMask, 9,
                                             0, false));
    }
    AppendNvTestInstruction(program, EncodeNvTestInstruction(
                                         1, 0, 0, destinationSource, unused, unused, 0, 0, 0,
                                         0xFu, 10, 0, false));
    AppendNvTestInstruction(program, EncodeNvTestInstruction(
                                         1, 0, 2, vertex2, unused, unused, 0, 0, 0, 0xFu,
                                         0, 0, true));
    return program;
}

void PrintShaderOutputs(const char* label, const ShaderOutputs& outputs)
{
    std::fprintf(stderr, "VSHMATRIX| %s position", label);
    for(const float value : outputs.position)
    {
        std::fprintf(stderr, " %.9g", static_cast<double>(value));
    }
    std::fprintf(stderr, " colors");
    for(const float value : outputs.colors)
    {
        std::fprintf(stderr, " %.9g", static_cast<double>(value));
    }
    std::fprintf(stderr, " texcoords");
    for(const float value : outputs.texCoords)
    {
        std::fprintf(stderr, " %.9g", static_cast<double>(value));
    }
    std::fprintf(stderr, " fog");
    for(const float value : outputs.fog)
    {
        std::fprintf(stderr, " %.9g", static_cast<double>(value));
    }
    std::fprintf(stderr, " point_size");
    for(const float value : outputs.pointSize)
    {
        std::fprintf(stderr, " %.9g", static_cast<double>(value));
    }
    std::fputc('\n', stderr);
}

void ReportDifferentialMatrixFailure(std::size_t caseId, const char* stage,
                                     const std::vector<DWORD>& program,
                                     const DWORD* translated, std::size_t maxD3dTokens,
                                     const ShaderOutputs& cpuOutputs,
                                     const ShaderOutputs& hostOutputs)
{
    const std::uint32_t hash = XTL::VshDiagnostics::HashXboxFunction(program);
    std::fprintf(stderr, "VSHMATRIX| mismatch case=%zu hash=%08X stage=%s words=%zu\n",
                 caseId, hash, stage, program.size());
    for(std::size_t word = 0; word < program.size(); ++word)
    {
        std::fprintf(stderr, "VSHMATRIX| raw case=%zu word=%zu value=%08X\n", caseId,
                     word, static_cast<unsigned int>(program[word]));
    }
    for(const std::string& line : XTL::VshDiagnostics::DecodeXboxFunction(program))
    {
        std::fprintf(stderr, "VSHMATRIX| nv2a case=%zu %s\n", caseId, line.c_str());
    }
    for(const std::string& line :
        XTL::VshDiagnostics::DecodeD3D8Function({ translated, maxD3dTokens }))
    {
        std::fprintf(stderr, "VSHMATRIX| d3d8 case=%zu %s\n", caseId, line.c_str());
    }
    PrintShaderOutputs("cpu", cpuOutputs);
    PrintShaderOutputs("d3d8", hostOutputs);
}

bool RunDifferentialMatrixCase(std::size_t caseId, bool reportFailure,
                               const std::vector<DWORD>& program,
                               const float* hardwareConstants, const float* hostConstants,
                               const float* inputs, DWORD primaryOutputAddress,
                               DWORD primaryOutputMask, DWORD secondaryOutputAddress = 0,
                               DWORD secondaryOutputMask = 0)
{
    ShaderOutputs cpuOutputs{};
    XTL::VshDiagnostics::RasterOutputs rasterOutputs{};
    if(!XTL::VshDiagnostics::ExecuteXboxVertexShader(
           program, hardwareConstants, inputs, cpuOutputs.position.data(),
           cpuOutputs.colors.data(), cpuOutputs.colors.size(), cpuOutputs.texCoords.data(),
           cpuOutputs.texCoords.size(), &rasterOutputs))
    {
        if(reportFailure)
        {
            ReportDifferentialMatrixFailure(caseId, "cpu_execution", program, nullptr, 0,
                                            cpuOutputs, {});
        }
        return false;
    }

    std::unique_ptr<DWORD[]> translated{ XTL::EmuVshRecompileXboxFunction(program.data()) };
    const std::size_t maxD3dTokens = 16 + program.size() * 24;
    if(translated == nullptr)
    {
        if(reportFailure)
        {
            ReportDifferentialMatrixFailure(caseId, "translation", program, nullptr, 0,
                                            cpuOutputs, {});
        }
        return false;
    }
    const XTL::VshDiagnostics::ValidationResult validation =
        XTL::VshDiagnostics::ValidateD3D8Translation(
            program, { translated.get(), maxD3dTokens });
    if(!validation.valid)
    {
        if(reportFailure)
        {
            ReportDifferentialMatrixFailure(caseId, validation.message.c_str(), program,
                                            translated.get(), maxD3dTokens, cpuOutputs, {});
        }
        return false;
    }

    ShaderOutputs hostOutputs{};
    if(!ExecuteD3D8Bytecode(translated.get(), maxD3dTokens, hostConstants, inputs,
                            hostOutputs))
    {
        if(reportFailure)
        {
            ReportDifferentialMatrixFailure(caseId, "d3d8_execution", program,
                                            translated.get(), maxD3dTokens, cpuOutputs,
                                            hostOutputs);
        }
        return false;
    }
    if(!MatrixOutputsEqual(cpuOutputs, hostOutputs, primaryOutputAddress, primaryOutputMask,
                           secondaryOutputAddress, secondaryOutputMask))
    {
        if(reportFailure)
        {
            ReportDifferentialMatrixFailure(caseId, "output", program, translated.get(),
                                            maxD3dTokens, cpuOutputs, hostOutputs);
        }
        return false;
    }
    return true;
}

using SequenceOutputMasks = std::array<DWORD, 13>;

enum class DifferentialFailureStage
{
    None,
    CpuExecution,
    Translation,
    Validation,
    D3D8Execution,
    Output,
};

const char* DifferentialFailureStageName(DifferentialFailureStage stage)
{
    switch(stage)
    {
        case DifferentialFailureStage::None: return "none";
        case DifferentialFailureStage::CpuExecution: return "cpu_execution";
        case DifferentialFailureStage::Translation: return "translation";
        case DifferentialFailureStage::Validation: return "validation";
        case DifferentialFailureStage::D3D8Execution: return "d3d8_execution";
        case DifferentialFailureStage::Output: return "output";
    }
    return "unknown";
}

SequenceOutputMasks CollectSequenceOutputMasks(const std::vector<DWORD>& program)
{
    SequenceOutputMasks masks{};
    if(program.empty())
    {
        return masks;
    }
    const std::size_t encodedCount = (program[0] >> 16) & 0xFFFFu;
    const std::size_t availableCount = (program.size() - 1) / 4;
    const std::size_t instructionCount = std::min(encodedCount, availableCount);
    for(std::size_t instruction = 0; instruction < instructionCount; ++instruction)
    {
        const DWORD outputToken = program[1 + instruction * 4 + 3];
        const DWORD outputMask = (outputToken >> 12) & 0xFu;
        const DWORD outputAddress = (outputToken >> 3) & 0xFFu;
        const bool outputRegisterBank = (outputToken & (1u << 11)) != 0;
        if(outputRegisterBank && outputMask != 0 && outputAddress < masks.size() &&
           outputAddress != 0)
        {
            masks[outputAddress] |= outputMask;
        }
        if((outputToken & 1u) != 0)
        {
            break;
        }
    }
    return masks;
}

bool SequenceOutputsEqual(const ShaderOutputs& left, const ShaderOutputs& right,
                          const SequenceOutputMasks& masks)
{
    if(!PositionsEqual(left, right))
    {
        return false;
    }
    for(DWORD outputAddress = 0; outputAddress < masks.size(); ++outputAddress)
    {
        const DWORD outputMask = masks[outputAddress];
        if(outputMask == 0)
        {
            continue;
        }
        const float* leftValues = ResolveMatrixOutput(left, outputAddress);
        const float* rightValues = ResolveMatrixOutput(right, outputAddress);
        if(leftValues == nullptr || rightValues == nullptr)
        {
            return false;
        }
        for(DWORD component = 0; component < 4; ++component)
        {
            const DWORD nv2aMaskBit = 1u << (3u - component);
            if((outputMask & nv2aMaskBit) != 0 &&
               !NearlyEqual(leftValues[component], rightValues[component]))
            {
                return false;
            }
        }
    }
    return true;
}

DifferentialFailureStage EvaluateSequenceProgram(std::size_t caseId, bool reportFailure,
                                                 const std::vector<DWORD>& program,
                                                 const float* hardwareConstants,
                                                 const float* hostConstants,
                                                 const float* inputs)
{
    ShaderOutputs cpuOutputs{};
    XTL::VshDiagnostics::RasterOutputs rasterOutputs{};
    if(!XTL::VshDiagnostics::ExecuteXboxVertexShader(
           program, hardwareConstants, inputs, cpuOutputs.position.data(),
           cpuOutputs.colors.data(), cpuOutputs.colors.size(), cpuOutputs.texCoords.data(),
           cpuOutputs.texCoords.size(), &rasterOutputs))
    {
        if(reportFailure)
        {
            ReportDifferentialMatrixFailure(caseId, "cpu_execution", program, nullptr, 0,
                                            cpuOutputs, {});
        }
        return DifferentialFailureStage::CpuExecution;
    }
    std::copy(std::begin(rasterOutputs.fog), std::end(rasterOutputs.fog),
              cpuOutputs.fog.begin());
    std::copy(std::begin(rasterOutputs.pointSize), std::end(rasterOutputs.pointSize),
              cpuOutputs.pointSize.begin());

    std::unique_ptr<DWORD[]> translated{ XTL::EmuVshRecompileXboxFunction(program.data()) };
    const std::size_t maxD3dTokens = 16 + program.size() * 24;
    if(translated == nullptr)
    {
        if(reportFailure)
        {
            ReportDifferentialMatrixFailure(caseId, "translation", program, nullptr, 0,
                                            cpuOutputs, {});
        }
        return DifferentialFailureStage::Translation;
    }
    const XTL::VshDiagnostics::ValidationResult validation =
        XTL::VshDiagnostics::ValidateD3D8Translation(
            program, { translated.get(), maxD3dTokens });
    if(!validation.valid)
    {
        if(reportFailure)
        {
            ReportDifferentialMatrixFailure(caseId, validation.message.c_str(), program,
                                            translated.get(), maxD3dTokens, cpuOutputs, {});
        }
        return DifferentialFailureStage::Validation;
    }

    ShaderOutputs hostOutputs{};
    if(!ExecuteD3D8Bytecode(translated.get(), maxD3dTokens, hostConstants, inputs,
                            hostOutputs))
    {
        if(reportFailure)
        {
            ReportDifferentialMatrixFailure(caseId, "d3d8_execution", program,
                                            translated.get(), maxD3dTokens, cpuOutputs,
                                            hostOutputs);
        }
        return DifferentialFailureStage::D3D8Execution;
    }
    const SequenceOutputMasks outputMasks = CollectSequenceOutputMasks(program);
    if(!SequenceOutputsEqual(cpuOutputs, hostOutputs, outputMasks))
    {
        if(reportFailure)
        {
            ReportDifferentialMatrixFailure(caseId, "output", program, translated.get(),
                                            maxD3dTokens, cpuOutputs, hostOutputs);
        }
        return DifferentialFailureStage::Output;
    }
    return DifferentialFailureStage::None;
}

std::vector<DWORD> ReduceFailingSequence(const std::vector<DWORD>& original,
                                         DifferentialFailureStage failureStage,
                                         const float* hardwareConstants,
                                         const float* hostConstants, const float* inputs)
{
    std::vector<DWORD> reduced = original;
    bool changed = true;
    while(changed)
    {
        changed = false;
        const std::size_t instructionCount = (reduced.size() - 1) / 4;
        for(std::size_t instruction = 0; instruction + 1 < instructionCount; ++instruction)
        {
            std::vector<DWORD> candidate = reduced;
            const auto first = candidate.begin() + 1 + static_cast<std::ptrdiff_t>(instruction * 4);
            candidate.erase(first, first + 4);
            const std::size_t candidateInstructionCount = (candidate.size() - 1) / 4;
            candidate[0] = (static_cast<DWORD>(candidateInstructionCount) << 16) | 0x2078u;
            if(EvaluateSequenceProgram(0, false, candidate, hardwareConstants, hostConstants,
                                       inputs) == failureStage)
            {
                reduced = std::move(candidate);
                changed = true;
                break;
            }
        }
    }
    return reduced;
}

struct ReplayCaptureRecord
{
    std::uint32_t hash = 0;
    std::string reason;
    std::string inputSource;
    std::vector<DWORD> function;
    std::vector<DWORD> declaration;
    std::array<float, 192 * 4> constants{};
    std::array<float, 16 * 4> inputs{};
};

bool ParseUnsigned(const std::string& text, unsigned int base, std::uint64_t& value)
{
    if(text.empty() || (base != 10 && base != 16))
    {
        return false;
    }
    value = 0;
    for(const unsigned char character : text)
    {
        unsigned int digit = 0;
        if(character >= '0' && character <= '9')
        {
            digit = character - '0';
        }
        else if(base == 16 && character >= 'A' && character <= 'F')
        {
            digit = 10u + character - 'A';
        }
        else if(base == 16 && character >= 'a' && character <= 'f')
        {
            digit = 10u + character - 'a';
        }
        else
        {
            return false;
        }
        if(digit >= base || value > (std::numeric_limits<std::uint64_t>::max() - digit) / base)
        {
            return false;
        }
        value = value * base + digit;
    }
    return true;
}

bool FindReplayField(const std::string& line, const char* name, std::string& value)
{
    const std::string marker = std::string(" ") + name + '=';
    const std::size_t markerPosition = line.find(marker);
    if(markerPosition == std::string::npos)
    {
        return false;
    }
    if(line.find(marker, markerPosition + marker.size()) != std::string::npos)
    {
        return false;
    }
    const std::size_t valuePosition = markerPosition + marker.size();
    const std::size_t valueEnd = line.find(' ', valuePosition);
    value = line.substr(valuePosition, valueEnd - valuePosition);
    return !value.empty();
}

bool ParseReplayWords(const std::string& text, std::vector<DWORD>& words,
                      std::size_t maximumWordCount)
{
    words.clear();
    if(text == "-")
    {
        return true;
    }
    std::size_t position = 0;
    while(position < text.size())
    {
        const std::size_t end = text.find(',', position);
        const std::string word = text.substr(position, end - position);
        std::uint64_t value = 0;
        if(word.size() != 8 || !ParseUnsigned(word, 16, value) || value > 0xFFFFFFFFu ||
           words.size() >= maximumWordCount)
        {
            return false;
        }
        words.push_back(static_cast<DWORD>(value));
        if(end == std::string::npos)
        {
            break;
        }
        if(end + 1 >= text.size())
        {
            return false;
        }
        position = end + 1;
    }
    return !words.empty();
}

template <std::size_t Size>
bool ParseReplaySparseFloats(const std::string& text, std::array<float, Size>& values)
{
    values.fill(0.0f);
    if(text == "-")
    {
        return true;
    }
    std::array<bool, Size> assigned{};
    std::size_t position = 0;
    while(position < text.size())
    {
        const std::size_t end = text.find(',', position);
        const std::string entry = text.substr(position, end - position);
        const std::size_t separator = entry.find(':');
        std::uint64_t index = 0;
        std::uint64_t bits = 0;
        if(separator == std::string::npos || entry.size() - separator - 1 != 8 ||
           !ParseUnsigned(entry.substr(0, separator), 10, index) ||
           !ParseUnsigned(entry.substr(separator + 1), 16, bits) || index >= Size ||
           bits > 0xFFFFFFFFu || assigned[static_cast<std::size_t>(index)])
        {
            return false;
        }
        const DWORD word = static_cast<DWORD>(bits);
        std::memcpy(&values[static_cast<std::size_t>(index)], &word, sizeof(word));
        assigned[static_cast<std::size_t>(index)] = true;
        if(end == std::string::npos)
        {
            break;
        }
        if(end + 1 >= text.size())
        {
            return false;
        }
        position = end + 1;
    }
    return true;
}

bool ReplayDeclarationIsFramed(const std::vector<DWORD>& declaration)
{
    if(declaration.empty())
    {
        return true;
    }
    for(std::size_t tokenIndex = 0; tokenIndex < declaration.size();)
    {
        const DWORD token = declaration[tokenIndex++];
        if(token == 0xFFFFFFFFu)
        {
            return tokenIndex == declaration.size();
        }
        const DWORD tokenType = token >> 29;
        if(tokenType >= 6)
        {
            return false;
        }
        std::size_t payloadCount = 0;
        if(tokenType == 4)
        {
            payloadCount = ((token >> 25) & 0xFu) * 4;
        }
        else if(tokenType == 5)
        {
            payloadCount = (token >> 24) & 0x1Fu;
        }
        if(payloadCount > declaration.size() - tokenIndex)
        {
            return false;
        }
        tokenIndex += payloadCount;
    }
    return false;
}

bool ParseReplayCaptureLine(const std::string& line, ReplayCaptureRecord& record,
                            std::string& error)
{
    std::string replayLine = line;
    if(!replayLine.empty() && replayLine.back() == '\r')
    {
        replayLine.pop_back();
    }
    if(replayLine.rfind("VSHREPLAY| ", 0) != 0)
    {
        error = "missing VSHREPLAY prefix";
        return false;
    }
    std::string version;
    std::string hash;
    std::string function;
    std::string declaration;
    std::string constants;
    std::string inputs;
    if(!FindReplayField(replayLine, "version", version) || version != "1" ||
       !FindReplayField(replayLine, "hash", hash) ||
       !FindReplayField(replayLine, "reason", record.reason) ||
       !FindReplayField(replayLine, "input_source", record.inputSource) ||
       !FindReplayField(replayLine, "function", function) ||
       !FindReplayField(replayLine, "declaration", declaration) ||
       !FindReplayField(replayLine, "constants", constants) ||
       !FindReplayField(replayLine, "inputs", inputs))
    {
        error = "missing or unsupported replay field";
        return false;
    }
    std::uint64_t parsedHash = 0;
    if(hash.size() != 8 || !ParseUnsigned(hash, 16, parsedHash) ||
       !ParseReplayWords(function, record.function, 1 + 136 * 4) ||
       !ParseReplayWords(declaration, record.declaration, 128) ||
       !ParseReplaySparseFloats(constants, record.constants) ||
       !ParseReplaySparseFloats(inputs, record.inputs))
    {
        error = "invalid replay field encoding";
        return false;
    }
    record.hash = static_cast<std::uint32_t>(parsedHash);
    if(record.function.size() < 5 || (record.function.size() - 1) % 4 != 0 ||
       (record.function[0] & 0xFFFFu) != 0x2078u ||
       (record.function.back() & 1u) == 0 || !ReplayDeclarationIsFramed(record.declaration) ||
       XTL::VshDiagnostics::HashXboxFunction(record.function) != record.hash)
    {
        error = "invalid function or hash";
        return false;
    }
    for(std::size_t instruction = 0; instruction + 1 < (record.function.size() - 1) / 4;
        ++instruction)
    {
        if((record.function[1 + instruction * 4 + 3] & 1u) != 0)
        {
            error = "function contains data after FINAL";
            return false;
        }
    }
    return true;
}

int ReplayCapture(const ReplayCaptureRecord& record)
{
    if(record.reason == "collapsed_geometry")
    {
        const std::vector<std::string> listing =
            XTL::VshDiagnostics::DecodeXboxFunction(record.function);
        for(const std::string& instruction : listing)
        {
            std::printf("VSHREPLAY| decoded hash=%08X %s\n", record.hash,
                        instruction.c_str());
        }
    }
    std::array<std::uint32_t, 128> translatedDeclaration{};
    XTL::VshDiagnostics::DeclarationTranslationResult declarationResult{};
    std::array<float, 192 * 4> hardwareConstants = record.constants;
    if(!record.declaration.empty())
    {
        declarationResult = XTL::VshDiagnostics::TranslateXboxDeclaration(
            record.declaration, translatedDeclaration);
        if(!XTL::VshDiagnostics::ApplyXboxDeclarationConstants(
               record.declaration, record.constants.data(), hardwareConstants.data(),
               hardwareConstants.size()))
        {
            std::fprintf(stderr,
                         "VSHREPLAY| replay hash=%08X status=fail stage=declaration_constants\n",
                         record.hash);
            return 1;
        }
    }

    std::array<float, 96 * 4> hostConstants{};
    std::copy(hardwareConstants.begin() + 96 * 4, hardwareConstants.end(),
              hostConstants.begin());
    std::string dispositionReason;
    const XTL::VshDiagnostics::XboxFunctionDisposition disposition =
        XTL::VshDiagnostics::ClassifyXboxFunction(record.function, dispositionReason);
    if(disposition != XTL::VshDiagnostics::XboxFunctionDisposition::TranslateToHost)
    {
        ShaderOutputs cpuOutputs{};
        const bool cpuExecuted = XTL::VshDiagnostics::ExecuteXboxVertexShader(
            record.function, hardwareConstants.data(), record.inputs.data(),
            cpuOutputs.position.data(), cpuOutputs.colors.data(), cpuOutputs.colors.size(),
            cpuOutputs.texCoords.data(), cpuOutputs.texCoords.size());
        const char* dispositionName =
            disposition == XTL::VshDiagnostics::XboxFunctionDisposition::ExecuteOnCpu
                ? "cpu"
                : "reject";
        std::printf("VSHREPLAY| replay hash=%08X status=classified disposition=%s "
                    "cpu=%s reason=%s declaration=%d position=%.9g,%.9g,%.9g,%.9g\n",
                    record.hash, dispositionName, cpuExecuted ? "pass" : "unavailable",
                    dispositionReason.c_str(), static_cast<int>(declarationResult.disposition),
                    cpuOutputs.position[0], cpuOutputs.position[1], cpuOutputs.position[2],
                    cpuOutputs.position[3]);
        return disposition == XTL::VshDiagnostics::XboxFunctionDisposition::ExecuteOnCpu &&
                       !cpuExecuted
                   ? 1
                   : 0;
    }

    const DifferentialFailureStage failureStage = EvaluateSequenceProgram(
        record.hash, false, record.function, hardwareConstants.data(), hostConstants.data(),
        record.inputs.data());
    if(failureStage == DifferentialFailureStage::None)
    {
        std::printf("VSHREPLAY| replay hash=%08X status=pass disposition=host declaration=%d\n",
                    record.hash, static_cast<int>(declarationResult.disposition));
        return 0;
    }

    const std::vector<DWORD> reduced = ReduceFailingSequence(
        record.function, failureStage, hardwareConstants.data(), hostConstants.data(),
        record.inputs.data());
    std::fprintf(stderr,
                 "VSHREPLAY| replay hash=%08X status=fail stage=%s "
                 "original_instructions=%zu reduced_instructions=%zu\n",
                 record.hash, DifferentialFailureStageName(failureStage),
                 (record.function.size() - 1) / 4, (reduced.size() - 1) / 4);
    EvaluateSequenceProgram(record.hash, true, reduced, hardwareConstants.data(),
                            hostConstants.data(), record.inputs.data());
    const XTL::VshDiagnostics::TranslationCapture reducedCapture = {
        reduced,
        {},
        record.declaration,
        {},
        DifferentialFailureStageName(failureStage),
        hardwareConstants.data(),
        hardwareConstants.size(),
        record.inputs.data(),
        record.inputs.size(),
        record.inputSource.c_str(),
    };
    XTL::VshDiagnostics::DumpReplayCapture(stderr, reducedCapture);
    return 1;
}

int ReplayCaptureFile(const char* path)
{
    std::ifstream input(path);
    if(!input)
    {
        std::fprintf(stderr, "FAIL cannot open replay capture: %s\n", path);
        return 1;
    }
    std::vector<std::uint32_t> replayedHashes;
    std::string line;
    std::size_t replayCount = 0;
    int failures = 0;
    while(std::getline(input, line))
    {
        if(line.rfind("VSHREPLAY| ", 0) != 0)
        {
            continue;
        }
        ReplayCaptureRecord record;
        std::string error;
        if(!ParseReplayCaptureLine(line, record, error))
        {
            std::fprintf(stderr, "FAIL replay parse line=%zu reason=%s\n", replayCount + 1,
                         error.c_str());
            ++failures;
            continue;
        }
        if(std::find(replayedHashes.begin(), replayedHashes.end(), record.hash) !=
           replayedHashes.end())
        {
            continue;
        }
        replayedHashes.push_back(record.hash);
        ++replayCount;
        failures += ReplayCapture(record);
    }
    if(replayCount == 0)
    {
        std::fputs("FAIL replay capture contains no VSHREPLAY records\n", stderr);
        return 1;
    }
    std::printf("VSHREPLAY| summary records=%zu failures=%d\n", replayCount, failures);
    return failures == 0 ? 0 : 1;
}

class DeterministicSequenceRng
{
  public:
    explicit DeterministicSequenceRng(std::uint32_t seed) : m_state(seed == 0 ? 1u : seed)
    {
    }

    std::uint32_t Next()
    {
        m_state ^= m_state << 13;
        m_state ^= m_state >> 17;
        m_state ^= m_state << 5;
        return m_state;
    }

  private:
    std::uint32_t m_state;
};

NvTestSource GenerateSequenceSource(DeterministicSequenceRng& rng,
                                    DWORD forcedTemporary = 0xFFFFFFFFu)
{
    static constexpr std::array<DWORD, 10> temporaryRegisters{
        0u,
        1u,
        2u,
        3u,
        4u,
        5u,
        6u,
        7u,
        11u,
        12u,
    };
    static constexpr std::array<std::array<DWORD, 4>, 4> swizzles{ {
        { 0u, 1u, 2u, 3u },
        { 3u, 2u, 1u, 0u },
        { 0u, 0u, 0u, 0u },
        { 1u, 0u, 3u, 2u },
    } };
    NvTestSource source{};
    const DWORD bank = forcedTemporary == 0xFFFFFFFFu ? rng.Next() % 3u : 0u;
    if(forcedTemporary != 0xFFFFFFFFu)
    {
        source.mux = 1;
        source.reg = forcedTemporary;
    }
    else if(bank == 0)
    {
        source.mux = 1;
        source.reg = temporaryRegisters[rng.Next() % temporaryRegisters.size()];
    }
    else if(bank == 1)
    {
        source.mux = 2;
    }
    else
    {
        source.mux = 3;
    }
    source.negate = (rng.Next() & 3u) == 0;
    source.swizzle = swizzles[rng.Next() % swizzles.size()];
    return source;
}

struct GeneratedSequenceProgram
{
    std::vector<DWORD> program;
    std::uint32_t seed = 0;
    std::size_t bodyInstructionCount = 0;
    bool hasDph = false;
    bool hasPairedInstruction = false;
    bool hasPositionFeedback = false;
    bool hasRepeatedDestination = false;
};

GeneratedSequenceProgram GenerateSequenceProgram(std::size_t caseId)
{
    static constexpr std::array<DWORD, 12> macOpcodes{
        1u,
        2u,
        3u,
        4u,
        5u,
        6u,
        7u,
        8u,
        9u,
        10u,
        11u,
        12u,
    };
    static constexpr std::array<DWORD, 6> iluOpcodes{ 1u, 2u, 4u, 5u, 6u, 7u };
    static constexpr std::array<DWORD, 4> masks{ 0x1u, 0x5u, 0xAu, 0xFu };
    static constexpr std::array<DWORD, 7> outputAddresses{ 0u, 3u, 4u, 9u, 10u, 11u, 12u };
    static constexpr std::array<DWORD, 10> temporaryRegisters{
        0u,
        1u,
        2u,
        3u,
        4u,
        5u,
        6u,
        7u,
        11u,
        12u,
    };
    const NvTestSource unused{};
    const std::uint32_t seed =
        0xC0B8A2D1u ^ (static_cast<std::uint32_t>(caseId) * 0x85EBCA6Bu);
    DeterministicSequenceRng rng(seed);
    GeneratedSequenceProgram generated;
    generated.seed = seed;
    generated.bodyInstructionCount = 2 + rng.Next() % 7u;
    if(caseId == 0)
    {
        generated.bodyInstructionCount = 3;
    }
    generated.program.push_back(0x2078u);

    const std::array<DWORD, 4> seedRegisters{ 0u, 1u, 11u, 12u };
    for(DWORD seedIndex = 0; seedIndex < seedRegisters.size(); ++seedIndex)
    {
        const NvTestSource vertex{ 2, 0, false, { 0, 1, 2, 3 } };
        AppendNvTestInstruction(generated.program, EncodeNvTestInstruction(
                                                       1, 0, seedIndex, vertex, unused,
                                                       unused, 0xFu,
                                                       seedRegisters[seedIndex], 0, 0, 9, 0,
                                                       false));
    }

    DWORD previousDestination = temporaryRegisters[rng.Next() % temporaryRegisters.size()];
    for(std::size_t bodyIndex = 0; bodyIndex < generated.bodyInstructionCount; ++bodyIndex)
    {
        DWORD mac = macOpcodes[rng.Next() % macOpcodes.size()];
        if(caseId % 5 == 0 && bodyIndex == 0)
        {
            mac = 6;
        }
        bool paired = (caseId + bodyIndex) % 3 == 0;
        DWORD ilu = paired ? iluOpcodes[rng.Next() % iluOpcodes.size()] : 0;
        const DWORD vertex = rng.Next() % 4u;
        const DWORD constant = 96u + rng.Next() % 4u;
        DWORD destination = temporaryRegisters[rng.Next() % temporaryRegisters.size()];
        if(caseId % 7 == 0 && bodyIndex == 1)
        {
            destination = previousDestination;
        }
        generated.hasRepeatedDestination =
            generated.hasRepeatedDestination || destination == previousDestination;
        previousDestination = destination;

        NvTestSource sourceA = GenerateSequenceSource(rng);
        NvTestSource sourceB = GenerateSequenceSource(rng);
        NvTestSource sourceC = GenerateSequenceSource(rng);
        DWORD macMask = masks[rng.Next() % masks.size()];
        DWORD iluMask = paired ? masks[rng.Next() % masks.size()] : 0;
        DWORD outputMask =
            (bodyIndex + 1 == generated.bodyInstructionCount || bodyIndex % 2 == 1)
                ? masks[rng.Next() % masks.size()]
                : 0;
        DWORD outputAddress = outputAddresses[rng.Next() % outputAddresses.size()];
        DWORD outputMux = paired ? rng.Next() & 1u : 0;

        if(caseId == 0)
        {
            paired = false;
            ilu = 0;
            if(bodyIndex == 0)
            {
                mac = 3;
                sourceA = NvTestSource{ 1, 12, false, { 0, 1, 2, 3 } };
                sourceC = NvTestSource{ 1, 0, false, { 0, 1, 2, 3 } };
                destination = 0;
                macMask = 0xAu;
                iluMask = 0;
                outputMask = 0xFu;
                outputAddress = 0;
                outputMux = 0;
                generated.hasPositionFeedback = true;
            }
            else if(bodyIndex == 1)
            {
                mac = 1;
                sourceA = NvTestSource{ 1, 12, false, { 0, 1, 2, 3 } };
                macMask = 0;
                iluMask = 0;
                outputMask = 0xFu;
                outputAddress = 9;
                outputMux = 0;
            }
            else
            {
                mac = 1;
                sourceA = NvTestSource{ 1, 0, false, { 0, 1, 2, 3 } };
                macMask = 0;
                iluMask = 0;
                outputMask = 0xFu;
                outputAddress = 10;
                outputMux = 0;
            }
        }
        else if(caseId % 4 == 0 && bodyIndex == 0)
        {
            mac = 1;
            sourceA = NvTestSource{ 2, 0, false, { 0, 1, 2, 3 } };
            macMask = 0;
            iluMask = 0;
            outputMask = 0xFu;
            outputAddress = 0;
            outputMux = 0;
            generated.hasPositionFeedback = true;
        }
        else if(caseId % 4 == 0 && bodyIndex == 1)
        {
            mac = 1;
            sourceA = GenerateSequenceSource(rng, 12);
            outputMask = 0xFu;
            outputAddress = 9;
            outputMux = 0;
        }
        else if(paired)
        {
            if((caseId + bodyIndex) % 2 == 0)
            {
                sourceC = GenerateSequenceSource(rng, destination);
            }
            else
            {
                sourceA = GenerateSequenceSource(rng, destination);
                macMask = 0;
            }
        }
        generated.hasDph = generated.hasDph || mac == 6;
        generated.hasPairedInstruction = generated.hasPairedInstruction || paired;

        AppendNvTestInstruction(generated.program, EncodeNvTestInstruction(
                                                       mac, ilu, vertex, sourceA, sourceB,
                                                       sourceC, macMask, destination, iluMask,
                                                       outputMask, outputAddress, outputMux,
                                                       false, constant));
    }
    const NvTestSource finalPosition{ 2, 0, false, { 0, 1, 2, 3 } };
    AppendNvTestInstruction(generated.program, EncodeNvTestInstruction(
                                                   1, 0, 3, finalPosition, unused, unused, 0,
                                                   0, 0, 0xFu, 0, 0, true));
    const std::size_t instructionCount = (generated.program.size() - 1) / 4;
    generated.program[0] = (static_cast<DWORD>(instructionCount) << 16) | 0x2078u;
    return generated;
}

constexpr DWORD kXboxProgram[] = {
    0x00052078u,
    0x00000000u,
    0x00E0001Bu,
    0x0836186Cu,
    0x00008800u,
    0x00000000u,
    0x00E0201Bu,
    0x0836186Cu,
    0x00004800u,
    0x00000000u,
    0x00E0401Bu,
    0x0836186Cu,
    0x00002800u,
    0x00000000u,
    0x00E0601Bu,
    0x0836186Cu,
    0x00001800u,
    0x00000000u,
    0x0020061Bu,
    0x0836006Cu,
    0x0000F819u,
};

constexpr DWORD kXboxDeclaration[] = {
    0x10000000u,
    0x40020000u,
    0xFFFFFFFFu,
};

constexpr DWORD kDifferentialPositionProgram[] = {
    0x00052078u,
    0x00000000u,
    0x00EC001Bu,
    0x0836186Cu,
    0x00008800u,
    0x00000000u,
    0x00EC201Bu,
    0x0836186Cu,
    0x00004800u,
    0x00000000u,
    0x00EC401Bu,
    0x0836186Cu,
    0x00002800u,
    0x00000000u,
    0x00CC601Bu,
    0x0836186Cu,
    0x00001800u,
    0x00000000u,
    0x0020061Bu,
    0x0836006Cu,
    0x0000F819u,
};

constexpr DWORD kDifferentialOutputsProgram[] = {
    0x00072078u,
    0x00000000u,
    0x0020001Bu,
    0x08000000u,
    0x0000F800u,
    0x00000000u,
    0x0020021Bu,
    0x08000000u,
    0x0000F818u,
    0x00000000u,
    0x0020041Bu,
    0x08000000u,
    0x0000F820u,
    0x00000000u,
    0x0020061Bu,
    0x08000000u,
    0x0000F848u,
    0x00000000u,
    0x0020081Bu,
    0x08000000u,
    0x0000F850u,
    0x00000000u,
    0x00200A1Bu,
    0x08000000u,
    0x0000F858u,
    0x00000000u,
    0x00200C1Bu,
    0x08000000u,
    0x0000F861u,
};

constexpr DWORD kScreenSpaceEpilogueProgram[] = {
    0x00022078u,
    0x00000000u,
    0x0020001Bu,
    0x08000000u,
    0x0000F800u,
    0x00000000u,
    0x0040021Bu,
    0xC4361000u,
    0x0000F801u,
};

constexpr DWORD kMidProgramPositionFeedbackProgram[] = {
    0x00042078u,
    0x00000000u,
    0x0020001Bu,
    0x08000000u,
    0x0000F800u,
    0x00000000u,
    0x0040021Bu,
    0xC4361000u,
    0x0000F800u,
    0x00000000u,
    0x0020001Bu,
    0xC4000000u,
    0x0000F818u,
    0x00000000u,
    0x0020001Bu,
    0x08000000u,
    0x0000F801u,
};

constexpr DWORD kAmbiguousScreenSpaceSuffixProgram[] = {
    0x00022078u,
    0x00000000u,
    0x0020001Bu,
    0x08000000u,
    0x0000F800u,
    0x00000000u,
    0x004C001Bu,
    0xC4361800u,
    0x0000F801u,
};

constexpr DWORD kPartialPositionProgram[] = {
    0x00012078u,
    0x00000000u,
    0x0020021Bu,
    0x08000000u,
    0x0000C801u,
};

constexpr DWORD kRasterOutputsProgram[] = {
    0x00032078u,
    0x00000000u,
    0x0020001Bu,
    0x08000000u,
    0x0000F800u,
    0x00000000u,
    0x0020021Bu,
    0x08000000u,
    0x00008828u,
    0x00000000u,
    0x0020041Bu,
    0x08000000u,
    0x00008831u,
};

constexpr DWORD kRccProgram[] = {
    0x00012078u,
    0x00000000u,
    0x06000000u,
    0x00000000u,
    0x2000F805u,
};

constexpr DWORD kR11R12Program[] = {
    0x00032078u,
    0x00000000u,
    0x0020021Bu,
    0x08000000u,
    0x0FB00000u,
    0x00000000u,
    0x0020001Bu,
    0x08000000u,
    0x0FC0F800u,
    0x00000000u,
    0x0020001Bu,
    0xB4000000u,
    0x0000F819u,
};

constexpr DWORD kPairedReadBeforeWriteProgram[] = {
    0x00032078u,
    0x00000000u,
    0x0020001Bu,
    0x08000000u,
    0x0F000000u,
    0x00000000u,
    0x0220021Bu,
    0x0800006Cu,
    0x1F0FF81Cu,
    0x00000000u,
    0x0020021Bu,
    0x08000000u,
    0x0000F801u,
};

constexpr DWORD kRelativeConstantProgram[] = {
    0x00032078u,
    0x00000000u,
    0x01A0001Bu,
    0x08000000u,
    0x00000000u,
    0x00000000u,
    0x002C201Bu,
    0x0C000000u,
    0x0000F81Au,
    0x00000000u,
    0x0020021Bu,
    0x08000000u,
    0x0000F801u,
};

constexpr DWORD kHostRelativeProgram[] = {
    0xFFFE0101u,
    0x00000001u,
    0xB0010000u,
    0x90000000u,
    0x00000001u,
    0xD00F0000u,
    0xA0E42001u,
    0x00000001u,
    0xC00F0000u,
    0x90E40001u,
    0x0000FFFFu,
};

void TestShaderRegistry()
{
    std::uint32_t shaderStorage = 0;
    auto* shader = reinterpret_cast<XTL::X_D3DVertexShader*>(&shaderStorage);
    Check(XTL::VshShaderRegistry::Current() == nullptr,
          "shader registry starts without a CPU fallback");
    Check(XTL::VshShaderRegistry::Register(shader, false, {}, {}),
          "shader registry accepts a host-translated shader");
    XTL::VshShaderRegistry::CpuFallbackMetadata* metadata =
        XTL::VshShaderRegistry::Find(shader);
    Check(metadata != nullptr && !metadata->enabled,
          "shader registry tracks a host-translated shader without CPU metadata");
    XTL::VshShaderRegistry::SetCurrent(metadata, "host-test");
    Check(XTL::VshShaderRegistry::Current() == metadata,
          "shader registry owns the current fallback binding");
    Check(XTL::VshShaderRegistry::Unregister(shader),
          "shader registry removes a live shader");
    Check(XTL::VshShaderRegistry::Current() == nullptr,
          "shader removal clears the current fallback binding");
    Check(!XTL::VshShaderRegistry::Unregister(shader),
          "shader registry rejects a stale shader");

    const std::array<std::uint32_t, 5> function{ 0x00012078u, 1u, 2u, 3u, 4u };
    const std::array<std::uint32_t, 1> declaration{ 0xFFFFFFFFu };
    Check(XTL::VshShaderRegistry::Register(shader, true, function, declaration),
          "shader registry accepts owned CPU fallback data");
    metadata = XTL::VshShaderRegistry::Find(shader);
    Check(metadata != nullptr && metadata->enabled && metadata->instructionCount == 1 &&
              metadata->declarationTokenCount == declaration.size() &&
              metadata->hash == XTL::VshDiagnostics::HashXboxFunction(function) &&
              std::equal(function.begin(), function.end(), metadata->function.begin()) &&
              metadata->declaration.front() == declaration.front(),
          "shader registry owns complete CPU fallback metadata");
    Check(XTL::VshShaderRegistry::Unregister(shader),
          "shader registry releases owned CPU fallback data");
    Check(XTL::VshShaderRegistry::Find(shader) == nullptr,
          "shader registry no longer resolves released CPU fallback data");
}
} // namespace

int RunTests()
{
    TestShaderRegistry();
    const XTL::VshDiagnostics::FunctionTranslationResult translation =
        XTL::VshDiagnostics::TranslateXboxFunction(kXboxProgram, ReportTranslationWarning);
    Check(!translation.tokens.empty(), "owned recompiler returns bytecode");
    if(translation.tokens.empty())
    {
        return g_failures;
    }
    const std::uint32_t hash = XTL::VshDiagnostics::HashXboxFunction(kXboxProgram);
    Check(hash == 0x79E4E0BEu, "stable Xbox-function hash");

    const XTL::VshDiagnostics::ValidationResult validation =
        XTL::VshDiagnostics::ValidateD3D8Translation(kXboxProgram, translation.tokens);
    Check(validation.valid, "generated bytecode validates");
    Check(validation.message == "ok", "validator success reason");
    std::string dispositionReason;
    Check(XTL::VshDiagnostics::ClassifyXboxFunction(kXboxProgram, dispositionReason) ==
              XTL::VshDiagnostics::XboxFunctionDisposition::TranslateToHost,
          "supported shader is classified for host translation");
    Check(dispositionReason.empty(), "host translation disposition has no failure reason");

    const std::array<DWORD, 1> truncatedXboxFunction{ 0x00012078u };
    Check(XTL::VshDiagnostics::ClassifyXboxFunction(truncatedXboxFunction,
                                                    dispositionReason) ==
              XTL::VshDiagnostics::XboxFunctionDisposition::Reject,
          "bounded Xbox function rejects missing instruction words");
    Check(dispositionReason == "truncated_xbox_function",
          "bounded Xbox function reports truncation");

    std::array<float, 192 * 4> opcodeHardwareConstants{};
    std::array<float, 96 * 4> opcodeHostConstants{};
    std::array<float, 16 * 4> opcodeInputs{};
    const std::array<float, 4> input0{ 0.5f, 0.25f, 2.0f, 2.0f };
    const std::array<float, 4> input1{ 2.0f, 3.0f, -1.0f, 0.5f };
    const std::array<float, 4> input2{ 1.0f, -2.0f, 0.25f, 3.0f };
    std::copy(input0.begin(), input0.end(), opcodeInputs.begin());
    std::copy(input1.begin(), input1.end(), opcodeInputs.begin() + 4);
    std::copy(input2.begin(), input2.end(), opcodeInputs.begin() + 8);

    struct OpcodeCase
    {
        DWORD opcode;
        const char* name;
    };
    const std::array<OpcodeCase, 12> macCases{ {
        { 1, "MOV" },
        { 2, "MUL" },
        { 3, "ADD" },
        { 4, "MAD" },
        { 5, "DP3" },
        { 6, "DPH" },
        { 7, "DP4" },
        { 8, "DST" },
        { 9, "MIN" },
        { 10, "MAX" },
        { 11, "SLT" },
        { 12, "SGE" },
    } };
    const NvTestSource vertex0{ 2, 0, false, { 0, 1, 2, 3 } };
    for(const OpcodeCase& opcodeCase : macCases)
    {
        const std::vector<DWORD> program =
            BuildMacDifferentialProgram(opcodeCase.opcode, vertex0);
        const std::string name =
            std::string("CPU and translated ") + opcodeCase.name + " semantics match";
        Check(DifferentialTexCoordMatches(program, opcodeHardwareConstants.data(),
                                          opcodeHostConstants.data(), opcodeInputs.data()),
              name.c_str());
    }

    const NvTestSource swizzledNegatedVertex0{ 2, 0, true, { 3, 2, 1, 0 } };
    const std::vector<DWORD> swizzleMaskProgram =
        BuildMacDifferentialProgram(1, swizzledNegatedVertex0, 0xAu);
    Check(DifferentialTexCoordMatches(swizzleMaskProgram, opcodeHardwareConstants.data(),
                                      opcodeHostConstants.data(), opcodeInputs.data()),
          "CPU and translator preserve source swizzle, negation, and partial output mask");

    const std::array<OpcodeCase, 6> iluCases{ {
        { 1, "ILU MOV" },
        { 2, "RCP" },
        { 4, "RSQ" },
        { 5, "EXP" },
        { 6, "LOG" },
        { 7, "LIT" },
    } };
    for(const OpcodeCase& opcodeCase : iluCases)
    {
        const std::vector<DWORD> program =
            BuildIluDifferentialProgram(opcodeCase.opcode, vertex0);
        const std::string name =
            std::string("CPU and translated ") + opcodeCase.name + " semantics match";
        Check(DifferentialTexCoordMatches(program, opcodeHardwareConstants.data(),
                                          opcodeHostConstants.data(), opcodeInputs.data()),
              name.c_str());
    }

    const NvTestSource temporary1{ 1, 1, false, { 0, 1, 2, 3 } };
    const NvTestSource temporary1ReverseNegated{ 1, 1, true, { 3, 2, 1, 0 } };
    const NvTestSource temporary2Swizzled{ 1, 2, false, { 1, 0, 3, 2 } };
    const NvTestSource constant96{ 3, 0, false, { 0, 1, 2, 3 } };
    const NvTestSource constant96Swizzled{ 3, 0, false, { 2, 3, 0, 1 } };
    const NvTestSource vertex0Negated{ 2, 0, true, { 0, 1, 2, 3 } };
    const NvTestSource vertex0ReverseNegated{ 2, 0, true, { 3, 2, 1, 0 } };
    const std::array<DifferentialSourcePattern, 3> matrixSourcePatterns{ {
        { vertex0, temporary1, temporary2Swizzled },
        { temporary1ReverseNegated, constant96, vertex0ReverseNegated },
        { constant96Swizzled, vertex0Negated, temporary1 },
    } };
    const std::array<DWORD, 4> matrixMasks{ 0x1u, 0x5u, 0xAu, 0xFu };
    const std::array<DWORD, 6> matrixOutputs{ 3u, 4u, 9u, 10u, 11u, 12u };
    const std::array<DWORD, 3> hazardDestinations{ 1u, 2u, 11u };
    const std::array<NvTestSource, 2> hazardReaders{ {
        { 1, 0, false, { 0, 1, 2, 3 } },
        { 1, 0, true, { 3, 2, 1, 0 } },
    } };
    const std::array<float, 4> matrixConstant{ 1.25f, 0.75f, 3.0f, 0.5f };
    std::copy(matrixConstant.begin(), matrixConstant.end(),
              opcodeHardwareConstants.begin() + 96 * 4);
    std::copy(matrixConstant.begin(), matrixConstant.end(), opcodeHostConstants.begin());

    std::size_t matrixCaseId = 0;
    std::size_t matrixFailures = 0;
    for(const OpcodeCase& opcodeCase : macCases)
    {
        for(const DifferentialSourcePattern& sources : matrixSourcePatterns)
        {
            for(const DWORD mask : matrixMasks)
            {
                for(const DWORD output : matrixOutputs)
                {
                    const bool targetIsFinal = (matrixCaseId & 1u) != 0;
                    const std::vector<DWORD> program = BuildPipelineMatrixProgram(
                        opcodeCase.opcode, 0, sources, mask, output, 0, targetIsFinal);
                    if(!RunDifferentialMatrixCase(
                           matrixCaseId, matrixFailures == 0, program,
                           opcodeHardwareConstants.data(),
                           opcodeHostConstants.data(), opcodeInputs.data(), output, mask))
                    {
                        ++matrixFailures;
                    }
                    ++matrixCaseId;
                }
            }
        }
    }
    for(const OpcodeCase& opcodeCase : iluCases)
    {
        for(const DifferentialSourcePattern& sources : matrixSourcePatterns)
        {
            for(const DWORD mask : matrixMasks)
            {
                for(const DWORD output : matrixOutputs)
                {
                    const bool targetIsFinal = (matrixCaseId & 1u) != 0;
                    const std::vector<DWORD> program = BuildPipelineMatrixProgram(
                        0, opcodeCase.opcode, sources, mask, output, 1, targetIsFinal);
                    if(!RunDifferentialMatrixCase(
                           matrixCaseId, matrixFailures == 0, program,
                           opcodeHardwareConstants.data(),
                           opcodeHostConstants.data(), opcodeInputs.data(), output, mask))
                    {
                        ++matrixFailures;
                    }
                    ++matrixCaseId;
                }
            }
        }
    }
    for(const bool iluReadsMacDestination : { false, true })
    {
        for(const DWORD destination : hazardDestinations)
        {
            for(const NvTestSource& reader : hazardReaders)
            {
                for(const DWORD temporaryMask : matrixMasks)
                {
                    const DWORD outputMask =
                        matrixMasks[(matrixCaseId + 1) % matrixMasks.size()];
                    const std::vector<DWORD> program = BuildPairedHazardMatrixProgram(
                        iluReadsMacDestination, destination, reader, temporaryMask,
                        outputMask);
                    if(!RunDifferentialMatrixCase(
                           matrixCaseId, matrixFailures == 0, program,
                           opcodeHardwareConstants.data(),
                           opcodeHostConstants.data(), opcodeInputs.data(), 9, outputMask, 10,
                           0xFu))
                    {
                        ++matrixFailures;
                    }
                    ++matrixCaseId;
                }
            }
        }
    }
    Check(matrixCaseId == 1344, "deterministic shader matrix case count is stable");
    if(matrixFailures != 0)
    {
        std::fprintf(stderr, "VSHMATRIX| summary cases=%zu failures=%zu\n", matrixCaseId,
                     matrixFailures);
    }
    Check(matrixFailures == 0, "deterministic shader matrix preserves CPU semantics");

    const std::array<std::array<float, 4>, 4> sequenceConstants{ {
        { 1.25f, 0.75f, 3.0f, 0.5f },
        { 2.0f, 0.5f, 1.5f, 4.0f },
        { 0.25f, 2.5f, 0.75f, 1.0f },
        { 3.0f, 1.25f, 0.5f, 2.0f },
    } };
    for(std::size_t constant = 0; constant < sequenceConstants.size(); ++constant)
    {
        std::copy(sequenceConstants[constant].begin(), sequenceConstants[constant].end(),
                  opcodeHardwareConstants.data() + (96 + constant) * 4);
        std::copy(sequenceConstants[constant].begin(), sequenceConstants[constant].end(),
                  opcodeHostConstants.data() + constant * 4);
    }
    const std::array<float, 4> input3{ 0.75f, 1.5f, 0.5f, 2.5f };
    std::copy(input3.begin(), input3.end(), opcodeInputs.begin() + 12);

    constexpr std::size_t sequenceCaseCount = 256;
    std::size_t sequenceFailures = 0;
    std::size_t dphSequences = 0;
    std::size_t pairedSequences = 0;
    std::size_t feedbackSequences = 0;
    std::size_t repeatedDestinationSequences = 0;
    std::size_t maximumBodyLength = 0;
    for(std::size_t sequenceCase = 0; sequenceCase < sequenceCaseCount; ++sequenceCase)
    {
        const GeneratedSequenceProgram generated = GenerateSequenceProgram(sequenceCase);
        dphSequences += static_cast<std::size_t>(generated.hasDph);
        pairedSequences += static_cast<std::size_t>(generated.hasPairedInstruction);
        feedbackSequences += static_cast<std::size_t>(generated.hasPositionFeedback);
        repeatedDestinationSequences +=
            static_cast<std::size_t>(generated.hasRepeatedDestination);
        maximumBodyLength = std::max(maximumBodyLength, generated.bodyInstructionCount);

        const DifferentialFailureStage failureStage = EvaluateSequenceProgram(
            sequenceCase, false, generated.program, opcodeHardwareConstants.data(),
            opcodeHostConstants.data(), opcodeInputs.data());
        if(failureStage == DifferentialFailureStage::None)
        {
            continue;
        }

        const std::vector<DWORD> reduced = ReduceFailingSequence(
            generated.program, failureStage, opcodeHardwareConstants.data(),
            opcodeHostConstants.data(), opcodeInputs.data());
        std::fprintf(stderr,
                     "VSHSEQUENCE| mismatch case=%zu seed=%08X stage=%s "
                     "original_instructions=%zu reduced_instructions=%zu\n",
                     sequenceCase, generated.seed, DifferentialFailureStageName(failureStage),
                     (generated.program.size() - 1) / 4, (reduced.size() - 1) / 4);
        EvaluateSequenceProgram(sequenceCase, true, reduced, opcodeHardwareConstants.data(),
                                opcodeHostConstants.data(), opcodeInputs.data());
        ++sequenceFailures;
        break;
    }
    Check(dphSequences != 0, "sequence generator covers DPH scratch allocation");
    Check(pairedSequences != 0, "sequence generator covers paired MAC and ILU issue");
    Check(feedbackSequences != 0, "sequence generator covers r12 position feedback");
    Check(repeatedDestinationSequences != 0,
          "sequence generator covers repeated temporary destinations");
    Check(maximumBodyLength == 8, "sequence generator reaches its bounded body length");
    Check(sequenceFailures == 0,
          "deterministic multi-instruction sequences preserve CPU semantics");

    const auto checkUnsupportedProgram = [&](const std::vector<DWORD>& program,
                                             const char* expectedReason, const char* name)
    {
        std::string reason;
        Check(XTL::VshDiagnostics::ClassifyXboxFunction(program, reason) ==
                  XTL::VshDiagnostics::XboxFunctionDisposition::Reject,
              name);
        Check(reason == expectedReason, name);
        Check(!XTL::VshDiagnostics::RequiresCpuFallback(program, reason), name);
        DWORD* rejectedTranslation = XTL::EmuVshRecompileXboxFunction(program.data());
        Check(rejectedTranslation == nullptr, name);
        delete[] rejectedTranslation;
        ShaderOutputs rejectedOutputs{};
        Check(!XTL::VshDiagnostics::ExecuteXboxVertexShader(
                  program, opcodeHardwareConstants.data(), opcodeInputs.data(),
                  rejectedOutputs.position.data(), rejectedOutputs.colors.data(),
                  rejectedOutputs.colors.size(), rejectedOutputs.texCoords.data(),
                  rejectedOutputs.texCoords.size()),
              name);
    };

    std::vector<DWORD> reservedMacProgram = BuildMacDifferentialProgram(1, vertex0);
    reservedMacProgram[10] = (reservedMacProgram[10] & ~(0xFu << 21)) | (14u << 21);
    checkUnsupportedProgram(reservedMacProgram, "unsupported_mac_opcode",
                            "reserved MAC opcode fails closed");
    const DifferentialFailureStage reducerFailureStage = EvaluateSequenceProgram(
        0, false, reservedMacProgram, opcodeHardwareConstants.data(),
        opcodeHostConstants.data(), opcodeInputs.data());
    const std::vector<DWORD> reducedInvalidProgram = ReduceFailingSequence(
        reservedMacProgram, reducerFailureStage, opcodeHardwareConstants.data(),
        opcodeHostConstants.data(), opcodeInputs.data());
    Check(reducerFailureStage == DifferentialFailureStage::CpuExecution,
          "sequence reducer fixture has a stable failure stage");
    Check(reducedInvalidProgram.size() < reservedMacProgram.size(),
          "sequence reducer removes irrelevant instructions");
    Check(EvaluateSequenceProgram(0, false, reducedInvalidProgram,
                                  opcodeHardwareConstants.data(), opcodeHostConstants.data(),
                                  opcodeInputs.data()) == reducerFailureStage,
          "sequence reducer preserves the original failure stage");

    std::vector<DWORD> invalidSourceProgram = BuildMacDifferentialProgram(1, vertex0);
    invalidSourceProgram[11] &= ~(0x3u << 26);
    checkUnsupportedProgram(invalidSourceProgram, "unsupported_source_mux",
                            "active source with reserved mux fails closed");

    std::vector<DWORD> invalidTempProgram = BuildMacDifferentialProgram(1, vertex0);
    invalidTempProgram[4] = (invalidTempProgram[4] & ~(0xFu << 20)) | (13u << 20);
    checkUnsupportedProgram(invalidTempProgram, "unsupported_temp_register",
                            "reserved temporary destination fails closed");

    std::vector<DWORD> invalidOutputProgram = BuildMacDifferentialProgram(1, vertex0);
    invalidOutputProgram[12] &= ~(1u << 11);
    checkUnsupportedProgram(invalidOutputProgram, "unsupported_output_route",
                            "constant-bank output route fails closed");

    std::vector<DWORD> noScratchDph(1 + 12 * 4, 0);
    noScratchDph[0] = 0x000C2078u;
    for(DWORD reg = 0; reg < 11; ++reg)
    {
        noScratchDph[1 + reg * 4 + 1] = 0x0020001Bu;
        noScratchDph[1 + reg * 4 + 2] = 0x08000000u;
        noScratchDph[1 + reg * 4 + 3] = 0x0F000000u | (reg << 20);
    }
    noScratchDph[1 + 11 * 4 + 1] = 0x00CC601Bu;
    noScratchDph[1 + 11 * 4 + 2] = 0x0836186Cu;
    noScratchDph[1 + 11 * 4 + 3] = 0x0000F801u;
    std::string fallbackReason;
    Check(XTL::VshDiagnostics::RequiresCpuFallback(noScratchDph, fallbackReason),
          "DPH without a free temporary requires CPU fallback");
    Check(XTL::VshDiagnostics::ClassifyXboxFunction(noScratchDph, fallbackReason) ==
              XTL::VshDiagnostics::XboxFunctionDisposition::ExecuteOnCpu,
          "DPH without scratch is classified for CPU execution");
    Check(fallbackReason == "dph_no_scratch", "DPH CPU fallback reason is stable");

    std::vector<DWORD> noPositionScratch(1 + 13 * 4, 0);
    noPositionScratch[0] = 0x000D2078u;
    for(DWORD reg = 0; reg < 12; ++reg)
    {
        noPositionScratch[1 + reg * 4 + 1] = 0x0020001Bu;
        noPositionScratch[1 + reg * 4 + 2] = 0x08000000u;
        noPositionScratch[1 + reg * 4 + 3] = 0x0F000000u | (reg << 20);
    }
    noPositionScratch[1 + 12 * 4 + 1] = 0x0020001Bu;
    noPositionScratch[1 + 12 * 4 + 2] = 0x08000000u;
    noPositionScratch[1 + 12 * 4 + 3] = 0x0000F801u;
    fallbackReason.clear();
    Check(XTL::VshDiagnostics::RequiresCpuFallback(noPositionScratch, fallbackReason),
          "R12 position alias without a free temporary requires CPU fallback");
    Check(fallbackReason == "position_alias_no_scratch",
          "R12 position-alias CPU fallback reason is stable");

    std::vector<DWORD> noPairedScratch(1 + 12 * 4, 0);
    noPairedScratch[0] = 0x000C2078u;
    for(DWORD reg = 0; reg < 11; ++reg)
    {
        noPairedScratch[1 + reg * 4 + 1] = 0x0020001Bu;
        noPairedScratch[1 + reg * 4 + 2] = 0x08000000u;
        noPairedScratch[1 + reg * 4 + 3] = 0x0F000000u | (reg << 20);
    }
    noPairedScratch[1 + 11 * 4 + 1] = 0x0220001Bu;
    noPairedScratch[1 + 11 * 4 + 2] = 0x0800006Cu;
    noPairedScratch[1 + 11 * 4 + 3] = 0x1F0FF801u;
    fallbackReason.clear();
    Check(XTL::VshDiagnostics::RequiresCpuFallback(noPairedScratch, fallbackReason),
          "paired hazard without a free temporary requires CPU fallback");
    Check(fallbackReason == "paired_ilu_no_scratch",
          "paired-hazard CPU fallback reason is stable");

    std::vector<DWORD> noDualDestinationScratch{ 0x000D2078u };
    const NvTestSource unusedSource{};
    const NvTestSource identityVertex{ 2, 0, false, { 0, 1, 2, 3 } };
    for(DWORD reg = 0; reg <= 10; ++reg)
    {
        AppendNvTestInstruction(noDualDestinationScratch, EncodeNvTestInstruction(
                                                              1, 0, 0, identityVertex,
                                                              unusedSource, unusedSource, 0xFu,
                                                              reg, 0, 0, 9, 0, false));
    }
    const NvTestSource positionSource{ 1, 12, false, { 0, 1, 2, 3 } };
    const NvTestSource temporary0Source{ 1, 0, false, { 0, 1, 2, 3 } };
    AppendNvTestInstruction(noDualDestinationScratch,
                            EncodeNvTestInstruction(3, 0, 0, positionSource, unusedSource,
                                                    temporary0Source, 0xFu, 0, 0, 0xFu, 0,
                                                    0, false));
    AppendNvTestInstruction(noDualDestinationScratch,
                            EncodeNvTestInstruction(1, 0, 0, identityVertex, unusedSource,
                                                    unusedSource, 0, 0, 0, 0xFu, 9, 0,
                                                    true));
    fallbackReason.clear();
    Check(XTL::VshDiagnostics::RequiresCpuFallback(noDualDestinationScratch,
                                                   fallbackReason),
          "dual-destination dependency without a free temporary requires CPU fallback");
    Check(fallbackReason == "dual_destination_no_scratch",
          "dual-destination CPU fallback reason is stable");

    fallbackReason.clear();
    Check(XTL::VshDiagnostics::RequiresCpuFallback(kRccProgram, fallbackReason),
          "RCC requires exact CPU fallback");
    Check(XTL::VshDiagnostics::ClassifyXboxFunction(kRccProgram, fallbackReason) ==
              XTL::VshDiagnostics::XboxFunctionDisposition::ExecuteOnCpu,
          "RCC is classified for CPU execution");
    Check(fallbackReason == "rcc_requires_clamp", "RCC CPU fallback reason is stable");
    DWORD* rccTranslation = XTL::EmuVshRecompileXboxFunction(kRccProgram);
    Check(rccTranslation == nullptr, "RCC never emits an approximate RCP translation");
    delete[] rccTranslation;

    std::array<float, 192 * 4> rccConstants{};
    std::array<float, 16 * 4> rccInputs{};
    const auto checkRcc = [&](float input, float expected, const char* name)
    {
        rccInputs[0] = input;
        ShaderOutputs outputs{};
        const bool executed = XTL::VshDiagnostics::ExecuteXboxVertexShader(
            kRccProgram, rccConstants.data(), rccInputs.data(), outputs.position.data(),
            outputs.colors.data(), outputs.colors.size(), outputs.texCoords.data(),
            outputs.texCoords.size());
        Check(executed, name);
        Check(NearlyEqual(outputs.position[0], expected) &&
                  NearlyEqual(outputs.position[1], expected) &&
                  NearlyEqual(outputs.position[2], expected) &&
                  NearlyEqual(outputs.position[3], expected),
              name);
    };
    checkRcc(2.0f, 0.5f, "RCC preserves ordinary positive reciprocal");
    checkRcc(-4.0f, -0.25f, "RCC preserves ordinary negative reciprocal");
    checkRcc(0x1p-80f, 0x1p64f, "RCC clamps tiny positive input to positive 2^64");
    checkRcc(-0x1p-80f, -0x1p64f, "RCC clamps tiny negative input to negative 2^64");
    checkRcc(0.0f, 0x1p64f, "RCC clamps zero to positive 2^64");

    fallbackReason.clear();
    Check(XTL::VshDiagnostics::RequiresCpuFallback(kRelativeConstantProgram, fallbackReason),
          "relative constants require exact CPU fallback");
    Check(fallbackReason == "relative_constant_dynamic_range",
          "relative-constant CPU fallback reason is stable");
    DWORD* relativeTranslation = XTL::EmuVshRecompileXboxFunction(kRelativeConstantProgram);
    Check(relativeTranslation == nullptr,
          "relative constants never emit round-to-nearest host ARL bytecode");
    delete[] relativeTranslation;

    for(std::size_t constant = 0; constant < 192; ++constant)
    {
        for(std::size_t component = 0; component < 4; ++component)
        {
            rccConstants[constant * 4 + component] =
                static_cast<float>(constant * 10 + component);
        }
    }
    const auto checkRelativeConstant = [&](float address, std::size_t expectedConstant,
                                           const char* name)
    {
        rccInputs.fill(0.0f);
        rccInputs[0] = address;
        ShaderOutputs outputs{};
        const bool executed = XTL::VshDiagnostics::ExecuteXboxVertexShader(
            kRelativeConstantProgram, rccConstants.data(), rccInputs.data(),
            outputs.position.data(), outputs.colors.data(), outputs.colors.size(),
            outputs.texCoords.data(), outputs.texCoords.size());
        Check(executed, name);
        bool colorMatches = true;
        for(std::size_t component = 0; component < 4; ++component)
        {
            colorMatches = colorMatches &&
                           NearlyEqual(outputs.colors[component],
                                       rccConstants[expectedConstant * 4 + component]);
        }
        Check(colorMatches, name);
    };
    checkRelativeConstant(-0.25f, 96, "NV2A ARL floors a negative fractional address");
    checkRelativeConstant(1.75f, 98, "NV2A ARL floors a positive fractional address");
    checkRelativeConstant(-100.0f, 0, "relative constant address clamps at hardware c0");
    checkRelativeConstant(100.0f, 191, "relative constant address clamps at hardware c191");

    std::array<float, 96 * 4> hostRelativeConstants{};
    for(std::size_t component = 0; component < 4; ++component)
    {
        hostRelativeConstants[component] = static_cast<float>(10 + component);
        hostRelativeConstants[4 + component] = static_cast<float>(20 + component);
    }
    rccInputs.fill(0.0f);
    rccInputs[0] = -0.25f;
    const XTL::VshDiagnostics::ValidationResult hostRelativeValidation =
        XTL::VshDiagnostics::ValidateD3D8Function(kHostRelativeProgram);
    Check(hostRelativeValidation.valid, "host relative-address bytecode validates");
    ShaderOutputs hostRoundedRelativeOutputs{};
    Check(ExecuteD3D8Bytecode(kHostRelativeProgram, std::size(kHostRelativeProgram),
                              hostRelativeConstants.data(), rccInputs.data(),
                              hostRoundedRelativeOutputs),
          "host relative-address bytecode executes independently");
    Check(NearlyEqual(hostRoundedRelativeOutputs.colors[0], hostRelativeConstants[4]),
          "vs.1.1 address MOV rounds -0.25 to zero");
    rccInputs[0] = 100.0f;
    ShaderOutputs hostOutOfRangeRelativeOutputs{};
    Check(ExecuteD3D8Bytecode(kHostRelativeProgram, std::size(kHostRelativeProgram),
                              hostRelativeConstants.data(), rccInputs.data(),
                              hostOutOfRangeRelativeOutputs),
          "host out-of-range relative bytecode executes independently");
    Check(NearlyEqual(hostOutOfRangeRelativeOutputs.colors[0], 0.0f) &&
              NearlyEqual(hostOutOfRangeRelativeOutputs.colors[1], 0.0f) &&
              NearlyEqual(hostOutOfRangeRelativeOutputs.colors[2], 0.0f) &&
              NearlyEqual(hostOutOfRangeRelativeOutputs.colors[3], 0.0f),
          "vs.1.1 out-of-range relative constants read zero");

    std::array<float, 16 * 4> r11R12Inputs{};
    r11R12Inputs[0] = 1.0f;
    r11R12Inputs[1] = 2.0f;
    r11R12Inputs[2] = 3.0f;
    r11R12Inputs[3] = 4.0f;
    r11R12Inputs[4] = 11.0f;
    r11R12Inputs[5] = 12.0f;
    r11R12Inputs[6] = 13.0f;
    r11R12Inputs[7] = 14.0f;
    ShaderOutputs cpuR11R12Outputs{};
    Check(XTL::VshDiagnostics::ExecuteXboxVertexShader(
              kR11R12Program, rccConstants.data(), r11R12Inputs.data(),
              cpuR11R12Outputs.position.data(), cpuR11R12Outputs.colors.data(),
              cpuR11R12Outputs.colors.size(), cpuR11R12Outputs.texCoords.data(),
              cpuR11R12Outputs.texCoords.size()),
          "CPU R11/R12 independence shader executes");
    DWORD* r11R12Translation = XTL::EmuVshRecompileXboxFunction(kR11R12Program);
    Check(r11R12Translation != nullptr, "R11/R12 independence shader translates");
    if(r11R12Translation != nullptr)
    {
        std::array<float, 96 * 4> hostConstants{};
        ShaderOutputs d3dR11R12Outputs{};
        Check(ExecuteD3D8Bytecode(r11R12Translation, 16 + 3 * 20, hostConstants.data(),
                                  r11R12Inputs.data(), d3dR11R12Outputs),
              "translated R11/R12 bytecode executes independently");
        bool firstColorMatches = true;
        for(std::size_t component = 0; component < 4; ++component)
        {
            firstColorMatches = firstColorMatches &&
                                NearlyEqual(cpuR11R12Outputs.colors[component],
                                            d3dR11R12Outputs.colors[component]);
        }
        Check(PositionsEqual(cpuR11R12Outputs, d3dR11R12Outputs) && firstColorMatches,
              "guest R11 remains independent from the R12 position alias");
        delete[] r11R12Translation;
    }

    ShaderOutputs cpuPairedOutputs{};
    Check(XTL::VshDiagnostics::ExecuteXboxVertexShader(
              kPairedReadBeforeWriteProgram, rccConstants.data(), r11R12Inputs.data(),
              cpuPairedOutputs.position.data(), cpuPairedOutputs.colors.data(),
              cpuPairedOutputs.colors.size(), cpuPairedOutputs.texCoords.data(),
              cpuPairedOutputs.texCoords.size()),
          "CPU paired read-before-write shader executes");
    DWORD* pairedTranslation =
        XTL::EmuVshRecompileXboxFunction(kPairedReadBeforeWriteProgram);
    Check(pairedTranslation != nullptr, "paired read-before-write shader translates");
    if(pairedTranslation != nullptr)
    {
        std::array<float, 96 * 4> hostConstants{};
        ShaderOutputs d3dPairedOutputs{};
        Check(ExecuteD3D8Bytecode(pairedTranslation, 16 + 3 * 24, hostConstants.data(),
                                  r11R12Inputs.data(), d3dPairedOutputs),
              "translated paired bytecode executes independently");
        bool firstColorMatches = true;
        for(std::size_t component = 0; component < 4; ++component)
        {
            firstColorMatches = firstColorMatches &&
                                NearlyEqual(cpuPairedOutputs.colors[component],
                                            d3dPairedOutputs.colors[component]);
        }
        Check(PositionsEqual(cpuPairedOutputs, d3dPairedOutputs) && firstColorMatches,
              "paired ILU reads the pre-MAC temporary value");
        delete[] pairedTranslation;
    }

    const std::vector<std::string> xboxListing = XTL::VshDiagnostics::DecodeXboxFunction(kXboxProgram);
    const std::vector<std::string> d3dListing =
        XTL::VshDiagnostics::DecodeD3D8Function(translation.tokens);
    Check(xboxListing.size() == 5, "decoded NV2A instruction count");
    Check(d3dListing.size() == 6, "decoded optimized D3D8 instruction count");
    Check(xboxListing.front().find("mac=dp4") != std::string::npos, "NV2A listing names DP4");
    Check(d3dListing.front().find("op=dp4") != std::string::npos, "dead position seed removed");
    Check(d3dListing.back().find("dst=oR0.xyzw") != std::string::npos, "D3D8 listing identifies oPos");

    const DWORD unknownOpcode[] = { 0xFFFE0101u, 0x00001234u, 0x0000FFFFu };
    const XTL::VshDiagnostics::ValidationResult unknownValidation =
        XTL::VshDiagnostics::ValidateD3D8Function(unknownOpcode);
    Check(!unknownValidation.valid, "unknown opcode rejected");
    Check(unknownValidation.instructionIndex == 0, "unknown opcode location");
    DWORD unknownOptimizationInput[] = { 0xFFFE0101u, 0x00001234u, 0x0000FFFFu };
    const XTL::VshDiagnostics::OptimizationResult unknownOptimization =
        XTL::VshDiagnostics::OptimizeD3D8Function(unknownOptimizationInput);
    Check(!unknownOptimization.valid, "unknown opcode is not optimized");
    Check(unknownOptimizationInput[1] == 0x00001234u, "failed optimization leaves bytecode unchanged");

    const DWORD truncatedInstruction[] = { 0xFFFE0101u, 0x00000001u, 0x800F0000u };
    const XTL::VshDiagnostics::ValidationResult truncatedValidation =
        XTL::VshDiagnostics::ValidateD3D8Function(truncatedInstruction);
    Check(!truncatedValidation.valid, "truncated instruction rejected");
    Check(truncatedValidation.message == "instruction is truncated", "truncated instruction reason");

    const DWORD invalidConstant[] = {
        0xFFFE0101u,
        0x00000001u,
        0xC00F0000u,
        0xA0E40060u,
        0x0000FFFFu,
    };
    const XTL::VshDiagnostics::ValidationResult constantValidation =
        XTL::VshDiagnostics::ValidateD3D8Function(invalidConstant);
    Check(!constantValidation.valid, "constant above c95 rejected");
    Check(constantValidation.message.find("exceeds c95") != std::string::npos,
          "invalid constant reason");

    const DWORD missingPosition[] = {
        0xFFFE0101u,
        0x00000001u,
        0x800F0000u,
        0x90E40000u,
        0x0000FFFFu,
    };
    const XTL::VshDiagnostics::ValidationResult positionValidation =
        XTL::VshDiagnostics::ValidateD3D8Function(missingPosition);
    Check(!positionValidation.valid, "missing oPos rejected");
    Check(positionValidation.message == "shader never writes oPos", "missing oPos reason");

    DWORD overwrittenTemporary[] = {
        0xFFFE0101u,
        0x00000001u,
        0x800F0000u,
        0x90E40000u,
        0x00000001u,
        0x800F0000u,
        0x90E40001u,
        0x00000001u,
        0xC00F0000u,
        0x80E40000u,
        0x0000FFFFu,
    };
    const XTL::VshDiagnostics::OptimizationResult overwriteOptimization =
        XTL::VshDiagnostics::OptimizeD3D8Function(overwrittenTemporary);
    Check(overwriteOptimization.valid, "dead-write optimization succeeds");
    Check(overwriteOptimization.beforeInstructionCount == 3, "dead-write input count");
    Check(overwriteOptimization.afterInstructionCount == 2, "overwritten temporary removed");
    Check((overwrittenTemporary[3] & 0x7FFu) == 1, "remaining temporary uses latest input");

    DWORD partialTemporary[] = {
        0xFFFE0101u,
        0x00000001u,
        0x80030000u,
        0x90E40000u,
        0x00000001u,
        0xC0010000u,
        0x80E40000u,
        0x0000FFFFu,
    };
    const XTL::VshDiagnostics::OptimizationResult partialOptimization =
        XTL::VshDiagnostics::OptimizeD3D8Function(partialTemporary);
    Check(partialOptimization.valid, "partial-mask optimization succeeds");
    Check(((partialTemporary[2] >> 16) & 0xFu) == 0x1u, "dead destination component removed");

    DWORD dotProductDependency[] = {
        0xFFFE0101u,
        0x00000001u,
        0x80020000u,
        0x90550000u,
        0x00000008u,
        0xC0010000u,
        0x80E40000u,
        0x80E40000u,
        0x0000FFFFu,
    };
    const XTL::VshDiagnostics::OptimizationResult dotOptimization =
        XTL::VshDiagnostics::OptimizeD3D8Function(dotProductDependency);
    Check(dotOptimization.afterInstructionCount == 2, "dot product preserves all source lanes");
    Check(((dotProductDependency[2] >> 16) & 0xFu) == 0x2u, "dot product preserves Y dependency");

    DWORD relativeAddress[] = {
        0xFFFE0101u,
        0x00000001u,
        0xB0010000u,
        0x90E40000u,
        0x00000001u,
        0x800F0000u,
        0xA0E42000u,
        0x00000001u,
        0xC00F0000u,
        0x80E40000u,
        0x0000FFFFu,
    };
    const XTL::VshDiagnostics::OptimizationResult relativeOptimization =
        XTL::VshDiagnostics::OptimizeD3D8Function(relativeAddress);
    Check(relativeOptimization.valid, "relative-address optimization succeeds");
    Check(relativeOptimization.afterInstructionCount == 3, "relative addressing preserves a0 write");

    DWORD unusedAddress[] = {
        0xFFFE0101u,
        0x00000001u,
        0xB0010000u,
        0x90E40000u,
        0x00000001u,
        0x800F0000u,
        0xA0E40000u,
        0x00000001u,
        0xC00F0000u,
        0x80E40000u,
        0x0000FFFFu,
    };
    const XTL::VshDiagnostics::OptimizationResult addressOptimization =
        XTL::VshDiagnostics::OptimizeD3D8Function(unusedAddress);
    Check(addressOptimization.valid, "unused-address optimization succeeds");
    Check(addressOptimization.afterInstructionCount == 2, "unused a0 write removed");

    std::vector<DWORD> oversizedShader = {
        0xFFFE0101u,
        0x00000001u,
        0xC00F0000u,
        0x90E40000u,
    };
    oversizedShader.insert(oversizedShader.end(), 128, 0x00000000u);
    oversizedShader.push_back(0x0000FFFFu);
    const XTL::VshDiagnostics::ValidationResult oversizedValidation =
        XTL::VshDiagnostics::ValidateD3D8Function(oversizedShader);
    Check(!oversizedValidation.valid, "shader above 128 instructions rejected");
    Check(oversizedValidation.instructionIndex == 128, "instruction-limit location");
    Check(oversizedValidation.message.find("limit of 128") != std::string::npos,
          "instruction-limit reason");
    const XTL::VshDiagnostics::OptimizationResult oversizedOptimization =
        XTL::VshDiagnostics::OptimizeD3D8Function(oversizedShader);
    Check(oversizedOptimization.beforeInstructionCount == 129, "oversized optimizer input count");
    Check(oversizedOptimization.afterInstructionCount == 1, "dead NOP instructions removed");
    const XTL::VshDiagnostics::ValidationResult optimizedOversizedValidation =
        XTL::VshDiagnostics::ValidateD3D8Function(
            { oversizedShader.data(), oversizedOptimization.tokenCount });
    Check(optimizedOversizedValidation.valid, "optimized shader meets instruction limit");

    std::vector<std::uint32_t> quadIndices;
    Check(XTL::VshDiagnostics::ExpandQuadListIndices(nullptr, 4, quadIndices),
          "single sequential quad expands");
    Check(quadIndices == std::vector<std::uint32_t>({ 0, 1, 2, 0, 2, 3 }),
          "single quad becomes two triangles");
    Check(XTL::VshDiagnostics::ExpandQuadListIndices(nullptr, 8, quadIndices),
          "multiple sequential quads expand");
    Check(quadIndices == std::vector<std::uint32_t>({ 0, 1, 2, 0, 2, 3,
                                                      4, 5, 6, 4, 6, 7 }),
          "multiple quads preserve independent topology");
    const std::uint32_t indexedQuad[] = { 9, 2, 7, 4 };
    Check(XTL::VshDiagnostics::ExpandQuadListIndices(indexedQuad, std::size(indexedQuad), quadIndices),
          "indexed quad expands");
    Check(quadIndices == std::vector<std::uint32_t>({ 9, 2, 7, 9, 7, 4 }),
          "indexed quad preserves source indices");
    quadIndices.assign({ 99, 100 });
    Check(!XTL::VshDiagnostics::ExpandQuadListIndices(nullptr, 5, quadIndices),
          "incomplete quad is rejected");
    Check(quadIndices.empty(), "failed quad expansion clears output");
    if constexpr((std::numeric_limits<std::size_t>::max)() >
                 (std::numeric_limits<std::uint32_t>::max)())
    {
        const std::size_t oversizedSequentialCount =
            static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()) + 1;
        Check(!XTL::VshDiagnostics::ExpandQuadListIndices(nullptr, oversizedSequentialCount,
                                                          quadIndices),
              "sequential quad index overflow is rejected");
    }

    const DWORD cpuDeclaration[] = {
        0x20000000u,
        0x40320000u,
        0x40400001u,
        0x40250002u,
        0xFFFFFFFFu,
    };
    std::array<std::uint8_t, 20> cpuVertex{};
    const float cpuPosition[] = { 2.0f, 3.0f, 4.0f };
    const DWORD cpuColor = 0x80402010u;
    const std::int16_t cpuShorts[] = { -2, 7 };
    std::memcpy(cpuVertex.data(), cpuPosition, sizeof(cpuPosition));
    std::memcpy(cpuVertex.data() + sizeof(cpuPosition), &cpuColor, sizeof(cpuColor));
    std::memcpy(cpuVertex.data() + sizeof(cpuPosition) + sizeof(cpuColor), cpuShorts, sizeof(cpuShorts));
    const XTL::VshDiagnostics::VertexStreamView cpuStream = {
        cpuVertex.data(),
        cpuVertex.size(),
        cpuVertex.size(),
    };
    std::array<float, 16 * 4> cpuInputs{};
    Check(XTL::VshDiagnostics::DecodeXboxVertex(cpuDeclaration, &cpuStream, 1, 0,
                                                cpuInputs.data(), cpuInputs.size()),
          "CPU fallback vertex declaration decodes");
    Check(cpuInputs[0] == 2.0f && cpuInputs[1] == 3.0f && cpuInputs[2] == 4.0f && cpuInputs[3] == 1.0f,
          "FLOAT3 expands with W=1");
    Check(cpuInputs[4] == 64.0f / 255.0f && cpuInputs[5] == 32.0f / 255.0f &&
              cpuInputs[6] == 16.0f / 255.0f && cpuInputs[7] == 128.0f / 255.0f,
          "D3DCOLOR expands to RGBA");
    Check(cpuInputs[8] == -2.0f && cpuInputs[9] == 7.0f && cpuInputs[10] == 0.0f && cpuInputs[11] == 1.0f,
          "SHORT2 expands with Z=0 W=1");

    const DWORD pcTypeDeclaration[] = {
        0x20000000u,
        0x40000000u,
        0x40010001u,
        0x40020002u,
        0x40030003u,
        0x40040004u,
        0x40050005u,
        0x40060006u,
        0x40070007u,
        0xFFFFFFFFu,
    };
    std::array<std::uint8_t, 60> pcTypeVertex{};
    const XTL::VshDiagnostics::VertexStreamView pcTypeStream = {
        pcTypeVertex.data(),
        pcTypeVertex.size(),
        pcTypeVertex.size(),
    };
    Check(XTL::VshDiagnostics::DecodeXboxVertex(pcTypeDeclaration, &pcTypeStream, 1, 0,
                                                cpuInputs.data(), cpuInputs.size()),
          "CPU fallback decodes every accepted PC declaration type");

    const DWORD skippedDeclaration[] = {
        0x20000000u,
        0x40120000u,
        0x50020000u,
        0x40120001u,
        0xFFFFFFFFu,
    };
    const std::array<float, 4> skippedVertex = { 1.0f, 99.0f, 98.0f, 7.0f };
    const XTL::VshDiagnostics::VertexStreamView skippedStream = {
        skippedVertex.data(),
        sizeof(skippedVertex),
        sizeof(skippedVertex),
    };
    Check(XTL::VshDiagnostics::DecodeXboxVertex(skippedDeclaration, &skippedStream, 1, 0,
                                                cpuInputs.data(), cpuInputs.size()) &&
              cpuInputs[0] == 1.0f && cpuInputs[4] == 7.0f,
          "CPU declaration skip advances by DWORDs");
    XTL::VshDiagnostics::VertexStreamView shortSkippedStream = skippedStream;
    shortSkippedStream.stride -= sizeof(float);
    Check(!XTL::VshDiagnostics::DecodeXboxVertex(skippedDeclaration, &shortSkippedStream, 1, 0,
                                                 cpuInputs.data(), cpuInputs.size()),
          "CPU declaration skip cannot cross the vertex stride");

    const DWORD multiStreamDeclaration[] = {
        0x20000000u,
        0x40120000u,
        0x20000001u,
        0x40120001u,
        0xFFFFFFFFu,
    };
    const float streamZeroValue = 3.0f;
    const float streamOneValue = 5.0f;
    const std::array<XTL::VshDiagnostics::VertexStreamView, 2> multiStreams = { {
        { &streamZeroValue, sizeof(streamZeroValue), sizeof(streamZeroValue) },
        { &streamOneValue, sizeof(streamOneValue), sizeof(streamOneValue) },
    } };
    Check(XTL::VshDiagnostics::DecodeXboxVertex(
              multiStreamDeclaration, multiStreams.data(), multiStreams.size(), 0,
              cpuInputs.data(), cpuInputs.size()) &&
              cpuInputs[0] == streamZeroValue && cpuInputs[4] == streamOneValue,
          "CPU declaration decoder maintains independent stream offsets");

    const DWORD packedDeclaration[] = {
        0x20000000u,
        0x40350000u,
        0x40160001u,
        0x40340002u,
        0xFFFFFFFFu,
    };
    std::array<std::uint8_t, 13> packedVertex{};
    const std::int16_t short3[] = { -3, 4, 9 };
    const DWORD normalizedPacked = 0x1FFu | (0x600u << 11);
    const std::uint8_t packedBytes[] = { 1, 2, 255 };
    std::memcpy(packedVertex.data(), short3, sizeof(short3));
    std::memcpy(packedVertex.data() + sizeof(short3), &normalizedPacked, sizeof(normalizedPacked));
    std::memcpy(packedVertex.data() + sizeof(short3) + sizeof(normalizedPacked), packedBytes, sizeof(packedBytes));
    const XTL::VshDiagnostics::VertexStreamView packedStream = {
        packedVertex.data(),
        packedVertex.size(),
        packedVertex.size(),
    };
    Check(XTL::VshDiagnostics::DecodeXboxVertex(packedDeclaration, &packedStream, 1, 0,
                                                cpuInputs.data(), cpuInputs.size()),
          "extended Xbox declaration types decode");
    Check(cpuInputs[0] == -3.0f && cpuInputs[1] == 4.0f && cpuInputs[2] == 9.0f && cpuInputs[3] == 1.0f,
          "SHORT3 expands with W=1");
    Check(cpuInputs[4] > 0.49f && cpuInputs[5] < -0.49f && cpuInputs[6] == 0.0f,
          "NORMPACKED3 sign-extends and normalizes");
    Check(NearlyEqual(cpuInputs[8], 1.0f / 255.0f) &&
              NearlyEqual(cpuInputs[9], 2.0f / 255.0f) && cpuInputs[10] == 1.0f,
          "PBYTE3 expands to normalized scalar components");

    std::array<float, 192 * 4> cpuConstants{};
    cpuConstants[0] = 1.0f;
    cpuConstants[5] = 1.0f;
    cpuConstants[10] = 1.0f;
    cpuConstants[15] = 1.0f;
    cpuInputs.fill(0.0f);
    cpuInputs[0] = 2.0f;
    cpuInputs[1] = 3.0f;
    cpuInputs[2] = 4.0f;
    cpuInputs[3] = 1.0f;
    std::array<float, 4> cpuOutputPosition{};
    std::array<float, 4> cpuOutputColor{};
    std::array<float, 4 * 4> cpuOutputTexCoord{};
    cpuOutputTexCoord.fill(99.0f);
    Check(XTL::VshDiagnostics::ExecuteXboxVertexShader(kXboxProgram, cpuConstants.data(), cpuInputs.data(),
                                                       cpuOutputPosition.data(), cpuOutputColor.data(),
                                                       cpuOutputColor.size(), cpuOutputTexCoord.data(),
                                                       cpuOutputTexCoord.size()),
          "CPU fallback executes NV2A microcode");
    Check(cpuOutputPosition[0] == 2.0f && cpuOutputPosition[1] == 3.0f &&
              cpuOutputPosition[2] == 4.0f && cpuOutputPosition[3] == 1.0f,
          "CPU fallback transforms position");
    Check(cpuOutputTexCoord.front() == 0.0f && cpuOutputTexCoord.back() == 0.0f,
          "CPU fallback returns all four texture-coordinate outputs");

    cpuInputs[4] = 0.5f;
    cpuInputs[8] = 7.5f;
    XTL::VshDiagnostics::RasterOutputs rasterOutputs{};
    Check(XTL::VshDiagnostics::ExecuteXboxVertexShader(
              kRasterOutputsProgram, cpuConstants.data(), cpuInputs.data(),
              cpuOutputPosition.data(), cpuOutputColor.data(), cpuOutputColor.size(),
              cpuOutputTexCoord.data(), cpuOutputTexCoord.size(), &rasterOutputs),
          "CPU fallback returns raster outputs");
    Check(rasterOutputs.fogWriteMask == 0x8u && rasterOutputs.fog[0] == 0.5f,
          "fog value and write mask are preserved");
    Check(rasterOutputs.pointSizeWriteMask == 0x8u && rasterOutputs.pointSize[0] == 7.5f,
          "point-size value and write mask are preserved");

    const float maskedRaster[] = { 1.0f, 2.0f, 3.0f, 4.0f };
    Check(XTL::VshDiagnostics::SelectRasterOutput(maskedRaster, 0x2u, 9.0f) == 3.0f,
          "raster selection honors NV2A component masks");
    Check(XTL::VshDiagnostics::SelectRasterOutput(maskedRaster, 0, 9.0f) == 9.0f,
          "unwritten raster output uses fallback");
    Check(XTL::VshDiagnostics::ClampPointSize(-1.0f, 3.0f, 10.0f) == 3.0f,
          "invalid point size uses render-state fallback");
    Check(XTL::VshDiagnostics::ClampPointSize(20.0f, 3.0f, 10.0f) == 10.0f,
          "point size clamps to host maximum");
    Check(XTL::VshDiagnostics::ClampPointSize(
              (std::numeric_limits<float>::quiet_NaN)(), 3.0f, 10.0f) == 3.0f,
          "NaN point size uses render-state fallback");

    const float packedColor[] = { -1.0f, 0.5f, 2.0f,
                                  (std::numeric_limits<float>::quiet_NaN)() };
    Check(XTL::VshDiagnostics::PackD3DColor(packedColor) == 0x00007FFFu,
          "D3D color packing clamps components and NaN");
    const float specularColor[] = { 0.25f, 0.5f, 1.0f, 0.0f };
    Check(XTL::VshDiagnostics::PackD3DSpecularFog(specularColor, rasterOutputs) == 0x7F3F7FFFu,
          "fog replaces specular alpha and preserves RGB");
    XTL::VshDiagnostics::RasterOutputs unwrittenRaster{};
    Check(XTL::VshDiagnostics::PackD3DSpecularFog(specularColor, unwrittenRaster) == 0xFF3F7FFFu,
          "unwritten fog defaults to no fog");

    std::array<float, 192 * 4> differentialHardwareConstants{};
    std::array<float, 96 * 4> differentialD3D8Constants{};
    for(std::size_t row = 0; row < 4; ++row)
    {
        differentialHardwareConstants[(96 + row) * 4 + row] = 1.0f;
        differentialD3D8Constants[row * 4 + row] = 1.0f;
    }
    std::array<float, 16 * 4> differentialInputs{};
    differentialInputs[0] = 2.0f;
    differentialInputs[1] = 3.0f;
    differentialInputs[2] = 4.0f;
    differentialInputs[3] = 7.0f;

    ShaderOutputs cpuPositionOutputs{};
    Check(XTL::VshDiagnostics::ExecuteXboxVertexShader(
              kDifferentialPositionProgram, differentialHardwareConstants.data(),
              differentialInputs.data(), cpuPositionOutputs.position.data(),
              cpuPositionOutputs.colors.data(), cpuPositionOutputs.colors.size(),
              cpuPositionOutputs.texCoords.data(), cpuPositionOutputs.texCoords.size()),
          "differential CPU position shader executes");
    DWORD* differentialPositionD3D8 =
        XTL::EmuVshRecompileXboxFunction(kDifferentialPositionProgram);
    Check(differentialPositionD3D8 != nullptr, "differential position shader translates");
    if(differentialPositionD3D8 != nullptr)
    {
        ShaderOutputs d3d8PositionOutputs{};
        Check(ExecuteD3D8Bytecode(differentialPositionD3D8, 16 + 5 * 20,
                                  differentialD3D8Constants.data(), differentialInputs.data(),
                                  d3d8PositionOutputs),
              "translated position bytecode executes independently");
        Check(PositionsEqual(cpuPositionOutputs, d3d8PositionOutputs),
              "CPU and translated position outputs match");
        Check(NearlyEqual(cpuPositionOutputs.position[3], 1.0f),
              "DPH adds B.w independently of A.w");
        const std::vector<std::string> dphListing = XTL::VshDiagnostics::DecodeD3D8Function(
            { differentialPositionD3D8, 16 + 5 * 20 });
        bool hasDp3 = false;
        bool hasAdd = false;
        for(const std::string& instruction : dphListing)
        {
            hasDp3 = hasDp3 || instruction.find("op=dp3") != std::string::npos;
            hasAdd = hasAdd || instruction.find("op=add") != std::string::npos;
        }
        Check(hasDp3 && hasAdd, "DPH lowers to exact DP3 plus ADD sequence");
        delete[] differentialPositionD3D8;
    }

    for(std::size_t inputRegister = 0; inputRegister < 7; ++inputRegister)
    {
        for(std::size_t component = 0; component < 4; ++component)
        {
            differentialInputs[inputRegister * 4 + component] =
                static_cast<float>(inputRegister * 10 + component + 1);
        }
    }
    ShaderOutputs cpuFullOutputs{};
    Check(XTL::VshDiagnostics::ExecuteXboxVertexShader(
              kDifferentialOutputsProgram, differentialHardwareConstants.data(),
              differentialInputs.data(), cpuFullOutputs.position.data(), cpuFullOutputs.colors.data(),
              cpuFullOutputs.colors.size(), cpuFullOutputs.texCoords.data(),
              cpuFullOutputs.texCoords.size()),
          "differential CPU output shader executes");
    DWORD* differentialOutputsD3D8 =
        XTL::EmuVshRecompileXboxFunction(kDifferentialOutputsProgram);
    Check(differentialOutputsD3D8 != nullptr, "differential output shader translates");
    if(differentialOutputsD3D8 != nullptr)
    {
        ShaderOutputs d3d8FullOutputs{};
        Check(ExecuteD3D8Bytecode(differentialOutputsD3D8, 4 + 7 * 20,
                                  differentialD3D8Constants.data(), differentialInputs.data(),
                                  d3d8FullOutputs),
              "translated output bytecode executes independently");
        Check(OutputsEqual(cpuFullOutputs, d3d8FullOutputs),
              "CPU and translated position/color/texcoord outputs match");
        delete[] differentialOutputsD3D8;
    }

    differentialInputs.fill(0.0f);
    differentialInputs[0] = 0.25f;
    differentialInputs[1] = -0.5f;
    differentialInputs[2] = 0.75f;
    differentialInputs[3] = 1.0f;
    differentialInputs[4] = 8.0f;
    differentialInputs[5] = 8.0f;
    differentialInputs[6] = 8.0f;
    differentialInputs[7] = 8.0f;
    ShaderOutputs cpuEpilogueOutputs{};
    Check(XTL::VshDiagnostics::ExecuteXboxVertexShader(
              kScreenSpaceEpilogueProgram, differentialHardwareConstants.data(),
              differentialInputs.data(), cpuEpilogueOutputs.position.data(),
              cpuEpilogueOutputs.colors.data(), cpuEpilogueOutputs.colors.size(),
              cpuEpilogueOutputs.texCoords.data(), cpuEpilogueOutputs.texCoords.size()),
          "CPU screen-space epilogue shader executes");
    DWORD* epilogueD3D8 = XTL::EmuVshRecompileXboxFunction(kScreenSpaceEpilogueProgram);
    Check(epilogueD3D8 != nullptr, "screen-space epilogue shader translates");
    if(epilogueD3D8 != nullptr)
    {
        ShaderOutputs d3d8EpilogueOutputs{};
        Check(ExecuteD3D8Bytecode(epilogueD3D8, 4 + 2 * 20,
                                  differentialD3D8Constants.data(), differentialInputs.data(),
                                  d3d8EpilogueOutputs),
              "translated epilogue bytecode executes independently");
        Check(PositionsEqual(cpuEpilogueOutputs, d3d8EpilogueOutputs),
              "CPU and translator remove the same screen-space epilogue");
        Check(NearlyEqual(cpuEpilogueOutputs.position[0], differentialInputs[0]) &&
                  NearlyEqual(cpuEpilogueOutputs.position[1], differentialInputs[1]) &&
                  NearlyEqual(cpuEpilogueOutputs.position[2], differentialInputs[2]) &&
                  NearlyEqual(cpuEpilogueOutputs.position[3], differentialInputs[3]),
              "screen-space removal preserves clip-space position");
        delete[] epilogueD3D8;
    }

    const NvTestSource viewportUnusedSource{};
    const NvTestSource viewportPositionSource{ 1, 12, false, { 0, 1, 2, 3 } };
    const NvTestSource reciprocalWSource{ 1, 1, false, { 0, 0, 0, 0 } };
    const NvTestSource constantSource{ 3, 0, false, { 0, 1, 2, 3 } };
    const NvTestSource vertexPositionSource{ 2, 0, false, { 0, 1, 2, 3 } };
    const NvTestSource vertexColorSource{ 2, 0, false, { 0, 1, 2, 3 } };
    std::vector<DWORD> viewportPairProgram{ 0x00042078u };
    AppendNvTestInstruction(
        viewportPairProgram,
        EncodeNvTestInstruction(1, 0, 0, vertexPositionSource, viewportUnusedSource,
                                viewportUnusedSource,
                                0, 0, 0, 0xF, 0, 0, false));
    AppendNvTestInstruction(
        viewportPairProgram,
        EncodeNvTestInstruction(2, 0, 0, viewportPositionSource, constantSource,
                                viewportUnusedSource,
                                0, 0, 0, 0xE, 0, 0, false, 58));
    AppendNvTestInstruction(
        viewportPairProgram,
        EncodeNvTestInstruction(4, 0, 0, viewportPositionSource, reciprocalWSource,
                                constantSource, 0, 0, 0, 0xE, 0, 0, false, 59));
    AppendNvTestInstruction(
        viewportPairProgram,
        EncodeNvTestInstruction(1, 0, 2, vertexColorSource, viewportUnusedSource,
                                viewportUnusedSource,
                                0, 0, 0, 0xF, 3, 0, true));
    ShaderOutputs cpuViewportPairOutputs{};
    Check(XTL::VshDiagnostics::ExecuteXboxVertexShader(
              viewportPairProgram, differentialHardwareConstants.data(),
              differentialInputs.data(), cpuViewportPairOutputs.position.data(),
              cpuViewportPairOutputs.colors.data(), cpuViewportPairOutputs.colors.size(),
              cpuViewportPairOutputs.texCoords.data(), cpuViewportPairOutputs.texCoords.size()),
          "CPU embedded viewport pair shader executes");
    Check(PositionsEqual(cpuViewportPairOutputs, cpuEpilogueOutputs),
          "CPU removes embedded c58/c59 viewport pair");
    DWORD* viewportPairD3D8 =
        XTL::EmuVshRecompileXboxFunction(viewportPairProgram.data());
    Check(viewportPairD3D8 != nullptr, "embedded viewport pair shader translates");
    if(viewportPairD3D8 != nullptr)
    {
        ShaderOutputs d3d8ViewportPairOutputs{};
        Check(ExecuteD3D8Bytecode(viewportPairD3D8, 4 + 4 * 20,
                                  differentialD3D8Constants.data(), differentialInputs.data(),
                                  d3d8ViewportPairOutputs),
              "translated embedded viewport pair bytecode executes independently");
        Check(PositionsEqual(cpuViewportPairOutputs, d3d8ViewportPairOutputs),
              "CPU and translator remove embedded c58/c59 viewport pair");
        delete[] viewportPairD3D8;
    }

    const NvTestSource vertexSecondaryColorSource{ 2, 1, false, { 0, 1, 2, 3 } };
    std::vector<DWORD> interleavedViewportPairProgram{ 0x00052078u };
    AppendNvTestInstruction(
        interleavedViewportPairProgram,
        EncodeNvTestInstruction(1, 0, 0, vertexPositionSource, viewportUnusedSource,
                                viewportUnusedSource, 0, 0, 0, 0xF, 0, 0, false));
    AppendNvTestInstruction(
        interleavedViewportPairProgram,
        EncodeNvTestInstruction(2, 0, 0, viewportPositionSource, constantSource,
                                viewportUnusedSource, 0, 0, 0, 0xE, 0, 0, false, 58));
    AppendNvTestInstruction(
        interleavedViewportPairProgram,
        EncodeNvTestInstruction(1, 0, 1, vertexSecondaryColorSource,
                                viewportUnusedSource, viewportUnusedSource, 0, 0, 0, 0xF,
                                4, 0, false));
    AppendNvTestInstruction(
        interleavedViewportPairProgram,
        EncodeNvTestInstruction(4, 0, 0, viewportPositionSource, reciprocalWSource,
                                constantSource, 0, 0, 0, 0xE, 0, 0, false, 59));
    AppendNvTestInstruction(
        interleavedViewportPairProgram,
        EncodeNvTestInstruction(1, 0, 2, vertexColorSource, viewportUnusedSource,
                                viewportUnusedSource, 0, 0, 0, 0xF, 3, 0, true));
    ShaderOutputs cpuInterleavedViewportPairOutputs{};
    Check(XTL::VshDiagnostics::ExecuteXboxVertexShader(
              interleavedViewportPairProgram, differentialHardwareConstants.data(),
              differentialInputs.data(), cpuInterleavedViewportPairOutputs.position.data(),
              cpuInterleavedViewportPairOutputs.colors.data(),
              cpuInterleavedViewportPairOutputs.colors.size(),
              cpuInterleavedViewportPairOutputs.texCoords.data(),
              cpuInterleavedViewportPairOutputs.texCoords.size()),
          "CPU interleaved viewport pair shader executes");
    Check(PositionsEqual(cpuInterleavedViewportPairOutputs, cpuEpilogueOutputs),
          "CPU removes interleaved c58/c59 viewport pair");
    Check(NearlyEqual(cpuInterleavedViewportPairOutputs.colors[4],
                      differentialInputs[4]),
          "CPU preserves instruction scheduled inside viewport pair");
    DWORD* interleavedViewportPairD3D8 =
        XTL::EmuVshRecompileXboxFunction(interleavedViewportPairProgram.data());
    Check(interleavedViewportPairD3D8 != nullptr,
          "interleaved viewport pair shader translates");
    if(interleavedViewportPairD3D8 != nullptr)
    {
        ShaderOutputs d3d8InterleavedViewportPairOutputs{};
        Check(ExecuteD3D8Bytecode(interleavedViewportPairD3D8, 4 + 5 * 20,
                                  differentialD3D8Constants.data(),
                                  differentialInputs.data(),
                                  d3d8InterleavedViewportPairOutputs),
              "translated interleaved viewport pair bytecode executes independently");
        Check(OutputsEqual(cpuInterleavedViewportPairOutputs,
                           d3d8InterleavedViewportPairOutputs),
              "CPU and translator preserve interleaved viewport pair semantics");
        delete[] interleavedViewportPairD3D8;
    }

    std::vector<DWORD> fusedViewportPairProgram = viewportPairProgram;
    const NvTestSource positionWSource{ 1, 12, false, { 3, 3, 3, 3 } };
    const std::array<DWORD, 4> fusedViewportScale = EncodeNvTestInstruction(
        2, 3, 0, viewportPositionSource, constantSource, positionWSource, 0, 7, 0x8,
        0xE, 0, 0, false, 58);
    std::copy(fusedViewportScale.begin(), fusedViewportScale.end(),
              fusedViewportPairProgram.begin() + 5);
    std::vector<DWORD> rasterViewportPairProgram = viewportPairProgram;
    const std::array<DWORD, 4> rasterViewportScale = EncodeNvTestInstruction(
        2, 3, 0, viewportPositionSource, constantSource, positionWSource, 0, 1, 0x8,
        0xE, 0, 0, false, 58);
    std::copy(rasterViewportScale.begin(), rasterViewportScale.end(),
              rasterViewportPairProgram.begin() + 5);
    std::array<float, 192 * 4> rasterViewportConstants = differentialHardwareConstants;
    rasterViewportConstants[58 * 4 + 0] = 100.0f;
    rasterViewportConstants[58 * 4 + 1] = 200.0f;
    rasterViewportConstants[58 * 4 + 2] = 300.0f;
    rasterViewportConstants[58 * 4 + 3] = 1.0f;
    rasterViewportConstants[59 * 4 + 0] = 10.0f;
    rasterViewportConstants[59 * 4 + 1] = 20.0f;
    rasterViewportConstants[59 * 4 + 2] = 30.0f;
    rasterViewportConstants[59 * 4 + 3] = 0.0f;
    ShaderOutputs rasterFusedViewportPairOutputs{};
    Check(EmuVshExecuteProgramRaster(
              rasterViewportPairProgram.data() + 1, 4, 0,
              rasterViewportConstants.data(), differentialInputs.data(),
              rasterFusedViewportPairOutputs.position.data(),
              rasterFusedViewportPairOutputs.colors.data(),
              rasterFusedViewportPairOutputs.texCoords.data()),
          "raw raster fused viewport pair shader executes");
    Check(NearlyEqual(rasterFusedViewportPairOutputs.position[0], 35.0f) &&
              NearlyEqual(rasterFusedViewportPairOutputs.position[1], -80.0f) &&
              NearlyEqual(rasterFusedViewportPairOutputs.position[2], 255.0f) &&
              NearlyEqual(rasterFusedViewportPairOutputs.position[3], 1.0f),
          "raw raster execution preserves fused c58/c59 viewport pair");
    ShaderOutputs cpuFusedViewportPairOutputs{};
    Check(XTL::VshDiagnostics::ExecuteXboxVertexShader(
              fusedViewportPairProgram, differentialHardwareConstants.data(),
              differentialInputs.data(), cpuFusedViewportPairOutputs.position.data(),
              cpuFusedViewportPairOutputs.colors.data(),
              cpuFusedViewportPairOutputs.colors.size(),
              cpuFusedViewportPairOutputs.texCoords.data(),
              cpuFusedViewportPairOutputs.texCoords.size()),
          "CPU fused viewport pair shader executes");
    Check(PositionsEqual(cpuFusedViewportPairOutputs, cpuEpilogueOutputs),
          "CPU removes fused RCC c58/c59 viewport pair");
    fallbackReason.clear();
    Check(XTL::VshDiagnostics::ClassifyXboxFunction(fusedViewportPairProgram,
                                                    fallbackReason) ==
              XTL::VshDiagnostics::XboxFunctionDisposition::TranslateToHost,
          "dead RCC in fused viewport pair permits host translation");
    Check(fallbackReason.empty(), "removed fused RCC has no fallback reason");
    DWORD* fusedViewportPairD3D8 =
        XTL::EmuVshRecompileXboxFunction(fusedViewportPairProgram.data());
    Check(fusedViewportPairD3D8 != nullptr, "fused RCC viewport pair shader translates");
    if(fusedViewportPairD3D8 != nullptr)
    {
        const XTL::VshDiagnostics::ValidationResult fusedValidation =
            XTL::VshDiagnostics::ValidateD3D8Translation(
                fusedViewportPairProgram, { fusedViewportPairD3D8, 4 + 4 * 20 });
        Check(fusedValidation.valid, "fused RCC viewport pair bytecode validates");
        ShaderOutputs d3d8FusedViewportPairOutputs{};
        Check(ExecuteD3D8Bytecode(fusedViewportPairD3D8, 4 + 4 * 20,
                                  differentialD3D8Constants.data(),
                                  differentialInputs.data(),
                                  d3d8FusedViewportPairOutputs),
              "translated fused viewport pair bytecode executes independently");
        Check(PositionsEqual(cpuFusedViewportPairOutputs, d3d8FusedViewportPairOutputs),
              "CPU and translator remove fused RCC c58/c59 viewport pair");
        delete[] fusedViewportPairD3D8;
    }

    std::vector<DWORD> delayedFusedViewportPairProgram{ 0x00062078u };
    AppendNvTestInstruction(
        delayedFusedViewportPairProgram,
        EncodeNvTestInstruction(1, 0, 0, vertexPositionSource, viewportUnusedSource,
                                viewportUnusedSource, 0, 0, 0, 0xF, 0, 0, false));
    AppendNvTestInstruction(delayedFusedViewportPairProgram, fusedViewportScale);
    AppendNvTestInstruction(
        delayedFusedViewportPairProgram,
        EncodeNvTestInstruction(0, 2, 0, viewportUnusedSource, viewportUnusedSource,
                                vertexPositionSource, 0, 1, 0x2, 0, 0, 0, false));
    AppendNvTestInstruction(
        delayedFusedViewportPairProgram,
        EncodeNvTestInstruction(1, 0, 1, vertexSecondaryColorSource,
                                viewportUnusedSource, viewportUnusedSource, 0, 0, 0, 0xF,
                                4, 0, false));
    AppendNvTestInstruction(
        delayedFusedViewportPairProgram,
        EncodeNvTestInstruction(1, 0, 0, vertexColorSource, viewportUnusedSource,
                                viewportUnusedSource, 0, 0, 0, 0xF, 3, 0, false));
    AppendNvTestInstruction(
        delayedFusedViewportPairProgram,
        EncodeNvTestInstruction(4, 0, 0, viewportPositionSource, reciprocalWSource,
                                constantSource, 0, 0, 0, 0xE, 0, 0, true, 59));
    fallbackReason.clear();
    Check(XTL::VshDiagnostics::ClassifyXboxFunction(
              delayedFusedViewportPairProgram, fallbackReason) ==
              XTL::VshDiagnostics::XboxFunctionDisposition::TranslateToHost,
          "delayed fused viewport pair permits host translation");
    Check(fallbackReason.empty(), "delayed fused viewport pair has no fallback reason");
    ShaderOutputs cpuDelayedFusedViewportPairOutputs{};
    Check(XTL::VshDiagnostics::ExecuteXboxVertexShader(
              delayedFusedViewportPairProgram,
              differentialHardwareConstants.data(), differentialInputs.data(),
              cpuDelayedFusedViewportPairOutputs.position.data(),
              cpuDelayedFusedViewportPairOutputs.colors.data(),
              cpuDelayedFusedViewportPairOutputs.colors.size(),
              cpuDelayedFusedViewportPairOutputs.texCoords.data(),
              cpuDelayedFusedViewportPairOutputs.texCoords.size()),
          "CPU delayed fused viewport pair shader executes");
    Check(PositionsEqual(cpuDelayedFusedViewportPairOutputs, cpuEpilogueOutputs),
          "CPU removes delayed fused RCC c58/c59 viewport pair");
    DWORD* delayedFusedViewportPairD3D8 =
        XTL::EmuVshRecompileXboxFunction(delayedFusedViewportPairProgram.data());
    Check(delayedFusedViewportPairD3D8 != nullptr,
          "delayed fused RCC viewport pair shader translates");
    if(delayedFusedViewportPairD3D8 != nullptr)
    {
        ShaderOutputs d3d8DelayedFusedViewportPairOutputs{};
        Check(ExecuteD3D8Bytecode(delayedFusedViewportPairD3D8, 4 + 6 * 20,
                                  differentialD3D8Constants.data(),
                                  differentialInputs.data(),
                                  d3d8DelayedFusedViewportPairOutputs),
              "translated delayed fused viewport pair bytecode executes independently");
        Check(OutputsEqual(cpuDelayedFusedViewportPairOutputs,
                           d3d8DelayedFusedViewportPairOutputs),
              "CPU and translator preserve delayed fused viewport pair semantics");
        delete[] delayedFusedViewportPairD3D8;
    }

    std::vector<DWORD> overwrittenFusedViewportPairProgram =
        delayedFusedViewportPairProgram;
    const std::array<DWORD, 4> overwriteReciprocal = EncodeNvTestInstruction(
        0, 2, 0, viewportUnusedSource, viewportUnusedSource, vertexPositionSource, 0, 1,
        0x8, 0, 0, 0, false);
    std::copy(overwriteReciprocal.begin(), overwriteReciprocal.end(),
              overwrittenFusedViewportPairProgram.begin() + 9);
    fallbackReason.clear();
    Check(XTL::VshDiagnostics::ClassifyXboxFunction(
              overwrittenFusedViewportPairProgram, fallbackReason) ==
              XTL::VshDiagnostics::XboxFunctionDisposition::ExecuteOnCpu,
          "overwritten fused viewport reciprocal requires CPU fallback");
    Check(fallbackReason == "ambiguous_screen_space_suffix",
          "overwritten fused viewport fallback reason is stable");

    std::vector<DWORD> terminalFusedViewportPairProgram{ 0x00032078u };
    AppendNvTestInstruction(
        terminalFusedViewportPairProgram,
        EncodeNvTestInstruction(1, 0, 0, vertexPositionSource, viewportUnusedSource,
                                viewportUnusedSource, 0, 0, 0, 0xF, 0, 0, false));
    AppendNvTestInstruction(terminalFusedViewportPairProgram, fusedViewportScale);
    AppendNvTestInstruction(
        terminalFusedViewportPairProgram,
        EncodeNvTestInstruction(4, 0, 0, viewportPositionSource, reciprocalWSource,
                                constantSource, 0, 0, 0, 0xE, 0, 0, true, 59));
    ShaderOutputs cpuTerminalFusedViewportPairOutputs{};
    Check(XTL::VshDiagnostics::ExecuteXboxVertexShader(
              terminalFusedViewportPairProgram,
              differentialHardwareConstants.data(), differentialInputs.data(),
              cpuTerminalFusedViewportPairOutputs.position.data(),
              cpuTerminalFusedViewportPairOutputs.colors.data(),
              cpuTerminalFusedViewportPairOutputs.colors.size(),
              cpuTerminalFusedViewportPairOutputs.texCoords.data(),
              cpuTerminalFusedViewportPairOutputs.texCoords.size()),
          "CPU terminal fused viewport pair shader executes");
    Check(PositionsEqual(cpuTerminalFusedViewportPairOutputs, cpuEpilogueOutputs),
          "CPU removes terminal fused RCC c58/c59 viewport pair");
    fallbackReason.clear();
    Check(XTL::VshDiagnostics::ClassifyXboxFunction(
              terminalFusedViewportPairProgram, fallbackReason) ==
              XTL::VshDiagnostics::XboxFunctionDisposition::TranslateToHost,
          "verified terminal viewport pair overrides ambiguous suffix classification");
    Check(fallbackReason.empty(), "terminal fused viewport pair has no fallback reason");
    DWORD* terminalFusedViewportPairD3D8 =
        XTL::EmuVshRecompileXboxFunction(terminalFusedViewportPairProgram.data());
    Check(terminalFusedViewportPairD3D8 != nullptr,
          "terminal fused RCC viewport pair shader translates");
    if(terminalFusedViewportPairD3D8 != nullptr)
    {
        const XTL::VshDiagnostics::ValidationResult terminalFusedValidation =
            XTL::VshDiagnostics::ValidateD3D8Translation(
                terminalFusedViewportPairProgram,
                { terminalFusedViewportPairD3D8, 4 + 4 * 20 });
        Check(terminalFusedValidation.valid,
              "terminal fused RCC viewport pair bytecode validates");
        ShaderOutputs d3d8TerminalFusedViewportPairOutputs{};
        Check(ExecuteD3D8Bytecode(terminalFusedViewportPairD3D8, 4 + 4 * 20,
                                  differentialD3D8Constants.data(),
                                  differentialInputs.data(),
                                  d3d8TerminalFusedViewportPairOutputs),
              "translated terminal fused viewport pair bytecode executes independently");
        Check(PositionsEqual(cpuTerminalFusedViewportPairOutputs,
                             d3d8TerminalFusedViewportPairOutputs),
              "CPU and translator remove terminal fused RCC c58/c59 viewport pair");
        delete[] terminalFusedViewportPairD3D8;
    }

    ShaderOutputs cpuFeedbackOutputs{};
    Check(XTL::VshDiagnostics::ExecuteXboxVertexShader(
              kMidProgramPositionFeedbackProgram, differentialHardwareConstants.data(),
              differentialInputs.data(), cpuFeedbackOutputs.position.data(),
              cpuFeedbackOutputs.colors.data(), cpuFeedbackOutputs.colors.size(),
              cpuFeedbackOutputs.texCoords.data(), cpuFeedbackOutputs.texCoords.size()),
          "CPU mid-program R12 feedback shader executes");
    DWORD* feedbackD3D8 =
        XTL::EmuVshRecompileXboxFunction(kMidProgramPositionFeedbackProgram);
    Check(feedbackD3D8 != nullptr, "mid-program R12 feedback shader translates");
    if(feedbackD3D8 != nullptr)
    {
        ShaderOutputs d3d8FeedbackOutputs{};
        Check(ExecuteD3D8Bytecode(feedbackD3D8, 4 + 4 * 20,
                                  differentialD3D8Constants.data(), differentialInputs.data(),
                                  d3d8FeedbackOutputs),
              "translated mid-program R12 feedback bytecode executes independently");
        bool feedbackMatches = PositionsEqual(cpuFeedbackOutputs, d3d8FeedbackOutputs);
        for(std::size_t component = 0; component < 4; ++component)
        {
            feedbackMatches = feedbackMatches &&
                              NearlyEqual(cpuFeedbackOutputs.colors[component],
                                          d3d8FeedbackOutputs.colors[component]);
        }
        Check(feedbackMatches, "CPU and translator preserve mid-program R12 feedback");
        Check(NearlyEqual(cpuFeedbackOutputs.colors[0], differentialInputs[0] * 8.0f) &&
                  NearlyEqual(cpuFeedbackOutputs.colors[1], differentialInputs[1] * 8.0f) &&
                  NearlyEqual(cpuFeedbackOutputs.colors[2], differentialInputs[2] * 8.0f) &&
                  NearlyEqual(cpuFeedbackOutputs.colors[3], differentialInputs[3] * 8.0f),
              "mid-program position feedback remains observable");
        delete[] feedbackD3D8;
    }

    fallbackReason.clear();
    Check(XTL::VshDiagnostics::RequiresCpuFallback(kAmbiguousScreenSpaceSuffixProgram,
                                                   fallbackReason),
          "ambiguous terminal R12 position feedback requires CPU fallback");
    Check(fallbackReason == "ambiguous_screen_space_suffix",
          "ambiguous screen-space suffix fallback reason is stable");
    DWORD* ambiguousSuffixD3D8 =
        XTL::EmuVshRecompileXboxFunction(kAmbiguousScreenSpaceSuffixProgram);
    Check(ambiguousSuffixD3D8 == nullptr,
          "ambiguous screen-space suffix never emits host bytecode");
    delete[] ambiguousSuffixD3D8;

    differentialInputs[0] = 0.25f;
    differentialInputs[1] = -0.5f;
    differentialInputs[2] = 0.75f;
    differentialInputs[3] = 1.0f;
    differentialInputs[4] = 4.0f;
    differentialInputs[5] = 5.0f;
    ShaderOutputs cpuPartialOutputs{};
    Check(XTL::VshDiagnostics::ExecuteXboxVertexShader(
              kPartialPositionProgram, differentialHardwareConstants.data(), differentialInputs.data(),
              cpuPartialOutputs.position.data(), cpuPartialOutputs.colors.data(),
              cpuPartialOutputs.colors.size(), cpuPartialOutputs.texCoords.data(),
              cpuPartialOutputs.texCoords.size()),
          "CPU partial-position shader executes");
    DWORD* partialPositionD3D8 = XTL::EmuVshRecompileXboxFunction(kPartialPositionProgram);
    Check(partialPositionD3D8 != nullptr, "partial-position shader translates");
    if(partialPositionD3D8 != nullptr)
    {
        ShaderOutputs d3d8PartialOutputs{};
        Check(ExecuteD3D8Bytecode(partialPositionD3D8, 4 + 1 * 20,
                                  differentialD3D8Constants.data(), differentialInputs.data(),
                                  d3d8PartialOutputs),
              "translated partial-position bytecode executes independently");
        Check(PositionsEqual(cpuPartialOutputs, d3d8PartialOutputs),
              "CPU and translator preserve the same partial position components");
        Check(NearlyEqual(cpuPartialOutputs.position[0], differentialInputs[4]) &&
                  NearlyEqual(cpuPartialOutputs.position[1], differentialInputs[5]) &&
                  NearlyEqual(cpuPartialOutputs.position[2], differentialInputs[2]) &&
                  NearlyEqual(cpuPartialOutputs.position[3], differentialInputs[3]),
              "partial position inherits unwritten Z/W from v0");
        delete[] partialPositionD3D8;
    }

    DWORD translatedDeclaration[8] = {};
    const XTL::VshDiagnostics::DeclarationTranslationResult declarationResult =
        XTL::VshDiagnostics::TranslateXboxDeclaration(kXboxDeclaration,
                                                      translatedDeclaration);
    Check(declarationResult.disposition ==
              XTL::VshDiagnostics::XboxFunctionDisposition::TranslateToHost,
          "host-compatible declaration is classified for translation");
    Check(declarationResult.reason.empty(), "translated declaration has no fallback reason");
    const int declarationTokens = XTL::EmuVshTranslateXboxDeclaration(
        kXboxDeclaration, translatedDeclaration, static_cast<int>(std::size(translatedDeclaration)));
    Check(declarationTokens == 3, "declaration token count");
    Check(translatedDeclaration[1] == 0x40020000u, "PC declaration token remains stable");

    const std::array<DWORD, 15> hostVertexTypes = {
        0x00,
        0x01,
        0x02,
        0x03,
        0x04,
        0x05,
        0x06,
        0x07,
        0x12,
        0x22,
        0x32,
        0x42,
        0x40,
        0x25,
        0x45,
    };
    for(const DWORD type : hostVertexTypes)
    {
        const DWORD declaration[] = {
            0x20000000u,
            0x40000000u | (type << 16),
            0xFFFFFFFFu,
        };
        DWORD output[3] = {};
        const XTL::VshDiagnostics::DeclarationTranslationResult typeResult =
            XTL::VshDiagnostics::TranslateXboxDeclaration(declaration, output);
        Check(typeResult.disposition ==
                  XTL::VshDiagnostics::XboxFunctionDisposition::TranslateToHost,
              "host vertex declaration type translates");
        Check(typeResult.tokenCount == 3, "host vertex declaration retains token count");
    }

    const std::array<DWORD, 12> cpuOnlyVertexTypes = {
        0x11,
        0x14,
        0x15,
        0x16,
        0x21,
        0x24,
        0x31,
        0x34,
        0x35,
        0x41,
        0x44,
        0x72,
    };
    for(const DWORD type : cpuOnlyVertexTypes)
    {
        const DWORD declaration[] = {
            0x20000000u,
            0x40000000u | (type << 16),
            0xFFFFFFFFu,
        };
        DWORD output[3] = {};
        const XTL::VshDiagnostics::DeclarationTranslationResult typeResult =
            XTL::VshDiagnostics::TranslateXboxDeclaration(declaration, output);
        Check(typeResult.disposition ==
                  XTL::VshDiagnostics::XboxFunctionDisposition::ExecuteOnCpu,
              "Xbox-only vertex declaration type selects CPU execution");
        Check(typeResult.reason == "declaration_cpu_vertex_type",
              "CPU-only declaration reason is stable");
    }

    const DWORD constantPayloadDeclaration[] = {
        0x82000000u,
        0xFFFFFFFFu,
        0x3F800000u,
        0x40000000u,
        0x40400000u,
        0xFFFFFFFFu,
    };
    DWORD translatedConstantPayload[6] = {};
    const XTL::VshDiagnostics::DeclarationTranslationResult constantPayloadResult =
        XTL::VshDiagnostics::TranslateXboxDeclaration(
            constantPayloadDeclaration, translatedConstantPayload);
    Check(constantPayloadResult.disposition ==
                  XTL::VshDiagnostics::XboxFunctionDisposition::TranslateToHost &&
              constantPayloadResult.tokenCount == std::size(constantPayloadDeclaration),
          "constant payload data cannot terminate declaration parsing");
    const std::vector<std::string> constantPayloadListing =
        XTL::VshDiagnostics::DecodeVertexDeclaration(constantPayloadDeclaration);
    Check(constantPayloadListing.size() == std::size(constantPayloadDeclaration),
          "declaration diagnostic listing retains every payload token");
    Check(constantPayloadListing.size() > 1 &&
              constantPayloadListing[1].find(
                  "raw=0xFFFFFFFF type=payload owner=const element=0") !=
                  std::string::npos,
          "declaration diagnostic treats all-ones constant data as payload");
    Check(constantPayloadListing.size() > 5 &&
              constantPayloadListing[5].find("type=end") != std::string::npos,
          "declaration diagnostic recognizes only the top-level END token");
    std::array<float, 192 * 4> baseDeclarationConstants{};
    std::array<float, 192 * 4> appliedDeclarationConstants{};
    baseDeclarationConstants[(96 + 1) * 4] = 9.0f;
    Check(XTL::VshDiagnostics::ApplyXboxDeclarationConstants(
              constantPayloadDeclaration, baseDeclarationConstants.data(),
              appliedDeclarationConstants.data(), appliedDeclarationConstants.size()),
          "embedded declaration constants apply to CPU constant snapshot");
    DWORD appliedFirstConstantBits = 0;
    std::memcpy(&appliedFirstConstantBits, &appliedDeclarationConstants[96 * 4], sizeof(DWORD));
    Check(appliedFirstConstantBits == 0xFFFFFFFFu &&
              appliedDeclarationConstants[96 * 4 + 1] == 1.0f &&
              appliedDeclarationConstants[96 * 4 + 2] == 2.0f &&
              appliedDeclarationConstants[96 * 4 + 3] == 3.0f &&
              appliedDeclarationConstants[(96 + 1) * 4] == 9.0f,
          "embedded constants override only their declared hardware range");

    const DWORD constantThenStreamDeclaration[] = {
        0x82000000u,
        0x3F800000u,
        0x40000000u,
        0x40400000u,
        0x40800000u,
        0x20000000u,
        0x40120000u,
        0xFFFFFFFFu,
    };
    const float constantThenStreamValue = 11.0f;
    const XTL::VshDiagnostics::VertexStreamView constantThenStream = {
        &constantThenStreamValue,
        sizeof(constantThenStreamValue),
        sizeof(constantThenStreamValue),
    };
    Check(XTL::VshDiagnostics::DecodeXboxVertex(
              constantThenStreamDeclaration, &constantThenStream, 1, 0,
              cpuInputs.data(), cpuInputs.size()) &&
              cpuInputs[0] == constantThenStreamValue,
          "CPU vertex decoding skips embedded constant payload tokens");

    const DWORD hostTessellatorDeclaration[] = { 0x60020000u, 0xFFFFFFFFu };
    DWORD tessellatorOutput[4] = {};
    const XTL::VshDiagnostics::DeclarationTranslationResult tessellatorResult =
        XTL::VshDiagnostics::TranslateXboxDeclaration(
            hostTessellatorDeclaration, tessellatorOutput);
    Check(tessellatorResult.disposition ==
                  XTL::VshDiagnostics::XboxFunctionDisposition::TranslateToHost &&
              !tessellatorResult.cpuCompatible &&
              tessellatorResult.cpuIncompatibilityReason == "declaration_cpu_tessellator",
          "host tessellator declaration records CPU incompatibility");

    const DWORD cpuTessellatorDeclaration[] = {
        0x20000000u,
        0x40350000u,
        0x60020000u,
        0xFFFFFFFFu,
    };
    const XTL::VshDiagnostics::DeclarationTranslationResult cpuTessellatorResult =
        XTL::VshDiagnostics::TranslateXboxDeclaration(
            cpuTessellatorDeclaration, tessellatorOutput);
    Check(cpuTessellatorResult.disposition ==
                  XTL::VshDiagnostics::XboxFunctionDisposition::Reject &&
              cpuTessellatorResult.reason == "declaration_cpu_tessellator",
          "CPU-only declaration with tessellation is rejected");

    const DWORD extensionDeclaration[] = {
        0xA1000000u,
        0x12345678u,
        0xFFFFFFFFu,
    };
    const XTL::VshDiagnostics::DeclarationTranslationResult extensionResult =
        XTL::VshDiagnostics::TranslateXboxDeclaration(
            extensionDeclaration, tessellatorOutput);
    Check(extensionResult.disposition ==
                  XTL::VshDiagnostics::XboxFunctionDisposition::TranslateToHost &&
              !extensionResult.cpuCompatible &&
              extensionResult.cpuIncompatibilityReason == "declaration_cpu_extension",
          "host extension declaration records CPU incompatibility");

    const DWORD invalidConstantRangeDeclaration[] = {
        0x8400005Fu,
        0xFFFFFFFFu,
    };
    XTL::VshDiagnostics::DeclarationTranslationResult invalidConstantResult =
        XTL::VshDiagnostics::TranslateXboxDeclaration(
            invalidConstantRangeDeclaration, tessellatorOutput);
    Check(invalidConstantResult.disposition ==
                  XTL::VshDiagnostics::XboxFunctionDisposition::Reject &&
              invalidConstantResult.reason == "declaration_constant_range",
          "embedded declaration constant range is validated");

    const DWORD unsupportedTypeDeclaration[] = {
        0x20000000u,
        0x40550000u,
        0xFFFFFFFFu,
    };
    DWORD rejectedDeclaration[8] = {};
    XTL::VshDiagnostics::DeclarationTranslationResult rejectedDeclarationResult =
        XTL::VshDiagnostics::TranslateXboxDeclaration(
            unsupportedTypeDeclaration, rejectedDeclaration);
    Check(rejectedDeclarationResult.disposition ==
                  XTL::VshDiagnostics::XboxFunctionDisposition::Reject &&
              rejectedDeclarationResult.reason == "unsupported_vertex_type",
          "unknown vertex declaration type is rejected");

    const DWORD invalidRegisterDeclaration[] = {
        0x20000000u,
        0x40000010u,
        0xFFFFFFFFu,
    };
    rejectedDeclarationResult = XTL::VshDiagnostics::TranslateXboxDeclaration(
        invalidRegisterDeclaration, rejectedDeclaration);
    Check(rejectedDeclarationResult.disposition ==
                  XTL::VshDiagnostics::XboxFunctionDisposition::Reject &&
              rejectedDeclarationResult.reason == "declaration_register_range",
          "out-of-range declaration register is rejected");

    const DWORD malformedDeclaration[] = { 0xC0000000u, 0xFFFFFFFFu };
    rejectedDeclarationResult = XTL::VshDiagnostics::TranslateXboxDeclaration(
        malformedDeclaration, rejectedDeclaration);
    Check(rejectedDeclarationResult.disposition ==
                  XTL::VshDiagnostics::XboxFunctionDisposition::Reject &&
              rejectedDeclarationResult.reason == "malformed_declaration_token",
          "reserved declaration token is rejected");

    std::array<DWORD, 128> unterminatedDeclaration{};
    rejectedDeclarationResult = XTL::VshDiagnostics::TranslateXboxDeclaration(
        unterminatedDeclaration, rejectedDeclaration);
    Check(rejectedDeclarationResult.disposition ==
                  XTL::VshDiagnostics::XboxFunctionDisposition::Reject &&
              rejectedDeclarationResult.reason == "declaration_capacity",
          "declaration exceeding output capacity is rejected");
    std::array<DWORD, 128> unterminatedOutput{};
    rejectedDeclarationResult = XTL::VshDiagnostics::TranslateXboxDeclaration(
        unterminatedDeclaration, unterminatedOutput);
    Check(rejectedDeclarationResult.disposition ==
                  XTL::VshDiagnostics::XboxFunctionDisposition::Reject &&
              rejectedDeclarationResult.reason == "declaration_missing_end",
          "unterminated declaration is rejected");

    rejectedDeclarationResult = XTL::VshDiagnostics::TranslateXboxDeclaration(
        kXboxDeclaration, { rejectedDeclaration, 2 });
    Check(rejectedDeclarationResult.disposition ==
                  XTL::VshDiagnostics::XboxFunctionDisposition::Reject &&
              rejectedDeclarationResult.reason == "declaration_capacity" &&
              XTL::EmuVshTranslateXboxDeclaration(kXboxDeclaration, rejectedDeclaration, 2) == 0,
          "declaration capacity failure is not truncated into success");

    std::FILE* capture = nullptr;
#if defined(_WIN32)
    const errno_t captureError = ::tmpfile_s(&capture);
    Check(captureError == 0, "diagnostic capture file result");
#else
    capture = std::tmpfile();
#endif
    Check(capture != nullptr, "diagnostic capture file");
    if(capture != nullptr)
    {
        const XTL::VshDiagnostics::TranslationCapture translationCapture = {
            kXboxProgram,
            translation.tokens,
            kXboxDeclaration,
            translatedDeclaration,
        };
        XTL::VshDiagnostics::DumpRejectedTranslation(capture, translationCapture);
        const int seekResult = std::fseek(capture, 0, SEEK_SET);
        Check(seekResult == 0, "diagnostic capture seek");
        if(seekResult == 0)
        {
            char output[256] = {};
            const std::size_t bytesRead = std::fread(output, 1, sizeof(output) - 1, capture);
            Check(std::ferror(capture) == 0, "diagnostic capture read");
            output[bytesRead] = '\0';
            Check(std::strstr(output, "VSH| rejected hash=") != nullptr, "rejection capture header");
        }
        std::fclose(capture);
    }

    {
        std::FILE* rejectedCapture = nullptr;
#if defined(_WIN32)
        const errno_t rejectedCaptureError = ::tmpfile_s(&rejectedCapture);
        Check(rejectedCaptureError == 0, "pre-translation diagnostic capture file result");
#else
        rejectedCapture = std::tmpfile();
#endif
        Check(rejectedCapture != nullptr, "pre-translation diagnostic capture file");
        if(rejectedCapture != nullptr)
        {
            const XTL::VshDiagnostics::TranslationCapture translationCapture = {
                reservedMacProgram,
                {},
                constantPayloadDeclaration,
                {},
                "unsupported_mac_opcode",
            };
            XTL::VshDiagnostics::DumpRejectedTranslation(rejectedCapture, translationCapture);
            XTL::VshDiagnostics::DumpReplayCapture(rejectedCapture, translationCapture);
            const GeneratedSequenceProgram replaySequence = GenerateSequenceProgram(1);
            const XTL::VshDiagnostics::TranslationCapture validReplayCapture = {
                replaySequence.program,
                {},
                kXboxDeclaration,
                translatedDeclaration,
                "round_trip",
                opcodeHardwareConstants.data(),
                opcodeHardwareConstants.size(),
                opcodeInputs.data(),
                opcodeInputs.size(),
                "host_test",
            };
            XTL::VshDiagnostics::DumpReplayCapture(rejectedCapture, validReplayCapture);
            const char* replayPath = "host_vsh_replay_smoke.tmp";
            std::FILE* replayFile = nullptr;
#if defined(_WIN32)
            const errno_t replayFileError = ::fopen_s(&replayFile, replayPath, "w");
            Check(replayFileError == 0, "replay smoke file result");
#else
            replayFile = std::fopen(replayPath, "w");
#endif
            Check(replayFile != nullptr, "replay smoke file");
            if(replayFile != nullptr)
            {
                XTL::VshDiagnostics::DumpReplayCapture(replayFile, validReplayCapture);
                Check(std::fclose(replayFile) == 0, "replay smoke file close");
                Check(ReplayCaptureFile(replayPath) == 0,
                      "replay file mode executes a captured shader");
                Check(std::remove(replayPath) == 0, "replay smoke file cleanup");
            }
            const int seekResult = std::fseek(rejectedCapture, 0, SEEK_SET);
            Check(seekResult == 0, "pre-translation diagnostic capture seek");
            if(seekResult == 0)
            {
                std::array<char, 32768> output{};
                const std::size_t bytesRead =
                    std::fread(output.data(), 1, output.size() - 1, rejectedCapture);
                Check(std::ferror(rejectedCapture) == 0,
                      "pre-translation diagnostic capture read");
                output[bytesRead] = '\0';
                Check(std::strstr(output.data(), "d3d8=unavailable validation=unavailable") !=
                          nullptr,
                      "pre-translation capture permits missing generated bytecode");
                Check(std::strstr(output.data(), "reason=unsupported_mac_opcode") != nullptr,
                      "pre-translation capture retains classification reason");
                Check(std::strstr(output.data(), "VSH| nv2a hash=") != nullptr,
                      "pre-translation capture lists decoded NV2A instructions");
                Check(std::strstr(
                          output.data(),
                          "raw=0xFFFFFFFF type=payload owner=const element=0") != nullptr,
                      "pre-translation capture lists all-ones declaration payload");
                const char* generatedDeclaration =
                    std::strstr(output.data(), "VSH| declaration_d3d8 hash=");
                Check(generatedDeclaration != nullptr &&
                          std::strstr(generatedDeclaration, "unavailable") != nullptr,
                      "pre-translation capture marks generated declaration unavailable");
                const char* rejectedReplay = std::strstr(output.data(), "VSHREPLAY| ");
                Check(rejectedReplay != nullptr, "pre-translation capture emits replay record");
                if(rejectedReplay != nullptr)
                {
                    const char* rejectedReplayEnd = std::strchr(rejectedReplay, '\n');
                    ReplayCaptureRecord rejectedRecord;
                    std::string replayError;
                    const std::string rejectedReplayLine =
                        rejectedReplayEnd == nullptr
                            ? std::string{}
                            : std::string(rejectedReplay, rejectedReplayEnd);
                    const bool rejectedParsed =
                        rejectedReplayEnd != nullptr &&
                        ParseReplayCaptureLine(rejectedReplayLine, rejectedRecord, replayError);
                    Check(rejectedParsed, "rejected replay record parses");
                    if(rejectedParsed)
                    {
                        Check(rejectedRecord.hash ==
                                      XTL::VshDiagnostics::HashXboxFunction(
                                          reservedMacProgram) &&
                                  rejectedRecord.declaration.size() ==
                                      std::size(constantPayloadDeclaration) &&
                                  rejectedRecord.inputSource == "canonical",
                              "rejected replay record preserves shader, declaration, and input source");
                        Check(ReplayCapture(rejectedRecord) == 0,
                              "rejected replay reproduces classification");
                        std::string corruptedReplay = rejectedReplayLine;
                        const std::size_t hashPosition = corruptedReplay.find(" hash=");
                        if(hashPosition != std::string::npos)
                        {
                            corruptedReplay[hashPosition + 6] =
                                corruptedReplay[hashPosition + 6] == '0' ? '1' : '0';
                        }
                        ReplayCaptureRecord corruptedRecord;
                        replayError.clear();
                        Check(!ParseReplayCaptureLine(corruptedReplay, corruptedRecord,
                                                      replayError),
                              "replay parser rejects a corrupted stable hash");
                    }

                    const char* validReplay =
                        rejectedReplayEnd == nullptr
                            ? nullptr
                            : std::strstr(rejectedReplayEnd + 1, "VSHREPLAY| ");
                    const char* validReplayEnd =
                        validReplay == nullptr ? nullptr : std::strchr(validReplay, '\n');
                    ReplayCaptureRecord validRecord;
                    replayError.clear();
                    const bool validParsed =
                        validReplay != nullptr && validReplayEnd != nullptr &&
                        ParseReplayCaptureLine(std::string(validReplay, validReplayEnd),
                                               validRecord, replayError);
                    Check(validParsed, "translated replay record parses");
                    if(validParsed)
                    {
                        Check(validRecord.inputSource == "host_test" &&
                                  ReplayCapture(validRecord) == 0,
                              "translated replay executes classification, validation, and semantics");
                    }
                }
            }
            std::fclose(rejectedCapture);
        }
    }

    if(g_failures == 0)
    {
        std::puts("PASS host vertex-shader recompiler");
    }
    else
    {
        std::printf("INFO hash=0x%08X\n", hash);
    }
    return g_failures;
}

int main(int argc, char** argv)
{
    try
    {
        if(argc == 3 && std::strcmp(argv[1], "--replay") == 0)
        {
            return ReplayCaptureFile(argv[2]);
        }
        if(argc != 1)
        {
            std::fputs("usage: host_vsh_recompiler_test [--replay <capture>]\n", stderr);
            return 2;
        }
        return RunTests();
    }
    catch(const std::exception& exception)
    {
        std::fprintf(stderr, "FAIL unexpected exception: %s\n", exception.what());
        return 1;
    }
    catch(...)
    {
        std::fputs("FAIL unexpected non-standard exception\n", stderr);
        return 1;
    }
}
