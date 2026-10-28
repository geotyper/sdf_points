// Material coordinates are periodic and never depend on the camera or ray hits.
layout(push_constant) uniform BraidPush {
    vec4 view;       // viewport width/height, flow phase, object rotation
    vec4 shape;      // ring radius, braid width, tube radius, braid repeats
    vec4 points;     // longitudinal samples, circumferential samples, pixel radius, strands
    vec4 look;       // tilt, glow, exposure, radius variation
    vec4 style;      // per-draw strand RGB (-1 = original tint), geometry mode
    vec4 wave;       // release strength, angular width, travel speed, squircle exponent
    vec4 motion;     // whole-loop torsion, travelling compression, circulation, optional radius limit
    vec4 loop;       // x: loop length in 2*pi cycles of the flow phase (0 = free running)
                     // Cloth geometries: y = pulse depth, z = closed point lattice
                     // step (0 = golden-angle spiral). Tube geometries: y = ridge
                     // depth (0 = smooth), z = ridge travel rate, w = ridges around
                     // the tube + (ridges along it + 32) / 128.
} pc;

const float TAU = 6.28318530718;
const float PI = 3.14159265359;
const int SURFACE_ROWS = 720;
const int SURFACE_COLUMNS = 64;
const int MAX_SPHERE_HOLES = 32;
const int MAX_NESTED_SPHERES = 12;
const int MORPH_SEGMENTS = 64;

// Phase of a motion running `frequency` times as fast as the flow phase. In a
// loop capture every frequency is rounded to a whole number of cycles over the
// loop, so all motions return to their start together and the clip is seamless.
float timePhase(float frequency) {
    float cycles = pc.loop.x;
    float rounded = cycles > 0.0 ? round(frequency * cycles) / cycles : frequency;
    float wrapped = mod(pc.view.z * rounded, TAU);
    // Passed through sine, cosine and arctangent so the optimiser cannot merge
    // the wrap into the sums that use the phase: after a long run that would
    // swallow the tiny steps the normals are differenced with.
    float phase = atan(sin(wrapped), cos(wrapped));
    return phase < 0.0 ? phase + TAU : phase;
}

bool nestedSphereMode() {
    return pc.style.w > 3.5 && pc.style.w < 4.5;
}

bool eversionMode() {
    return pc.style.w > 4.5 && pc.style.w < 5.5;
}

bool sleeveMode() {
    return pc.style.w > 5.5 && pc.style.w < 6.5;
}

bool morphMode() {
    return pc.style.w > 6.5 && pc.style.w < 7.5;
}

bool vortexMode() {
    return pc.style.w > 7.5 && pc.style.w < 8.5;
}

bool ridgeMode() {
    return pc.style.w > 8.5 && pc.style.w < 9.5;
}

bool tentacleMode() {
    return pc.style.w > 10.5 && pc.style.w < 11.5;
}

bool smaleMode() {
    return pc.style.w > 13.5 && pc.style.w < 14.5;
}

bool archMode() {
    return pc.style.w > 14.5 && pc.style.w < 15.5;
}

bool nestedCubeMode() {
    return pc.style.w > 15.5 && pc.style.w < 16.5;
}

bool hopfMode() {
    return pc.style.w > 16.5 && pc.style.w < 17.5;
}

bool tesseractMode() {
    return pc.style.w > 17.5 && pc.style.w < 18.5;
}

bool polytopeMode() {
    return pc.style.w > 18.5 && pc.style.w < 19.5;
}

// Defined in polytope.glsl, which only the vertex shaders include.
float polytopeSpan();

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
    float angle = timePhase(speed * direction);
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
    float angle = timePhase(speed * rotationDirection);
    return rotateAroundAxis(direction, axis, -angle);
}

vec3 nestedSpherePoint(float longitude, float latitudeParameter, float sphere) {
    vec3 localDirection = localSphereDirection(longitude, latitudeParameter);
    return nestedSphereCenter(sphere)
         + nestedSphereRadius(sphere) * rotateSphereDirection(localDirection, sphere);
}

// Nested cubes reuse the nested spheres' push constants and rotations: shape =
// outer half-size, size ratio between neighbours, border width (face units, 1 =
// solid faces), roundness (0 cube .. 1 sphere). Only a frame around the rim of
// every face is solid, so the inner cubes show through the windows.
// wave.yzw: infinite zoom on, zoom levels per flow cycle, twist per level;
// loop.y: camera distance in outer sizes (0 = orthographic); loop.z: 0, or
// 1 + hold for cubes turning in sync; loop.w: flash when they line up.
//
// Roundness turns the cube into a superellipsoid |x|^n + |y|^n + |z|^n = 1 whose
// exponent falls from 40 (all but a cube) to 2 (the inscribed sphere).
float cubeExponent() {
    return 2.0 / mix(0.05, 1.0, pc.shape.w);
}

float nestedCubeSize(float depth) {
    return pc.shape.x * pow(pc.shape.y, depth);
}

bool cubeZoom() {
    return pc.wave.y > 0.5;
}

// Depth of the cube drawn in this slot, in levels below the outer size. While
// zooming every cube slides one level out per zoom cycle; the slots hand over
// at the cycle's end, so the picture repeats exactly.
float cubeDepth(float slot) {
    return cubeZoom() ? slot - timePhase(pc.wave.z) / TAU : slot;
}

// The cube leaving past the camera fades out, the one born inside fades in.
float cubeFade(float depth) {
    if (!cubeZoom()) return 1.0;
    return smoothstep(-1.0, -0.45, depth) * (1.0 - smoothstep(pc.points.w - 1.0, pc.points.w, depth));
}

// While zooming the turn belongs to the depth, not to the cube: each cube spins
// about an axis that wanders with depth and is twisted against its neighbours.
bool cubeSync() {
    return !cubeZoom() && pc.loop.z > 0.5;
}

// In sync every cube turns a whole number of times per flow cycle, so at the
// cycle's end they all line up at once. The hold slows the turn around that
// moment: the cubes drift together, stand aligned, and burst apart.
float cubeSyncAngle() {
    float phase = timePhase(1.0);
    return phase - (pc.loop.z - 1.0) * sin(phase);
}

// 1 while the cubes stand aligned, falling to 0 away from it.
float cubeAlignment() {
    return cubeSync() ? pow(0.5 + 0.5 * cos(timePhase(1.0)), 12.0) : 0.0;
}

vec3 turnCube(vec3 direction, float depth) {
    if (cubeSync()) {
        float identity = depth + pc.motion.x * 17.0;
        vec3 axis = vec3(hashScalar(identity + 1.3), hashScalar(identity + 7.1),
                         hashScalar(identity + 13.7)) * 2.0 - 1.0;
        axis = normalize(axis + vec3(0.001, 0.002, 0.003));
        float turns = 1.0 + floor(3.0 * hashScalar(identity + 23.9));
        float sense = hashScalar(identity + 31.7) < 0.5 ? -1.0 : 1.0;
        return rotateAroundAxis(direction, axis, sense * turns * cubeSyncAngle());
    }
    if (!cubeZoom()) return rotateSphereDirection(direction, depth);
    float place = depth + 3.1 * pc.motion.x;
    vec3 axis = normalize(vec3(sin(1.7 * place + 0.3), 0.8, cos(1.1 * place + 1.0)));
    direction = rotateAroundAxis(direction, vec3(0.0, 1.0, 0.0), pc.wave.w * depth);
    return rotateAroundAxis(direction, axis, timePhase(1.0));
}

