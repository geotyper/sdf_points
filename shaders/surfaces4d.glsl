// Surfaces that live in 4D (mode 22): a slice of the Calabi-Yau quintic, the
// Klein bottle, the Veronese surface, the Clifford torus and the sheets of the
// Milnor fibration of a torus knot. Every patch has
// its own coordinates (s, t) in [0, 1]^2; the points sit on a grid of them and
// the depth skin is a mesh of them, so the points ride the surface while it
// turns in 4D and is projected to 3D.
//
// Push constants: shape = size, surface, Fermat power, Calabi-Yau reach;
// points = columns, rows, pixel radius, colour mode; look = tilt, glow,
// brightness, XW turn rate; style.rgb = near colour; wave = far colour + spin
// rate; motion = YW turn rate, ZW turn rate, fixed ZW view angle, pulse rate;
// loop = cycles, 4D camera distance (0 = flat), pulse strength, zoom.
// The polytope storage buffer holds the palette in its first five vec4 and,
// in the sixth: the flow rate (Milnor: sheet turn rate), the pulse's waves
// along s, the knot's q and how many Milnor sheets; in the seventh the
// Moebius flow amount and rate.

// Magnifications past this are too near the 4D camera and left out.
const float FOUR_SURFACE_NEAR = 6.0;

int fourSurface() {
    return int(pc.shape.y + 0.5);
}

int fermatPower() {
    return max(int(pc.shape.z + 0.5), 2);
}

// Matches fourSurfaceSkinGrid in FourSurfaces.hpp.
ivec2 fourSurfaceSkinGrid() {
    return fourSurface() == 0 || fourSurface() == 4 ? ivec2(96, 96) : ivec2(256, 192);
}

vec3 fourSurfacePalette(float index) {
    return polytopeData[int(mod(index, 5.0))].rgb;
}

