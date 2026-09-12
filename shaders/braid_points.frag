#version 450
#extension GL_GOOGLE_include_directive : require
#include "braid_common.glsl"

layout(location = 0) in vec2 disc;
layout(location = 1) flat in vec3 pointColor;
layout(location = 2) flat in float pointFacing;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D surfaceDepth;

void main() {
    float r = length(disc);
    float aa = max(fwidth(r), 0.001);
    float coverage = 1.0 - smoothstep(1.0 - aa * 0.5, 1.0 + aa * 0.5, r);
    if (coverage < 0.01) discard;
    ivec2 depthSize = textureSize(surfaceDepth, 0);
    ivec2 depthPixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), depthSize - 1);
    float depth = texelFetch(surfaceDepth, depthPixel, 0).r;
    float visibility = smoothstep(-0.00015, 0.00015, depth + 0.0004 - gl_FragCoord.z);
    if (visibility <= 0.001 || pointFacing <= 0.001) discard;
    // A small spherical highlight inside each disc; no broad blurry halo.
    float sphere = sqrt(max(0.0, 1.0 - min(r * r, 1.0)));
    vec3 color = pointColor * (0.72 + 0.28 * sphere + 0.12 * pc.look.y * sphere * sphere);
    outColor = vec4(color, coverage * visibility * pointFacing);
}
