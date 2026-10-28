#pragma once

#include "vkexp/demo/LoopPlan.hpp"
#include "vkexp/presets/Preset.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace vkexp {

struct RenderViewport {
    VkImage image{};
    VkImageView imageView{};
    VkSampler sampler{};
    VkExtent2D extent{960, 540};
    std::uint32_t requestedWidth{960};
    std::uint32_t requestedHeight{540};
    // Set while capturing: the target is rendered at this size instead of the panel size.
    std::optional<VkExtent2D> lockedExtent;
    std::uint64_t generation{};
};

struct ComputeOutput {
    VkImageView imageView{};
    VkSampler sampler{};
    VkExtent2D extent{};
    std::uint64_t generation{};
    bool requested{};
    bool ready{};
    int radius{4};
};

struct BraidSettings {
    int geometryMode{0}; // 0..3: braid variants, 4: nested spheres, 5: punctured sphere, 6: sleeve, 7: morphing sleeve, 8: vortex ring, 9: ridged torus, 10: ridged braid, 11: tentacle sphere, 12: bumpy torus, 13: torus chain, 14: sphere eversion, 15: arched sphere, 16: nested cubes, 17: Hopf fibration, 18: tesseract, 19: 4D polytopes, 20: 4D cross-sections, 21: quaternion Julia, 22: 4D surfaces
    bool paused{};
    bool autoRotate{false};
    bool limitTubeOverlap{false};
    float animationSpeed{0.42F};
    float rotationSpeed{0.05F};
    float majorRadius{1.23F};
    float weaveRadius{0.38F};
    float tubeRadius{0.195F};
    float radiusVariation{0.36F};
    float squareness{4.0F};
    float releaseStrength{0.78F};
    float releaseWidth{0.65F};
    float releaseSpeed{1.15F};
    float wholeLoopTorsion{0.85F};
    float torsionCompression{0.85F};
    float materialCirculation{0.18F};
    float pointSize{1.65F};
    float glow{0.15F};
    float brightness{1.15F};
    float tilt{0.10F};
    int twists{3};
    int strands{3};
    int majorPointCount{360};
    int minorPointCount{40};
};

struct NestedSphereSettings {
    bool paused{};
    bool offsetCenters{};
    float animationSpeed{0.38F};
    float speedVariation{0.65F};
    float centerOffset{0.45F};
    float outerRadius{1.35F};
    float minimumRadiusRatio{0.10F};
    float radiusCurve{1.0F};
    float holeAngle{0.27F};
    float pointSize{1.45F};
    float glow{0.18F};
    float brightness{1.20F};
    float tilt{0.18F};
    int sphereCount{6};
    int holeCount{12};
    int pointCount{12000};
    int directionSeed{1};
};

struct NestedCubeSettings {
    bool paused{};
    bool keepApart{true};
    // Infinite zoom: the cubes grow out past the camera and new ones are born inside.
    bool zoom{true};
    float zoomRate{4.0F};   // levels per flow cycle
    float zoomTwist{0.35F}; // turn between neighbouring levels, radians
    bool perspective{true};
    float cameraDistance{2.2F}; // in outer sizes
    // Without the zoom: every cube turns a whole number of times per cycle, so
    // they all line up at once, stand for a moment and burst apart.
    bool sync{false};
    float syncHold{0.6F};  // 0 .. 0.95: how long they linger aligned
    float syncFlash{0.8F}; // glow as they line up
    float animationSpeed{0.38F};
    float speedVariation{0.65F};
    float outerSize{0.80F};
    float sizeRatio{0.56F};
    float border{0.22F};    // frame width in face units: 1 fills the faces
    float roundness{0.0F};  // 0 cube .. 1 sphere
    float pointSize{1.45F};
    float glow{0.18F};
    float brightness{1.20F};
    float tilt{0.18F};
    int cubeCount{5};
    int pointCount{24000};  // points a solid outer cube would carry
    int directionSeed{1};
};

