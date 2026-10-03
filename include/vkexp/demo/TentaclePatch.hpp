#pragma once

// Tentacle sphere sampling. Each tentacle is drawn with its own lattice of
// points over a round patch of the sphere, and the rest of the sphere with
// another; this sizes the patch lattice so both have the same density.
// Pure logic (no Vulkan) so it can be unit tested.

#include <algorithm>
#include <cmath>

namespace vkexp {

// Patch radius in tentacle widths; the shader uses the same factor.
inline constexpr double tentaclePatchWidths = 2.2;

// Widest tentacle (radians) whose patches still fit side by side on the sphere.
[[nodiscard]] inline float tentacleWidthLimit(const int tentacles) {
    return static_cast<float>(0.72 / std::sqrt(static_cast<double>(std::max(tentacles, 1))));
}

// Area of one tentacle's patch divided by the area of the whole sphere. The
// patch is a surface of revolution whose distance from the centre is
// radius + length * exp(-(angle / width)^roundness) at `angle` from its axis.
[[nodiscard]] inline double tentaclePatchAreaShare(const double radius, const double length,
                                                   const double width, const double roundness) {
    constexpr int segments = 512;
    const double patch = tentaclePatchWidths * width;
    const auto section = [&](const double angle, double& distance, double& height) {
        const double reach = radius + length * std::exp(-std::pow(angle / width, roundness));
        distance = reach * std::sin(angle);
        height = reach * std::cos(angle);
    };
    double area = 0.0; // divided by 2*pi
    double previousDistance = 0.0;
    double previousHeight = 0.0;
    section(0.0, previousDistance, previousHeight);
    for (int segment = 1; segment <= segments; ++segment) {
        double distance = 0.0;
        double height = 0.0;
        section(patch * segment / segments, distance, height);
        area += 0.5 * (previousDistance + distance) *
                std::hypot(distance - previousDistance, height - previousHeight);
        previousDistance = distance;
        previousHeight = height;
    }
    return area / (2.0 * radius * radius);
}

// Points per tentacle patch that match the density of `spherePoints` spread
// over the whole sphere.
[[nodiscard]] inline int tentaclePatchPoints(const int spherePoints, const double radius,
                                             const double length, const double width,
                                             const double roundness) {
    const double points =
        spherePoints * tentaclePatchAreaShare(radius, length, width, roundness);
    return std::clamp(static_cast<int>(std::lround(points)), 16, 20000);
}

} // namespace vkexp
