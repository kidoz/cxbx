#pragma once

// Translate an Xbox pixel shader (X_D3DPIXELSHADERDEF, NV2A register-combiner
// state; layout per XDK 5849 d3d8types.h) into Direct3D 8 ps.1.1 bytecode.
//
// The translation is CONSERVATIVE: every construct either maps exactly onto a
// ps.1.1 instruction/modifier or the translation fails with a stable reason
// string and the caller keeps its fixed-function approximation. Failure is
// normal and safe; a wrong-but-plausible translation is not.
//
// Supported: 1-8 combiner stages (within the ps.1.1 budget of 8 arithmetic
// instructions), multiply/dot/sum portions, all eight NV2A input mappings,
// alpha/blue channel selects, output shifts x2/x4/d2, plain 2D/3D/cube
// texture lookups, per-stage or shared constants (up to 7 distinct values),
// and A*B+(1-A)*C+D final combiners over ordinary registers.
// Unsupported (fails): MUX, output BIAS, blue-to-alpha, dependent/bump
// texture modes, FOG/V1R0_SUM/EF_PROD reads, register reads before any
// write (r0/r1), more constants or instructions than ps.1.1 holds.

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "d3d_pixel_shader.h"

namespace cxbx::d3d
{

struct PixelShaderConstant
{
    unsigned index;   // ps.1.1 constant register c<index>
    float value[4];   // BGRA-source dword expanded to RGBA floats
};

struct PixelShaderTranslation
{
    std::vector<std::uint32_t> bytecode;
    std::vector<PixelShaderConstant> constants;
    unsigned arithmetic = 0;   // emitted arithmetic instruction count
    unsigned textures = 0;     // emitted tex instruction count
    const char* failure = nullptr;

