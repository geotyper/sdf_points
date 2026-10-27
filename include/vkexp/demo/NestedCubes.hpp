#pragma once

// Nested cubes: sizes and point counts of the frames. Pure logic (no Vulkan)
// so it can be unit tested. The shader builds the same rounded bodies.

#include <algorithm>
#include <cmath>

namespace vkexp {

// Exponent of the superellipsoid |x|^n + |y|^n + |z|^n = 1: 40 at roundness 0
// (all but a cube), 2 at roundness 1 (the sphere inscribed in it).
[[nodiscard]] inline double cubeExponent(const double roundness) {
    return 2.0 / (0.05 + 0.95 * std::clamp(roundness, 0.0, 1.0));
}

// Largest size ratio at which the next cube in, turned any way at all, still
// fits inside this one: its far corner against this one's face centre.
[[nodiscard]] inline double nestedCubeRatioLimit(const double roundness) {
    return std::pow(3.0, 1.0 / cubeExponent(roundness)) / std::sqrt(3.0);
}

// Size ratio between neighbouring cubes, held below the limit when they are
// kept apart, with a margin for the points' own radius.
[[nodiscard]] inline double nestedCubeRatio(const double ratio, const double roundness,
                                            const bool keepApart) {
    return keepApart ? std::min(ratio, 0.97 * nestedCubeRatioLimit(roundness)) : ratio;
}

// Points on one cube's frames. `solidPoints` would cover the whole outer cube;
// a frame of `border` face units takes its share of every face, and smaller
// cubes take fewer, so all cubes keep the same spacing. Always a multiple of
// 24: six faces of four equal strips.
[[nodiscard]] inline int nestedCubePoints(const int solidPoints, const double border,
                                          const double sizeRatio) {
    const double width = std::clamp(border, 0.0, 1.0);
    const double share = width * (2.0 - width) * sizeRatio * sizeRatio;
    return 24 * std::max(1, static_cast<int>(std::lround(solidPoints * share / 24.0)));
}

// How far the zoom has carried every cube out of its slot, 0 .. 1 levels: the
// same rounding as the shader's timePhase, so a loop ends where it began.
[[nodiscard]] inline double cubeZoomOffset(const double phase, const double rate,
                                           const double loopCycles) {
    constexpr double tau = 6.28318530717958647692;
    const double rounded = loopCycles > 0.0 ? std::round(rate * loopCycles) / loopCycles : rate;
    const double wrapped = std::fmod(phase * rounded, tau);
    return (wrapped < 0.0 ? wrapped + tau : wrapped) / tau;
}

} // namespace vkexp
