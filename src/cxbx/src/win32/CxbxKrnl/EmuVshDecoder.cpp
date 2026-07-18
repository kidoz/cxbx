// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;;
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbxkrnl->EmuVshDecoder.cpp
// *
// *  Xbox vertex-shader recompiler: decodes the 128-bit NV2A vertex-
// *  program microcode an XDK title passes to D3DDevice_CreateVertexShader
// *  (version token low word 0x2078) and re-emits it as host D3D8 vs.1.1
// *  bytecode, plus translates the Xbox vertex declaration's extended type
// *  codes to their PC equivalents. The microcode field layout follows the
// *  NV2A encoder in nxdk's vp20compiler (tools/vp20compiler/main.c), which
// *  is the same format the XDK shader assembler produces.
// *
// ******************************************************************

#define _CXBXKRNL_INTERNAL
#define _XBOXKRNL_LOCAL_

#if !defined(CXBX_VSH_HOST_TEST)
// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace xboxkrnl
{
#include <xboxkrnl/xboxkrnl.h>
};

#include "Emu.h"

#undef FIELD_OFFSET // prevent macro redefinition warnings
#include <windows.h>
#else
#include <cstdarg>
#include <cstdio>

#define FALSE 0

static void EmuWarning(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    std::fputc('\n', stderr);
    va_end(arguments);
}
#endif

#include "EmuVshDecoder.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

namespace XTL
{
DWORD* EmuVshRecompileXboxFunction(const DWORD* xboxFunction);
int EmuVshTranslateXboxDeclaration(const DWORD* xboxDeclaration, DWORD* pcDeclaration, int maxTokens);
} // namespace XTL

// ******************************************************************
// * NV2A microcode field layout (from nxdk vp20compiler main.c)
// ******************************************************************

enum VshField
{
    FLD_ILU = 0, FLD_MAC, FLD_CONST, FLD_V,
    FLD_A_NEG, FLD_A_SWZ_X, FLD_A_SWZ_Y, FLD_A_SWZ_Z, FLD_A_SWZ_W, FLD_A_R, FLD_A_MUX,
    FLD_B_NEG, FLD_B_SWZ_X, FLD_B_SWZ_Y, FLD_B_SWZ_Z, FLD_B_SWZ_W, FLD_B_R, FLD_B_MUX,
    FLD_C_NEG, FLD_C_SWZ_X, FLD_C_SWZ_Y, FLD_C_SWZ_Z, FLD_C_SWZ_W, FLD_C_R_HIGH, FLD_C_R_LOW, FLD_C_MUX,
    FLD_OUT_MAC_MASK, FLD_OUT_R, FLD_OUT_ILU_MASK, FLD_OUT_O_MASK, FLD_OUT_ORB, FLD_OUT_ADDRESS, FLD_OUT_MUX,
    FLD_A0X, FLD_FINAL,
    FLD__COUNT
};

struct VshFieldMapping { unsigned char SubToken, StartBit, BitLength; };

static const VshFieldMapping g_VshFields[FLD__COUNT] =
{
    { 1, 25, 3 },   // FLD_ILU
    { 1, 21, 4 },   // FLD_MAC
    { 1, 13, 8 },   // FLD_CONST
    { 1,  9, 4 },   // FLD_V
    { 1,  8, 1 },   // FLD_A_NEG
    { 1,  6, 2 },   // FLD_A_SWZ_X
    { 1,  4, 2 },   // FLD_A_SWZ_Y
    { 1,  2, 2 },   // FLD_A_SWZ_Z
    { 1,  0, 2 },   // FLD_A_SWZ_W
    { 2, 28, 4 },   // FLD_A_R
    { 2, 26, 2 },   // FLD_A_MUX
    { 2, 25, 1 },   // FLD_B_NEG
    { 2, 23, 2 },   // FLD_B_SWZ_X
    { 2, 21, 2 },   // FLD_B_SWZ_Y
    { 2, 19, 2 },   // FLD_B_SWZ_Z
    { 2, 17, 2 },   // FLD_B_SWZ_W
    { 2, 13, 4 },   // FLD_B_R
    { 2, 11, 2 },   // FLD_B_MUX
    { 2, 10, 1 },   // FLD_C_NEG
    { 2,  8, 2 },   // FLD_C_SWZ_X
    { 2,  6, 2 },   // FLD_C_SWZ_Y
    { 2,  4, 2 },   // FLD_C_SWZ_Z
    { 2,  2, 2 },   // FLD_C_SWZ_W
    { 2,  0, 2 },   // FLD_C_R_HIGH
    { 3, 30, 2 },   // FLD_C_R_LOW
    { 3, 28, 2 },   // FLD_C_MUX
    { 3, 24, 4 },   // FLD_OUT_MAC_MASK
    { 3, 20, 4 },   // FLD_OUT_R
    { 3, 16, 4 },   // FLD_OUT_ILU_MASK
    { 3, 12, 4 },   // FLD_OUT_O_MASK
    { 3, 11, 1 },   // FLD_OUT_ORB
    { 3,  3, 8 },   // FLD_OUT_ADDRESS
    { 3,  2, 1 },   // FLD_OUT_MUX
    { 3,  1, 1 },   // FLD_A0X
    { 3,  0, 1 }    // FLD_FINAL
};

static DWORD VshGetField(const DWORD *Instr, VshField Field)
{
    const VshFieldMapping &f = g_VshFields[Field];
    return (Instr[f.SubToken] >> f.StartBit) & ((1u << f.BitLength) - 1);
}

// MAC / ILU opcodes (vp20compiler enums)
enum { MAC_NOP = 0, MAC_MOV, MAC_MUL, MAC_ADD, MAC_MAD, MAC_DP3, MAC_DPH, MAC_DP4,
       MAC_DST, MAC_MIN, MAC_MAX, MAC_SLT, MAC_SGE, MAC_ARL };
enum { ILU_NOP = 0, ILU_MOV, ILU_RCP, ILU_RCC, ILU_RSQ, ILU_EXP, ILU_LOG, ILU_LIT };
enum { PARAM_R = 1, PARAM_V = 2, PARAM_C = 3 };
enum { OUTPUT_C = 0, OUTPUT_O = 1 };
enum { OMUX_MAC = 0, OMUX_ILU = 1 };

// ******************************************************************
// * vs.1.1 token emission
// ******************************************************************

// D3DSIO opcodes (d3d8types.h)
enum { SIO_NOP = 0, SIO_MOV = 1, SIO_ADD = 2, SIO_MAD = 4, SIO_MUL = 5, SIO_RCP = 6,
       SIO_RSQ = 7, SIO_DP3 = 8, SIO_DP4 = 9, SIO_MIN = 10, SIO_MAX = 11, SIO_SLT = 12,
       SIO_SGE = 13, SIO_EXP = 14, SIO_LOG = 15, SIO_LIT = 16, SIO_DST = 17 };

// D3DSPR register types (shifted into bits 28-30 of parameter tokens)
enum { SPR_TEMP = 0, SPR_INPUT = 1, SPR_CONST = 2, SPR_ADDR = 3,
       SPR_RASTOUT = 4, SPR_ATTROUT = 5, SPR_TEXCRDOUT = 6 };

static DWORD VshDstToken(DWORD RegType, DWORD RegNum, DWORD PcMask)
{
    return 0x80000000 | (RegType << 28) | (PcMask << 16) | (RegNum & 0x7FF);
}

static DWORD VshSrcToken(DWORD RegType, DWORD RegNum, const DWORD Swz[4], DWORD Negate, DWORD Relative)
{
    DWORD Token = 0x80000000 | (RegType << 28) | (RegNum & 0x7FF);
    Token |= (Swz[0] << 16) | (Swz[1] << 18) | (Swz[2] << 20) | (Swz[3] << 22);
    if(Negate)
        Token |= (1u << 24);        // D3DSPSM_NEG
    if(Relative)
        Token |= (1u << 13);        // D3DVS_ADDRMODE_RELATIVE
    return Token;
}

// NV2A write mask (bit3=X .. bit0=W) -> PC write mask (bit0=X .. bit3=W)
static DWORD VshPcMask(DWORD NvMask)
{
    return ((NvMask >> 3) & 1) | ((NvMask >> 1) & 2) | ((NvMask << 1) & 4) | ((NvMask << 3) & 8);
}

// One decoded source operand
struct VshSrc
{
    DWORD Mux, R, Neg, Swz[4];
};

constexpr DWORD VSH_HOST_TEMP_COUNT = 12;

static void VshDecodeSrc(const DWORD *I, int Which, VshSrc *Src)
{
    switch(Which)
    {
        case 0:
            Src->Mux = VshGetField(I, FLD_A_MUX);   Src->R = VshGetField(I, FLD_A_R);
            Src->Neg = VshGetField(I, FLD_A_NEG);
            Src->Swz[0] = VshGetField(I, FLD_A_SWZ_X); Src->Swz[1] = VshGetField(I, FLD_A_SWZ_Y);
            Src->Swz[2] = VshGetField(I, FLD_A_SWZ_Z); Src->Swz[3] = VshGetField(I, FLD_A_SWZ_W);
            break;
        case 1:
            Src->Mux = VshGetField(I, FLD_B_MUX);   Src->R = VshGetField(I, FLD_B_R);
            Src->Neg = VshGetField(I, FLD_B_NEG);
            Src->Swz[0] = VshGetField(I, FLD_B_SWZ_X); Src->Swz[1] = VshGetField(I, FLD_B_SWZ_Y);
            Src->Swz[2] = VshGetField(I, FLD_B_SWZ_Z); Src->Swz[3] = VshGetField(I, FLD_B_SWZ_W);
            break;
        default:
            Src->Mux = VshGetField(I, FLD_C_MUX);
            Src->R   = (VshGetField(I, FLD_C_R_HIGH) << 2) | VshGetField(I, FLD_C_R_LOW);
            Src->Neg = VshGetField(I, FLD_C_NEG);
            Src->Swz[0] = VshGetField(I, FLD_C_SWZ_X); Src->Swz[1] = VshGetField(I, FLD_C_SWZ_Y);
            Src->Swz[2] = VshGetField(I, FLD_C_SWZ_Z); Src->Swz[3] = VshGetField(I, FLD_C_SWZ_W);
            break;
    }
}

static std::string VshFormatSource(const DWORD* instruction, const VshSrc& source)
{
    std::ostringstream text;
    if(source.Neg != 0)
    {
        text << '-';
    }
    switch(source.Mux)
    {
        case PARAM_R: text << 'r' << source.R; break;
        case PARAM_V: text << 'v' << VshGetField(instruction, FLD_V); break;
        case PARAM_C: text << 'c' << VshGetField(instruction, FLD_CONST); break;
        default: text << "invalid" << source.Mux; break;
    }
    static constexpr char Components[] = "xyzw";
    text << '.';
    for(DWORD component : source.Swz)
    {
        text << Components[component & 3u];
    }
    return text.str();
}

struct VshScratchPlan
{
    bool valid = false;
    bool needsPosition = false;
    bool hasDph = false;
    bool needsPairedIlu = false;
    bool needsDualDestination = false;
    DWORD position = 0;
    DWORD dph = 0;
    DWORD pairedIlu = 0;
    DWORD dualDestination = 0;
    const char* failureReason = nullptr;
};

static void VshMarkTempSource(const VshSrc& Source, std::array<bool, VSH_HOST_TEMP_COUNT>& Used,
                              bool& NeedsPosition)
{
    if(Source.Mux != PARAM_R)
    {
        return;
    }
    if(Source.R == 12)
    {
        NeedsPosition = true;
    }
    else if(Source.R < Used.size())
    {
        Used[Source.R] = true;
    }
}

static bool VshNeedsPairedIluScratch(const DWORD* Instruction, DWORD Mac, DWORD Ilu,
                                     DWORD MacMask, DWORD OutputR, DWORD OutputMask,
                                     const VshSrc& IluSource)
{
    if(Mac == MAC_NOP || Ilu == ILU_NOP)
    {
        return false;
    }
    if(Mac == MAC_ARL && IluSource.Mux == PARAM_C && VshGetField(Instruction, FLD_A0X) != 0)
    {
        return true;
    }
    if(IluSource.Mux != PARAM_R)
    {
        return false;
    }
    if(MacMask != 0 && OutputR == IluSource.R)
    {
        return true;
    }
    return OutputMask != 0 && VshGetField(Instruction, FLD_OUT_MUX) == OMUX_MAC &&
           VshGetField(Instruction, FLD_OUT_ORB) == OUTPUT_O &&
           VshGetField(Instruction, FLD_OUT_ADDRESS) == 0 && IluSource.R == 12;
}

static bool VshNeedsDualDestinationScratch(const DWORD* Instruction, DWORD Mac, DWORD Ilu,
                                           DWORD MacMask, DWORD IluMask, DWORD OutputR,
                                           DWORD OutputMask, DWORD OutputMux,
                                           const VshSrc Sources[3])
{
    if(OutputMask == 0 || VshGetField(Instruction, FLD_OUT_ORB) != OUTPUT_O ||
       VshGetField(Instruction, FLD_OUT_ADDRESS) != 0)
    {
        return false;
    }
    if(OutputMux == OMUX_ILU)
    {
        const DWORD iluDestination = Mac != MAC_NOP && MacMask != 0 ? 1 : OutputR;
        return Ilu != ILU_NOP && IluMask != 0 && iluDestination == 12 &&
               Sources[2].Mux == PARAM_R && Sources[2].R == 12;
    }
    if(Mac == MAC_NOP || Mac == MAC_ARL || MacMask == 0)
    {
        return false;
    }

    bool readsPosition = false;
    bool readsTemporaryDestination = false;
    const bool usesSource[3] = {
        true,
        Mac == MAC_MUL || Mac == MAC_MAD || Mac == MAC_DP3 || Mac == MAC_DPH ||
            Mac == MAC_DP4 || Mac == MAC_DST || Mac == MAC_MIN || Mac == MAC_MAX ||
            Mac == MAC_SLT || Mac == MAC_SGE,
        Mac == MAC_ADD || Mac == MAC_MAD,
    };
    for(std::size_t source = 0; source < 3; ++source)
    {
        if(usesSource[source] && Sources[source].Mux == PARAM_R)
        {
            readsPosition = readsPosition || Sources[source].R == 12;
            readsTemporaryDestination =
                readsTemporaryDestination || Sources[source].R == OutputR;
        }
    }
    return readsPosition && readsTemporaryDestination;
}

