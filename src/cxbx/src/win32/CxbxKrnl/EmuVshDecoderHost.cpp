// Host-test diagnostics adapter for the vertex-shader translator.

#include "core/VertexShaderTranslator.h"
#include "EmuVshDecoderInternal.h"

#include <cstdarg>
#include <cstdio>

namespace
{
void ReportHostWarning(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    std::fputc('\n', stderr);
    va_end(arguments);
}
} // namespace

DWORD* XTL::EmuVshRecompileXboxFunction(const DWORD* xboxFunction)
{
    return VshInternal::RecompileXboxFunction(xboxFunction, ReportHostWarning);
}
