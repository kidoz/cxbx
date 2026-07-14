// Host unit test for the Xbox pixel-shader (register combiner) -> ps.1.1
// translator. Builds combiner definitions the way the XDK PS_* macros do and
// checks the emitted D3D8 bytecode, the constants, and the bail-out reasons.

#include "core/d3d_pixel_shader_translate.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using cxbx::d3d::PixelShaderTranslation;
using cxbx::d3d::TranslatePixelShader;
using cxbx::d3d::XboxPixelShaderDefinition;

static int g_failures = 0;

static void Check(bool condition, const char* what)
{
    if(!condition)
    {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

// ---- XDK-style builders --------------------------------------------------

// PS_REGISTER | PS_CHANNEL | PS_INPUTMAPPING, as one operand byte.
static std::uint32_t In(unsigned reg, unsigned channel = 0x00,
                        unsigned mapping = 0x00)
{
    return reg | channel | mapping;
}

static std::uint32_t Inputs(std::uint32_t a, std::uint32_t b, std::uint32_t c,
                            std::uint32_t d)
{
    return (a << 24) | (b << 16) | (c << 8) | d;
}

static std::uint32_t Outputs(unsigned ab, unsigned cd, unsigned muxSum,
                             unsigned flags = 0)
{
    return (flags << 12) | (muxSum << 8) | (ab << 4) | cd;
}

// Common registers / mappings (mirroring the XDK enums).
enum : unsigned
{
    ZERO = 0x0, C0 = 0x1, C1 = 0x2, FOG = 0x3, V0 = 0x4, V1 = 0x5,
    T0 = 0x8, T1 = 0x9, T2 = 0xA, T3 = 0xB, R0 = 0xC, R1 = 0xD,
    DISCARD = 0x0,
    CH_RGB = 0x00, CH_ALPHA = 0x10, CH_BLUE = 0x00,
    MAP_IDENTITY = 0x00, MAP_INVERT = 0x20, MAP_EXPAND = 0x40,
    MAP_SIGNED_NEG = 0xE0,
    OUT_SHIFTLEFT_1 = 0x10, OUT_MUX = 0x04, OUT_BIAS = 0x08,
};

struct DefBuilder
{
    XboxPixelShaderDefinition def{};

    DefBuilder& Combiners(unsigned count, unsigned flags = 0)
    {
        def[53] = (flags << 8) | count;
        return *this;
    }
    DefBuilder& TextureMode(unsigned stage, unsigned mode)
    {
        def[54] |= mode << (stage * 5);
        return *this;
    }
    DefBuilder& Rgb(unsigned stage, std::uint32_t inputs, std::uint32_t outputs)
    {
        def[34 + stage] = inputs;
        def[45 + stage] = outputs;
        return *this;
    }
    DefBuilder& Alpha(unsigned stage, std::uint32_t inputs, std::uint32_t outputs)
    {
        def[stage] = inputs;
        def[26 + stage] = outputs;
        return *this;
    }
    DefBuilder& StageConstant0(unsigned stage, std::uint32_t value)
    {
        def[10 + stage] = value;
        return *this;
    }
    DefBuilder& Final(std::uint32_t abcd, std::uint32_t efg)
    {
        def[8] = abcd;
        def[9] = efg;
        return *this;
    }
};

// ---- bytecode inspection ---------------------------------------------------

static unsigned CountToken(const PixelShaderTranslation& t, std::uint32_t opcode)
{
    unsigned count = 0;
    for(std::uint32_t token : t.bytecode)
        if(token == opcode)
            ++count;
    return count;
}

int main()
{
    // 1. Texture * diffuse modulate through the sum slot (the classic
    //    "modulate" shader): rgb and alpha portions, CD zeroed.
    {
        DefBuilder b;
        b.Combiners(1)
            .TextureMode(0, 1 /* PROJECT2D */)
            .Rgb(0, Inputs(In(T0), In(V0), In(ZERO), In(ZERO)),
                 Outputs(DISCARD, DISCARD, R0))
            .Alpha(0, Inputs(In(T0, CH_ALPHA), In(V0, CH_ALPHA), In(ZERO), In(ZERO)),
                   Outputs(DISCARD, DISCARD, R0));
        const PixelShaderTranslation t = TranslatePixelShader(b.def);
        Check(t.ok(), "modulate translates");
        Check(t.textures == 1, "modulate uses one tex");
        Check(t.arithmetic == 2, "modulate is two arithmetic instructions");
        Check(t.bytecode.front() == 0xFFFF0101u, "version token is ps.1.1");
        Check(t.bytecode.back() == 0x0000FFFFu, "END token present");
        Check(CountToken(t, 0x05) == 2, "two muls emitted");
        // The ZERO operands are optimized away, so no constant is needed.
        Check(t.constants.empty(), "modulate needs no constants");
    }

    // 1b. An operand of literal ONE (ZERO | UNSIGNED_INVERT) materializes as
    //     the complemented c7 zero constant.
    {
        DefBuilder b;
        b.Combiners(1)
            .TextureMode(0, 1)
            .Rgb(0, Inputs(In(T0), In(ZERO, CH_RGB, MAP_INVERT), In(ZERO), In(ZERO)),
                 Outputs(DISCARD, DISCARD, R0))
            .Alpha(0, Inputs(In(T0, CH_ALPHA), In(V0, CH_ALPHA), In(ZERO), In(ZERO)),
                   Outputs(DISCARD, DISCARD, R0));
        const PixelShaderTranslation t = TranslatePixelShader(b.def);
        Check(t.ok(), "one-operand shader translates");
        bool sawZeroConstant = false;
        for(const auto& constant : t.constants)
            sawZeroConstant |= constant.index == 7;
        Check(sawZeroConstant, "zero constant reserved for ONE");
    }

    // 2. UNSIGNED_INVERT maps to the 1-x source modifier (D3DSPSM_COMP).
    {
        DefBuilder b;
        b.Combiners(1)
            .TextureMode(0, 1)
            .Rgb(0, Inputs(In(T0, CH_RGB, MAP_INVERT), In(V0), In(ZERO), In(ZERO)),
                 Outputs(DISCARD, DISCARD, R0))
            .Alpha(0, Inputs(In(T0, CH_ALPHA), In(V0, CH_ALPHA), In(ZERO), In(ZERO)),
                   Outputs(DISCARD, DISCARD, R0));
        const PixelShaderTranslation t = TranslatePixelShader(b.def);
        Check(t.ok(), "invert translates");
        bool sawComp = false;
        for(std::uint32_t token : t.bytecode)
            sawComp |= (token & 0x8F000000u) == (0x80000000u | (6u << 24));
        Check(sawComp, "invert emits the complement modifier");
    }

    // 3. SHIFTLEFT_1 becomes the _x2 destination shift.
    {
        DefBuilder b;
        b.Combiners(1)
            .TextureMode(0, 1)
            .Rgb(0, Inputs(In(T0), In(V0), In(ZERO), In(ZERO)),
                 Outputs(DISCARD, DISCARD, R0, OUT_SHIFTLEFT_1))
            .Alpha(0, Inputs(In(T0, CH_ALPHA), In(V0, CH_ALPHA), In(ZERO), In(ZERO)),
                   Outputs(DISCARD, DISCARD, R0));
        const PixelShaderTranslation t = TranslatePixelShader(b.def);
        Check(t.ok(), "shift translates");
        bool sawX2Dest = false;
        for(std::uint32_t token : t.bytecode)
            sawX2Dest |= (token & 0x8F0F0000u) == (0x80000000u | (1u << 24) | (0x7u << 16));
        Check(sawX2Dest, "x2 destination shift emitted");
    }

    // 4. Stage constants dedupe into ps constant slots and carry values.
    {
        DefBuilder b;
        b.Combiners(1)
            .StageConstant0(0, 0x80FF4020u) // A=0x80 R=0xFF G=0x40 B=0x20
            .Rgb(0, Inputs(In(C0), In(V0), In(ZERO), In(ZERO)),
                 Outputs(DISCARD, DISCARD, R0))
            .Alpha(0, Inputs(In(C0, CH_ALPHA), In(V0, CH_ALPHA), In(ZERO), In(ZERO)),
                   Outputs(DISCARD, DISCARD, R0));
        const PixelShaderTranslation t = TranslatePixelShader(b.def);
        Check(t.ok(), "constant shader translates");
        bool sawConstant = false;
        for(const auto& constant : t.constants)
        {
            if(constant.index == 0)
            {
                sawConstant = true;
                Check(constant.value[0] > 0.99f, "constant red expanded");
                Check(constant.value[2] > 0.12f && constant.value[2] < 0.13f,
                      "constant blue expanded");
                Check(constant.value[3] > 0.50f && constant.value[3] < 0.51f,
                      "constant alpha expanded");
            }
        }
        Check(sawConstant, "stage constant assigned slot 0");
    }

    // 5. MUX and output BIAS bail out with stable reasons.
    {
        DefBuilder b;
        b.Combiners(1)
            .TextureMode(0, 1)
            .Rgb(0, Inputs(In(T0), In(V0), In(V0), In(V0)),
                 Outputs(DISCARD, DISCARD, R0, OUT_MUX))
            .Alpha(0, Inputs(In(T0, CH_ALPHA), In(V0, CH_ALPHA), In(ZERO), In(ZERO)),
                   Outputs(DISCARD, DISCARD, R0));
        const PixelShaderTranslation t = TranslatePixelShader(b.def);
        Check(!t.ok() && std::strcmp(t.failure, "mux") == 0, "mux bails");
    }
    {
        DefBuilder b;
        b.Combiners(1)
            .TextureMode(0, 1)
            .Rgb(0, Inputs(In(T0), In(V0), In(ZERO), In(ZERO)),
                 Outputs(DISCARD, DISCARD, R0, OUT_BIAS))
            .Alpha(0, Inputs(In(T0, CH_ALPHA), In(V0, CH_ALPHA), In(ZERO), In(ZERO)),
                   Outputs(DISCARD, DISCARD, R0));
        const PixelShaderTranslation t = TranslatePixelShader(b.def);
        Check(!t.ok() && std::strcmp(t.failure, "output_bias") == 0,
              "output bias bails");
    }

    // 6. Reading a texture register whose stage mode is NONE bails.
    {
        DefBuilder b;
        b.Combiners(1)
            .Rgb(0, Inputs(In(T1), In(V0), In(ZERO), In(ZERO)),
                 Outputs(DISCARD, DISCARD, R0))
            .Alpha(0, Inputs(In(T1, CH_ALPHA), In(V0, CH_ALPHA), In(ZERO), In(ZERO)),
                   Outputs(DISCARD, DISCARD, R0));
        const PixelShaderTranslation t = TranslatePixelShader(b.def);
        Check(!t.ok() && std::strcmp(t.failure, "texture_mode_none") == 0,
              "texture mode NONE bails");
    }

    // 7. Reading r0 before any write bails (startup contents unmodeled).
    {
        DefBuilder b;
        b.Combiners(1)
            .TextureMode(0, 1)
            .Rgb(0, Inputs(In(R0), In(T0), In(ZERO), In(ZERO)),
                 Outputs(DISCARD, DISCARD, R1))
            .Alpha(0, Inputs(In(T0, CH_ALPHA), In(V0, CH_ALPHA), In(ZERO), In(ZERO)),
                   Outputs(DISCARD, DISCARD, R1));
        const PixelShaderTranslation t = TranslatePixelShader(b.def);
        Check(!t.ok() && std::strcmp(t.failure, "register_read_before_write") == 0,
              "r0 read before write bails");
    }

    // 8. A*B + C*D through the sum with no stored halves uses r1 scratch;
    //    dual-texture stage keeps two tex loads.
    {
        DefBuilder b;
        b.Combiners(1)
            .TextureMode(0, 1)
            .TextureMode(1, 1)
            .Rgb(0, Inputs(In(T0), In(V0), In(T1), In(V1)),
                 Outputs(DISCARD, DISCARD, R0))
            .Alpha(0, Inputs(In(T0, CH_ALPHA), In(V0, CH_ALPHA), In(ZERO), In(ZERO)),
                   Outputs(DISCARD, DISCARD, R0));
        const PixelShaderTranslation t = TranslatePixelShader(b.def);
        Check(t.ok(), "dual product sum translates");
        Check(t.textures == 2, "dual product uses two tex loads");
        Check(CountToken(t, 0x04) == 1, "sum emitted as one mad");
    }

    // 9. Final combiner A*B+(1-A)*C emits lrp into r0; G drives r0.a.
    {
        DefBuilder b;
        b.Combiners(1)
            .TextureMode(0, 1)
            .Rgb(0, Inputs(In(T0), In(V0), In(ZERO), In(ZERO)),
                 Outputs(DISCARD, DISCARD, R0))
            .Alpha(0, Inputs(In(T0, CH_ALPHA), In(V0, CH_ALPHA), In(ZERO), In(ZERO)),
                   Outputs(DISCARD, DISCARD, R0))
            .Final(Inputs(In(V0, CH_ALPHA), In(R0), In(T0), In(ZERO)),
                   Inputs(In(ZERO), In(ZERO), In(R0, CH_ALPHA), 0));
        const PixelShaderTranslation t = TranslatePixelShader(b.def);
        Check(t.ok(), "final combiner translates");
        Check(CountToken(t, 0x12) == 1, "final combiner emits lrp");
    }

    // 10. Instruction budget: 8 combiners of rgb+alpha exceed ps.1.1.
    {
        DefBuilder b;
        b.Combiners(8).TextureMode(0, 1);
        for(unsigned stage = 0; stage < 8; ++stage)
        {
            b.Rgb(stage, Inputs(In(T0), In(V0), In(ZERO), In(ZERO)),
                  Outputs(DISCARD, DISCARD, R0));
            b.Alpha(stage,
                    Inputs(In(T0, CH_ALPHA), In(V0, CH_ALPHA), In(ZERO), In(ZERO)),
                    Outputs(DISCARD, DISCARD, R0));
        }
        const PixelShaderTranslation t = TranslatePixelShader(b.def);
        Check(!t.ok() && std::strcmp(t.failure, "instruction_budget") == 0,
              "instruction budget bails");
    }

    if(g_failures == 0)
        std::printf("psh_translate: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