static VshScratchPlan VshBuildScratchPlan(const DWORD* XboxFunction)
{
    VshScratchPlan plan{};
    if(XboxFunction == nullptr || (XboxFunction[0] & 0xFFFFu) != 0x2078u)
    {
        plan.failureReason = "invalid_xbox_shader";
        return plan;
    }

    DWORD instructionCount = (XboxFunction[0] >> 16) & 0xFFFFu;
    if(instructionCount == 0 || instructionCount > 136)
    {
        instructionCount = 136;
    }

    std::array<bool, VSH_HOST_TEMP_COUNT> used{};
    for(DWORD instruction = 0; instruction < instructionCount; ++instruction)
    {
        const DWORD* encoded = &XboxFunction[1 + instruction * 4];
        const DWORD mac = VshGetField(encoded, FLD_MAC);
        const DWORD ilu = VshGetField(encoded, FLD_ILU);
        const DWORD macMask = VshGetField(encoded, FLD_OUT_MAC_MASK);
        const DWORD iluMask = VshGetField(encoded, FLD_OUT_ILU_MASK);
        const DWORD outputMask = VshGetField(encoded, FLD_OUT_O_MASK);
        const DWORD outputR = VshGetField(encoded, FLD_OUT_R);
        plan.hasDph = plan.hasDph || mac == MAC_DPH;

        VshSrc sources[3]{};
        for(int source = 0; source < 3; ++source)
        {
            VshDecodeSrc(encoded, source, &sources[source]);
        }
        plan.needsPairedIlu =
            plan.needsPairedIlu ||
            VshNeedsPairedIluScratch(encoded, mac, ilu, macMask, outputR, outputMask, sources[2]);
        plan.needsDualDestination =
            plan.needsDualDestination ||
            VshNeedsDualDestinationScratch(
                encoded, mac, ilu, macMask, iluMask, outputR, outputMask,
                VshGetField(encoded, FLD_OUT_MUX), sources);
        if(mac == MAC_MOV || mac == MAC_ARL)
        {
            VshMarkTempSource(sources[0], used, plan.needsPosition);
        }
        else if(mac == MAC_ADD)
        {
            VshMarkTempSource(sources[0], used, plan.needsPosition);
            VshMarkTempSource(sources[2], used, plan.needsPosition);
        }
        else if(mac == MAC_MAD)
        {
            VshMarkTempSource(sources[0], used, plan.needsPosition);
            VshMarkTempSource(sources[1], used, plan.needsPosition);
            VshMarkTempSource(sources[2], used, plan.needsPosition);
        }
        else if(mac != MAC_NOP)
        {
            VshMarkTempSource(sources[0], used, plan.needsPosition);
            VshMarkTempSource(sources[1], used, plan.needsPosition);
        }
        if(ilu != ILU_NOP)
        {
            VshMarkTempSource(sources[2], used, plan.needsPosition);
        }

        if(macMask != 0)
        {
            if(outputR == 12)
            {
                plan.needsPosition = true;
            }
            else if(outputR < used.size())
            {
                used[outputR] = true;
            }
        }
        if(iluMask != 0)
        {
            const DWORD iluR = (mac != MAC_NOP && macMask != 0) ? 1 : outputR;
            if(iluR == 12)
            {
                plan.needsPosition = true;
            }
            else if(iluR < used.size())
            {
                used[iluR] = true;
            }
        }
        if(outputMask != 0 && VshGetField(encoded, FLD_OUT_ORB) == OUTPUT_O &&
           VshGetField(encoded, FLD_OUT_ADDRESS) == 0)
        {
            plan.needsPosition = true;
        }
        if(VshGetField(encoded, FLD_FINAL) != 0)
        {
            break;
        }
    }

    const auto allocateScratch = [&used](DWORD& Scratch)
    {
        for(DWORD candidate = VSH_HOST_TEMP_COUNT; candidate-- > 0;)
        {
            if(!used[candidate])
            {
                used[candidate] = true;
                Scratch = candidate;
                return true;
            }
        }
        return false;
    };
    if(plan.needsPosition && !allocateScratch(plan.position))
    {
        plan.failureReason = "position_alias_no_scratch";
        return plan;
    }
    if(plan.hasDph && !allocateScratch(plan.dph))
    {
        plan.failureReason = "dph_no_scratch";
        return plan;
    }
    if(plan.needsPairedIlu && !allocateScratch(plan.pairedIlu))
    {
        plan.failureReason = "paired_ilu_no_scratch";
        return plan;
    }
    if(plan.needsDualDestination)
    {
        if(plan.hasDph)
        {
            plan.dualDestination = plan.dph;
        }
        else if(!allocateScratch(plan.dualDestination))
        {
            plan.failureReason = "dual_destination_no_scratch";
            return plan;
        }
    }
    plan.valid = true;
    return plan;
}

static bool VshHasIluOpcodeOutsidePair(const DWORD* XboxFunction, DWORD Opcode,
                                       std::size_t IgnoredScale,
                                       std::size_t IgnoredOffset)
{
    if(XboxFunction == nullptr || (XboxFunction[0] & 0xFFFFu) != 0x2078u)
    {
        return false;
    }

    DWORD instructionCount = (XboxFunction[0] >> 16) & 0xFFFFu;
    if(instructionCount == 0 || instructionCount > 136)
    {
        instructionCount = 136;
    }
    for(DWORD instruction = 0; instruction < instructionCount; ++instruction)
    {
        if(instruction == IgnoredScale || instruction == IgnoredOffset)
        {
            continue;
        }
        const DWORD* encoded = &XboxFunction[1 + instruction * 4];
        if(VshGetField(encoded, FLD_ILU) == Opcode)
        {
            return true;
        }
        if(VshGetField(encoded, FLD_FINAL) != 0)
        {
            break;
        }
    }
    return false;
}

static bool VshUsesRelativeConstants(const DWORD* XboxFunction)
{
    if(XboxFunction == nullptr || (XboxFunction[0] & 0xFFFFu) != 0x2078u)
    {
        return false;
    }

    DWORD instructionCount = (XboxFunction[0] >> 16) & 0xFFFFu;
    if(instructionCount == 0 || instructionCount > 136)
    {
        instructionCount = 136;
    }
    for(DWORD instruction = 0; instruction < instructionCount; ++instruction)
    {
        const DWORD* encoded = &XboxFunction[1 + instruction * 4];
        if(VshGetField(encoded, FLD_A0X) != 0)
        {
            return true;
        }
        if(VshGetField(encoded, FLD_FINAL) != 0)
        {
            break;
        }
    }
    return false;
}

static bool VshInstructionWritesPosition(const DWORD* Instruction)
{
    const DWORD macMask = VshGetField(Instruction, FLD_OUT_MAC_MASK);
    const DWORD iluMask = VshGetField(Instruction, FLD_OUT_ILU_MASK);
    const DWORD outputMask = VshGetField(Instruction, FLD_OUT_O_MASK);
    return ((macMask != 0 || iluMask != 0) && VshGetField(Instruction, FLD_OUT_R) == 12) ||
           (outputMask != 0 && VshGetField(Instruction, FLD_OUT_ORB) == OUTPUT_O &&
            VshGetField(Instruction, FLD_OUT_ADDRESS) == 0);
}

static bool VshInstructionReadsR12(const DWORD* Instruction)
{
    VshSrc srcA{}, srcB{}, srcC{};
    VshDecodeSrc(Instruction, 0, &srcA);
    VshDecodeSrc(Instruction, 1, &srcB);
    VshDecodeSrc(Instruction, 2, &srcC);
    const DWORD mac = VshGetField(Instruction, FLD_MAC);
    const DWORD ilu = VshGetField(Instruction, FLD_ILU);
    return (mac != MAC_NOP &&
            ((srcA.Mux == PARAM_R && srcA.R == 12) ||
             (srcB.Mux == PARAM_R && srcB.R == 12) ||
             (srcC.Mux == PARAM_R && srcC.R == 12))) ||
           (ilu != ILU_NOP && srcC.Mux == PARAM_R && srcC.R == 12);
}

static bool VshIsIdentitySource(const VshSrc& Source)
{
    return !Source.Neg && Source.Swz[0] == 0 && Source.Swz[1] == 1 &&
           Source.Swz[2] == 2 && Source.Swz[3] == 3;
}

static bool VshIsSupportedOutputAddress(DWORD Address)
{
    switch(Address)
    {
        case 0:
        case 3:
        case 4:
        case 5:
        case 6:
        case 9:
        case 10:
        case 11:
        case 12: return true;
        default: return false;
    }
}

static bool VshIsValidSource(const VshSrc& Source)
{
    return Source.Mux == PARAM_R || Source.Mux == PARAM_V || Source.Mux == PARAM_C;
}

static const char* VshTranslationFallbackReason(const DWORD* Instructions,
                                                std::size_t InstructionCount)
{
    for(std::size_t index = 0; index < InstructionCount; ++index)
    {
        const DWORD* instruction = &Instructions[index * 4];
        const DWORD mac = VshGetField(instruction, FLD_MAC);
        const DWORD ilu = VshGetField(instruction, FLD_ILU);
        const DWORD macMask = VshGetField(instruction, FLD_OUT_MAC_MASK);
        const DWORD iluMask = VshGetField(instruction, FLD_OUT_ILU_MASK);
        const DWORD outputMask = VshGetField(instruction, FLD_OUT_O_MASK);
        if(mac > MAC_ARL)
        {
            return "unsupported_mac_opcode";
        }

        VshSrc sourceA{}, sourceB{}, sourceC{};
        VshDecodeSrc(instruction, 0, &sourceA);
        VshDecodeSrc(instruction, 1, &sourceB);
        VshDecodeSrc(instruction, 2, &sourceC);
        const bool usesA = mac != MAC_NOP;
        const bool usesB = mac == MAC_MUL || mac == MAC_MAD || mac == MAC_DP3 ||
                           mac == MAC_DPH || mac == MAC_DP4 || mac == MAC_DST ||
                           mac == MAC_MIN || mac == MAC_MAX || mac == MAC_SLT ||
                           mac == MAC_SGE;
        const bool usesC = mac == MAC_ADD || mac == MAC_MAD || ilu != ILU_NOP;
        if((usesA && !VshIsValidSource(sourceA)) ||
           (usesB && !VshIsValidSource(sourceB)) ||
           (usesC && !VshIsValidSource(sourceC)))
        {
            return "unsupported_source_mux";
        }

        if((macMask != 0 || iluMask != 0) && VshGetField(instruction, FLD_OUT_R) > 12)
        {
            return "unsupported_temp_register";
        }
        if(outputMask != 0)
        {
            const DWORD outputMux = VshGetField(instruction, FLD_OUT_MUX);
            const bool selectedPipelineActive =
                outputMux == OMUX_MAC ? mac != MAC_NOP && mac != MAC_ARL : ilu != ILU_NOP;
            if(!selectedPipelineActive || VshGetField(instruction, FLD_OUT_ORB) != OUTPUT_O ||
               !VshIsSupportedOutputAddress(VshGetField(instruction, FLD_OUT_ADDRESS)))
            {
                return "unsupported_output_route";
            }
        }
    }
    return nullptr;
}

struct VshScreenSpaceSuffix
{
    std::size_t start = 0;
    bool ambiguous = false;
};

static VshScreenSpaceSuffix VshClassifyScreenSpaceSuffix(const DWORD* Instructions,
                                                         std::size_t InstructionCount)
{
    VshScreenSpaceSuffix suffix{ InstructionCount, false };
    if(Instructions == nullptr || InstructionCount < 2)
    {
        return suffix;
    }

    const DWORD* finalInstruction = &Instructions[(InstructionCount - 1) * 4];
    if(VshGetField(finalInstruction, FLD_FINAL) == 0 ||
       !VshInstructionWritesPosition(finalInstruction) ||
       !VshInstructionReadsR12(finalInstruction))
    {
        return suffix;
    }

    VshSrc srcA{}, srcB{};
    VshDecodeSrc(finalInstruction, 0, &srcA);
    VshDecodeSrc(finalInstruction, 1, &srcB);
    const DWORD* previousInstruction = finalInstruction - 4;
    // Recognize only the verified XDK-style terminal multiply. R12 feedback in
    // the shader body is guest computation and must remain intact; a terminal
    // lookalike that differs from this shape is safer on the CPU interpreter.
    const bool exactFinalMultiply =
        VshGetField(finalInstruction, FLD_MAC) == MAC_MUL &&
        VshGetField(finalInstruction, FLD_ILU) == ILU_NOP &&
        VshGetField(finalInstruction, FLD_V) == 1 &&
        VshGetField(finalInstruction, FLD_A0X) == 0 &&
        VshGetField(finalInstruction, FLD_OUT_MAC_MASK) == 0 &&
        VshGetField(finalInstruction, FLD_OUT_ILU_MASK) == 0 &&
        VshGetField(finalInstruction, FLD_OUT_O_MASK) == 0xF &&
        VshGetField(finalInstruction, FLD_OUT_MUX) == OMUX_MAC &&
        VshGetField(finalInstruction, FLD_OUT_ORB) == OUTPUT_O &&
        VshGetField(finalInstruction, FLD_OUT_ADDRESS) == 0 && srcA.Mux == PARAM_R &&
        srcA.R == 12 && VshIsIdentitySource(srcA) && srcB.Mux == PARAM_V &&
        VshIsIdentitySource(srcB) && VshInstructionWritesPosition(previousInstruction) &&
        !VshInstructionReadsR12(previousInstruction);
    if(exactFinalMultiply)
    {
        suffix.start = InstructionCount - 1;
    }
    else
    {
        suffix.ambiguous = true;
    }
    return suffix;
}

struct VshViewportPair
{
    std::size_t scale;
    std::size_t offset;
    bool discardScale;
};

static VshViewportPair VshFindViewportScaleAddPair(const DWORD* instructions,
                                                   std::size_t instructionCount)
{
    const VshViewportPair notFound{ instructionCount, instructionCount, false };
    if(instructions == nullptr || instructionCount < 3)
    {
        return notFound;
    }

    for(std::size_t scaleIndex = 0; scaleIndex + 1 < instructionCount; ++scaleIndex)
    {
        const DWORD* scale = &instructions[scaleIndex * 4];
        VshSrc scaleA{}, scaleB{}, scaleC{};
        VshDecodeSrc(scale, 0, &scaleA);
        VshDecodeSrc(scale, 1, &scaleB);
        VshDecodeSrc(scale, 2, &scaleC);
        const bool noScaleIlu = VshGetField(scale, FLD_ILU) == ILU_NOP &&
                                VshGetField(scale, FLD_OUT_ILU_MASK) == 0;
        const bool scaleShape =
            VshGetField(scale, FLD_MAC) == MAC_MUL &&
            VshGetField(scale, FLD_CONST) == 58 &&
            VshGetField(scale, FLD_OUT_MAC_MASK) == 0 &&
            VshGetField(scale, FLD_OUT_O_MASK) == 0xE &&
            VshGetField(scale, FLD_OUT_ORB) == OUTPUT_O &&
            VshGetField(scale, FLD_OUT_ADDRESS) == 0 &&
            VshGetField(scale, FLD_OUT_MUX) == OMUX_MAC && scaleA.Mux == PARAM_R &&
            scaleA.R == 12 && VshIsIdentitySource(scaleA) && scaleB.Mux == PARAM_C &&
            VshIsIdentitySource(scaleB);
        if(!scaleShape)
        {
            continue;
        }

        const std::size_t lastOffset = (std::min)(scaleIndex + 2, instructionCount - 1);
        for(std::size_t offsetIndex = scaleIndex + 1; offsetIndex <= lastOffset;
            ++offsetIndex)
        {
            const DWORD* offset = &instructions[offsetIndex * 4];
            VshSrc offsetA{}, offsetB{}, offsetC{};
            VshDecodeSrc(offset, 0, &offsetA);
            VshDecodeSrc(offset, 1, &offsetB);
            VshDecodeSrc(offset, 2, &offsetC);
            const bool fusedReciprocal =
                VshGetField(scale, FLD_ILU) == ILU_RCC &&
                VshGetField(scale, FLD_OUT_ILU_MASK) == 0x8 &&
                offsetB.Mux == PARAM_R && offsetB.R == 1 &&
                scaleC.Mux == PARAM_R && scaleC.R == 12 &&
                scaleC.Swz[0] == 3 && scaleC.Swz[1] == 3 &&
                scaleC.Swz[2] == 3 && scaleC.Swz[3] == 3;
            const bool exactPair =
                VshGetField(offset, FLD_MAC) == MAC_MAD &&
                VshGetField(offset, FLD_ILU) == ILU_NOP &&
                VshGetField(offset, FLD_CONST) == 59 &&
                VshGetField(offset, FLD_OUT_MAC_MASK) == 0 &&
                VshGetField(offset, FLD_OUT_ILU_MASK) == 0 &&
                VshGetField(offset, FLD_OUT_O_MASK) == 0xE &&
                VshGetField(offset, FLD_OUT_ORB) == OUTPUT_O &&
                VshGetField(offset, FLD_OUT_ADDRESS) == 0 &&
                VshGetField(offset, FLD_OUT_MUX) == OMUX_MAC &&
                offsetA.Mux == PARAM_R && offsetA.R == 12 &&
                VshIsIdentitySource(offsetA) && offsetB.Mux == PARAM_R &&
                offsetB.R == 1 && offsetB.Swz[0] == 0 && offsetB.Swz[1] == 0 &&
                offsetB.Swz[2] == 0 && offsetB.Swz[3] == 0 &&
                offsetC.Mux == PARAM_C && VshIsIdentitySource(offsetC);
            if(!exactPair)
            {
                continue;
            }

            bool positionUsedBetweenOrAfter = false;
            for(std::size_t trailing = scaleIndex + 1; trailing < instructionCount;
                ++trailing)
            {
                if(trailing == offsetIndex)
                {
                    continue;
                }
                const DWORD* instruction = &instructions[trailing * 4];
                positionUsedBetweenOrAfter =
                    positionUsedBetweenOrAfter || VshInstructionReadsR12(instruction) ||
                    VshInstructionWritesPosition(instruction);
            }
            if(!positionUsedBetweenOrAfter)
            {
                // A fused RCC computes only the reciprocal consumed by the
                // removed offset instruction. Other paired ILU work is an
                // independently scheduled side effect and must still run.
                return { scaleIndex, offsetIndex, noScaleIlu || fusedReciprocal };
            }
        }
    }
    return notFound;
}