    bool ok() const noexcept { return failure == nullptr; }
};

namespace detail::psh
{

// X_D3DPIXELSHADERDEF dword indices (see the struct in the XDK d3d8types.h).
inline constexpr unsigned AlphaInputs = 0;           // [8]
inline constexpr unsigned FinalInputsABCD = 8;
inline constexpr unsigned FinalInputsEFG = 9;
inline constexpr unsigned Constant0 = 10;            // [8]
inline constexpr unsigned Constant1 = 18;            // [8]
inline constexpr unsigned AlphaOutputs = 26;         // [8]
inline constexpr unsigned RgbInputs = 34;            // [8]
inline constexpr unsigned FinalConstant0 = 43;
inline constexpr unsigned FinalConstant1 = 44;
inline constexpr unsigned RgbOutputs = 45;           // [8]
inline constexpr unsigned CombinerCount = 53;
inline constexpr unsigned TextureModes = 54;

// PS_REGISTER
inline constexpr unsigned RegZero = 0x0;
inline constexpr unsigned RegC0 = 0x1;
inline constexpr unsigned RegC1 = 0x2;
inline constexpr unsigned RegFog = 0x3;
inline constexpr unsigned RegV0 = 0x4;
inline constexpr unsigned RegV1 = 0x5;
inline constexpr unsigned RegT0 = 0x8;
inline constexpr unsigned RegT3 = 0xB;
inline constexpr unsigned RegR0 = 0xC;
inline constexpr unsigned RegR1 = 0xD;
inline constexpr unsigned RegV1R0Sum = 0xE;
inline constexpr unsigned RegEfProd = 0xF;

// PS_INPUTMAPPING (operand bits 5-7)
inline constexpr unsigned MapUnsignedIdentity = 0x0;
inline constexpr unsigned MapUnsignedInvert = 0x1;
inline constexpr unsigned MapExpandNormal = 0x2;
inline constexpr unsigned MapExpandNegate = 0x3;
inline constexpr unsigned MapHalfbiasNormal = 0x4;
inline constexpr unsigned MapHalfbiasNegate = 0x5;
inline constexpr unsigned MapSignedIdentity = 0x6;
inline constexpr unsigned MapSignedNegate = 0x7;

// D3D8 shader tokens.
inline constexpr std::uint32_t TokVersionPs11 = 0xFFFF0101u;
inline constexpr std::uint32_t TokEnd = 0x0000FFFFu;
inline constexpr std::uint32_t OpMov = 0x01, OpAdd = 0x02, OpMad = 0x04,
                               OpMul = 0x05, OpDp3 = 0x08, OpLrp = 0x12,
                               OpTex = 0x42;
inline constexpr std::uint32_t RtTemp = 0, RtInput = 1, RtConst = 2, RtTexture = 3;
inline constexpr std::uint32_t MaskRgb = 0x7u << 16, MaskA = 0x8u << 16,
                               MaskAll = 0xFu << 16;
inline constexpr std::uint32_t SwizNone = 0xE4u << 16, SwizA = 0xFFu << 16,
                               SwizB = 0xAAu << 16;
inline constexpr std::uint32_t SmNeg = 1u << 24, SmBias = 2u << 24,
                               SmBiasNeg = 3u << 24, SmSign = 4u << 24,
                               SmSignNeg = 5u << 24, SmComp = 6u << 24;
inline constexpr std::uint32_t ShiftX2 = 1u << 24, ShiftX4 = 2u << 24,
                               ShiftD2 = 0xFu << 24;

// The ps.1.1 register a translated operand lives in.
struct HostReg
{
    std::uint32_t type;
    std::uint32_t number;
    bool valid = false;
};

struct Operand
{
    unsigned reg;      // PS_REGISTER
    bool alphaChannel; // operand channel bit (RGB portion: RGB/ALPHA;
                       // alpha portion: BLUE/ALPHA)
    unsigned mapping;  // PS_INPUTMAPPING >> 5
};

inline Operand DecodeOperand(std::uint32_t inputs, unsigned slot) noexcept
{
    // Slot 0..3 = A..D at bytes 3..0.
    const std::uint32_t byte = (inputs >> ((3u - slot) * 8u)) & 0xFFu;
    return { static_cast<unsigned>(byte & 0xFu), (byte & 0x10u) != 0,
             static_cast<unsigned>(byte >> 5) };
}

inline bool OperandIsZero(const Operand& op) noexcept
{
    return op.reg == RegZero &&
           (op.mapping == MapUnsignedIdentity || op.mapping == MapSignedIdentity ||
            op.mapping == MapSignedNegate);
}

inline bool OperandIsOne(const Operand& op) noexcept
{
    return op.reg == RegZero && op.mapping == MapUnsignedInvert;
}

struct Emitter
{
    PixelShaderTranslation out;
    // Which ps.1.1 constant register each distinct constant dword got.
    std::vector<std::pair<std::uint32_t, unsigned>> constantSlots;
    bool zeroConstantUsed = false;
    std::uint32_t writtenRegs = 0;   // bit per PS_REGISTER already written
    std::uint32_t scratchReg = 0;    // PS_REGISTER used as scratch (0 = none)

    void Fail(const char* reason)
    {
        if(out.failure == nullptr)
            out.failure = reason;
    }

    unsigned ConstantSlot(std::uint32_t value)
    {
        for(const auto& entry : constantSlots)
        {
            if(entry.first == value)
                return entry.second;
        }
        // c7 is reserved as literal zero.
        if(constantSlots.size() >= 7)
        {
            Fail("constant_budget");
            return 0;
        }
        const unsigned slot = static_cast<unsigned>(constantSlots.size());
        constantSlots.push_back({ value, slot });
        PixelShaderConstant constant{};
        constant.index = slot;
        constant.value[0] = ((value >> 16) & 0xFF) / 255.0f; // R
        constant.value[1] = ((value >> 8) & 0xFF) / 255.0f;  // G
        constant.value[2] = (value & 0xFF) / 255.0f;         // B
        constant.value[3] = ((value >> 24) & 0xFF) / 255.0f; // A
        out.constants.push_back(constant);
        return slot;
    }

