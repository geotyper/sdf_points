// Origami (mode 23): a Platonic solid unfolding on hinges into its flat net
// and folding on through into the mirror solid (Origami.hpp poses the faces).
// Every face is a fan of triangles from its centre to its rim; the points sit
// in rows parallel to the rim, the depth skin is a mesh of the same fan. A
// frame narrower than 1 leaves only the rows near the rim, a window inside.
//
// Push constants: shape = size, frame width (1 = solid faces), how far the
// faces have swollen onto the circumsphere, corners per face; points = rows
// per fan triangle, row offset (rows the window takes), pixel radius, 0;
// look = tilt, glow, brightness, swirl angle at the rim; style.rgb = 0, w = 23;
// wave.w = spin rate; motion = faces, scene reach, hinge glow, flash;
// loop = cycles, 0, 0, zoom.
// The polytope storage buffer holds origamiFaceVec4 = 8 vec4 per face: centre
// and fold, normal and depth, colour and hinge corner (-1 at the root), then
// the corners.

vec4 origamiFace(int face, int slot) {
    return polytopeData[8 * face + slot];
}

int origamiSides() {
    return int(pc.shape.w + 0.5);
}

float origamiSpan() {
    return pc.shape.x * pc.motion.y * 1.08 / max(pc.loop.w, 0.05);
}

// Rows from rho0 = offset / (offset + rows) out to the rim.
float origamiInnerRadius() {
    return pc.points.y / (pc.points.y + pc.points.x);
}

// The point at radius `rho` (0 centre .. 1 rim) of fan triangle `fan`, `u`
// along its stretch of rim; `normal` is the face's, bent with the swelling.
vec3 origamiPoint(int face, int fan, float rho, float u, out vec3 normal) {
    int sides = origamiSides();
    vec3 centre = origamiFace(face, 0).xyz;
    vec3 a = origamiFace(face, 3 + fan).xyz, b = origamiFace(face, 3 + (fan + 1) % sides).xyz;
    vec3 p = centre + rho * (mix(a, b, u) - centre);
    normal = origamiFace(face, 1).xyz;
    // Closed, the faces swell onto the sphere through their corners.
    float swell = pc.shape.z;
    if (swell > 0.0) {
        vec3 radial = normalize(p + vec3(1e-6, 0.0, 0.0));
        p = mix(p, radial, swell);
        normal = normalize(mix(normal, radial * sign(dot(radial, normal) + 1e-6), swell));
    }
    // Lying flat the net swirls about the vertical, more towards its rim.
    float reach = length(p.xz) / max(pc.motion.y, 1e-3);
    float swirl = pc.look.w * reach * reach;
    mat2 turn = mat2(cos(swirl), sin(swirl), -sin(swirl), cos(swirl));
    p.xz = turn * p.xz;
    normal.xz = turn * normal.xz;
    float spin = timePhase(pc.wave.w);
    mat2 spinTurn = mat2(cos(spin), sin(spin), -sin(spin), cos(spin));
    p.xz = spinTurn * p.xz;
    normal.xz = spinTurn * normal.xz;
    return pc.shape.x * p;
}