float cubeCameraDistance() {
    return pc.loop.y * pc.shape.x;
}

// Half the outer cube's diagonal at the current roundness, with a margin.
float nestedCubeSpan() {
    return pc.shape.x * sqrt(3.0) / pow(3.0, 1.0 / cubeExponent()) * 1.10;
}

// Point (a, b) in [-1, 1]^2 on one of the six faces of the unit cube.
vec3 cubeFacePoint(int face, vec2 ab) {
    float side = face % 2 == 0 ? 1.0 : -1.0;
    int axis = face / 2;
    if (axis == 0) return vec3(side, ab.x * side, ab.y);
    if (axis == 1) return vec3(ab.y, side, ab.x * side);
    return vec3(ab.x * side, ab.y, side);
}

// Pushes a point of the cube along its ray onto the rounded body.
vec3 roundedCubePoint(vec3 cubePoint) {
    float n = cubeExponent();
    vec3 a = abs(cubePoint) / max(abs(cubePoint.x), max(abs(cubePoint.y), abs(cubePoint.z)));
    float norm = pow(pow(a.x, n) + pow(a.y, n) + pow(a.z, n), 1.0 / n);
    return cubePoint / (max(abs(cubePoint.x), max(abs(cubePoint.y), abs(cubePoint.z))) * norm);
}

vec3 roundedCubeNormal(vec3 p) {
    vec3 a = abs(p) / max(abs(p.x), max(abs(p.y), abs(p.z)));
    return normalize(sign(p) * pow(a, vec3(cubeExponent() - 1.0)));
}

// Hopf fibration: every point of the sphere S^2 has a circle of S^3 over it,
// and any two of these circles are linked. Seen through stereographic
// projection the fibres over one latitude fill a torus, and the tori nest.
// shape = scale, tube radius, 4D tumble angle, tumble rate; points = samples
// along a fibre, around its tube, pixel radius, fibres per torus; look.w =
// tori; wave = innermost and outermost latitude, light flow rate, fibre drift
// rate; motion.x = spin rate, motion.y = torus drawn, motion.z = breathing.
float hopfLatitude(float torus) {
    float tori = max(pc.look.w, 1.0);
    return mix(pc.wave.x, pc.wave.y, torus / max(tori - 1.0, 1.0))
         + pc.motion.z * sin(timePhase(1.0) + TAU * torus / tori);
}

// Camera zoom of the Hopf fibration and the tesseract in loop.w: 0 or 1 frame
// the whole figure, more comes closer.
float fourDZoom() {
    return pc.loop.w > 0.0 ? pc.loop.w : 1.0;
}

float hopfSpan() {
    return pc.shape.x * 3.4 / fourDZoom();
}

// Fibres reaching further than this pass near the projection pole: dropped.
float hopfCutoff() {
    return pc.shape.x * 6.0;
}

// Point at fibre parameter t (0 .. 4 pi once around) of one fibre of a torus.
vec3 hopfPoint(float t, float torus, float fiber) {
    float eta = hopfLatitude(torus);
    // Neighbouring tori are staggered by half a fibre.
    float alpha = TAU * (fiber + 0.5 * mod(torus, 2.0)) / max(pc.points.w, 1.0);
    // Turning z1 and z2 against each other moves every fibre to the next base
    // point; a whole turn of beta returns every point, so the drift loops.
    float beta = timePhase(pc.wave.w);
    float first = 0.5 * (t + alpha) + beta, second = 0.5 * (t - alpha) - beta;
    vec4 q = vec4(cos(eta) * vec2(cos(first), sin(first)),
                  sin(eta) * vec2(cos(second), sin(second)));
    // A tilt in 4D rolls the tori through each other and through infinity.
    float tumble = pc.shape.z * sin(timePhase(pc.shape.w));
    float c = cos(tumble), s = sin(tumble);
    q = vec4(c * q.x - s * q.w, q.y, q.z, s * q.x + c * q.w);
    vec3 p = q.xyz / max(1.0 - q.w, 1e-4);
    float spin = timePhase(pc.motion.x);
    p.xz = mat2(cos(spin), sin(spin), -sin(spin), cos(spin)) * p.xz;
    return pc.shape.x * p;
}

// Tube frame around a fibre: every fibre is a circle, so its curvature points
// at the centre and gives a normal that never flips.
void hopfFrame(float t, float torus, float fiber, out vec3 center, out vec3 normal,
               out vec3 binormal) {
    const float step = 0.02;
    center = hopfPoint(t, torus, fiber);
    vec3 ahead = hopfPoint(t + step, torus, fiber);
    vec3 behind = hopfPoint(t - step, torus, fiber);
    vec3 tangent = normalize(ahead - behind);
    vec3 bend = ahead + behind - 2.0 * center;
    bend -= tangent * dot(bend, tangent);
    if (dot(bend, bend) < 1e-14) {
        bend = cross(tangent, abs(tangent.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0));
    }
    normal = normalize(bend);
    binormal = cross(tangent, normal);
}

// Tesseract: the 32 edges of the 4D cube [-1, 1]^4 turned in 4D and seen in
// perspective from a 4D camera, so its far cell shows inside its near one and
// the turn carries one through the other. shape = size, tube radius, 4D camera
// distance, XW turn rate; points = samples along an edge, around its tube,
// pixel radius; look.w = YZ turn rate; wave = near colour + spin rate;
// motion = far colour + light flow rate; loop.y = light strength.
vec4 tesseractVertex(int edge, float along) {
    int axis = edge / 8;
    int combo = edge % 8;
    vec4 vertex;
    int bit = 0;
    for (int coordinate = 0; coordinate < 4; ++coordinate) {
        if (coordinate == axis) {
            vertex[coordinate] = 2.0 * along - 1.0;
        } else {
            vertex[coordinate] = ((combo >> bit) & 1) == 1 ? 1.0 : -1.0;
            ++bit;
        }
    }
    return vertex;
}

vec4 turnTesseract(vec4 q) {
    float a = timePhase(pc.shape.w), b = timePhase(pc.look.w);
    q = vec4(cos(a) * q.x - sin(a) * q.w, q.y, q.z, sin(a) * q.x + cos(a) * q.w);
    q = vec4(q.x, cos(b) * q.y - sin(b) * q.z, sin(b) * q.y + cos(b) * q.z, q.w);
    return q;
}

