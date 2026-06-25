#version 440

// Theme-reactive topographic contour background for PinPoint Studio.
// Renders animated iso-lines through an fbm value-noise height field.
// Line colour ramps across three theme-supplied stops by elevation.
// Output is premultiplied alpha so it composites over the screen's
// existing themed background (or paints its own when fillBackground > 0.5).
//
// Interaction: a hover "lift" raises the terrain under the cursor (contours
// bunch around it), and up to three click/tap "ripples" send expanding,
// decaying rings through the field. Both perturb the height before the
// contour pass, so the existing line look is preserved.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

// NOTE: this UBO must be byte-for-byte identical to the one in topo.vert.
// ShaderEffect maps each member (except qt_Matrix / qt_Opacity) to a
// same-named QML property on the ShaderEffect element.
layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;        // provided by ShaderEffect
    float qt_Opacity;       // provided by ShaderEffect
    float time;             // seconds * speed, drives evolution
    vec2  resolution;       // item size in px, for aspect correction
    vec4  colorLow;         // warm / low-elevation stop
    vec4  colorMid;         // mid stop
    vec4  colorHigh;        // cool / high-elevation stop
    vec4  backgroundColor;  // used only when fillBackground > 0.5
    float levels;           // number of contour bands
    float lineWidth;        // line thickness in screen px (anti-aliased)
    float noiseScale;       // spatial frequency of the terrain
    float intensity;        // overall line opacity 0..1
    float fillBackground;   // 0 = overlay only, 1 = paint background
    // -- interaction --------------------------------------------------------
    vec2  mouse;            // cursor in 0..1 UV (hover gated by `hover`)
    float hover;            // 0..1 hover strength (eased host-side)
    vec3  ripple0;          // xy = UV centre, z = progress 0..1 (<0 = inactive)
    vec3  ripple1;
    vec3  ripple2;
    float hoverLift;        // height bump under cursor
    float hoverRadius;      // bump falloff radius (UV, aspect-corrected)
    float rippleRadius;     // max ring travel (UV)
    float rippleWidth;      // ring thickness (UV)
    float rippleAmp;        // ring height
};

// Compact hash (iq-style) -> [0,1)
float hash(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// Trilinear value noise
float vnoise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i + vec3(0.0, 0.0, 0.0)), hash(i + vec3(1.0, 0.0, 0.0)), f.x),
                   mix(hash(i + vec3(0.0, 1.0, 0.0)), hash(i + vec3(1.0, 1.0, 0.0)), f.x), f.y),
               mix(mix(hash(i + vec3(0.0, 0.0, 1.0)), hash(i + vec3(1.0, 0.0, 1.0)), f.x),
                   mix(hash(i + vec3(0.0, 1.0, 1.0)), hash(i + vec3(1.0, 1.0, 1.0)), f.x), f.y), f.z);
}

// 4-octave fractional Brownian motion -> [0,1]
float fbm(vec3 p) {
    float amp = 0.5, total = 0.0, norm = 0.0;
    for (int o = 0; o < 4; o++) {
        total += amp * vnoise(p);
        norm  += amp;
        p     *= 2.0;
        amp   *= 0.5;
    }
    return total / norm;
}

// Three-stop ramp by elevation t in [0,1]
vec3 ramp(float t) {
    t = clamp(t, 0.0, 1.0);
    if (t < 0.5) return mix(colorLow.rgb,  colorMid.rgb,  t * 2.0);
    return            mix(colorMid.rgb,  colorHigh.rgb, (t - 0.5) * 2.0);
}

// A single expanding, decaying ring perturbation in aspect-corrected UV.
float ripContribution(vec3 r, vec2 auv, float aspect) {
    if (r.z < 0.0) return 0.0;                       // inactive slot
    vec2 rc = vec2(r.x * aspect, r.y);
    float d = distance(auv, rc);
    float radius = r.z * rippleRadius;               // ring grows with progress
    float ring = exp(-pow((d - radius) / max(rippleWidth, 1e-4), 2.0));
    return rippleAmp * ring * (1.0 - r.z);           // fade as it expands
}

void main() {
    vec2 uv = qt_TexCoord0;
    float aspect = resolution.x / max(resolution.y, 1.0);

    // Aspect-corrected screen UV, used for both noise sampling and interaction
    // so hover/ripple radii stay in consistent screen-fraction units.
    vec2 auv = vec2(uv.x * aspect, uv.y);
    vec2 p = auv * noiseScale;

    float h = fbm(vec3(p, time));

    // Pointer lift: raise terrain under the cursor (contours bunch around it).
    // Gated by `hover`, which is eased to 0 when the pointer leaves.
    vec2 am = vec2(mouse.x * aspect, mouse.y);
    float dm = distance(auv, am);
    h += hover * hoverLift * exp(-(dm * dm) / max(hoverRadius * hoverRadius, 1e-5));

    // Click / tap ripples.
    h += ripContribution(ripple0, auv, aspect);
    h += ripContribution(ripple1, auv, aspect);
    h += ripContribution(ripple2, auv, aspect);

    // Distance to the nearest contour line, normalised to screen pixels via
    // fwidth so line thickness stays constant regardless of terrain slope.
    float v  = h * levels;
    float df = 0.5 - abs(fract(v) - 0.5);          // 0 at a line, 0.5 between
    float g  = df / max(fwidth(v), 1e-5);          // distance in "line units"
    float line = 1.0 - smoothstep(0.0, lineWidth, g);

    vec3 lineCol = ramp(h);

    vec3 rgb;
    float a;
    if (fillBackground > 0.5) {
        rgb = mix(backgroundColor.rgb, lineCol, line * intensity);
        a = 1.0;
    } else {
        rgb = lineCol;
        a = line * intensity;
    }

    // Premultiplied alpha for the Qt scene graph.
    fragColor = vec4(rgb * a, a) * qt_Opacity;
}
