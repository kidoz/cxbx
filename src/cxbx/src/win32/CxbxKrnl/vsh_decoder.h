// Legacy Win32/Xbox compatibility entry points for the vertex-shader translator.

#pragma once

#include <cstdint>

#if defined(CXBX_VSH_HOST_TEST)
using DWORD = std::uint32_t;
#endif

namespace XTL
{
DWORD* EmuVshRecompileXboxFunction(const DWORD* xboxFunction);
int EmuVshTranslateXboxDeclaration(const DWORD* xboxDeclaration, DWORD* pcDeclaration, int maxTokens);
} // namespace XTL
