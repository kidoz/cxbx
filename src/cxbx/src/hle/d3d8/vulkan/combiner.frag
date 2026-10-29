#version 450

layout(set = 0, binding = 0) uniform sampler2D textures[4];
layout(set = 0, binding = 2) uniform samplerCube cubeTextures[4];
layout(set = 0, binding = 3) uniform sampler3D volumeTextures[4];
layout(set = 0, binding = 1, std140) uniform CombinerConfig
{
    vec4 constants[8];
    uvec4 stageA[8]; // RGB/alpha inputs, RGB/alpha outputs
    uvec4 stageB[8]; // packed C0/C1, runtime register mappings
    uvec4 meta;     // count/flags, final ABCD/EFG, final mappings
    uvec4 meta2;    // final C0/C1, valid runtime constants
    uvec4 meta3;    // PS texture modes, PSInputTexture source, fog start/end
    vec4 bumpMat[3]; // stages 1..3: BUMPENVMAT (M00, M01, M10, M11)
    vec4 bumpLum[2]; // stages 1..2 lum pairs, stage 3 pair, fog density/state
} comb;
layout(push_constant) uniform DrawState
{
    vec4 viewport;
    uvec4 stageOp[4];
    uvec4 extra;
} pc;
layout(location = 0) in vec4 color;
layout(location = 1) in vec4 texCoord[4];
layout(location = 5) in float fogValue;
layout(location = 0) out vec4 outColor;

vec4 constantColor(uint packed, uint mapping)
{
    if(mapping < 8u && (comb.meta2.z & (1u << mapping)) != 0u)
    {
        return comb.constants[mapping];
    }
    return vec4((packed >> 16u) & 255u, (packed >> 8u) & 255u,
                packed & 255u, packed >> 24u) / 255.0;
}

vec3 mappedInput(vec3 value, uint mapping)
{
    vec3 positive = max(value, vec3(0));
    switch(mapping)
    {
        case 0u: return positive;
        case 1u: return 1.0 - positive;
        case 2u: return 2.0 * positive - 1.0;
        case 3u: return 1.0 - 2.0 * positive;
        case 4u: return positive - 0.5;
        case 5u: return 0.5 - positive;
        case 6u: return value;
        default: return -value;
    }
}

vec3 operand(vec4 regs[16], uint inputByte, bool alpha)
{
    vec4 value = regs[inputByte & 15u];
    vec3 channel = (inputByte & 16u) != 0u ? vec3(value.a)
                   : (alpha ? vec3(value.b) : value.rgb);
    return mappedInput(channel, inputByte >> 5u);
}

vec3 mappedOutput(vec3 value, uint flags)
{
    if((flags & 8u) != 0u) { value -= 0.5; }
    uint shift = (flags >> 4u) & 3u;
    float scale = shift == 1u ? 2.0 : (shift == 2u ? 4.0 : (shift == 3u ? 0.5 : 1.0));
    return clamp(value * scale, -1.0, 1.0);
}

void storeResult(inout vec4 regs[16], uint destination, vec3 value, bool alpha)
{
    if(destination == 0u) { return; }
    if(alpha) { regs[destination].a = value.x; }
    else { regs[destination].rgb = value; }
}

// Both portions read the same pre-stage register snapshot.
void portion(vec4 inputs[16], inout vec4 outputs[16], uint program, uint outputWord, bool alpha)
{
    vec3 a = operand(inputs, program >> 24u, alpha);
    vec3 b = operand(inputs, (program >> 16u) & 255u, alpha);
    vec3 c = operand(inputs, (program >> 8u) & 255u, alpha);
    vec3 d = operand(inputs, program & 255u, alpha);
    uint flags = outputWord >> 12u;
    vec3 ab = !alpha && (flags & 2u) != 0u ? vec3(dot(a, b)) : a * b;
    vec3 cd = !alpha && (flags & 1u) != 0u ? vec3(dot(c, d)) : c * d;
    bool muxHigh = (comb.meta.x & 256u) != 0u ? inputs[12].a >= 0.5
        : (uint(clamp(inputs[12].a, 0.0, 1.0) * 255.0) & 1u) != 0u;
    vec3 sum = (flags & 4u) != 0u ? (muxHigh ? cd : ab) : ab + cd;
    ab = mappedOutput(ab, flags);
    cd = mappedOutput(cd, flags);
    storeResult(outputs, (outputWord >> 4u) & 15u, ab, alpha);
    storeResult(outputs, outputWord & 15u, cd, alpha);
    storeResult(outputs, (outputWord >> 8u) & 15u, mappedOutput(sum, flags), alpha);
    if(!alpha && (flags & 128u) != 0u)
    {
        storeResult(outputs, (outputWord >> 4u) & 15u, vec3(ab.b), true);
    }
    if(!alpha && (flags & 64u) != 0u)
    {
        storeResult(outputs, outputWord & 15u, vec3(cd.b), true);
    }
}

vec4 fixedArgument(uint selector, vec4 textureColor, vec4 current)
{
    uint source = selector & 15u;
    vec4 value = source == 2u ? textureColor : (source == 1u ? current : color);
    if((selector & 32u) != 0u) { value = vec4(value.a); }
    if((selector & 16u) != 0u) { value = 1.0 - value; }
    return value;
}

