#version 450
#extension GL_GOOGLE_include_directive : require
#include "braid_common.glsl"

layout(location = 0) in vec3 sphereLocalDirection;

// The closed tubes populate only depth; their colour is never drawn.
void main() {
    if (nestedSphereMode() && insideSphereHole(normalize(sphereLocalDirection))) discard;
}