    // Map a PS_REGISTER to a ps.1.1 register. stageC0/stageC1 are the constant
    // dwords C0/C1 resolve to for the current stage (or final combiner).
    HostReg MapRegister(unsigned reg, std::uint32_t stageC0, std::uint32_t stageC1)
    {
        switch(reg)
        {
            case RegZero:
                zeroConstantUsed = true;
                return { RtConst, 7, true };
            case RegC0:
                return { RtConst, ConstantSlot(stageC0), true };
            case RegC1:
                return { RtConst, ConstantSlot(stageC1), true };
            case RegV0:
                return { RtInput, 0, true };
            case RegV1:
                return { RtInput, 1, true };
            case RegR0:
                return { RtTemp, 0, true };
            case RegR1:
                return { RtTemp, 1, true };
            default:
                if(reg >= RegT0 && reg <= RegT3)
                    return { RtTexture, reg - RegT0, true };
                Fail(reg == RegFog ? "fog_register" : "special_register");
                return {};
        }
    }

    std::uint32_t SourceToken(const Operand& op, bool alphaPortion,
                              std::uint32_t stageC0, std::uint32_t stageC1)
    {
        const HostReg reg = MapRegister(op.reg, stageC0, stageC1);
        if(!reg.valid)
            return 0;

        // Reads of r0/r1 before anything wrote them have hardware-defined
        // startup contents this translation does not model.
        if((op.reg == RegR0 || op.reg == RegR1) &&
           (writtenRegs & (1u << op.reg)) == 0)
        {
            Fail("register_read_before_write");
            return 0;
        }

        std::uint32_t swizzle = SwizNone;
        if(alphaPortion)
            swizzle = op.alphaChannel ? SwizA : SwizB;
        else if(op.alphaChannel)
            swizzle = SwizA;

        std::uint32_t modifier = 0;
        switch(op.mapping)
        {
            case MapUnsignedIdentity: modifier = 0; break;
            case MapUnsignedInvert:   modifier = SmComp; break;
            case MapExpandNormal:     modifier = SmSign; break;
            case MapExpandNegate:     modifier = SmSignNeg; break;
            case MapHalfbiasNormal:   modifier = SmBias; break;
            case MapHalfbiasNegate:   modifier = SmBiasNeg; break;
            case MapSignedIdentity:   modifier = 0; break;
            case MapSignedNegate:     modifier = SmNeg; break;
        }

        return 0x80000000u | (reg.type << 28) | modifier | swizzle | reg.number;
    }

    std::uint32_t DestToken(unsigned reg, std::uint32_t writeMask,
                            std::uint32_t shift)
    {
        const HostReg host = MapRegister(reg, 0, 0);
        if(!host.valid)
            return 0;
        if(host.type == RtInput || host.type == RtConst)
        {
            // ps.1.1 v#/c# registers are read-only destinations.
            Fail("readonly_register_write");
            return 0;
        }
        writtenRegs |= 1u << reg;
        return 0x80000000u | (host.type << 28) | shift | writeMask | host.number;
    }

