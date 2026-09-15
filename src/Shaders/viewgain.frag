#version 440

// View gain for the impact camera tile (impact_camera_design.md §10.3): a
// plain multiply on the displayed picture, clipped at white, so a 70 µs
// frame whose club body sits at 10–30 of 255 can be aimed and judged on the
// tile. Display only — the recorded pixels are never touched, and the
// level readout beside the tile is computed on the raw frame, not this.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

// NOTE: this UBO must be byte-for-byte identical to the one in viewgain.vert.
// ShaderEffect maps `gain` to the same-named QML property.
layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float gain;
};

layout(binding = 1) uniform sampler2D source;

void main() {
    vec4 c = texture(source, qt_TexCoord0);
    // The source is a premultiplied layer; scale colour, keep alpha.
    fragColor = vec4(min(c.rgb * gain, vec3(c.a)), c.a) * qt_Opacity;
}
