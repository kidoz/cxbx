// Legacy allocation adapter for the vertex-shader translator.

#include "core/VertexShaderTranslator.h"

#if !defined(CXBX_VSH_HOST_TEST)
using DWORD = unsigned long;
static_assert(sizeof(DWORD) == sizeof(std::uint32_t));
#endif

#include "vsh_decoder_internal.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace
{
constexpr std::size_t MaxXboxInstructionCount = 136;
constexpr std::size_t MaxDeclarationTokenCount = 128;
constexpr std::uint32_t XboxFunctionVersion = 0x2078;

std::uint32_t CopyWord(DWORD word)
{
    return static_cast<std::uint32_t>(word);
}
} // namespace

std::vector<std::uint32_t>
XTL::VshInternal::CopyXboxFunction(const DWORD* xboxFunction)
{
    if(xboxFunction == nullptr)
    {
        return {};
    }

    const std::uint32_t versionToken = CopyWord(xboxFunction[0]);
    std::vector<std::uint32_t> function{ versionToken };
    if((versionToken & 0xFFFFu) != XboxFunctionVersion)
    {
        return function;
    }

    const std::uint32_t encodedCount = versionToken >> 16;
    const std::size_t instructionCount =
        encodedCount == 0 || encodedCount > MaxXboxInstructionCount
            ? MaxXboxInstructionCount
            : static_cast<std::size_t>(encodedCount);
    function.reserve(1 + instructionCount * 4);
    for(std::size_t instruction = 0; instruction < instructionCount; ++instruction)
    {
        for(std::size_t word = 0; word < 4; ++word)
        {
            function.push_back(CopyWord(xboxFunction[1 + instruction * 4 + word]));
        }
        if((encodedCount == 0 || encodedCount > MaxXboxInstructionCount) &&
           (function.back() & 1u) != 0)
        {
            break;
        }
    }
    return function;
}

std::vector<std::uint32_t>
XTL::VshInternal::CopyXboxDeclaration(const DWORD* xboxDeclaration)
{
    if(xboxDeclaration == nullptr)
    {
        return {};
    }

    std::vector<std::uint32_t> declaration;
    declaration.reserve(MaxDeclarationTokenCount);
    for(std::size_t tokenIndex = 0; tokenIndex < MaxDeclarationTokenCount;)
    {
        const std::uint32_t token = CopyWord(xboxDeclaration[tokenIndex++]);
        declaration.push_back(token);
        if(token == 0xFFFFFFFFu)
        {
            break;
        }

        const std::uint32_t tokenType = token >> 29;
        if(tokenType >= 6)
        {
            break;
        }
        std::size_t payloadCount = 0;
        if(tokenType == 4)
        {
            payloadCount = static_cast<std::size_t>((token >> 25) & 0xFu) * 4;
        }
        else if(tokenType == 5)
        {
            payloadCount = (token >> 24) & 0x1Fu;
        }
        if(payloadCount > MaxDeclarationTokenCount - tokenIndex)
        {
            break;
        }
        for(std::size_t payloadIndex = 0; payloadIndex < payloadCount;
            ++payloadIndex, ++tokenIndex)
        {
            declaration.push_back(CopyWord(xboxDeclaration[tokenIndex]));
        }
    }
    return declaration;
}

DWORD* XTL::VshInternal::RecompileXboxFunction(
    const DWORD* xboxFunction, VshDiagnostics::DiagnosticSink diagnosticSink)
{
    const std::vector<std::uint32_t> fixedFunction = CopyXboxFunction(xboxFunction);
    const VshDiagnostics::FunctionTranslationResult translation =
        VshDiagnostics::TranslateXboxFunction(fixedFunction, diagnosticSink);
    if(translation.tokens.empty())
    {
        return nullptr;
    }

    auto legacyFunction = std::make_unique<DWORD[]>(translation.tokens.size());
    std::transform(translation.tokens.begin(), translation.tokens.end(), legacyFunction.get(),
                   [](std::uint32_t word)
                   { return static_cast<DWORD>(word); });
    return legacyFunction.release();
}

int XTL::EmuVshTranslateXboxDeclaration(const DWORD* xboxDeclaration, DWORD* pcDeclaration,
                                        int maxTokens)
{
    if(xboxDeclaration == nullptr || pcDeclaration == nullptr || maxTokens <= 0)
    {
        return 0;
    }

    const std::vector<std::uint32_t> fixedDeclaration =
        VshInternal::CopyXboxDeclaration(xboxDeclaration);
    std::vector<std::uint32_t> translatedDeclaration(static_cast<std::size_t>(maxTokens));
    const VshDiagnostics::DeclarationTranslationResult result =
        VshDiagnostics::TranslateXboxDeclaration(fixedDeclaration, translatedDeclaration);
    if(result.disposition == VshDiagnostics::XboxFunctionDisposition::Reject)
    {
        return 0;
    }
    for(std::size_t tokenIndex = 0; tokenIndex < result.tokenCount; ++tokenIndex)
    {
        pcDeclaration[tokenIndex] = static_cast<DWORD>(translatedDeclaration[tokenIndex]);
    }
    return static_cast<int>(result.tokenCount);
}