    void Arithmetic(std::uint32_t opcode, std::uint32_t dest,
                    std::initializer_list<std::uint32_t> sources)
    {
        if(out.failure != nullptr)
            return;
        if(++out.arithmetic > 8)
        {
            Fail("instruction_budget");
            return;
        }
        out.bytecode.push_back(opcode);
        out.bytecode.push_back(dest);
        for(std::uint32_t source : sources)
            out.bytecode.push_back(source);
    }
};

// One combiner portion: dests for the AB product, the CD product and the sum,
// plus flags, over four operands.
struct Portion
{
    Operand a, b, c, d;
    unsigned abDest, cdDest, sumDest;
    bool abDot, cdDot, mux;
    std::uint32_t shift;       // ps.1.1 dest-shift bits
    bool biasOutput;           // PS_COMBINEROUTPUT_BIAS (unsupported)
    bool blueToAlpha;          // AB/CD_BLUE_TO_ALPHA (unsupported)
};

inline Portion DecodePortion(std::uint32_t inputs, std::uint32_t output) noexcept
{
    Portion p{};
    p.a = DecodeOperand(inputs, 0);
    p.b = DecodeOperand(inputs, 1);
    p.c = DecodeOperand(inputs, 2);
    p.d = DecodeOperand(inputs, 3);
    p.cdDest = output & 0xFu;
    p.abDest = (output >> 4) & 0xFu;
    p.sumDest = (output >> 8) & 0xFu;
    const std::uint32_t flags = output >> 12;
    p.cdDot = (flags & 0x01u) != 0;
    p.abDot = (flags & 0x02u) != 0;
    p.mux = (flags & 0x04u) != 0;
    p.biasOutput = (flags & 0x08u) != 0;
    switch(flags & 0x30u)
    {
        case 0x10u: p.shift = ShiftX2; break;
        case 0x20u: p.shift = ShiftX4; break;
        case 0x30u: p.shift = ShiftD2; break;
        default:    p.shift = 0; break;
    }
    p.blueToAlpha = (flags & 0xC0u) != 0;
    return p;
}

} // namespace detail::psh

inline PixelShaderTranslation TranslatePixelShader(
    const XboxPixelShaderDefinition& def)
{
    using namespace detail::psh;

    Emitter emitter;
    auto& out = emitter.out;

    const unsigned combinerCount = def[CombinerCount] & 0xFu;
    if(combinerCount == 0 || combinerCount > 8)
    {
        emitter.Fail("combiner_count");
        return out;
    }
    const bool uniqueC0 = (def[CombinerCount] & (0x10u << 8)) != 0;
    const bool uniqueC1 = (def[CombinerCount] & (0x100u << 8)) != 0;

    // ---- Pass 1: which registers are read/written, and where scratch fits.
    std::uint32_t readRegs = 0, destRegs = 0;
    auto noteInputs = [&](std::uint32_t inputs) {
        for(unsigned slot = 0; slot < 4; ++slot)
            readRegs |= 1u << DecodeOperand(inputs, slot).reg;
    };
    auto noteOutputs = [&](std::uint32_t output) {
        destRegs |= 1u << (output & 0xFu);
        destRegs |= 1u << ((output >> 4) & 0xFu);
        destRegs |= 1u << ((output >> 8) & 0xFu);
    };
    for(unsigned stage = 0; stage < combinerCount; ++stage)
    {
        noteInputs(def[RgbInputs + stage]);
        noteInputs(def[AlphaInputs + stage]);
        noteOutputs(def[RgbOutputs + stage]);
        noteOutputs(def[AlphaOutputs + stage]);
    }
    const bool hasFinalCombiner =
        def[FinalInputsABCD] != 0 || (def[FinalInputsEFG] & ~0xFFu) != 0;
    if(hasFinalCombiner)
    {
        noteInputs(def[FinalInputsABCD]);
        for(unsigned slot = 0; slot < 3; ++slot)
            readRegs |= 1u << DecodeOperand(def[FinalInputsEFG], slot).reg;
    }
    destRegs &= ~(1u << RegZero); // DISCARD

    // Scratch register for product/sum shapes that need one. Only r1
    // qualifies: t# scratch would need a tex load the validator may demand,
    // and everything else is read-only or reserved.
    if((readRegs & (1u << RegR1)) == 0 && (destRegs & (1u << RegR1)) == 0)
        emitter.scratchReg = RegR1;

    // ---- Texture loads: every t# that is read before a combiner writes it
    // needs a `tex`, and its stage mode must be a plain lookup.
    out.bytecode.push_back(TokVersionPs11);
    for(unsigned stage = 0; stage < 4; ++stage)
    {
        const unsigned reg = RegT0 + stage;
        if((readRegs & (1u << reg)) == 0)
            continue;
        const std::uint32_t mode = PixelShaderTextureMode(def, stage);
        if(mode < 1 || mode > 3) // PROJECT2D / PROJECT3D / CUBEMAP
        {
            emitter.Fail(mode == 0 ? "texture_mode_none" : "texture_mode_complex");
            return out;
        }
        out.bytecode.push_back(OpTex);
        out.bytecode.push_back(0x80000000u | (RtTexture << 28) | MaskAll | stage);
        out.textures++;
        emitter.writtenRegs |= 1u << reg;
    }

    // ---- Combiner stages.
    for(unsigned stage = 0; stage < combinerCount && out.failure == nullptr; ++stage)
    {
        const std::uint32_t stageC0 = def[Constant0 + (uniqueC0 ? stage : 0)];
        const std::uint32_t stageC1 = def[Constant1 + (uniqueC1 ? stage : 0)];
        const std::uint32_t writtenBeforeStage = emitter.writtenRegs;

        for(unsigned portionIndex = 0; portionIndex < 2; ++portionIndex)
        {
            const bool alphaPortion = portionIndex == 1;
            const Portion p = alphaPortion
                ? DecodePortion(def[AlphaInputs + stage], def[AlphaOutputs + stage])
                : DecodePortion(def[RgbInputs + stage], def[RgbOutputs + stage]);

            if(p.abDest == RegZero && p.cdDest == RegZero && p.sumDest == RegZero)
                continue; // fully discarded portion

            if(p.mux)                    { emitter.Fail("mux"); break; }
            if(p.biasOutput)             { emitter.Fail("output_bias"); break; }
            if(p.blueToAlpha)            { emitter.Fail("blue_to_alpha"); break; }
            if(alphaPortion && (p.abDot || p.cdDot))
            {
                emitter.Fail("alpha_dot");
                break;
            }

            // NV2A evaluates the whole stage in parallel: the alpha portion
            // reads the PRE-stage blue channel of any register the RGB
            // portion writes. Sequential ps.1.1 cannot express that.
            if(alphaPortion)
            {
                const std::uint32_t rgbWrites =
                    emitter.writtenRegs & ~writtenBeforeStage;
                bool blueHazard = false;
                for(const Operand* op : { &p.a, &p.b, &p.c, &p.d })
                {
                    blueHazard = blueHazard ||
                                 (!op->alphaChannel && op->reg != RegZero &&
                                  (rgbWrites & (1u << op->reg)) != 0);
                }
                if(blueHazard)
                {
                    emitter.Fail("stage_blue_hazard");
                    break;
                }
            }

            const std::uint32_t mask = alphaPortion ? MaskA : MaskRgb;
            auto src = [&](const Operand& op) {
                return emitter.SourceToken(op, alphaPortion, stageC0, stageC1);
            };
            auto dst = [&](unsigned reg) {
                return emitter.DestToken(reg, mask, p.shift);
            };
            auto regSrc = [&](unsigned reg) {
                // Read back a register this portion just wrote (no mapping).
                return emitter.SourceToken({ reg, alphaPortion, MapSignedIdentity },
                                           alphaPortion, stageC0, stageC1);
            };
            auto product = [&](unsigned destReg, const Operand& x, const Operand& y,
                               bool dot) {
                emitter.Arithmetic(dot ? OpDp3 : OpMul, dst(destReg),
                                   { src(x), src(y) });
            };

            const bool abZero = OperandIsZero(p.a) || OperandIsZero(p.b);
            const bool cdZero = OperandIsZero(p.c) || OperandIsZero(p.d);
            const bool abStored = p.abDest != RegZero;
            const bool cdStored = p.cdDest != RegZero;

            // Parallel-portion write hazards: storing one product must not
            // clobber the other's inputs before they are read. Pick a safe
            // emission order; bail when neither order works. stale* records
            // which original inputs the chosen order clobbers before the
            // sum could re-read them.
            const bool abClobbersCd =
                abStored && !cdZero &&
                (p.c.reg == p.abDest || p.d.reg == p.abDest);
            const bool cdClobbersAb =
                cdStored && !abZero &&
                (p.a.reg == p.cdDest || p.b.reg == p.cdDest);
            bool staleAB = false, staleCD = false;
            if(abClobbersCd && cdClobbersAb)
            {
                emitter.Fail("portion_hazard");
                break;
            }
            if(abClobbersCd)
            {
                if(cdStored)
                    product(p.cdDest, p.c, p.d, p.cdDot);
                if(abStored)
                    product(p.abDest, p.a, p.b, p.abDot);
                staleCD = true;
            }
            else
            {
                if(abStored)
                    product(p.abDest, p.a, p.b, p.abDot);
                if(cdStored)
                    product(p.cdDest, p.c, p.d, p.cdDot);
                staleAB = cdClobbersAb;
            }

            if(p.sumDest == RegZero)
                continue;

            // sum = AB + CD, using whichever halves already exist.
            if(abStored && cdStored)
                emitter.Arithmetic(OpAdd, dst(p.sumDest),
                                   { regSrc(p.abDest), regSrc(p.cdDest) });
            else if(cdZero && abStored)
                emitter.Arithmetic(OpMov, dst(p.sumDest), { regSrc(p.abDest) });
            else if(cdZero)
                product(p.sumDest, p.a, p.b, p.abDot);
            else if(abZero && cdStored)
                emitter.Arithmetic(OpMov, dst(p.sumDest), { regSrc(p.cdDest) });
            else if(abZero)
                product(p.sumDest, p.c, p.d, p.cdDot);
            else if(abStored && !p.cdDot && !staleCD)
                emitter.Arithmetic(OpMad, dst(p.sumDest),
                                   { src(p.c), src(p.d), regSrc(p.abDest) });
            else if(cdStored && !p.abDot && !staleAB)
                emitter.Arithmetic(OpMad, dst(p.sumDest),
                                   { src(p.a), src(p.b), regSrc(p.cdDest) });
            else if(abStored || cdStored)
                emitter.Fail("portion_hazard");
            else if(OperandIsOne(p.d) && !p.abDot && !p.cdDot)
                emitter.Arithmetic(OpMad, dst(p.sumDest),
                                   { src(p.a), src(p.b), src(p.c) });
            else if(OperandIsOne(p.b) && !p.abDot && !p.cdDot)
                emitter.Arithmetic(OpMad, dst(p.sumDest),
                                   { src(p.c), src(p.d), src(p.a) });
            else if(OperandIsOne(p.c) && !p.abDot && !p.cdDot)
                emitter.Arithmetic(OpMad, dst(p.sumDest),
                                   { src(p.a), src(p.b), src(p.d) });
            else if(OperandIsOne(p.a) && !p.abDot && !p.cdDot)
                emitter.Arithmetic(OpMad, dst(p.sumDest),
                                   { src(p.c), src(p.d), src(p.b) });
            else if(emitter.scratchReg != 0 && !p.abDot)
            {
                emitter.Arithmetic(p.cdDot ? OpDp3 : OpMul,
                                   emitter.DestToken(emitter.scratchReg, mask, 0),
                                   { src(p.c), src(p.d) });
                emitter.Arithmetic(OpMad, dst(p.sumDest),
                                   { src(p.a), src(p.b),
                                     regSrc(emitter.scratchReg) });
            }
            else if(!p.abDot && !p.cdDot && p.a.reg != p.sumDest &&
                    p.b.reg != p.sumDest)
            {
                // No free scratch: the sum destination itself can stage the
                // CD product as long as A/B do not read it afterwards. Only
                // the final mad carries the output shift.
                emitter.Arithmetic(OpMul, emitter.DestToken(p.sumDest, mask, 0),
                                   { src(p.c), src(p.d) });
                emitter.Arithmetic(OpMad, dst(p.sumDest),
                                   { src(p.a), src(p.b), regSrc(p.sumDest) });
            }
            else
                emitter.Fail("sum_shape");
        }
    }

    // ---- Final combiner: out.rgb = A*B + (1-A)*C + D, out.a = G.
    if(out.failure == nullptr && hasFinalCombiner)
    {
        const Operand e = DecodeOperand(def[FinalInputsEFG], 0);
        const Operand f = DecodeOperand(def[FinalInputsEFG], 1);
        const Operand g = DecodeOperand(def[FinalInputsEFG], 2);
        const Operand a = DecodeOperand(def[FinalInputsABCD], 0);
        const Operand b = DecodeOperand(def[FinalInputsABCD], 1);
        const Operand c = DecodeOperand(def[FinalInputsABCD], 2);
        const Operand d = DecodeOperand(def[FinalInputsABCD], 3);

        // The PS_FINALCOMBINERSETTING flags (byte 0 of EFG) only shape the
        // V1R0_SUM special input; when nothing reads it they are inert, so
        // they need no handling here. Special registers that ARE read fail
        // in MapRegister. E/F feed only EF_PROD, which likewise fails when
        // read; non-zero E/F with no EF_PROD reader would be inert too, but
        // bail conservatively rather than assume.
        if(!OperandIsZero(e) || !OperandIsZero(f))
            emitter.Fail("final_combiner_ef");

        if(out.failure == nullptr)
        {
            const std::uint32_t c0 = def[FinalConstant0];
            const std::uint32_t c1 = def[FinalConstant1];
            auto src = [&](const Operand& op) {
                return emitter.SourceToken(op, false, c0, c1);
            };
            auto isR0RgbIdentity = [&](const Operand& op) {
                return op.reg == RegR0 && !op.alphaChannel &&
                       (op.mapping == MapUnsignedIdentity ||
                        op.mapping == MapSignedIdentity);
            };

            // Algebraic pre-simplification: a degenerate A collapses the
            // lerp, and the dropped term's register is never even mapped
            // (titles routinely park FOG in the dead slot).
            if(OperandIsOne(a))
            {
                // rgb = B + D
                if(OperandIsZero(d))
                {
                    if(!isR0RgbIdentity(b))
                        emitter.Arithmetic(OpMov, emitter.DestToken(RegR0, MaskRgb, 0),
                                           { src(b) });
                }
                else
                    emitter.Arithmetic(OpAdd, emitter.DestToken(RegR0, MaskRgb, 0),
                                       { src(b), src(d) });
            }
            else if(OperandIsZero(a))
            {
                // rgb = C + D
                if(OperandIsZero(d))
                {
                    if(!isR0RgbIdentity(c))
                        emitter.Arithmetic(OpMov, emitter.DestToken(RegR0, MaskRgb, 0),
                                           { src(c) });
                }
                else
                    emitter.Arithmetic(OpAdd, emitter.DestToken(RegR0, MaskRgb, 0),
                                       { src(c), src(d) });
            }
            else
            {
                // lrp computes src0*src1 + (1-src0)*src2 -- the final
                // combiner shape exactly. A trailing +D needs its own add.
                if(OperandIsZero(d))
                    emitter.Arithmetic(OpLrp, emitter.DestToken(RegR0, MaskRgb, 0),
                                       { src(a), src(b), src(c) });
                else if(emitter.scratchReg != 0)
                {
                    emitter.Arithmetic(
                        OpLrp, emitter.DestToken(emitter.scratchReg, MaskRgb, 0),
                        { src(a), src(b), src(c) });
                    emitter.Arithmetic(
                        OpAdd, emitter.DestToken(RegR0, MaskRgb, 0),
                        { emitter.SourceToken({ emitter.scratchReg, false,
                                                MapSignedIdentity }, false, c0, c1),
                          src(d) });
                }
                else
                    emitter.Fail("final_combiner_no_scratch");
            }

            // Alpha: r0.a = G. Skip the identity case (G == r0.a).
            if(out.failure == nullptr &&
               !(g.reg == RegR0 && g.alphaChannel &&
                 (g.mapping == MapUnsignedIdentity ||
                  g.mapping == MapSignedIdentity)))
            {
                emitter.Arithmetic(
                    OpMov, emitter.DestToken(RegR0, MaskA, 0),
                    { emitter.SourceToken({ g.reg, g.alphaChannel, g.mapping },
                                          true, c0, c1) });
            }
        }
    }

    if(out.failure == nullptr && (emitter.writtenRegs & (1u << RegR0)) == 0)
        emitter.Fail("r0_never_written");

    if(out.failure != nullptr)
    {
        out.bytecode.clear();
        out.constants.clear();
        return out;
    }

    if(emitter.zeroConstantUsed)
    {
        PixelShaderConstant zero{};
        zero.index = 7;
        out.constants.push_back(zero);
    }

    out.bytecode.push_back(TokEnd);
    return out;
}

} // namespace cxbx::d3d