static DWORD VshMapTemp(DWORD R, DWORD PositionScratch)
{
    return (R == 12) ? PositionScratch : R;
}

// Emit a PC source token for a decoded operand. Constants live at hardware
// index (API register + 96) in the microcode, so unbias them here.
static DWORD VshEmitSrc(const DWORD* I, const VshSrc* Src, bool Relative, DWORD PositionScratch)
{
    switch(Src->Mux)
    {
        case PARAM_R:
            return VshSrcToken(SPR_TEMP, VshMapTemp(Src->R, PositionScratch), Src->Swz, Src->Neg, FALSE);
        case PARAM_V:
            return VshSrcToken(SPR_INPUT, VshGetField(I, FLD_V), Src->Swz, Src->Neg, FALSE);
        case PARAM_C:
        {
            int Reg = (int)VshGetField(I, FLD_CONST) - 96;
            if(Reg < 0)
            {
                EmuWarning("VshDecoder: negative constant register c%d clamped to 0", Reg);
                Reg = 0;
            }
            return VshSrcToken(SPR_CONST, (DWORD)Reg, Src->Swz, Src->Neg, Relative);
        }
        default:
            EmuWarning("VshDecoder: unknown source mux %lu", Src->Mux);
            return VshSrcToken(SPR_TEMP, 0, Src->Swz, Src->Neg, FALSE);
    }
}

// Map an NV2A hardware output-register index (FLD_OUT_ADDRESS with ORB=O) to a
// PC vs.1.1 destination. Order per nxdk nvvertparse.c HardwareOutputRegisters:
// 0=HPOS 3=COL0 4=COL1 5=FOGC 6=PSIZ 7=BFC0 8=BFC1 9..12=TEX0..3.
static bool VshMapOutput(DWORD Address, DWORD *RegType, DWORD *RegNum)
{
    switch(Address)
    {
        case 0:  *RegType = SPR_RASTOUT;   *RegNum = 0; return true;   // oPos
        case 3:  *RegType = SPR_ATTROUT;   *RegNum = 0; return true;   // oD0
        case 4:  *RegType = SPR_ATTROUT;   *RegNum = 1; return true;   // oD1
        case 5:  *RegType = SPR_RASTOUT;   *RegNum = 1; return true;   // oFog
        case 6:
            *RegType = SPR_RASTOUT;
            *RegNum = 2;
            return true; // oPts
        case 9:
        case 10:
        case 11:
        case 12:
            *RegType = SPR_TEXCRDOUT;
            *RegNum = Address - 9;
            return true; // oT0..3
        default:
            // 7/8 = back-face colors (no PC equivalent), anything else unknown
            return false;
    }
}

static void VshEmit(DWORD* Out, int* n, DWORD Token)
{
    Out[(*n)++] = Token;
}

namespace
{
constexpr DWORD VSH_D3D_VERSION = 0xFFFE0101u;
constexpr DWORD VSH_D3D_END = 0x0000FFFFu;
constexpr DWORD VSH_XBOX_VERSION = 0x2078u;
constexpr std::size_t VSH_MAX_XBOX_INSTRUCTIONS = 136;
constexpr std::size_t VSH_MAX_D3D8_INSTRUCTIONS = 128;
constexpr std::size_t VSH_MAX_D3D8_TOKENS_PER_XBOX_INSTRUCTION = 28;

std::string VshHex(DWORD value)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return stream.str();
}

std::size_t VshXboxInstructionCapacity(const DWORD* xboxFunction)
{
    if(xboxFunction == nullptr || (xboxFunction[0] & 0xFFFFu) != VSH_XBOX_VERSION)
    {
        return 0;
    }

    const DWORD encodedCount = (xboxFunction[0] >> 16) & 0xFFFFu;
    if(encodedCount == 0 || encodedCount > VSH_MAX_XBOX_INSTRUCTIONS)
    {
        return VSH_MAX_XBOX_INSTRUCTIONS;
    }
    return static_cast<std::size_t>(encodedCount);
}

std::size_t VshXboxInstructionCount(const DWORD* xboxFunction)
{
    const std::size_t capacity = VshXboxInstructionCapacity(xboxFunction);
    for(std::size_t index = 0; index < capacity; ++index)
    {
        const DWORD* instruction = &xboxFunction[1 + index * 4];
        if(VshGetField(instruction, FLD_FINAL) != 0)
        {
            return index + 1;
        }
    }
    return capacity;
}

const char* VshMacName(DWORD opcode)
{
    static constexpr std::array<const char*, 15> names = {
        "nop", "mov", "mul", "add", "mad", "dp3", "dph", "dp4",
        "dst", "min", "max", "slt", "sge", "arl", "unknown"
    };
    return opcode < names.size() - 1 ? names[opcode] : names.back();
}

const char* VshIluName(DWORD opcode)
{
    static constexpr std::array<const char*, 9> names = {
        "nop", "mov", "rcp", "rcc", "rsq", "exp", "log", "lit", "unknown"
    };
    return opcode < names.size() - 1 ? names[opcode] : names.back();
}

const char* VshD3dOpcodeName(DWORD opcode)
{
    switch(opcode)
    {
        case SIO_NOP: return "nop";
        case SIO_MOV: return "mov";
        case SIO_ADD: return "add";
        case SIO_MAD: return "mad";
        case SIO_MUL: return "mul";
        case SIO_RCP: return "rcp";
        case SIO_RSQ: return "rsq";
        case SIO_DP3: return "dp3";
        case SIO_DP4: return "dp4";
        case SIO_MIN: return "min";
        case SIO_MAX: return "max";
        case SIO_SLT: return "slt";
        case SIO_SGE: return "sge";
        case SIO_EXP: return "exp";
        case SIO_LOG: return "log";
        case SIO_LIT: return "lit";
        case SIO_DST: return "dst";
        default: return "unknown";
    }
}

int VshD3dSourceCount(DWORD opcode)
{
    switch(opcode)
    {
        case SIO_NOP: return 0;
        case SIO_MOV:
        case SIO_RCP:
        case SIO_RSQ:
        case SIO_EXP:
        case SIO_LOG:
        case SIO_LIT: return 1;
        case SIO_ADD:
        case SIO_MUL:
        case SIO_DP3:
        case SIO_DP4:
        case SIO_MIN:
        case SIO_MAX:
        case SIO_SLT:
        case SIO_SGE:
        case SIO_DST: return 2;
        case SIO_MAD: return 3;
        default: return -1;
    }
}

struct VshD3dInstruction
{
    std::size_t tokenIndex = 0;
    std::size_t tokenCount = 0;
    DWORD opcode = 0;
    int sourceCount = 0;
    DWORD optimizedDestination = 0;
    bool keep = true;
};

using VshD3dLiveness = std::array<std::array<DWORD, 16>, 7>;

DWORD* VshD3dLiveMask(VshD3dLiveness& liveness, DWORD type, DWORD number)
{
    if(type >= liveness.size() || number >= liveness[type].size())
    {
        return nullptr;
    }
    return &liveness[type][number];
}

DWORD* VshD3dDestinationLiveMask(VshD3dLiveness& liveness, DWORD type, DWORD number)
{
    switch(type)
    {
        case SPR_TEMP:
            return number <= 11 ? VshD3dLiveMask(liveness, type, number) : nullptr;
        case SPR_ADDR:
            return number == 0 ? VshD3dLiveMask(liveness, type, number) : nullptr;
        case SPR_RASTOUT:
            return number <= 2 ? VshD3dLiveMask(liveness, type, number) : nullptr;
        case SPR_ATTROUT:
            return number <= 1 ? VshD3dLiveMask(liveness, type, number) : nullptr;
        case SPR_TEXCRDOUT:
            return number <= 7 ? VshD3dLiveMask(liveness, type, number) : nullptr;
        default:
            return nullptr;
    }
}

DWORD VshD3dSwizzledMask(DWORD sourceToken, DWORD laneMask)
{
    DWORD sourceMask = 0;
    for(DWORD lane = 0; lane < 4; ++lane)
    {
        if((laneMask & (1u << lane)) != 0)
        {
            sourceMask |= 1u << ((sourceToken >> (16 + lane * 2)) & 3u);
        }
    }
    return sourceMask;
}

DWORD VshD3dSourceReadMask(DWORD opcode, DWORD destinationMask, DWORD sourceToken)
{
    switch(opcode)
    {
        case SIO_DP3:
            return VshD3dSwizzledMask(sourceToken, 0x7u);
        case SIO_DP4:
            return VshD3dSwizzledMask(sourceToken, 0xFu);
        case SIO_RCP:
        case SIO_RSQ:
        case SIO_EXP:
        case SIO_LOG:
            return VshD3dSwizzledMask(sourceToken, 0x1u);
        case SIO_LIT:
        case SIO_DST:
            // These instructions have component-specific semantics. Keeping all
            // source lanes live is conservative and cannot remove a dependency.
            return VshD3dSwizzledMask(sourceToken, 0xFu);
        default:
            return VshD3dSwizzledMask(sourceToken, destinationMask);
    }
}

void VshD3dMarkSourceLive(VshD3dLiveness& liveness, DWORD opcode, DWORD destinationMask,
                          DWORD sourceToken)
{
    const DWORD type = (sourceToken >> 28) & 7u;
    const DWORD number = sourceToken & 0x7FFu;
    if(DWORD* liveMask = VshD3dLiveMask(liveness, type, number))
    {
        *liveMask |= VshD3dSourceReadMask(opcode, destinationMask, sourceToken);
    }
    if((sourceToken & (1u << 13)) != 0)
    {
        liveness[SPR_ADDR][0] |= 0x1u;
    }
}

bool VshParseD3dInstructions(const DWORD* function, std::size_t maxTokens,
                             std::vector<VshD3dInstruction>& instructions)
{
    if(function == nullptr || maxTokens < 2 || function[0] != VSH_D3D_VERSION)
    {
        return false;
    }

    std::size_t tokenIndex = 1;
    while(tokenIndex < maxTokens)
    {
        const DWORD instructionToken = function[tokenIndex];
        if(instructionToken == VSH_D3D_END)
        {
            return true;
        }

        const DWORD opcode = instructionToken & 0xFFFFu;
        const int sourceCount = VshD3dSourceCount(opcode);
        if(sourceCount < 0)
        {
            return false;
        }
        const std::size_t parameterCount = sourceCount == 0 ? 0 : static_cast<std::size_t>(sourceCount + 1);
        const std::size_t tokenCount = 1 + parameterCount;
        if(tokenCount > maxTokens - tokenIndex)
        {
            return false;
        }

        VshD3dInstruction instruction{};
        instruction.tokenIndex = tokenIndex;
        instruction.tokenCount = tokenCount;
        instruction.opcode = opcode;
        instruction.sourceCount = sourceCount;
        instruction.optimizedDestination = sourceCount == 0 ? 0 : function[tokenIndex + 1];
        instructions.push_back(instruction);
        tokenIndex += tokenCount;
    }
    return false;
}

const char* VshD3dRegisterName(DWORD type)
{
    static constexpr std::array<const char*, 8> names = {
        "r", "v", "c", "a", "oR", "oD", "oT", "reserved"
    };
    return names[type & 7u];
}

std::string VshFormatD3dParameter(DWORD token, bool destination)
{
    const DWORD type = (token >> 28) & 7u;
    const DWORD number = token & 0x7FFu;
    std::ostringstream stream;
    stream << VshD3dRegisterName(type) << std::dec << number;
    if(destination)
    {
        static constexpr char components[] = "xyzw";
        const DWORD mask = (token >> 16) & 0xFu;
        stream << '.';
        for(DWORD component = 0; component < 4; ++component)
        {
            if((mask & (1u << component)) != 0)
            {
                stream << components[component];
            }
        }
    }
    else
    {
        static constexpr char components[] = "xyzw";
        stream << '.';
        for(DWORD component = 0; component < 4; ++component)
        {
            stream << components[(token >> (16 + component * 2)) & 3u];
        }
        if((token & (1u << 24)) != 0)
        {
            stream << " neg";
        }
        if((token & (1u << 13)) != 0)
        {
            stream << " rel";
        }
    }
    return stream.str();
}

bool VshValidateRegister(DWORD token, bool destination, std::string& message)
{
    if((token & 0x80000000u) == 0)
    {
        message = "parameter token does not have bit 31 set";
        return false;
    }

    const DWORD type = (token >> 28) & 7u;
    const DWORD number = token & 0x7FFu;
    if(destination && ((token >> 16) & 0xFu) == 0)
    {
        message = "destination has an empty write mask";
        return false;
    }
    if(destination && (type == SPR_INPUT || type == SPR_CONST || type == 7))
    {
        message = "register type is not writable";
        return false;
    }
    if(!destination && type > SPR_CONST)
    {
        message = "register type is not readable";
        return false;
    }
    if(type == SPR_TEMP && number > 11)
    {
        message = "temporary register exceeds r11";
        return false;
    }
    if(type == SPR_INPUT && number > 15)
    {
        message = "input register exceeds v15";
        return false;
    }
    if(type == SPR_CONST && number > 95)
    {
        message = "constant register exceeds c95";
        return false;
    }
    if(type == SPR_ADDR && number != 0)
    {
        message = "address register is not a0";
        return false;
    }
    if(type == SPR_RASTOUT && number > 2)
    {
        message = "raster output register is out of range";
        return false;
    }
    if(type == SPR_ATTROUT && number > 1)
    {
        message = "attribute output register is out of range";
        return false;
    }
    if(type == SPR_TEXCRDOUT && number > 7)
    {
        message = "texture-coordinate output register is out of range";
        return false;
    }
    if(!destination && (token & (1u << 13)) != 0 && type != SPR_CONST)
    {
        message = "relative addressing is used on a non-constant source";
        return false;
    }
    return true;
}
} // namespace

std::uint32_t XTL::VshDiagnostics::HashXboxFunction(const void* xboxFunctionData)
{
    const DWORD* xboxFunction = static_cast<const DWORD*>(xboxFunctionData);
    if(xboxFunction == nullptr)
    {
        return 0;
    }

    constexpr std::uint32_t offsetBasis = 2166136261u;
    constexpr std::uint32_t prime = 16777619u;
    std::uint32_t hash = offsetBasis;
    const std::size_t wordCount = 1 + VshXboxInstructionCount(xboxFunction) * 4;
    for(std::size_t wordIndex = 0; wordIndex < wordCount; ++wordIndex)
    {
        const std::uint32_t word = static_cast<std::uint32_t>(xboxFunction[wordIndex]);
        for(unsigned int byteIndex = 0; byteIndex < 4; ++byteIndex)
        {
            hash ^= (word >> (byteIndex * 8)) & 0xFFu;
            hash *= prime;
        }
    }
    return hash;
}

