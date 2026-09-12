// Material coordinates are periodic and never depend on the camera or ray hits.
layout(push_constant) uniform BraidPush {
    vec4 view;       // viewport width/height, flow phase, object rotation
    vec4 shape;      // ring radius, braid width, tube radius, braid repeats
    vec4 points;     // longitudinal samples, circumferential samples, pixel radius, strands
    vec4 look;       // tilt, glow, exposure, radius variation
    vec4 style;      // per-draw strand RGB (-1 = original tint), geometry mode
    vec4 wave;       // release strength, angular width, travel speed, squircle exponent
    vec4 motion;     // whole-loop torsion, travelling compression, circulation, optional radius limit
} pc;

const float TAU = 6.28318530718;
const float PI = 3.14159265359;
const int SURFACE_ROWS = 720;
const int SURFACE_COLUMNS = 64;
const int MAX_SPHERE_HOLES = 32;
const int MAX_NESTED_SPHERES = 12;

bool nestedSphereMode() {
    return pc.style.w > 3.5;
}

float hashScalar(float value) {
    return fract(sin(value * 127.1) * 43758.5453123);
}

vec3 sphereHoleDirection(int index, int count) {
    const float goldenAngle = 2.39996322973;
    float y = 1.0 - 2.0 * (float(index) + 0.5) / float(count);
    float radial = sqrt(max(0.0, 1.0 - y * y));
    float angle = goldenAngle * float(index);
    return vec3(radial * cos(angle), y, radial * sin(angle));
}

bool insideSphereHole(vec3 localDirection) {
    int holeCount = clamp(int(pc.shape.w + 0.5), 1, MAX_SPHERE_HOLES);
    float edge = cos(pc.shape.z);
    for (int hole = 0; hole < MAX_SPHERE_HOLES; ++hole) {
        if (hole >= holeCount) break;
        if (dot(localDirection, sphereHoleDirection(hole, holeCount)) > edge) {
            return true;
        }
    }
    return false;
}

vec3 localSphereDirection(float longitude, float latitudeParameter) {
    float latitude = -0.5 * PI + 0.5 * latitudeParameter;
    float ring = cos(latitude);
    return vec3(ring * cos(longitude), sin(latitude), ring * sin(longitude));
}

float nestedSphereRadius(float sphere) {
    float denominator = max(pc.points.w - 1.0, 1.0);
    float sequence = clamp(sphere / denominator, 0.0, 1.0);
    return pc.shape.x * mix(1.0, pc.shape.y, pow(sequence, pc.look.w));
}

vec3 nestedSphereCenter(float sphere) {
    float identity = sphere + pc.motion.x * 17.0;
    vec3 direction = vec3(hashScalar(identity + 41.3), hashScalar(identity + 47.9),
                          hashScalar(identity + 53.1)) * 2.0 - 1.0;
    direction = normalize(direction + vec3(0.003, 0.001, 0.002));
    float sequence = sphere / max(pc.points.w - 1.0, 1.0);
    float displacement = pc.shape.x * pc.motion.w * pow(sequence, 0.45);
    return direction * displacement;
}

float nestedSphereSpan() {
    int sphereCount = clamp(int(pc.points.w + 0.5), 2, MAX_NESTED_SPHERES);
    float largestExtent = 1.0;
    for (int sphere = 1; sphere < MAX_NESTED_SPHERES; ++sphere) {
        if (sphere >= sphereCount) break;
        float sequence = float(sphere) / float(sphereCount - 1);
        float radiusRatio = mix(1.0, pc.shape.y, pow(sequence, pc.look.w));
        float offsetRatio = pc.motion.w * pow(sequence, 0.45);
        largestExtent = max(largestExtent, radiusRatio + offsetRatio);
    }
    return pc.shape.x * largestExtent * 1.10;
}

vec3 rotateAroundAxis(vec3 value, vec3 axis, float angle) {
    float c = cos(angle), s = sin(angle);
    return value * c + cross(axis, value) * s + axis * dot(axis, value) * (1.0 - c);
}

vec3 rotateSphereDirection(vec3 localDirection, float sphere) {
    float identity = sphere + pc.motion.x * 17.0;
    vec3 axis = vec3(hashScalar(identity + 1.3), hashScalar(identity + 7.1),
                     hashScalar(identity + 13.7)) * 2.0 - 1.0;
    axis = normalize(axis + vec3(0.001, 0.002, 0.003));
    float randomSpeed = mix(0.55, 1.45, hashScalar(identity + 23.9));
    float speed = mix(1.0, randomSpeed, pc.wave.x);
    float direction = hashScalar(identity + 31.7) < 0.5 ? -1.0 : 1.0;
    float angle = mod(pc.view.z * speed * direction, TAU);
    return rotateAroundAxis(localDirection, axis, angle);
}