struct HopfSettings {
    float zoom{1.0F}; // camera: 1 frames the whole figure, more comes closer
    bool paused{};
    float animationSpeed{0.30F};
    float flow{2.0F};       // lights running along the fibres
    float drift{0.5F};      // fibres sliding to the next base point
    float spin{0.15F};
    float tumble{0.45F};    // 4D tilt, radians
    float tumbleRate{1.0F};
    float breathing{0.06F}; // latitude swing of every torus
    float scale{0.55F};
    float tubeRadius{0.022F};
    float innerLatitude{0.30F};
    float outerLatitude{0.85F};
    float pointSize{1.25F};
    float glow{0.18F};
    float brightness{1.25F};
    float tilt{0.45F};
    int tori{3};
    int fibers{14};
    float mobius{0.0F}; // Moebius flow: rapidity of the conformal push
    int knotP{1}; // fibres wind p times one way and q the other: (p, q) torus knots
    int knotQ{1};
    int pointsAlong{240};
    int pointsAround{8};
};

struct TesseractSettings {
    float zoom{1.0F}; // camera: 1 frames the whole figure, more comes closer
    bool paused{};
    float animationSpeed{0.30F};
    float turnXW{1.0F};  // turns through the fourth dimension per flow cycle
    float turnYZ{0.0F};  // ordinary turn added to it
    float spin{0.25F};
    float size{0.42F};
    float tubeRadius{0.030F};
    float cameraDistance{3.0F}; // 4D camera, in half-edges
    float lightFlow{2.0F};
    float lightStrength{0.6F};
    float pointSize{1.30F};
    float glow{0.18F};
    float brightness{1.20F};
    float tilt{0.35F};
    int pointsAlong{160};
    int pointsAround{10};
    std::array<float, 3> nearColor{1.00F, 0.62F, 0.16F};
    std::array<float, 3> farColor{0.14F, 0.58F, 1.00F};
};

// Regular 4D polytopes and compounds of them (see Polytope4D.hpp).
struct PolytopeSettings {
    float zoom{1.0F}; // camera: 1 frames the whole figure, more comes closer
    bool paused{};
    int shape{3}; // Polytope4D
    int duoprismP{6};
    int duoprismQ{12};
    int colorMode{0}; // 0 4D depth, 1 parts, 2 4D directions, 3 parts shaded by depth
    bool counterTurn{true}; // parts of a compound turn against each other
    // Curved edges: 0 straight, 1 great circles on the 3-sphere, beyond that
    // bulging out, below 0 caving in.
    float edgeBend{0.0F};
    float coil{0.0F};      // edges winding around their chord in 4D, share of edge length
    float coilTurns{3.0F};
    float coilFlow{1.0F};
    int coilWave{0}; // 0 steady, 1 waves along every edge, 2 fronts across the figure
    float coilWaveLength{0.5F};
    float coilWaveSpeed{1.0F};
    // The tubes under the points: 0 black, 1 the point colour a shade darker,
    // 2 that colour fading to black towards the rim.
    int tubes{0};
    float tubeDarkness{0.55F}; // 0 the point colour itself, 1 black
    // 4D noise displacing every point before the projection.
    bool noise{false};
    float noiseAmount{0.08F};
    float noiseScale{2.5F};
    float noiseSpeed{1.0F};
    float animationSpeed{0.30F};
    float turnXW{1.0F};
    float turnYZ{0.0F};
    float turnZW{0.0F};
    float spin{0.20F};
    float size{0.85F};
    float tubeRadius{0.018F};
    float cameraDistance{2.4F}; // in circumradii; 1 with bent edges is stereographic
    float slice{0.0F};          // glow of the cross-section a sweeping hyperplane cuts
    float sliceWidth{0.10F};
    float lightFlow{2.0F};
    float lightStrength{0.4F};
    float pointSize{1.20F};
    float glow{0.18F};
    float brightness{1.20F};
    float tilt{0.35F};
    int pointBudget{400000};
    int pointsAround{6};
    std::array<float, 3> nearColor{1.00F, 0.62F, 0.16F};
    std::array<float, 3> farColor{0.14F, 0.58F, 1.00F};
    // Quasicrystal: cluster radius in edge lengths, window scale (1 exact),
    // how softly vertices fade at the window, and the phason shift sliding the
    // window round a loop, which flips vertices in waves.
    float crystalRadius{3.2F};
    float crystalWindow{1.0F};
    float crystalSoftness{0.15F};
    float phasonShift{0.45F};
    float phasonSpeed{1.0F};
    // Algebraic knot z1^p = g(z2): g has `knotRoots` roots on a ring of
    // `knotSpread`, each wandering round its own circle of `knotWander`.
    int knotP{2};
    int knotRoots{3};
    float knotSpread{0.0F};
    float knotWander{0.30F};
    float knotSpeed{1.0F};
    float knotGain{1.0F};
    // Hyperbolic honeycomb: cube widths it flows through per loop of time.
    float hyperbolicFlow{1.0F};
    float higherTurn{0.5F};     // 5-cube and 6-cube: turns into the extra dimensions
    float higherDistance{2.5F}; // their camera, out along every extra axis
    float mobius{0.0F}; // Moebius flow: rapidity of the conformal push
    float mobiusRate{1.0F};
};

