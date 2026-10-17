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

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

// Must match the vertex shader's UBO layout exactly.
layout(std140, binding = 0) uniform UBO {
    mat4  corrMatrix;
    float pattern;   // 0=RGGB, 1=BGGR, 2=GRBG, 3=GBRG (float avoids HLSL cbuffer int/float issues)
    float texW;
    float texH;
    float opacity;
};

layout(binding = 1) uniform sampler2D bayerTex;

// Every read is an exact sensor pixel: a texel centre through the NEAREST
// sampler (texelFetch would do, but GLSL ES 1.0 has none). The mosaic must never
// be filtered: a linear sample between two sites blends a red pixel into its green
// neighbour before the demosaic sees either, which softened the live tile and
// bled colour whenever it was not drawn 1:1 (KB-30, flir_image_clarity.md §3).

ivec2 texSize() { return ivec2(int(texW), int(texH)); }

// A neighbour off the edge is reflected about the edge pixel's neighbour, so it
// lands on a site of the same colour (a clamp would hand back the wrong one).
float site(ivec2 q)
{
    ivec2 last = texSize() - 1;
    q = abs(q);
    q = clamp(min(q, 2 * last - q), ivec2(0), last);   // the clamp only bites on a 1-px texture
    return texture(bayerTex, (vec2(q) + 0.5) / vec2(texW, texH)).r;
}

// The 4x4 sensor block around the fragment, read once: N(x, y) is the
// pixel at base + (x - 1, y - 1). The four demosaics below share it. Flat,
// because GLSL ES before 3.1 has no arrays of arrays.
float n[16];
float N(int x, int y) { return n[y * 4 + x]; }

// Bilinear demosaic of block pixel (x, y), x and y in 1..2, which is the
// sensor pixel pos.
vec3 demosaicAt(int x, int y, ivec2 pos)
{
    float tl = N(x - 1, y - 1), tc = N(x, y - 1), tr = N(x + 1, y - 1);
    float ml = N(x - 1, y),     mc = N(x, y),     mr = N(x + 1, y);
    float bl = N(x - 1, y + 1), bc = N(x, y + 1), br = N(x + 1, y + 1);

    // Shift so the RGGB cell starts at (0,0) for all four pattern variants:
    //   RGGB (0): no shift
    //   BGGR (1): shift (1,1) — B is at (0,0) without shift, R is at (1,1)
    //   GRBG (2): shift (1,0) — G/R row starts with R at column 1
    //   GBRG (3): shift (0,1) — G/B row starts with B at column 1
    int  ipat = int(pattern);
    ivec2 off = ivec2(0);
    if      (ipat == 1) off = ivec2(1, 1);
    else if (ipat == 2) off = ivec2(1, 0);
    else if (ipat == 3) off = ivec2(0, 1);

    ivec2 p = (pos + off) & 1; // (col%2, row%2) in RGGB space

    if (p.x == 0 && p.y == 0)      // R pixel: R=self, G=cross average, B=diagonal average
        return vec3(mc, (ml + mr + tc + bc) * 0.25, (tl + tr + bl + br) * 0.25);
    if (p.x == 1 && p.y == 0)      // Gr pixel (G on R-row): R=h-average, B=v-average
        return vec3((ml + mr) * 0.5, mc, (tc + bc) * 0.5);
    if (p.x == 0 && p.y == 1)      // Gb pixel (G on B-row): R=v-average, B=h-average
        return vec3((tc + bc) * 0.5, mc, (ml + mr) * 0.5);
    return vec3((tl + tr + bl + br) * 0.25, (ml + mr + tc + bc) * 0.25, mc);   // B pixel
}

void main()
{
    // The tile is rarely drawn 1:1, so demosaic the four sensor pixels around
    // this fragment and blend the COLOURS — resampling after the demosaic,
    // never before it.
    vec2  p    = v_uv * vec2(texW, texH) - 0.5;
    ivec2 base = clamp(ivec2(floor(p)), ivec2(0), texSize() - 1);
    vec2  f    = clamp(p - vec2(base), 0.0, 1.0);

    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            n[y * 4 + x] = site(base + ivec2(x - 1, y - 1));

    vec3 c00 = demosaicAt(1, 1, base);
    vec3 c10 = demosaicAt(2, 1, base + ivec2(1, 0));
    vec3 c01 = demosaicAt(1, 2, base + ivec2(0, 1));
    vec3 c11 = demosaicAt(2, 2, base + ivec2(1, 1));

    fragColor = vec4(mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y), opacity);
}