// Perspective magnification of a 4D point: near cells grow, far ones shrink.
float tesseractScale(float w) {
    return pc.shape.z / (pc.shape.z - w);
}

vec3 tesseractProject(vec4 q) {
    vec3 p = pc.shape.x * tesseractScale(q.w) * q.xyz;
    float spin = timePhase(pc.wave.w);
    p.xz = mat2(cos(spin), sin(spin), -sin(spin), cos(spin)) * p.xz;
    return p;
}

// Furthest any vertex can project (|q| = 2), whatever the turn.
float tesseractSpan() {
    float reach = 0.0;
    for (int i = 0; i <= 32; ++i) {
        float angle = 0.5 * PI * float(i) / 32.0;
        reach = max(reach, 2.0 * cos(angle) * tesseractScale(2.0 * sin(angle)));
    }
    return pc.shape.x * reach * 1.08 / fourDZoom();
}

// Point on the tube around one edge; the tube swells with the 4D perspective.
// `w` returns the turned 4D depth for the colour.
vec3 tesseractTubePoint(int edge, float along, float around, out vec3 outward, out float w) {
    vec4 start = turnTesseract(tesseractVertex(edge, 0.0));
    vec4 end = turnTesseract(tesseractVertex(edge, 1.0));
    vec4 q = mix(start, end, along);
    w = q.w;
    vec3 a = tesseractProject(start), b = tesseractProject(end);
    vec3 direction = normalize(b - a + vec3(1e-6, 0.0, 0.0));
    vec3 side = cross(direction, abs(direction.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0));
    side = normalize(side);
    vec3 up = cross(direction, side);
    outward = side * cos(around) + up * sin(around);
    return tesseractProject(q) + pc.shape.y * tesseractScale(q.w) * outward;
}

// Inside a face's window, away from its frame.
bool insideCubeWindow(vec2 ab) {
    return max(abs(ab.x), abs(ab.y)) < 1.0 - pc.shape.z;
}

// Punctured sphere eversion reuses the push constants: shape = radius, hole
// half-angle, end hold, spin; wave.rgb / motion.rgb = outer / inner side colour.
//
// The sphere is pulled through its own opening like a sock, pole first. A fold
// circle climbs the still unturned sphere; the turned material runs from it as a
// cone up to the opening and ends in a rounded tip, which swells into the
// mirrored sphere once it is through. Every stage has the sphere's area, and a
// point sits where the area between it and the pole stays what it was on the
// sphere, so the cloth of points slides without stretching its area.
float eversionProgress() {
    float swing = cos(timePhase(1.0));
    // Ease towards both ends so the closed spheres linger.
    swing = mix(swing, swing * (1.5 - 0.5 * swing * swing), pc.shape.z);
    return 0.5 - 0.5 * swing;
}

// Turned area left for a flat-topped tip once the cone's throat has advanced
// this far from the fold (0) towards the opening (1), less a rounded tip's share.
float eversionSpareArea(float turned, float foldRadius, float ring, float reach, float advance) {
    const float tipRoundness = 0.6;
    float throatRadius = mix(foldRadius, ring, advance);
    return turned - (foldRadius + throatRadius) * advance * reach
         - (1.0 + tipRoundness * tipRoundness) * throatRadius * throatRadius;
}

// Meridian section at the material's area fraction 0 (pole) .. 1 (rim of the
// hole). xy: distance from the axis and height along it; zw: outer side normal.
// Areas below are divided by pi.
vec4 eversionProfile(float material) {
    float radius = pc.shape.x;
    float ring = radius * sin(pc.shape.y);
    float centreDepth = radius * cos(pc.shape.y);
    float total = 2.0 * radius * (radius + centreDepth);
    float progress = eversionProgress();
    float turned = progress * total;
    // Sphere area grows linearly with height, and so does the fold.
    vec2 fold;
    fold.y = -(1.0 - progress) * (radius + centreDepth);
    float foldOffset = fold.y + centreDepth;
    fold.x = sqrt(max(radius * radius - foldOffset * foldOffset, 0.0));
    // The cone's throat advances from the fold towards the opening as far as
    // the turned area allows while keeping a rounded tip on top of it.
    float reach = length(vec2(ring, 0.0) - fold);
    float advance = 0.0;
    if (eversionSpareArea(turned, fold.x, ring, reach, 1.0) >= 0.0) {
        advance = 1.0;
    } else if (eversionSpareArea(turned, fold.x, ring, reach, 0.0) > 0.0) {
        float low = 0.0, high = 1.0;
        for (int step = 0; step < 20; ++step) {
            float middle = 0.5 * (low + high);
            if (eversionSpareArea(turned, fold.x, ring, reach, middle) > 0.0) {
                low = middle;
            } else {
                high = middle;
            }
        }
        advance = 0.5 * (low + high);
    }
    vec2 throat = mix(fold, vec2(ring, 0.0), advance);
    float coneLength = advance * reach;
    float coneArea = (fold.x + throat.x) * coneLength;
    // What is left after the cone forms the tip, a spherical cap on the throat.
    float tipHeight = sqrt(max(turned - coneArea - throat.x * throat.x, 0.0));
    float tipArea = throat.x * throat.x + tipHeight * tipHeight;

    float area = material * total;
    vec2 position, tangent;
    if (area <= tipArea) {
        float drop = area * tipHeight / max(tipArea, 1e-9);
        position = vec2(sqrt(max(area - drop * drop, 0.0)), throat.y + tipHeight - drop);
        tangent = vec2(tipArea - 2.0 * tipHeight * drop, -2.0 * tipHeight * position.x);
    } else if (area <= tipArea + coneArea && coneLength > 1e-6) {
        float along = (area - tipArea) / coneLength;
        along /= throat.x + sqrt(throat.x * throat.x + (fold.x - throat.x) * along);
        position = mix(throat, fold, along);
        tangent = fold - throat;
    } else {
        float height = fold.y + (area - tipArea - coneArea) / (2.0 * radius) + centreDepth;
        height = min(height, centreDepth);
        position = vec2(sqrt(max(radius * radius - height * height, 0.0)), height - centreDepth);
        tangent = vec2(-height, position.x);
    }
    tangent = dot(tangent, tangent) > 1e-12 ? normalize(tangent) : vec2(1.0, 0.0);
    // Follow the cloth so the remaining sphere and the new one share the frame.
    position.y -= 0.5 * (radius + centreDepth) * (progress * progress + progress - 1.0);
    return vec4(position, tangent.y, -tangent.x);
}

vec3 eversionAround(float longitude, vec2 section) {
    longitude += timePhase(pc.shape.w);
    return vec3(section.x * cos(longitude), section.y, section.x * sin(longitude));
}

vec3 eversionPoint(float longitude, float material) {
    return eversionAround(longitude, eversionProfile(material).xy);
}

vec3 eversionNormal(float longitude, float material) {
    return eversionAround(longitude, eversionProfile(material).zw);
}

float eversionSpan() {
    // Largest distance from the origin over all stages and hole sizes, in radii.
    return pc.shape.x * 1.25 * 1.10;
}

