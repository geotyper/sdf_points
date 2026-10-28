// 4D polytopes and compounds. The CPU writes three vec4 of shaping first:
// (edge bend, coil size, coil turns, coil flow rate), (4D noise amount, noise
// scale, noise rate, 0), (coil wave: 0 steady, 1 along edges, 2 across the
// figure; wave length, wave rate, 0); then the edges, three vec4 each: start, end,
// (colour, part). Push constants: shape = size, tube radius, 4D
// camera distance, XW turn rate; points = samples along an edge, around its
// tube, pixel radius, edge count; look.w = YZ turn rate; style.rgb = near
// colour; wave = far colour + spin rate; motion = ZW turn rate, colour mode,
// slice glow, slice width; loop = cycles, light strength, flags (2: parts
// turn against each other), light flow rate.
layout(set = 0, binding = 2, std430) readonly buffer PolytopeEdges {
    vec4 polytopeData[];
};

vec4 polytopeEdge(int edge, int slot) {
    return polytopeData[3 + 3 * edge + slot];
}

// Smooth 4D displacement: crossed sine waves in all four coordinates, drifting
// with the flow phase so a loop closes. Applied after the turn, so the figure
// moves through a field that stays put.
vec4 polytopeNoise(vec4 q) {
    vec4 p = q * polytopeData[1].y;
    float first = timePhase(polytopeData[1].z), second = timePhase(2.0 * polytopeData[1].z);
    vec4 n = vec4(sin(1.3 * p.y + 0.7 * p.z + first) + 0.5 * sin(1.7 * p.w - 0.5 * p.x + second),
                  sin(1.1 * p.z - 0.9 * p.w + second) + 0.5 * sin(1.9 * p.x + 0.3 * p.y - first),
                  sin(1.5 * p.w + 0.8 * p.x - first) + 0.5 * sin(1.4 * p.y - 0.6 * p.z + second),
                  sin(1.2 * p.x - 1.0 * p.y + second) + 0.5 * sin(1.6 * p.z + 0.4 * p.w + first));
    return n / 1.5;
}

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
    // Bulging edges, coils and noise all reach a little further out.
    vec4 shaping = polytopeData[0];
    float growth = 1.0 + 0.3 * max(shaping.x - 1.0, 0.0) + 0.5 * shaping.y + polytopeData[1].x;
    return pc.shape.x * min(reach * growth, 3.0) * 1.08;
}

// Coil size as a wave runs through it: crests travel along every edge from
// its start, or as fronts across the figure along a fixed 4D direction of its
// own frame, so the wave turns with it. 1 when the coil is steady.
float polytopeCoilWave(vec4 start, vec4 end, float along) {
    vec4 wave = polytopeData[2];
    if (wave.x < 0.5) return 1.0;
    float position = wave.x < 1.5
                         ? along
                         : dot(mix(start, end, along), normalize(vec4(0.8, 0.5, 0.3, 0.6)));
    float crest = 0.5 + 0.5 * cos(TAU * position / max(wave.y, 0.05) - timePhase(wave.z));
    return crest * crest;
}

vec4 polytopePoint4(int edge, float along) {
    vec4 start = polytopeEdge(edge, 0), end = polytopeEdge(edge, 1);
    float part = polytopeEdge(edge, 2).w;
    vec4 shaping = polytopeData[0];
    vec4 q = mix(start, end, along);
    // Bend: 0 straight, 1 along the great circle on the part's own 3-sphere,
    // beyond that bulging out, below 0 caving in.
    q *= mix(1.0, length(start) / max(length(q), 1e-4), shaping.x);
    if (shaping.y > 0.0) {
        // Coil: the edge winds around its chord in the two 4D directions
        // across it, pinned at both vertices so the edges still meet.
        vec4 chord = end - start;
        float span = length(chord);
        vec4 direction = chord / span;
        vec4 middle = 0.5 * (start + end);
        vec4 across = middle - direction * dot(middle, direction);
        across = dot(across, across) > 1e-8 ? normalize(across) : vec4(0.0, 0.0, 0.0, 1.0);
        vec4 third = vec4(1.0, 0.0, 0.0, 0.0);
        float best = 2.0;
        for (int axis = 0; axis < 4; ++axis) {
            vec4 basis = vec4(0.0);
            basis[axis] = 1.0;
            float overlap = abs(dot(basis, direction)) + abs(dot(basis, across));
            if (overlap < best) {
                best = overlap;
                third = basis;
            }
        }
        third = normalize(third - direction * dot(third, direction) - across * dot(third, across));
        float angle = TAU * shaping.z * along - timePhase(shaping.w);
        q += shaping.y * span * sin(PI * along) * polytopeCoilWave(start, end, along) *
             (across * cos(angle) + third * sin(angle));
    }
    q = polytopeTurn(q, part);
    if (polytopeData[1].x > 0.0) {
        q += polytopeData[1].x * polytopeNoise(q);
    }
    return q;
}

vec3 polytopeProject(vec4 q) {
    vec3 p = pc.shape.x * polytopeScale(q.w) * q.xyz;
    float spin = timePhase(pc.wave.w);
    p.xz = mat2(cos(spin), sin(spin), -sin(spin), cos(spin)) * p.xz;
    return p;
}

// Tube frame along an edge. One reference direction serves the whole edge so
// the frame never switches along it: across the plane of its ends and middle
// when it bends (a circle arc always runs square to it), else a fixed side.
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
    vec3 plane = cross(chord, sag);
    vec3 reference = dot(sag, sag) > 1e-6 * dot(last - first, last - first) && dot(plane, plane) > 1e-12
                         ? normalize(plane)
                         : normalize(fixedSide);
    vec3 side = cross(reference, tangent);
    // A coiled or noisy edge can turn along the reference for a moment.
    if (dot(side, side) < 1e-6) side = cross(normalize(fixedSide + 0.5 * chord), tangent);
    normal = normalize(side);
    binormal = cross(tangent, normal);
}

bool polytopeTooClose(float w) {
    return polytopeScale(w) > POLYTOPE_CUTOFF || pc.shape.z - w < 1e-2;
}
