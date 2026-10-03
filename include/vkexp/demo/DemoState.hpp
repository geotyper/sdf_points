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
    int geometryMode{0}; // 0..3: braid variants, 4: nested spheres, 5: punctured sphere, 6: sleeve, 7: morphing sleeve, 8: vortex ring, 9: ridged torus
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
        palette.enabled = braid.geometryMode == 4;
    }

    Preset preset;
    RenderViewport viewport;
    ComputeOutput blur;
    BraidSettings braid;
    NestedSphereSettings nestedSpheres;
    EversionSettings eversion;
    SleeveSettings sleeve;
    MorphSettings morph;
    VortexSettings vortex;
    RidgedTorusSettings ridgedTorus;
    StrandPalette palette;
    LoopSettings loop;
};

[[nodiscard]] inline LoopInputs loopInputs(const DemoState& state) {
    LoopInputs inputs;
    inputs.geometryMode = state.braid.geometryMode;
    inputs.driver = state.loop.driver;
    inputs.cycles = state.loop.cycles;
    if (state.braid.geometryMode == 4) {
        inputs.paused = state.nestedSpheres.paused;
        inputs.animationSpeed = state.nestedSpheres.animationSpeed;
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
