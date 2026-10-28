// Surfaces found by marching rays through a field: the 3D cross-sections of
// solid 4D bodies (mode 20) and a 3D slice of a quaternion Julia set (mode
// 21). sections.comp marches every ray once and stores what it found; the
// point shader reads that. The skin pass draws nothing: instead every point
// marched a ray to the camera to see whether it is hidden.
//
// The points come from rays parallel to the axes of the figure's own frame on
// a jittered grid: every crossing of a ray with the surface is a point, kept
// only by the family whose axis lies closest to the surface normal, so the
// whole surface is covered about evenly, inner walls of rings included, and
// the points turn with the figure.
//
// Push constants, mode 20: shape = size, body, slice sweep, slice rate;
// points = grid, crossings per ray, pixel radius, colour mode; look = tilt,
// glow, brightness, XW turn rate; style.rgb = near colour; wave = far colour +
// spin rate; motion = YW turn rate, ZW turn rate, slice offset, thickness;
// loop = cycles, ray families, 0, zoom.
// Mode 21: shape = size, power, c orbit radius, c orbit rate; points as mode
// 20; look as mode 20; style, wave as mode 20; motion = base c; loop =
// cycles, ray families, slice w, zoom.
// The palette is in the polytope storage buffer's first five vec4.

// Half-size of the box the rays cross, in field units.
float sectionBound() {
    return juliaMode() ? 1.45 : 1.05;
}

// Field units to scene units.
float sectionScale() {
    return pc.shape.x / sectionBound() * 1.2;
}

vec4 sectionTurn(vec4 p) {
    float a = timePhase(pc.look.w);
    p = vec4(cos(a) * p.x - sin(a) * p.w, p.y, p.z, sin(a) * p.x + cos(a) * p.w);
    if (sliceMode()) {
        float b = timePhase(pc.motion.x), c = timePhase(pc.motion.y);
        p = vec4(p.x, cos(b) * p.y - sin(b) * p.w, p.z, sin(b) * p.y + cos(b) * p.w);
        p = vec4(p.x, p.y, cos(c) * p.z - sin(c) * p.w, sin(c) * p.z + cos(c) * p.w);
    }
    return p;
}

// ---- Solid 4D bodies, circumradius about 1. `tag` picks the cell colour.

float maxOf(vec4 v) {
    return max(max(v.x, v.y), max(v.z, v.w));
}

float signBits(vec4 p) {
    return float((p.x > 0.0 ? 1 : 0) + (p.y > 0.0 ? 2 : 0) + (p.z > 0.0 ? 4 : 0) + (p.w > 0.0 ? 8 : 0));
}

float axisFacet(vec4 q, vec4 p) {
    int axis = 0;
    for (int i = 1; i < 4; ++i) {
        if (q[i] > q[axis]) axis = i;
    }
    return float(2 * axis + (p[axis] > 0.0 ? 1 : 0));
}

float bodyField(vec4 p, out float tag) {
    int body = int(pc.shape.y + 0.5);
    float thickness = pc.motion.w;
    vec4 a = abs(p);
    if (body == 0) {
        // 5-cell: four unit axes and (s, s, s, s), centred and scaled to
        // circumradius 1; each cell faces away from one vertex at a quarter.
        const float s = -0.30901699, centre = 0.13819660, radius = 0.89442719;
        float best = -1e9;
        for (int i = 0; i < 5; ++i) {
            vec4 v = i < 4 ? vec4(equal(ivec4(i), ivec4(0, 1, 2, 3))) : vec4(s);
            float d = dot(p, -(v - centre) / radius) - 0.25;
            if (d > best) {
                best = d;
                tag = float(i);
            }
        }
        return best;
    }
    if (body == 1) {
        // Tesseract, half-size 1/2.
        vec4 q = a - 0.5;
        tag = axisFacet(q, p);
        return length(max(q, 0.0)) + min(maxOf(q), 0.0);
    }
    if (body == 2) {
        // 16-cell: the cells are the sixteen sign patterns.
        tag = signBits(p);
        return (a.x + a.y + a.z + a.w - 1.0) * 0.5;
    }
    if (body == 3) {
        // 24-cell: eight cells across the axes, sixteen across the diagonals.
        float across = maxOf(a) - 0.70710678;
        float diagonal = (a.x + a.y + a.z + a.w - 1.41421356) * 0.5;
        tag = across > diagonal ? axisFacet(a, p) : 8.0 + signBits(p);
        return max(across, diagonal);
    }
    if (body == 4) {
        // Duocylinder: two solid tori meet along the Clifford torus.
        float first = length(p.xy) - 0.70710678, second = length(p.zw) - 0.70710678;
        tag = first > second ? floor(3.0 * (atan(p.w, p.z) / TAU + 0.5)) * 2.0
                             : floor(3.0 * (atan(p.y, p.x) / TAU + 0.5)) * 2.0 + 1.0;
        return max(first, second);
    }
    float u = atan(p.y, p.x) / TAU + 0.5, v = atan(p.w, p.z) / TAU + 0.5;
    if (body == 5) {
        // Tiger: a tube around the Clifford torus, checkered by its two angles.
        tag = mod(floor(6.0 * u) + floor(6.0 * v), 2.0) + 2.0 * mod(floor(3.0 * u), 2.0);
        return length(vec2(length(p.xy) - 0.55, length(p.zw) - 0.55)) - thickness;
    }
    if (body == 6) {
        // Spheritorus: a 2-sphere swept round a circle through w.
        float w = atan(p.w, length(p.xyz) - 0.62);
        tag = floor(5.0 * (w / TAU + 0.5));
        return length(vec2(length(p.xyz) - 0.62, p.w)) - thickness;
    }
    // Ditorus: a torus swept round a circle.
    tag = floor(5.0 * u);
    return length(vec2(length(vec2(length(p.xy) - 0.58, p.z)) - 0.24, p.w)) - 0.6 * thickness;
}

