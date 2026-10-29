#version 450
#extension GL_GOOGLE_include_directive : require
#include "braid_common.glsl"
#include "polytope.glsl"
#include "sections4d.glsl"
#include "surfaces4d.glsl"
#include "origami.glsl"
#include "shells.glsl"
#include "metaballs.glsl"

layout(set = 0, binding = 3, std430) readonly buffer SectionHits {
    vec4 sectionHits[];
};

vec3 sectionPalette(float index) {
    return polytopeData[int(mod(index, 5.0))].rgb;
}

layout(location = 0) out vec2 disc;
layout(location = 1) flat out vec3 pointColor;
layout(location = 2) flat out float pointFacing;

// Effects shared by every geometry; all zero when they are switched off.
layout(set = 0, binding = 1) uniform PointEffects {
    vec4 drift; // flow amount, flow scale, speed, per-point jitter amount
    vec4 shed;  // share of points shedding, flight distance, rate, fall
    vec4 view;  // x: see-through (no hidden skin, points turned away stay visible)
} fx;

const vec2 corners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(-1, 1),
                             vec2(-1, 1), vec2(1, -1), vec2(1, 1));

// Places a point on screen after the effects have moved it. Every motion is
// driven by the scene's own phase, so loops stay seamless.
vec4 effectClip(vec3 position, vec3 viewNormal, inout float radiusPixels) {
    int id = gl_InstanceIndex;
    vec3 offset = vec3(0.0);
    if (fx.drift.x > 0.0) {
        // A slow flow field: neighbouring points wander together.
        vec3 place = position * fx.drift.y;
        float first = timePhase(fx.drift.z), second = timePhase(2.0 * fx.drift.z);
        offset += 0.5 * fx.drift.x * vec3(sin(place.y + first) + sin(1.7 * place.z - second),
                                          sin(place.z + second) + sin(1.7 * place.x + first),
                                          sin(place.x - first) + sin(1.7 * place.y + second));
    }
    if (fx.drift.w > 0.0) {
        // Jitter: every point trembles on its own.
        vec3 own = vec3(hashIndex(id, 1u), hashIndex(id, 2u), hashIndex(id, 3u));
        offset += fx.drift.w * sin(TAU * own + timePhase(3.0 * fx.drift.z)
                                             * (1.0 + floor(3.0 * own.yzx)));
    }
    // Slide along the surface or away from it, never under the hidden skin.
    offset -= viewNormal * min(dot(offset, viewNormal), 0.0);
    if (fx.shed.x > 0.0 && hashIndex(id, 4u) < fx.shed.x) {
        // A shed point leaves along the normal, shrinking until it is gone,
        // then grows back where it started.
        float life = fract(timePhase(fx.shed.z) / TAU + hashIndex(id, 5u));
        float flight = smoothstep(0.0, 0.7, life);
        float fade = life < 0.7 ? 1.0 - flight : smoothstep(0.7, 1.0, life);
        if (life < 0.7) {
            offset += flight * flight * (fx.shed.y * viewNormal - vec3(0.0, fx.shed.w, 0.0));
        }
        radiusPixels *= fade;
        pointColor *= fade;
    }
    radiusPixels = max(radiusPixels, 0.05);
    // The nested spheres keep the sign: their fragment shader reads a side from it.
    if (fx.view.x > 0.5 && !nestedSphereMode()) {
        pointFacing = 1.0;
    }
    return projectView(toView(position) + offset);
}

