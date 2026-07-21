// Legacy allocation adapter for the vertex-shader translator.

#include "core/VertexShaderTranslator.h"

#if !defined(CXBX_VSH_HOST_TEST)
using DWORD = unsigned long;
static_assert(sizeof(DWORD) == sizeof(std::uint32_t));
#endif

#include "EmuVshDecoderInternal.h"

#include <algorithm>
#include <memory>

DWORD* XTL::VshInternal::RecompileXboxFunction(
    const DWORD* xboxFunction, VshDiagnostics::DiagnosticSink diagnosticSink)
{
    const VshDiagnostics::FunctionTranslationResult translation =
        VshDiagnostics::TranslateXboxFunction(xboxFunction, diagnosticSink);
    if(translation.tokens.empty())
    {
        return nullptr;
    }

    auto legacyFunction = std::make_unique<DWORD[]>(translation.tokens.size());
    std::copy(translation.tokens.begin(), translation.tokens.end(), legacyFunction.get());
    return legacyFunction.release();
}

int XTL::EmuVshTranslateXboxDeclaration(const DWORD* xboxDeclaration, DWORD* pcDeclaration,
                                        int maxTokens)
{
    if(maxTokens <= 0)
    {
        return 0;
    }

    const VshDiagnostics::DeclarationTranslationResult result =
        VshDiagnostics::TranslateXboxDeclaration(xboxDeclaration, pcDeclaration,
                                                 static_cast<std::size_t>(maxTokens));
    return result.disposition == VshDiagnostics::XboxFunctionDisposition::Reject
               ? 0
               : static_cast<int>(result.tokenCount);
}