void writeColor(vec4 result)
{
    // D3D8 compares the quantized alpha with an 8-bit reference.
    float alpha = floor(clamp(result.a, 0.0, 1.0) * 255.0 + 0.5);
    float reference = float(pc.extra.z);
    bool passes = true;
    switch(pc.extra.y)
    {
        case 1u: passes = false; break;
        case 2u: passes = alpha < reference; break;
        case 3u: passes = alpha == reference; break;
        case 4u: passes = alpha <= reference; break;
        case 5u: passes = alpha > reference; break;
        case 6u: passes = alpha != reference; break;
        case 7u: passes = alpha >= reference; break;
    }
    if(!passes) { discard; }
    outColor = clamp(result, 0.0, 1.0);
}

bool depthComparePasses(float depth, float reference, uint func)
{
    // d3d8 D3DCMPFUNC enumeration: 1 NEVER .. 8 ALWAYS.
    switch(func)
    {
        case 1u: return false;
        case 2u: return depth < reference;
        case 3u: return depth == reference;
        case 4u: return depth <= reference;
        case 5u: return depth > reference;
        case 6u: return depth != reference;
        case 7u: return depth >= reference;
        default: return true;
    }
}

// NV2A pixel fog runs after the combiner (the raster unit's fog blend):
// D3DRS_FOGENABLE/FOGTABLEMODE/FOGSTART/FOGEND/FOGDENSITY/FOGCOLOR, mirrored
// by the renderer into the spare config words. The Xbox D3DFOGMODE enum
// orders NONE=0, EXP=1, EXP2=2, LINEAR=3. Per the XDK Fog sample contract:
// mode NONE means the vertex program wrote the final fog factor to o[FOG]
// (the hardware passes it through clamped); EXP/EXP2/LINEAR mean the program
// wrote a view-space coordinate and this table lookup derives the factor
// from FOGSTART/FOGEND/FOGDENSITY. Factor 1 leaves the shade, 0 is full fog.
void applyFog(inout vec4 shade)
{
    uint fogWord = floatBitsToUint(comb.bumpLum[1].w);
    if((fogWord & 1u) == 0u)
    {
        return;
    }
    float factor = clamp(fogValue, 0.0, 1.0);
    // bumpLum declares float components but carries the raw float-bit
    // patterns the host wrote; round-trip through the uint representation.
    float density = uintBitsToFloat(floatBitsToUint(comb.bumpLum[1].z));
    switch((fogWord >> 1u) & 7u)
    {
        case 1u: // EXP
            factor = exp(-density * fogValue);
            break;
        case 2u: // EXP2
        {
            float d = density * fogValue;
            factor = exp(-d * d);
            break;
        }
        case 3u: // LINEAR
        {
            float start = uintBitsToFloat(comb.meta3.z);
            float end = uintBitsToFloat(comb.meta3.w);
            float span = end - start;
            factor = clamp((end - fogValue) /
                               (abs(span) < 1.0e-6 ? 1.0 : span), 0.0, 1.0);
            break;
        }
        default: // NONE: the program wrote the factor directly.
            break;
    }
    vec3 fogColor = vec3(float((fogWord >> 24u) & 255u),
                         float((fogWord >> 16u) & 255u),
                         float((fogWord >> 8u) & 255u)) / 255.0;
    shade.rgb = mix(fogColor, shade.rgb, factor);
}

