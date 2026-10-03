#version 450
#extension GL_GOOGLE_include_directive : require
#include "braid_common.glsl"

layout(location = 0) out vec2 disc;
layout(location = 1) flat out vec3 pointColor;
layout(location = 2) flat out float pointFacing;

const vec2 corners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(-1, 1),
                             vec2(-1, 1), vec2(1, -1), vec2(1, 1));

void main() {
    disc = corners[gl_VertexIndex];
    if (nestedSphereMode()) {
        int pointsPerSphere = max(int(pc.motion.z + 0.5), 1);
        int sphereIndex = int(pc.motion.y + 0.5);
        int pointIndex = gl_InstanceIndex;
        const float goldenAngle = 2.39996322973;
        float y = 1.0 - 2.0 * (float(pointIndex) + 0.5) / float(pointsPerSphere);
        float radial = sqrt(max(0.0, 1.0 - y * y));
        float angle = goldenAngle * float(pointIndex);
        vec3 localDirection = vec3(radial * cos(angle), y, radial * sin(angle));
        vec3 position = nestedSphereCenter(float(sphereIndex))
                      + nestedSphereRadius(float(sphereIndex))
                      * rotateSphereDirection(localDirection, float(sphereIndex));
        vec3 normal = toView(rotateSphereDirection(localDirection, float(sphereIndex)));
        vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
        float diffuse = abs(dot(normal, light));
        float illumination = 0.10 + 0.90 * pow(diffuse, 0.8);
        float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                           * mix(0.36, 1.0, illumination);
        radiusPixels = max(radiusPixels, 0.30);
        vec3 tint = pc.style.x >= 0.0 ? pc.style.rgb : vec3(0.55, 0.72, 1.0);
        pointColor = tint * illumination * pc.look.z;
        // The fragment shader uses the sign to select the matching ray/sphere
        // intersection before evaluating the animated hole mask.
        pointFacing = normal.z;

        vec4 clip = projectPoint(position);
        clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
        gl_Position = clip;
        disc *= (radiusPixels + 0.65) / radiusPixels;
        return;
    }

    if (eversionMode() || sleeveMode() || morphMode() || ridgeMode()) {
        int pointCount = max(int(pc.points.x + 0.5), 1);
        const float goldenAngle = 2.39996322973;
        // Equal-area samples of the cloth. Each point keeps its material coordinates.
        float material = (float(gl_InstanceIndex) + 0.5) / float(pointCount);
        float longitude = goldenAngle * float(gl_InstanceIndex);
        if (pc.loop.z > 0.5) {
            // A whole-number step returns to the first point after the last one,
            // so the lattice has no seam where the material wraps around.
            uint turned = uint(gl_InstanceIndex) * uint(pc.loop.z + 0.5) % uint(pointCount);
            longitude = TAU * float(turned) / float(pointCount);
        }
        vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
        vec3 position, normal, tint;
        float diffuse;
        if (ridgeMode()) {
            float angle = vortexAngle(fract(material + timePhase(1.0) / TAU));
            position = ridgePoint(longitude, angle);
            normal = toView(ridgeNormal(longitude, angle));
            diffuse = max(dot(normal, light), 0.0);
            // The ridges take their colour as they rise out of the body.
            tint = mix(pc.motion.rgb, pc.wave.rgb,
                       ridgeCrest(longitude, material) * ridgeGrowth());
        } else if (morphMode()) {
            vec4 section = morphProfile(material, true);
            position = sleeveAround(longitude, section.xy);
            normal = toView(sleeveAround(longitude, section.zw));
            diffuse = max(dot(normal, light), 0.0);
            tint = fract(material * pc.shape.w) < 0.5 ? pc.wave.rgb : pc.motion.rgb;
        } else if (sleeveMode()) {
            position = sleevePoint(longitude, material);
            normal = toView(sleeveNormal(longitude, material));
            diffuse = max(dot(normal, light), 0.0);
            // Bands of material: the sleeve shows another one as it turns out.
            tint = fract(material * pc.shape.w) < 0.5 ? pc.wave.rgb : pc.motion.rgb;
        } else {
            position = eversionPoint(longitude, material);
            normal = toView(eversionNormal(longitude, material));
            diffuse = abs(dot(normal, light));
            // Both sides stay visible; the colour tells which one faces the camera.
            tint = normal.z > 0.0 ? pc.wave.rgb : pc.motion.rgb;
        }
        float illumination = 0.10 + 0.90 * pow(diffuse, 0.8);
        float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                           * mix(0.36, 1.0, illumination);
        radiusPixels = max(radiusPixels, 0.30);
        pointColor = tint * illumination * pc.look.z;
        // The punctured sphere shows both sides of its cloth. The other bodies
        // are closed, so points turned away would only bleed past the silhouette.
        pointFacing = eversionMode() ? 1.0 : smoothstep(-0.015, 0.025, normal.z);

        vec4 clip = projectPoint(position);
        clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
        gl_Position = clip;
        disc *= (radiusPixels + 0.65) / radiusPixels;
        return;
    }

    int rows = int(pc.points.x), columns = int(pc.points.y);
    int strand = gl_InstanceIndex / (rows * columns);
    int pointIndex = gl_InstanceIndex % (rows * columns);
    int row = pointIndex / columns, column = pointIndex % columns;
    // Fixed IDs and staggered material coordinates: no per-frame re-sampling.
    float u = TAU * (float(row) + 0.5 * float(column % 2)) / float(rows);
    float v = TAU * float(column) / float(columns);
    vec3 position = surfacePoint(u, v, float(strand));
    vec3 normal = toView(surfaceNormal(u, v, float(strand)));
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
    if (vortexMode()) {
        // Strands change colour on their way from the rim into the hole.
        float inward = 0.5 - 0.5 * cos(vortexStrandAngle(u, float(strand)));
        tint = mix(pc.motion.rgb, pc.wave.rgb, smoothstep(0.15, 0.85, inward));
    }
    if (pc.style.x >= 0.0) {
        tint = pc.style.rgb;
    }
    pointColor = tint * illumination * pc.look.z;
    pointFacing = smoothstep(-0.015, 0.025, normal.z);

    vec4 clip = projectPoint(position);
    clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
    gl_Position = clip;
    // Keep the antialias fringe one physical pixel wide at every viewport size.
    disc *= (radiusPixels + 0.65) / radiusPixels;
}
