// Metaballs (mode 25): balls (or capsules and rings) whose fields add up, the surface where the sum
// reaches a threshold (Metaballs.hpp moves the balls on the CPU). The points
// are particles living on the surface (after Witkin and Heckbert), kept from
// frame to frame by metaballs.comp: each frame they are carried along with a
// blend of the balls' motions, weighted by the balls' fields, so on a neck
// between two balls everything flows by the same law; pulled back onto the
// surface; pushed apart by their neighbours; and where they crowd they fade
// out, while spare particles are born in the gaps, so the density stays even
// however the surface stretches, merges or splits. There is no depth skin: a
// point is hidden when the field reaches the threshold anywhere on its way
// to the camera.
//
// Push constants: shape = size, threshold, scene reach, balls; points =
// particles, 0, pixel radius, 0; look = tilt, glow, brightness, seam glow;
// style.rgb = bottom colour, w = 25; wave.rgb = top colour, w = spin rate;
// motion = colour mode (0 blended, 1 own ball, 2 bottom to top), field reach
// in radii, oversampling of new particles, seam sharpness; loop = cycles,
// 0, 0, zoom.
// The polytope storage buffer holds six vec4 per ball (centre and radius,
// colour and where its share of the area starts, extent and kind, centre and
// radius the frame before, extent the frame before and group, a cube's half
// size now and the frame before), then from
// METABALL_HEADER: (reset, frame number, particles, fade step), (neighbour
// reach, neighbours' weight at the rest density, hash table cells, spacing),
// (particles to start with, push strength, density tolerance, 0).
//
// A "ball" has a core: its centre alone, a segment centre +- extent (a
// capsule), or with kind 1 a ring of radius |extent| about the extent's axis
// (a torus), or with kind 2 a cube of half size `halfSize` turned by the unit
// quaternion whose x, y, z are the extent (w >= 0), its surface a rounded
// cube. Its field falls off with the distance from the nearest point of
// the core. Balls of one group (consecutive, as a worm's segments) take the
// strongest of their fields instead of the sum, so a chain is one smooth tube
// with no bulge at the joints; groups add up as before.

const int METABALL = 6;
const int METABALL_HEADER = METABALL * 24;
// Particle slots in the shared storage buffer (binding 3): position and
// life, (alive, 0, 0, 0), scratch position and life. The neighbour table
// follows, METABALL_CELL uints per cell: a count (with a birth flag above
// its low 16 bits) and the particles. A cell is two and a half spacings
// wide and holds about ten particles where the surface cuts it slantwise;
// 31 slots leave room for two cells sharing a bucket of the hash.
const int METABALL_PARTICLE = 3;
const int METABALL_PARTICLE_LIMIT = 600000;
const int METABALL_CELL = 32;

vec4 metaball(int ball, int slot) {
    return polytopeData[METABALL * ball + slot];
}

vec4 metaballHeader(int slot) {
    return polytopeData[METABALL_HEADER + slot];
}

int metaballCount() {
    return max(int(pc.shape.w + 0.5), 1);
}

int metaballGroup(int ball) {
    return int(metaball(ball, 4).w + 0.5);
}

float metaballSpan() {
    return pc.shape.x * pc.shape.z * 1.08 / max(pc.loop.w, 0.05);
}