void main()
{
    // Stage i samples its own coordinate set with the D3D projective
    // divide (st / q, q in the set's w). Legacy single-vec2 layouts leave
    // the unused sets at (0,0,0,1), a neutral q of one. Cube stages
    // (extra.w bits 0..3) sample the direction texCoord[i].xyz; volume
    // stages (bits 11..14) sample texCoord[i].xyz; shadow stages
    // (bits 4..7, comparison in bits 8..10) sample the depth image and
    // compare with the set's r after the same divide. PS texture modes
    // BUMPENVMAP (6) and BUMPENVMAP_LUM (7) perturb the divided 2D
    // coordinate with an earlier stage's texel through this stage's bump
    // matrix -- texbem's u' = u + M00*t.R + M10*t.G, v' = v + M01*t.R +
    // M11*t.G, and _LUM scales the sample by t.B*lScale + lOffset. The
    // source is t0 for stage 1 and the PSInputTexture-selected stage for
    // stages 2/3; a source at or after the stage samples plain. Stages
    // run in order, so a bump stage always sees its source's texel.
    vec2 stageTexCoord[4];
    vec4 sampled[4];
    for(uint set = 0u; set < 4u; ++set)
    {
        float q = texCoord[set].w;
        q = abs(q) < 1.0e-6 ? 1.0 : q;
        stageTexCoord[set] = texCoord[set].xy / q;
        uint mode = (comb.meta3.x >> (set * 5u)) & 31u;
        bool bump = set > 0u && (mode == 6u || mode == 7u);
        uint bumpSource = 0u;
        if(bump)
        {
            bumpSource = set == 1u
                             ? 0u
                             : (comb.meta3.y >> ((set - 2u) * 4u)) & 3u;
            bump = bumpSource < set;
        }
        if((pc.extra.w & (16u << set)) != 0u)
        {
            // NV2A shadow sampling: the unit's sampled value is the
            // comparison of the depth texel with the reference r.
            float depth = texture(textures[set], stageTexCoord[set]).r;
            float reference = texCoord[set].z / q;
            float lit = depthComparePasses(depth, reference,
                                           (pc.extra.w >> 8u) & 7u) ? 1.0 : 0.0;
            sampled[set] = vec4(lit);
        }
        else if((pc.extra.w & (2048u << set)) != 0u)
        {
            sampled[set] = texture(volumeTextures[set], texCoord[set].xyz);
        }
        else if(((pc.extra.w >> set) & 1u) != 0u)
        {
            sampled[set] = texture(cubeTextures[set], texCoord[set].xyz);
        }
        else if(bump)
        {
            vec4 source = sampled[bumpSource];
            vec4 matrix = comb.bumpMat[set - 1u];
            vec2 perturbed = stageTexCoord[set] +
                vec2(source.r * matrix.x + source.g * matrix.z,
                     source.r * matrix.y + source.g * matrix.w);
            sampled[set] = texture(textures[set], perturbed);
            if(mode == 7u)
            {
                uint pair = set - 1u;
                vec2 lum = (pair & 1u) == 0u ? comb.bumpLum[pair >> 1u].xy
                                             : comb.bumpLum[pair >> 1u].zw;
                sampled[set] *= vec4(source.b * lum.x + lum.y);
            }
        }
        else
        {
            sampled[set] = texture(textures[set], stageTexCoord[set]);
        }
    }
    if(pc.extra.x == 0u)
    {
        vec4 current = color;
        for(uint stage = 0u; stage < 4u; ++stage)
        {
            uint op = pc.stageOp[stage].x;
            if(op == 1u) { break; }
            vec4 a = fixedArgument(pc.stageOp[stage].y, sampled[stage], current);
            vec4 b = fixedArgument(pc.stageOp[stage].z, sampled[stage], current);
            switch(op)
            {
                case 2u: current = a; break;
                case 3u: current = b; break;
                case 4u: current = a * b; break;
                case 5u: current = 2.0 * a * b; break;
                case 6u: current = 4.0 * a * b; break;
                case 7u: current = a + b; break;
                case 8u: current = a + b - 0.5; break;
            }
            current = clamp(current, 0.0, 1.0);
        }
        applyFog(current);
        writeColor(current);
        return;
    }
    vec4 regs[16];
    for(uint i = 0u; i < 16u; ++i) { regs[i] = vec4(0); }
    regs[4] = color;
    for(uint i = 0u; i < 4u; ++i) { regs[8u + i] = sampled[i]; }
    regs[12].a = sampled[0].a;
    for(uint stage = 0u; stage < min(comb.meta.x & 15u, 8u); ++stage)
    {
        uint c0Stage = (comb.meta.x & 4096u) != 0u ? stage : 0u;
        uint c1Stage = (comb.meta.x & 65536u) != 0u ? stage : 0u;
        regs[1] = constantColor(comb.stageB[c0Stage].x, (comb.stageB[stage].z >> (stage * 4u)) & 15u);
        regs[2] = constantColor(comb.stageB[c1Stage].y, (comb.stageB[stage].w >> (stage * 4u)) & 15u);
        vec4 previous[16] = regs;
        portion(previous, regs, comb.stageA[stage].y, comb.stageA[stage].w, true);
        portion(previous, regs, comb.stageA[stage].x, comb.stageA[stage].z, false);
    }
    vec4 result = regs[12];
    if(comb.meta.y != 0u || (comb.meta.z & 0xFFFFFF00u) != 0u)
    {
        regs[1] = constantColor(comb.meta2.x, comb.meta.w & 15u);
        regs[2] = constantColor(comb.meta2.y, (comb.meta.w >> 4u) & 15u);
        vec3 v1 = (comb.meta.z & 64u) != 0u ? 1.0 - regs[5].rgb : regs[5].rgb;
        vec3 r0 = (comb.meta.z & 32u) != 0u ? 1.0 - regs[12].rgb : regs[12].rgb;
        regs[14].rgb = v1 + r0;
        if((comb.meta.z & 128u) != 0u) { regs[14].rgb = clamp(regs[14].rgb, 0.0, 1.0); }
        regs[15].rgb = operand(regs, comb.meta.z >> 24u, false)
                      * operand(regs, (comb.meta.z >> 16u) & 255u, false);
        vec3 a = operand(regs, comb.meta.y >> 24u, false);
        vec3 b = operand(regs, (comb.meta.y >> 16u) & 255u, false);
        vec3 c = operand(regs, (comb.meta.y >> 8u) & 255u, false);
        vec3 d = operand(regs, comb.meta.y & 255u, false);
        result = vec4(a * b + (1.0 - a) * c + d,
                      operand(regs, (comb.meta.z >> 8u) & 255u, true).x);
    }
    applyFog(result);
    writeColor(result);
}
