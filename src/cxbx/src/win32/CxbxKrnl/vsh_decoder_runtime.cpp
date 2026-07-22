// CXBX runtime diagnostics adapter for the vertex-shader translator.

#include "core/VertexShaderTranslator.h"

#define _XBOXKRNL_LOCAL_

namespace xboxkrnl
{
#include <xboxkrnl/xboxkrnl.h>
};

#include "emulation_runtime.h"
#include "EmuVshDecoderInternal.h"

DWORD* XTL::EmuVshRecompileXboxFunction(const DWORD* xboxFunction)
{
    return VshInternal::RecompileXboxFunction(xboxFunction, EmuWarning);
}