vec3 unrotateSphereDirection(vec3 direction, float sphere) {
    float identity = sphere + pc.motion.x * 17.0;
    vec3 axis = vec3(hashScalar(identity + 1.3), hashScalar(identity + 7.1),
                     hashScalar(identity + 13.7)) * 2.0 - 1.0;
    axis = normalize(axis + vec3(0.001, 0.002, 0.003));
    float randomSpeed = mix(0.55, 1.45, hashScalar(identity + 23.9));
    float speed = mix(1.0, randomSpeed, pc.wave.x);
    float rotationDirection = hashScalar(identity + 31.7) < 0.5 ? -1.0 : 1.0;
    float angle = mod(pc.view.z * speed * rotationDirection, TAU);
    return rotateAroundAxis(direction, axis, -angle);
}

vec3 nestedSpherePoint(float longitude, float latitudeParameter, float sphere) {
    vec3 localDirection = localSphereDirection(longitude, latitudeParameter);
    return nestedSphereCenter(sphere)
         + nestedSphereRadius(sphere) * rotateSphereDirection(localDirection, sphere);
}

float releaseEnvelope(float u) {
    // Smooth and periodic even as the wave crosses the material seam at 2*pi.
    float delta = u - mod(pc.view.z * pc.wave.z, TAU);
    return exp((cos(delta) - 1.0) / (pc.wave.y * pc.wave.y));
}

void curveFrame(float u, float strand, out vec3 position, out vec3 derivative,
                out float packingRadius) {
    bool torsionLoop = pc.style.w > 1.5 && pc.style.w < 2.5;
    float phase = TAU * strand / pc.points.w;
    if (pc.style.w > 2.5) {
        // A five-petal travelling rosette with a separate three-lobed depth
        // wave. Circular strands orbit the guide like luminous filaments.
        float petal = 5.0 * u - mod(0.4 * pc.view.z, TAU);
        float fold = 3.0 * u + mod(0.65 * pc.view.z, TAU);
        float orbit = pc.shape.w * u - mod(pc.view.z, TAU) + phase;
        float breath = 1.0 + 0.12 * sin(mod(0.2 * pc.view.z, TAU));
        float separation = pc.shape.y * breath;
        float r = pc.shape.x * (1.0 + 0.18 * cos(petal)) + separation * cos(orbit);
        float dr = -0.90 * pc.shape.x * sin(petal) - separation * pc.shape.w * sin(orbit);
        float z = pc.shape.x * 0.16 * sin(fold) + separation * sin(orbit);
        float dz = pc.shape.x * 0.48 * cos(fold) + separation * pc.shape.w * cos(orbit);
        vec2 radial = vec2(cos(u), sin(u));
        position = vec3(radial * r, z);
        derivative = vec3(radial * dr + vec2(-radial.y, radial.x) * r, dz);
        float pitch = pc.shape.x / sqrt(pc.shape.x * pc.shape.x +
                                        separation * separation * pc.shape.w * pc.shape.w);
        packingRadius = 0.78 * separation * sin(TAU * 0.5 / pc.points.w) * pitch;
        return;
    }
    float delta = u - mod(pc.view.z * pc.wave.z, TAU);
    float release = torsionLoop ? 0.0 : pc.wave.x * releaseEnvelope(u);
    float releaseDerivative = -release * sin(delta) / (pc.wave.y * pc.wave.y);
    // Flatten the twist locally as the pulse passes; the periodic phase warp
    // preserves the total winding count and restores the braid behind it.
    float braid = pc.shape.w * (u - release * sin(delta)) - mod(pc.view.z, TAU) + phase;
    float braidDerivative = pc.shape.w *
        (1.0 - releaseDerivative * sin(delta) - release * cos(delta));
    // Distributed compression keeps the winding positive everywhere. It
    // travels through the complete bundle instead of opening one local gap.
    float compression = 0.0;
    float compressionDerivative = 0.0;
    if (torsionLoop) {
        compression = pc.motion.y * (0.75 * cos(delta) + 0.25 * cos(2.0 * delta));
        compressionDerivative = -pc.motion.y * (0.75 * sin(delta) + 0.50 * sin(2.0 * delta));
        float modulation = pc.shape.w * 0.32 * pc.motion.y;
        braid += modulation * (sin(delta) + 0.20 * sin(2.0 * delta));
        braidDerivative += modulation * (cos(delta) + 0.40 * cos(2.0 * delta));
    }
    vec2 direction = vec2(cos(u), sin(u));
    vec2 directionDerivative = vec2(-direction.y, direction.x);
    vec2 powers = pow(abs(direction), vec2(pc.wave.w));
    float sum = powers.x + powers.y;
    float ring = pc.shape.x / pow(sum, 1.0 / pc.wave.w);
    // Offset along the squircle's normal, not its radius, so all four sides
    // retain an even band width and the central opening also becomes square.
    vec2 gradient = sign(direction) * pow(abs(direction), vec2(pc.wave.w - 1.0));
    float ringDerivative = -ring * dot(gradient, directionDerivative) / sum;
    vec2 outward = normalize(gradient);
    vec2 gradientDerivative = (pc.wave.w - 1.0) *
        pow(max(abs(direction), vec2(1e-8)), vec2(pc.wave.w - 2.0)) * directionDerivative;
    vec2 outwardDerivative = (gradientDerivative - outward * dot(outward, gradientDerivative))
                           / length(gradient);
    float separation = pc.shape.y * (1.0 + 0.32 * release + 0.25 * compression);
    float separationDerivative = pc.shape.y * (0.32 * releaseDerivative + 0.25 * compressionDerivative);
    vec2 baseDerivative = directionDerivative * ring + direction * ringDerivative;
    vec2 xy = direction * ring + outward * separation * cos(braid);
    vec2 dxy = baseDerivative + outwardDerivative * separation * cos(braid)
             + outward * (separationDerivative * cos(braid)
                        - separation * sin(braid) * braidDerivative);
    bool bundle = pc.style.w > 0.5;
    float turns = bundle ? 1.0 : 2.0;
    float depthScale = bundle ? 1.0 : 0.72;
    float depth = separation * depthScale * sin(turns * braid);
    float dz = depthScale * (separationDerivative * sin(turns * braid)
              + separation * turns * cos(turns * braid) * braidDerivative);
    position = vec3(xy, depth);
    derivative = vec3(dxy, dz);
    // Account for the helical pitch when packing circular bundle strands.
    // Leave clearance instead of allowing diameter pulses to merge neighbours.
    float pitch = length(baseDerivative) /
        sqrt(dot(baseDerivative, baseDerivative) + pow(separation * braidDerivative, 2.0));
    packingRadius = bundle ? 0.78 * separation * sin(TAU * 0.5 / pc.points.w) * pitch : 1e5;
}

