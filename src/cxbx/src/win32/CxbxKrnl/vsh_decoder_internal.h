// Private vertex-shader translation entry points shared by runtime adapters.

#pragma once

#include "core/VertexShaderTranslator.h"
#include "vsh_decoder.h"

namespace XTL::VshInternal
{
std::vector<std::uint32_t> CopyXboxFunction(const DWORD* xboxFunction);
std::vector<std::uint32_t> CopyXboxDeclaration(const DWORD* xboxDeclaration);

// Copies the owned fixed-width translation into the historical delete[] API.
DWORD* RecompileXboxFunction(const DWORD* xboxFunction,
                             VshDiagnostics::DiagnosticSink diagnosticSink);
} // namespace XTL::VshInternal
