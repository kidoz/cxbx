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

// NV2A temp R12 is a readable alias of the position output (oPos); vs.1.1
// outputs are write-only and temps stop at r11. Route R12 through scratch
// temp r11 and have the recompiler emit a final `mov oPos, r11`.
#define VSH_POS_SCRATCH 11

static DWORD VshMapTemp(DWORD R)
{
    return (R == 12) ? VSH_POS_SCRATCH : R;
}

// Emit a PC source token for a decoded operand. Constants live at hardware
// index (API register + 96) in the microcode, so unbias them here.
static DWORD VshEmitSrc(const DWORD *I, const VshSrc *Src, bool Relative)
{
    switch(Src->Mux)
    {
        case PARAM_R:
            return VshSrcToken(SPR_TEMP, VshMapTemp(Src->R), Src->Swz, Src->Neg, FALSE);
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
    const std::size_t maxD3dTokens = 4 + VshXboxInstructionCount(xboxFunction) * 20;
    return ValidateD3D8Function(d3dFunction, maxD3dTokens);
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
        std::ostringstream line;
        line << "pc=" << std::dec << std::setw(3) << std::setfill('0') << index
             << " raw=" << VshHex(instruction[0]) << ',' << VshHex(instruction[1]) << ','
             << VshHex(instruction[2]) << ',' << VshHex(instruction[3])
             << " mac=" << VshMacName(VshGetField(instruction, FLD_MAC))
             << " ilu=" << VshIluName(VshGetField(instruction, FLD_ILU))
             << " c=" << VshGetField(instruction, FLD_CONST)
             << " v=" << VshGetField(instruction, FLD_V)
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

    const std::uint32_t hash = HashXboxFunction(xboxFunction);
    const std::size_t xboxInstructionCount = VshXboxInstructionCount(xboxFunction);
    const std::size_t maxD3dTokens = 4 + xboxInstructionCount * 20;
    const ValidationResult validation = ValidateD3D8Translation(xboxFunction, d3dFunction);
    std::fprintf(stream, "VSH| rejected hash=%08X xbox_instructions=%zu validation=%s at=%zu reason=%s\n",
                 hash, xboxInstructionCount, validation.valid ? "pass" : "fail",
                 validation.instructionIndex, validation.message.c_str());

    for(const std::string& line : DecodeXboxFunction(xboxFunction))
    {
        std::fprintf(stream, "VSH| nv2a hash=%08X %s\n", hash, line.c_str());
    }
    for(const std::string& line : DecodeD3D8Function(d3dFunction, maxD3dTokens))
    {
        std::fprintf(stream, "VSH| d3d8 hash=%08X %s\n", hash, line.c_str());
    }

    if(xboxDeclaration == nullptr || d3dDeclaration == nullptr)
    {
        std::fprintf(stream, "VSH| declaration hash=%08X unavailable\n", hash);
        std::fflush(stream);
        return;
    }

    for(std::size_t index = 0; index < 128; ++index)
    {
        std::fprintf(stream, "VSH| declaration hash=%08X token=%zu xbox=%08X d3d8=%08X\n",
                     hash, index, static_cast<unsigned int>(xboxDeclaration[index]),
                     static_cast<unsigned int>(d3dDeclaration[index]));
        if(xboxDeclaration[index] == 0xFFFFFFFFu || d3dDeclaration[index] == 0xFFFFFFFFu)
        {
            break;
        }
    }
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

    DWORD InstrCount = (pXboxFunction[0] >> 16) & 0xFFFF;
    if(InstrCount == 0 || InstrCount > 136)
        InstrCount = 136;

    // Worst case each Xbox instruction becomes 4 PC instructions of 5 tokens
    DWORD *Out = new DWORD[4 + InstrCount * 20];
    int n = 0;
    bool NeedsPosEpilogue = false;
    VshEmit(Out, &n, 0xFFFE0101);   // vs.1.1

    // Seed the position scratch (the R12/oPos alias) from the position input v0.
    // 2D / UI shaders often write only oPos.xy and leave z,w to the vertex; the
    // epilogue then copies the whole scratch to oPos, so unwritten components
    // must be defined -- reading an uninitialized temp makes the host reject the
    // shader (D3DERR_INVALIDCALL). A full oPos-writing shader overwrites this.
    {
        static const DWORD IdSwz[4] = { 0, 1, 2, 3 };
        VshEmit(Out, &n, SIO_MOV);
        VshEmit(Out, &n, VshDstToken(SPR_TEMP, VSH_POS_SCRATCH, 0xF));
        VshEmit(Out, &n, VshSrcToken(SPR_INPUT, 0, IdSwz, FALSE, FALSE));
    }

    for(DWORD i = 0; i < InstrCount; i++)
    {
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

        VshSrc SrcA{}, SrcB{}, SrcC{};
        VshDecodeSrc(I, 0, &SrcA);
        VshDecodeSrc(I, 1, &SrcB);
        VshDecodeSrc(I, 2, &SrcC);

        // Screen-space transform removal: the Xbox library appends a position
        // epilogue that reads oPos back (the R12 alias) and rescales it by 1/w
        // and the viewport, because the NV2A wants screen coordinates. The PC
        // pipeline expects CLIP-space positions and performs the divide itself,
        // so keeping those instructions double-transforms everything off view.
        // Any instruction that both reads R12 and writes the position is part
        // of that epilogue -- drop it.
        {
            bool WritesPos = ((MacMask != 0 || IluMask != 0) && OutR == 12) ||
                             (OMask != 0 && Orb == OUTPUT_O && OutAddr == 0);
            bool ReadsR12  = (Mac != MAC_NOP &&
                              ((SrcA.Mux == PARAM_R && SrcA.R == 12) ||
                               (SrcB.Mux == PARAM_R && SrcB.R == 12) ||
                               (SrcC.Mux == PARAM_R && SrcC.R == 12))) ||
                             (Ilu != ILU_NOP && SrcC.Mux == PARAM_R && SrcC.R == 12);
            if(WritesPos && ReadsR12)
            {
                NeedsPosEpilogue = true;   // position already staged in the scratch temp
                if(VshGetField(I, FLD_FINAL) != 0)
                    break;
                continue;
            }
        }

        if(Mac != MAC_NOP)
        {
            DWORD Opcode = 0;
            const VshSrc *Srcs[3] = { NULL, NULL, NULL };
            int SrcCount = 0;

            switch(Mac)
            {
                case MAC_MOV: Opcode = SIO_MOV; Srcs[0] = &SrcA; SrcCount = 1; break;
                case MAC_MUL: Opcode = SIO_MUL; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2; break;
                case MAC_ADD: Opcode = SIO_ADD; Srcs[0] = &SrcA; Srcs[1] = &SrcC; SrcCount = 2; break;
                case MAC_MAD: Opcode = SIO_MAD; Srcs[0] = &SrcA; Srcs[1] = &SrcB; Srcs[2] = &SrcC; SrcCount = 3; break;
                case MAC_DP3: Opcode = SIO_DP3; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2; break;
                case MAC_DP4: Opcode = SIO_DP4; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2; break;
                case MAC_DPH:
                    // No vs.1.1 DPH (a.xyz1 dot b); approximate with DP4 -- the
                    // A source's W contribution is a.w*b.w instead of b.w.
                    EmuWarning("VshDecoder: DPH approximated with DP4");
                    Opcode = SIO_DP4; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2;
                    break;
                case MAC_DST: Opcode = SIO_DST; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2; break;
                case MAC_MIN: Opcode = SIO_MIN; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2; break;
                case MAC_MAX: Opcode = SIO_MAX; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2; break;
                case MAC_SLT: Opcode = SIO_SLT; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2; break;
                case MAC_SGE: Opcode = SIO_SGE; Srcs[0] = &SrcA; Srcs[1] = &SrcB; SrcCount = 2; break;
                case MAC_ARL:
                    // mov a0.x, srcA
                    VshEmit(Out, &n, SIO_MOV);
                    VshEmit(Out, &n, VshDstToken(SPR_ADDR, 0, 0x1));
                    VshEmit(Out, &n, VshEmitSrc(I, &SrcA, false));
                    Opcode = 0;
                    break;
                default:
                    EmuWarning("VshDecoder: unknown MAC opcode %lu", Mac);
                    Opcode = 0;
                    break;
            }

            if(Opcode != 0)
            {
                // Temp-register destination (R12 = the readable oPos alias)
                if(MacMask != 0)
                {
                    if(OutR == 12)
                        NeedsPosEpilogue = true;
                    VshEmit(Out, &n, Opcode);
                    VshEmit(Out, &n, VshDstToken(SPR_TEMP, VshMapTemp(OutR), VshPcMask(MacMask)));
                    for(int s = 0; s < SrcCount; s++)
                        VshEmit(Out, &n, VshEmitSrc(I, Srcs[s], A0x));
                }
                // Output-register destination
                if(OMask != 0 && OutMux == OMUX_MAC)
                {
                    DWORD RegType, RegNum;
                    if(Orb == OUTPUT_O && OutAddr == 0)
                    {
                        // oPos: stage in the scratch temp so later instructions
                        // can read it back (the NV2A R12 alias); the epilogue
                        // emits the real output write.
                        NeedsPosEpilogue = true;
                        VshEmit(Out, &n, Opcode);
                        VshEmit(Out, &n, VshDstToken(SPR_TEMP, VSH_POS_SCRATCH, VshPcMask(OMask)));
                        for(int s = 0; s < SrcCount; s++)
                            VshEmit(Out, &n, VshEmitSrc(I, Srcs[s], A0x));
                    }
                    else if(Orb == OUTPUT_O && VshMapOutput(OutAddr, &RegType, &RegNum))
                    {
                        VshEmit(Out, &n, Opcode);
                        VshEmit(Out, &n, VshDstToken(RegType, RegNum, VshPcMask(OMask)));
                        for(int s = 0; s < SrcCount; s++)
                            VshEmit(Out, &n, VshEmitSrc(I, Srcs[s], A0x));
                    }
                    else
                        EmuWarning("VshDecoder: unsupported MAC output (orb=%lu, addr=%lu) skipped", Orb, OutAddr);
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
                case ILU_RCC:
                    EmuWarning("VshDecoder: RCC approximated with RCP");
                    Opcode = SIO_RCP;
                    break;
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
                VshSrc SrcIlu = SrcC;
                if(Opcode == SIO_RCP || Opcode == SIO_RSQ || Opcode == SIO_EXP || Opcode == SIO_LOG)
                {
                    SrcIlu.Swz[1] = SrcIlu.Swz[0];
                    SrcIlu.Swz[2] = SrcIlu.Swz[0];
                    SrcIlu.Swz[3] = SrcIlu.Swz[0];
                }

                // Temp-register destination. When paired with an active MAC
                // that also writes a temp, the hardware ILU target is R1.
                if(IluMask != 0)
                {
                    DWORD IluR = (Mac != MAC_NOP && MacMask != 0) ? 1 : OutR;
                    if(IluR == 12)
                        NeedsPosEpilogue = true;
                    VshEmit(Out, &n, Opcode);
                    VshEmit(Out, &n, VshDstToken(SPR_TEMP, VshMapTemp(IluR), VshPcMask(IluMask)));
                    VshEmit(Out, &n, VshEmitSrc(I, &SrcIlu, A0x));
                }
                // Output-register destination
                if(OMask != 0 && OutMux == OMUX_ILU)
                {
                    DWORD RegType, RegNum;
                    if(Orb == OUTPUT_O && OutAddr == 0)
                    {
                        NeedsPosEpilogue = true;
                        VshEmit(Out, &n, Opcode);
                        VshEmit(Out, &n, VshDstToken(SPR_TEMP, VSH_POS_SCRATCH, VshPcMask(OMask)));
                        VshEmit(Out, &n, VshEmitSrc(I, &SrcIlu, A0x));
                    }
                    else if(Orb == OUTPUT_O && VshMapOutput(OutAddr, &RegType, &RegNum))
                    {
                        VshEmit(Out, &n, Opcode);
                        VshEmit(Out, &n, VshDstToken(RegType, RegNum, VshPcMask(OMask)));
                        VshEmit(Out, &n, VshEmitSrc(I, &SrcIlu, A0x));
                    }
                    else
                        EmuWarning("VshDecoder: unsupported ILU output (orb=%lu, addr=%lu) skipped", Orb, OutAddr);
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
        VshEmit(Out, &n, VshSrcToken(SPR_TEMP, VSH_POS_SCRATCH, IdSwz, FALSE, FALSE));
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
int XTL::EmuVshTranslateXboxDeclaration(const DWORD *pXboxDecl, DWORD *pPcDecl, int MaxTokens)
{
    int n = 0;

    while(n < MaxTokens - 1)
    {
        DWORD Token = pXboxDecl[n];

        if(Token == 0xFFFFFFFF)
        {
            pPcDecl[n++] = Token;   // D3DVSD_END
            return n;
        }

        if((Token >> 29) == 2 && (Token & 0x10000000) == 0)   // D3DVSD_REG
        {
            DWORD XboxType = (Token >> 16) & 0xFF;
            DWORD PcType;
            switch(XboxType)
            {
                case 0x12: PcType = 0; break;   // FLOAT1
                case 0x22: PcType = 1; break;   // FLOAT2
                case 0x32: PcType = 2; break;   // FLOAT3
                case 0x42: PcType = 3; break;   // FLOAT4
                case 0x40: PcType = 4; break;   // D3DCOLOR
                case 0x25: PcType = 6; break;   // SHORT2
                case 0x45: PcType = 7; break;   // SHORT4
                default:
                    if(XboxType <= 0x07)
                        PcType = XboxType;      // already a PC type code
                    else
                    {
                        EmuWarning("VshDecoder: unsupported Xbox vertex type 0x%02lX -> FLOAT4", XboxType);
                        PcType = 3;
                    }
                    break;
            }
            pPcDecl[n] = (Token & 0xE000FFFF) | (PcType << 16);
            n++;
            continue;
        }

        // STREAM / CONST / NOP / tessellator tokens pass through unchanged
        pPcDecl[n] = Token;
        n++;
    }

    pPcDecl[n++] = 0xFFFFFFFF;
    EmuWarning("VshDecoder: declaration exceeded %d tokens; truncated", MaxTokens);
    return n;
}

namespace
{
std::size_t VshXboxVertexTypeSize(DWORD type)
{
    switch(type)
    {
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
        case 0x02: return 0;
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
    if(type == 0x40)
    {
        DWORD color = 0;
        std::memcpy(&color, source, sizeof(color));
        output[0] = static_cast<float>((color >> 16) & 0xFFu) / 255.0f;
        output[1] = static_cast<float>((color >> 8) & 0xFFu) / 255.0f;
        output[2] = static_cast<float>(color & 0xFFu) / 255.0f;
        output[3] = static_cast<float>((color >> 24) & 0xFFu) / 255.0f;
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
            output[component] = static_cast<float>(source[component]);
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
    for(std::size_t tokenIndex = 0; tokenIndex < 128; ++tokenIndex)
    {
        const DWORD token = declaration[tokenIndex];
        if(token == 0xFFFFFFFFu)
        {
            return true;
        }
        if((token >> 29) == 1u)
        {
            currentStream = token & 0xFu;
            continue;
        }
        if((token >> 29) != 2u || (token & 0x10000000u) != 0)
        {
            continue;
        }

        const DWORD reg = token & 0x1Fu;
        const DWORD type = (token >> 16) & 0xFFu;
        const std::size_t valueSize = VshXboxVertexTypeSize(type);
        if(reg >= 16 || currentStream >= streamCount ||
           valueSize == (std::numeric_limits<std::size_t>::max)())
        {
            return false;
        }
        if(valueSize == 0)
        {
            continue;
        }

        const VertexStreamView& stream = streams[currentStream];
        if(stream.data == nullptr || stream.stride == 0 ||
           vertexIndex > (std::numeric_limits<std::size_t>::max)() / stream.stride)
        {
            return false;
        }
        const std::size_t vertexOffset = vertexIndex * stream.stride;
        if(vertexOffset > stream.byteSize || streamOffsets[currentStream] > stream.byteSize - vertexOffset ||
           valueSize > stream.byteSize - vertexOffset - streamOffsets[currentStream])
        {
            return false;
        }

        const std::uint8_t* source = static_cast<const std::uint8_t*>(stream.data) + vertexOffset +
                                     streamOffsets[currentStream];
        VshDecodeXboxVertexValue(source, type, &inputRegisters[reg * 4]);
        streamOffsets[currentStream] += valueSize;
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
    if(x <= 0.0f) return 0.0f;
    std::uint32_t bits;
    memcpy(&bits, &x, 4);
    int e = static_cast<int>((bits >> 23) & 0xFFu) - 127;
    bits = (bits & 0x807FFFFFu) | 0x3F800000u; // mantissa m in [1,2)
    float m;
    memcpy(&m, &bits, 4);
    float p = -1.7417939f + (2.8212026f + (-1.4699568f + (0.4471900f - 0.0568825f * m) * m) * m) * m;
    return p + (float)e;
}

static float VshExp2(float x)
{
    if(x < -126.0f) return 0.0f;
    if(x >  126.0f) return 3.4e38f;
    int xi = VshFloorI(x);
    float f = x - (float)xi;
    float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    int bits = (xi + 127) << 23;
    float scale; memcpy(&scale, &bits, 4);
    return p * scale;
}

// ILU scalar op on the C source (already swizzled). LIT writes a full vec4.
static void VshExecIlu(DWORD Op, const float C[4], float R[4])
{
    float s = C[0];
    float r = 0.0f;
    switch(Op)
    {
        case ILU_MOV: R[0]=C[0]; R[1]=C[1]; R[2]=C[2]; R[3]=C[3]; return;
        case ILU_RCP: r = (s != 0.0f) ? 1.0f / s : 0.0f; break;
        case ILU_RCC:
        {
            float a = s;
            if(a >= 0.0f) { if(a < 5.42101e-20f) a = 5.42101e-20f; }
            else          { if(a > -5.42101e-20f) a = -5.42101e-20f; }
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
                                      std::size_t OutTexCoordFloatCount)
{
    if(Program == NULL || InstrCount <= 0)
        return false;

    float Reg[13][4];
    for(int r = 0; r < 13; r++)
        Reg[r][0] = Reg[r][1] = Reg[r][2] = Reg[r][3] = 0.0f;
    float Col[2][4] = { { 0, 0, 0, 1 }, { 0, 0, 0, 1 } };
    float Tex[4][4] = { { 0 } };
    float Fog[4] = { 0, 0, 0, 0 };
    float Pts[4] = { 0, 0, 0, 0 };
    int A0 = 0;

    if(Start < 0) Start = 0;
    for(int pc = Start; pc < InstrCount; pc++)
    {
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
                if(d) VshExecWriteMasked(d, Rm, OMask);
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
                if(d) VshExecWriteMasked(d, Ri, OMask);
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
    return true;
}

extern "C" bool EmuVshExecuteProgram(const DWORD* Program, int InstrCount, int Start,
                                     const float* Const, const float* Input,
                                     float* OutPos, float* OutCol0, float* OutTex0)
{
    return VshExecuteProgramInternal(Program, InstrCount, Start, Const, Input, OutPos, OutCol0,
                                     OutCol0 == nullptr ? 0 : 4, OutTex0, OutTex0 == nullptr ? 0 : 4);
}

bool XTL::VshDiagnostics::ExecuteXboxVertexShader(const void* xboxFunctionData, const float* constants,
                                                  const float* inputRegisters, float* outputPosition,
                                                  float* outputColors, std::size_t outputColorFloatCount,
                                                  float* outputTexCoords,
                                                  std::size_t outputTexCoordFloatCount)
{
    const DWORD* xboxFunction = static_cast<const DWORD*>(xboxFunctionData);
    if(xboxFunction == nullptr || constants == nullptr || inputRegisters == nullptr ||
       outputPosition == nullptr || outputColors == nullptr ||
       (xboxFunction[0] & 0xFFFFu) != VSH_XBOX_VERSION)
    {
        return false;
    }
    const std::size_t instructionCount = VshXboxInstructionCount(xboxFunction);
    return VshExecuteProgramInternal(&xboxFunction[1], static_cast<int>(instructionCount), 0, constants,
                                     inputRegisters, outputPosition, outputColors,
                                     outputColorFloatCount, outputTexCoords, outputTexCoordFloatCount);
}
