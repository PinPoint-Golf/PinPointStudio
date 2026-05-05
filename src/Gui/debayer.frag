#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

layout(binding = 1) uniform sampler2D source;

void main() {
    // Basic Grayscale pass-through for Bayer data
    // (Actual debayering logic can be expanded here)
    vec4 raw = texture(source, qt_TexCoord0);
    fragColor = vec4(raw.rrr, 1.0) * qt_Opacity;
}
