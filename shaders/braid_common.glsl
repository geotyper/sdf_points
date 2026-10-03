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
    return mod(pc.view.z * rounded, TAU);
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
    return pc.style.w > 10.5;
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
    vec2 scale = min(pc.view.x, pc.view.y) / pc.view.xy;
    return vec4(p.xy * vec2(1.0, -1.0) * scale / span, (4.0 - p.z) / 8.0, 1.0);
}
