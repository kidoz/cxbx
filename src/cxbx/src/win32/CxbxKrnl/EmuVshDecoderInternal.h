// Private vertex-shader translation entry points shared by runtime adapters.

#pragma once

#include "core/VertexShaderTranslator.h"
#include "EmuVshDecoder.h"

namespace XTL::VshInternal
{
// Copies the owned fixed-width translation into the historical delete[] API.
DWORD* RecompileXboxFunction(const DWORD* xboxFunction,
                             VshDiagnostics::DiagnosticSink diagnosticSink);
} // namespace XTL::VshInternal