void main() {
    disc = corners[gl_VertexIndex];
    if (nestedSphereMode()) {
        int pointsPerSphere = max(int(pc.motion.z + 0.5), 1);
        int sphereIndex = int(pc.motion.y + 0.5);
        int pointIndex = gl_InstanceIndex;
        const float goldenAngle = 2.39996322973;
        float y = 1.0 - 2.0 * (float(pointIndex) + 0.5) / float(pointsPerSphere);
        float radial = sqrt(max(0.0, 1.0 - y * y));
        float angle = goldenAngle * float(pointIndex);
        vec3 localDirection = vec3(radial * cos(angle), y, radial * sin(angle));
        vec3 position = nestedSphereCenter(float(sphereIndex))
                      + nestedSphereRadius(float(sphereIndex))
                      * rotateSphereDirection(localDirection, float(sphereIndex));
        vec3 normal = toView(rotateSphereDirection(localDirection, float(sphereIndex)));
        vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
        float diffuse = abs(dot(normal, light));
        float illumination = 0.10 + 0.90 * pow(diffuse, 0.8);
        float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                           * mix(0.36, 1.0, illumination);
        radiusPixels = max(radiusPixels, 0.30);
        vec3 tint = pc.style.x >= 0.0 ? pc.style.rgb : vec3(0.55, 0.72, 1.0);
        pointColor = tint * illumination * pc.look.z;
        // The fragment shader uses the sign to select the matching ray/sphere
        // intersection before evaluating the animated hole mask.
        pointFacing = normal.z;

        vec4 clip = effectClip(position, normal, radiusPixels);
        clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
        gl_Position = clip;
        disc *= (radiusPixels + 0.65) / radiusPixels;
        return;
    }

    if (nestedCubeMode()) {
        // Each face's frame is four equal strips laid as a pinwheel; every
        // strip gets a grid of points with the same spacing both ways.
        float depth = cubeDepth(float(int(pc.motion.y + 0.5)));
        float fade = cubeFade(depth);
        int perStrip = max(int(pc.motion.z + 0.5) / 24, 1);
        int strip = gl_InstanceIndex / perStrip;
        int sampleIndex = gl_InstanceIndex % perStrip;
        float border = pc.shape.z;
        float stripLength = 2.0 - border;
        int rows = max(int(round(sqrt(float(perStrip) * border / stripLength))), 1);
        int columns = max(perStrip / rows, 1);
        int row = sampleIndex / columns, column = sampleIndex % columns;
        if (strip >= 24 || row >= rows || fade < 0.005) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            pointColor = vec3(0.0);
            pointFacing = 0.0;
            return;
        }
        vec2 ab = vec2(-1.0 + stripLength * (float(column) + 0.5) / float(columns),
                       1.0 - border * (float(row) + 0.5) / float(rows));
        for (int turn = 0; turn < strip % 4; ++turn) {
            ab = vec2(-ab.y, ab.x);
        }
        vec3 local = roundedCubePoint(cubeFacePoint(strip / 4, ab));
        vec3 position = nestedCubeSize(depth) * turnCube(local, depth);
        vec3 normal = toView(turnCube(roundedCubeNormal(local), depth));
        float nearness = 1.0;
        if (pc.loop.y > 0.0) {
            float distance = cubeCameraDistance();
            float viewDepth = toView(position).z;
            if (viewDepth > 0.92 * distance) {
                gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
                pointColor = vec3(0.0);
                pointFacing = 0.0;
                return;
            }
            nearness = distance / (distance - viewDepth);
        }
        vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
        // Both sides light up: the far faces are seen from inside, through the windows.
        float diffuse = abs(dot(normal, light));
        float illumination = 0.10 + 0.90 * pow(diffuse, 0.8);
        float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                           * mix(0.36, 1.0, illumination);
        if (cubeZoom()) {
            // Every cube carries the same points, so they shrink with it: the
            // picture is the same at every level.
            radiusPixels *= clamp(pow(pc.shape.y, depth) * nearness, 0.3, 2.5);
        }
        radiusPixels = max(radiusPixels, 0.30);
        vec3 tint = pc.style.x >= 0.0 ? pc.style.rgb : vec3(0.55, 0.72, 1.0);
        // A flash as the synchronised cubes line up.
        float aligned = cubeAlignment();
        pointColor = (tint * (1.0 + pc.loop.w * aligned) + 0.3 * pc.loop.w * aligned)
                   * illumination * pc.look.z * fade;
        // The depth skin alone decides what is hidden.
        pointFacing = 1.0;

        vec4 clip = effectClip(position, normal, radiusPixels);
        clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy * clip.w;
        gl_Position = clip;
        disc *= (radiusPixels + 0.65) / radiusPixels;
        return;
    }

    if (sliceMode() || juliaMode()) {
        // sections.comp found the point; one instance per ray and crossing.
        vec4 found = sectionHits[2 * gl_InstanceIndex];
        vec4 normalFlags = sectionHits[2 * gl_InstanceIndex + 1];
        vec3 hit = found.xyz, normal = normalFlags.xyz;
        float tag = found.w;
        int flags = int(normalFlags.w + 0.5);
        vec3 toCamera = sectionSpin(fromView(vec3(0.0, 0.0, 1.0)), -1.0);
        bool shown = (flags & 1) != 0 && dot(normal, toCamera) > -0.02
                  && (fx.view.x > 0.5 || (flags & 2) == 0);
        if (!shown) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            pointColor = vec3(0.0);
            pointFacing = 0.0;
            return;
        }
        vec3 position = sectionSpin(hit * sectionScale(), 1.0);
        vec3 viewNormal = toView(sectionSpin(normal, 1.0));
        vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
        float diffuse = max(dot(viewNormal, light), 0.0);
        float illumination = 0.12 + 0.88 * pow(diffuse, 0.8);
        float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                           * mix(0.36, 1.0, illumination);
        radiusPixels = max(radiusPixels, 0.30);
        // Colour: 0 cells (palette) or orbit bands, 1 by depth in 4D or orbit
        // trap, 2 cells shaded by depth in 4D.
        int colorMode = int(pc.points.w + 0.5);
        vec3 tint;
        if (juliaMode()) {
            float trap = clamp(tag, 0.0, 1.5) / 1.5;
            tint = colorMode == 0 ? sectionPalette(floor(trap * 7.0))
                                  : mix(pc.style.rgb, pc.wave.rgb, smoothstep(0.05, 0.9, trap));
        } else {
            float depth = smoothstep(-0.8, 0.8, sectionTurn(vec4(hit, sliceW())).w);
            tint = colorMode == 1 ? mix(pc.wave.rgb, pc.style.rgb, depth)
                 : colorMode == 2 ? sectionPalette(tag) * mix(0.35, 1.25, depth)
                                  : sectionPalette(tag);
        }
        pointColor = tint * illumination * pc.look.z;
        pointFacing = smoothstep(-0.02, 0.03, viewNormal.z);

        vec4 clip = effectClip(position, viewNormal, radiusPixels);
        clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
        gl_Position = clip;
        disc *= (radiusPixels + 0.65) / radiusPixels;
        return;
    }

    if (origamiMode()) {
        // Every fan triangle of a face holds rows parallel to its rim,
        // offset + 1, offset + 2, ... points long, so the spacing stays even.
        int face = origamiFaceOf(gl_InstanceIndex);
        vec4 share = origamiFace(face, 3);
        int rows = max(int(share.y + 0.5), 1), offset = int(share.z + 0.5);
        int sides = origamiSides(face);
        int perFan = rows * offset + rows * (rows + 1) / 2;
        int rest = gl_InstanceIndex - int(share.x + 0.5);
        int fan = rest / perFan, index = rest % perFan;
        float b = float(2 * offset + 1);
        int row = int((sqrt(b * b + 8.0 * float(index)) - b) * 0.5);
        if (row * offset + row * (row + 1) / 2 > index) --row;
        if ((row + 1) * offset + (row + 1) * (row + 2) / 2 <= index) ++row;
        int column = index - (row * offset + row * (row + 1) / 2);
        if (fan >= sides || row >= rows || gl_InstanceIndex >= int(pc.points.x + 0.5)) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            pointColor = vec3(0.0);
            pointFacing = 0.0;
            return;
        }
        float rho = (float(offset + row) + 0.5) / float(offset + rows);
        float u = (float(column) + 0.5) / float(offset + row + 1);
        vec3 normal;
        vec3 position = origamiPoint(face, fan, rho, u, normal);
        vec3 viewNormal = toView(normal);
        vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
        // Both sides light up: the skin alone decides what is hidden.
        float diffuse = abs(dot(viewNormal, light));
        float illumination = 0.12 + 0.88 * pow(diffuse, 0.8);
        float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                           * mix(0.36, 1.0, illumination);
        radiusPixels = max(radiusPixels, 0.30);
        vec4 colour = origamiFace(face, 2);
        // The hinge lights up while it turns, and so do whole pieces the
        // figure marks (a tesseract's hinge squares, the jitterbug's icosahedron).
        float hinge = int(colour.w + 0.5) == fan && colour.w > -0.5
                    ? smoothstep(0.75, 1.0, rho) * origamiFace(face, 0).w : 0.0;
        hinge = max(hinge, share.w);
        vec3 tint = colour.rgb * (1.0 + pc.motion.w) + 0.25 * pc.motion.w;
        tint = tint * (1.0 + 2.0 * pc.motion.z * hinge) + 0.4 * pc.motion.z * hinge;
        radiusPixels *= 1.0 + 0.4 * pc.motion.z * hinge;
        pointColor = tint * illumination * pc.look.z;
        pointFacing = 1.0;

        vec4 clip = effectClip(position, viewNormal, radiusPixels);
        clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
        gl_Position = clip;
        disc *= (radiusPixels + 0.65) / radiusPixels;
        return;
    }

    if (metaballMode()) {
        // Every ball's Fibonacci sphere, slid out along its directions to
        // the surface; shown where the ball leads, hidden by the field
        // between it and the camera.
        int ball = metaballOf(gl_InstanceIndex);
        vec4 centre = metaball(ball, 0);
        int first = int(metaball(ball, 1).w + 0.5);
        int count = max(int(metaball(ball, 2).x + 0.5), 1);
        int local = gl_InstanceIndex - first;
        float y = 1.0 - 2.0 * (float(local) + 0.5) / float(count);
        float around = 2.39996322973 * float(local);
        vec3 direction = metaballTurn(vec3(sqrt(max(0.0, 1.0 - y * y)) * cos(around), y,
                                           sqrt(max(0.0, 1.0 - y * y)) * sin(around)), ball);
        float t = local < count && gl_InstanceIndex < int(pc.points.x + 0.5)
                ? metaballRay(ball, direction) : -1.0;
        vec3 surface = centre.xyz + t * direction;
        vec3 normal, blend;
        float share = 0.0, leading = 1.0, keep = 0.0;
        if (t > 0.0) {
            metaballShade(ball, surface, normal, blend, share, leading);
            // Along its ray a ball's points spread as t^2 / cos: the chance
            // to stay that holds the rest density, times the ball's share.
            float slant = max(dot(normal, direction), 0.15);
            keep = share * t * t / (max(pc.motion.z, 1.0) * centre.w * centre.w * slant);
        }
        // A point fades in and out with its chance, never pops.
        float presence = clamp((keep - hashIndex(gl_InstanceIndex, 9u)) / 0.06, 0.0, 1.0);
        vec3 position = pc.shape.x * metaballSpin(surface);
        vec3 viewNormal = toView(metaballSpin(normal));
        bool hidden = t <= 0.0 || presence <= 0.0;
        if (!hidden && fx.view.x < 0.5) {
            hidden = viewNormal.z < -0.05
                  || metaballHidden(surface, metaballToCamera(), 0.1 * centre.w);
        }
        if (hidden) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            pointColor = vec3(0.0);
            pointFacing = 0.0;
            return;
        }
        vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
        float diffuse = abs(dot(viewNormal, light));
        float illumination = 0.12 + 0.88 * pow(diffuse, 0.8);
        // Stretched past what the spare points cover, they grow to match.
        float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                           * mix(0.36, 1.0, illumination) * sqrt(max(keep, 1.0))
                           * sqrt(presence);
        radiusPixels = max(radiusPixels, 0.30);
        int colorMode = int(pc.motion.x + 0.5);
        float height = smoothstep(-pc.shape.z, pc.shape.z, surface.y);
        vec3 tint = colorMode == 1 ? metaball(ball, 1).rgb
                  : colorMode == 2 ? mix(pc.style.rgb, pc.wave.rgb, height)
                                   : blend;
        // Where two balls meet the seam lights up.
        float seam = 1.0 - smoothstep(0.5, 0.95, leading);
        tint = tint * (1.0 + 1.5 * pc.look.w * seam) + 0.3 * pc.look.w * seam;
        pointColor = tint * illumination * pc.look.z;
        pointFacing = 1.0;

        vec4 clip = effectClip(position, viewNormal, radiusPixels);
        clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
        gl_Position = clip;
        disc *= (radiusPixels + 0.65) / radiusPixels;
        return;
    }

    if (shellMode()) {
        // The same pinwheel of frame strips on every shell (see nested cubes).
        int shells = shellCount();
        int perShell = max(int(pc.points.x + 0.5), 24);
        int shell = gl_InstanceIndex / perShell;
        int slot = gl_InstanceIndex % perShell;
        int perStrip = perShell / 24;
        int strip = slot / perStrip;
        int sampleIndex = slot % perStrip;
        float border = pc.shape.z;
        float stripLength = 2.0 - border;
        int rows = max(int(round(sqrt(float(perStrip) * border / stripLength))), 1);
        int columns = max(perStrip / rows, 1);
        int row = sampleIndex / columns, column = sampleIndex % columns;
        if (shell >= shells || strip >= 24 || row >= rows) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            pointColor = vec3(0.0);
            pointFacing = 0.0;
            return;
        }
        vec2 ab = vec2(-1.0 + stripLength * (float(column) + 0.5) / float(columns),
                       1.0 - border * (float(row) + 0.5) / float(rows));
        for (int turn = 0; turn < strip % 4; ++turn) {
            ab = vec2(-ab.y, ab.x);
        }
        vec3 local = roundedCubePoint(cubeFacePoint(strip / 4, ab));
        // When this point lets go within the hop: by chance or from the top
        // of its shell down, and the outer shells first with the cascade.
        float order = mix(hashIndex(slot, 6u),
                          0.5 - 0.5 * local.y / max(length(local), 1e-4), pc.loop.w);
        order = mix(order, float(shell) / float(shells - 1), pc.loop.z);
        float rest = clamp(pc.motion.x, 0.0, 0.9), spread = clamp(pc.motion.y, 0.0, 0.95);
        float hop = timePhase(pc.wave.w) / TAU;
        float leave = 0.5 * rest + spread * (1.0 - rest) * order;
        float flight = clamp((hop - leave) / max((1.0 - spread) * (1.0 - rest), 1e-3), 0.0, 1.0);
        float eased = flight * flight * (3.0 - 2.0 * flight);
        int next = (shell + 1) % shells;
        bool fountain = next == 0;
        vec3 from = shellSize(float(shell)) * turnShell(local, shell);
        vec3 to = shellSize(float(next)) * turnShell(local, next);
        float arc = sin(PI * flight);
        vec3 position = mix(from, to, eased);
        // Falling inward the points sag under their weight; the fountain
        // throws them up and over.
        position.y += fountain ? pc.loop.y * pc.shape.x * arc
                               : -pc.motion.z * shellSize(float(shell)) * arc;
        vec3 scatter = vec3(hashIndex(gl_InstanceIndex, 1u), hashIndex(gl_InstanceIndex, 2u),
                            hashIndex(gl_InstanceIndex, 3u)) * 2.0 - 1.0;
        position += pc.motion.w * shellSize(float(shell)) * arc * scatter;
        float level = mix(float(shell), fountain ? 0.0 : float(next), eased);
        vec3 normal = normalize(mix(turnShell(roundedCubeNormal(local), shell),
                                    turnShell(roundedCubeNormal(local), next), eased));
        vec3 viewNormal = toView(normal);
        vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
        float diffuse = abs(dot(viewNormal, light));
        float illumination = 0.10 + 0.90 * pow(diffuse, 0.8);
        // No skin hides the far side: it dims with depth instead.
        float span = shellSpan();
        float nearness = smoothstep(-span, span, toView(position).z);
        float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                           * mix(0.36, 1.0, illumination)
                           * clamp(pow(pc.shape.y, 0.5 * level), 0.35, 1.0);
        radiusPixels = max(radiusPixels, 0.30);
        int colorMode = int(pc.points.w + 0.5);
        vec3 tint = colorMode == 0 ? mix(shellPalette(shell), shellPalette(next), eased)
                  : colorMode == 1 ? mix(pc.style.rgb, pc.wave.rgb, level / float(shells - 1))
                                   : pc.style.rgb;
        // Points in flight burn brighter.
        tint = tint * (1.0 + 0.8 * arc) + 0.15 * arc;
        pointColor = tint * illumination * mix(0.45, 1.0, nearness) * pc.look.z;
        pointFacing = 1.0;

        vec4 clip = effectClip(position, viewNormal, radiusPixels);
        clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
        gl_Position = clip;
        disc *= (radiusPixels + 0.65) / radiusPixels;
        return;
    }

    if (fourSurfaceMode()) {
        // One grid of staggered rows per patch, in the patch's own (s, t).
        int columns = max(int(pc.points.x + 0.5), 1), rows = max(int(pc.points.y + 0.5), 1);
        int piece = gl_InstanceIndex / (columns * rows);
        int sampleIndex = gl_InstanceIndex % (columns * rows);
        int row = sampleIndex / columns, column = sampleIndex % columns;
        vec2 st = fourSurfaceFlow(vec2((float(column) + 0.25 + 0.5 * float(row % 2)) / float(columns),
                                       (float(row) + 0.5) / float(rows)));
        float tag, w, magnification;
        vec3 position = fourSurfacePoint(piece, st, tag, w, magnification);
        if (fourSurfaceTooClose(magnification)) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            pointColor = vec3(0.0);
            pointFacing = 0.0;
            return;
        }
        vec3 viewNormal = toView(fourSurfaceNormal(piece, st));
        vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
        // Both sides light up: the skin alone decides what is hidden.
        float diffuse = abs(dot(viewNormal, light));
        float illumination = 0.12 + 0.88 * pow(diffuse, 0.8);
        float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                           * mix(0.36, 1.0, illumination) * clamp(sqrt(magnification), 0.5, 2.0);
        radiusPixels = max(radiusPixels, 0.30);
        // Colour: 0 patches (palette), 1 near to far in 4D, 2 patches shaded
        // by 4D depth.
        int colorMode = int(pc.points.w + 0.5);
        float depth = smoothstep(-0.9, 0.9, w);
        vec3 tint = colorMode == 1 ? mix(pc.wave.rgb, pc.style.rgb, depth)
                  : colorMode == 2 ? fourSurfacePalette(tag) * mix(0.35, 1.25, depth)
                                   : fourSurfacePalette(tag);
        // A light running along s, carried by the surface.
        float pulse = pow(0.5 + 0.5 * cos(TAU * polytopeData[5].y * st.x - timePhase(pc.motion.w)), 6.0);
        tint = tint * (1.0 + pc.loop.z * pulse) + 0.25 * pc.loop.z * pulse;
        radiusPixels *= 1.0 + 0.5 * pc.look.y * pc.loop.z * pulse;
        pointColor = tint * illumination * pc.look.z;
        pointFacing = 1.0;

        vec4 clip = effectClip(position, viewNormal, radiusPixels);
        clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
        gl_Position = clip;
        disc *= (radiusPixels + 0.65) / radiusPixels;
        return;
    }

    if (polytopeMode()) {
        int along = max(int(pc.points.x + 0.5), 1), around = max(int(pc.points.y + 0.5), 1);
        int edge = gl_InstanceIndex / (along * around);
        int sampleIndex = gl_InstanceIndex % (along * around);
        int row = sampleIndex / around, column = sampleIndex % around;
        float s = (float(row) + 0.5) / float(along);
        float v = TAU * (float(column) + 0.5 * float(row % 2)) / float(around);
        vec3 center, normal, binormal;
        float w;
        polytopeFrame(edge, s, center, normal, binormal, w);
        float presence = polytopePresence(edge);
        if (polytopeTooClose(w) || presence < 0.02) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            pointColor = vec3(0.0);
            pointFacing = 0.0;
            return;
        }
        float magnification = polytopeScale(w);
        vec3 outward = normal * cos(v) + binormal * sin(v);
        // A fading edge thins down to its centre line and dims.
        vec3 position = center + pc.shape.y * magnification * presence * outward;
        vec3 viewNormal = toView(outward);
        vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
        float diffuse = max(dot(viewNormal, light), 0.0);
        float illumination = 0.10 + 0.90 * pow(diffuse, 0.8);
        float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                           * mix(0.36, 1.0, illumination) * clamp(sqrt(magnification), polytopeHyperbolic() ? 0.12 : 0.5, 2.0);

        float glow;
        vec3 tint = polytopeTint(edge, s, w, glow);
        radiusPixels *= 1.0 + 0.6 * pc.motion.z * glow;
        radiusPixels = max(radiusPixels * sqrt(presence), 0.30);
        pointColor = tint * illumination * pc.look.z * presence;
        pointFacing = smoothstep(-0.015, 0.025, viewNormal.z);

        vec4 clip = effectClip(position, viewNormal, radiusPixels);
        clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
        gl_Position = clip;
        disc *= (radiusPixels + 0.65) / radiusPixels;
        return;
    }

    if (tesseractMode()) {
        int along = max(int(pc.points.x + 0.5), 1), around = max(int(pc.points.y + 0.5), 1);
        int edge = gl_InstanceIndex / (along * around);
        int sampleIndex = gl_InstanceIndex % (along * around);
        int row = sampleIndex / around, column = sampleIndex % around;
        float s = (float(row) + 0.5) / float(along);
        float v = TAU * (float(column) + 0.5 * float(row % 2)) / float(around);
        vec3 outward;
        float w;
        vec3 position = tesseractTubePoint(edge, s, v, outward, w);
        vec3 viewNormal = toView(outward);
        vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
        float diffuse = max(dot(viewNormal, light), 0.0);
        float illumination = 0.10 + 0.90 * pow(diffuse, 0.8);
        float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                           * mix(0.36, 1.0, illumination) * sqrt(tesseractScale(w));
        radiusPixels = max(radiusPixels, 0.30);
        // Near cell in one colour, far cell in the other; lights run along the
        // edges in the direction of their axis.
        vec3 tint = mix(pc.motion.rgb, pc.wave.rgb, smoothstep(-1.6, 1.6, w));
        float pulse = pow(0.5 + 0.5 * cos(TAU * s - timePhase(pc.motion.w)), 10.0);
        tint = tint * (1.0 + pc.loop.y * pulse) + 0.3 * pc.loop.y * pulse;
        pointColor = tint * illumination * pc.look.z;
        pointFacing = smoothstep(-0.015, 0.025, viewNormal.z);

        vec4 clip = effectClip(position, viewNormal, radiusPixels);
        clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
        gl_Position = clip;
        disc *= (radiusPixels + 0.65) / radiusPixels;
        return;
    }

    if (hopfMode()) {
        // One draw per torus: fibres of staggered rows around a thin tube.
        int along = max(int(pc.points.x + 0.5), 1), around = max(int(pc.points.y + 0.5), 1);
        int fiber = gl_InstanceIndex / (along * around);
        int sampleIndex = gl_InstanceIndex % (along * around);
        int row = sampleIndex / around, column = sampleIndex % around;
        float t = 2.0 * TAU * (float(row) + 0.5 * float(column % 2)) / float(along);
        float v = TAU * float(column) / float(around);
        float torus = pc.motion.y;
        vec3 center, normal, binormal;
        hopfFrame(t, torus, float(fiber), center, normal, binormal);
        if (length(center) > hopfCutoff()) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            pointColor = vec3(0.0);
            pointFacing = 0.0;
            return;
        }
        vec3 outward = normal * cos(v) + binormal * sin(v);
        vec3 position = center + pc.shape.y * outward;
        vec3 viewNormal = toView(outward);
        vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
        float diffuse = max(dot(viewNormal, light), 0.0);
        float illumination = 0.10 + 0.90 * pow(diffuse, 0.8);
        float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                           * mix(0.36, 1.0, illumination);
        radiusPixels = max(radiusPixels, 0.30);
        // A light runs along every fibre, carried by the material.
        float pulse = pow(0.5 + 0.5 * cos(0.5 * t - timePhase(pc.wave.z)), 8.0);
        vec3 tint = pc.style.x >= 0.0 ? pc.style.rgb : vec3(0.55, 0.72, 1.0);
        pointColor = (tint * (0.40 + 0.85 * pulse) + 0.25 * pulse) * illumination * pc.look.z;
        pointFacing = smoothstep(-0.015, 0.025, viewNormal.z);

        vec4 clip = effectClip(position, viewNormal, radiusPixels);
        clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
        gl_Position = clip;
        disc *= (radiusPixels + 0.65) / radiusPixels;
        return;
    }

    if (archMode()) {
        vec3 position, normal;
        float pattern;
        if (pc.motion.w > 0.5) {
            // Arches: staggered rows that travel from the north foot to the south.
            int rows = int(pc.points.y);
            int columns = max(int(fract(pc.points.y) * 128.0 + 0.5), 1);
            int perArch = rows * columns;
            int arch = gl_InstanceIndex / perArch;
            int sampleIndex = gl_InstanceIndex % perArch;
            int row = sampleIndex / columns, column = sampleIndex % columns;
            float material = (float(row) + 0.5 * float(column % 2)) / float(rows);
            float along = fract(material + archFlow());
            float around = TAU * float(column) / float(columns);
            position = archPoint(along, around, float(arch));
            normal = archNormal(along, around, float(arch));
            pattern = archPattern(fract(material), 2.0 * around / PI);
        } else {
            // Sphere: a closed equal-area lattice that climbs from pole to pole.
            int spherePoints = max(int(pc.points.x + 0.5), 1);
            float material = (float(gl_InstanceIndex) + 0.5) / float(spherePoints);
            uint turned = uint(gl_InstanceIndex) * uint(pc.points.w + 0.5) % uint(spherePoints);
            float longitude = TAU * float(turned) / float(spherePoints);
            normal = archSphereDirection(fract(material + archFlow()), longitude);
            position = pc.shape.x * normal;
            pattern = archPattern(material, pc.loop.z * longitude / TAU);
        }
        normal = toView(normal);
        vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
        float diffuse = max(dot(normal, light), 0.0);
        float illumination = 0.10 + 0.90 * pow(diffuse, 0.8);
        float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                           * mix(0.36, 1.0, illumination);
        radiusPixels = max(radiusPixels, 0.30);
        pointColor = (pattern < 0.5 ? pc.wave.rgb : pc.motion.rgb) * illumination * pc.look.z;
        pointFacing = smoothstep(-0.015, 0.025, normal.z);

        vec4 clip = effectClip(position, normal, radiusPixels);
        clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
        gl_Position = clip;
        disc *= (radiusPixels + 0.65) / radiusPixels;
        return;
    }

    if (tentacleMode()) {
        const float goldenAngle = 2.39996322973;
        int index;
        float angle, turn;
        if (pc.motion.w > 0.5) {
            // Tentacles: a sunflower of equal-area samples around each axis.
            int perTentacle = max(int(pc.points.y + 0.5), 1);
            int sampleIndex = gl_InstanceIndex % perTentacle;
            index = gl_InstanceIndex / perTentacle;
            angle = tentacleAreaAngle((float(sampleIndex) + 0.5) / float(perTentacle));
            turn = goldenAngle * float(sampleIndex);
        } else {
            // Body: a Fibonacci sphere, minus the patches the tentacles cover.
            int bodyPoints = max(int(pc.points.x + 0.5), 1);
            float y = 1.0 - 2.0 * (float(gl_InstanceIndex) + 0.5) / float(bodyPoints);
            float radial = sqrt(max(0.0, 1.0 - y * y));
            float longitude = goldenAngle * float(gl_InstanceIndex);
            tentacleNearest(vec3(radial * cos(longitude), y, radial * sin(longitude)),
                            index, angle, turn);
            if (angle < tentaclePatchAngle()) {
                gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
                pointColor = vec3(0.0);
                pointFacing = 0.0;
                return;
            }
        }
        vec3 position = tentacleSurface(index, angle, turn);
        vec3 normal = toView(tentacleNormal(index, angle, turn));
        vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
        float diffuse = max(dot(normal, light), 0.0);
        float illumination = 0.10 + 0.90 * pow(diffuse, 0.8);
        float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                           * mix(0.36, 1.0, illumination);
        radiusPixels = max(radiusPixels, 0.30);
        float tipward = tentacleHeight(angle) / max(pc.shape.y, 1e-6);
        vec3 tint = mix(pc.motion.rgb, pc.wave.rgb, smoothstep(0.05, 0.95, tipward));
        // Troughs fall into shade so the travelling ripples read clearly.
        tint *= mix(0.62, 1.0, 0.5 + 0.5 * tentacleRipple(angle));
        pointColor = tint * illumination * pc.look.z;
        pointFacing = smoothstep(-0.015, 0.025, normal.z);

        vec4 clip = effectClip(position, normal, radiusPixels);
        clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
        gl_Position = clip;
        disc *= (radiusPixels + 0.65) / radiusPixels;
        return;
    }

    if (eversionMode() || sleeveMode() || morphMode() || ridgeMode() || smaleMode()) {
        int pointCount = max(int(pc.points.x + 0.5), 1);
        // Equal-area samples of the cloth. Each point keeps its material coordinates.
        float material = (float(gl_InstanceIndex) + 0.5) / float(pointCount);
        // The golden-angle turn in whole numbers (2^32 / golden ratio), so the
        // angle stays small and exact however many points there are; a float
        // product would be too coarse for the normals taken by differences.
        float longitude = TAU * float(uint(gl_InstanceIndex) * 2654435769u) / 4294967296.0;
        if (pc.loop.z > 0.5) {
            // A whole-number step returns to the first point after the last one,
            // so the lattice has no seam where the material wraps around.
            uint turned = uint(gl_InstanceIndex) * uint(pc.loop.z + 0.5) % uint(pointCount);
            longitude = TAU * float(turned) / float(pointCount);
        }
        vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
        vec3 position, normal, tint;
        float diffuse;
        if (smaleMode()) {
            // A Fibonacci sphere: equal areas while the sphere is round.
            float latitude = asin(clamp(1.0 - 2.0 * material, -1.0, 1.0));
            position = smalePoint(latitude, longitude);
            normal = toView(smaleNormal(latitude, longitude));
            diffuse = abs(dot(normal, light));
            // Each side of the material keeps its colour through the eversion.
            tint = normal.z > 0.0 ? pc.wave.rgb : pc.motion.rgb;
        } else if (ridgeMode()) {
            float angle = vortexAngle(fract(material + timePhase(1.0) / TAU));
            int link = int(pc.style.x + 0.5);
            position = ridgeLinkPoint(ridgePoint(longitude, angle), link);
            normal = toView(ridgeLinkTurn(ridgeNormal(longitude, angle), link));
            diffuse = max(dot(normal, light), 0.0);
            // The ridges take their colour as they rise out of the body.
            tint = mix(pc.motion.rgb, pc.wave.rgb,
                       ridgeCrest(longitude, material) * ridgeGrowth());
        } else if (morphMode()) {
            vec4 section = morphProfile(material, true);
            position = sleeveAround(longitude, section.xy);
            normal = toView(sleeveAround(longitude, section.zw));
            diffuse = max(dot(normal, light), 0.0);
            tint = fract(material * pc.shape.w) < 0.5 ? pc.wave.rgb : pc.motion.rgb;
        } else if (sleeveMode()) {
            position = sleevePoint(longitude, material);
            normal = toView(sleeveNormal(longitude, material));
            diffuse = max(dot(normal, light), 0.0);
            // Bands of material: the sleeve shows another one as it turns out.
            tint = fract(material * pc.shape.w) < 0.5 ? pc.wave.rgb : pc.motion.rgb;
        } else {
            position = eversionPoint(longitude, material);
            normal = toView(eversionNormal(longitude, material));
            diffuse = abs(dot(normal, light));
            // Both sides stay visible; the colour tells which one faces the camera.
            tint = normal.z > 0.0 ? pc.wave.rgb : pc.motion.rgb;
        }
        float illumination = 0.10 + 0.90 * pow(diffuse, 0.8);
        float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                           * mix(0.36, 1.0, illumination);
        radiusPixels = max(radiusPixels, 0.30);
        pointColor = tint * illumination * pc.look.z;
        // The punctured and the everting sphere show both sides of their cloth. The
        // other bodies stay closed, so points turned away would only bleed past
        // the silhouette.
        pointFacing = eversionMode() || smaleMode() ? 1.0 : smoothstep(-0.015, 0.025, normal.z);

        vec4 clip = effectClip(position, normal, radiusPixels);
        clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
        gl_Position = clip;
        disc *= (radiusPixels + 0.65) / radiusPixels;
        return;
    }

    int rows = int(pc.points.x), columns = int(pc.points.y);
    int strand = gl_InstanceIndex / (rows * columns);
    int pointIndex = gl_InstanceIndex % (rows * columns);
    int row = pointIndex / columns, column = pointIndex % columns;
    // Fixed IDs and staggered material coordinates: no per-frame re-sampling.
    float u = TAU * (float(row) + 0.5 * float(column % 2)) / float(rows);
    float v = TAU * float(column) / float(columns);
    vec3 position = surfacePoint(u, v, float(strand));
    vec3 normal = toView(surfaceNormal(u, v, float(strand)));
    vec3 light = normalize(vec3(-0.65, 0.8, 1.15));
    float diffuse = max(dot(normal, light), 0.0);
    float illumination = 0.10 + 0.90 * pow(diffuse, 0.8);
    // Radius changes with light, never with the local UV stretch or orientation.
    float radiusPixels = pc.points.z * min(pc.view.x, pc.view.y) / 700.0
                       * mix(0.36, 1.0, illumination);
    radiusPixels = max(radiusPixels, 0.30);
    vec3 tint = mix(vec3(0.55, 0.43, 0.58), vec3(1.0, 0.87, 0.77), diffuse);
    if (pc.style.w > 2.5) {
        tint = mix(vec3(0.38, 0.48, 0.88), vec3(0.86, 1.0, 0.94), diffuse);
    }
    if (vortexMode()) {
        // Strands change colour on their way from the rim into the hole.
        float inward = 0.5 - 0.5 * cos(vortexStrandAngle(u, float(strand)));
        tint = mix(pc.motion.rgb, pc.wave.rgb, smoothstep(0.15, 0.85, inward));
    }
    if (pc.style.x >= 0.0) {
        tint = pc.style.rgb;
    }
    if (tubeRidges()) {
        // Valleys fall into shade so the travelling crests read clearly.
        tint *= mix(1.0 - 0.6 * pc.loop.y, 1.0, tubeRidge(u, v));
    }
    pointColor = tint * illumination * pc.look.z;
    pointFacing = smoothstep(-0.015, 0.025, normal.z);

    vec4 clip = effectClip(position, normal, radiusPixels);
    clip.xy += disc * (radiusPixels + 0.65) * 2.0 / pc.view.xy;
    gl_Position = clip;
    // Keep the antialias fringe one physical pixel wide at every viewport size.
    disc *= (radiusPixels + 0.65) / radiusPixels;
}
