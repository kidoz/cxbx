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

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace xboxkrnl
{
    #include <xboxkrnl/xboxkrnl.h>
};

#include "Emu.h"

#undef FIELD_OFFSET     // prevent macro redefinition warnings
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace XTL
{
    DWORD *EmuVshRecompileXboxFunction(const DWORD *pXboxFunction);
    int    EmuVshTranslateXboxDeclaration(const DWORD *pXboxDecl, DWORD *pPcDecl, int MaxTokens);
};

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
        case 6:  *RegType = SPR_RASTOUT;   *RegNum = 2; return true;   // oPts
        case 9: case 10: case 11: case 12:
                 *RegType = SPR_TEXCRDOUT; *RegNum = Address - 9; return true;   // oT0..3
        default:
            // 7/8 = back-face colors (no PC equivalent), anything else unknown
            return false;
    }
}

static void VshEmit(DWORD *Out, int *n, DWORD Token)
{
    Out[(*n)++] = Token;
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

        VshSrc SrcA, SrcB, SrcC;
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

    VshEmit(Out, &n, 0x0000FFFF);   // end

    printf("EmuD3D8: VshDecoder recompiled %lu NV2A instructions into %d vs.1.1 tokens.\n",
           InstrCount, n);
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
    int bits; memcpy(&bits, &x, 4);
    int e = ((bits >> 23) & 0xFF) - 127;
    bits = (bits & 0x807FFFFF) | 0x3F800000; // mantissa m in [1,2)
    float m; memcpy(&m, &bits, 4);
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
        case 4:  return Col[1];       // oD1
        case 5:  return Fog;          // oFog
        case 6:  return Pts;          // oPts
        case 9: case 10: case 11: case 12: return Tex[Addr - 9]; // oT0..3
        default: return NULL;
    }
}

extern "C" bool EmuVshExecuteProgram(const DWORD *Program, int InstrCount, int Start,
                                     const float *Const, const float *Input,
                                     float *OutPos, float *OutCol0, float *OutTex0)
{
    if(Program == NULL || InstrCount <= 0)
        return false;

    float Reg[13][4];
    for(int r = 0; r < 13; r++)
        Reg[r][0] = Reg[r][1] = Reg[r][2] = Reg[r][3] = 0.0f;
    float Col[2][4] = {{0,0,0,1},{0,0,0,1}};
    float Tex[4][4] = {{0}};
    float Fog[4] = {0,0,0,0};
    float Pts[4] = {0,0,0,0};
    int A0 = 0;

    if(Start < 0) Start = 0;
    for(int pc = Start; pc < InstrCount; pc++)
    {
        const DWORD *I = &Program[pc * 4];

        DWORD Mac     = VshGetField(I, FLD_MAC);
        DWORD Ilu     = VshGetField(I, FLD_ILU);
        DWORD MacMask = VshGetField(I, FLD_OUT_MAC_MASK);
        DWORD IluMask = VshGetField(I, FLD_OUT_ILU_MASK);
        DWORD OMask   = VshGetField(I, FLD_OUT_O_MASK);
        DWORD OutR    = VshGetField(I, FLD_OUT_R);
        DWORD Orb     = VshGetField(I, FLD_OUT_ORB);
        DWORD OutAddr = VshGetField(I, FLD_OUT_ADDRESS);
        DWORD OutMux  = VshGetField(I, FLD_OUT_MUX);
        bool  Relative= VshGetField(I, FLD_A0X) != 0;
        DWORD Vfield  = VshGetField(I, FLD_V);
        bool  Final   = VshGetField(I, FLD_FINAL) != 0;

        VshSrc SA, SB, SC;
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
                float *d = VshExecOutputDst(OutAddr, Reg, Col, Tex, Fog, Pts);
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
                float *d = VshExecOutputDst(OutAddr, Reg, Col, Tex, Fog, Pts);
                if(d) VshExecWriteMasked(d, Ri, OMask);
            }
        }

        if(Final)
            break;
    }

    // oPos lives in the R12 alias; diffuse in oD0; texcoord0 in oT0.
    OutPos[0] = Reg[12][0]; OutPos[1] = Reg[12][1]; OutPos[2] = Reg[12][2]; OutPos[3] = Reg[12][3];
    OutCol0[0] = Col[0][0]; OutCol0[1] = Col[0][1]; OutCol0[2] = Col[0][2]; OutCol0[3] = Col[0][3];
    if(OutTex0 != NULL)
    {
        OutTex0[0] = Tex[0][0]; OutTex0[1] = Tex[0][1];
        OutTex0[2] = Tex[0][2]; OutTex0[3] = Tex[0][3];
    }
    return true;
}
