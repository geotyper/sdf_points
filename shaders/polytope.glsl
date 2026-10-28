// 4D polytopes and compounds. The edges come from the CPU, three vec4 each:
// start, end, (colour, part). Push constants: shape = size, tube radius, 4D
// camera distance, XW turn rate; points = samples along an edge, around its
// tube, pixel radius, edge count; look.w = YZ turn rate; style.rgb = near
// colour; wave = far colour + spin rate; motion = ZW turn rate, colour mode,
// slice glow, slice width; loop = cycles, light strength, flags (1: edges
// bent onto the 3-sphere, 2: parts turn against each other), light flow rate.
layout(set = 0, binding = 2, std430) readonly buffer PolytopeEdges {
    vec4 polytopeEdges[];
};

int polytopeFlags() {
    return int(pc.loop.z + 0.5);
}

vec4 polytopeTurn(vec4 q, float part) {
    float sense = (polytopeFlags() & 2) != 0 && mod(part, 2.0) > 0.5 ? -1.0 : 1.0;
    float a = timePhase(sense * pc.shape.w);
    float b = timePhase(sense * pc.look.w);
    float c = timePhase(sense * pc.motion.x);
    q = vec4(cos(a) * q.x - sin(a) * q.w, q.y, q.z, sin(a) * q.x + cos(a) * q.w);
    q = vec4(q.x, cos(b) * q.y - sin(b) * q.z, sin(b) * q.y + cos(b) * q.z, q.w);
    q = vec4(q.x, q.y, cos(c) * q.z - sin(c) * q.w, sin(c) * q.z + cos(c) * q.w);
    return q;
}

// Perspective magnification from the 4D camera. At distance 1 with bent edges
// this is the stereographic projection of the 3-sphere.
float polytopeScale(float w) {
    return pc.shape.z / max(pc.shape.z - w, 1e-3);
}

// Beyond this magnification a piece passes too close to the 4D camera: dropped.
const float POLYTOPE_CUTOFF = 8.0;

float polytopeSpan() {
    float reach = 0.0;
    for (int i = 0; i <= 32; ++i) {
        float angle = 0.5 * PI * float(i) / 32.0;
        reach = max(reach, cos(angle) * polytopeScale(sin(angle)));
    }
    return pc.shape.x * min(reach, 3.0) * 1.08;
}

vec4 polytopePoint4(int edge, float along) {
    float part = polytopeEdges[3 * edge + 2].w;
    vec4 start = polytopeTurn(polytopeEdges[3 * edge], part);
    vec4 end = polytopeTurn(polytopeEdges[3 * edge + 1], part);
    vec4 q = mix(start, end, along);
    if ((polytopeFlags() & 1) != 0) {
        // Along the great circle on the part's own 3-sphere.
        q = normalize(q) * length(start);
    }
    return q;
}

vec3 polytopeProject(vec4 q) {
    vec3 p = pc.shape.x * polytopeScale(q.w) * q.xyz;
    float spin = timePhase(pc.wave.w);
    p.xz = mat2(cos(spin), sin(spin), -sin(spin), cos(spin)) * p.xz;
    return p;
}

// Tube frame along an edge. A bent edge is a circle arc, whose curvature
// points at its centre; one decided at the edge's middle for the whole edge,
// so the frame never switches along it.
void polytopeFrame(int edge, float along, out vec3 center, out vec3 normal, out vec3 binormal,
                   out float w) {
    const float step = 0.01;
    vec4 q = polytopePoint4(edge, along);
    w = q.w;
    center = polytopeProject(q);
    vec3 ahead = polytopeProject(polytopePoint4(edge, along + step));
    vec3 behind = polytopeProject(polytopePoint4(edge, along - step));
    vec3 tangent = normalize(ahead - behind);
    vec3 first = polytopeProject(polytopePoint4(edge, 0.0));
    vec3 last = polytopeProject(polytopePoint4(edge, 1.0));
    vec3 chord = normalize(last - first + vec3(1e-6, 0.0, 0.0));
    vec3 fixedSide = cross(chord, abs(chord.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0));
    vec3 middle = polytopeProject(polytopePoint4(edge, 0.5));
    vec3 sag = middle - 0.5 * (first + last);
    bool bent = (polytopeFlags() & 1) != 0 && length(sag) > 1e-3 * length(last - first);
    vec3 reference;
    if (bent) {
        vec3 bend = ahead + behind - 2.0 * center;
        reference = bend - tangent * dot(bend, tangent);
        if (dot(reference, reference) < 1e-14) reference = sag;
    } else {
        reference = cross(fixedSide, tangent);
    }
    normal = normalize(reference - tangent * dot(reference, tangent));
    binormal = cross(tangent, normal);
}

bool polytopeTooClose(float w) {
    return polytopeScale(w) > POLYTOPE_CUTOFF || pc.shape.z - w < 1e-2;
}