// 3D cross-sections of solid 4D bodies: the hyperplane sweeps and the body
// turns through it, so the section changes shape (sections4d.glsl).
struct SliceSettings {
    bool paused{};
    int body{5}; // 5-cell, tesseract, 16-cell, 24-cell, duocylinder, tiger, spheritorus, ditorus
    int colorMode{0}; // 0 cells, 1 depth in 4D, 2 cells shaded by depth
    float animationSpeed{0.25F};
    float sliceSweep{0.45F}; // how far the hyperplane swings through the body
    float sliceRate{1.0F};
    float sliceOffset{0.0F};
    float turnXW{1.0F};
    float turnYW{0.0F};
    float turnZW{0.0F};
    float spin{0.15F};
    float thickness{0.22F}; // tube radius of the rounded bodies
    float size{0.85F};
    int grid{220};     // rays across each side
    int crossings{4};  // surface crossings looked for along each ray
    float pointSize{1.0F};
    float glow{0.18F};
    float brightness{1.20F};
    float tilt{0.35F};
    float zoom{1.0F};
    std::array<float, 3> nearColor{1.00F, 0.62F, 0.16F};
    std::array<float, 3> farColor{0.14F, 0.58F, 1.00F};
};

// A 3D slice of the quaternion Julia set of z -> z^power + c, with c circling
// round a loop so the fractal swells and splits.
struct JuliaSettings {
    bool paused{};
    int power{2};
    int colorMode{0}; // 0 palette bands, 1 near to far
    std::array<float, 4> c{-0.291F, -0.399F, 0.339F, 0.437F};
    float orbit{0.06F}; // radius of c's loop
    float orbitRate{1.0F};
    float sliceW{0.0F};
    float turnXW{0.5F};
    float spin{0.15F};
    float animationSpeed{0.20F};
    float size{0.85F};
    int grid{200};
    float pointSize{0.9F};
    float glow{0.18F};
    float brightness{1.20F};
    float tilt{0.35F};
    float zoom{1.0F};
    std::array<float, 3> nearColor{1.00F, 0.62F, 0.16F};
    std::array<float, 3> farColor{0.14F, 0.58F, 1.00F};
};

// Surfaces living in 4D (FourSurfaces.hpp), turned there and projected; the
// points sit on the surface's own grid.
struct FourSurfaceSettings {
    bool paused{};
    int surface{0};    // FourSurface
    int power{5};      // Calabi-Yau: z1^n + z2^n = 1; Milnor: the knot's p
    int powerQ{3};     // Milnor: the knot's q
    int fibres{4};     // Milnor: sheets shown at once
    float reach{1.0F}; // Calabi-Yau and Milnor: how far each patch runs out
    float viewAngle{0.785F}; // the fixed ZW turn mixing the two imaginary parts
    float turnXW{0.0F};
    float turnYW{0.0F};
    float turnZW{1.0F};
    float cameraDistance{0.0F}; // 4D perspective; 0 is a flat projection
    float flow{0.0F}; // material sliding round the closed surfaces; Milnor: sheets turning
    float mobius{0.0F}; // Moebius flow: rapidity of the conformal push
    float mobiusRate{1.0F};
    float pulse{0.0F};
    float pulseRate{1.0F};
    int pulseWaves{3};
    int colorMode{0}; // 0 patches, 1 near to far in 4D, 2 patches shaded by 4D depth
    float spin{0.1F};
    float animationSpeed{0.25F};
    float size{0.75F};
    int pointBudget{500000};
    float pointSize{0.9F};
    float glow{0.18F};
    float brightness{1.20F};
    float tilt{0.35F};
    float zoom{1.0F};
    std::array<float, 3> nearColor{1.00F, 0.62F, 0.16F};
    std::array<float, 3> farColor{0.14F, 0.58F, 1.00F};
};

