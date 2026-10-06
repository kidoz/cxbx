#version 450

layout(push_constant) uniform Viewport
{
    vec4 viewport;
    // Texcoord layout mode for this draw: 0 broadcasts set 0 to every
    // stage (legacy single-vec2 layouts); 4 keeps the program's per-stage
    // sets with their projective q.
    layout(offset = 96) uint texCoordSets;
} pc;
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec4 inTexCoord[4];
layout(location = 0) out vec4 color;
layout(location = 1) out vec4 texCoord[4];

void main()
{
    color = inColor;
    if(pc.texCoordSets == 4u)
    {
        texCoord = inTexCoord;
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
