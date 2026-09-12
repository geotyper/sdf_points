#version 450
#extension GL_GOOGLE_include_directive : require
#include "braid_common.glsl"

const ivec2 corners[6] = ivec2[](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1),
                               ivec2(0, 1), ivec2(1, 0), ivec2(1, 1));

void main() {
    int cell = gl_VertexIndex / 6;
    ivec2 sampleIndex = ivec2(cell / SURFACE_COLUMNS, cell % SURFACE_COLUMNS)
                     + corners[gl_VertexIndex % 6];
    vec2 material = TAU * vec2(sampleIndex) / vec2(SURFACE_ROWS, SURFACE_COLUMNS);
    if (nestedSphereMode()) {
        ivec2 cellIndex = ivec2(cell / SURFACE_COLUMNS, cell % SURFACE_COLUMNS);
        vec2 cellCenter = TAU * (vec2(cellIndex) + vec2(0.5))
                        / vec2(SURFACE_ROWS, SURFACE_COLUMNS);
        if (insideSphereHole(localSphereDirection(cellCenter.x, cellCenter.y))) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }
    }
    gl_Position = projectPoint(surfacePoint(material.x, material.y, float(gl_InstanceIndex)));
}