XTL::VshDiagnostics::OptimizationResult XTL::VshDiagnostics::OptimizeD3D8Function(void* d3dFunctionData,
                                                                                  std::size_t maxTokens)
{
    DWORD* function = static_cast<DWORD*>(d3dFunctionData);
    OptimizationResult result{};
    std::vector<VshD3dInstruction> instructions;
    if(!VshParseD3dInstructions(function, maxTokens, instructions))
    {
        return result;
    }

    result.valid = true;
    result.beforeInstructionCount = instructions.size();

    VshD3dLiveness liveness{};
    for(DWORD number = 0; number < 3; ++number)
    {
        liveness[SPR_RASTOUT][number] = 0xFu;
    }
    for(DWORD number = 0; number < 2; ++number)
    {
        liveness[SPR_ATTROUT][number] = 0xFu;
    }
    for(DWORD number = 0; number < 8; ++number)
    {
        liveness[SPR_TEXCRDOUT][number] = 0xFu;
    }

    for(auto instruction = instructions.rbegin(); instruction != instructions.rend(); ++instruction)
    {
        if(instruction->sourceCount == 0)
        {
            instruction->keep = false;
            continue;
        }

        const DWORD destination = function[instruction->tokenIndex + 1];
        const DWORD destinationType = (destination >> 28) & 7u;
        const DWORD destinationNumber = destination & 0x7FFu;
        const DWORD destinationMask = (destination >> 16) & 0xFu;
        DWORD* liveMask = VshD3dDestinationLiveMask(liveness, destinationType, destinationNumber);
        if(liveMask == nullptr)
        {
            // Preserve instructions using a register outside the optimizer's
            // model. The validator will provide the precise rejection reason.
            for(int sourceIndex = 0; sourceIndex < instruction->sourceCount; ++sourceIndex)
            {
                const DWORD source = function[instruction->tokenIndex + 2 + static_cast<std::size_t>(sourceIndex)];
                VshD3dMarkSourceLive(liveness, instruction->opcode, destinationMask, source);
            }
            continue;
        }

        const DWORD neededMask = *liveMask & destinationMask;
        if(neededMask == 0)
        {
            instruction->keep = false;
            continue;
        }

        instruction->optimizedDestination = (destination & ~(0xFu << 16)) | (neededMask << 16);
        *liveMask &= ~neededMask;
        for(int sourceIndex = 0; sourceIndex < instruction->sourceCount; ++sourceIndex)
        {
            const DWORD source = function[instruction->tokenIndex + 2 + static_cast<std::size_t>(sourceIndex)];
            VshD3dMarkSourceLive(liveness, instruction->opcode, neededMask, source);
        }
    }

    std::size_t writeIndex = 1;
    for(const VshD3dInstruction& instruction : instructions)
    {
        if(!instruction.keep)
        {
            continue;
        }
        for(std::size_t offset = 0; offset < instruction.tokenCount; ++offset)
        {
            function[writeIndex + offset] = function[instruction.tokenIndex + offset];
        }
        function[writeIndex + 1] = instruction.optimizedDestination;
        writeIndex += instruction.tokenCount;
        ++result.afterInstructionCount;
    }
    function[writeIndex++] = VSH_D3D_END;
    result.tokenCount = writeIndex;
    return result;
}

XTL::VshDiagnostics::ValidationResult XTL::VshDiagnostics::ValidateD3D8Function(const void* d3dFunctionData,
                                                                                std::size_t maxTokens)
{
    const DWORD* d3dFunction = static_cast<const DWORD*>(d3dFunctionData);
    ValidationResult result{};
    if(d3dFunction == nullptr)
    {
        result.message = "function is null";
        return result;
    }
    if(maxTokens < 2 || d3dFunction[0] != VSH_D3D_VERSION)
    {
        result.message = "missing vs.1.1 version token";
        return result;
    }

    std::size_t tokenIndex = 1;
    std::size_t instructionIndex = 0;
    bool writesPosition = false;
    while(tokenIndex < maxTokens)
    {
        const DWORD instructionToken = d3dFunction[tokenIndex];
        if(instructionToken == VSH_D3D_END)
        {
            result.valid = writesPosition;
            result.instructionIndex = instructionIndex;
            result.message = writesPosition ? "ok" : "shader never writes oPos";
            return result;
        }
        if(instructionIndex >= VSH_MAX_D3D8_INSTRUCTIONS)
        {
            result.instructionIndex = instructionIndex;
            result.message = "instruction count exceeds the vs.1.1 limit of 128";
            return result;
        }

        const DWORD opcode = instructionToken & 0xFFFFu;
        const int sourceCount = VshD3dSourceCount(opcode);
        if(sourceCount < 0)
        {
            result.instructionIndex = instructionIndex;
            result.message = "unknown opcode " + VshHex(opcode);
            return result;
        }

        const std::size_t parameterCount = sourceCount == 0 ? 0 : static_cast<std::size_t>(sourceCount + 1);
        if(tokenIndex + 1 + parameterCount > maxTokens)
        {
            result.instructionIndex = instructionIndex;
            result.message = "instruction is truncated";
            return result;
        }
        if(parameterCount != 0)
        {
            std::string message;
            const DWORD destination = d3dFunction[tokenIndex + 1];
            if(!VshValidateRegister(destination, true, message))
            {
                result.instructionIndex = instructionIndex;
                result.message = "invalid destination: " + message;
                return result;
            }
            const DWORD destinationType = (destination >> 28) & 7u;
            const DWORD destinationNumber = destination & 0x7FFu;
            writesPosition = writesPosition || (destinationType == SPR_RASTOUT && destinationNumber == 0);

            for(int sourceIndex = 0; sourceIndex < sourceCount; ++sourceIndex)
            {
                const DWORD source = d3dFunction[tokenIndex + 2 + static_cast<std::size_t>(sourceIndex)];
                if(!VshValidateRegister(source, false, message))
                {
                    result.instructionIndex = instructionIndex;
                    result.message = "invalid source " + std::to_string(sourceIndex) + ": " + message;
                    return result;
                }
            }
        }

        tokenIndex += 1 + parameterCount;
        ++instructionIndex;
    }

    result.instructionIndex = instructionIndex;
    result.message = "missing END token";
    return result;
}

XTL::VshDiagnostics::ValidationResult XTL::VshDiagnostics::ValidateD3D8Translation(const void* xboxFunctionData,
                                                                                   const void* d3dFunction)
{
    const DWORD* xboxFunction = static_cast<const DWORD*>(xboxFunctionData);
    const std::size_t maxD3dTokens =
        16 + VshXboxInstructionCount(xboxFunction) *
                 VSH_MAX_D3D8_TOKENS_PER_XBOX_INSTRUCTION;
    return ValidateD3D8Function(d3dFunction, maxD3dTokens);
}

bool XTL::VshDiagnostics::ExpandQuadListIndices(const std::uint32_t* sourceIndices,
                                                std::size_t sourceIndexCount,
                                                std::vector<std::uint32_t>& expandedIndices)
{
    expandedIndices.clear();
    if(sourceIndexCount % 4 != 0 ||
       (sourceIndices == nullptr &&
        sourceIndexCount > (std::numeric_limits<std::uint32_t>::max)()))
    {
        return false;
    }
    const std::size_t quadCount = sourceIndexCount / 4;
    if(quadCount > (std::numeric_limits<std::size_t>::max)() / 6)
    {
        return false;
    }
    expandedIndices.reserve(quadCount * 6);
    for(std::size_t quad = 0; quad < quadCount; ++quad)
    {
        const std::size_t base = quad * 4;
        const std::uint32_t index0 = sourceIndices == nullptr
                                         ? static_cast<std::uint32_t>(base)
                                         : sourceIndices[base];
        const std::uint32_t index1 = sourceIndices == nullptr
                                         ? static_cast<std::uint32_t>(base + 1)
                                         : sourceIndices[base + 1];
        const std::uint32_t index2 = sourceIndices == nullptr
                                         ? static_cast<std::uint32_t>(base + 2)
                                         : sourceIndices[base + 2];
        const std::uint32_t index3 = sourceIndices == nullptr
                                         ? static_cast<std::uint32_t>(base + 3)
                                         : sourceIndices[base + 3];
        expandedIndices.push_back(index0);
        expandedIndices.push_back(index1);
        expandedIndices.push_back(index2);
        expandedIndices.push_back(index0);
        expandedIndices.push_back(index2);
        expandedIndices.push_back(index3);
    }
    return true;
}

float XTL::VshDiagnostics::SelectRasterOutput(const float values[4], std::uint8_t writeMask,
                                              float fallback)
{
    for(std::size_t component = 0; component < 4; ++component)
    {
        if((writeMask & (0x8u >> component)) != 0)
        {
            return values[component];
        }
    }
    return fallback;
}

float XTL::VshDiagnostics::ClampPointSize(float pointSize, float fallback, float maximum)
{
    if(!(fallback > 0.0f))
    {
        fallback = 1.0f;
    }
    if(!(maximum > 0.0f))
    {
        maximum = fallback;
    }
    if(!(pointSize > 0.0f))
    {
        pointSize = fallback;
    }
    return pointSize > maximum ? maximum : pointSize;
}

std::uint32_t XTL::VshDiagnostics::PackD3DColor(const float color[4])
{
    std::uint32_t channels[4] = {};
    for(std::size_t component = 0; component < 4; ++component)
    {
        if(!(color[component] > 0.0f))
        {
            channels[component] = 0;
        }
        else if(color[component] >= 1.0f)
        {
            channels[component] = 255;
        }
        else
        {
            channels[component] = static_cast<std::uint32_t>(color[component] * 255.0f);
        }
    }
    return (channels[3] << 24) | (channels[0] << 16) | (channels[1] << 8) | channels[2];
}

std::uint32_t XTL::VshDiagnostics::PackD3DSpecularFog(
    const float specular[4], const RasterOutputs& rasterOutputs)
{
    const float color[4] = {
        specular[0],
        specular[1],
        specular[2],
        SelectRasterOutput(rasterOutputs.fog, rasterOutputs.fogWriteMask, 1.0f),
    };
    return PackD3DColor(color);
}

XTL::VshDiagnostics::XboxFunctionDisposition
XTL::VshDiagnostics::ClassifyXboxFunction(const void* xboxFunctionData, std::string& reason)
{
    const DWORD* xboxFunction = static_cast<const DWORD*>(xboxFunctionData);
    if(xboxFunction == nullptr || (xboxFunction[0] & 0xFFFFu) != VSH_XBOX_VERSION)
    {
        reason = "invalid_xbox_function";
        return XboxFunctionDisposition::Reject;
    }

    const std::size_t instructionCount = VshXboxInstructionCount(xboxFunction);
    const char* rejectionReason =
        VshTranslationFallbackReason(&xboxFunction[1], instructionCount);
    if(rejectionReason != nullptr)
    {
        reason = rejectionReason;
        return XboxFunctionDisposition::Reject;
    }
    const VshScreenSpaceSuffix screenSpaceSuffix =
        VshClassifyScreenSpaceSuffix(&xboxFunction[1], instructionCount);
    const VshViewportPair viewportPair =
        VshFindViewportScaleAddPair(&xboxFunction[1], instructionCount);
    const bool viewportPairCoversFinalInstruction =
        viewportPair.scale < instructionCount && viewportPair.offset == instructionCount - 1;
    if(screenSpaceSuffix.ambiguous && !viewportPairCoversFinalInstruction)
    {
        reason = "ambiguous_screen_space_suffix";
        return XboxFunctionDisposition::ExecuteOnCpu;
    }
    const std::size_t ignoredViewportScale =
        viewportPair.discardScale ? viewportPair.scale : instructionCount;
    if(VshHasIluOpcodeOutsidePair(xboxFunction, ILU_RCC, ignoredViewportScale,
                                 viewportPair.offset))
    {
        reason = "rcc_requires_clamp";
        return XboxFunctionDisposition::ExecuteOnCpu;
    }
    if(VshUsesRelativeConstants(xboxFunction))
    {
        reason = "relative_constant_dynamic_range";
        return XboxFunctionDisposition::ExecuteOnCpu;
    }

    const VshScratchPlan scratchPlan = VshBuildScratchPlan(xboxFunction);
    if(!scratchPlan.valid)
    {
        reason = scratchPlan.failureReason;
        return XboxFunctionDisposition::ExecuteOnCpu;
    }
    reason.clear();
    return XboxFunctionDisposition::TranslateToHost;
}

bool XTL::VshDiagnostics::RequiresCpuFallback(const void* xboxFunctionData, std::string& reason)
{
    return ClassifyXboxFunction(xboxFunctionData, reason) ==
           XboxFunctionDisposition::ExecuteOnCpu;
}

std::vector<std::string> XTL::VshDiagnostics::DecodeXboxFunction(const void* xboxFunctionData)
{
    const DWORD* xboxFunction = static_cast<const DWORD*>(xboxFunctionData);
    std::vector<std::string> listing;
    const std::size_t instructionCount = VshXboxInstructionCount(xboxFunction);
    listing.reserve(instructionCount);
    for(std::size_t index = 0; index < instructionCount; ++index)
    {
        const DWORD* instruction = &xboxFunction[1 + index * 4];
        VshSrc sourceA{}, sourceB{}, sourceC{};
        VshDecodeSrc(instruction, 0, &sourceA);
        VshDecodeSrc(instruction, 1, &sourceB);
        VshDecodeSrc(instruction, 2, &sourceC);
        std::ostringstream line;
        line << "pc=" << std::dec << std::setw(3) << std::setfill('0') << index
             << " raw=" << VshHex(instruction[0]) << ',' << VshHex(instruction[1]) << ','
             << VshHex(instruction[2]) << ',' << VshHex(instruction[3])
             << " mac=" << VshMacName(VshGetField(instruction, FLD_MAC))
             << " ilu=" << VshIluName(VshGetField(instruction, FLD_ILU))
             << " c=" << VshGetField(instruction, FLD_CONST)
             << " v=" << VshGetField(instruction, FLD_V)
             << " a=" << VshFormatSource(instruction, sourceA)
             << " b=" << VshFormatSource(instruction, sourceB)
             << " csrc=" << VshFormatSource(instruction, sourceC)
             << " out_r=" << VshGetField(instruction, FLD_OUT_R)
             << " mac_mask=0x" << std::hex << VshGetField(instruction, FLD_OUT_MAC_MASK)
             << " ilu_mask=0x" << VshGetField(instruction, FLD_OUT_ILU_MASK)
             << " out_mask=0x" << VshGetField(instruction, FLD_OUT_O_MASK)
             << " orb=" << VshGetField(instruction, FLD_OUT_ORB)
             << " out_addr=" << std::dec << VshGetField(instruction, FLD_OUT_ADDRESS)
             << " out_mux=" << VshGetField(instruction, FLD_OUT_MUX)
             << " a0x=" << VshGetField(instruction, FLD_A0X)
             << " final=" << VshGetField(instruction, FLD_FINAL);
        listing.push_back(line.str());
    }
    return listing;
}

std::vector<std::string> XTL::VshDiagnostics::DecodeD3D8Function(const void* d3dFunctionData,
                                                                 std::size_t maxTokens)
{
    const DWORD* d3dFunction = static_cast<const DWORD*>(d3dFunctionData);
    std::vector<std::string> listing;
    if(d3dFunction == nullptr || maxTokens == 0)
    {
        return listing;
    }

    std::size_t tokenIndex = 1;
    std::size_t instructionIndex = 0;
    while(tokenIndex < maxTokens && d3dFunction[tokenIndex] != VSH_D3D_END)
    {
        const DWORD token = d3dFunction[tokenIndex];
        const DWORD opcode = token & 0xFFFFu;
        const int sourceCount = VshD3dSourceCount(opcode);
        std::ostringstream line;
        line << "pc=" << std::dec << std::setw(3) << std::setfill('0') << instructionIndex
             << " token=" << VshHex(token) << " op=" << VshD3dOpcodeName(opcode);
        if(sourceCount < 0)
        {
            line << " invalid=unknown_opcode";
            listing.push_back(line.str());
            break;
        }
        const std::size_t parameterCount = sourceCount == 0 ? 0 : static_cast<std::size_t>(sourceCount + 1);
        if(tokenIndex + 1 + parameterCount > maxTokens)
        {
            line << " invalid=truncated";
            listing.push_back(line.str());
            break;
        }
        if(parameterCount != 0)
        {
            line << " dst=" << VshFormatD3dParameter(d3dFunction[tokenIndex + 1], true);
            for(int sourceIndex = 0; sourceIndex < sourceCount; ++sourceIndex)
            {
                line << " src" << sourceIndex << '='
                     << VshFormatD3dParameter(d3dFunction[tokenIndex + 2 + static_cast<std::size_t>(sourceIndex)], false);
            }
        }
        listing.push_back(line.str());
        tokenIndex += 1 + parameterCount;
        ++instructionIndex;
    }
    return listing;
}

