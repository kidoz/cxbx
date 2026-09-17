#version 450

layout(set = 0, binding = 0) uniform sampler2D textures[4];
layout(set = 0, binding = 1, std140) uniform CombinerConfig
{
    vec4 constants[8];
    uvec4 stageA[8]; // RGB/alpha inputs, RGB/alpha outputs
    uvec4 stageB[8]; // packed C0/C1, runtime register mappings
    uvec4 meta;     // count/flags, final ABCD/EFG, final mappings
    uvec4 meta2;    // final C0/C1, valid runtime constants
} comb;
layout(push_constant) uniform DrawState
{
    vec4 viewport;
    uvec4 stageOp[4];
    uvec4 extra;
} pc;
layout(location = 0) in vec4 color;
layout(location = 1) in vec2 texCoord;
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

void main()
{
    vec4 sampled[4] = vec4[4](texture(textures[0], texCoord), texture(textures[1], texCoord),
                              texture(textures[2], texCoord), texture(textures[3], texCoord));
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
    writeColor(result);
}
