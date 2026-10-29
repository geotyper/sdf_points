// Metaballs (mode 25): balls whose fields add up, the surface where the sum
// reaches a threshold (Metaballs.hpp moves the balls on the CPU). Every ball
// carries a Fibonacci sphere of points; a point keeps its direction from the
// ball's centre and slides out along it to the surface. It shows only where
// its own ball's field is the strongest, so merging balls share the skin and
// a ball swallowed by another hides its points. There is no depth skin: a
// point is hidden when the field reaches the threshold anywhere on its way
// to the camera.
//
// Push constants: shape = size, threshold, scene reach, balls; points = all
// points, 0, pixel radius, 0; look = tilt, glow, brightness, seam
// glow; style.rgb = bottom colour, w = 25; wave.rgb = top colour, w = spin
// rate; motion = colour mode (0 blended, 1 own ball, 2 bottom to top), field
// reach in radii, 0, 0; loop = cycles, 0, 0, zoom.
// The polytope storage buffer holds three vec4 per ball: centre and radius,
// colour and first point, (points, 0, 0, 0).

const int METABALL = 3;

vec4 metaball(int ball, int slot) {
    return polytopeData[METABALL * ball + slot];
}

int metaballCount() {
    return max(int(pc.shape.w + 0.5), 1);
}

float metaballSpan() {
    return pc.shape.x * pc.shape.z * 1.08 / max(pc.loop.w, 0.05);
}

float metaballKernel(int ball, vec3 p) {
    vec4 b = metaball(ball, 0);
    float far = pc.motion.y * b.w;
    vec3 d = p - b.xyz;
    float q = 1.0 - dot(d, d) / (far * far);
    return q > 0.0 ? q * q * q : 0.0;
}

float metaballField(vec3 p) {
    float sum = 0.0;
    for (int i = 0; i < metaballCount(); ++i) {
        sum += metaballKernel(i, p);
    }
    return sum;
}

// The ball holding point `index`: the last one starting at or before it.
int metaballOf(int index) {
    int low = 0, high = metaballCount() - 1;
    while (low < high) {
        int middle = (low + high + 1) / 2;
        if (metaball(middle, 1).w <= float(index) + 0.5) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return low;
}

// From the ball's centre along `direction`, the first distance where the
// field falls below the threshold (-1 if the march meets none). The ball's
// own field holds the threshold up to its radius, so the march starts there.
float metaballRay(int ball, vec3 direction) {
    vec4 b = metaball(ball, 0);
    float threshold = pc.shape.y;
    float low = b.w, step = 0.15 * b.w, high = -1.0;
    for (int k = 0; k < 64; ++k) {
        if (metaballField(b.xyz + (low + step) * direction) < threshold) {
            high = low + step;
            break;
        }
        low += step;
    }
    if (high < 0.0) return -1.0;
    for (int k = 0; k < 10; ++k) {
        float middle = 0.5 * (low + high);
        if (metaballField(b.xyz + middle * direction) < threshold) {
            high = middle;
        } else {
            low = middle;
        }
    }
    return 0.5 * (low + high);
}

// At `p`: the outward normal, the blend of the balls' colours by their
// fields, and how much `ball` outweighs the strongest other ball (large
// alone, 1 on the seam between two).
void metaballShade(int ball, vec3 p, out vec3 normal, out vec3 blend, out float lead) {
    vec3 gradient = vec3(0.0);
    blend = vec3(0.0);
    float total = 0.0, own = 0.0, other = 0.0;
    for (int i = 0; i < metaballCount(); ++i) {
        vec4 b = metaball(i, 0);
        float far = pc.motion.y * b.w;
        vec3 d = p - b.xyz;
        float q = 1.0 - dot(d, d) / (far * far);
        if (q <= 0.0) continue;
        float k = q * q * q;
        gradient += 6.0 * q * q * d / (far * far);
        blend += k * metaball(i, 1).rgb;
        total += k;
        if (i == ball) {
            own = k;
        } else {
            other = max(other, k);
        }
    }
    normal = normalize(gradient + vec3(1e-7, 0.0, 0.0));
    blend /= max(total, 1e-6);
    lead = own / max(other, 1e-6 * own + 1e-12);
}

// A ball's own turn, so the point lattices of neighbours never line up.
vec3 metaballTurn(vec3 direction, int ball) {
    float identity = float(ball) * 3.17 + 0.41;
    vec3 axis = normalize(vec3(hashScalar(identity + 1.3), hashScalar(identity + 7.1),
                               hashScalar(identity + 13.7)) * 2.0 - 1.0 + vec3(0.001, 0.002, 0.003));
    return rotateAroundAxis(direction, axis, TAU * hashScalar(identity + 23.9));
}

// The whole figure spins about the vertical, then scales.
vec3 metaballSpin(vec3 p) {
    float spin = timePhase(pc.wave.w);
    p.xz = mat2(cos(spin), sin(spin), -sin(spin), cos(spin)) * p.xz;
    return p;
}

// The way to the camera in the balls' own (unspun) frame.
vec3 metaballToCamera() {
    vec3 along = fromView(vec3(0.0, 0.0, 1.0));
    float spin = timePhase(pc.wave.w);
    along.xz = mat2(cos(spin), -sin(spin), sin(spin), cos(spin)) * along.xz;
    return along;
}

// Whether the surface covers `p` (on it, facing the camera): the field met
// again on the straight way to the camera. `p` and `along` are in the
// unspun, unscaled frame; `along` points to the camera.
bool metaballHidden(vec3 p, vec3 along, float step) {
    float threshold = pc.shape.y;
    float travel = 2.0 * pc.shape.z;
    float t = 1.5 * step;
    for (int k = 0; k < 96 && t < travel; ++k) {
        if (metaballField(p + t * along) >= threshold) return true;
        t += step;
    }
    return false;
}