// A unit vector square to `axis`.
vec3 metaballAcross(vec3 axis) {
    return normalize(cross(axis, abs(axis.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
}

// A vector turned by the unit quaternion with x, y, z `turn` and w >= 0
// (`back` turns the other way).
vec3 metaballTurn(vec3 turn, vec3 v, bool back) {
    float w = sqrt(max(0.0, 1.0 - dot(turn, turn)));
    vec3 axis = back ? -turn : turn;
    return v + 2.0 * cross(axis, cross(axis, v) + w * v);
}

// The point of the core (centre `ball.xyz`, extent and kind `extent`, a
// cube's half size `halfSize`) nearest `p`.
vec3 metaballCore(vec4 ball, vec4 extent, float halfSize, vec3 p) {
    vec3 v = p - ball.xyz;
    if (extent.w > 1.5) {
        vec3 local = metaballTurn(extent.xyz, v, true);
        return ball.xyz + metaballTurn(extent.xyz, clamp(local, -halfSize, halfSize), false);
    }
    if (extent.w > 0.5) {
        float major = length(extent.xyz);
        vec3 axis = extent.xyz / max(major, 1e-9);
        vec3 around = v - axis * dot(v, axis);
        float l = length(around);
        return ball.xyz + major * (l > 1e-9 ? around / l : metaballAcross(axis));
    }
    float e2 = dot(extent.xyz, extent.xyz);
    float s = e2 > 1e-12 ? clamp(dot(v, extent.xyz) / e2, -1.0, 1.0) : 0.0;
    return ball.xyz + s * extent.xyz;
}

// 1 - d^2 / R^2 for ball `ball` at `p` (the kernel is its cube where
// positive), d the offset from the core and R the field's reach.
float metaballFalloff(int ball, vec3 p, out vec3 offset, out float far) {
    vec4 b = metaball(ball, 0);
    offset = p - metaballCore(b, metaball(ball, 2), metaball(ball, 5).x, p);
    far = pc.motion.y * b.w;
    return 1.0 - dot(offset, offset) / (far * far);
}

float metaballField(vec3 p) {
    float sum = 0.0, best = 0.0;
    int group = -1;
    for (int i = 0; i < metaballCount(); ++i) {
        int g = metaballGroup(i);
        if (g != group) {
            sum += best;
            best = 0.0;
            group = g;
        }
        vec3 d;
        float far;
        float q = metaballFalloff(i, p, d, far);
        if (q > 0.0) best = max(best, q * q * q);
    }
    return sum + best;
}

// The field and its gradient.
float metaballField(vec3 p, out vec3 gradient) {
    float sum = 0.0, best = 0.0;
    vec3 bestGradient = vec3(0.0);
    gradient = vec3(0.0);
    int group = -1;
    for (int i = 0; i < metaballCount(); ++i) {
        int g = metaballGroup(i);
        if (g != group) {
            sum += best;
            gradient += bestGradient;
            best = 0.0;
            bestGradient = vec3(0.0);
            group = g;
        }
        vec3 d;
        float far;
        float q = metaballFalloff(i, p, d, far);
        if (q <= 0.0 || q * q * q <= best) continue;
        best = q * q * q;
        bestGradient = -6.0 * q * q * d / (far * far);
    }
    gradient += bestGradient;
    return sum + best;
}

// The ball whose share of the total area holds `u` (0..1).
int metaballOf(float u) {
    int low = 0, high = metaballCount() - 1;
    while (low < high) {
        int middle = (low + high + 1) / 2;
        if (metaball(middle, 1).w <= u) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return low;
}

// From `origin` on a ball's core along `direction`: the first distance where
// the field falls below the threshold (-1 if the march meets none). The
// ball's own field holds the threshold up to its radius, so the march starts
// there, in steps of `step`.
float metaballRay(vec3 origin, float radius, float step, vec3 direction) {
    float threshold = pc.shape.y;
    float low = radius, high = -1.0;
    for (int k = 0; k < 64; ++k) {
        if (metaballField(origin + (low + step) * direction) < threshold) {
            high = low + step;
            break;
        }
        low += step;
    }
    if (high < 0.0) return -1.0;
    for (int k = 0; k < 10; ++k) {
        float middle = 0.5 * (low + high);
        if (metaballField(origin + middle * direction) < threshold) {
            high = middle;
        } else {
            low = middle;
        }
    }
    return 0.5 * (low + high);
}

// At `p`: the outward normal, the blend of the groups' colours by their
// fields, the strongest ball, and how far its group leads: the largest of
// the shares that go as the fields to the power `sharpness` and add up to 1.
void metaballShade(vec3 p, out vec3 normal, out vec3 blend, out int strongest, out float leading) {
    metaballField(p, normal);
    normal = normalize(-normal + vec3(1e-7, 0.0, 0.0));
    blend = vec3(0.0);
    float total = 0.0, weights = 0.0, most = 0.0;
    float sharpness = max(pc.motion.w, 0.5);
    float best = 0.0;
    int bestBall = 0, group = -1;
    strongest = 0;
    for (int i = 0; i <= metaballCount(); ++i) {
        int g = i < metaballCount() ? metaballGroup(i) : -2;
        if (g != group) {
            if (best > 0.0) {
                blend += best * metaball(bestBall, 1).rgb;
                total += best;
                float weight = pow(best, sharpness);
                weights += weight;
                if (weight > most) {
                    most = weight;
                    strongest = bestBall;
                }
            }
            best = 0.0;
            group = g;
        }
        if (i == metaballCount()) break;
        vec3 d;
        float far;
        float q = metaballFalloff(i, p, d, far);
        if (q > 0.0 && q * q * q > best) {
            best = q * q * q;
            bestBall = i;
        }
    }
    blend /= max(total, 1e-6);
    leading = most / max(weights, 1e-30);
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
    step = max(step, travel / 96.0);
    float t = 1.5 * step;
    for (int k = 0; k < 96 && t < travel; ++k) {
        if (metaballField(p + t * along) >= threshold) return true;
        t += step;
    }
    return false;
}