vec2 complexTimes(vec2 a, vec2 b) {
    return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

vec2 complexPower(vec2 z, float p) {
    float r = length(z);
    if (r < 1e-12) return vec2(0.0);
    float a = p * atan(z.y, z.x);
    return pow(r, p) * vec2(cos(a), sin(a));
}

// The surface before it turns. `tag` picks the palette colour.
vec4 fourSurfaceLocal(int piece, vec2 st, out float tag) {
    int surface = fourSurface();
    if (surface == 0) {
        // Hanson's patches of z1^n + z2^n = 1: z1 = cos(z)^(2/n), z2 = sin(z)^(2/n)
        // for z in a strip, times the n-th roots of unity k1, k2. Both bases have
        // a positive real part there, so the powers never cross their cut.
        int n = fermatPower();
        int k1 = piece / n, k2 = piece % n;
        float x = 0.5 * PI * st.x, y = pc.shape.w * (2.0 * st.y - 1.0);
        vec2 c = vec2(cos(x) * cosh(y), -sin(x) * sinh(y));
        vec2 s = vec2(sin(x) * cosh(y), cos(x) * sinh(y));
        float p = 2.0 / float(n);
        float a1 = TAU * float(k1) / float(n), a2 = TAU * float(k2) / float(n);
        vec2 z1 = complexTimes(complexPower(c, p), vec2(cos(a1), sin(a1)));
        vec2 z2 = complexTimes(complexPower(s, p), vec2(cos(a2), sin(a2)));
        tag = float(k1 + 2 * k2);
        return vec4(z1.x, z2.x, z1.y, z2.y);
    }
    if (surface == 4) {
        // Milnor fibration of z1^p + z2^q: the affine curve z1^p + z2^q = 1,
        // in Hanson's patches, pushed onto the 3-sphere by the weighted scaling
        // (t^(1/p) z1, t^(1/q) z2), which keeps f real and positive. Turning by
        // (e^(i a / p), e^(i a / q)) turns f by e^(i a): one sheet sweeps to the
        // next, and a whole turn carries the sheet onto itself. The far ends of
        // the strip run into the knot f = 0 they span.
        int p = fermatPower(), q = max(int(polytopeData[5].z + 0.5), 2);
        int sheets = max(int(polytopeData[5].w + 0.5), 1);
        int sheet = piece / (p * q), rest = piece % (p * q);
        int k1 = rest / q, k2 = rest % q;
        float x = 0.5 * PI * st.x, y = pc.shape.w * (2.0 * st.y - 1.0);
        vec2 c = vec2(cos(x) * cosh(y), -sin(x) * sinh(y));
        vec2 s = vec2(sin(x) * cosh(y), cos(x) * sinh(y));
        float a1 = TAU * float(k1) / float(p), a2 = TAU * float(k2) / float(q);
        vec2 z1 = complexTimes(complexPower(c, 2.0 / float(p)), vec2(cos(a1), sin(a1)));
        vec2 z2 = complexTimes(complexPower(s, 2.0 / float(q)), vec2(cos(a2), sin(a2)));
        // log t from |z1|^2 t^(2/p) + |z2|^2 t^(2/q) = 1: convex and rising, so
        // Newton gets there from any start.
        float m1 = dot(z1, z1), m2 = dot(z2, z2), g = 0.0;
        for (int i = 0; i < 24; ++i) {
            float e1 = m1 * exp(2.0 * g / float(p)), e2 = m2 * exp(2.0 * g / float(q));
            g -= (e1 + e2 - 1.0) / max(2.0 * e1 / float(p) + 2.0 * e2 / float(q), 1e-6);
        }
        float a = timePhase(polytopeData[5].x) + TAU * float(sheet) / float(sheets);
        z1 = complexTimes(z1, vec2(cos(a / float(p)), sin(a / float(p)))) * exp(g / float(p));
        z2 = complexTimes(z2, vec2(cos(a / float(q)), sin(a / float(q)))) * exp(g / float(q));
        tag = float(sheet);
        return vec4(z1, z2);
    }
    float u = TAU * st.x, v = TAU * st.y;
    if (surface == 1) {
        // Klein bottle: a tube round a circle whose cross-section turns half
        // a time through w, so it meets itself only in 3D shadows.
        const float major = 0.70, minor = 0.32;
        tag = floor(5.0 * fract(st.x));
        float ring = major + minor * cos(v);
        return vec4(ring * cos(u), ring * sin(u), minor * sin(v) * cos(0.5 * u),
                    minor * sin(v) * sin(0.5 * u));
    }
    if (surface == 2) {
        // Veronese: (a, b, c) on the upper half sphere, equal area in t, to
        // (ab, bc, ca, (a^2 - b^2) / 2); opposite points agree, so the
        // rim closes up into the projective plane.
        float c = 1.0 - st.y;
        float radial = sqrt(max(1.0 - c * c, 0.0));
        float a = radial * cos(u), b = radial * sin(u);
        tag = floor(5.0 * fract(st.x));
        return 1.8 * vec4(a * b, b * c, c * a, 0.5 * (a * a - b * b));
    }
    // Clifford torus on the unit 3-sphere, checkered.
    tag = mod(floor(8.0 * st.x) + floor(8.0 * st.y), 2.0) + 2.0 * mod(floor(2.0 * st.x), 2.0);
    return 0.70710678 * vec4(cos(u), sin(u), cos(v), sin(v));
}

vec4 fourSurfaceTurn(vec4 p) {
    float a = pc.motion.z + timePhase(pc.motion.y);
    p.zw = mat2(cos(a), -sin(a), sin(a), cos(a)) * p.zw;
    float b = timePhase(pc.look.w);
    p.xw = mat2(cos(b), -sin(b), sin(b), cos(b)) * p.xw;
    float c = timePhase(pc.motion.x);
    p.yw = mat2(cos(c), -sin(c), sin(c), cos(c)) * p.yw;
    return p;
}

// The point in the scene; `w` is its turned 4D depth, `magnification` how
// much the 4D perspective enlarges it.
vec3 fourSurfacePoint(int piece, vec2 st, out float tag, out float w, out float magnification) {
    vec4 p = mobiusFlow(fourSurfaceTurn(fourSurfaceLocal(piece, st, tag)), polytopeData[6].x,
                        polytopeData[6].y);
    w = p.w;
    magnification = 1.0;
    float distance = pc.loop.y;
    if (distance > 0.0) {
        float gap = distance - p.w;
        magnification = gap > distance / FOUR_SURFACE_NEAR ? distance / gap : 2.0 * FOUR_SURFACE_NEAR;
    }
    vec3 q = p.xyz * min(magnification, FOUR_SURFACE_NEAR);
    float spin = timePhase(pc.wave.w);
    q.xz = mat2(cos(spin), sin(spin), -sin(spin), cos(spin)) * q.xz;
    return pc.shape.x * q;
}

vec3 fourSurfacePoint(int piece, vec2 st) {
    float tag, w, magnification;
    return fourSurfacePoint(piece, st, tag, w, magnification);
}

bool fourSurfaceTooClose(float magnification) {
    return magnification > FOUR_SURFACE_NEAR;
}

// The patch's normal in the scene, either side: the surfaces need not be
// orientable, so lighting takes both sides alike.
vec3 fourSurfaceNormal(int piece, vec2 st) {
    const float h = 0.0007;
    vec3 ds = fourSurfacePoint(piece, st + vec2(h, 0.0)) - fourSurfacePoint(piece, st - vec2(h, 0.0));
    vec3 dt = fourSurfacePoint(piece, st + vec2(0.0, h)) - fourSurfacePoint(piece, st - vec2(0.0, h));
    vec3 n = cross(ds, dt);
    float size = length(n);
    return size > 1e-12 ? n / size : vec3(0.0, 0.0, 1.0);
}

// Closed surfaces carry their points round: the Klein bottle round its tube
// (along t, whole turns there), the others round s.
vec2 fourSurfaceFlow(vec2 st) {
    int surface = fourSurface();
    if (surface == 0 || surface == 4) return st;
    float shift = timePhase(polytopeData[5].x) / TAU;
    if (surface == 1) return vec2(st.x, fract(st.y + shift));
    return vec2(fract(st.x + shift), st.y);
}