// Everting sleeve reuses the push constants: shape = outer radius, half length
// of the straight walls, inner / outer radius ratio, colour bands; wave.rgb /
// motion.rgb = the two band colours, wave.w = spin.
//
// A tube of cloth folded back into itself: the material climbs the inner wall,
// rolls outwards over the top lip, descends the outer wall and is swallowed at
// the bottom lip, so it turns inside out without end. Points are spaced by area
// along this track and therefore keep their density on both walls.
// xy: distance from the axis and height along it; zw: normal away from the walls' gap.
vec4 sleeveProfile(float material) {
    float outer = pc.shape.x, inner = outer * pc.shape.z, straight = pc.shape.y;
    float centre = 0.5 * (outer + inner), lip = 0.5 * (outer - inner);
    float wall = 2.0 * straight;
    // Areas are divided by 2*pi.
    float lipArea = PI * lip * centre;
    float total = (inner + outer) * wall + 2.0 * lipArea;
    float area = fract(material + timePhase(1.0) / TAU) * total;
    if (area < inner * wall) {
        return vec4(inner, area / inner - straight, -1.0, 0.0);
    }
    area -= inner * wall;
    bool top = area < lipArea;
    if (!top) {
        area -= lipArea;
        if (area < outer * wall) {
            return vec4(outer, straight - area / outer, 1.0, 0.0);
        }
        area -= outer * wall;
    }
    // Half a torus: its radius shrinks towards the axis, so the angle swept by
    // a given area has no closed form. Newton converges in a few steps.
    float side = top ? -1.0 : 1.0;
    float angle = area / (lip * centre);
    for (int step = 0; step < 4; ++step) {
        angle -= (lip * (centre * angle + side * lip * sin(angle)) - area)
               / (lip * (centre + side * lip * cos(angle)));
    }
    vec2 around = vec2(side * cos(angle), -side * sin(angle));
    return vec4(centre + lip * around.x, -side * straight + lip * around.y, around);
}

vec3 sleeveAround(float longitude, vec2 section) {
    longitude += timePhase(pc.wave.w);
    return vec3(section.x * cos(longitude), section.y, section.x * sin(longitude));
}

vec3 sleevePoint(float longitude, float material) {
    return sleeveAround(longitude, sleeveProfile(material).xy);
}

vec3 sleeveNormal(float longitude, float material) {
    return sleeveAround(longitude, sleeveProfile(material).zw);
}

float sleeveSpan() {
    float lip = 0.5 * pc.shape.x * (1.0 - pc.shape.z);
    return length(vec2(pc.shape.x, pc.shape.y + lip)) * 1.10;
}

// Morphing sleeve reuses the push constants: shape = size, hole ratio, corner
// roundness, colour bands; points.y = half length; look.w = morph amount;
// wave = first colour and spin; motion = second colour and morph rate.
//
// The sleeve's section becomes a superellipse around the tube of cloth, and
// four slow waves reshape it while the material keeps flowing: the length
// stretches, the corners round off towards a torus, the walls flare into a cup
// one way and the other, and the waist pinches or bulges.
// size: half thickness, half length, superellipse exponent; bend: flare, waist.
void morphShape(out vec3 size, out vec2 bend) {
    float rate = pc.motion.w, amount = pc.look.w, hole = pc.shape.y;
    size.x = 0.5 * (1.0 - hole);
    size.y = max(pc.points.y * (1.0 + 0.4 * amount * sin(timePhase(rate))), size.x);
    float corners = 2.0 + (pc.shape.z - 2.0)
                  * (1.0 - amount * (0.5 + 0.5 * sin(timePhase(2.0 * rate) + 1.0)));
    size.z = corners;
    // The flare stops short of closing the hole at either end.
    bend.x = 0.8 * hole * amount * sin(timePhase(rate) + 1.9);
    bend.y = 0.35 * amount * sin(timePhase(3.0 * rate) + 0.6);
}

// Section point after `turn` laps of the track, in units of the outer radius.
// The track runs down the outer wall and back up the inner one.
vec2 morphCurve(float turn, vec3 size, vec2 bend) {
    float angle = -TAU * turn;
    vec2 direction = vec2(cos(angle), sin(angle));
    // Polar form of the superellipse: its speed stays finite along the walls,
    // so equal steps of the lap never leave a long stretch unsampled.
    vec2 powers = pow(abs(direction), vec2(size.z));
    vec2 corner = direction / pow(powers.x + powers.y, 1.0 / size.z);
    float radius = 0.5 * (1.0 + pc.shape.y) + size.x * corner.x + bend.x * corner.y;
    return vec2(radius * (1.0 - bend.y * (1.0 - corner.y * corner.y)), size.y * corner.y);
}

// xy: distance from the axis and height along it; zw: normal away from the
// walls' gap. With byArea the material coordinate is an area fraction carried
// by the flow, which keeps the points' density; otherwise it is the lap itself.
vec4 morphProfile(float material, bool byArea) {
    vec3 size;
    vec2 bend;
    morphShape(size, bend);
    // Areas are divided by 2*pi and measured on the polygon through the samples.
    float total = 0.0;
    vec2 previous = morphCurve(0.0, size, bend);
    for (int segment = 1; segment <= MORPH_SEGMENTS; ++segment) {
        vec2 current = morphCurve(float(segment) / float(MORPH_SEGMENTS), size, bend);
        total += 0.5 * (previous.x + current.x) * distance(previous, current);
        previous = current;
    }
    float turn = material;
    if (byArea) {
        float area = fract(material + timePhase(1.0) / TAU) * total;
        turn = 1.0;
        previous = morphCurve(0.0, size, bend);
        for (int segment = 1; segment <= MORPH_SEGMENTS; ++segment) {
            vec2 current = morphCurve(float(segment) / float(MORPH_SEGMENTS), size, bend);
            float piece = 0.5 * (previous.x + current.x) * distance(previous, current);
            if (area <= piece) {
                turn = (float(segment - 1) + area / max(piece, 1e-9)) / float(MORPH_SEGMENTS);
                break;
            }
            area -= piece;
            previous = current;
        }
    }
    // Every shape is scaled to the same area, so the cloth neither grows nor shrinks.
    float scale = pc.shape.x / sqrt(total);
    vec2 tangent = morphCurve(turn + 0.002, size, bend) - morphCurve(turn - 0.002, size, bend);
    vec2 normal = dot(tangent, tangent) > 1e-14 ? normalize(vec2(-tangent.y, tangent.x))
                                                : vec2(1.0, 0.0);
    return vec4(scale * morphCurve(turn, size, bend), normal);
}

float morphSpan() {
    // Largest extent over all morph phases and slider settings, per unit of size.
    return pc.shape.x * mix(0.88, 1.14, pc.look.w) * 1.08;
}

