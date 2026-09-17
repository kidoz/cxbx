#version 450

layout(push_constant) uniform Viewport { vec4 viewport; } pc;
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 0) out vec4 color;
layout(location = 1) out vec2 texCoord;

void main()
{
    color = inColor;
    texCoord = inTexCoord;
    float w = 1.0 / inPosition.w;
    vec2 ndc = ((inPosition.xy + 0.5 - pc.viewport.xy) / pc.viewport.zw) * 2.0 - 1.0;
    gl_Position = vec4(ndc * w, inPosition.z * w, w);
}
