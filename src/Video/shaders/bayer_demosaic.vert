#version 440

layout(location = 0) in vec4 a_pos;
layout(location = 1) in vec2 a_uv;

layout(location = 0) out vec2 v_uv;

layout(std140, binding = 0) uniform UBO {
    mat4  corrMatrix;  // rhi->clipSpaceCorrMatrix() to handle Y-axis differences
    float pattern;     // float to avoid int/float mixing in HLSL cbuffer
    float texW;
    float texH;
    float opacity;
};

void main()
{
    v_uv = a_uv;
    gl_Position = corrMatrix * a_pos;
}