// Vortex ring reuses the push constants: shape = ring radius, coil radius, tube
// radius, twist; points = samples along, samples around, pixel radius, strands;
// look.w = twist wave; wave = hole colour and spin; motion = rim colour.
//
// Strands wound around an unseen torus that rolls through its own hole without
// end. The roll carries them inwards over the top: they dive into the hole
// together, fan out underneath and climb back over the rim, while a torsion
// wave runs around the ring and wrings them.

// Angle around the torus' tube that has this share of its area behind it.
// Strands spaced by area keep their distance on the way through the hole.
float vortexAngle(float fraction) {
    float angle = TAU * fraction;
    for (int step = 0; step < 4; ++step) {
        angle -= (pc.shape.x * (angle - TAU * fraction) + pc.shape.y * sin(angle))
               / (pc.shape.x + pc.shape.y * cos(angle));
    }
    return angle;
}

// Where the strand's centreline sits around the tube at this place on the ring.
float vortexStrandAngle(float u, float strand) {
    float winding = (pc.shape.w * u + pc.look.w * sin(u - timePhase(1.0))) / TAU;
    return vortexAngle(fract((strand + winding) / pc.points.w + timePhase(1.0) / TAU));
}

vec3 vortexCentre(float u, float strand) {
    float angle = vortexStrandAngle(u, strand);
    return sleeveAround(u, vec2(pc.shape.x + pc.shape.y * cos(angle), pc.shape.y * sin(angle)));
}

void vortexFrame(float u, float strand, out vec3 center, out vec3 tangent,
                 out vec3 x, out vec3 y, out float radius) {
    center = vortexCentre(u, strand);
    tangent = normalize(vortexCentre(u + 0.001, strand) - vortexCentre(u - 0.001, strand));
    float angle = vortexStrandAngle(u, strand);
    vec3 reference = sleeveAround(u, vec2(cos(angle), sin(angle)));
    x = normalize(reference - tangent * dot(reference, tangent));
    y = cross(tangent, x);
    // Strands thin a little where the ring crowds them into the hole.
    float crowding = (pc.shape.x + pc.shape.y * cos(angle)) / pc.shape.x;
    radius = pc.shape.z * mix(1.0, crowding, 0.5);
}

float vortexSpan() {
    return (pc.shape.x + pc.shape.y + pc.shape.z * 1.25) * 1.10;
}

// Ridged torus reuses the push constants: shape = ring radius, tube radius, ridge
// height, ridge count; points.y = twist, points.w = ridge sharpness; look.w =
// twist wave; wave = ridge colour and spin; motion = body colour and pulse
// rate; loop.y = pulse depth. With loop.w rows of bumps around the tube the
// ridges give way to bumps: shape.w then counts bumps along the ring, points.y
// shifts each row along it, and look.w is the bump size within its cell.
// style.x >= 0 makes it a chain of two such tori and, for the points, says
// which link is being drawn.
//
// A torus of cloth rolls through its own hole without end. Ridges rise out of
// it, each winding around the ring, and are carried by the roll: they dive into
// the hole together, fan out underneath and climb back over the rim. A torsion
// wave runs around the ring and wrings them, and with a full pulse they sink
// back into the plain torus once per pulse cycle.
float ridgeGrowth() {
    return 1.0 - pc.loop.y * (0.5 + 0.5 * cos(timePhase(pc.motion.w)));
}

// Share of the plain torus' area between the outer equator and this angle
// around the tube; points spaced by it keep their density through the hole.
float ridgeFraction(float angle) {
    return (pc.shape.x * angle + pc.shape.y * sin(angle)) / (pc.shape.x * TAU);
}

// Ridges belong to the material: 0 in the valleys, 1 on the crests.
float ridgeCrest(float longitude, float material) {
    if (pc.loop.w > 0.5) {
        // Staggered rows of round bumps, fixed to the material like the ridges.
        float across = material * pc.loop.w;
        float row = floor(across);
        float along = pc.shape.w * longitude / TAU + 0.5 * row + pc.points.y * row / pc.loop.w;
        vec2 cell = vec2(fract(across), fract(along)) - 0.5;
        float reach = 4.0 * dot(cell, cell) / (pc.look.w * pc.look.w);
        return pow(max(1.0 - reach, 0.0), pc.points.w);
    }
    float phase = TAU * pc.shape.w * material - pc.points.y * longitude
                - pc.look.w * sin(longitude - timePhase(1.0));
    return pow(max(0.5 + 0.5 * cos(phase), 0.0), pc.points.w);
}

vec3 ridgePoint(float longitude, float angle) {
    float material = fract(ridgeFraction(angle) - timePhase(1.0) / TAU);
    float growth = pc.shape.z * ridgeGrowth();
    // The body thins as the ridges rise from it; bumps only add to it.
    float crest = ridgeCrest(longitude, material);
    float radius = pc.shape.y * (pc.loop.w > 0.5 ? 1.0 + 0.45 * growth * crest
                                                 : 1.0 - 0.45 * growth + growth * crest);
    return sleeveAround(longitude, vec2(pc.shape.x + radius * cos(angle), radius * sin(angle)));
}

vec3 ridgeNormal(float longitude, float angle) {
    vec3 around = ridgePoint(longitude, angle + 0.002) - ridgePoint(longitude, angle - 0.002);
    vec3 along = ridgePoint(longitude + 0.002, angle) - ridgePoint(longitude - 0.002, angle);
    return normalize(cross(around, along));
}

bool ridgeChain() {
    return pc.style.x > -0.5;
}

// Two links of a chain: each torus passes through the other's hole, in planes
// at right angles, with its tube centred on the other's middle.
vec3 ridgeLinkTurn(vec3 value, int link) {
    return ridgeChain() && link > 0 ? vec3(value.x, -value.z, value.y) : value;
}

vec3 ridgeLinkPoint(vec3 point, int link) {
    if (!ridgeChain()) return point;
    return ridgeLinkTurn(point, link) + vec3(link > 0 ? 0.5 : -0.5, 0.0, 0.0) * pc.shape.x;
}

float ridgeSpan() {
    float reach = pc.shape.x + pc.shape.y * (1.0 + 0.55 * pc.shape.z);
    return (ridgeChain() ? reach + 0.5 * pc.shape.x : reach) * 1.10;
}

// Tentacle sphere reuses the push constants: shape = sphere radius, tentacle
// length, tentacle width (radians), tentacle count; points = sphere samples,
// samples per tentacle, pixel radius, tentacle roundness; look.w = ripple
// height; wave = tip colour and spin; motion = body colour and, per draw, which
// part is drawn (0 body, 1 tentacles); loop = ripples per tentacle, swirl, sway.
//
// One closed skin: a sphere that grows tentacles around evenly spread axes.
// Ripples start between the tentacles, run across the body and climb each one
// to its tip. The tentacles lean and circle on their own, and together they
// are wrung around the vertical axis one way and then the other.
const float TENTACLE_PATCH_WIDTHS = 2.2;
const int TENTACLE_SEGMENTS = 64;
const int MAX_TENTACLES = 32;