float sliceW() {
    return pc.motion.z + pc.shape.z * sin(timePhase(pc.shape.w));
}

// ---- Quaternion Julia set z -> z^power + c, distance estimate.

vec4 juliaC() {
    float a = timePhase(pc.shape.w);
    return pc.motion + pc.shape.z * vec4(cos(a), sin(a), 0.5 * sin(2.0 * a), 0.5 * cos(2.0 * a));
}

vec4 quaternionTimes(vec4 a, vec4 b) {
    return vec4(a.x * b.x - dot(a.yzw, b.yzw), a.x * b.yzw + b.x * a.yzw + cross(a.yzw, b.yzw));
}

float juliaField(vec4 z, out float tag) {
    vec4 c = juliaC();
    float power = pc.shape.y > 2.5 ? 3.0 : 2.0;
    float derivative = 1.0;
    float trap = 1e9;
    float r = length(z);
    for (int i = 0; i < 12; ++i) {
        derivative *= power * pow(r, power - 1.0);
        z = power > 2.5 ? quaternionTimes(quaternionTimes(z, z), z) : quaternionTimes(z, z);
        z += c;
        trap = min(trap, dot(z, z));
        r = length(z);
        if (r > 4.0) break;
    }
    tag = sqrt(trap);
    return 0.5 * r * log(max(r, 1e-6)) / max(derivative, 1e-6);
}

// ---- The field at a point of the figure's frame.

float sectionField(vec3 x, out float tag) {
    if (juliaMode()) {
        return juliaField(sectionTurn(vec4(x, pc.loop.z)), tag);
    }
    return bodyField(sectionTurn(vec4(x, sliceW())), tag);
}

float sectionField(vec3 x) {
    float tag;
    return sectionField(x, tag);
}

vec3 sectionNormal(vec3 x) {
    const vec2 k = vec2(1.0, -1.0);
    float h = juliaMode() ? 0.0025 : 0.0015;
    return normalize(k.xyy * sectionField(x + h * k.xyy) + k.yyx * sectionField(x + h * k.yyx)
                   + k.yxy * sectionField(x + h * k.yxy) + k.xxx * sectionField(x + h * k.xxx));
}

const float SECTION_EPSILON = 0.0018;

// The `crossing`-th time a ray from `origin` along `direction` meets the
// surface within the box, by sphere tracing on |field|.
bool sectionCrossing(vec3 origin, vec3 direction, int crossing, out vec3 hit) {
    float bound = sectionBound();
    float t = 0.0, end = 2.0 * bound;
    int found = 0;
    bool near = false;
    int steps = juliaMode() ? 200 : 160;
    for (int i = 0; i < steps && t < end; ++i) {
        vec3 x = origin + t * direction;
        float f = sectionField(x);
        if (abs(f) < SECTION_EPSILON) {
            if (!near) {
                if (found == crossing) {
                    hit = x;
                    return true;
                }
                ++found;
                near = true;
            }
        } else if (abs(f) > 2.0 * SECTION_EPSILON) {
            near = false;
        }
        t += max(abs(f) * (juliaMode() ? 0.8 : 0.95), 1.2 * SECTION_EPSILON);
    }
    return false;
}

// Whether anything lies between a surface point and the camera.
bool sectionHidden(vec3 x, vec3 normal, vec3 toCamera) {
    vec3 origin = x + normal * 4.0 * SECTION_EPSILON;
    float t = 0.0, bound = sectionBound();
    for (int i = 0; i < 120; ++i) {
        vec3 p = origin + t * toCamera;
        if (any(greaterThan(abs(p), vec3(bound)))) return false;
        float f = sectionField(p);
        if (f < 0.5 * SECTION_EPSILON) return true;
        t += max(f * (juliaMode() ? 0.8 : 0.95), SECTION_EPSILON);
    }
    return false;
}

vec3 sectionSpin(vec3 p, float sense) {
    float spin = sense * timePhase(pc.wave.w);
    p.xz = mat2(cos(spin), sin(spin), -sin(spin), cos(spin)) * p.xz;
    return p;
}
