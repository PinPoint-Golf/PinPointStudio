#version 440

// Pass-through vertex shader for the impact tile's view-gain effect
// (impact_camera_design.md §10.3). Marked BATCHABLE in qt6_add_shaders.

layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;
layout(location = 0) out vec2 qt_TexCoord0;

// IMPORTANT: this block must be identical to the one in viewgain.frag.
layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float gain;
};

void main() {
    qt_TexCoord0 = qt_MultiTexCoord0;
    gl_Position = qt_Matrix * qt_Vertex;
}