std::vector<std::string>
XTL::VshDiagnostics::DecodeVertexDeclaration(const void* declarationData,
                                             std::size_t maxTokens)
{
    const DWORD* declaration = static_cast<const DWORD*>(declarationData);
    std::vector<std::string> listing;
    if(declaration == nullptr || maxTokens == 0)
    {
        return listing;
    }

    for(std::size_t tokenIndex = 0; tokenIndex < maxTokens;)
    {
        const DWORD token = declaration[tokenIndex];
        std::ostringstream line;
        line << "token=" << std::dec << std::setw(3) << std::setfill('0') << tokenIndex
             << " raw=" << VshHex(token);
        if(token == 0xFFFFFFFFu)
        {
            line << " type=end";
            listing.push_back(line.str());
            return listing;
        }

        const DWORD tokenType = token >> 29;
        std::size_t payloadCount = 0;
        const char* payloadOwner = nullptr;
        switch(tokenType)
        {
            case 0:
                line << " type=nop";
                break;
            case 1:
                if((token & 0x10000000u) != 0)
                {
                    line << " type=stream stream=tessellator";
                }
                else
                {
                    line << " type=stream stream=" << std::dec << (token & 0xFu);
                }
                break;
            case 2:
                if((token & 0x10000000u) != 0)
                {
                    line << " type=skip dwords=" << std::dec << ((token >> 16) & 0xFu);
                }
                else
                {
                    line << " type=reg reg=" << std::dec << (token & 0x1Fu)
                         << " data_type=" << VshHex((token >> 16) & 0xFFu);
                }
                break;
            case 3:
                if((token & 0x10000000u) != 0)
                {
                    line << " type=tess_uv reg=" << std::dec << (token & 0x1Fu);
                }
                else
                {
                    line << " type=tess_normal in=" << std::dec << ((token >> 20) & 0xFu)
                         << " out=" << (token & 0x1Fu);
                }
                break;
            case 4:
            {
                const std::size_t constantCount = (token >> 25) & 0xFu;
                line << " type=const addr=" << std::dec << (token & 0x7Fu)
                     << " count=" << constantCount;
                payloadCount = constantCount * 4;
                payloadOwner = "const";
                break;
            }
            case 5:
                payloadCount = (token >> 24) & 0x1Fu;
                payloadOwner = "extension";
                line << " type=extension count=" << std::dec << payloadCount
                     << " info=" << VshHex(token & 0xFFFFFFu);
                break;
            case 6:
                line << " type=invalid invalid=reserved_token_type";
                listing.push_back(line.str());
                return listing;
            case 7:
                line << " type=invalid invalid=malformed_end";
                listing.push_back(line.str());
                return listing;
            default:
                break;
        }
        listing.push_back(line.str());
        ++tokenIndex;

        if(payloadCount > maxTokens - tokenIndex)
        {
            std::ostringstream truncated;
            truncated << "token=" << std::dec << std::setw(3) << std::setfill('0')
                      << tokenIndex << " type=invalid invalid=truncated_" << payloadOwner
                      << "_payload expected=" << payloadCount
                      << " available=" << (maxTokens - tokenIndex);
            listing.push_back(truncated.str());
            return listing;
        }
        for(std::size_t payloadIndex = 0; payloadIndex < payloadCount;
            ++payloadIndex, ++tokenIndex)
        {
            std::ostringstream payload;
            payload << "token=" << std::dec << std::setw(3) << std::setfill('0') << tokenIndex
                    << " raw=" << VshHex(declaration[tokenIndex]) << " type=payload owner="
                    << payloadOwner << " element=" << payloadIndex;
            listing.push_back(payload.str());
        }
    }

    listing.push_back("type=invalid invalid=declaration_missing_end");
    return listing;
}

void XTL::VshDiagnostics::DumpRejectedTranslation(FILE* stream, const TranslationCapture& capture)
{
    const DWORD* xboxFunction = static_cast<const DWORD*>(capture.xboxFunction);
    const DWORD* d3dFunction = static_cast<const DWORD*>(capture.d3dFunction);
    const DWORD* xboxDeclaration = static_cast<const DWORD*>(capture.xboxDeclaration);
    const DWORD* d3dDeclaration = static_cast<const DWORD*>(capture.d3dDeclaration);
    if(stream == nullptr)
    {
        return;
    }

    const bool hasXboxFunction =
        xboxFunction != nullptr && (xboxFunction[0] & 0xFFFFu) == VSH_XBOX_VERSION;
    const std::uint32_t hash = hasXboxFunction ? HashXboxFunction(xboxFunction) : 0;
    const std::size_t xboxInstructionCount = VshXboxInstructionCount(xboxFunction);
    const std::size_t maxD3dTokens =
        16 + xboxInstructionCount * VSH_MAX_D3D8_TOKENS_PER_XBOX_INSTRUCTION;
    const bool validationAvailable = hasXboxFunction && d3dFunction != nullptr;
    ValidationResult validation;
    if(validationAvailable)
    {
        validation = ValidateD3D8Translation(xboxFunction, d3dFunction);
    }
    const char* reason = capture.rejectionReason;
    if(reason == nullptr || reason[0] == '\0')
    {
        reason = validationAvailable ? validation.message.c_str() : "unspecified";
    }
    const char* validationState = validationAvailable
                                      ? (validation.valid ? "pass" : "fail")
                                      : "unavailable";
    std::fprintf(stream,
                 "VSH| rejected hash=%08X xbox_instructions=%zu d3d8=%s validation=%s "
                 "at=%zu reason=%s\n",
                 hash, xboxInstructionCount, d3dFunction != nullptr ? "present" : "unavailable",
                 validationState, validation.instructionIndex, reason);

    for(const std::string& line : DecodeXboxFunction(xboxFunction))
    {
        std::fprintf(stream, "VSH| nv2a hash=%08X %s\n", hash, line.c_str());
    }
    for(const std::string& line : DecodeD3D8Function(d3dFunction, maxD3dTokens))
    {
        std::fprintf(stream, "VSH| d3d8 hash=%08X %s\n", hash, line.c_str());
    }

    if(xboxDeclaration == nullptr)
    {
        std::fprintf(stream, "VSH| declaration_xbox hash=%08X unavailable\n", hash);
    }
    else
    {
        for(const std::string& line : DecodeVertexDeclaration(xboxDeclaration, 128))
        {
            std::fprintf(stream, "VSH| declaration_xbox hash=%08X %s\n", hash, line.c_str());
        }
    }
    if(d3dDeclaration == nullptr)
    {
        std::fprintf(stream, "VSH| declaration_d3d8 hash=%08X unavailable\n", hash);
    }
    else
    {
        for(const std::string& line : DecodeVertexDeclaration(d3dDeclaration, 128))
        {
            std::fprintf(stream, "VSH| declaration_d3d8 hash=%08X %s\n", hash, line.c_str());
        }
    }
    std::fflush(stream);
}

