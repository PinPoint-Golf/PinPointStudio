/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
#version 440

// The display pass over a camera tile's picture (PpCameraFrame.qml), two
// display-only steps on each of R, G and B, in this order:
//   1. the view gain (impact_camera_design.md §10.3): a plain multiply,
//      clipped at white, so a 70 µs impact frame whose club body sits at
//      10–30 of 255 can be aimed and judged on the tile;
//   2. the display tone curve (#183; src/Video/display_tone.h): in^toneExponent,
//      which lifts a raw-Bayer camera's linear picture for the eye. The
//      exponent comes from the C++ rule per stream (displayToneFor); it is 1
//      for a phone or webcam stream, which is already tone-mapped. The number
//      never appears here or in QML.
// The recorded pixels are never touched, and the level readout beside the
// tile is computed on the raw frame, not this.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

// NOTE: this UBO must be byte-for-byte identical to the one in viewgain.vert.
// ShaderEffect maps `gain` and `toneExponent` to the same-named QML properties.
layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float gain;
    float toneExponent;
};

layout(binding = 1) uniform sampler2D source;

void main() {
    vec4 c = texture(source, qt_TexCoord0);
    // The source is a premultiplied layer: un-premultiply, curve, premultiply
    // again. Alpha 0 (the letterbox) passes through as transparent.
    vec3 rgb = c.a > 0.0 ? c.rgb / c.a : vec3(0.0);
    rgb = pow(min(rgb * gain, vec3(1.0)), vec3(toneExponent));
    fragColor = vec4(rgb * c.a, c.a) * qt_Opacity;
}
