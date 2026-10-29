#version 450

layout(push_constant) uniform Viewport
{
    vec4 viewport;
    // Texcoord layout mode for this draw: 0 broadcasts set 0 to every
    // stage (legacy single-vec2 layouts); 4 keeps the program's per-stage
    // sets with their projective q.
    layout(offset = 96) uint texCoordSets;
    // Bit 0: the draw supplies the o[FOG] fog coordinate at location 6;
    // without it the varying falls back to 1.0 (no fog).
    layout(offset = 100) uint fogBits;
} pc;
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec4 inTexCoord[4];
layout(location = 6) in float inFog;
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 texCoord[4];
layout(location = 5) out float fogValue;

void main()
{
    color = inColor;
    fogValue = (pc.fogBits & 1u) != 0u ? inFog : 1.0;
    if(pc.texCoordSets == 4u)
    {
        texCoord = inTexCoord;
    }
    else if(pc.texCoordSets == 5u)
    {
        // Single vec4 set (shadow-map u/v/r/q): broadcast set 0; sets 1..3
        // have no bound attributes.
        texCoord = vec4[4](inTexCoord[0], inTexCoord[0], inTexCoord[0],
                           inTexCoord[0]);
    }
    else if(pc.texCoordSets == 3u)
    {
        // Single vec3 set (cube-map directions): broadcast set 0 with a
        // neutral projective q.
        texCoord = vec4[4](vec4(inTexCoord[0].xyz, 1.0),
                           vec4(inTexCoord[0].xyz, 1.0),
                           vec4(inTexCoord[0].xyz, 1.0),
                           vec4(inTexCoord[0].xyz, 1.0));
    }
    else
    {
        texCoord = vec4[4](inTexCoord[0], inTexCoord[0], inTexCoord[0],
                           inTexCoord[0]);
    }
    float w = 1.0 / inPosition.w;
    vec2 ndc = ((inPosition.xy + 0.5 - pc.viewport.xy) / pc.viewport.zw) * 2.0 - 1.0;
    gl_Position = vec4(ndc * w, inPosition.z * w, w);
}
