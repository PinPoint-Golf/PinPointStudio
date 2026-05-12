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

void main()
{
    vec2 d = vec2(1.0 / texW, 1.0 / texH);

    // Sample 3x3 neighbourhood in the R8 Bayer texture.
    float tl = texture(bayerTex, v_uv + vec2(-d.x, -d.y)).r;
    float tc = texture(bayerTex, v_uv + vec2(  0,  -d.y)).r;
    float tr = texture(bayerTex, v_uv + vec2( d.x, -d.y)).r;
    float ml = texture(bayerTex, v_uv + vec2(-d.x,   0 )).r;
    float mc = texture(bayerTex, v_uv                   ).r;
    float mr = texture(bayerTex, v_uv + vec2( d.x,   0 )).r;
    float bl = texture(bayerTex, v_uv + vec2(-d.x,  d.y)).r;
    float bc = texture(bayerTex, v_uv + vec2(  0,   d.y)).r;
    float br = texture(bayerTex, v_uv + vec2( d.x,  d.y)).r;

    // Integer pixel coordinate in the Bayer grid.
    ivec2 pos = ivec2(v_uv * vec2(texW, texH));

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

    float red, green, blue;

    if (p.x == 0 && p.y == 0) {
        // R pixel: R=self, G=cross average, B=diagonal average
        red   = mc;
        green = (ml + mr + tc + bc) * 0.25;
        blue  = (tl + tr + bl + br) * 0.25;
    } else if (p.x == 1 && p.y == 0) {
        // Gr pixel (G on R-row): G=self, R=h-average, B=v-average
        red   = (ml + mr) * 0.5;
        green = mc;
        blue  = (tc + bc) * 0.5;
    } else if (p.x == 0 && p.y == 1) {
        // Gb pixel (G on B-row): G=self, R=v-average, B=h-average
        red   = (tc + bc) * 0.5;
        green = mc;
        blue  = (ml + mr) * 0.5;
    } else {
        // B pixel: B=self, G=cross average, R=diagonal average
        red   = (tl + tr + bl + br) * 0.25;
        green = (ml + mr + tc + bc) * 0.25;
        blue  = mc;
    }

    fragColor = vec4(red, green, blue, opacity);
}