// What suits each surface: the Calabi-Yau turns its imaginary parts (Hanson's
// picture), the torus turns inside out through the 4D camera.
[[nodiscard]] inline FourSurfaceSettings fourSurfaceDefaults(const int surface) {
    FourSurfaceSettings settings;
    settings.surface = surface;
    if (surface == 1) {
        settings.turnXW = 0.5F;
        settings.turnZW = 0.0F;
        settings.turnYW = 1.0F;
        settings.flow = 1.0F;
        settings.colorMode = 2;
        settings.zoom = 1.2F;
    } else if (surface == 2) {
        settings.turnXW = 1.0F;
        settings.turnZW = 0.0F;
        settings.flow = 1.0F;
        settings.zoom = 1.3F;
    } else if (surface == 3) {
        settings.turnXW = 1.0F;
        settings.turnZW = 0.0F;
        settings.cameraDistance = 1.15F;
        settings.flow = 1.0F;
        settings.zoom = 0.5F;
    } else if (surface == 4) {
        // Trefoil sheets, seen stereographically: they turn round the knot
        // and sweep the whole 3-sphere.
        settings.power = 2;
        settings.powerQ = 3;
        settings.reach = 3.0F;
        settings.viewAngle = 0.0F;
        settings.turnZW = 0.0F;
        settings.cameraDistance = 1.0F;
        settings.flow = 0.5F;
        settings.zoom = 0.55F;
    }
    return settings;
}

// Trail of the 4D figures: fading copies drawn at earlier moments, so the
// projected motion paints its paths.
struct FourDTrailSettings {
    bool enabled{false};
    int copies{10};
    float spacing{0.02F}; // animation time between copies
    float fade{1.5F};     // brightness falls as (1 - age)^fade
    float shrink{0.5F};   // the oldest copy's point size, as a share
};

struct EversionSettings {
    bool paused{};
    float animationSpeed{0.50F};
    float endHold{0.60F};
    float spin{0.15F};
    float radius{1.25F};
    float holeAngle{0.35F};
    float pointSize{1.45F};
    float glow{0.18F};
    float brightness{1.20F};
    float tilt{0.45F};
    int pointCount{16000};
    std::array<float, 3> outerColor{1.00F, 0.62F, 0.16F};
    std::array<float, 3> innerColor{0.14F, 0.58F, 1.00F};
};

struct SleeveSettings {
    bool paused{};
    float animationSpeed{0.35F};
    float spin{0.0F};
    float outerRadius{0.72F};
    float halfLength{0.80F};
    float innerRatio{0.55F};
    float pointSize{1.45F};
    float glow{0.18F};
    float brightness{1.20F};
    float tilt{0.50F};
    int bands{1};
    int pointCount{24000};
    std::array<float, 3> firstColor{1.00F, 0.62F, 0.16F};
    std::array<float, 3> secondColor{0.14F, 0.58F, 1.00F};
};

struct MorphSettings {
    bool paused{};
    float animationSpeed{0.35F};
    float morphRate{0.50F};
    float morphAmount{1.0F};
    float spin{0.0F};
    float size{1.45F};
    float halfLength{1.20F};
    float holeRatio{0.55F};
    float roundness{6.0F};
    float pointSize{1.45F};
    float glow{0.18F};
    float brightness{1.20F};
    float tilt{0.50F};
    int bands{2};
    int pointCount{24000};
    std::array<float, 3> firstColor{1.00F, 0.62F, 0.16F};
    std::array<float, 3> secondColor{0.14F, 0.58F, 1.00F};
};

