#version 450
#extension GL_GOOGLE_include_directive : require
#include "braid_common.glsl"

layout(location = 0) out vec2 disc;
layout(location = 1) flat out vec3 pointColor;
layout(location = 2) flat out float pointVisibility;
layout(set = 0, binding = 0) uniform sampler2D surfaceDepth;

float depthAtCenter(vec2 ndc) {
    // Manual bilinear interpolation: depth formats need not support linear
    // filtering. Query exactly the anchor, never individual billboard pixels.
    ivec2 size = textureSize(surfaceDepth, 0);
    vec2 pixel = (ndc * 0.5 + 0.5) * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(pixel));
    vec2 f = fract(pixel);
    float a = texelFetch(surfaceDepth, clamp(base, ivec2(0), size - 1), 0).r;
    float b = texelFetch(surfaceDepth, clamp(base + ivec2(1, 0), ivec2(0), size - 1), 0).r;
    float c = texelFetch(surfaceDepth, clamp(base + ivec2(0, 1), ivec2(0), size - 1), 0).r;
    float d = texelFetch(surfaceDepth, clamp(base + ivec2(1, 1), ivec2(0), size - 1), 0).r;
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

const vec2 corners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(-1, 1),
                             vec2(-1, 1), vec2(1, -1), vec2(1, 1));

void main() {
    int rows = int(pc.points.x), columns = int(pc.points.y);
    int strand = gl_InstanceIndex / (rows * columns);
    int pointIndex = gl_InstanceIndex % (rows * columns);
    int row = pointIndex / columns, column = pointIndex % columns;
    // Fixed IDs and staggered material coordinates: no per-frame re-sampling.
    float u = TAU * (float(row) + 0.5 * float(column % 2)) / float(rows);
    float v = TAU * float(column) / float(columns);
    vec3 position = surfacePoint(u, v, float(strand));
    vec3 normal = toView(surfaceNormal(u, v, float(strand)));
    disc = corners[gl_VertexIndex];

    vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
    float diffuse = max(dot(normal, light), 0.0);
    float illumination = 0.10 + 0.90 * pow(diffuse, 0.8);
    // Radius changes with light, never with the local UV stretch or orientation.
    float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                       * mix(0.36, 1.0, illumination);
    radiusPixels = max(radiusPixels, 0.30);
    vec3 tint = mix(vec3(0.55, 0.43, 0.58), vec3(1.0, 0.87, 0.77), diffuse);
    if (pc.style.w > 2.5) {
        tint = mix(vec3(0.38, 0.48, 0.88), vec3(0.86, 1.0, 0.94), diffuse);
    }
    if (pc.style.x >= 0.0) {
        tint = pc.style.rgb;
    }
    pointColor = tint * illumination * pc.look.z;

    vec4 clip = projectPoint(position);
    float clearance = depthAtCenter(clip.xy) + 0.0004 - clip.z;
    pointVisibility = smoothstep(-0.00015, 0.00015, clearance)
                    * smoothstep(-0.015, 0.025, normal.z);
    clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
    if (pointVisibility <= 0.001) {
        clip = vec4(2.0, 2.0, 2.0, 1.0);
    }
    gl_Position = clip;
    // Keep the antialias fringe one physical pixel wide at every viewport size.
    disc *= (radiusPixels + 0.65) / radiusPixels;
}
