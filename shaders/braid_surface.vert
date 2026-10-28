#version 450
#extension GL_GOOGLE_include_directive : require
#include "braid_common.glsl"
#include "polytope.glsl"

const ivec2 corners[6] = ivec2[](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1),
                               ivec2(0, 1), ivec2(1, 0), ivec2(1, 1));

layout(location = 0) out vec3 sphereLocalDirection;
// Coloured polytope tubes only: colour and how squarely the skin faces the
// viewer; every other skin writes depth alone.
layout(location = 1) out vec3 tubeColor;
layout(location = 2) out float tubeFacing;

void main() {
    int cell = gl_VertexIndex / 6;
    ivec2 sampleIndex = ivec2(cell / SURFACE_COLUMNS, cell % SURFACE_COLUMNS)
                     + corners[gl_VertexIndex % 6];
    vec2 material = TAU * vec2(sampleIndex) / vec2(SURFACE_ROWS, SURFACE_COLUMNS);
    if (nestedSphereMode()) {
        sphereLocalDirection = localSphereDirection(material.x, material.y);
    } else {
        sphereLocalDirection = vec3(0.0, 0.0, 1.0);
    }
    tubeColor = vec3(0.0);
    tubeFacing = 1.0;
    if (nestedCubeMode()) {
        // Every instance is one cube: a square grid on each of its six faces.
        // The fragment shader cuts the windows out; spare cells collapse.
        const int side = 87;
        int face = cell / (side * side);
        if (face >= 6) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }
        int faceCell = cell % (side * side);
        vec2 ab = vec2(ivec2(faceCell / side, faceCell % side) + corners[gl_VertexIndex % 6])
                * (2.0 / float(side)) - 1.0;
        sphereLocalDirection = vec3(ab, 0.0);
        float depth = cubeDepth(float(gl_InstanceIndex));
        // A fading cube no longer hides what is behind it.
        if (cubeFade(depth) < 0.5) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }
        vec3 position = nestedCubeSize(depth) * turnCube(roundedCubePoint(cubeFacePoint(face, ab)), depth);
        if (pc.loop.y > 0.0) {
            // Cells reaching the camera are dropped whole, decided at their centre.
            vec2 middle = (vec2(faceCell / side, faceCell % side) + 0.5) * (2.0 / float(side)) - 1.0;
            vec3 centre = nestedCubeSize(depth)
                        * turnCube(roundedCubePoint(cubeFacePoint(face, middle)), depth);
            if (toView(centre).z > 0.9 * cubeCameraDistance()) {
                gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
                return;
            }
        }
        gl_Position = projectPoint(position);
        return;
    }
    if (polytopeMode()) {
        // One instance per edge: a row per point sample along it, 12 around.
        const int around = 12;
        int along = max(int(pc.points.x + 0.5), 1);
        int row = cell / around;
        if (row >= along) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }
        // Rows near the 4D camera are dropped whole, decided at their ends.
        float wStart = polytopePoint4(gl_InstanceIndex, float(row) / float(along)).w;
        float wEnd = polytopePoint4(gl_InstanceIndex, float(row + 1) / float(along)).w;
        float presence = polytopePresence(gl_InstanceIndex);
        if (polytopeTooClose(wStart) || polytopeTooClose(wEnd) || presence < 0.5) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }
        ivec2 corner = ivec2(row, cell % around) + corners[gl_VertexIndex % 6];
        vec3 center, normal, binormal;
        float w;
        polytopeFrame(gl_InstanceIndex, float(corner.x) / float(along), center, normal, binormal, w);
        float angle = TAU * float(corner.y) / float(around);
        vec3 outward = normal * cos(angle) + binormal * sin(angle);
        // A hair inside the points, so they sit on the skin and not under it.
        gl_Position = projectPoint(center + 0.97 * pc.shape.y * polytopeScale(w) * presence * outward);
        vec4 tubes = polytopeData[3];
        if (tubes.x > 0.5) {
            float glow;
            float stretch = float(corner.x) / float(along);
            vec3 viewNormal = toView(outward);
            float diffuse = max(dot(viewNormal, normalize(vec3(-0.65, 0.8, 1.15))), 0.0);
            tubeColor = polytopeTint(gl_InstanceIndex, stretch, w, glow) * pc.look.z
                      * (1.0 - tubes.y) * (tubes.x < 1.5 ? mix(0.6, 1.0, diffuse) : 1.0);
            tubeFacing = tubes.x < 1.5 ? 1.0 : viewNormal.z;
        }
        return;
    }
    if (tesseractMode()) {
        // One instance per edge: 240 rows along it, 24 around its tube.
        const ivec2 grid = ivec2(240, 24);
        if (cell >= grid.x * grid.y) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }
        ivec2 corner = ivec2(cell / grid.y, cell % grid.y) + corners[gl_VertexIndex % 6];
        vec3 outward;
        float w;
        vec3 position = tesseractTubePoint(gl_InstanceIndex, float(corner.x) / float(grid.x),
                                           TAU * float(corner.y) / float(grid.y), outward, w);
        // A hair inside the points, so they sit on the skin and not under it.
        float thickness = pc.shape.y * tesseractScale(w);
        gl_Position = projectPoint(position - 0.03 * thickness * outward);
        return;
    }
    if (hopfMode()) {
        // One instance per fibre: 240 rows along it, 24 around its tube.
        const ivec2 grid = ivec2(240, 24);
        int fibers = max(int(pc.points.w + 0.5), 1);
        float torus = float(gl_InstanceIndex / fibers);
        float fiber = float(gl_InstanceIndex % fibers);
        int row = cell / grid.y;
        // Fibres through the far region are dropped a whole row at a time.
        if (row >= grid.x
            || length(hopfPoint(2.0 * TAU * float(row) / float(grid.x), torus, fiber)) > hopfCutoff()
            || length(hopfPoint(2.0 * TAU * float(row + 1) / float(grid.x), torus, fiber))
                   > hopfCutoff()) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }
        ivec2 corner = ivec2(row, cell % grid.y) + corners[gl_VertexIndex % 6];
        material = TAU * vec2(corner) / vec2(grid);
        vec3 center, normal, binormal;
        hopfFrame(2.0 * material.x, torus, fiber, center, normal, binormal);
        // A hair inside the points, so they sit on the skin and not under it.
        gl_Position = projectPoint(center + 0.97 * pc.shape.y
                                   * (normal * cos(material.y) + binormal * sin(material.y)));
        return;
    }
    if (tentacleMode()) {
        if (gl_InstanceIndex == 0) {
            // The body: a squarer grid over the whole sphere. Its cells are too
            // coarse for tentacles, so the fragment shader cuts their patches out.
            const ivec2 grid = ivec2(240, SURFACE_ROWS * SURFACE_COLUMNS / 240);
            sampleIndex = ivec2(cell / grid.y, cell % grid.y) + corners[gl_VertexIndex % 6];
            material = TAU * vec2(sampleIndex) / vec2(grid);
            sphereLocalDirection = localSphereDirection(material.x, material.y);
            gl_Position = projectPoint(tentacleBodyPoint(sphereLocalDirection));
        } else {
            // One tentacle patch: fine rows from its tip out to the patch rim.
            sphereLocalDirection = vec3(0.0);
            gl_Position = projectPoint(tentacleSurface(
                gl_InstanceIndex - 1, tentaclePatchAngle() * material.x / TAU, material.y));
        }
        return;
    }
    if (archMode()) {
        // Instance 0 is the sphere, the others are the arches, lengthwise rows.
        gl_Position = projectPoint(archGridPoint(material.x, material.y, float(gl_InstanceIndex)));
        return;
    }
    if (smaleMode()) {
        // The everting sphere folds in both directions: a squarer grid again.
        const ivec2 grid = ivec2(240, SURFACE_ROWS * SURFACE_COLUMNS / 240);
        sampleIndex = ivec2(cell / grid.y, cell % grid.y) + corners[gl_VertexIndex % 6];
        material = TAU * vec2(sampleIndex) / vec2(grid);
        gl_Position = projectPoint(smalePoint(0.5 * (material.y - PI), material.x));
        return;
    }
    if (ridgeMode()) {
        // The ridged torus curves both ways, so the same number of cells is
        // regrouped into a squarer grid: 240 along the ring, 192 around the tube.
        const ivec2 grid = ivec2(240, SURFACE_ROWS * SURFACE_COLUMNS / 240);
        sampleIndex = ivec2(cell / grid.y, cell % grid.y) + corners[gl_VertexIndex % 6];
        material = TAU * vec2(sampleIndex) / vec2(grid);
        gl_Position = projectPoint(
            ridgeLinkPoint(ridgePoint(material.x, material.y), gl_InstanceIndex));
        return;
    }
    if (tubeRidges()) {
        // Ridges need more cells around the tube than a smooth one does.
        const ivec2 grid = ivec2(240, SURFACE_ROWS * SURFACE_COLUMNS / 240);
        sampleIndex = ivec2(cell / grid.y, cell % grid.y) + corners[gl_VertexIndex % 6];
        material = TAU * vec2(sampleIndex) / vec2(grid);
    }
    gl_Position = projectPoint(surfacePoint(material.x, material.y, float(gl_InstanceIndex)));
}