int tentacleCount() {
    return clamp(int(pc.shape.w + 0.5), 1, MAX_TENTACLES);
}

float tentaclePatchAngle() {
    return TENTACLE_PATCH_WIDTHS * pc.shape.z;
}

// Height above the sphere at this angle from the tentacle's axis.
float tentacleHeight(float angle) {
    return pc.shape.y * exp(-pow(angle / pc.shape.z, pc.points.w));
}

// Unbent, unrippled section of a tentacle: distance from its axis and height
// along it. The sphere's own surface continues it beyond the tentacle.
vec2 tentacleSection(float angle) {
    return (pc.shape.x + tentacleHeight(angle)) * vec2(sin(angle), cos(angle));
}

// Angle from the axis with this share of the patch's area inside it, so the
// points of a tentacle keep one density from its tip down to the body.
float tentacleAreaAngle(float fraction) {
    float patchAngle = tentaclePatchAngle();
    float total = 0.0;
    vec2 previous = tentacleSection(0.0);
    for (int segment = 1; segment <= TENTACLE_SEGMENTS; ++segment) {
        vec2 current = tentacleSection(patchAngle * float(segment) / float(TENTACLE_SEGMENTS));
        total += 0.5 * (previous.x + current.x) * distance(previous, current);
        previous = current;
    }
    float area = fraction * total;
    previous = tentacleSection(0.0);
    for (int segment = 1; segment <= TENTACLE_SEGMENTS; ++segment) {
        vec2 current = tentacleSection(patchAngle * float(segment) / float(TENTACLE_SEGMENTS));
        float piece = 0.5 * (previous.x + current.x) * distance(previous, current);
        if (area <= piece) {
            // Area grows with the square of the angle next to the axis.
            float within = area / max(piece, 1e-12);
            within = segment == 1 ? sqrt(within) : within;
            return patchAngle * (float(segment - 1) + within) / float(TENTACLE_SEGMENTS);
        }
        area -= piece;
        previous = current;
    }
    return patchAngle;
}

void tentacleFrame(int index, out vec3 axis, out vec3 first, out vec3 second) {
    axis = sphereHoleDirection(index, tentacleCount());
    first = normalize(cross(abs(axis.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), axis));
    second = cross(axis, first);
}

// The tentacle whose axis is nearest to this direction, and the direction's
// polar coordinates around that axis.
void tentacleNearest(vec3 direction, out int index, out float angle, out float turn) {
    int count = tentacleCount();
    float nearest = -2.0;
    index = 0;
    for (int candidate = 0; candidate < MAX_TENTACLES; ++candidate) {
        if (candidate >= count) break;
        float alignment = dot(direction, sphereHoleDirection(candidate, count));
        if (alignment > nearest) {
            nearest = alignment;
            index = candidate;
        }
    }
    vec3 axis, first, second;
    tentacleFrame(index, axis, first, second);
    angle = acos(clamp(nearest, -1.0, 1.0));
    turn = atan(dot(direction, second), dot(direction, first));
}

// -1..1; crests travel from the body up to the tentacle tips.
float tentacleRipple(float angle) {
    float run = pc.shape.x * angle + pc.shape.y - tentacleHeight(angle);
    return cos(TAU * pc.loop.y * run / max(pc.shape.y, 1e-3) - timePhase(2.0));
}

vec3 tentacleSurface(int index, float angle, float turn) {
    vec3 axis, first, second;
    tentacleFrame(index, axis, first, second);
    float height = tentacleHeight(angle);
    float slope = -height * pc.points.w * pow(angle / pc.shape.z, pc.points.w - 1.0) / pc.shape.z;
    vec2 along = vec2(sin(angle), cos(angle));
    vec2 tangent = slope * along + (pc.shape.x + height) * vec2(along.y, -along.x);
    vec2 outward = normalize(vec2(-tangent.y, tangent.x));
    // Ripples push along the skin's normal, so they show on the tentacle walls too.
    vec2 section = (pc.shape.x + height) * along
                 + pc.look.w * pc.shape.x * tentacleRipple(angle) * outward;
    vec3 point = section.x * (cos(turn) * first + sin(turn) * second) + section.y * axis;
    // Bending turns about the sphere's centre by an angle that grows towards
    // the tip, which leaves the body where it is.
    float tipward = pow(height / max(pc.shape.y, 1e-6), 1.5);
    float lean = timePhase(1.0) + 2.39996322973 * float(index);
    point = rotateAroundAxis(point, cos(lean) * first + sin(lean) * second, pc.loop.w * tipward);
    point = rotateAroundAxis(point, vec3(0.0, 1.0, 0.0),
                             pc.loop.z * sin(timePhase(1.0)) * tipward);
    return rotateAroundAxis(point, vec3(0.0, 1.0, 0.0), timePhase(pc.wave.w));
}

vec3 tentacleNormal(int index, float angle, float turn) {
    angle = max(angle, 0.004);
    vec3 outwards = tentacleSurface(index, angle + 0.002, turn)
                  - tentacleSurface(index, angle - 0.002, turn);
    vec3 around = tentacleSurface(index, angle, turn + 0.01)
                - tentacleSurface(index, angle, turn - 0.01);
    return normalize(cross(outwards, around));
}

vec3 tentacleBodyPoint(vec3 direction) {
    int index;
    float angle, turn;
    tentacleNearest(direction, index, angle, turn);
    return tentacleSurface(index, angle, turn);
}

float tentacleSpan() {
    return (pc.shape.x * (1.0 + pc.look.w) + pc.shape.y) * 1.10;
}

// Sphere eversion reuses the push constants: shape = radius, end hold;
// wave = colour of one side and spin; motion = colour of the other side.
//
// A closed sphere turned inside out through itself, with no hole, crease or
// pinch: A. and W. Bednorz, "Analytic sphere eversion using ruled surfaces"
// (arXiv:1711.10466), for n = 2 and Q = 2/3. The poles are pushed through
// each other into a wormhole whose throat is a ruled surface; the throat
// twists through the halfway surface, where neither side is preferred, and
// the same steps then run backwards on the other side.
const float SMALE_Q = 2.0 / 3.0;
const float SMALE_OMEGA = 2.0;
// Radius of the round sphere the formulas start and end with.
const float SMALE_SPHERE = 0.8164966;

// Closes the infinite cylinder into a sphere: damping far from the axis, then
// a stereographic-like inversion (the paper's eta = beta = 1).
vec3 smaleClose(vec3 point, float xi, float alpha) {
    float damping = xi + dot(point.xy, point.xy);
    vec2 planar = point.xy * pow(damping, -0.25);
    float height = point.z / damping;
    float reach = dot(planar, planar);
    float gamma = 2.0 * sqrt(alpha);
    float denominator = alpha + reach;
    float growth = exp(gamma * height);
    // (exp(gamma * height) - 1) / gamma, which stays finite as gamma goes to zero.
    float scaled = gamma * height;
    float lift = abs(scaled) < 0.01 ? height * (1.0 + scaled * (0.5 + scaled / 6.0))
                                    : (growth - 1.0) / gamma;
    return vec3(planar * growth / denominator,
                (alpha - reach) / denominator * lift
                    + sqrt(alpha) * (1.0 - reach) / (denominator * (alpha + 1.0)));
}