float tubeRadius(float u, float strand) {
    float phase = TAU * strand / pc.points.w;
    float wave = 0.76 * sin(2.0 * u - mod(0.65 * pc.view.z, TAU) + phase)
               + 0.24 * sin(5.0 * u + mod(0.4 * pc.view.z, TAU) - phase);
    if (pc.style.w > 2.5) {
        return pc.shape.z * (1.0 + pc.look.w * 0.65 *
            sin(5.0 * u - mod(0.4 * pc.view.z, TAU) + phase));
    }
    if (pc.style.w > 1.5) {
        float delta = u - mod(pc.view.z * pc.wave.z, TAU);
        float compression = pc.motion.y * (0.75 * cos(delta) + 0.25 * cos(2.0 * delta));
        return pc.shape.z * (1.0 + 0.55 * pc.look.w * wave) * (1.0 + 0.28 * compression);
    }
    float thinning = 1.0 - 0.62 * pc.wave.x * releaseEnvelope(u);
    return pc.shape.z * (1.0 + pc.look.w * wave) * thinning;
}

void deformCurveFrame(inout vec3 center, inout vec3 derivative, inout vec3 reference) {
    // Deform only the centreline and transport its frame with the analytic
    // Jacobian. Sweeping a NEW circle afterwards preserves a round section;
    // warping all surface vertices would stretch it into an ellipse.
    float bendPhase = mod(pc.view.z * 0.4, TAU);
    float twistPhase = mod(pc.view.z * 0.65, TAU);
    float bend = 0.10 * pc.motion.x / pc.shape.x * sin(bendPhase);
    derivative.z += 2.0 * bend * (center.x * derivative.x - center.y * derivative.y);
    reference.z += 2.0 * bend * (center.x * reference.x - center.y * reference.y);
    center.z += bend * (center.x * center.x - center.y * center.y);
    // Tube radius must not also move the centreline when its slider changes.
    float depthScale = 1.70 * pc.shape.y;
    float twistGradient = pc.motion.x * 0.38 / depthScale * sin(twistPhase + 0.6);
    float angle = pc.motion.x * (0.13 * sin(bendPhase)
                + 0.38 * center.z / depthScale * sin(twistPhase + 0.6));
    float c = cos(angle), s = sin(angle);
    mat2 rotation = mat2(c, s, -s, c);
    center.xy = rotation * center.xy;
    vec2 angularDerivative = vec2(-center.y, center.x);
    derivative.xy = rotation * derivative.xy + angularDerivative * twistGradient * derivative.z;
    reference.xy = rotation * reference.xy + angularDerivative * twistGradient * reference.z;
}

