// Folding figures (mode 23): flat pieces on hinges or creases (Folding.hpp
// poses them on the CPU every frame). Every face is a fan of triangles from
// its centre to its rim; its points sit in rows parallel to the rim, the depth
// skin is a mesh of the same fan. With a frame narrower than 1 only the rows
// near the rim are left, a window inside.
//
// Push constants: shape = size, 0, how far the faces have swollen onto the
// unit sphere, the most corners of any face; points = all points, 0, pixel
// radius, skin cells per fan side; look = tilt, glow, brightness, swirl angle
// at the rim; style.w = 23; wave.w = spin rate; motion = faces, scene reach,
// hinge glow, flash; loop = cycles, 0, 0, zoom.
// The polytope storage buffer holds origamiFaceVec4 = 9 vec4 per face: centre
// and hinge glow, normal and corners, colour and hinge corner (-1 none),
// (first point, rows, window rows, glow), then the corners.

const int ORIGAMI_FACE = 9;

vec4 origamiFace(int face, int slot) {
    return polytopeData[ORIGAMI_FACE * face + slot];
}

int origamiSides(int face) {
    return int(origamiFace(face, 1).w + 0.5);
}

float origamiSpan() {
    return pc.shape.x * pc.motion.y * 1.08 / max(pc.loop.w, 0.05);
}

// Rows from offset / (offset + rows) of the way out to the rim.
float origamiInnerRadius(int face) {
    vec4 rows = origamiFace(face, 3);
    return rows.z / (rows.z + rows.y);
}

// The face holding point `index`: the last one starting at or before it.
int origamiFaceOf(int index) {
    int low = 0, high = max(int(pc.motion.x + 0.5), 1) - 1;
    while (low < high) {
        int middle = (low + high + 1) / 2;
        if (origamiFace(middle, 3).x <= float(index) + 0.5) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return low;
}

// The point at radius `rho` (0 centre .. 1 rim) of fan triangle `fan`, `u`
// along its stretch of rim; `normal` is the face's, bent with the swelling.
vec3 origamiPoint(int face, int fan, float rho, float u, out vec3 normal) {
    int sides = origamiSides(face);
    vec3 centre = origamiFace(face, 0).xyz;
    vec3 a = origamiFace(face, 4 + fan).xyz, b = origamiFace(face, 4 + (fan + 1) % sides).xyz;
    vec3 p = centre + rho * (mix(a, b, u) - centre);
    normal = origamiFace(face, 1).xyz;
    // Closed, the faces swell onto the sphere through their corners.
    float swell = pc.shape.z;
    if (swell > 0.0) {
        vec3 radial = normalize(p + vec3(1e-6, 0.0, 0.0));
        p = mix(p, radial, swell);
        normal = normalize(mix(normal, radial * sign(dot(radial, normal) + 1e-6), swell));
    }
    // Lying open the figure swirls about the vertical, more towards its rim.
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
