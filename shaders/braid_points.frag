#version 450
#extension GL_GOOGLE_include_directive : require
#include "braid_common.glsl"

layout(location = 0) in vec2 disc;
layout(location = 1) flat in vec3 pointColor;
layout(location = 2) flat in float pointVisibility;
layout(location = 0) out vec4 outColor;

void main() {
    float r = length(disc);
    float aa = max(fwidth(r), 0.001);
    float coverage = 1.0 - smoothstep(1.0 - aa * 0.5, 1.0 + aa * 0.5, r);
    if (coverage < 0.01) discard;
    // A small spherical highlight inside each disc; no broad blurry halo.
    float sphere = sqrt(max(0.0, 1.0 - min(r * r, 1.0)));
    vec3 color = pointColor * (0.72 + 0.28 * sphere + 0.12 * pc.look.y * sphere * sphere);
    outColor = vec4(color, coverage * pointVisibility);
}