// The wormhole: latitude spreads along the ruling, which runs off to infinity
// at both poles.
vec3 smaleWormhole(float latitude, float longitude, float time, float q, float xi, float alpha) {
    float cosine = max(cos(latitude), 1e-3);
    float ruling = SMALE_OMEGA * sin(latitude) / (cosine * cosine);
    float p = 1.0 - abs(q * time);
    vec2 turn = vec2(cos(longitude), sin(longitude));
    vec3 point = vec3(time * turn.x + p * turn.y - ruling * turn.y,
                      time * turn.y + p * turn.x + ruling * turn.x,
                      ruling * sin(2.0 * longitude) - 0.5 * time * cos(2.0 * longitude)
                          - q * time * ruling);
    return smaleClose(point, xi, alpha);
}

// Between the round sphere (unfolding 0) and the unfolded wormhole (1).
vec3 smaleUnfold(float latitude, float longitude, float time, float unfolding) {
    float cosine = cos(latitude), sine = sin(latitude);
    float square = cosine * cosine;
    vec2 turn = vec2(cos(longitude), sin(longitude));
    float blend = 1.0 - unfolding + unfolding * square;
    float swing = unfolding * SMALE_OMEGA * sine;
    vec2 planar = vec2(time * blend * turn.x - swing * turn.y, time * blend * turn.y + swing * turn.x);
    float spread = time * time * blend * blend + swing * swing;
    float height = unfolding * (SMALE_OMEGA * sine * (sin(2.0 * longitude) - SMALE_Q * time) * square
                              - 0.5 * time * cos(2.0 * longitude) * square * square)
                 - (1.0 - unfolding) * time * sqrt(abs(time)) * sine;
    return vec3(planar * cosine / pow(spread, 0.75), -height / spread);
}

// Progress 0..1 through the whole eversion; the second half mirrors the first.
float smaleProgress() {
    float swing = cos(timePhase(1.0));
    // Ease towards both ends so the round spheres linger.
    swing = mix(swing, swing * (1.5 - 0.5 * swing * swing), pc.shape.y);
    return 0.5 - 0.5 * swing;
}

vec3 smalePoint(float latitude, float longitude) {
    float progress = smaleProgress();
    float side = progress < 0.5 ? -1.0 : 1.0;
    float end = side / SMALE_Q;
    // Stage lengths as shares of one half: unfold the sphere, open the
    // inverted wormhole, twist its throat shut, pass through the halfway surface.
    float stage = (1.0 - abs(2.0 * progress - 1.0)) * 0.62;
    vec3 point;
    if (stage < 0.16) {
        point = smaleUnfold(latitude, longitude, end, stage / 0.16);
    } else if (stage < 0.28) {
        // Squared, because the opening moves with the square root of alpha.
        float opening = (stage - 0.16) / 0.12;
        opening *= opening;
        point = smaleWormhole(latitude, longitude, end, SMALE_Q, opening, opening);
    } else if (stage < 0.38) {
        point = smaleWormhole(latitude, longitude, end,
                              SMALE_Q * (1.0 - (stage - 0.28) / 0.10), 1.0, 1.0);
    } else {
        point = smaleWormhole(latitude, longitude, end * (1.0 - min((stage - 0.38) / 0.24, 1.0)),
                              0.0, 1.0, 1.0);
    }
    // The paper's axis of symmetry becomes the vertical one.
    point = point.xzy * pc.shape.x / SMALE_SPHERE;
    return rotateAroundAxis(point, vec3(0.0, 1.0, 0.0), timePhase(pc.wave.w));
}

// Normal of one side of the material; it ends up pointing the other way.
vec3 smaleNormal(float latitude, float longitude) {
    vec3 north = smalePoint(latitude + 0.002, longitude) - smalePoint(latitude - 0.002, longitude);
    vec3 east = smalePoint(latitude, longitude + 0.002) - smalePoint(latitude, longitude - 0.002);
    return normalize(cross(north, east));
}

float smaleSpan() {
    // Largest extent of any stage, measured in sphere radii.
    return pc.shape.x * 1.97 * 1.08;
}

// Arched sphere reuses the push constants: shape = sphere radius, arch height,
// tube radius, arch count; points = sphere samples, arch rows + columns / 128,
// pixel radius, closed lattice step; look.w = twist; wave = first colour and
// spin; motion = second colour and, per draw, which part is drawn (0 sphere,
// 1 arches); loop.y = bands, loop.z = checker sectors (0 = plain bands).
//
// A fountain that feeds itself: the cloth climbs the sphere from the south
// pole to the north, pours out through a crown of arches, runs down them
// outside and re-enters around the south pole. Bands painted on the material
// show the circulation.
// How much wider than its tube an arch is where it meets the sphere.
const float ARCH_FLARE = 1.2;

float archFoot() {
    // Far enough from the pole for the flared feet to stand side by side.
    return asin(clamp(2.2 * pc.shape.w * pc.shape.z / (PI * pc.shape.x), 0.12, 0.9));
}

float archFlow() {
    return timePhase(1.0) / TAU;
}

// Centreline of an arch, 0 at its north foot and 1 at its south foot.
vec3 archCentre(float along, float arch) {
    float foot = archFoot();
    // Eased, so the arch leaves and meets the sphere at a right angle.
    float travel = along * along * (3.0 - 2.0 * along);
    float colatitude = foot + (PI - 2.0 * foot) * travel;
    float longitude = TAU * (arch / pc.shape.w + pc.look.w * travel) + timePhase(pc.wave.w);
    float reach = pc.shape.x + pc.shape.y * sin(PI * along);
    return reach * vec3(sin(colatitude) * cos(longitude), cos(colatitude),
                        sin(colatitude) * sin(longitude));
}

vec3 archPoint(float along, float around, float arch) {
    vec3 centre = archCentre(along, arch);
    vec3 tangent = normalize(archCentre(along + 0.001, arch) - archCentre(along - 0.001, arch));
    vec3 outward = normalize(centre);
    vec3 x = normalize(outward - tangent * dot(outward, tangent));
    vec3 y = cross(tangent, x);
    // The tube flares like a root as it nears the sphere and grows out of it.
    float above = pc.shape.y * sin(PI * along);
    float radius = pc.shape.z * (1.0 + ARCH_FLARE * exp(-above / (0.9 * pc.shape.z)));
    return centre + radius * (x * cos(around) + y * sin(around));
}

vec3 archNormal(float along, float around, float arch) {
    vec3 lengthwise = archPoint(along + 0.001, around, arch) - archPoint(along - 0.001, around, arch);
    vec3 crosswise = archPoint(along, around + 0.01, arch) - archPoint(along, around - 0.01, arch);
    return normalize(cross(crosswise, lengthwise));
}

