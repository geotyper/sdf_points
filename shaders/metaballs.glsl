// Metaballs (mode 25): balls whose fields add up, the surface where the sum
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
// The polytope storage buffer holds four vec4 per ball (centre and radius,
// colour and where its share of the area starts, 0, centre and radius the
// frame before), then from METABALL_HEADER: (reset, frame number, particles,
// fade step), (neighbour reach, neighbours' weight at the rest density, hash
// table cells, spacing), (particles to start with, push strength, 0, 0).

const int METABALL = 4;
const int METABALL_HEADER = 4 * 24;
// Particle slots in the shared storage buffer (binding 3): position and
// life, (alive, 0, 0, 0), scratch position and life. The neighbour table
// follows, METABALL_CELL uints per cell: a count and the particles.
const int METABALL_PARTICLE = 3;
const int METABALL_PARTICLE_LIMIT = 600000;
const int METABALL_CELL = 16;

vec4 metaball(int ball, int slot) {
    return polytopeData[METABALL * ball + slot];
}

vec4 metaballHeader(int slot) {
    return polytopeData[METABALL_HEADER + slot];
}

int metaballCount() {
    return max(int(pc.shape.w + 0.5), 1);
}

float metaballSpan() {
    return pc.shape.x * pc.shape.z * 1.08 / max(pc.loop.w, 0.05);
}

float metaballField(vec3 p) {
    float sum = 0.0;
    for (int i = 0; i < metaballCount(); ++i) {
        vec4 b = metaball(i, 0);
        float far = pc.motion.y * b.w;
        vec3 d = p - b.xyz;
        float q = 1.0 - dot(d, d) / (far * far);
        sum += q > 0.0 ? q * q * q : 0.0;
    }
    return sum;
}

// The field and its gradient.
float metaballField(vec3 p, out vec3 gradient) {
    float sum = 0.0;
    gradient = vec3(0.0);
    for (int i = 0; i < metaballCount(); ++i) {
        vec4 b = metaball(i, 0);
        float far = pc.motion.y * b.w;
        vec3 d = p - b.xyz;
        float q = 1.0 - dot(d, d) / (far * far);
        if (q <= 0.0) continue;
        sum += q * q * q;
        gradient -= 6.0 * q * q * d / (far * far);
    }
    return sum;
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
// fields, the strongest ball, and how far it leads: the largest of the
// shares that go as the fields to the power `sharpness` and add up to 1.
void metaballShade(vec3 p, out vec3 normal, out vec3 blend, out int strongest, out float leading) {
    vec3 gradient = vec3(0.0);
    blend = vec3(0.0);
    float total = 0.0, weights = 0.0, most = 0.0;
    float sharpness = max(pc.motion.w, 0.5);
    strongest = 0;
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
        float weight = pow(k, sharpness);
        weights += weight;
        if (weight > most) {
            most = weight;
            strongest = i;
        }
    }
    normal = normalize(gradient + vec3(1e-7, 0.0, 0.0));
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
    float t = 1.5 * step;
    for (int k = 0; k < 96 && t < travel; ++k) {
        if (metaballField(p + t * along) >= threshold) return true;
        t += step;
    }
    return false;
}