// Ridges carved into tubes: screw threads, or beads when there are none around.
struct TubeRidges {
    float depth{0.0F};
    float travelRate{1.5F};
    int around{2};
    int along{30};

    // Packed for the shader: depth, travel rate, around + (along + 32) / 128.
    [[nodiscard]] std::array<float, 3> packed() const {
        return {depth, travelRate,
                static_cast<float>(around) + static_cast<float>(along + 32) / 128.0F};
    }
};

struct RidgedBraidSettings {
    int braid{1}; // which braid variant (0..3) carries the ridges
    TubeRidges ridges{0.80F, 1.5F, 2, 30};
};

struct VortexSettings {
    bool paused{};
    float animationSpeed{0.30F};
    float spin{0.0F};
    float ringRadius{0.95F};
    float coilRadius{0.48F};
    float tubeRadius{0.14F};
    float twistWave{0.0F};
    float pointSize{1.55F};
    float glow{0.18F};
    float brightness{1.20F};
    float tilt{0.85F};
    int strands{6};
    int twist{6};
    int majorPointCount{300};
    int minorPointCount{26};
    std::array<float, 3> holeColor{1.00F, 0.62F, 0.16F};
    std::array<float, 3> rimColor{0.14F, 0.58F, 1.00F};
    TubeRidges ridges;
};

struct RidgedTorusSettings {
    bool paused{};
    float animationSpeed{0.30F};
    float pulseRate{1.0F};
    float pulseDepth{0.0F};
    float spin{0.0F};
    float ringRadius{0.95F};
    float tubeRadius{0.40F};
    float ridgeHeight{1.0F};
    float sharpness{1.6F};
    float twistWave{0.7F};
    float pointSize{1.45F};
    float glow{0.18F};
    float brightness{1.20F};
    float tilt{0.85F};
    int ridges{5};
    int twist{2};
    int pointCount{32000};
    std::array<float, 3> ridgeColor{1.00F, 0.62F, 0.16F};
    std::array<float, 3> bodyColor{0.14F, 0.58F, 1.00F};
};

struct TentacleSettings {
    bool paused{};
    float animationSpeed{0.35F};
    float spin{0.10F};
    float radius{0.62F};
    float length{0.75F};
    float width{0.13F}; // radians on the sphere
    float roundness{3.0F};
    float rippleHeight{0.035F};
    float ripples{6.0F};
    float swirl{0.6F};
    float sway{0.35F};
    float pointSize{1.45F};
    float glow{0.18F};
    float brightness{1.20F};
    float tilt{0.25F};
    int tentacles{12};
    int spherePoints{16000};
    std::array<float, 3> tipColor{1.00F, 0.62F, 0.16F};
    std::array<float, 3> bodyColor{0.14F, 0.58F, 1.00F};
};

struct BumpyTorusSettings {
    bool paused{};
    float animationSpeed{0.30F};
    float pulseRate{1.0F};
    float pulseDepth{0.0F};
    float spin{0.0F};
    float ringRadius{0.95F};
    float tubeRadius{0.40F};
    float bumpHeight{0.8F};
    float bumpSize{0.85F};
    float roundness{1.5F};
    float pointSize{1.35F};
    float glow{0.18F};
    float brightness{1.20F};
    float tilt{0.85F};
    int bumpsAround{8};
    int bumpsAlong{20};
    int rowShift{0};
    int pointCount{44000};
    std::array<float, 3> bumpColor{1.00F, 0.62F, 0.16F};
    std::array<float, 3> bodyColor{0.14F, 0.58F, 1.00F};
};

// Two bumpy tori linked like a chain: slimmer, so each fits the other's hole.
[[nodiscard]] inline BumpyTorusSettings torusChainDefaults() {
    BumpyTorusSettings settings;
    settings.ringRadius = 1.00F;
    settings.tubeRadius = 0.26F;
    settings.bumpHeight = 0.7F;
    settings.bumpsAlong = 28;
    settings.pointCount = 30000;
    settings.tilt = 0.55F;
    return settings;
}