// Direction on the sphere for a share of its area counted from the south pole.
vec3 archSphereDirection(float areaShare, float longitude) {
    float height = 2.0 * areaShare - 1.0;
    float ring = sqrt(max(1.0 - height * height, 0.0));
    longitude += timePhase(pc.wave.w);
    return vec3(ring * cos(longitude), height, ring * sin(longitude));
}

// Which of the two colours this piece of material has: bands across the flow,
// optionally cut into sectors for a checkerboard.
float archPattern(float along, float sector) {
    float band = floor(along * pc.loop.y);
    return mod(band + (pc.loop.z > 0.5 ? floor(sector) : 0.0), 2.0);
}

// The sphere (part 0) or one of the arches (parts 1..) on a regular grid.
vec3 archGridPoint(float u, float v, float part) {
    return part < 0.5 ? pc.shape.x * localSphereDirection(u, v)
                      : archPoint(u / TAU, v, part - 1.0);
}

float archSpan() {
    return (pc.shape.x + max(pc.shape.y + pc.shape.z, (1.0 + ARCH_FLARE) * pc.shape.z)) * 1.10;
}

float releaseEnvelope(float u) {
    // Smooth and periodic even as the wave crosses the material seam at 2*pi.
    float delta = u - timePhase(pc.wave.z);
    return exp((cos(delta) - 1.0) / (pc.wave.y * pc.wave.y));
}

void curveFrame(float u, float strand, out vec3 position, out vec3 derivative,
                out float packingRadius) {
    bool torsionLoop = pc.style.w > 1.5 && pc.style.w < 2.5;
    float phase = TAU * strand / pc.points.w;
    if (pc.style.w > 2.5) {
        // A five-petal travelling rosette with a separate three-lobed depth
        // wave. Circular strands orbit the guide like luminous filaments.
        float petal = 5.0 * u - timePhase(0.4);
        float fold = 3.0 * u + timePhase(0.65);
        float orbit = pc.shape.w * u - timePhase(1.0) + phase;
        float breath = 1.0 + 0.12 * sin(timePhase(0.2));
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
    float delta = u - timePhase(pc.wave.z);
    float release = torsionLoop ? 0.0 : pc.wave.x * releaseEnvelope(u);
    float releaseDerivative = -release * sin(delta) / (pc.wave.y * pc.wave.y);
    // Flatten the twist locally as the pulse passes; the periodic phase warp
    // preserves the total winding count and restores the braid behind it.
    float braid = pc.shape.w * (u - release * sin(delta)) - timePhase(1.0) + phase;
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
    float wave = 0.76 * sin(2.0 * u - timePhase(0.65) + phase)
               + 0.24 * sin(5.0 * u + timePhase(0.4) - phase);
    if (pc.style.w > 2.5) {
        return pc.shape.z * (1.0 + pc.look.w * 0.65 *
            sin(5.0 * u - timePhase(0.4) + phase));
    }
    if (pc.style.w > 1.5) {
        float delta = u - timePhase(pc.wave.z);
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
    float bendPhase = timePhase(0.4);
    float twistPhase = timePhase(0.65);
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
    if (vortexMode()) {
        vortexFrame(u, strand, center, tangent, x, y, radius);
        return;
    }
    bool torsionLoop = pc.style.w > 1.5 && pc.style.w < 2.5;
    // Material coordinates remain permanent: advect the whole tube frame and
    // its attached points together along the rounded-square guide.
    if (torsionLoop) {
        u += timePhase(pc.motion.z);
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

// Ridges carved into a tube: 1 on the crests, 0 in the valleys. Ridges around
// the section and along the strand combine into screw threads; with none around
// they are beads running along it. The wave travels, the points stay put.
float tubeRidge(float u, float v) {
    float around = floor(pc.loop.w);
    float along = fract(pc.loop.w) * 128.0 - 32.0;
    return pow(max(0.5 + 0.5 * cos(around * v - along * u + timePhase(pc.loop.z)), 0.0), 1.5);
}

bool tubeRidges() {
    return pc.loop.y > 0.0 && !nestedSphereMode() && !eversionMode() && !sleeveMode()
        && !morphMode() && !ridgeMode() && !tentacleMode();
}

vec3 surfacePoint(float u, float v, float strand) {
    if (nestedSphereMode()) {
        return nestedSpherePoint(u, v, strand);
    }
    if (eversionMode()) {
        // The long grid axis follows the meridian, where the folds are.
        return eversionPoint(v, u / TAU);
    }
    if (sleeveMode()) {
        return sleevePoint(v, u / TAU);
    }
    if (morphMode()) {
        return sleeveAround(v, morphProfile(u / TAU, false).xy);
    }
    if (ridgeMode()) {
        return ridgeLinkPoint(ridgePoint(v, u), int(strand + 0.5));
    }
    if (tentacleMode()) {
        return tentacleBodyPoint(localSphereDirection(u, v));
    }
    if (smaleMode()) {
        return smalePoint(0.5 * (v - PI), u);
    }
    vec3 center, tangent, x, y;
    float radius;
    tubeFrame(u, strand, center, tangent, x, y, radius);
    if (tubeRidges()) {
        // Crests keep the tube's radius, so ridges never add to its overlap.
        radius *= 1.0 - 0.6 * pc.loop.y * (1.0 - tubeRidge(u, v));
    }
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

// Projects a point already turned into view space.
vec4 projectView(vec3 p) {
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
    if (eversionMode()) {
        span = eversionSpan();
    }
    if (sleeveMode()) {
        span = sleeveSpan();
    }
    if (morphMode()) {
        span = morphSpan();
    }
    if (vortexMode()) {
        span = vortexSpan();
    }
    if (ridgeMode()) {
        span = ridgeSpan();
    }
    if (tentacleMode()) {
        span = tentacleSpan();
    }
    if (smaleMode()) {
        span = smaleSpan();
    }
    if (archMode()) {
        span = archSpan();
    }
    if (nestedCubeMode()) {
        span = nestedCubeSpan();
    }
    if (hopfMode()) {
        span = hopfSpan();
    }
    if (tesseractMode()) {
        span = tesseractSpan();
    }
    if (polytopeMode()) {
        span = polytopeSpan();
    }
    vec2 scale = min(pc.view.x, pc.view.y) / pc.view.xy;
    vec4 clip = vec4(p.xy * vec2(1.0, -1.0) * scale / span, (4.0 - p.z) / 8.0, 1.0);
    if (nestedCubeMode() && pc.loop.y > 0.0) {
        // Perspective for flying through the cubes; sizes at the centre stay put.
        // Whatever reaches the camera is dropped by the callers first.
        float distance = cubeCameraDistance();
        clip *= max(distance - p.z, 1e-4) / distance;
    }
    return clip;
}

vec4 projectPoint(vec3 p) {
    return projectView(toView(p));
}
