#version 440

// Pass-through vertex shader for the topographic background effect.
// Mark BATCHABLE in qt6_add_shaders so Qt Quick can rewrite qt_Matrix
// handling for scene-graph batching.

layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;
layout(location = 0) out vec2 qt_TexCoord0;

// IMPORTANT: this block must be identical to the one in topo.frag.
// Both stages share binding 0; a mismatch is a validation error.
layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float time;
    vec2  resolution;
    vec4  colorLow;
    vec4  colorMid;
    vec4  colorHigh;
    vec4  backgroundColor;
    float levels;
    float lineWidth;
    float noiseScale;
    float intensity;
    float fillBackground;
    // -- interaction --
    vec2  mouse;
    float hover;
    vec3  ripple0;
    vec3  ripple1;
    vec3  ripple2;
    float hoverLift;
    float hoverRadius;
    float rippleRadius;
    float rippleWidth;
    float rippleAmp;
    vec4  accentColor;
    float rippleTint;
    // -- occasional light sweep --
    vec4  flash;        // xy = sweep dir (aspect-UV, unit), z = wavefront pos, w = strength
    vec4  flashColor;   // colour of the travelling glint
    float flashWidth;   // band half-width (aspect-UV)
};

void main() {
    qt_TexCoord0 = qt_MultiTexCoord0;
    gl_Position = qt_Matrix * qt_Vertex;
}