// Thickest tube that still passes through the other link's hole with its bumps.
[[nodiscard]] inline float torusChainTubeLimit(const BumpyTorusSettings& settings) {
    return 0.46F * settings.ringRadius / (1.0F + 0.45F * settings.bumpHeight);
}

struct SphereEversionSettings {
    bool paused{};
    float animationSpeed{0.22F};
    float endHold{0.5F};
    float spin{0.0F};
    float radius{0.62F};
    float pointSize{1.30F};
    float glow{0.18F};
    float brightness{1.20F};
    float tilt{0.45F};
    int pointCount{40000};
    std::array<float, 3> firstColor{1.00F, 0.62F, 0.16F};
    std::array<float, 3> secondColor{0.14F, 0.58F, 1.00F};
};

struct ArchedSphereSettings {
    bool paused{};
    float animationSpeed{0.30F};
    float spin{0.05F};
    float radius{0.60F};
    float archHeight{0.55F};
    float tubeRadius{0.09F};
    float twist{0.25F};
    float pointSize{1.40F};
    float glow{0.18F};
    float brightness{1.20F};
    float tilt{0.35F};
    bool checker{true};
    int arches{6};
    int bands{8};
    int sectors{12};
    int spherePoints{16000};
    std::array<float, 3> firstColor{1.00F, 0.62F, 0.16F};
    std::array<float, 3> secondColor{0.14F, 0.58F, 1.00F};
};

// Effects that apply to the points of every geometry.
struct PointEffects {
    bool drift{};
    float driftAmount{0.03F};
    float driftScale{3.0F};
    float jitter{0.0F};
    float driftSpeed{1.0F};
    bool shed{};
    float shedShare{0.12F};
    float shedDistance{0.35F};
    float shedRate{1.0F};
    float shedFall{0.0F};
    // No hidden skin behind the points: they all show and overlap each other.
    bool seeThrough{};
    bool trails{};
    int trailCount{6};
    float trailSpacing{0.02F}; // in units of the scene's phase
    float trailFade{0.75F};
};

struct StrandPalette {
    bool enabled{false};
    std::array<std::array<float, 3>, 5> colors{{
        {1.00F, 0.20F, 0.13F}, // coral
        {1.00F, 0.65F, 0.12F}, // amber
        {0.16F, 0.90F, 0.52F}, // mint
        {0.12F, 0.56F, 1.00F}, // blue
        {0.70F, 0.25F, 1.00F}, // violet
    }};
};

struct LoopSettings {
    bool enabled{};
    int cycles{1};
    LoopDriver driver{LoopDriver::Wave};
    // Set by CaptureModule while a loop is recorded: the animation then advances
    // by exact per-frame steps, independent of frame time and the speed sliders.
    struct Recording {
        double phaseCycles{};
        double phaseStep{};
        double rotationStep{};
    };
    std::optional<Recording> recording;
};

struct DemoState {
    explicit DemoState(Preset selectedPreset) : preset(std::move(selectedPreset)) {
        braid.geometryMode = preset.initialGeometryMode;
        palette.enabled =
            braid.geometryMode == 4 || braid.geometryMode >= 16;
    }

    Preset preset;
    RenderViewport viewport;
    ComputeOutput blur;
    BraidSettings braid;
    NestedSphereSettings nestedSpheres;
    NestedCubeSettings nestedCubes;
    HopfSettings hopf;
    TesseractSettings tesseract;
    PolytopeSettings polytope;
    FourDTrailSettings fourDTrail;
    SliceSettings slice;
    JuliaSettings julia;
    FourSurfaceSettings fourSurface;
    EversionSettings eversion;
    SleeveSettings sleeve;
    MorphSettings morph;
    VortexSettings vortex;
    RidgedTorusSettings ridgedTorus;
    RidgedBraidSettings ridgedBraid;
    TentacleSettings tentacle;
    BumpyTorusSettings bumpyTorus;
    BumpyTorusSettings torusChain{torusChainDefaults()};
    SphereEversionSettings sphereEversion;
    ArchedSphereSettings archedSphere;
    PointEffects effects;
    StrandPalette palette;
    LoopSettings loop;
};

