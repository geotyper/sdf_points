#pragma once

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
    int geometryMode{0}; // 0..3: braid variants, 4: nested spheres
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
    StrandPalette palette;
};

} // namespace vkexp
