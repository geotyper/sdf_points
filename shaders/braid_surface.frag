#version 450
#extension GL_GOOGLE_include_directive : require
#include "braid_common.glsl"

layout(location = 0) in vec3 sphereLocalDirection;
layout(location = 1) in vec3 tubeColor;
layout(location = 2) in float tubeFacing;
layout(location = 0) out vec4 outColor;

// The closed tubes populate depth; only the coloured polytope tubes' pipeline
// writes the colour too, shaded down to black towards the rim.
void main() {
    if (nestedSphereMode() && insideSphereHole(normalize(sphereLocalDirection))) discard;
    // Nested cubes pass the face coordinates instead.
    if (nestedCubeMode() && insideCubeWindow(sphereLocalDirection.xy)) discard;
    if (tentacleMode() && dot(sphereLocalDirection, sphereLocalDirection) > 0.25) {
        int index;
        float angle, turn;
        tentacleNearest(normalize(sphereLocalDirection), index, angle, turn);
        if (angle < tentaclePatchAngle()) discard;
    }
    outColor = vec4(tubeColor * pow(clamp(tubeFacing, 0.0, 1.0), 0.8), 1.0);
}