static std::size_t VshReplayDeclarationTokenCount(const DWORD* declaration,
                                                  std::size_t maxTokens)
{
    if(declaration == nullptr)
    {
        return 0;
    }
    for(std::size_t tokenIndex = 0; tokenIndex < maxTokens;)
    {
        const DWORD token = declaration[tokenIndex++];
        if(token == 0xFFFFFFFFu)
        {
            return tokenIndex;
        }
        const DWORD tokenType = token >> 29;
        if(tokenType >= 6)
        {
            return tokenIndex;
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
        if(payloadCount > maxTokens - tokenIndex)
        {
            return maxTokens;
        }
        tokenIndex += payloadCount;
    }
    return maxTokens;
}

static void VshReplayPrintReason(FILE* stream, const char* reason)
{
    if(reason == nullptr || reason[0] == '\0')
    {
        std::fputs("unspecified", stream);
        return;
    }
    for(const unsigned char character : std::string(reason))
    {
        const bool safe = (character >= 'a' && character <= 'z') ||
                          (character >= 'A' && character <= 'Z') ||
                          (character >= '0' && character <= '9') || character == '_' ||
                          character == '-';
        std::fputc(safe ? character : '_', stream);
    }
}

static void VshReplayPrintWords(FILE* stream, const DWORD* words, std::size_t wordCount)
{
    if(words == nullptr || wordCount == 0)
    {
        std::fputc('-', stream);
        return;
    }
    for(std::size_t index = 0; index < wordCount; ++index)
    {
        std::fprintf(stream, "%s%08X", index == 0 ? "" : ",",
                     static_cast<unsigned int>(words[index]));
    }
}

static void VshReplayPrintSparseFloats(FILE* stream, const float* values,
                                       std::size_t valueCount)
{
    bool wroteValue = false;
    for(std::size_t index = 0; index < valueCount; ++index)
    {
        DWORD bits = 0;
        std::memcpy(&bits, &values[index], sizeof(bits));
        if(bits == 0)
        {
            continue;
        }
        std::fprintf(stream, "%s%zu:%08X", wroteValue ? "," : "", index,
                     static_cast<unsigned int>(bits));
        wroteValue = true;
    }
    if(!wroteValue)
    {
        std::fputc('-', stream);
    }
}

void XTL::VshDiagnostics::DumpReplayCapture(FILE* stream,
                                            const TranslationCapture& capture)
{
    const DWORD* xboxFunction = static_cast<const DWORD*>(capture.xboxFunction);
    if(stream == nullptr || xboxFunction == nullptr ||
       (xboxFunction[0] & 0xFFFFu) != VSH_XBOX_VERSION)
    {
        return;
    }

    std::array<float, 16 * 4> canonicalInputs{};
    const float* inputs = capture.inputs;
    std::size_t inputFloatCount = std::min<std::size_t>(capture.inputFloatCount, 16 * 4);
    const char* inputSource = capture.inputSource;
    if(inputs == nullptr || inputFloatCount == 0)
    {
        for(std::size_t index = 0; index < canonicalInputs.size(); ++index)
        {
            const std::size_t registerIndex = index / 4;
            canonicalInputs[index] = static_cast<float>(registerIndex + 1) * 0.0625f +
                                     static_cast<float>((index % 4) + 1) * 0.25f;
        }
        inputs = canonicalInputs.data();
        inputFloatCount = canonicalInputs.size();
        inputSource = "canonical";
    }
    else if(inputSource == nullptr || inputSource[0] == '\0')
    {
        inputSource = "runtime";
    }

    const std::size_t functionWordCount = 1 + VshXboxInstructionCount(xboxFunction) * 4;
    const DWORD* declaration = static_cast<const DWORD*>(capture.xboxDeclaration);
    const std::size_t declarationTokenCount =
        VshReplayDeclarationTokenCount(declaration, 128);
    const std::size_t constantFloatCount =
        capture.constants == nullptr
            ? 0
            : std::min<std::size_t>(capture.constantFloatCount, 192 * 4);
    std::fprintf(stream, "VSHREPLAY| version=1 hash=%08X reason=",
                 static_cast<unsigned int>(HashXboxFunction(xboxFunction)));
    VshReplayPrintReason(stream, capture.rejectionReason);
    std::fputs(" input_source=", stream);
    VshReplayPrintReason(stream, inputSource);
    std::fputs(" function=", stream);
    VshReplayPrintWords(stream, xboxFunction, functionWordCount);
    std::fputs(" declaration=", stream);
    VshReplayPrintWords(stream, declaration, declarationTokenCount);
    std::fputs(" constants=", stream);
    VshReplayPrintSparseFloats(stream, capture.constants, constantFloatCount);
    std::fputs(" inputs=", stream);
    VshReplayPrintSparseFloats(stream, inputs, inputFloatCount);
    std::fputc('\n', stream);
    std::fflush(stream);
}

// ******************************************************************
// * EmuVshRecompileXboxFunction
// ******************************************************************
// * Translate an Xbox CreateVertexShader function blob (0x2078 version
// * token + NV2A microcode) into freshly-allocated vs.1.1 bytecode.
// * Returns NULL on failure. Caller delete[]s the result.
// ******************************************************************
DWORD *XTL::EmuVshRecompileXboxFunction(const DWORD *pXboxFunction)
{
    if(pXboxFunction == NULL || (pXboxFunction[0] & 0xFFFF) != 0x2078)
        return NULL;

    std::string dispositionReason;
    const VshDiagnostics::XboxFunctionDisposition disposition =
        VshDiagnostics::ClassifyXboxFunction(pXboxFunction, dispositionReason);
    if(disposition != VshDiagnostics::XboxFunctionDisposition::TranslateToHost)
    {
        const char* mode =
            disposition == VshDiagnostics::XboxFunctionDisposition::ExecuteOnCpu ? "CPU fallback"
                                                                                 : "rejection";
        EmuWarning("VshDecoder: function requires %s (%s)", mode, dispositionReason.c_str());
        return NULL;
    }

    const DWORD InstrCount = static_cast<DWORD>(VshXboxInstructionCount(pXboxFunction));

    const VshScreenSpaceSuffix ScreenSpaceSuffix =
        VshClassifyScreenSpaceSuffix(&pXboxFunction[1], InstrCount);
    const VshViewportPair ViewportPair =
        VshFindViewportScaleAddPair(&pXboxFunction[1], InstrCount);

    const VshScratchPlan ScratchPlan = VshBuildScratchPlan(pXboxFunction);

    // Paired DPH+ILU can require staged source and dual-destination results.
    DWORD* Out =
        new DWORD[16 + InstrCount * VSH_MAX_D3D8_TOKENS_PER_XBOX_INSTRUCTION];
    int n = 0;
    bool NeedsPosEpilogue = false;
    VshEmit(Out, &n, 0xFFFE0101);   // vs.1.1

    // Seed the position scratch (the R12/oPos alias) from the position input v0.
    // 2D / UI shaders often write only oPos.xy and leave z,w to the vertex; the
    // epilogue then copies the whole scratch to oPos, so unwritten components
    // must be defined -- reading an uninitialized temp makes the host reject the
    // shader (D3DERR_INVALIDCALL). A full oPos-writing shader overwrites this.
    if(ScratchPlan.needsPosition)
    {
        static const DWORD IdSwz[4] = { 0, 1, 2, 3 };
        VshEmit(Out, &n, SIO_MOV);
        VshEmit(Out, &n, VshDstToken(SPR_TEMP, ScratchPlan.position, 0xF));
        VshEmit(Out, &n, VshSrcToken(SPR_INPUT, 0, IdSwz, FALSE, FALSE));
    }

    for(DWORD i = 0; i < InstrCount; i++)
    {
        if(i == ViewportPair.offset ||
           (i == ViewportPair.scale && ViewportPair.discardScale))
        {
            NeedsPosEpilogue = true;
            continue;
        }
        if(i >= ScreenSpaceSuffix.start)
        {
            NeedsPosEpilogue = true;
            break;
        }

        const DWORD *I = &pXboxFunction[1 + i * 4];

        DWORD Mac = VshGetField(I, FLD_MAC);
        DWORD Ilu = VshGetField(I, FLD_ILU);
        DWORD MacMask = VshGetField(I, FLD_OUT_MAC_MASK);
        DWORD IluMask = VshGetField(I, FLD_OUT_ILU_MASK);
        DWORD OMask   = VshGetField(I, FLD_OUT_O_MASK);
        DWORD OutR    = VshGetField(I, FLD_OUT_R);
        DWORD Orb     = VshGetField(I, FLD_OUT_ORB);
        DWORD OutAddr = VshGetField(I, FLD_OUT_ADDRESS);
        DWORD OutMux  = VshGetField(I, FLD_OUT_MUX);
        bool  A0x     = VshGetField(I, FLD_A0X) != 0;
        if(i == ViewportPair.scale)
        {
            OMask = 0;
            NeedsPosEpilogue = true;
        }

        VshSrc SrcA{}, SrcB{}, SrcC{};
        VshDecodeSrc(I, 0, &SrcA);
        VshDecodeSrc(I, 1, &SrcB);
        VshDecodeSrc(I, 2, &SrcC);
        VshSrc StagedIluSource{ PARAM_R, ScratchPlan.pairedIlu, FALSE, { 0, 1, 2, 3 } };
        VshSrc StagedDualDestination{
            PARAM_R, ScratchPlan.dualDestination, FALSE, { 0, 1, 2, 3 }
        };

        const bool StageIluSource =
            VshNeedsPairedIluScratch(I, Mac, Ilu, MacMask, OutR, OMask, SrcC);
        const VshSrc Sources[3] = { SrcA, SrcB, SrcC };
        const bool StageDualDestination = VshNeedsDualDestinationScratch(
            I, Mac, Ilu, MacMask, IluMask, OutR, OMask, OutMux, Sources);
        if(StageIluSource)
        {
            // NV2A's paired pipelines read their operands before either result
            // is committed. Preserve C before the sequential host MAC can
            // overwrite its temporary, R12 alias, or relative-address base.
            VshEmit(Out, &n, SIO_MOV);
            VshEmit(Out, &n, VshDstToken(SPR_TEMP, ScratchPlan.pairedIlu, 0xF));
            VshEmit(Out, &n, VshEmitSrc(I, &SrcC, A0x, ScratchPlan.position));
        }

        if(Mac != MAC_NOP)
        {
            DWORD Opcode = 0;
            const VshSrc *Srcs[3] = { NULL, NULL, NULL };
            int SrcCount = 0;
            VshSrc DphResult{};

            switch(Mac)
            {
                case MAC_MOV: Opcode = SIO_MOV; Srcs[0] = &SrcA; SrcCount = 1; break;
                case MAC_MUL: Opcode = SIO_MUL; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2; break;
                case MAC_ADD: Opcode = SIO_ADD; Srcs[0] = &SrcA; Srcs[1] = &SrcC; SrcCount = 2; break;
                case MAC_MAD: Opcode = SIO_MAD; Srcs[0] = &SrcA; Srcs[1] = &SrcB; Srcs[2] = &SrcC; SrcCount = 3; break;
                case MAC_DP3: Opcode = SIO_DP3; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2; break;
                case MAC_DP4: Opcode = SIO_DP4; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2; break;
                case MAC_DPH:
                {
                    // vs.1.1 has no DPH. Compute dot(a.xyz, b.xyz) + b.w in a
                    // shader-global unused temp, then route that exact scalar
                    // through the normal NV2A temp/output masks below.
                    DphResult = { PARAM_R, ScratchPlan.dph, FALSE, { 0, 1, 2, 3 } };
                    VshSrc Bwwww = SrcB;
                    Bwwww.Swz[0] = SrcB.Swz[3];
                    Bwwww.Swz[1] = SrcB.Swz[3];
                    Bwwww.Swz[2] = SrcB.Swz[3];
                    Bwwww.Swz[3] = SrcB.Swz[3];
                    VshEmit(Out, &n, SIO_DP3);
                    VshEmit(Out, &n, VshDstToken(SPR_TEMP, ScratchPlan.dph, 0xF));
                    VshEmit(Out, &n, VshEmitSrc(I, &SrcA, A0x, ScratchPlan.position));
                    VshEmit(Out, &n, VshEmitSrc(I, &SrcB, A0x, ScratchPlan.position));
                    VshEmit(Out, &n, SIO_ADD);
                    VshEmit(Out, &n, VshDstToken(SPR_TEMP, ScratchPlan.dph, 0xF));
                    VshEmit(Out, &n, VshEmitSrc(I, &DphResult, false, ScratchPlan.position));
                    VshEmit(Out, &n, VshEmitSrc(I, &Bwwww, A0x, ScratchPlan.position));
                    Opcode = SIO_MOV;
                    Srcs[0] = &DphResult;
                    SrcCount = 1;
                    break;
                }
                case MAC_DST: Opcode = SIO_DST; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2; break;
                case MAC_MIN: Opcode = SIO_MIN; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2; break;
                case MAC_MAX: Opcode = SIO_MAX; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2; break;
                case MAC_SLT: Opcode = SIO_SLT; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2; break;
                case MAC_SGE: Opcode = SIO_SGE; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2; break;
                case MAC_ARL:
                    // mov a0.x, srcA
                    VshEmit(Out, &n, SIO_MOV);
                    VshEmit(Out, &n, VshDstToken(SPR_ADDR, 0, 0x1));
                    VshEmit(Out, &n, VshEmitSrc(I, &SrcA, false, ScratchPlan.position));
                    Opcode = 0;
                    break;
                default:
                    EmuWarning("VshDecoder: unknown MAC opcode %lu", Mac);
                    Opcode = 0;
                    break;
            }

            if(Opcode != 0 && StageDualDestination && OutMux == OMUX_MAC &&
               Mac != MAC_DPH)
            {
                VshEmit(Out, &n, Opcode);
                VshEmit(Out, &n,
                        VshDstToken(SPR_TEMP, ScratchPlan.dualDestination, 0xF));
                for(int source = 0; source < SrcCount; ++source)
                {
                    VshEmit(Out, &n,
                            VshEmitSrc(I, Srcs[source], A0x, ScratchPlan.position));
                }
                Opcode = SIO_MOV;
                Srcs[0] = &StagedDualDestination;
                SrcCount = 1;
            }

            if(Opcode != 0)
            {
                bool MacReadsPosition = false;
                for(int source = 0; source < SrcCount; ++source)
                {
                    MacReadsPosition = MacReadsPosition ||
                                       (Srcs[source]->Mux == PARAM_R && Srcs[source]->R == 12);
                }
                const bool MacOutputFirst =
                    OMask != 0 && OutMux == OMUX_MAC &&
                    (!(Orb == OUTPUT_O && OutAddr == 0) || !MacReadsPosition);
                if(MacOutputFirst)
                {
                    DWORD RegType = 0;
                    DWORD RegNum = 0;
                    if(Orb == OUTPUT_O && OutAddr == 0)
                    {
                        NeedsPosEpilogue = true;
                        VshEmit(Out, &n, Opcode);
                        VshEmit(Out, &n, VshDstToken(SPR_TEMP, ScratchPlan.position, VshPcMask(OMask)));
                        for(int source = 0; source < SrcCount; ++source)
                        {
                            VshEmit(Out, &n, VshEmitSrc(I, Srcs[source], A0x, ScratchPlan.position));
                        }
                    }
                    else if(Orb == OUTPUT_O && VshMapOutput(OutAddr, &RegType, &RegNum))
                    {
                        VshEmit(Out, &n, Opcode);
                        VshEmit(Out, &n, VshDstToken(RegType, RegNum, VshPcMask(OMask)));
                        for(int source = 0; source < SrcCount; ++source)
                        {
                            VshEmit(Out, &n, VshEmitSrc(I, Srcs[source], A0x, ScratchPlan.position));
                        }
                    }
                    else
                    {
                        EmuWarning("VshDecoder: unsupported MAC output (orb=%lu, addr=%lu) skipped", Orb, OutAddr);
                    }
                }
                // Temp-register destination (R12 = the readable oPos alias)
                if(MacMask != 0)
                {
                    if(OutR == 12)
                        NeedsPosEpilogue = true;
                    VshEmit(Out, &n, Opcode);
                    VshEmit(Out, &n, VshDstToken(SPR_TEMP, VshMapTemp(OutR, ScratchPlan.position), VshPcMask(MacMask)));
                    for(int s = 0; s < SrcCount; s++)
                        VshEmit(Out, &n, VshEmitSrc(I, Srcs[s], A0x, ScratchPlan.position));
                }
                // Output-register destination
                if(OMask != 0 && OutMux == OMUX_MAC && !MacOutputFirst)
                {
                    DWORD RegType, RegNum;
                    if(Orb == OUTPUT_O && OutAddr == 0)
                    {
                        // oPos: stage in the scratch temp so later instructions
                        // can read it back (the NV2A R12 alias); the epilogue
                        // emits the real output write.
                        NeedsPosEpilogue = true;
                        VshEmit(Out, &n, Opcode);
                        VshEmit(Out, &n, VshDstToken(SPR_TEMP, ScratchPlan.position, VshPcMask(OMask)));
                        for(int s = 0; s < SrcCount; s++)
                        {
                            VshEmit(Out, &n, VshEmitSrc(I, Srcs[s], A0x, ScratchPlan.position));
                        }
                    }
                    else if(Orb == OUTPUT_O && VshMapOutput(OutAddr, &RegType, &RegNum))
                    {
                        VshEmit(Out, &n, Opcode);
                        VshEmit(Out, &n, VshDstToken(RegType, RegNum, VshPcMask(OMask)));
                        for(int s = 0; s < SrcCount; s++)
                        {
                            VshEmit(Out, &n, VshEmitSrc(I, Srcs[s], A0x, ScratchPlan.position));
                        }
                    }
                    else
                    {
                        EmuWarning("VshDecoder: unsupported MAC output (orb=%lu, addr=%lu) skipped", Orb, OutAddr);
                    }
                }
            }
        }

        if(Ilu != ILU_NOP)
        {
            DWORD Opcode = 0;
            switch(Ilu)
            {
                case ILU_MOV: Opcode = SIO_MOV; break;
                case ILU_RCP: Opcode = SIO_RCP; break;
                case ILU_RCC: break; // rejected before emission; CPU fallback preserves the clamp
                case ILU_RSQ: Opcode = SIO_RSQ; break;
                case ILU_EXP: Opcode = SIO_EXP; break;
                case ILU_LOG: Opcode = SIO_LOG; break;
                case ILU_LIT: Opcode = SIO_LIT; break;
                default:
                    EmuWarning("VshDecoder: unknown ILU opcode %lu", Ilu);
                    break;
            }

            if(Opcode != 0)
            {
                // RCP/RSQ/EXP/LOG require a replicate swizzle on PC; the
                // microcode is normally already replicated -- enforce it.
                VshSrc SrcIlu = StageIluSource ? StagedIluSource : SrcC;
                if(Opcode == SIO_RCP || Opcode == SIO_RSQ || Opcode == SIO_EXP || Opcode == SIO_LOG)
                {
                    SrcIlu.Swz[1] = SrcIlu.Swz[0];
                    SrcIlu.Swz[2] = SrcIlu.Swz[0];
                    SrcIlu.Swz[3] = SrcIlu.Swz[0];
                }
                if(StageDualDestination && OutMux == OMUX_ILU)
                {
                    VshEmit(Out, &n, Opcode);
                    VshEmit(Out, &n,
                            VshDstToken(SPR_TEMP, ScratchPlan.dualDestination, 0xF));
                    VshEmit(Out, &n,
                            VshEmitSrc(I, &SrcIlu, A0x, ScratchPlan.position));
                    Opcode = SIO_MOV;
                    SrcIlu = StagedDualDestination;
                }

                const bool IluReadsPosition = SrcIlu.Mux == PARAM_R && SrcIlu.R == 12;
                const bool IluOutputFirst =
                    OMask != 0 && OutMux == OMUX_ILU &&
                    (!(Orb == OUTPUT_O && OutAddr == 0) || !IluReadsPosition);
                if(IluOutputFirst)
                {
                    DWORD RegType = 0;
                    DWORD RegNum = 0;
                    if(Orb == OUTPUT_O && OutAddr == 0)
                    {
                        NeedsPosEpilogue = true;
                        VshEmit(Out, &n, Opcode);
                        VshEmit(Out, &n, VshDstToken(SPR_TEMP, ScratchPlan.position, VshPcMask(OMask)));
                        VshEmit(Out, &n, VshEmitSrc(I, &SrcIlu, A0x, ScratchPlan.position));
                    }
                    else if(Orb == OUTPUT_O && VshMapOutput(OutAddr, &RegType, &RegNum))
                    {
                        VshEmit(Out, &n, Opcode);
                        VshEmit(Out, &n, VshDstToken(RegType, RegNum, VshPcMask(OMask)));
                        VshEmit(Out, &n, VshEmitSrc(I, &SrcIlu, A0x, ScratchPlan.position));
                    }
                    else
                    {
                        EmuWarning("VshDecoder: unsupported ILU output (orb=%lu, addr=%lu) skipped", Orb, OutAddr);
                    }
                }
                // Temp-register destination. When paired with an active MAC
                // that also writes a temp, the hardware ILU target is R1.
                if(IluMask != 0)
                {
                    DWORD IluR = (Mac != MAC_NOP && MacMask != 0) ? 1 : OutR;
                    if(IluR == 12)
                        NeedsPosEpilogue = true;
                    VshEmit(Out, &n, Opcode);
                    VshEmit(Out, &n, VshDstToken(SPR_TEMP, VshMapTemp(IluR, ScratchPlan.position), VshPcMask(IluMask)));
                    VshEmit(Out, &n, VshEmitSrc(I, &SrcIlu, A0x, ScratchPlan.position));
                }
                // Output-register destination
                if(OMask != 0 && OutMux == OMUX_ILU && !IluOutputFirst)
                {
                    DWORD RegType, RegNum;
                    if(Orb == OUTPUT_O && OutAddr == 0)
                    {
                        NeedsPosEpilogue = true;
                        VshEmit(Out, &n, Opcode);
                        VshEmit(Out, &n, VshDstToken(SPR_TEMP, ScratchPlan.position, VshPcMask(OMask)));
                        VshEmit(Out, &n, VshEmitSrc(I, &SrcIlu, A0x, ScratchPlan.position));
                    }
                    else if(Orb == OUTPUT_O && VshMapOutput(OutAddr, &RegType, &RegNum))
                    {
                        VshEmit(Out, &n, Opcode);
                        VshEmit(Out, &n, VshDstToken(RegType, RegNum, VshPcMask(OMask)));
                        VshEmit(Out, &n, VshEmitSrc(I, &SrcIlu, A0x, ScratchPlan.position));
                    }
                    else
                    {
                        EmuWarning("VshDecoder: unsupported ILU output (orb=%lu, addr=%lu) skipped", Orb, OutAddr);
                    }
                }
            }
        }

        if(VshGetField(I, FLD_FINAL) != 0)
            break;
    }

    // Position staged in the scratch temp (the NV2A R12/oPos alias) -- emit
    // the actual output-register write vs.1.1 requires.
    if(NeedsPosEpilogue)
    {
        static const DWORD IdSwz[4] = { 0, 1, 2, 3 };
        VshEmit(Out, &n, SIO_MOV);
        VshEmit(Out, &n, VshDstToken(SPR_RASTOUT, 0, 0xF));
        VshEmit(Out, &n, VshSrcToken(SPR_TEMP, ScratchPlan.position, IdSwz, FALSE, FALSE));
    }

    VshEmit(Out, &n, 0x0000FFFF); // end

    const VshDiagnostics::OptimizationResult optimization =
        VshDiagnostics::OptimizeD3D8Function(Out, static_cast<std::size_t>(n));
    if(optimization.valid)
    {
        n = static_cast<int>(optimization.tokenCount);
    }

    printf("VSH| optimized hash=%08X xbox_instructions=%lu before=%zu after=%zu tokens=%d\n",
           static_cast<unsigned int>(VshDiagnostics::HashXboxFunction(pXboxFunction)),
           static_cast<unsigned long>(InstrCount), optimization.beforeInstructionCount,
           optimization.afterInstructionCount, n);
    fflush(stdout);

    return Out;
}

// ******************************************************************
// * EmuVshTranslateXboxDeclaration
// ******************************************************************
// * Rewrite the Xbox vertex declaration's extended data-type codes to PC
// * D3DVSDT equivalents, in place into pPcDecl. The token framing (STREAM/
// * REG/CONST/END) is shared between the two APIs. Returns the token count.
// ******************************************************************
namespace
{
constexpr std::size_t VSH_MAX_DECLARATION_TOKENS = 128;

bool VshMapHostVertexType(DWORD xboxType, DWORD& hostType)
{
    switch(xboxType)
    {
        case 0x12: hostType = 0; return true;
        case 0x22: hostType = 1; return true;
        case 0x32: hostType = 2; return true;
        case 0x42: hostType = 3; return true;
        case 0x40: hostType = 4; return true;
        case 0x25: hostType = 6; return true;
        case 0x45: hostType = 7; return true;
        default:
            if(xboxType <= 0x07)
            {
                hostType = xboxType;
                return true;
            }
            return false;
    }
}

bool VshIsCpuVertexType(DWORD type)
{
    switch(type)
    {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x11:
        case 0x12:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x21:
        case 0x22:
        case 0x24:
        case 0x25:
        case 0x31:
        case 0x32:
        case 0x34:
        case 0x35:
        case 0x40:
        case 0x41:
        case 0x42:
        case 0x44:
        case 0x45:
        case 0x72: return true;
        default: return false;
    }
}
} // namespace

XTL::VshDiagnostics::DeclarationTranslationResult
XTL::VshDiagnostics::TranslateXboxDeclaration(const void* xboxDeclarationData,
                                              void* d3dDeclarationData,
                                              std::size_t maxTokens)
{
    DeclarationTranslationResult result;
    const DWORD* xboxDeclaration = static_cast<const DWORD*>(xboxDeclarationData);
    DWORD* d3dDeclaration = static_cast<DWORD*>(d3dDeclarationData);
    if(xboxDeclaration == nullptr || d3dDeclaration == nullptr || maxTokens == 0)
    {
        result.reason = "invalid_declaration_arguments";
        return result;
    }

    result.disposition = XboxFunctionDisposition::TranslateToHost;
    for(std::size_t inputIndex = 0; inputIndex < VSH_MAX_DECLARATION_TOKENS;)
    {
        if(result.tokenCount >= maxTokens)
        {
            result.disposition = XboxFunctionDisposition::Reject;
            result.tokenCount = 0;
            result.reason = "declaration_capacity";
            return result;
        }

        const DWORD token = xboxDeclaration[inputIndex++];
        if(token == 0xFFFFFFFFu)
        {
            d3dDeclaration[result.tokenCount++] = token;
            if(result.disposition == XboxFunctionDisposition::ExecuteOnCpu &&
               !result.cpuCompatible)
            {
                result.disposition = XboxFunctionDisposition::Reject;
                result.tokenCount = 0;
                result.reason = result.cpuIncompatibilityReason;
                return result;
            }
            if(result.disposition == XboxFunctionDisposition::ExecuteOnCpu)
            {
                result.reason = "declaration_cpu_vertex_type";
            }
            return result;
        }

        const DWORD tokenType = token >> 29;
        if(tokenType == 7 || tokenType == 6)
        {
            result.disposition = XboxFunctionDisposition::Reject;
            result.tokenCount = 0;
            result.reason = "malformed_declaration_token";
            return result;
        }

        DWORD translatedToken = token;
        if(tokenType == 3)
        {
            result.cpuCompatible = false;
            result.cpuIncompatibilityReason = "declaration_cpu_tessellator";
        }
        else if(tokenType == 5)
        {
            result.cpuCompatible = false;
            result.cpuIncompatibilityReason = "declaration_cpu_extension";
        }
        if(tokenType == 2 && (token & 0x10000000u) == 0)
        {
            const DWORD reg = token & 0x1Fu;
            const DWORD xboxType = (token >> 16) & 0xFFu;
            DWORD hostType = 0;
            if(reg >= 16)
            {
                result.disposition = XboxFunctionDisposition::Reject;
                result.tokenCount = 0;
                result.reason = "declaration_register_range";
                return result;
            }
            if(VshMapHostVertexType(xboxType, hostType))
            {
                translatedToken = (token & 0xE000FFFFu) | (hostType << 16);
            }
            else if(VshIsCpuVertexType(xboxType))
            {
                result.disposition = XboxFunctionDisposition::ExecuteOnCpu;
            }
            else
            {
                result.disposition = XboxFunctionDisposition::Reject;
                result.tokenCount = 0;
                result.reason = "unsupported_vertex_type";
                return result;
            }
        }

        d3dDeclaration[result.tokenCount++] = translatedToken;

        std::size_t payloadCount = 0;
        if(tokenType == 4)
        {
            const std::size_t constantCount = (token >> 25) & 0xFu;
            const std::size_t constantAddress = token & 0x7Fu;
            if(constantAddress > 95 || constantCount > 96 - constantAddress)
            {
                result.disposition = XboxFunctionDisposition::Reject;
                result.tokenCount = 0;
                result.reason = "declaration_constant_range";
                return result;
            }
            payloadCount = constantCount * 4;
        }
        else if(tokenType == 5)
        {
            payloadCount = (token >> 24) & 0x1Fu;
        }
        if(payloadCount > VSH_MAX_DECLARATION_TOKENS - inputIndex ||
           payloadCount > maxTokens - result.tokenCount)
        {
            result.disposition = XboxFunctionDisposition::Reject;
            result.tokenCount = 0;
            result.reason = "declaration_capacity";
            return result;
        }
        for(std::size_t payloadIndex = 0; payloadIndex < payloadCount; ++payloadIndex)
        {
            d3dDeclaration[result.tokenCount++] = xboxDeclaration[inputIndex++];
        }
    }

    result.disposition = XboxFunctionDisposition::Reject;
    result.tokenCount = 0;
    result.reason = "declaration_missing_end";
    return result;
}

int XTL::EmuVshTranslateXboxDeclaration(const DWORD* pXboxDecl, DWORD* pPcDecl, int MaxTokens)
{
    if(MaxTokens <= 0)
    {
        return 0;
    }
    const VshDiagnostics::DeclarationTranslationResult result =
        VshDiagnostics::TranslateXboxDeclaration(pXboxDecl, pPcDecl,
                                                 static_cast<std::size_t>(MaxTokens));
    return result.disposition == VshDiagnostics::XboxFunctionDisposition::Reject
               ? 0
               : static_cast<int>(result.tokenCount);
}

bool XTL::VshDiagnostics::ApplyXboxDeclarationConstants(const void* xboxDeclarationData,
                                                        const float* baseConstants,
                                                        float* outputConstants,
                                                        std::size_t constantFloatCount)
{
    constexpr std::size_t HardwareConstantFloatCount = 192 * 4;
    if(xboxDeclarationData == nullptr || baseConstants == nullptr || outputConstants == nullptr ||
       constantFloatCount < HardwareConstantFloatCount)
    {
        return false;
    }

    std::memmove(outputConstants, baseConstants, HardwareConstantFloatCount * sizeof(float));
    const DWORD* declaration = static_cast<const DWORD*>(xboxDeclarationData);
    for(std::size_t tokenIndex = 0; tokenIndex < VSH_MAX_DECLARATION_TOKENS;)
    {
        const DWORD token = declaration[tokenIndex++];
        if(token == 0xFFFFFFFFu)
        {
            return true;
        }
        const DWORD tokenType = token >> 29;
        if(tokenType == 6 || tokenType == 7)
        {
            return false;
        }

        std::size_t payloadCount = 0;
        if(tokenType == 4)
        {
            const std::size_t constantCount = (token >> 25) & 0xFu;
            const std::size_t constantAddress = token & 0x7Fu;
            if(constantAddress > 95 || constantCount > 96 - constantAddress)
            {
                return false;
            }
            payloadCount = constantCount * 4;
            if(payloadCount > VSH_MAX_DECLARATION_TOKENS - tokenIndex)
            {
                return false;
            }
            std::memcpy(&outputConstants[(96 + constantAddress) * 4],
                        &declaration[tokenIndex], payloadCount * sizeof(DWORD));
        }
        else if(tokenType == 5)
        {
            payloadCount = (token >> 24) & 0x1Fu;
        }
        if(payloadCount > VSH_MAX_DECLARATION_TOKENS - tokenIndex)
        {
            return false;
        }
        tokenIndex += payloadCount;
    }
    return false;
}

namespace
{
std::size_t VshXboxVertexTypeSize(DWORD type)
{
    switch(type)
    {
        case 0x00:
        case 0x04:
        case 0x05:
        case 0x06: return 4;
        case 0x01:
        case 0x07: return 8;
        case 0x02: return 12;
        case 0x03: return 16;
        case 0x14: return 1;
        case 0x11:
        case 0x15:
        case 0x24: return 2;
        case 0x34: return 3;
        case 0x12:
        case 0x16:
        case 0x21:
        case 0x25:
        case 0x40:
        case 0x44: return 4;
        case 0x31:
        case 0x35: return 6;
        case 0x22:
        case 0x41:
        case 0x45: return 8;
        case 0x32:
        case 0x72: return 12;
        case 0x42: return 16;
        default: return (std::numeric_limits<std::size_t>::max)();
    }
}

float VshNormalizeShort(std::int16_t value)
{
    if(value == (std::numeric_limits<std::int16_t>::min)())
    {
        return -1.0f;
    }
    return static_cast<float>(value) / 32767.0f;
}

std::int32_t VshSignExtend(DWORD value, unsigned int bitCount)
{
    const DWORD signBit = 1u << (bitCount - 1);
    const DWORD mask = (1u << bitCount) - 1u;
    value &= mask;
    return static_cast<std::int32_t>((value ^ signBit) - signBit);
}

float VshNormalizePacked(std::int32_t value, unsigned int bitCount)
{
    const std::int32_t minimum = -(1 << (bitCount - 1));
    if(value == minimum)
    {
        return -1.0f;
    }
    return static_cast<float>(value) / static_cast<float>((1 << (bitCount - 1)) - 1);
}

void VshDecodeXboxVertexValue(const std::uint8_t* source, DWORD type, float output[4])
{
    if(type == 0x40 || type == 0x04)
    {
        DWORD color = 0;
        std::memcpy(&color, source, sizeof(color));
        output[0] = static_cast<float>((color >> 16) & 0xFFu) / 255.0f;
        output[1] = static_cast<float>((color >> 8) & 0xFFu) / 255.0f;
        output[2] = static_cast<float>(color & 0xFFu) / 255.0f;
        output[3] = static_cast<float>((color >> 24) & 0xFFu) / 255.0f;
        return;
    }
    if(type <= 0x03)
    {
        const DWORD floatCount = type + 1;
        float values[4] = {};
        std::memcpy(values, source, floatCount * sizeof(float));
        for(DWORD component = 0; component < floatCount; ++component)
        {
            output[component] = values[component];
        }
        return;
    }
    if(type == 0x05)
    {
        for(DWORD component = 0; component < 4; ++component)
        {
            output[component] = static_cast<float>(source[component]);
        }
        return;
    }
    if(type == 0x06 || type == 0x07)
    {
        const DWORD componentCount = type == 0x06 ? 2 : 4;
        for(DWORD component = 0; component < componentCount; ++component)
        {
            std::int16_t value = 0;
            std::memcpy(&value, source + component * sizeof(value), sizeof(value));
            output[component] = static_cast<float>(value);
        }
        return;
    }
    if(type == 0x16)
    {
        DWORD packed = 0;
        std::memcpy(&packed, source, sizeof(packed));
        output[0] = VshNormalizePacked(VshSignExtend(packed, 11), 11);
        output[1] = VshNormalizePacked(VshSignExtend(packed >> 11, 11), 11);
        output[2] = VshNormalizePacked(VshSignExtend(packed >> 22, 10), 10);
        return;
    }

    const DWORD componentCount = (type >> 4) & 7u;
    if((type & 0xFu) == 2u)
    {
        const DWORD floatCount = type == 0x72 ? 3u : componentCount;
        float values[4] = {};
        std::memcpy(values, source, floatCount * sizeof(float));
        output[0] = values[0];
        output[1] = floatCount > 1 ? values[1] : 0.0f;
        output[2] = type == 0x72 ? 0.0f : (floatCount > 2 ? values[2] : 0.0f);
        output[3] = type == 0x72 ? values[2] : (floatCount > 3 ? values[3] : 1.0f);
        return;
    }
    if((type & 0xFu) == 5u || (type & 0xFu) == 1u)
    {
        for(DWORD component = 0; component < componentCount && component < 4; ++component)
        {
            std::int16_t value = 0;
            std::memcpy(&value, source + component * sizeof(value), sizeof(value));
            output[component] = (type & 0xFu) == 1u ? VshNormalizeShort(value) : static_cast<float>(value);
        }
        return;
    }
    if((type & 0xFu) == 4u)
    {
        for(DWORD component = 0; component < componentCount && component < 4; ++component)
        {
            output[component] = static_cast<float>(source[component]) / 255.0f;
        }
    }
}
} // namespace

bool XTL::VshDiagnostics::DecodeXboxVertex(const void* xboxDeclarationData, const VertexStreamView* streams,
                                           std::size_t streamCount, std::size_t vertexIndex,
                                           float* inputRegisters, std::size_t inputFloatCount)
{
    if(xboxDeclarationData == nullptr || streams == nullptr || inputRegisters == nullptr ||
       inputFloatCount < 16 * 4)
    {
        return false;
    }

    for(std::size_t reg = 0; reg < 16; ++reg)
    {
        inputRegisters[reg * 4] = 0.0f;
        inputRegisters[reg * 4 + 1] = 0.0f;
        inputRegisters[reg * 4 + 2] = 0.0f;
        inputRegisters[reg * 4 + 3] = 1.0f;
    }

    const DWORD* declaration = static_cast<const DWORD*>(xboxDeclarationData);
    std::array<std::size_t, 16> streamOffsets{};
    DWORD currentStream = 0;
    const auto consumeStreamBytes = [&](DWORD streamIndex, std::size_t byteCount)
    {
        if(streamIndex >= streamCount)
        {
            return false;
        }
        const VertexStreamView& stream = streams[streamIndex];
        if(stream.data == nullptr || stream.stride == 0 ||
           vertexIndex > (std::numeric_limits<std::size_t>::max)() / stream.stride)
        {
            return false;
        }
        const std::size_t vertexOffset = vertexIndex * stream.stride;
        const std::size_t streamOffset = streamOffsets[streamIndex];
        if(streamOffset > stream.stride || byteCount > stream.stride - streamOffset ||
           vertexOffset > stream.byteSize || streamOffset > stream.byteSize - vertexOffset ||
           byteCount > stream.byteSize - vertexOffset - streamOffset)
        {
            return false;
        }
        streamOffsets[streamIndex] += byteCount;
        return true;
    };
    for(std::size_t tokenIndex = 0; tokenIndex < 128; ++tokenIndex)
    {
        const DWORD token = declaration[tokenIndex];
        if(token == 0xFFFFFFFFu)
        {
            return true;
        }
        const DWORD tokenType = token >> 29;
        if(tokenType == 1u)
        {
            if((token & 0x10000000u) != 0)
            {
                return false;
            }
            currentStream = token & 0xFu;
            continue;
        }
        if(tokenType == 3u || tokenType == 5u || tokenType == 6u || tokenType == 7u)
        {
            return false;
        }
        if(tokenType == 4u)
        {
            const std::size_t payloadCount = static_cast<std::size_t>((token >> 25) & 0xFu) * 4;
            if(payloadCount > 127 - tokenIndex)
            {
                return false;
            }
            tokenIndex += payloadCount;
            continue;
        }
        if(tokenType != 2u)
        {
            continue;
        }
        if((token & 0x10000000u) != 0)
        {
            const std::size_t skipBytes = static_cast<std::size_t>((token >> 16) & 0xFu) * 4;
            if(!consumeStreamBytes(currentStream, skipBytes))
            {
                return false;
            }
            continue;
        }

        const DWORD reg = token & 0x1Fu;
        const DWORD type = (token >> 16) & 0xFFu;
        const std::size_t valueSize = VshXboxVertexTypeSize(type);
        if(reg >= 16 || valueSize == (std::numeric_limits<std::size_t>::max)())
        {
            return false;
        }
        if(valueSize == 0)
        {
            continue;
        }

        const std::size_t sourceOffset = streamOffsets[currentStream];
        if(!consumeStreamBytes(currentStream, valueSize))
        {
            return false;
        }

        const VertexStreamView& stream = streams[currentStream];
        const std::size_t vertexOffset = vertexIndex * stream.stride;
        const std::uint8_t* source = static_cast<const std::uint8_t*>(stream.data) + vertexOffset +
                                     sourceOffset;
        VshDecodeXboxVertexValue(source, type, &inputRegisters[reg * 4]);
    }
    return false;
}

// ******************************************************************
// * EmuVshExecuteProgram - CPU vertex-program interpreter (Phase 2)
// ******************************************************************
// * Execute NV2A vertex-program microcode on the host to transform one
// * vertex for the software rasterizer. Where EmuVshRecompileXboxFunction
// * re-emits the microcode for a host GPU, this runs it directly: 16 input
// * attribute registers in, clip-space oPos + oD0 diffuse out. Constants are
// * indexed straight by the microcode CONST field (hardware constant memory
// * 0..191); the readable R12/oPos alias, dual MAC+ILU issue, swizzles,
// * negation, write masks and A0-relative constant addressing are honored.
// ******************************************************************

// Read a source operand: select register bank by mux, apply swizzle + negate.
static void VshExecReadSrc(const DWORD *I, const VshSrc *S, DWORD Vfield, bool Relative,
                           int A0, const float Reg[13][4], const float *Const,
                           const float *Input, float Out[4])
{
    float Tmp[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    switch(S->Mux)
    {
        case PARAM_R:
        {
            DWORD r = S->R > 12 ? 12 : S->R;
            Tmp[0] = Reg[r][0]; Tmp[1] = Reg[r][1]; Tmp[2] = Reg[r][2]; Tmp[3] = Reg[r][3];
            break;
        }
        case PARAM_V:
        {
            DWORD v = Vfield & 15;
            const float *in = &Input[v * 4];
            Tmp[0] = in[0]; Tmp[1] = in[1]; Tmp[2] = in[2]; Tmp[3] = in[3];
            break;
        }
        case PARAM_C:
        {
            int idx = (int)VshGetField(I, FLD_CONST);
            if(Relative) idx += A0;
            if(idx < 0) idx = 0;
            if(idx > 191) idx = 191;
            const float *c = &Const[idx * 4];
            Tmp[0] = c[0]; Tmp[1] = c[1]; Tmp[2] = c[2]; Tmp[3] = c[3];
            break;
        }
        default:
            break;
    }

    for(int c = 0; c < 4; c++)
        Out[c] = Tmp[S->Swz[c] & 3];
    if(S->Neg)
        for(int c = 0; c < 4; c++)
            Out[c] = -Out[c];
}

// NV2A write mask: bit3=X bit2=Y bit1=Z bit0=W.
static void VshExecWriteMasked(float Dst[4], const float Src[4], DWORD NvMask)
{
    if(NvMask & 0x8) Dst[0] = Src[0];
    if(NvMask & 0x4) Dst[1] = Src[1];
    if(NvMask & 0x2) Dst[2] = Src[2];
    if(NvMask & 0x1) Dst[3] = Src[3];
}

static void VshExecMac(DWORD Op, const float A[4], const float B[4], const float C[4], float R[4])
{
    switch(Op)
    {
        case MAC_MOV: R[0]=A[0]; R[1]=A[1]; R[2]=A[2]; R[3]=A[3]; break;
        case MAC_MUL: for(int i=0;i<4;i++) R[i]=A[i]*B[i]; break;
        case MAC_ADD: for(int i=0;i<4;i++) R[i]=A[i]+C[i]; break;
        case MAC_MAD: for(int i=0;i<4;i++) R[i]=A[i]*B[i]+C[i]; break;
        case MAC_DP3: { float d=A[0]*B[0]+A[1]*B[1]+A[2]*B[2]; R[0]=R[1]=R[2]=R[3]=d; } break;
        case MAC_DP4: { float d=A[0]*B[0]+A[1]*B[1]+A[2]*B[2]+A[3]*B[3]; R[0]=R[1]=R[2]=R[3]=d; } break;
        case MAC_DPH: { float d=A[0]*B[0]+A[1]*B[1]+A[2]*B[2]+B[3]; R[0]=R[1]=R[2]=R[3]=d; } break;
        case MAC_DST: R[0]=1.0f; R[1]=A[1]*B[1]; R[2]=A[2]; R[3]=B[3]; break;
        case MAC_MIN: for(int i=0;i<4;i++) R[i]=A[i]<B[i]?A[i]:B[i]; break;
        case MAC_MAX: for(int i=0;i<4;i++) R[i]=A[i]>B[i]?A[i]:B[i]; break;
        case MAC_SLT: for(int i=0;i<4;i++) R[i]=A[i]<B[i]?1.0f:0.0f; break;
        case MAC_SGE: for(int i=0;i<4;i++) R[i]=A[i]>=B[i]?1.0f:0.0f; break;
        default: R[0]=R[1]=R[2]=R[3]=0.0f; break;
    }
}

// Freestanding float math: this old Cxbx links against a CRT whose libm float
// symbols (sqrtf/powf/logf) collide at link time, so the interpreter carries its
// own approximations. Accuracy is ample for the rare ILU transcendentals; the
// hot path (DP4/MUL/MAD/RCP) uses none of these.
static int VshFloorI(float x)
{
    int i = (int)x;
    if((float)i > x) i--;
    return i;
}

static float VshInvSqrt(float x)
{
    if(x <= 0.0f) return 0.0f;
    float xhalf = 0.5f * x;
    int i; memcpy(&i, &x, 4);
    i = 0x5f3759df - (i >> 1);           // fast inverse-sqrt seed
    float y; memcpy(&y, &i, 4);
    y = y * (1.5f - xhalf * y * y);      // Newton refine x3 -> ~1e-6 rel error
    y = y * (1.5f - xhalf * y * y);
    y = y * (1.5f - xhalf * y * y);
    return y;
}

static float VshLog2(float x)
{
    if(x <= 0.0f)
    {
        return 0.0f;
    }
    std::uint32_t bits;
    memcpy(&bits, &x, 4);
    const int exponent = static_cast<int>((bits >> 23) & 0xFFu) - 127;
    bits = (bits & 0x807FFFFFu) | 0x3F800000u; // mantissa m in [1,2)
    float m;
    memcpy(&m, &bits, 4);
    const float y = (m - 1.0f) / (m + 1.0f);
    const float y2 = y * y;
    const float series =
        y * (1.0f + y2 * (1.0f / 3.0f +
                          y2 * (1.0f / 5.0f +
                                y2 * (1.0f / 7.0f +
                                      y2 * (1.0f / 9.0f + y2 * (1.0f / 11.0f))))));
    return static_cast<float>(exponent) + 2.8853900818f * series;
}

static float VshExp2(float x)
{
    if(x < -126.0f)
    {
        return 0.0f;
    }
    if(x > 126.0f)
    {
        return 3.4e38f;
    }
    const int integer = VshFloorI(x);
    const float fraction = x - static_cast<float>(integer);
    const float polynomial =
        1.0f +
        fraction *
            (0.6931471806f +
             fraction *
                 (0.2402265070f +
                  fraction *
                      (0.0555041087f +
                       fraction *
                           (0.0096181291f +
                            fraction *
                                (0.0013333558f +
                                 fraction * (0.0001540353f + fraction * 0.0000152527f))))));
    const int bits = (integer + 127) << 23;
    float scale;
    memcpy(&scale, &bits, 4);
    return polynomial * scale;
}

// ILU scalar op on the C source (already swizzled). LIT writes a full vec4.
static void VshExecIlu(DWORD Op, const float C[4], float R[4])
{
    static constexpr float RccMinimumMagnitude = 0x1p-64f;
    float s = C[0];
    float r = 0.0f;
    switch(Op)
    {
        case ILU_MOV: R[0]=C[0]; R[1]=C[1]; R[2]=C[2]; R[3]=C[3]; return;
        case ILU_RCP: r = (s != 0.0f) ? 1.0f / s : 0.0f; break;
        case ILU_RCC:
        {
            float a = s;
            if(a >= 0.0f)
            {
                if(a < RccMinimumMagnitude)
                {
                    a = RccMinimumMagnitude;
                }
            }
            else if(a > -RccMinimumMagnitude)
            {
                a = -RccMinimumMagnitude;
            }
            r = 1.0f / a;
            break;
        }
        case ILU_RSQ: { float a = s < 0.0f ? -s : s; r = VshInvSqrt(a); } break;
        case ILU_EXP: r = VshExp2(s); break;
        case ILU_LOG: r = VshLog2(s); break;
        case ILU_LIT:
        {
            float diffuse = C[0], specular = C[1], power = C[3];
            if(power < -127.9961f) power = -127.9961f;
            if(power >  127.9961f) power =  127.9961f;
            R[0] = 1.0f;
            R[1] = diffuse > 0.0f ? diffuse : 0.0f;
            R[2] = (diffuse > 0.0f && specular > 0.0f) ? VshExp2(power * VshLog2(specular)) : 0.0f;
            R[3] = 1.0f;
            return;
        }
        default: break;
    }
    R[0] = R[1] = R[2] = R[3] = r;
}

// Destination vector for an output-register address (NV2A HardwareOutputRegisters
// order). oPos is the R12 alias so later instructions can read it back.
static float *VshExecOutputDst(DWORD Addr, float Reg[13][4], float Col[2][4],
                               float Tex[4][4], float Fog[4], float Pts[4])
{
    switch(Addr)
    {
        case 0:  return Reg[12];      // oPos (R12 alias)
        case 3:  return Col[0];       // oD0
        case 4: return Col[1];        // oD1
        case 5: return Fog;           // oFog
        case 6: return Pts;           // oPts
        case 9:
        case 10:
        case 11:
        case 12: return Tex[Addr - 9]; // oT0..3
        default: return NULL;
    }
}

static bool VshExecuteProgramInternal(const DWORD* Program, int InstrCount, int Start,
                                      const float* Const, const float* Input,
                                      float* OutPos, float* OutColors,
                                      std::size_t OutColorFloatCount, float* OutTexCoords,
                                      std::size_t OutTexCoordFloatCount,
                                      XTL::VshDiagnostics::RasterOutputs* OutRaster,
                                      bool ElideViewportTransform)
{
    if(Program == NULL || InstrCount <= 0)
        return false;

    float Reg[13][4];
    for(int r = 0; r < 13; r++)
    {
        Reg[r][0] = Reg[r][1] = Reg[r][2] = Reg[r][3] = 0.0f;
    }
    Reg[12][0] = Input[0];
    Reg[12][1] = Input[1];
    Reg[12][2] = Input[2];
    Reg[12][3] = Input[3];
    float Col[2][4] = { { 0, 0, 0, 1 }, { 0, 0, 0, 1 } };
    float Tex[4][4] = { { 0 } };
    float Fog[4] = { 0, 0, 0, 0 };
    float Pts[4] = { 0, 0, 0, 0 };
    std::uint8_t FogWriteMask = 0;
    std::uint8_t PtsWriteMask = 0;
    int A0 = 0;

    const VshScreenSpaceSuffix screenSpaceSuffix =
        VshClassifyScreenSpaceSuffix(Program, static_cast<std::size_t>(InstrCount));
    const VshViewportPair viewportPair =
        VshFindViewportScaleAddPair(Program, static_cast<std::size_t>(InstrCount));

    if(Start < 0) Start = 0;
    for(int pc = Start; pc < InstrCount; pc++)
    {
        if(ElideViewportTransform &&
           (static_cast<std::size_t>(pc) == viewportPair.offset ||
            (static_cast<std::size_t>(pc) == viewportPair.scale &&
             viewportPair.discardScale)))
        {
            continue;
        }
        if(ElideViewportTransform &&
           static_cast<std::size_t>(pc) >= screenSpaceSuffix.start)
        {
            break;
        }

        const DWORD* I = &Program[pc * 4];

        DWORD Mac = VshGetField(I, FLD_MAC);
        DWORD Ilu = VshGetField(I, FLD_ILU);
        DWORD MacMask = VshGetField(I, FLD_OUT_MAC_MASK);
        DWORD IluMask = VshGetField(I, FLD_OUT_ILU_MASK);
        DWORD OMask = VshGetField(I, FLD_OUT_O_MASK);
        DWORD OutR = VshGetField(I, FLD_OUT_R);
        DWORD Orb = VshGetField(I, FLD_OUT_ORB);
        DWORD OutAddr = VshGetField(I, FLD_OUT_ADDRESS);
        DWORD OutMux = VshGetField(I, FLD_OUT_MUX);
        bool Relative = VshGetField(I, FLD_A0X) != 0;
        DWORD Vfield = VshGetField(I, FLD_V);
        bool Final = VshGetField(I, FLD_FINAL) != 0;
        if(ElideViewportTransform &&
           static_cast<std::size_t>(pc) == viewportPair.scale)
        {
            OMask = 0;
        }

        VshSrc SA{}, SB{}, SC{};
        VshDecodeSrc(I, 0, &SA);
        VshDecodeSrc(I, 1, &SB);
        VshDecodeSrc(I, 2, &SC);
        float A[4], B[4], C[4];
        VshExecReadSrc(I, &SA, Vfield, Relative, A0, Reg, Const, Input, A);
        VshExecReadSrc(I, &SB, Vfield, Relative, A0, Reg, Const, Input, B);
        VshExecReadSrc(I, &SC, Vfield, Relative, A0, Reg, Const, Input, C);

        if(Mac == MAC_ARL)
        {
            A0 = VshFloorI(A[0]);
        }
        else if(Mac != MAC_NOP)
        {
            float Rm[4];
            VshExecMac(Mac, A, B, C, Rm);
            if(MacMask != 0)
                VshExecWriteMasked(Reg[OutR > 12 ? 12 : OutR], Rm, MacMask);
            if(OMask != 0 && OutMux == OMUX_MAC && Orb == OUTPUT_O)
            {
                float* d = VshExecOutputDst(OutAddr, Reg, Col, Tex, Fog, Pts);
                if(d != nullptr)
                {
                    VshExecWriteMasked(d, Rm, OMask);
                    if(OutAddr == 5)
                    {
                        FogWriteMask |= static_cast<std::uint8_t>(OMask);
                    }
                    else if(OutAddr == 6)
                    {
                        PtsWriteMask |= static_cast<std::uint8_t>(OMask);
                    }
                }
            }
        }

        if(Ilu != ILU_NOP)
        {
            float Ri[4];
            VshExecIlu(Ilu, C, Ri);
            DWORD IluR = (Mac != MAC_NOP && MacMask != 0) ? 1 : OutR;
            if(IluMask != 0)
                VshExecWriteMasked(Reg[IluR > 12 ? 12 : IluR], Ri, IluMask);
            if(OMask != 0 && OutMux == OMUX_ILU && Orb == OUTPUT_O)
            {
                float* d = VshExecOutputDst(OutAddr, Reg, Col, Tex, Fog, Pts);
                if(d != nullptr)
                {
                    VshExecWriteMasked(d, Ri, OMask);
                    if(OutAddr == 5)
                    {
                        FogWriteMask |= static_cast<std::uint8_t>(OMask);
                    }
                    else if(OutAddr == 6)
                    {
                        PtsWriteMask |= static_cast<std::uint8_t>(OMask);
                    }
                }
            }
        }

        if(Final)
            break;
    }

    // oPos lives in the R12 alias; diffuse in oD0; texcoord0 in oT0.
    OutPos[0] = Reg[12][0];
    OutPos[1] = Reg[12][1];
    OutPos[2] = Reg[12][2];
    OutPos[3] = Reg[12][3];
    if(OutColors != nullptr)
    {
        const std::size_t copyCount = std::min<std::size_t>(OutColorFloatCount, 2 * 4);
        std::memcpy(OutColors, Col, copyCount * sizeof(float));
    }
    if(OutTexCoords != nullptr)
    {
        const std::size_t copyCount = std::min<std::size_t>(OutTexCoordFloatCount, 4 * 4);
        std::memcpy(OutTexCoords, Tex, copyCount * sizeof(float));
    }
    if(OutRaster != nullptr)
    {
        std::memcpy(OutRaster->fog, Fog, sizeof(Fog));
        std::memcpy(OutRaster->pointSize, Pts, sizeof(Pts));
        OutRaster->fogWriteMask = FogWriteMask;
        OutRaster->pointSizeWriteMask = PtsWriteMask;
    }
    return true;
}

extern "C" bool EmuVshExecuteProgram(const DWORD* Program, int InstrCount, int Start,
                                     const float* Const, const float* Input,
                                     float* OutPos, float* OutCol0, float* OutTex0)
{
    return VshExecuteProgramInternal(Program, InstrCount, Start, Const, Input, OutPos, OutCol0,
                                     OutCol0 == nullptr ? 0 : 4, OutTex0,
                                     OutTex0 == nullptr ? 0 : 4, nullptr, true);
}

extern "C" bool EmuVshExecuteProgramRaster(const DWORD* Program, int InstrCount, int Start,
                                            const float* Const, const float* Input,
                                            float* OutPos, float* OutColors,
                                            float* OutTexCoords)
{
    return VshExecuteProgramInternal(Program, InstrCount, Start, Const, Input, OutPos,
                                     OutColors, OutColors == nullptr ? 0 : 8,
                                     OutTexCoords, OutTexCoords == nullptr ? 0 : 16,
                                     nullptr, false);
}

bool XTL::VshDiagnostics::ExecuteXboxVertexShader(const void* xboxFunctionData, const float* constants,
                                                  const float* inputRegisters, float* outputPosition,
                                                  float* outputColors, std::size_t outputColorFloatCount,
                                                  float* outputTexCoords,
                                                  std::size_t outputTexCoordFloatCount,
                                                  RasterOutputs* outputRaster)
{
    const DWORD* xboxFunction = static_cast<const DWORD*>(xboxFunctionData);
    if(xboxFunction == nullptr || constants == nullptr || inputRegisters == nullptr ||
       outputPosition == nullptr || outputColors == nullptr ||
       (xboxFunction[0] & 0xFFFFu) != VSH_XBOX_VERSION)
    {
        return false;
    }
    std::string dispositionReason;
    if(ClassifyXboxFunction(xboxFunction, dispositionReason) == XboxFunctionDisposition::Reject)
    {
        return false;
    }
    const std::size_t instructionCount = VshXboxInstructionCount(xboxFunction);
    return VshExecuteProgramInternal(&xboxFunction[1], static_cast<int>(instructionCount), 0, constants,
                                     inputRegisters, outputPosition, outputColors,
                                     outputColorFloatCount, outputTexCoords,
                                     outputTexCoordFloatCount, outputRaster, true);
}