// The braid variant in charge of the motion: a ridged braid moves like the
// braid it is carved into. Other geometries stand for themselves.
[[nodiscard]] inline int motionGeometryMode(const DemoState& state) {
    return state.braid.geometryMode == 10 ? state.ridgedBraid.braid : state.braid.geometryMode;
}

[[nodiscard]] inline LoopInputs loopInputs(const DemoState& state) {
    LoopInputs inputs;
    inputs.geometryMode = motionGeometryMode(state);
    inputs.driver = state.loop.driver;
    inputs.cycles = state.loop.cycles;
    if (state.braid.geometryMode == 4) {
        inputs.paused = state.nestedSpheres.paused;
        inputs.animationSpeed = state.nestedSpheres.animationSpeed;
    } else if (state.braid.geometryMode == 16) {
        inputs.paused = state.nestedCubes.paused;
        inputs.animationSpeed = state.nestedCubes.animationSpeed;
    } else if (state.braid.geometryMode == 20) {
        inputs.paused = state.slice.paused;
        inputs.animationSpeed = state.slice.animationSpeed;
    } else if (state.braid.geometryMode == 21) {
        inputs.paused = state.julia.paused;
        inputs.animationSpeed = state.julia.animationSpeed;
    } else if (state.braid.geometryMode == 22) {
        inputs.paused = state.fourSurface.paused;
        inputs.animationSpeed = state.fourSurface.animationSpeed;
    } else if (state.braid.geometryMode == 19) {
        inputs.paused = state.polytope.paused;
        inputs.animationSpeed = state.polytope.animationSpeed;
    } else if (state.braid.geometryMode == 18) {
        inputs.paused = state.tesseract.paused;
        inputs.animationSpeed = state.tesseract.animationSpeed;
    } else if (state.braid.geometryMode == 17) {
        inputs.paused = state.hopf.paused;
        inputs.animationSpeed = state.hopf.animationSpeed;
    } else if (state.braid.geometryMode == 5) {
        inputs.paused = state.eversion.paused;
        inputs.animationSpeed = state.eversion.animationSpeed;
    } else if (state.braid.geometryMode == 6) {
        inputs.paused = state.sleeve.paused;
        inputs.animationSpeed = state.sleeve.animationSpeed;
    } else if (state.braid.geometryMode == 7) {
        inputs.paused = state.morph.paused;
        inputs.animationSpeed = state.morph.animationSpeed;
    } else if (state.braid.geometryMode == 8) {
        inputs.paused = state.vortex.paused;
        inputs.animationSpeed = state.vortex.animationSpeed;
    } else if (state.braid.geometryMode == 9) {
        inputs.paused = state.ridgedTorus.paused;
        inputs.animationSpeed = state.ridgedTorus.animationSpeed;
    } else if (state.braid.geometryMode == 11) {
        inputs.paused = state.tentacle.paused;
        inputs.animationSpeed = state.tentacle.animationSpeed;
    } else if (state.braid.geometryMode == 12) {
        inputs.paused = state.bumpyTorus.paused;
        inputs.animationSpeed = state.bumpyTorus.animationSpeed;
    } else if (state.braid.geometryMode == 13) {
        inputs.paused = state.torusChain.paused;
        inputs.animationSpeed = state.torusChain.animationSpeed;
    } else if (state.braid.geometryMode == 14) {
        inputs.paused = state.sphereEversion.paused;
        inputs.animationSpeed = state.sphereEversion.animationSpeed;
    } else if (state.braid.geometryMode == 15) {
        inputs.paused = state.archedSphere.paused;
        inputs.animationSpeed = state.archedSphere.animationSpeed;
    } else {
        inputs.paused = state.braid.paused;
        inputs.animationSpeed = state.braid.animationSpeed;
        inputs.releaseSpeed = state.braid.releaseSpeed;
        inputs.autoRotate = state.braid.autoRotate;
        inputs.rotationSpeed = state.braid.rotationSpeed;
    }
    return inputs;
}

} // namespace vkexp
