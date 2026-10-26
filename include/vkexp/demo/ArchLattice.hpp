#pragma once

// Arched sphere sampling: the arches' rows and columns of points are sized to
// the density of the sphere's own lattice. Pure logic (no Vulkan) so it can be
// unit tested. The shader builds the same centreline.

#include <algorithm>
#include <cmath>

namespace vkexp {

struct ArchLattice {
    int rows{};    // along an arch
    int columns{}; // around its tube, always even for the staggered pattern
};

// Angle from the pole at which the arches meet the sphere: far enough for
// their tubes to stand side by side around it.
[[nodiscard]] inline double archFootAngle(const double radius, const double tubeRadius,
                                          const int arches) {
    constexpr double pi = 3.14159265358979323846;
    return std::asin(std::clamp(2.2 * arches * tubeRadius / (pi * radius), 0.12, 0.9));
}

// Length of one arch's centreline, from its foot near the north pole out over
// the equator to its foot near the south pole.
[[nodiscard]] inline double archLength(const double radius, const double height,
                                       const double tubeRadius, const int arches,
                                       const double twist) {
    constexpr double pi = 3.14159265358979323846;
    constexpr int segments = 256;
    const double foot = archFootAngle(radius, tubeRadius, arches);
    const auto centre = [&](const double along, double point[3]) {
        const double travel = along * along * (3.0 - 2.0 * along);
        const double colatitude = foot + (pi - 2.0 * foot) * travel;
        const double longitude = 2.0 * pi * twist * travel;
        const double reach = radius + height * std::sin(pi * along);
        point[0] = reach * std::sin(colatitude) * std::cos(longitude);
        point[1] = reach * std::cos(colatitude);
        point[2] = reach * std::sin(colatitude) * std::sin(longitude);
    };
    double length = 0.0;
    double previous[3]{};
    centre(0.0, previous);
    for (int segment = 1; segment <= segments; ++segment) {
        double current[3]{};
        centre(static_cast<double>(segment) / segments, current);
        length += std::hypot(current[0] - previous[0], current[1] - previous[1],
                             current[2] - previous[2]);
        std::copy(current, current + 3, previous);
    }
    return length;
}

// Rows and columns that give an arch the same point spacing as `spherePoints`
// spread over the sphere.
[[nodiscard]] inline ArchLattice archLattice(const int spherePoints, const double radius,
                                             const double height, const double tubeRadius,
                                             const int arches, const double twist) {
    constexpr double pi = 3.14159265358979323846;
    const double spacing = std::sqrt(4.0 * pi * radius * radius / std::max(spherePoints, 1));
    ArchLattice lattice;
    lattice.columns =
        std::clamp(2 * static_cast<int>(std::lround(pi * tubeRadius / spacing)), 6, 64);
    lattice.rows = std::clamp(
        static_cast<int>(std::lround(archLength(radius, height, tubeRadius, arches, twist) /
                                     spacing)),
        16, 720);
    return lattice;
}

} // namespace vkexp