void tubeFrame(float u, float strand, out vec3 center, out vec3 tangent,
               out vec3 x, out vec3 y, out float radius) {
    bool torsionLoop = pc.style.w > 1.5 && pc.style.w < 2.5;
    // Material coordinates remain permanent: advect the whole tube frame and
    // its attached points together along the rounded-square guide.
    if (torsionLoop) {
        u += mod(pc.view.z * pc.motion.z, TAU);
    }
    vec3 derivative;
    float packingRadius;
    curveFrame(u, strand, center, derivative, packingRadius);
    vec3 reference = vec3(cos(u), sin(u), 0.0);
    if (torsionLoop) {
        deformCurveFrame(center, derivative, reference);
    }
    tangent = normalize(derivative);
    x = normalize(reference - tangent * dot(reference, tangent));
    y = cross(tangent, x);
    radius = tubeRadius(u, strand);
    if (pc.motion.w > 0.5 && pc.style.w > 0.5) {
        radius = min(radius, packingRadius);
    }
}

vec3 surfacePoint(float u, float v, float strand) {
    if (nestedSphereMode()) {
        return nestedSpherePoint(u, v, strand);
    }
    vec3 center, tangent, x, y;
    float radius;
    tubeFrame(u, strand, center, tangent, x, y, radius);
    return center + radius * (x * cos(v) + y * sin(v));
}

vec3 surfaceNormal(float u, float v, float strand) {
    if (nestedSphereMode()) {
        return rotateSphereDirection(localSphereDirection(u, v), strand);
    }
    vec3 du = surfacePoint(u + 0.0005, v, strand) - surfacePoint(u - 0.0005, v, strand);
    vec3 dv = surfacePoint(u, v + 0.0005, strand) - surfacePoint(u, v - 0.0005, strand);
    return normalize(cross(dv, du));
}

vec3 toView(vec3 p) {
    float c = cos(pc.view.w), s = sin(pc.view.w);
    p.xy = mat2(c, s, -s, c) * p.xy;
    c = cos(pc.look.x); s = sin(pc.look.x);
    p.yz = mat2(c, s, -s, c) * p.yz;
    return p;
}

vec3 fromView(vec3 p) {
    float c = cos(pc.look.x), s = sin(pc.look.x);
    p.yz = mat2(c, -s, s, c) * p.yz;
    c = cos(pc.view.w); s = sin(pc.view.w);
    p.xy = mat2(c, -s, s, c) * p.xy;
    return p;
}

vec4 projectPoint(vec3 p) {
    p = toView(p);
    // Bound all pulse phases and rotations without breathing the camera zoom.
    float ringBound = pc.shape.x * pow(2.0, 0.5 - 1.0 / pc.wave.w);
    float span = (ringBound + pc.shape.y * (1.0 + 0.32 * pc.wave.x)
                + pc.shape.z * (1.0 + pc.look.w)) * 1.10;
    if (pc.style.w > 1.5) {
        // Z-axis torsion preserves XY radius. Include the saddle's possible
        // projection under camera tilt, using a bound fixed across all phases.
        float radialBound = ringBound + pc.shape.y * (1.0 + 0.25 * pc.motion.y)
                          + pc.shape.z * (1.0 + pc.look.w);
        float bendBound = 0.10 * pc.motion.x * radialBound * radialBound / pc.shape.x;
        span = (radialBound + bendBound * abs(sin(pc.look.x))) * 1.10;
    }
    if (pc.style.w > 2.5) {
        span = (pc.shape.x * 1.18 + pc.shape.y * 1.12 + pc.shape.z * (1.0 + pc.look.w)
              + 0.16 * pc.shape.x * abs(sin(pc.look.x))) * 1.10;
    }
    if (nestedSphereMode()) {
        span = nestedSphereSpan();
    }
    vec2 scale = min(pc.view.x, pc.view.y) / pc.view.xy;
    return vec4(p.xy * vec2(1.0, -1.0) * scale / span, (4.0 - p.z) / 8.0, 1.0);
}
